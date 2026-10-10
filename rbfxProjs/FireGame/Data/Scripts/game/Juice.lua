-- Scripts/game/Juice.lua
--
-- 打击感三件套：hit-stop（击杀瞬间世界时间近乎冻结）、震屏（trauma 模型）、
-- 击杀飘字（世界坐标锚定的 +分数 弹字）。
--
-- 设计要点：
--   * 触发点唯一：main.lua 的 OnScore（玩家子弹与爆桶连锁的击杀都汇入那里），
--     juice:OnKill(unit, score) 一个入口覆盖全部击杀路径，Shooting/Waves/Enemy 零改动。
--   * hit-stop 只缩放“世界 dt”（船/海盗/子弹/波次/海面滚动的 Lua 逻辑），不碰引擎
--     动画——引擎骨骼/粒子走自己的 Frame::timeStep（Lua 改不了），结果恰好是想要
--     的效果：世界停了、死亡动画和爆炸粒子照常播完，定格里带着“死因演出”。
--   * 震屏用 trauma 模型（GDC "Juice it or lose it" 的标准做法）：抖幅 = trauma²，
--     线性衰减，多次击杀叠加封顶 1。相机基础姿态由 Player:UpdateCamera 每帧摆好，
--     Juice 在 EndFrame 阶段（player:Update 之后）追加位置偏移 + 绕视轴 Roll，
--     不侵入 Player.lua。
--   * 飘字是预建池：N 个 LuaRmlUIComponent 各开一份 ScorePop.rml（Knob.rml 同款
--     独立文档模式，body 必须 position:absolute 否则 SetPosition 被 static 布局
--     静默丢弃）。击杀时轮询取槽改文本，逐帧把锚点世界坐标经 Camera:WorldToScreenPoint
--     投影成画布像素再 SetPosition。绝不运行期建文档——爆桶连锁高峰一帧仨击杀，
--     现场解析 RML 会卡顿。

local Config = require("game.Config")

---@class Juice 打击感反馈（hit-stop / 震屏 / 飘字）
local Juice = {}
Juice.__index = Juice

-- 每种击杀的强度配比：hit-stop 时长（秒）/ 震屏 trauma 增量。
-- 大船击沉是全场最高潮（沉船动画 2.2s），给它最长的定格；海盗 1 发死，给最短的
-- 一顿——连杀时 0.06s 的顿挫像鼓点而不是卡死。
local KILL_PROFILE = {
    pirate  = { stop = 0.06, trauma = 0.25 },
    boat    = { stop = 0.10, trauma = 0.45 },
    bigboat = { stop = 0.14, trauma = 0.70 },
    barrel  = { stop = 0.08, trauma = 0.50 },
}
local DEFAULT_PROFILE = { stop = 0.08, trauma = 0.35 }

-- 飘字池大小：连环爆桶 + 双船同沉的峰值同屏约 6-8 条，10 个槽够轮
local POP_POOL = 10
-- 飘字文档的固定尺寸（画布像素），与 ScorePop.rml 的 body 宽高一致
local POP_W, POP_H = 200.0, 44.0

--- 伪噪声：两个不可通约频率的正弦叠加，看不出周期但不需要随机数库。
--- t 是相位（真实时间 × 频率），seed 给 x/y/roll 各自错开。
local function noise(t, seed)
    return math.sin(t * 47.1 + seed * 13.7) * 0.62 + math.sin(t * 89.3 + seed * 7.9) * 0.38
end

---@param scene Scene
---@param deps { camera: Camera, cameraNode: Node, canvasSize: fun():(number, number) }
---@return Juice
function Juice.Build(scene, deps)
    local self = setmetatable({}, Juice)
    self.camera = deps.camera
    self.cameraNode = deps.cameraNode
    self.canvasSize = deps.canvasSize

    self.stopT = 0.0        -- hit-stop 剩余时长（真实秒）
    self.trauma = 0.0       -- 震屏强度 0..1
    self.shakeT = 0.0       -- 噪声相位种子（真实时间推进，hit-stop 期间照走）

    self.pops = {}
    self.popCursor = 1
    for i = 1, POP_POOL do
        local comp = scene:CreateComponent("LuaRmlUIComponent")
        local slot = { comp = comp, w = nil, text = "", visible = false, world = Vector3(0.0, 0.0, 0.0), t = 0.0 }
        comp:SetHandlers({
            onInit = function(w)
                slot.w = w
                w:BindReadonlyProperty("text", function() return slot.text end)
                w:BindReadonlyProperty("visible", function() return slot.visible end)
            end,
        })
        comp:SetResource("UI/ScorePop.rml")
        self.pops[i] = slot
    end
    return self
end

--- 击杀反馈入口（main.lua 的 OnScore 调）：hit-stop + 震屏 + 飘字一次全给。
---@param unit table Enemy 单位（读 kind 与节点世界坐标）
---@param score number 飘字显示的分值
function Juice:OnKill(unit, score)
    local profile = KILL_PROFILE[(unit and unit.kind) or ""] or DEFAULT_PROFILE

    -- hit-stop 只在“更长”时刷新：连环爆炸不会把定格串成半秒卡顿
    if profile.stop > self.stopT then
        self.stopT = profile.stop
    end

    self.trauma = math.min(self.trauma + profile.trauma, 1.0)

    if unit and unit.node then
        -- 世界锚定在单位头顶（船 1.6 ≈ 甲板上方、海盗 1.6 ≈ 头顶）
        local wp = unit.node:GetWorldPosition()
        self:PopText("+" .. (score or 0), wp + Vector3(0.0, 1.6, 0.0))
    end
end

--- 只加震屏不加定格（防线被砍这类“受挫”时刻：停时间只会让玩家困惑）。
---@param amount number
function Juice:AddTrauma(amount)
    self.trauma = math.min(self.trauma + (amount or 0.3), 1.0)
end

--- 从池里轮询一个槽位显示飘字（老槽还在飞就被顶掉，最多同屏 POP_POOL 条）。
---@param text string
---@param worldPos Vector3
function Juice:PopText(text, worldPos)
    local slot = self.pops[self.popCursor]
    self.popCursor = self.popCursor % #self.pops + 1
    slot.text = text
    slot.world = worldPos
    slot.t = 0.0
    slot.visible = true
    if slot.w then
        slot.w:MarkDirty("text")
        slot.w:MarkDirty("visible")
    end
end

--- 帧首：推进 hit-stop 的真实倒计时，返回本帧世界 dt。
--- 世界 dt 供 Shooting/Enemy/Waves/Player/海面滚动使用；引擎动画与粒子不受影响。
---@param dt number 真实帧步长
---@return number
function Juice:BeginFrame(dt)
    if self.stopT > 0.0 then
        self.stopT = self.stopT - dt
        if self.stopT < 0.0 then
            self.stopT = 0.0
        end
        return dt * Config.HITSTOP_SCALE
    end
    return dt
end

--- 帧尾（player:Update 之后）：推进震屏相位、衰减 trauma、投影并移动飘字、
--- 给相机叠加抖动偏移。必须在 Player:UpdateCamera 摆好基础姿态之后调，
--- 否则偏移会被下一轮相机重摆覆盖。
---@param dt number 真实帧步长
function Juice:EndFrame(dt)
    self.shakeT = self.shakeT + dt
    if self.trauma > 0.0 then
        self.trauma = self.trauma - Config.SHAKE_DECAY * dt
        if self.trauma < 0.0 then
            self.trauma = 0.0
        end
    end

    -- 飘字走真实时间（定格时 UI 反馈照飞）；累计上浮量由年龄直接算出（帧率无关）：
    -- rise(k) = RISE*L*(2k-k²) 是减速曲线——出生快、飘到顶前趋停
    local cw, ch = self.canvasSize()
    if cw > 0.0 and ch > 0.0 then
        for _, slot in ipairs(self.pops) do
            if slot.visible then
                slot.t = slot.t + dt
                if slot.t >= Config.POPSCORE_LIFE then
                    slot.visible = false
                    if slot.w then
                        slot.w:MarkDirty("visible")
                    end
                else
                    local k = slot.t / Config.POPSCORE_LIFE
                    local sp = self.camera:WorldToScreenPoint(slot.world)
                    local rise = Config.POPSCORE_RISE * Config.POPSCORE_LIFE * (2.0 * k - k * k)
                    slot.comp:SetPosition(Vector2(sp.x * cw - POP_W * 0.5,
                        sp.y * ch - POP_H * 0.5 - rise))
                end
            end
        end
    end

    -- 相机抖动：trauma² 映射抖幅（小强度几乎无感、叠满才到配置峰值，天然衰减曲线）
    if self.trauma > 0.0 then
        local s = self.trauma * self.trauma
        local t = self.shakeT * 30.0
        local amp = Config.SHAKE_AMPLITUDE * s
        local pos = self.cameraNode:GetPosition()
        self.cameraNode:SetPosition(pos
            + Vector3(noise(t, 1.0) * amp, noise(t, 2.0) * amp * 0.6, 0.0))
        -- 绕视轴 Roll：LookAt 后相机自身 Z 轴即视轴，Roll 是已验证的旋转出口
        local roll = Config.SHAKE_ROLL * s * noise(t, 3.0)
        if math.abs(roll) > 0.01 then
            self.cameraNode:Roll(roll)
        end
    end
end

--- 换局清零（RestartGame 调）。
function Juice:Reset()
    self.stopT = 0.0
    self.trauma = 0.0
    for _, slot in ipairs(self.pops) do
        if slot.visible then
            slot.visible = false
            if slot.w then
                slot.w:MarkDirty("visible")
            end
        end
    end
end

return Juice

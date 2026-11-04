-- Scripts/game/Hud.lua
--
-- 数据绑定的 HUD：包一层 LuaRmlUIComponent + GameHud.rml。
--
-- 机制（见 107_HelloRmlUI 与 LuaRmlUIBindings.cpp）：
--   * SetHandlers{onInit=fn} 必须在 SetResource 之前调用；onInit 只在文档构造那一刻触发，
--     是唯一能 Bind* 的时机。绑定用 BindReadonlyProperty(name, getter)，事件用 BindEvent。
--   * 之后 Lua 改的是本模块的 local 值，再 MarkDirty(name) 通知 RmlUi 重新取 getter。
--   * 所以每个 SetXxx 都是"改 self 字段 + MarkDirty"，getter 只是读 self 字段。
--
-- 所有 SetXxx 都做了组件是否就绪的判断：SetResource 之前（onInit 未跑）调用不会崩，
-- 只是那次 MarkDirty 落空，值仍存在 self 上，onInit 后首次渲染就能读到。

local Config = require("game.Config")

---@class Hud 玩法 HUD
local Hud = {}
Hud.__index = Hud

local function clamp01(v)
    if v < 0.0 then
        return 0.0
    elseif v > 1.0 then
        return 1.0
    end
    return v
end

---@param scene Scene
---@param handlers { onRestart: fun()|nil, onBoard: fun()|nil, onDisembark: fun()|nil,
---                   onVoiceRecord: fun(slot:string)|nil, onVoicePreview: fun()|nil,
---                   onVoiceFilter: fun()|nil }|nil
---@return Hud
function Hud.Build(scene, handlers)
    local self = setmetatable({}, Hud)
    handlers = handlers or {}

    -- 数据（getter 读这些；MarkDirty 通知刷新）
    self.score = 0
    self.wave = 1
    self.combo = 0
    self.ammo = Config.MAG_SIZE
    self.reload = 0.0          -- 0..1，绑定 <progress data-value="reload">
    -- 主角血、炮塔血都不往 HUD 喂：分挂在人物/炮塔头顶的世界空间血条（game/HpBar.lua）每帧直读。
    self.narrate = ""
    self.narrateT = 0.0
    -- 开始画面：会话首次默认盖着战场（data-if="showStart"），点“开始”收起后
    -- 本会话不再出现；开着的期间 main.lua 把世界 dt 压成 0（波间倒计时不走）。
    self.showStart = true
    -- hitmarker 靠两个 bool + data-if 二选一显示（内联 style 绑不了 opacity/color）
    self.hitNormal = false
    self.hitStrong = false
    self.hitT = 0.0            -- >0 时显示 hitmarker，到点自动灭
    self.gameOver = false      -- data-if="gameOver"：结算面板显隐
    self.endTitle = ""
    self.endScore = 0
    -- 结算页分享入口：与语音入口同源的宿主能力判定（编辑器/桌面没有分享通路），
    -- main.lua 在 Build 后按 sdk:IsAvailable() 打开。
    self.shareAvailable = false
    -- 结算页「看广告复活」按钮显隐：三层全真才亮——败局且未用过（Waves:CanRevive）+
    -- 宿主 SDK 可用 + Config.AD.REWARDED_UNIT_ID 非空（休眠链路：没广告位就整条不渲染）。
    -- main.lua 的 OnGameOver 点亮，ReviveGame/RestartGame 熄灭。
    self.reviveAvailable = false
    -- 登船/下船按钮显隐（data-if）：main.lua 帧循环里根据交互条件 Show/Hide
    self.showBoard = false
    self.showDisembark = false
    -- 局内成长（game/Grow.lua 推）：等级 + 经验条进度(0..1) + 武器文本（“弹丸 xN”，
    -- 力量可见增长的直读回报）
    self.level = 1
    self.xpProg = 0.0
    self.weaponText = "弹丸 x1"
    -- 性能 Overlay（调试）：perfText 是 Lua 拼好的一行文本，showPerf 控制 data-if 整体显隐。
    self.perfText = ""
    self.showPerf = false
    -- 自定义语音（game/Voice.lua 推）：入口只在宿主 SDK 可用时出现（编辑器/桌面没有
    -- 麦克风通路）；voiceOpen 是模态面板，main.lua 帧循环据此把世界 dt 压成 0——
    -- 录一句要 3 秒，不暂停就是“调个性调着调着被偷家”。
    self.voiceAvailable = false
    self.voiceOpen = false
    self.voiceRecording = false
    self.voiceStatus = ""
    self.voiceFilterName = ""

    local comp = scene:CreateComponent("LuaRmlUIComponent")
    self.comp = comp

    comp:SetHandlers({
        onInit = function(w)
            self.w = w
            w:BindReadonlyProperty("score", function() return self.score end)
            w:BindReadonlyProperty("wave", function() return self.wave end)
            w:BindReadonlyProperty("ammo", function() return self.ammo end)
            w:BindReadonlyProperty("endScore", function() return self.endScore end)
            w:BindReadonlyProperty("comboText", function()
                return self.combo > 1 and ("x" .. self.combo) or ""
            end)
            -- progress 条：0..1 数值，RmlUi <progress data-value> 直接吃
            w:BindReadonlyProperty("reload", function() return clamp01(self.reload) end)
            -- data-if 显隐：bool
            w:BindReadonlyProperty("hitNormal", function() return self.hitNormal end)
            w:BindReadonlyProperty("hitStrong", function() return self.hitStrong end)
            w:BindReadonlyProperty("gameOver", function() return self.gameOver end)
            w:BindReadonlyProperty("showStart", function() return self.showStart end)
            w:BindReadonlyProperty("showBoard", function() return self.showBoard end)
            w:BindReadonlyProperty("showDisembark", function() return self.showDisembark end)
            w:BindReadonlyProperty("narrate", function() return self.narrate end)
            w:BindReadonlyProperty("endTitle", function() return self.endTitle end)
            w:BindReadonlyProperty("shareAvailable", function() return self.shareAvailable end)
            w:BindReadonlyProperty("reviveAvailable", function() return self.reviveAvailable end)
            -- 局内成长：等级、经验条(0..1)、武器文本
            w:BindReadonlyProperty("level", function() return self.level end)
            w:BindReadonlyProperty("xp", function() return clamp01(self.xpProg) end)
            w:BindReadonlyProperty("weaponText", function() return self.weaponText end)
            -- 性能 Overlay（调试）：文本 + 显隐开关
            w:BindReadonlyProperty("perfText", function() return self.perfText end)
            w:BindReadonlyProperty("showPerf", function() return self.showPerf end)
            -- 自定义语音：入口显隐 / 面板显隐 / 录音中标记 / 状态行 / 当前音色名
            w:BindReadonlyProperty("voiceAvailable", function() return self.voiceAvailable end)
            w:BindReadonlyProperty("voiceOpen", function() return self.voiceOpen end)
            w:BindReadonlyProperty("voiceRecording", function() return self.voiceRecording end)
            w:BindReadonlyProperty("voiceStatus", function() return self.voiceStatus end)
            w:BindReadonlyProperty("voiceFilterName", function() return self.voiceFilterName end)

            w:BindEvent("restart", function()
                if handlers.onRestart then
                    handlers.onRestart()
                end
            end)
            -- 开始画面：收起面板并放行世界（game_start 埋点在 main.lua 的回调里报，
            -- 那才是“用户主动决定开始”的漏斗起点）
            w:BindEvent("start", function()
                if handlers.onStart then
                    handlers.onStart()
                end
            end)
            -- 载具交互：登船/下船
            w:BindEvent("board", function()
                if handlers.onBoard then
                    handlers.onBoard()
                end
            end)
            w:BindEvent("disembark", function()
                if handlers.onDisembark then
                    handlers.onDisembark()
                end
            end)
            -- 结算页分享：战绩由 main.lua 的回调从 Waves 取（分数/波数只在局终有意义）
            w:BindEvent("share", function()
                if handlers.onShare then
                    handlers.onShare()
                end
            end)
            -- 结算页看广告复活：真正的广告拉起/看完判定在 main.lua 的回调里，
            -- 这里只转发点击；按钮本身靠 reviveAvailable 的 data-if 控制显隐。
            w:BindEvent("revive", function()
                if handlers.onRevive then
                    handlers.onRevive()
                end
            end)
            -- 自定义语音：开/关面板是纯 UI 状态（就地切），录/试听/换音色转发给 Voice
            w:BindEvent("voice_open", function() self:OpenVoice() end)
            w:BindEvent("voice_close", function() self:CloseVoice() end)
            w:BindEvent("voice_rec_lose", function()
                if handlers.onVoiceRecord then
                    handlers.onVoiceRecord(Config.VOICE_SLOT_LOSE)
                end
            end)
            w:BindEvent("voice_rec_kill", function()
                if handlers.onVoiceRecord then
                    handlers.onVoiceRecord(Config.VOICE_SLOT_KILLSTREAK)
                end
            end)
            w:BindEvent("voice_preview", function()
                if handlers.onVoicePreview then
                    handlers.onVoicePreview()
                end
            end)
            w:BindEvent("voice_clear_lose", function()
                if handlers.onVoiceClear then
                    handlers.onVoiceClear(Config.VOICE_SLOT_LOSE)
                end
            end)
            w:BindEvent("voice_clear_kill", function()
                if handlers.onVoiceClear then
                    handlers.onVoiceClear(Config.VOICE_SLOT_KILLSTREAK)
                end
            end)
            w:BindEvent("voice_filter", function()
                if handlers.onVoiceFilter then
                    handlers.onVoiceFilter()
                end
            end)
        end,
    })

    -- SetResource 打开文档，触发上面的 onInit（顺序不能反）
    comp:SetResource("UI/GameHud.rml")
    return self
end

--- 安全 MarkDirty：组件还没跑完 onInit 时 self.w 为 nil，跳过即可（值已在 self 上）。
---@param name string
function Hud:Dirty(name)
    if self.w then
        self.w:MarkDirty(name)
    end
end

function Hud:SetScore(v)
    self.score = v or 0
    self:Dirty("score")
end

function Hud:SetWave(v)
    self.wave = v or 1
    self:Dirty("wave")
end

function Hud:SetCombo(v)
    self.combo = v or 0
    self:Dirty("comboText")
end

function Hud:SetAmmo(v)
    self.ammo = v or 0
    self:Dirty("ammo")
end

--- 装弹进度 0..1。
function Hud:SetReload(v)
    self.reload = clamp01(v or 0.0)
    self:Dirty("reload")
end

--- 性能 Overlay（调试）：直接喂拼好的一行文本。
function Hud:SetPerf(text)
    self.perfText = text or ""
    self:Dirty("perfText")
end

--- 性能 Overlay 显隐（上线关/调试开）。关掉后元素不进渲染树，顺带刷一次文本。
function Hud:ShowPerf(b)
    self.showPerf = b and true or false
    self:Dirty("showPerf")
    self:Dirty("perfText")
end

--- 命中标记闪一下。strong（爆头/击杀）时显红色那组，否则白色。~0.15s 后由 Update 自动灭。
function Hud:FlashHit(strong)
    self.hitStrong = strong and true or false
    self.hitNormal = not self.hitStrong
    self.hitT = 0.15
    self:Dirty("hitNormal")
    self:Dirty("hitStrong")
end

--- 顶部旁白（波次提示 / 间歇倒计时），几秒后自动清。
---@param text string
---@param duration number|nil
function Hud:Narrate(text, duration)
    self.narrate = text or ""
    self.narrateT = duration or 2.5
    self:Dirty("narrate")
end

--- 波间间歇倒计时文案（Waves 每帧调）。<=0 时留空。
function Hud:SetIntermission(t)
    if t and t > 0.0 then
        local secs = math.floor(t + 0.5)
        local text = "下一波 " .. secs .. "s"
        if text ~= self.narrate then
            self.narrate = text
            self.narrateT = 0.0 -- 由 Waves 持续刷新，不走自动清
            self:Dirty("narrate")
        end
    end
end

--- 结算面板。
---@param win boolean
---@param score number
---@param title string|nil 可选自定义标题（如“主角阵亡”）；缺省按胜/败给默认
function Hud:ShowEnd(win, score, title)
    self.gameOver = true
    self.endTitle = title or (win and "胜利!" or "防线失守")
    self.endScore = score or 0
    self:Dirty("gameOver")
    self:Dirty("endTitle")
    self:Dirty("endScore")
end

--- 收起开始画面（点“开始”后；会话内不再出现）。
function Hud:HideStart()
    if self.showStart then
        self.showStart = false
        self:Dirty("showStart")
    end
end

---@return boolean 开始画面是否盖着（帧循环用它把世界 dt 压成 0，波间倒计时不走）
function Hud:IsStartModal()
    return self.showStart
end

--- 结算页分享入口显隐（宿主 SDK 可用才显示）。
function Hud:SetShareAvailable(b)
    self.shareAvailable = b and true or false
    self:Dirty("shareAvailable")
end

--- 结算页复活按钮显隐（main.lua：OnGameOver 亮、ReviveGame/RestartGame 灭）。
function Hud:SetReviveAvailable(b)
    self.reviveAvailable = b and true or false
    self:Dirty("reviveAvailable")
end

function Hud:HideEnd()
    self.gameOver = false
    self:Dirty("gameOver")
end

--- 显/隐登船、下船按钮（id="board"/"disembark"）。main.lua 帧循环每帧调。
---@param id string
function Hud:ShowButton(id)
    if id == "board" then
        if not self.showBoard then
            self.showBoard = true
            self:Dirty("showBoard")
        end
    elseif id == "disembark" then
        if not self.showDisembark then
            self.showDisembark = true
            self:Dirty("showDisembark")
        end
    end
end

---@param id string
function Hud:HideButton(id)
    if id == "board" then
        if self.showBoard then
            self.showBoard = false
            self:Dirty("showBoard")
        end
    elseif id == "disembark" then
        if self.showDisembark then
            self.showDisembark = false
            self:Dirty("showDisembark")
        end
    end
end

----------------------------------------------------------------------
-- 局内成长（game/Grow.lua 推）
----------------------------------------------------------------------

--- 等级数字。
function Hud:SetLevel(v)
    self.level = v or 1
    self:Dirty("level")
end

--- 经验条进度 0..1（当前级到下一级的百分比）。
function Hud:SetXpProgress(v)
    self.xpProg = clamp01(v or 0.0)
    self:Dirty("xp")
end

--- 武器文本（“弹丸 xN”），升级时随当前弹丸数刷新。
function Hud:SetWeaponText(text)
    self.weaponText = text or ""
    self:Dirty("weaponText")
end

----------------------------------------------------------------------
-- 自定义语音（game/Voice.lua）
----------------------------------------------------------------------

--- 是否显示语音入口（宿主 SDK 可用才显示；编辑器/桌面整块不渲染）。
function Hud:SetVoiceAvailable(b)
    self.voiceAvailable = b and true or false
    self:Dirty("voiceAvailable")
end

--- 打开语音面板（模态，main.lua 据此暂停世界推进）。
function Hud:OpenVoice()
    if not self.voiceOpen then
        self.voiceOpen = true
        self:Dirty("voiceOpen")
    end
end

function Hud:CloseVoice()
    if self.voiceOpen then
        self.voiceOpen = false
        self:Dirty("voiceOpen")
    end
end

---@return boolean 语音面板是否开着（帧循环用它把世界 dt 压成 0）
function Hud:IsVoiceModal()
    return self.voiceOpen
end

--- 录音中标记（面板上亮红点）。
function Hud:SetVoiceRecording(b)
    self.voiceRecording = b and true or false
    self:Dirty("voiceRecording")
end

--- 面板状态行（录好了 / 没授权麦克风 / 还没录过）。
---@param text string
function Hud:SetVoiceStatus(text)
    self.voiceStatus = text or ""
    self:Dirty("voiceStatus")
end

--- 当前变声音色名（Voice:CycleFilter 的返回值）。
---@param name string
function Hud:SetVoiceFilterName(name)
    self.voiceFilterName = name or ""
    self:Dirty("voiceFilterName")
end

--- 每帧：hitmarker 到点自动关闭 + 旁白到期清除。
---@param dt number
function Hud:Update(dt)
    if self.hitT > 0.0 then
        self.hitT = self.hitT - dt
        if self.hitT <= 0.0 then
            self.hitT = 0.0
            if self.hitNormal or self.hitStrong then
                self.hitNormal = false
                self.hitStrong = false
                self:Dirty("hitNormal")
                self:Dirty("hitStrong")
            end
        end
    end
    if self.narrateT > 0.0 then
        self.narrateT = self.narrateT - dt
        if self.narrateT <= 0.0 and self.narrate ~= "" then
            self.narrate = ""
            self:Dirty("narrate")
        end
    end
end

return Hud

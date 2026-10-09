-- Scripts/game/Fx.lua
--
-- 一次性粒子与枪口闪光的生命周期管理。
--
-- 为什么要这个模块：rbfx 的 ParticleEmitter 在 Lua 里只暴露 SetEffect / SetEmitting /
-- IsEmitting —— 没有 SetAutoRemoveMode，效果播完（activetime 到点停止发射、粒子活完 ttl）
-- 之后节点会一直留在场景里。所以这里维护一张"到点就 Remove"的活动表，由 main.lua 每帧
-- 驱动 Fx.Update。闪光（Point Light）同理：亮一帧没人管就会常亮，也得自己收。
--
-- 粒子效果一律用工程自带的 Particle/*.xml（配套 ParticleSoft/ParticleAdd 材质 + Dot.png），
-- 不能用引擎示例 msvc/bin/Data 下那批：它们引用的材质与贴图不进抖音包，真机上会静默消失。

local Config = require("game.Config")

---@class Fx 特效发射器与闪光的运行时管理
local Fx = {}
Fx.__index = Fx

--- 每种特效的存活时长（秒）：必须 >= activetime + max(timetolive)，否则粒子还没淡完
--- 就被 Remove 掉，看着像"啪一下没了"。数值与 Particle/*.xml 里的 ttl 对齐，可调。
local LIFE = {
    hitFlesh = 0.8,
    hitWood = 0.9,
    hitMetal = 0.6,
    splash = 1.1,
    explosion = 1.2,
    smoke = 2.6,
    sink = 1.1,
}
local DEFAULT_LIFE = 1.2

---@param scene Scene
---@return Fx
function Fx.Build(scene)
    local self = setmetatable({}, Fx)
    self.scene = scene
    self.root = scene:CreateChild("Fx")
    ---@type { node: Node, ttl: number }[]
    self.active = {}
    return self
end

--- 在 worldPos 喷一次性粒子。
---@param key string Config.Fx 的键，如 "hitFlesh"
---@param worldPos Vector3
---@param scale number|nil 整体缩放，缺省 1
function Fx:Spawn(key, worldPos, scale)
    local path = Config.Fx[key]
    if not path then
        print("Fx: unknown effect key " .. tostring(key))
        return nil
    end
    local effect = GetResource("ParticleEffect", path)
    if not effect then
        print("Fx: missing ParticleEffect " .. path)
        return nil
    end

    local node = self.root:CreateChild("Fx_" .. key)
    node:SetPosition(worldPos)
    if scale and scale ~= 1.0 then
        node:SetScale(Vector3(scale, scale, scale))
    end
    local emitter = node:CreateComponent("ParticleEmitter")
    emitter:SetEffect(effect)

    self.active[#self.active + 1] = { node = node, ttl = LIFE[key] or DEFAULT_LIFE }
    return node
end

--- 枪口/爆炸的一帧点光。亮 lifetime 秒后自动收。
---@param worldPos Vector3
---@param color Color
---@param range number
---@param lifetime number|nil
function Fx:Flash(worldPos, color, range, lifetime)
    local node = self.root:CreateChild("Flash")
    node:SetPosition(worldPos)
    local light = node:CreateComponent("Light")
    -- SetLightType 收的是 LightType 枚举（LUA_ENUM_TABLE(LIGHT,...) 生成的全局表 LIGHT），
    -- 不是字符串；传 "Point" 会报 expected number, received string（同 MOUSEB.LEFT/TS.WORLD 一套）。
    light:SetLightType(LIGHT.POINT)
    light:SetColor(color)
    light:SetRange(range)
    light:SetBrightness(1.0)
    self.active[#self.active + 1] = { node = node, ttl = lifetime or 0.06 }
    return node
end

--- 每帧：递减存活时间，到点的节点 Remove 掉。
--- 倒序遍历，Remove 时直接 swap-pop，省掉表移动。
---@param dt number 秒
function Fx:Update(dt)
    local list = self.active
    for i = #list, 1, -1 do
        local entry = list[i]
        entry.ttl = entry.ttl - dt
        if entry.ttl <= 0.0 then
            entry.node:Remove()
            list[i] = list[#list]
            list[#list] = nil
        end
    end
end

--- 清空所有活动特效（换局/停止时用）。
function Fx:Clear()
    for _, entry in ipairs(self.active) do
        entry.node:Remove()
    end
    self.active = {}
end

return Fx

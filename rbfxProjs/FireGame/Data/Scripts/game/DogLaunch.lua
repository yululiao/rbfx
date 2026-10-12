-- Scripts/game/DogLaunch.lua
--
-- 宠物狗"被大船炮弹炸飞"的独立状态机：程序化抛物线 + 多轴翻滚 + 落地起身回正。
-- 从 game/Dog.lua 里拆出来，是为了把这段与索敌/移动完全正交的生命周期单点收拢——
-- Dog 只在 Enemy:CannonImpact 命中时转发一句 HitByExplosion，其余（空中推进、翻滚、
-- 起身）都归本模块，空中期间整体接管狗节点，落地后交回 Dog 的主状态机。
--
-- 它不改血量、不判存活：狗炸不死（纯搞笑反馈）。
-- 抖音包无物理，绝不做刚体——竖直靠 vy/重力积分，水平靠带阻尼的弹出位移，翻滚靠每帧
-- 在当前世界朝向上前乘一个绕斜轴的小角增量。坐标/朝向约定与 Dog 一致：node 原点在四脚
-- 底面、贴沙面 Config.SAND_Y。
--
-- 依赖注入：构造时传入 dog，读写 dog 的 node/x/z/yaw/state/curAnim/biteT/cfg.launch，
-- 并调用 dog:ClampXZ / dog:SetGround / dog:EnsureAnim / dog:Bark 与 dog.anim。本模块不
-- require Dog（避免循环），只 require Config 取 SAND_Y / DogAnim。

local Config = require("game.Config")

---@class DogLaunch 炸飞/起身生命周期状态机
local DogLaunch = {}
DogLaunch.__index = DogLaunch

local function distXZ(ax, az, bx, bz)
    local dx, dz = ax - bx, az - bz
    return math.sqrt(dx * dx + dz * dz)
end

---@param dog Dog 宿主（提供 node/x/z/yaw/cfg.launch 与 ClampXZ/SetGround/EnsureAnim/Bark/anim）
---@return DogLaunch
function DogLaunch.New(dog)
    return setmetatable({
        dog = dog,
        launch = nil,       -- 非 nil 表示在空中：{ vy, dirx, dirz, axis }
        recoverT = 0.0,     -- 落地起身计时
        h = 0.0,            -- 离地高度（正常 0，被炸飞时走抛物线）
    }, DogLaunch)
end

--- 是否正处于"在空中 / 落地起身"期间——Dog 的主状态机据此让路，休息/索敌也不该插队。
---@return boolean
function DogLaunch:IsActive()
    return self.launch ~= nil or self.recoverT > 0.0
end

--- 回到无炸飞状态（换局/重生用）。
function DogLaunch:Reset()
    self.launch = nil
    self.recoverT = 0.0
    self.h = 0.0
end

--- 由 Enemy:CannonImpact 经 Dog:HitByExplosion 转发进来：blastPos 为炮弹落点（世界，
--- y 传地面即可），radius 为杀伤半径。只有水平距离在半径内、且此刻不在空中/不在起身时才掀飞。
---@param blastPos Vector3
---@param radius number
function DogLaunch:HitByExplosion(blastPos, radius)
    if self:IsActive() then return end
    local dog = self.dog
    if distXZ(dog.x, dog.z, blastPos.x, blastPos.z) <= radius then
        self:Launch(blastPos)
    end
end

--- 起跳：算出弹出方向与初速，切到 launched 态，播 Run_Jump 并惨叫一声。
---@param blastPos Vector3
function DogLaunch:Launch(blastPos)
    local dog = self.dog
    local lc = dog.cfg.launch
    -- 弹出方向 = 由爆炸点指向狗（被气浪推开），取不到方向就随机一个
    local dx, dz = dog.x - blastPos.x, dog.z - blastPos.z
    local d = math.sqrt(dx * dx + dz * dz)
    if d < 1e-4 then
        local a = math.random() * math.pi * 2.0
        dx, dz, d = math.cos(a), math.sin(a), 1.0
    end
    self.launch = {
        vy = lc.upSpeed,
        dirx = dx / d * lc.forwardKick,
        dirz = dz / d * lc.forwardKick,
        -- 斜翻轴：不是纯水平/纯竖直，滚起来像被甩飞而非原地打转
        axis = Vector3(0.8, 0.35, 0.6):Normalized(),
    }
    self.h = 0.0
    dog.state = "launched"
    dog.biteT = 0.0
    dog.curAnim = nil
    dog.anim:PlayExclusive(Config.DogAnim("Run_Jump"), 0, true, 0.05)
    dog:Bark()   -- 被炸飞时惨叫一声
end

--- 每帧推进：在空中则积分抛物线 + 翻滚，落地转入起身；起身则平滑滚回四脚着地。
--- 返回 true 表示本帧被本模块接管（Dog 应跳过主状态机）。
---@param dt number
---@return boolean
function DogLaunch:Update(dt)
    if self.launch then
        self:UpdateLaunch(dt)
        return true
    end
    if self.recoverT > 0.0 then
        self:UpdateRecover(dt)
        return true
    end
    return false
end

function DogLaunch:UpdateLaunch(dt)
    local dog = self.dog
    local lc = dog.cfg.launch
    local l = self.launch
    -- 竖直：上升→重力回落，回到 h<=0 即落地
    l.vy = l.vy - lc.gravity * dt
    self.h = self.h + l.vy * dt
    -- 水平：沿弹出方向位移，带阻尼衰减（气浪推一下就停）
    dog.x = dog.x + l.dirx * dt
    dog.z = dog.z + l.dirz * dt
    local damp = 1.0 - 2.0 * dt
    if damp < 0.0 then damp = 0.0 end
    l.dirx, l.dirz = l.dirx * damp, l.dirz * damp
    dog:ClampXZ()

    if self.h <= 0.0 then
        self.h = 0.0
        self.launch = nil
        self.recoverT = lc.recoverTime
        dog:SetGround()
        return
    end

    dog.node:SetPosition(Vector3(dog.x, Config.SAND_Y + self.h, dog.z))
    -- 绕世界斜轴叠加一个小角增量 → 连续翻滚（不累积误差，每帧在当前朝向上前乘）
    local delta = Quaternion(lc.spinSpeed * dt, l.axis)
    dog.node:SetRotation(delta * dog.node:GetWorldRotation())
end

--- 落地起身：把翻滚留下的朝向平滑滚回四脚着地（回正到 dog.yaw），期间待机动画。
function DogLaunch:UpdateRecover(dt)
    local dog = self.dog
    self.recoverT = self.recoverT - dt
    local up = Quaternion(dog.yaw, Vector3.UP)
    dog.node:SetRotation(dog.node:GetWorldRotation():Slerp(up, math.min(1.0, dt * 6.0)))
    dog:SetGround()
    dog:EnsureAnim("Idle")
    if self.recoverT <= 0.0 then
        dog.node:SetRotation(Quaternion(dog.yaw, Vector3.UP))
        dog.state = "wander"
    end
end

return DogLaunch

-- Scripts/game/Dog.lua
--
-- 主角的宠物狗：一个友方伴随单位。刻意独立于 Enemy 的 self.units（不注册进敌人表），
-- 于是天然获得三条隔离——玩家自动吸附与子弹扫掠只遍历 enemy.units 且只认 pirate/boat/
-- bigboat，够不着狗；波次清怪 ThreatCount 不含狗；爆桶连锁 damage 也只遍历 enemy.units
-- 不误伤狗。狗与外界只有两处接触：
--   * 主动咬海盗：直接对海盗 unit 调 TakeDamage（静默击杀——海盗照样死、照样播死亡动画，
--     但不进 onScore，不加分数/combo/经验、不触发 hit-stop）；
--   * 被大船炮弹炸飞：Enemy:CannonImpact 命中范围内时回调 Dog:HitByExplosion，狗被掀上天
--     翻滚几圈再落地起身。狗无血量，炸不死（纯搞笑反馈）。
-- 海盗不会主动攻击狗（突袭型 AI 只追 player.spot，逻辑未动）。
--
-- 抖音包无物理/导航，移动是 Lua 直线推进 + 矩形钳制；炸飞靠 Lua 抛物线积分 + 多轴翻滚。
-- 面向约定与海盗一致：角色模型原生朝 -z，朝目标 (dx,dz) 的 yaw = atan(dx,-dz)，取负角度绕 UP。

local Config = require("game.Config")
local Materials = require("game.Materials")

---@class Dog 宠物狗（行为 + 生命周期）
local Dog = {}
Dog.__index = Dog

local function distXZ(ax, az, bx, bz)
    local dx, dz = ax - bx, az - bz
    return math.sqrt(dx * dx + dz * dz)
end

---@param deps { scene: Scene, player: Player, enemies: Enemy, audio: Audio }
---@return Dog
function Dog.Build(deps)
    local self = setmetatable({}, Dog)
    self.scene = deps.scene
    self.player = deps.player
    self.enemies = deps.enemies
    self.audio = deps.audio
    self.cfg = Config.DOG

    local root = self.scene:CreateChild("Pets")
    local node = root:CreateChild("Dog")
    self.node = node

    local model = node:CreateComponent("AnimatedModel")
    model:SetModel(GetResource("Model", Config.DogModel()))
    model:SetMaterial(Materials.Character())   -- 与主角共用 Zombie_Atlas 图集材质
    model:SetCastShadows(true)
    model:SetUpdateInvisible(false)

    self.anim = node:CreateComponent("AnimationController")

    -- 地面 XZ（self.x/self.z）+ 离地高度 self.h（正常 0，被炸飞时走抛物线）。狗 node 原点
    -- 在四脚底面，贴沙面 SAND_Y；追击时海盗多已在滩头，咬涉水海盗靠 XZ 判距，狗不必真下海。
    self.x, self.z, self.h = 0.0, 0.0, 0.0
    self.yaw = 0.0
    self.state = "wander"
    self.biteT = 0.0        -- 咬击冷却
    self.barkT = 0.0        -- 吠叫冷却（防转向时刷叫）
    self.idleBarkT = 0.0    -- 闲逛随机吠叫倒计时
    self.wanderT = 0.0      -- 当前游荡目标点的停留/重选倒计时
    self.wx, self.wz = 0.0, 0.0
    self.curAnim = nil      -- 当前循环动画名，避免每帧重复 Play
    self.launch = nil       -- 非 nil 表示在空中：{ vy, dirx, dirz, axis }
    self.recoverT = 0.0     -- 落地起身计时

    self:PlaceAtSpawn()
    self:EnsureAnim("Idle")
    self:SeedIdleBark()
    return self
end

function Dog:PlaceAtSpawn()
    local p = (self.player and self.player.spot) or Config.PLAYER_SPAWN
    self.x = p.x + self.cfg.spawnOffset.x
    self.z = p.z + self.cfg.spawnOffset.z
    self.h = 0.0
    self:ClampXZ()
    self.yaw = Config.PIRATE_YAW   -- 默认朝海（与主角出生朝向一致）
    self.node:SetPosition(Vector3(self.x, Config.SAND_Y, self.z))
    self.node:SetRotation(Quaternion(self.yaw, Vector3.UP))
end

--- 把 (x,z) 钳进可活动矩形：x 用主角可走范围，z 允许略微探到水线去咬刚上岸的海盗，
--- 但不许深入海里、也不绕到塔后村子。
function Dog:ClampPair(x, z)
    local zmin = Config.SHORE_Z - 1.5
    if x < Config.WALK_MIN_X then
        x = Config.WALK_MIN_X
    elseif x > Config.WALK_MAX_X then
        x = Config.WALK_MAX_X
    end
    if z < zmin then
        z = zmin
    elseif z > Config.WALK_MAX_Z then
        z = Config.WALK_MAX_Z
    end
    return x, z
end

function Dog:ClampXZ()
    self.x, self.z = self:ClampPair(self.x, self.z)
end

function Dog:SetGround()
    self.node:SetPosition(Vector3(self.x, Config.SAND_Y, self.z))
end

--- 朝 (dx,dz) 方向即时转身（与海盗同一套 yaw 约定），并记下来供起身时回正。
function Dog:FaceDir(dx, dz)
    if dx == 0.0 and dz == 0.0 then return end
    local yaw = math.atan(dx, -dz)
    self.yaw = -math.deg(yaw)
    self.node:SetRotation(Quaternion(self.yaw, Vector3.UP))
end

function Dog:EnsureAnim(name)
    if self.curAnim == name then return end
    self.curAnim = name
    self.anim:Play(Config.DogAnim(name), 0, true, 0.15)
end

--- 吠一声（狗无 audio 实例时静默，不影响主流程）。
function Dog:Bark()
    if self.audio then
        self.audio:Play("dogBark")
    end
end

--- 重新排下一次闲逛吠叫的时间点（[idleBarkMin, idleBarkMax] 均匀随机）。
function Dog:SeedIdleBark()
    local c = self.cfg
    self.idleBarkT = c.idleBarkMin + math.random() * (c.idleBarkMax - c.idleBarkMin)
end

--- 视野内最近的存活海盗。只挑 kind=="pirate"（不咬船、不碰爆桶），返回 unit 与抬到躯干
--- 中段的命中世界点（伤害/特效都打胸口，与 Enemy:NearestTarget 同一套 combat 瞄点约定）。
---@return table|nil, Vector3|nil
function Dog:FindPirate()
    local best, bestD, bestPos = nil, self.cfg.aggroRadius, nil
    local units = self.enemies:GetUnits()
    for _, u in ipairs(units) do
        if u.alive and u.node and u.kind == "pirate" then
            local p = u.node:GetWorldPosition()
            local d = distXZ(self.x, self.z, p.x, p.z)
            if d <= bestD then
                best, bestD = u, d
                bestPos = Vector3(p.x, p.y + Config.PIRATE_HIT_HEIGHT, p.z)
            end
        end
    end
    return best, bestPos
end

----------------------------------------------------------------------
-- 帧更新
----------------------------------------------------------------------

---@param dt number
function Dog:Update(dt)
    if self.launch then
        self:UpdateLaunch(dt)
        return
    end
    if self.recoverT > 0.0 then
        self:UpdateRecover(dt)
        return
    end

    self.biteT = self.biteT - dt
    self.barkT = self.barkT - dt
    local c = self.cfg
    local target, tpos = self:FindPirate()

    if target then
        local d = distXZ(self.x, self.z, tpos.x, tpos.z)
        if d <= c.biteRange then
            self.state = "bite"
            self:FaceDir(tpos.x - self.x, tpos.z - self.z)
            self:SetGround()
            if self.biteT <= 0.0 then
                self.biteT = c.biteInterval
                self.anim:Play(Config.DogAnim(c.biteAnim), 0, false, 0.05)
                self.curAnim = nil                       -- 让下段移动重新起循环动画
                target:TakeDamage(c.biteDamage, tpos, false)   -- 静默击杀：不走 onScore
            end
        else
            self.state = "chase"
            local step = c.speed * dt
            local inv = step / d
            self.x = self.x + (tpos.x - self.x) * inv
            self.z = self.z + (tpos.z - self.z) * inv
            self:ClampXZ()
            self:FaceDir(tpos.x - self.x, tpos.z - self.z)
            self:SetGround()
            self:EnsureAnim("Run")
            if self.barkT <= 0.0 then          -- 冲去咬的路上反复吠
                self.barkT = c.barkCooldown
                self:Bark()
            end
        end
    else
        self:UpdateWander(dt)
    end
end

--- 无海盗时的溜达：离主太远先回家；否则在主角周围随机挑点慢逛，到位就原地待机。
function Dog:UpdateWander(dt)
    local c = self.cfg
    self.state = "wander"
    -- 平时溜达：隔一阵随机叫几声（计时到就吠一声并重排下一次）
    self.idleBarkT = self.idleBarkT - dt
    if self.idleBarkT <= 0.0 then
        self:SeedIdleBark()
        self:Bark()
    end
    local p = (self.player and self.player.spot) or Config.PLAYER_SPAWN
    local dHome = distXZ(self.x, self.z, p.x, p.z)

    if dHome > c.followRadius then
        local step = c.speed * dt
        local inv = step / dHome
        self.x = self.x + (p.x - self.x) * inv
        self.z = self.z + (p.z - self.z) * inv
        self:ClampXZ()
        self:FaceDir(p.x - self.x, p.z - self.z)
        self:SetGround()
        self:EnsureAnim("Run")
        return
    end

    self.wanderT = self.wanderT - dt
    if self.wanderT <= 0.0 or distXZ(self.x, self.z, self.wx, self.wz) < 0.5 then
        local a = math.random() * math.pi * 2.0
        local r = math.random() * c.wanderRadius
        self.wx, self.wz = self:ClampPair(p.x + math.cos(a) * r, p.z + math.sin(a) * r)
        self.wanderT = 2.0 + math.random() * 3.0
    end

    local d = distXZ(self.x, self.z, self.wx, self.wz)
    if d > 0.3 then
        local step = c.wanderSpeed * dt
        local inv = step / d
        self.x = self.x + (self.wx - self.x) * inv
        self.z = self.z + (self.wz - self.z) * inv
        self:ClampXZ()
        self:FaceDir(self.wx - self.x, self.wz - self.z)
        self:SetGround()
        self:EnsureAnim("Walk")
    else
        self:SetGround()
        self:EnsureAnim("Idle")
    end
end

----------------------------------------------------------------------
-- 被炮弹炸飞（程序化抛物线 + 多轴翻滚）
----------------------------------------------------------------------

--- 由 Enemy:CannonImpact 调用：blastPos 为炮弹落点（世界，y 传地面即可），radius 为杀伤半径。
--- 只有水平距离在半径内、且此刻不在空中/不在起身时才被掀飞。
---@param blastPos Vector3
---@param radius number
function Dog:HitByExplosion(blastPos, radius)
    if self.launch or self.recoverT > 0.0 then return end
    if distXZ(self.x, self.z, blastPos.x, blastPos.z) <= radius then
        self:Launch(blastPos)
    end
end

function Dog:Launch(blastPos)
    local lc = self.cfg.launch
    -- 弹出方向 = 由爆炸点指向狗（被气浪推开），取不到方向就随机一个
    local dx, dz = self.x - blastPos.x, self.z - blastPos.z
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
    self.state = "launched"
    self.biteT = 0.0
    self.curAnim = nil
    self.anim:Play(Config.DogAnim("Run_Jump"), 0, true, 0.05)
    self:Bark()   -- 被炸飞时惨叫一声
end

function Dog:UpdateLaunch(dt)
    local lc = self.cfg.launch
    local l = self.launch
    -- 竖直：上升→重力回落，回到 h<=0 即落地
    l.vy = l.vy - lc.gravity * dt
    self.h = self.h + l.vy * dt
    -- 水平：沿弹出方向位移，带阻尼衰减（气浪推一下就停）
    self.x = self.x + l.dirx * dt
    self.z = self.z + l.dirz * dt
    local damp = 1.0 - 2.0 * dt
    if damp < 0.0 then damp = 0.0 end
    l.dirx, l.dirz = l.dirx * damp, l.dirz * damp
    self:ClampXZ()

    if self.h <= 0.0 then
        self.h = 0.0
        self.launch = nil
        self.recoverT = lc.recoverTime
        self:SetGround()
        return
    end

    self.node:SetPosition(Vector3(self.x, Config.SAND_Y + self.h, self.z))
    -- 绕世界斜轴叠加一个小角增量 → 连续翻滚（不累积误差，每帧在当前朝向上前乘）
    local delta = Quaternion(lc.spinSpeed * dt, l.axis)
    self.node:SetRotation(delta * self.node:GetWorldRotation())
end

--- 落地起身：把翻滚留下的朝向平滑滚回四脚着地（回正到 self.yaw），期间待机动画。
function Dog:UpdateRecover(dt)
    self.recoverT = self.recoverT - dt
    local up = Quaternion(self.yaw, Vector3.UP)
    self.node:SetRotation(self.node:GetWorldRotation():Slerp(up, math.min(1.0, dt * 6.0)))
    self:SetGround()
    self:EnsureAnim("Idle")
    if self.recoverT <= 0.0 then
        self.node:SetRotation(Quaternion(self.yaw, Vector3.UP))
        self.state = "wander"
    end
end

----------------------------------------------------------------------
-- 换局
----------------------------------------------------------------------

function Dog:Reset()
    self.launch = nil
    self.recoverT = 0.0
    self.biteT = 0.0
    self.barkT = 0.0
    self.wanderT = 0.0
    self.curAnim = nil
    self.state = "wander"
    self:PlaceAtSpawn()
    self:EnsureAnim("Idle")
    self:SeedIdleBark()
end

return Dog

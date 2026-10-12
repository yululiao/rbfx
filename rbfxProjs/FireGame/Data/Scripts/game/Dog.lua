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
local DogLaunch = require("game.DogLaunch")

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

    -- 地面 XZ（self.x/self.z）。狗 node 原点在四脚底面，贴沙面 SAND_Y；追击时海盗多已在
    -- 滩头，咬涉水海盗靠 XZ 判距，狗不必真下海。离地高度与“被炸飞/落地起身”整段生命周期
    -- 已拆进 game/DogLaunch.lua（self.launchCtl），Dog 不再持有 self.h。
    self.x, self.z = 0.0, 0.0
    self.yaw = 0.0
    self.state = "wander"
    self.biteT = 0.0        -- 咬击冷却
    self.barkT = 0.0        -- 吠叫冷却（防转向时刷叫）
    self.idleBarkT = 0.0    -- 闲逛随机吠叫倒计时
    self.wanderT = 0.0      -- 当前游荡目标点的停留/重选倒计时
    self.wx, self.wz = 0.0, 0.0
    self.curAnim = nil      -- 当前循环动画名，避免每帧重复 Play
    self.restT = 0.0        -- 就地休息计时（>0 时不移动不索敌）
    self.restReturn = false -- 波次清完：是否正在“跑回主角身边准备休息”
    self.launchCtl = DogLaunch.New(self)   -- 炸飞/起身的独立状态机（见 game/DogLaunch.lua）

    self:PlaceAtSpawn()
    self:EnsureAnim("Idle")
    self:SeedIdleBark()
    return self
end

function Dog:PlaceAtSpawn()
    local p = (self.player and self.player.spot) or Config.PLAYER_SPAWN
    self.x = p.x + self.cfg.spawnOffset.x
    self.z = p.z + self.cfg.spawnOffset.z
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
    -- PlayExclusive：同层其余动画淡出。切勿用 Play——循环态（Idle/Walk/Run/Idle_2）不会
    -- 自然结束，Play 不关旧态会让多个循环动画叠在一起满权重混播（休息后走动仍显 Idle_2）。
    self.anim:PlayExclusive(Config.DogAnim(name), 0, true, 0.15)
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

--- 喘气一声（一波清完休息时播；无 audio 实例时静默）。
function Dog:Pant()
    if self.audio then
        self.audio:Play("dogPant")
    end
end

--- 波次清完的入口：狗先跑回主角身边，到位后由 Update 的 restReturn 分支调用 StartRest 就地
--- 休息。空中/起身期间不插队（等落地后主状态机自然接管，这波就不休息了）。
function Dog:GoRest()
    if self.launchCtl:IsActive() then return end
    self.restReturn = true
end

--- 就地休息 restDuration 秒，播一次喘气（跑回主角身边到位后调用）。
function Dog:StartRest()
    if self.launchCtl:IsActive() then return end
    self.restT = self.cfg.restDuration
    self.state = "rest"
    self:SetGround()
    -- 强制起播 Idle：不走 EnsureAnim 的 curAnim 守卫。上一段可能刚播完一次性 Attack（bite）
    -- 或从 recover 收尾出来，curAnim 记账与实际播放状态不同步，走守卫会让它不真正起播。
    self.curAnim = "Idle"
    self.anim:PlayExclusive(Config.DogAnim("Idle"), 0, true, 0.1)
    self:Pant()
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
    -- 在空中/落地起身期间由 launch 状态机整体接管（推进位移/翻滚/回正），主状态机让路。
    if self.launchCtl:Update(dt) then
        return
    end

    local c = self.cfg

    -- 波次刚清完：先跑回主角身边，到位再就地休息。途中若冒出新海盗，放弃“回家休息”转去撕咬。
    if self.restReturn then
        local p = (self.player and self.player.spot) or Config.PLAYER_SPAWN
        local d = distXZ(self.x, self.z, p.x, p.z)
        if d <= c.restArriveRadius then
            self.restReturn = false
            self:StartRest()
            return
        end
        if self:FindPirate() then
            self.restReturn = false          -- 有新目标：落到下面正常索敌/撕咬
        else
            local step = c.speed * dt
            local inv = step / d
            self.x = self.x + (p.x - self.x) * inv
            self.z = self.z + (p.z - self.z) * inv
            self:ClampXZ()
            self:FaceDir(p.x - self.x, p.z - self.z)
            self:SetGround()
            self:EnsureAnim("Run")
            return
        end
    end

    -- 已到位休息：钉在原地、待机、不索敌不移动，计时到了自动恢复溜达/追击。
    if self.restT > 0.0 then
        self.restT = self.restT - dt
        self.state = "rest"
        self:SetGround()
        self:EnsureAnim("Idle")
        return
    end

    self.biteT = self.biteT - dt
    self.barkT = self.barkT - dt
    local target, tpos = self:FindPirate()

    if target then
        local d = distXZ(self.x, self.z, tpos.x, tpos.z)
        if d <= c.biteRange then
            self.state = "bite"
            self:FaceDir(tpos.x - self.x, tpos.z - self.z)
            self:SetGround()
            if self.biteT <= 0.0 then
                self.biteT = c.biteInterval
                self.anim:PlayExclusive(Config.DogAnim(c.biteAnim), 0, false, 0.05)
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
-- 被炮弹炸飞（程序化抛物线 + 多轴翻滚 + 落地起身）：整段生命周期见 game/DogLaunch.lua。
-- Enemy:CannonImpact 仍打到 Dog:HitByExplosion，这里只转发给内部的 launch 状态机；
-- blastPos 为炮弹落点（世界，y 传地面即可），radius 为杀伤半径。
----------------------------------------------------------------------
---@param blastPos Vector3
---@param radius number
function Dog:HitByExplosion(blastPos, radius)
    self.launchCtl:HitByExplosion(blastPos, radius)
end

----------------------------------------------------------------------
-- 换局
----------------------------------------------------------------------

function Dog:Reset()
    self.launchCtl:Reset()
    self.restT = 0.0
    self.restReturn = false
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

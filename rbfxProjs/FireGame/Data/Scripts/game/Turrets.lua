-- Scripts/game/Turrets.lua
--
-- 滩头两座炮塔的火控与表现：索敌（只咬海盗船）→ 开炮 → 抛物线弹 → 落水溅射，外加"塔被打哑"的反馈。
--
-- 职责边界（走的是 Dog.lua 那种"独立友方单位模块"的路子）：
--   * 布景不在这里——节点由 Coast.BuildTurrets 摆好，句柄（node/gun/x）从这里持有；
--   * 血量也不在这里——唯一真相是 waves.turretHp（每塔一份，下标对齐 Config.TURRETS）。
--     本模块每帧读它来决定"这座塔还能不能开火""炮管要不要歪下来"，掉血由 Enemy 递
--     (塔号, 伤害) 给 Waves:DamageTurret。这里再存一份的话，"塔哑了还在开炮"迟早出现。
--
-- 定位是漏防兜底，不是第二路主力输出，且已改成只打海盗船：单发才 2 点，打 HP=1 的涌岸海盗
-- 一炮一个、把玩家该割的草全抢了（用户反馈"海盗被完虐"），故只咬血厚的船（小船 3 / 大船 8）。
-- 三条硬约束：
--   * 只打船（kind boat/bigboat），不碰走路海盗也不碰爆桶——打海盗会抢人头、打爆桶会经它的
--     OnKilled 连锁走 Enemy:Damage 那条计分路径，兜底炮立刻变成刷分/清屏器；
--   * 船停在海面，旧的上岸警戒线本就拦不到它，改为纯按 Config.TURRET_RANGE 的 XZ 射程索敌；
--   * 击杀直调 unit:TakeDamage，不经 Enemy:Damage / onScore——与宠物狗同一条静默约定：船被
--     炮台打沉也不计分、不涨 combo、不喂经验，自然也不会去触发连杀爆炸。
--
-- 弹道数学与 Enemy:UpdateProjectiles 同源（水平线性 + 4u(1-u) 拱形，t=dur 精确落在瞄点），
-- 但那是敌方慢弹配落点预警盘、这里是友方短弧直射，两张弹列表与两个音效键各自独立，
-- 不共用一条代码路径（同一功能两个入口迟早各改各的）。

local Config = require("game.Config")

---@class Turrets 友方炮塔管理器
local Turrets = {}
Turrets.__index = Turrets

--- XZ 水平距离（全项目"距离"都是这个口径：瞄点/杀伤判定不掺 y）。
---@return number
local function distXZ(ax, az, bx, bz)
    local dx, dz = ax - bx, az - bz
    return math.sqrt(dx * dx + dz * dz)
end

--- 塔心指向 pos 的 yaw（弧度）。炮管与角色模型同一约定（原生朝 -z）：
--- yaw = atan(dx, -dz) 配 Quaternion(-deg(yaw), UP)，Enemy/Player 已验证的同款公式。
---@param tw table
---@param pos Vector3
---@return number
local function yawTo(tw, pos)
    return math.atan(pos.x - tw.x, -(pos.z - tw.z))
end

--- 两 yaw 的最小带符号角差（弧度，归一到 [-π,π]）：跨 ±180 取短弧，转向不走远路整圈甩。
---@return number
local function yawDiff(a, b)
    return (a - b + math.pi) % (2.0 * math.pi) - math.pi
end

---@param deps { scene: Scene, handles: { node: Node, gun: Node, x: number }[], enemies: Enemy, waves: Waves, fx: Fx, audio: Audio, onTowerDown: fun(index:number,x:number)|nil }
---@return Turrets
function Turrets.Build(deps)
    local self = setmetatable({}, Turrets)
    self.enemies = deps.enemies
    self.waves = deps.waves
    self.fx = deps.fx
    self.audio = deps.audio
    self.scene = deps.scene
    -- 塔失能时报个信（main.lua 接 HUD 旁白）。只递出去、不在这里拼文案，
    -- 免得 UI 词汇往玩法模块里渗。
    self.onTowerDown = deps.onTowerDown

    ---@type { node: Node, gun: Node, x: number, z: number, cd: number, dead: boolean,
    ---         aimYaw: number, gunRest: Vector3, recoilT: number|nil }[]
    self.towers = {}
    for i, h in ipairs(deps.handles) do
        self.towers[#self.towers + 1] = {
            node = h.node,
            gun = h.gun,
            x = h.x,
            z = Config.TURRET_Z,
            cd = Config.TURRET_FIRST,
            dead = false,
            aimYaw = 0.0,                    -- 炮管当前朝向（弧度；0 = 炮口朝海）
            gunRest = h.gun:GetPosition(),   -- 炮管静止局部位置，后坐力绕它振荡
            recoilT = nil,                   -- 后坐力计时（nil = 没在晃）
        }
    end
    ---@type { ball: Node, from: Vector3, to: Vector3, t: number, dur: number }[]
    self.projectiles = {}
    return self
end

----------------------------------------------------------------------
-- 供 Enemy 查询（冲塔型海盗要走到哪、砍哪座）
----------------------------------------------------------------------

--- 最近的一座还活着的塔。返回塔号（Config.TURRETS 下标）与海盗该站定的砍击点
--- （塔的海侧外沿，距心 TURRET_STAND_OFF）。全哑了返回 nil，调用方走收尸分支。
---@param x number
---@param z number
---@return number|nil, number|nil, number|nil
function Turrets:NearestAlive(x, z)
    local bestIdx, bestD = nil, math.huge
    for i, tw in ipairs(self.towers) do
        if not tw.dead then
            local d = distXZ(x, z, tw.x, tw.z)
            if d < bestD then
                bestIdx, bestD = i, d
            end
        end
    end
    if not bestIdx then
        return nil, nil, nil
    end
    return bestIdx, self.towers[bestIdx].x, Config.TURRET_Z - Config.TURRET_STAND_OFF
end

----------------------------------------------------------------------
-- 索敌与开火
----------------------------------------------------------------------

--- 这座塔射程内的最近存活海盗船。返回 unit 与抬到甲板的命中点（按 kind 分档，与 Enemy 给
--- 船挑命中点同一套约定：大船 BOAT_BIG_HIT_HEIGHT、小船 BOAT_HIT_HEIGHT）。船本就停在海面，
--- 不再走那道为海盗设的 z 门槛，只看 XZ 距离够不够得着。
---@param tw table
---@param enemies Enemy
---@return table|nil, Vector3|nil
local function FindTarget(tw, enemies)
    local best, bestD, bestPos = nil, Config.TURRET_RANGE, nil
    for _, u in ipairs(enemies:GetUnits()) do
        if u.alive and u.node and (u.kind == "boat" or u.kind == "bigboat") then
            local p = u.node:GetPosition()
            local d = distXZ(tw.x, tw.z, p.x, p.z)
            if d <= bestD then
                best, bestD = u, d
                local lift = (u.kind == "bigboat") and Config.BOAT_BIG_HIT_HEIGHT or Config.BOAT_HIT_HEIGHT
                bestPos = Vector3(p.x, p.y + lift, p.z)
            end
        end
    end
    return best, bestPos
end

--- 从炮口向 toPos 开一炮：炮口光 + 专用炮声 + 一发在飞弹丸（无预警盘，友方射击不用）。
---@param tw table
---@param toPos Vector3
local function Fire(self, tw, toPos)
    -- 后坐力起跳：这一帧起炮管沿炮尾方向被顶出去，之后由 UpdateRecoil 阻尼振荡回座。
    tw.recoilT = 0.0
    -- 炮口位随炮管转向同步转：TURRET_MUZZLE 是 yaw=0 时的偏移，gun 已咬向目标后
    -- 把它绕 Y 转过去，否则炮管横向指着船、炮弹却从朝海侧的固定点冒出来（近看穿帮）。
    local m = Config.TURRET_MUZZLE
    local yaw = tw.aimYaw
    local muzzle = tw.node:GetWorldPosition() + Vector3(
        m.x * math.cos(yaw) - m.z * math.sin(yaw),
        m.y,
        m.x * math.sin(yaw) + m.z * math.cos(yaw))
    self.fx:Flash(muzzle, Color(1.0, 0.78, 0.42), 7.0, 0.08)
    self.audio:Play("turretShot")

    -- 弹丸实体：同用 pirate-kit 的 cannon-ball prefab（自带材质）。ball 是父节点，飞行/落点
    -- 沿用 pr.ball:SetPosition / Remove。
    local ball = self.scene:CreateChild("TurretShell")
    ball:SetPosition(muzzle)
    local prefab = GetResource("PrefabResource", Config.PiratePrefab("cannon-ball"))
    if prefab then
        local model = ball:InstantiatePrefab(prefab, Vector3.ZERO, Quaternion.IDENTITY)
        if model then
            model:SetScale(Config.TURRET_SHELL_SCALE)
            local smc = model:GetComponent("StaticModel")
            if smc then
                smc:SetCastShadows(false)
            end
        end
    else
        LogError("Turrets: 弹丸 prefab 加载失败 'cannon-ball'")
    end

    self.projectiles[#self.projectiles + 1] = {
        ball = ball,
        from = muzzle,
        to = Vector3(toPos.x, Config.SEA_Y, toPos.z),   -- 船在水面：落点按海面不是沙面
        t = 0.0,
        dur = Config.TURRET_FLIGHT_TIME,
    }
end

--- 每帧推进在飞弹丸：抛物线插值，到点结算落地。
---@param dt number
function Turrets:UpdateProjectiles(dt)
    local list = self.projectiles
    for i = #list, 1, -1 do
        local pr = list[i]
        pr.t = pr.t + dt
        local u = pr.t / pr.dur
        if u >= 1.0 then
            self:Impact(pr)
            list[i] = list[#list]
            list[#list] = nil
        else
            local x = pr.from.x + (pr.to.x - pr.from.x) * u
            local z = pr.from.z + (pr.to.z - pr.from.z) * u
            local y = pr.from.y + (pr.to.y - pr.from.y) * u + Config.TURRET_LOB_HEIGHT * 4.0 * u * (1.0 - u)
            pr.ball:SetPosition(Vector3(x, y, z))
        end
    end
end

--- 弹丸落水：爆炸特效 + 专用音效，只对杀伤半径内的海盗船结算静默伤害（见文件头三条硬约束）。
---@param pr table
function Turrets:Impact(pr)
    pr.ball:Remove()
    local ip = pr.to
    local center = Vector3(ip.x, Config.SEA_Y + 0.4, ip.z)
    self.fx:Spawn("explosion", center)
    self.fx:Flash(center, Color(1.0, 0.62, 0.28), 10.0, 0.10)
    self.audio:Play("turretBlast")

    local r2 = Config.TURRET_SPLASH_RADIUS * Config.TURRET_SPLASH_RADIUS
    -- 倒序：TakeDamage 可能让船进入沉没/移除队列，虽然真正剔除由 Enemy:Update 负责、这一帧
    -- 不会改动 units 的长度，但按惯例遍历中可能致死的列表一律倒序走。
    local units = self.enemies:GetUnits()
    for i = #units, 1, -1 do
        local u = units[i]
        if u.alive and u.node and (u.kind == "boat" or u.kind == "bigboat") then
            local p = u.node:GetPosition()
            local dx, dz = p.x - ip.x, p.z - ip.z
            if dx * dx + dz * dz <= r2 then
                local lift = (u.kind == "bigboat") and Config.BOAT_BIG_HIT_HEIGHT or Config.BOAT_HIT_HEIGHT
                u:TakeDamage(Config.TURRET_DAMAGE, Vector3(p.x, p.y + lift, p.z), false)
            end
        end
    end
end

----------------------------------------------------------------------
-- 塔的生死表现
----------------------------------------------------------------------

--- 血量归零 → 标记失能：一次性爆闪 + 烟 + 炮管歪下来；恢复（重开）则把表现收回去。
--- 血量本身不在这里改，只从 Waves 读——读一次、演一次，不产生第二处真相。
function Turrets:SyncTowerState()
    for i, tw in ipairs(self.towers) do
        local hp = (self.waves and self.waves:GetTurretHp(i)) or 0
        local dead = hp <= 0
        if dead ~= tw.dead then
            tw.dead = dead
            if dead then
                local top = Vector3(tw.x, Config.TURRET_TOP_Y + 0.6, tw.z)
                self.fx:Spawn("explosion", top)
                self.fx:Spawn("smoke", Vector3(tw.x, Config.TURRET_TOP_Y + 0.8, tw.z))
                self.fx:Flash(top, Color(1.0, 0.5, 0.2), 12.0, 0.14)
                self.audio:Play("breakWood")
                tw.gun:SetRotation(Quaternion(Config.TURRET_GUN_PITCH_DEAD, Vector3(1.0, 0.0, 0.0)))
                if self.onTowerDown then
                    self.onTowerDown(i, tw.x)
                end
            else
                tw.gun:SetRotation(Quaternion.IDENTITY)
            end
        end
    end
end

----------------------------------------------------------------------
-- 开炮表现：缓动转向 + 后坐力回座（纯演出层，不碰火控数值与命中判定）
----------------------------------------------------------------------

--- 炮管缓动转向：有船则平滑咬向射程内最近的船，没船则平滑回正（炮口朝海）。
--- 弹道落点在 Fire 里按目标独立计算，gun 指没指准都不影响命中；点火的
--- 对准门槛在 Update 里另行把关。失能塔不转（炮管由 SyncTowerState 歪着）。
---@param dt number
function Turrets:UpdateAim(dt)
    for _, tw in ipairs(self.towers) do
        if not tw.dead then
            local targetYaw = 0.0
            local _, pos = FindTarget(tw, self.enemies)
            if pos then
                targetYaw = yawTo(tw, pos)
            end
            -- 指数趋近（每帧走剩余角差的一小口）：快而不僵，跨 ±180 走短弧。
            local diff = yawDiff(targetYaw, tw.aimYaw)
            tw.aimYaw = tw.aimYaw + diff * math.min(1.0, dt * Config.TURRET_AIM_TURN_RATE)
            tw.gun:SetRotation(Quaternion(-math.deg(tw.aimYaw), Vector3.UP))
        end
    end
end

--- 后坐力回座：开炮瞬间炮管沿自身朝向的反方向被顶退，随后按阻尼余弦振荡
--- （一退、一扑、再小退……）衰减回静止位。方向随 aimYaw 转——炮管指向哪、
--- 就朝哪的反方向挫；失能塔也照走完，位置动画不碰 SyncTowerState 管的旋转。
---@param dt number
function Turrets:UpdateRecoil(dt)
    for _, tw in ipairs(self.towers) do
        if tw.recoilT then
            tw.recoilT = tw.recoilT + dt
            if tw.recoilT >= Config.TURRET_RECOIL_TIME then
                tw.recoilT = nil
                tw.gun:SetPosition(tw.gunRest)
            else
                local t = tw.recoilT
                local s = Config.TURRET_RECOIL_AMPLITUDE
                    * math.exp(-Config.TURRET_RECOIL_DECAY * t)
                    * math.cos(Config.TURRET_RECOIL_FREQ * t)
                tw.gun:SetPosition(Vector3(
                    tw.gunRest.x - math.sin(tw.aimYaw) * s,
                    tw.gunRest.y,
                    tw.gunRest.z + math.cos(tw.aimYaw) * s))
            end
        end
    end
end

----------------------------------------------------------------------
-- 帧更新与换局
----------------------------------------------------------------------

---@param dt number
function Turrets:Update(dt)
    self:SyncTowerState()
    self:UpdateAim(dt)
    self:UpdateRecoil(dt)
    -- 结算面板期间不放炮（也不再攒冷却）：这时候战场已经判死，炮声只是噪音。
    if self.waves and self.waves.phase == "ended" then
        return
    end

    for _, tw in ipairs(self.towers) do
        if not tw.dead then
            -- 没目标时冷却继续减到 0 以下不去钳：这样船一进射程就立刻挨一炮，
            -- 而"开局首发迟"只作用在第一次（TURRET_FIRST）那一下。
            tw.cd = tw.cd - dt
            if tw.cd <= 0.0 then
                local _, pos = FindTarget(tw, self.enemies)
                if pos then
                    -- 转向没咬住先不开炮（±TURRET_AIM_FIRE_TOL）：等待期间 cd 不回填，
                    -- 炮口一对准这一帧就点火，转向耗时（约 0.3s）不会被算进间隔里。
                    if math.abs(yawDiff(yawTo(tw, pos), tw.aimYaw)) <= math.rad(Config.TURRET_AIM_FIRE_TOL) then
                        tw.cd = Config.TURRET_INTERVAL + (math.random() * 2.0 - 1.0) * Config.TURRET_INTERVAL_SPREAD
                        Fire(self, tw, pos)
                    end
                end
            end
        end
    end

    self:UpdateProjectiles(dt)
end

--- 新局/重开：在飞弹丸清空、冷却回首发迟、炮管回正回座、失能表现收回。血量归零由 Waves:Reset 负责。
function Turrets:Reset()
    for _, pr in ipairs(self.projectiles) do
        if pr.ball then
            pr.ball:Remove()
        end
    end
    self.projectiles = {}
    for _, tw in ipairs(self.towers) do
        tw.cd = Config.TURRET_FIRST
        tw.aimYaw = 0.0
        tw.recoilT = nil
        tw.gun:SetPosition(tw.gunRest)
        tw.gun:SetRotation(Quaternion.IDENTITY)
        tw.dead = false
    end
end

return Turrets

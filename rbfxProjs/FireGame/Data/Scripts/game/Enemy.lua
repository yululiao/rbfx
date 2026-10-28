-- Scripts/game/Enemy.lua
--
-- 敌人单位与生成器：船（驶来→靠岸吐海盗→离开/被击沉）、海盗（涉水冲滩→砍炮塔）、
-- 爆桶（打爆→半径内连锁伤害）。所有单位共用一套"状态机 + 生命周期"骨架。
--
-- 无物理/导航模块（抖音包禁用 URHO3D_PHYSICS / NAVIGATION），所以：
--   * 移动是 Lua 直线推进（沿 +z 朝防线），不用 CrowdManager；
--   * 爆桶连锁是"距离遍历"而不是球形查询。
--
-- 单位接口（Shooting.lua 依赖）：
--   unit = { node, alive, kind, hp, TakeDamage(amount, hitPos, headshot) -> {killed, score}|nil }
--   射线命中反查走管理器 Enemy:ResolveUnit(node)：rbfx 的 Node:SetVar 不能存 Lua table
--   （LuaToVariant 只认标量/向量/ResourceRef 简写，table 会被丢弃），AddTag 又收 eastl
--   String（Lua 字符串字面量喂不进），所以单位表一律留在 Lua 侧 self.units，用节点指针
--   相等来反查，不往节点上挂表。
--
-- 生命周期字段：
--   alive    是否可被瞄准/受伤（被击杀即 false，但节点还在播死亡动画）
--   removing 是否已进入移除倒计时
--   removeT  倒计时，<=0 时 Remove 节点并从 units 列表剔除

local Config = require("game.Config")
local Materials = require("game.Materials")
local FlipbookCrowd = require("game.FlipbookCrowd")

---@class Enemy 敌人管理器
local Enemy = {}
Enemy.__index = Enemy

local function randSpread(base, spread)
    return base + (math.random() * 2.0 - 1.0) * spread
end

local function pickSkin()
    -- ~15% 稀有皮（骷髅/船长），其余普通海盗
    if math.random() < 0.15 and #Config.PIRATE_SKIN_RARE > 0 then
        return Config.PIRATE_SKIN_RARE[math.random(#Config.PIRATE_SKIN_RARE)]
    end
    return Config.PIRATE_SKINS[math.random(#Config.PIRATE_SKINS)]
end

--- 突袭型专用皮：固定从稀有皮（骷髅/船长）里挑，给玩家一个“这东西是冲我来的”视觉信号。
local function pickRaiderSkin()
    if #Config.PIRATE_SKIN_RARE > 0 then
        return Config.PIRATE_SKIN_RARE[math.random(#Config.PIRATE_SKIN_RARE)]
    end
    return Config.PIRATE_SKINS[math.random(#Config.PIRATE_SKINS)]
end

----------------------------------------------------------------------
-- 涉水判定：岸线是一条曲线，不是一个常量
----------------------------------------------------------------------

--- 这个位置是在沙上还是在水里。旧写法是 p.z < SHORE_Z（一条直水线），现在读
--- Config.ShoreZ(x)：几何与玩法同源，不会出现“看着已上岸、判据还在水里”。
--- 注意用移动后的 x 算：航道是等分直线，但两侧小湾里的水线会往内陆鼓进来。
---@param x number
---@param z number
---@return boolean
local function onLand(x, z)
    return z >= Config.ShoreZ(x)
end

--- 涉水/贴沙两个高度（抬高 PIRATE_WADE_DEPTH 是为了别让模型半截埋在水里）。
---@param x number
---@param z number
---@return number
local function wadeY(x, z)
    if onLand(x, z) then
        return Config.SAND_Y
    end
    return Config.SEA_Y + Config.PIRATE_WADE_DEPTH
end

----------------------------------------------------------------------
-- 管理器
----------------------------------------------------------------------

---@param deps { scene: Scene, fx: Fx, audio: Audio, player: Player, onScore: fun(result:any,unit:any,headshot:boolean,fromChain:boolean|nil)|nil, onTurretHit: fun(index:number,dmg:number)|nil }
---@return Enemy
function Enemy.Build(deps)
    local self = setmetatable({}, Enemy)
    self.scene = deps.scene
    self.fx = deps.fx
    self.audio = deps.audio
    self.player = deps.player
    self.onScore = deps.onScore
    self.onTurretHit = deps.onTurretHit
    self.onPlayerHit = deps.onPlayerHit
    self.dog = deps.dog          -- 宠物狗引用（main.lua 在 Dog.Build 后回填）：大船炮弹炸到它就把它掀飞
    -- 友方炮塔模块引用（main.lua 在 Turrets.Build 后回填，与 dog 同一条路子）：冲塔型海盗
    -- 靠它回答"最近哪座塔还活着、该走到哪个位置"。本模块不自存塔的状态。
    self.turrets = nil
    self.root = deps.scene:CreateChild("Enemies")
    -- 海盗序列帧 billboard 群体渲染器（每皮一个 BillboardSet 合批）。
    self.crowd = FlipbookCrowd.Build(deps.scene)
    ---@type table[]
    self.units = {}
    ---@type { t: number, fn: function }[]
    self.timers = {}
    ---@type { ball: Node, mark: Node, from: Vector3, to: Vector3, t: number, dur: number }[]
    self.projectiles = {}   -- 大船炮台的在飞炮弹（抛物线 + 落点预警盘）
    return self
end

---@return table[] 存活与待移除的单位（Shooting 自动吸附只挑 alive 的）
function Enemy:GetUnits()
    return self.units
end

--- 射线命中反查：给定命中节点，找出它属于哪个单位。
--- sol3 给 Node 注册了 equal_to（比较 Node* 指针），所以 == 可靠；再向父节点回溯几层，
--- 兜住命中点是单位子节点的情况。找不到（地形/工事/天空）返回 nil。
---@param node Node|nil
---@return table|nil
function Enemy:ResolveUnit(node)
    if not node then
        return nil
    end
    for _, u in ipairs(self.units) do
        if u.node then
            local n = node
            for _ = 1, 6 do
                if n == nil then
                    break
                end
                if n == u.node then
                    return u
                end
                n = n:GetParent()
            end
        end
    end
    return nil
end

function Enemy:AliveCount()
    local n = 0
    for _, u in ipairs(self.units) do
        if u.alive then
            n = n + 1
        end
    end
    return n
end

--- 同屏单位闸：超上限时 Waves 会延后生成（58 骨骼蒙皮不便宜）。
function Enemy:CanSpawn()
    return self:AliveCount() < Config.MAX_ALIVE_UNITS
end

--- 活着的"威胁"数（船 + 海盗），不含常驻爆桶。Waves 靠它判断一波是否清完。
function Enemy:ThreatCount()
    local n = 0
    for _, u in ipairs(self.units) do
        if u.alive and (u.kind == "boat" or u.kind == "bigboat" or u.kind == "pirate") then
            n = n + 1
        end
    end
    return n
end

--- 自动吸附取最近目标：从 fromPos 出发，在 maxDist 内挑最近的存活威胁单位（船/海盗，
--- 跳过爆桶——不然会放着快摸防线的海盗不去打而去捅桶）。王者射手式普攻用，玩家无需手动对准。
--- 返回 unit, worldPos。
---@param fromPos Vector3
---@param maxDist number
---@return table|nil, Vector3|nil
function Enemy:NearestTarget(fromPos, maxDist)
    local best, bestD, bestPos = nil, maxDist, nil
    for _, u in ipairs(self.units) do
        if u.alive and u.node and (u.kind == "pirate" or u.kind == "boat" or u.kind == "bigboat") then
            -- 船在"投递兵员"的窗口内不作目标，否则全程在射程里被扇形弹糊、没靠岸就沉，
            -- 海盗吐不出来就没得割（尤其大船载 12 人、卸货窗口长）。规则：
            --   小船：航行(sail)中免锁，靠岸开始卸货(unload)后就可被打——保留玩家拦下后半程的自主。
            --   大船：航行+卸货(sail/unload)都免锁，直到吐完转滞留炮击(bombard)才成为可拆的炮舰。
            -- 滩头一有海盗，自动瞄自然优先割草（海盗始终可打），船得以成批靠岸 = 兵如潮水。
            local delivering = (u.kind == "boat" and u.state == "sail")
                or (u.kind == "bigboat" and u.state ~= "bombard")
            if not delivering then
                local p = u.node:GetWorldPosition()
                -- 单位 node 原点都在“脚下/船底”（脚底与水面下），直接拿它当瞄点会让子弹打脚、
                -- 命中特效也喷在脚底（船更甚：扎进水里被水面盖住）。按 kind 抬到躯干/甲板：选择仍
                -- 按 XZ 距离（下面只用 dx/dz），y 抬高不影响挑谁，只把弹道与 hitPos 提到身体中段。
                if u.kind == "pirate" then
                    p.y = p.y + Config.PIRATE_HIT_HEIGHT
                elseif u.kind == "bigboat" then
                    -- 大船船壳约 8m（ship-pirate-large ×0.8），与小船的 0.85 差一个数量级，
                    -- 共用一个高度必有一边飘，所以按 kind 分档。
                    p.y = p.y + Config.BOAT_BIG_HIT_HEIGHT
                else
                    p.y = p.y + Config.BOAT_HIT_HEIGHT
                end
                local dx, dz = p.x - fromPos.x, p.z - fromPos.z
                local d = math.sqrt(dx * dx + dz * dz)
                if d <= bestD then
                    best, bestD, bestPos = u, d, p
                end
            end
        end
    end
    return best, bestPos
end

--- 延时执行（连锁爆炸的递延靠它，避免同一帧递归引爆）。
---@param delay number
---@param fn function
function Enemy:Schedule(delay, fn)
    self.timers[#self.timers + 1] = { t = delay, fn = fn }
end

--- 统一的"受伤 + 计分转发"入口（连锁爆炸走这条；玩家子弹由 Shooting 自己转发，不重复）。
function Enemy:Damage(unit, amount, hitPos, headshot)
    if not unit.alive then
        return nil
    end
    local result = unit:TakeDamage(amount, hitPos, headshot)
    if result and result.score and result.score > 0 and self.onScore then
        -- Enemy:Damage 只服务连锁/爆炸（玩家子弹由 Shooting 自己转发），故恒标 fromChain=true：
        -- 这些击杀计分、涨 combo，但不再触发新爆炸（切断自激）。
        self.onScore(result, unit, headshot, true)
    end
    return result
end

--- combo 触发的一次范围爆炸：在 centerPos 放一圈 AoE，杀伤 COMBO_BLAST_RADIUS 内的所有单位
--- （含爆桶，可引发连锁）。复用爆桶连锁那套“距离遍历 + Schedule 递延”，避免与当前这次击杀
--- 结算同帧递归引爆（爆炸伤害→onScore→AddKill 可能再触爆炸，全部递延到下帧计时器，不会无限叠加）。
---@param centerPos Vector3
function Enemy:ComboBlast(centerPos)
    self.fx:Spawn("explosion", centerPos)
    self.fx:Flash(centerPos, Color(1.0, 0.8, 0.35), 16.0, 0.16)
    self.audio:Play("explosion")
    local mgr = self
    mgr:Schedule(Config.BARREL_CHAIN_DELAY, function()
        for _, other in ipairs(mgr.units) do
            if other.alive and other.node then
                local d = (other.node:GetWorldPosition() - centerPos):Length()
                if d <= Config.COMBO_BLAST_RADIUS then
                    mgr:Damage(other, Config.COMBO_BLAST_DAMAGE, other.node:GetWorldPosition(), false)
                end
            end
        end
    end)
end

----------------------------------------------------------------------
-- 单位骨架
----------------------------------------------------------------------

local SCORE_BY_KIND = {
    boat = "SCORE_BOAT",
    bigboat = "SCORE_BOAT_BIG",
    pirate = "SCORE_PIRATE",
    barrel = "SCORE_BARREL",
}

--- 建一个单位表并挂到节点上。kind-specific 的初始化在各自 Spawn 里补。
---@param mgr Enemy
---@param kind string
---@param node Node
local function newUnit(mgr, kind, node)
    local unit = {
        mgr = mgr,
        kind = kind,
        node = node,
        alive = true,
        removing = false,
        removeT = 0.0,
        hp = 1,
        scoreBase = Config[SCORE_BY_KIND[kind]] or 0,
        state = "idle",
    }
    -- 单位表留在 Lua 侧 self.units（见文件头：SetVar 存不了 table、AddTag 收 eastl String）。
    -- 射线命中后由 Enemy:ResolveUnit(node) 用节点指针相等反查。

    --- 受伤。headshot 时伤害翻倍由调用方传入的 amount 决定；分数按 headshot 再翻倍。
    function unit:TakeDamage(amount, hitPos, headshot)
        if not self.alive then
            return nil
        end
        self.hp = self.hp - (amount or 1)
        if self.hp > 0 then
            return { killed = false, score = 0 }
        end
        self.alive = false
        local score = self.scoreBase
        if headshot then
            score = score * Config.HEADSHOT_MULTIPLIER
        end
        self:OnKilled(hitPos)
        return { killed = true, score = score }
    end

    -- OnKilled / Update 由各 kind 覆盖
    function unit:OnKilled(hitPos) end
    function unit:Update(dt) end

    return unit
end

--- 进入移除倒计时。
local function startRemove(unit, delay)
    unit.removing = true
    unit.removeT = delay or 0.0
end

----------------------------------------------------------------------
-- 船
----------------------------------------------------------------------

---@param kind string "boat" | "bigboat"
---@param laneIndex integer Config.LANES 的下标
function Enemy:SpawnBoat(kind, laneIndex)
    local big = (kind == "bigboat")
    local laneX = Config.LANES[laneIndex] or Config.LANES[1]

    local node = self.root:CreateChild(big and "BigBoat" or "Boat")
    node:SetPosition(Vector3(laneX, Config.SEA_Y - Config.BOAT_DRAFT, Config.BOAT_SPAWN_Z))
    node:SetRotation(Quaternion(Config.BOAT_YAW, Vector3.UP))
    -- 船体走 prefab：旧 Quaternius Ship_Small/Ship_Large 已连资产删除（Ship_Large 三角面太多），
    -- 换成同风格 kenney prefab。prefab 自带模型引用 + kennypirate.material + 投影，所以不再
    -- CreateComponent("StaticModel")/SetMaterial；实例化出的子节点继承本节点的位置/朝向，
    -- 吃水、偏航、沉船翻滚仍由 unit.node 统一驱动，视觉缩放也只加在子节点上（不连累
    -- 相对逻辑节点的吃水/瞄点）。
    local prefabName = big and "ship-pirate-large" or "ship-row-large"
    local prefab = GetResource("PrefabResource", Config.PiratePrefab(prefabName))
    if prefab then
        local hull = node:InstantiatePrefab(prefab, Vector3.ZERO, Quaternion.IDENTITY)
        if hull then
            hull:SetScale(big and Config.BOAT_BIG_SCALE or Config.BOAT_SMALL_SCALE)
        end
    else
        LogError("Enemy: 船 prefab 加载失败 '" .. Config.PiratePrefab(prefabName) .. "'")
    end

    local unit = newUnit(self, kind, node)
    unit.hp = big and Config.BOAT_BIG_HP or Config.BOAT_HP
    unit.speed = big and Config.BOAT_BIG_SPEED or Config.BOAT_SPEED
    unit.carry = big and Config.BOAT_BIG_CARRY or Config.BOAT_CARRY
    unit.remaining = unit.carry
    unit.unloadT = 0.0
    unit.state = "sail"
    unit.laneX = laneX
    unit.big = big                                   -- 大船才架炮台
    if big then
        unit.cannonT = Config.BOAT_CANNON_FIRST      -- 靠岸后首发炮弹倒计（只在 unload/bombard 里计时）
    end

    --- 大船炮台计时开火：到点从甲板抛一发炮弹向岸上随机落点。
    function unit:TickCannon(dt)
        self.cannonT = self.cannonT - dt
        if self.cannonT <= 0.0 then
            self.cannonT = randSpread(Config.BOAT_CANNON_INTERVAL, Config.BOAT_CANNON_INTERVAL_SPREAD)
            local deck = self.node:GetWorldPosition()
            deck.y = deck.y + Config.BOAT_BIG_HIT_HEIGHT   -- 从甲板高度出膛，不要从船底（本函数只跑在大船上）
            self.mgr:FireCannon(deck)
        end
    end

    function unit:Update(dt)
        if self.state == "sail" then
            local p = self.node:GetPosition()
            p.z = p.z + self.speed * dt
            self.node:SetPosition(p)
            -- 大船一出场就边驶边炮击（不等靠岸，否则大半在途中就被集火掉、看不到开炮）
            if self.big then
                self:TickCannon(dt)
            end
            if p.z >= Config.BOAT_STOP_Z then
                self.state = "unload"
                self.unloadT = 0.0
            end
        elseif self.state == "unload" then
            self.unloadT = self.unloadT - dt
            if self.unloadT <= 0.0 and self.remaining > 0 then
                self.unloadT = Config.BOAT_UNLOAD_TIME
                self.remaining = self.remaining - 1
                local p = self.node:GetPosition()
                self.mgr:SpawnPirate(Vector3(p.x + randSpread(0.0, 1.2), 0.0, p.z))
                if self.remaining <= 0 then
                    if self.big then
                        -- 大船卸完兵不立即走，转 bombard 滞留往岸上炮击（先打大船的战术理由）
                        self.state = "bombard"
                        self.departT = Config.BOAT_CANNON_LINGER
                    else
                        startRemove(self, 0.0)
                    end
                end
            end
            if self.big then
                self:TickCannon(dt)
            end
        elseif self.state == "bombard" then
            self:TickCannon(dt)
            self.departT = self.departT - dt
            if self.departT <= 0.0 then
                -- 炮击够了，离开（占位阶段直接移除，不做驶离动画）
                startRemove(self, 0.0)
            end
        elseif self.state == "sinking" then
            -- 下沉 + 侧倾，给击沉一点分量（removeT 由管理器统一递减，这里不碰）
            local p = self.node:GetPosition()
            p.y = p.y - dt * 0.9
            self.node:SetPosition(p)
            self.node:Roll(dt * 22.0)
        end
    end

    function unit:OnKilled(hitPos)
        self.state = "sinking"
        self.mgr.fx:Spawn("explosion", self.node:GetWorldPosition())
        self.mgr.fx:Spawn("splash", self.node:GetWorldPosition())
        self.mgr.audio:Play("breakWood")
        startRemove(self, Config.BOAT_SINK_TIME)
    end

    self.units[#self.units + 1] = unit
    return unit
end

----------------------------------------------------------------------
-- 海盗
----------------------------------------------------------------------

---@param spawnPos Vector3 x/z 用得上，y 由涉水/上岸逻辑接管
---@param skin string|nil 缺省随机
function Enemy:SpawnPirate(spawnPos, skin)
    -- 突袭型按概率摇中；未显式指定皮肤时，突袭型用稀有皮做视觉区分。
    local raider = math.random() < Config.PIRATE_RAIDER_CHANCE
    if not skin then
        skin = raider and pickRaiderSkin() or pickSkin()
    end
    local node = self.root:CreateChild("Pirate")
    local y = Config.SEA_Y + Config.PIRATE_WADE_DEPTH
    node:SetPosition(Vector3(spawnPos.x, y, spawnPos.z))
    node:SetRotation(Quaternion(Config.PIRATE_YAW, Vector3.UP))

    -- 视觉降级为序列帧 billboard：海盗不再挂 AnimatedModel/AnimationController（不再逐实例蒙皮），
    -- 改由 FlipbookCrowd 在该皮肤的 BillboardSet 里占一张卡片、逐帧改写 UV 切帧。node 仅保留世界
    -- 位置与朝向，承载全部玩法数学（自动吸附/子弹扫掠/溅射/移除倒计时都读 u.node）。billboard 恒朝
    -- 相机，故 node 旋转不影响视觉。
    local sprite = self.crowd:Acquire(skin, node)
    self.crowd:Play(sprite, "Run")

    local unit = newUnit(self, "pirate", node)
    unit.hp = raider and Config.PIRATE_RAIDER_HP or Config.PIRATE_HP
    unit.raider = raider           -- 突袭型：Update 里转追主角而非直冲防线
    unit.turretIdx = nil           -- 冲塔型进入砍击时定下的目标塔号（扣血时回传给 Waves）
    unit.skin = skin
    unit.sprite = sprite
    unit.speed = randSpread(Config.PIRATE_SPEED, Config.PIRATE_SPEED_SPREAD)
    unit.state = "advance"
    unit.attackT = 0.0

    function unit:Update(dt)
        if self.state == "advance" then
            local p = self.node:GetPosition()
            local player = self.mgr.player
            -- 可追击：突袭型 & 主角在陆地（未上船出海、未阵亡）。主角一旦上船，突袭型
            -- 够不着，退回直冲防线分支（载具因此对突袭型有天然闪避价值）。
            local chase = self.raider and player and (not player.inBoat) and player.hp > 0
            if chase then
                -- 转追主角实时位置（走位变真决策：风筝 / 边退边打 / 绕位）
                local pp = player.spot
                local dx, dz = pp.x - p.x, pp.z - p.z
                local dist = math.sqrt(dx * dx + dz * dz)
                if dist <= Config.PIRATE_MELEE_RANGE then
                    self.state = "attack"
                    self.attackT = Config.PIRATE_ATTACK_TIME
                    self.mgr.crowd:Play(self.sprite, "Sword")
                else
                    local step = self.speed * dt
                    p.x = p.x + dx / dist * step
                    p.z = p.z + dz / dist * step
                    p.y = wadeY(p.x, p.z)
                    self.node:SetPosition(p)
                    -- 面向主角（角色模型原生朝 -z，与 Player 同一 yaw 约定）
                    local yaw = math.atan(dx, -dz)
                    self.node:SetRotation(Quaternion(-math.deg(yaw), Vector3.UP))
                end
            else
                -- 冲滩型（或主角已上船/阵亡）：直线 +z 朝滩头，抵近防线带后转打最近那座炮塔。
                local reachZ = Config.DEFENSE_Z - Config.TURRET_STAND_OFF
                if p.z >= reachZ - 1.0 and self.mgr.turrets then
                    -- 进塔前圈：锁最近的一座活塔并走到它的砍击点（海侧、距心 STAND_OFF）。
                    -- 塔在 x=±9、航道在 ±13/±4.5，正对着塔的只有少数几条，不在这里转过去
                    -- 就会当着两塔中间对空气挥刀、却算砍到塔——视觉与判据脱节。
                    local idx, tx, tz = self.mgr.turrets:NearestAlive(p.x, p.z)
                    if not idx then
                        -- 塔全哑了（Waves 下一帧就判负），别再拖：直接同归于尽收尸
                        self.alive = false
                        self:PlayDeath()
                    else
                        local dx, dz = tx - p.x, tz - p.z
                        local dist = math.sqrt(dx * dx + dz * dz)
                        if dist <= 0.35 then
                            self.state = "attack"
                            self.attackT = Config.PIRATE_ATTACK_TIME
                            self.turretIdx = idx
                            self.mgr.crowd:Play(self.sprite, "Sword")
                        else
                            -- 塔在正内陆侧，所以这一步是"向内陆挨过去"；billboard 恒朝相机，
                            -- 旋转不影响观感，这里就不调了。
                            local step = self.speed * dt
                            p.x = p.x + dx / dist * step
                            p.z = p.z + dz / dist * step
                            p.y = wadeY(p.x, p.z)
                            self.node:SetPosition(p)
                        end
                    end
                else
                    -- 直线冲滩：x 恒在航道上，只有 z 在动，所以“涉水→贴沙”的切换点就是
                    -- 这条航道自己撞岸那一处（四条航道各不相同）——这正是曲线岸线顺手给的好处。
                    p.z = p.z + self.speed * dt
                    p.y = wadeY(p.x, p.z)
                    self.node:SetPosition(p)
                end
            end
        elseif self.state == "attack" then
            self.attackT = self.attackT - dt
            local player = self.mgr.player
            if self.raider then
                -- 突袭型：目标不可达（上船/阵亡）→ 退回直冲防线；被风筝拉开 → 退回追击。
                -- 否则每 PIRATE_ATTACK_TIME 砍一刀并循环（不自杀，必须玩家主动点掉）。
                local reachable = player and (not player.inBoat) and player.hp > 0
                if not reachable then
                    self.state = "advance"
                    self.mgr.crowd:Play(self.sprite, "Run")
                else
                    local p = self.node:GetPosition()
                    local dx, dz = player.spot.x - p.x, player.spot.z - p.z
                    local leash = Config.PIRATE_MELEE_RANGE * 1.9
                    if dx * dx + dz * dz > leash * leash then
                        self.state = "advance"
                        self.mgr.crowd:Play(self.sprite, "Run")
                    elseif self.attackT <= 0.0 then
                        self.attackT = Config.PIRATE_ATTACK_TIME
                        self.mgr.crowd:Play(self.sprite, "Sword")
                        if self.mgr.onPlayerHit and not player:IsInvuln() then
                            self.mgr.onPlayerHit(Config.PIRATE_PLAYER_DAMAGE)
                        end
                    end
                end
            elseif self.attackT <= 0.0 then
                -- 冲塔型：一刀砍到塔即与塔同归于尽（占位：不做回撤）。扣的是这座塔自己的血，
                -- 塔号在进塔前圈时就定下了（不在这里重新选，否则两边可能选到不同的塔）。
                if self.mgr.onTurretHit and self.turretIdx then
                    self.mgr.onTurretHit(self.turretIdx, Config.LEAK_DAMAGE)
                end
                self.alive = false
                self:PlayDeath()
            end
        end
        -- state == "dead"：只等移除倒计时，由管理器统一递减，这里不做任何事
    end

    function unit:PlayDeath()
        self.state = "dead"
        self.mgr.crowd:Play(self.sprite, "Death")
        startRemove(self, Config.PIRATE_DEATH_TIME)
    end

    function unit:OnKilled(hitPos)
        self:PlayDeath()
    end

    self.units[#self.units + 1] = unit
    return unit
end

----------------------------------------------------------------------
-- 爆桶
----------------------------------------------------------------------

---@param pos Vector3
function Enemy:SpawnBarrel(pos)
    local node = self.root:CreateChild("Barrel")
    node:SetPosition(pos)
    local model = node:CreateComponent("StaticModel")
    model:SetModel(GetResource("Model", Config.PirateModel("Prop_Barrel")))
    model:SetMaterial(Materials.Pirate())
    model:SetCastShadows(true)

    local unit = newUnit(self, "barrel", node)
    unit.hp = Config.BARREL_HP
    unit.state = "idle"

    function unit:OnKilled(hitPos)
        local center = self.node:GetWorldPosition()
        self.mgr.fx:Spawn("explosion", center)
        self.mgr.fx:Spawn("smoke", center)
        self.mgr.fx:Flash(center, Color(1.0, 0.7, 0.3), 12.0, 0.12)
        self.mgr.audio:Play("explosion")
        startRemove(self, 0.05)

        -- 连锁：递延一帧再对半径内单位造成伤害，让爆炸看起来是一串而不是一帧全炸
        local mgr = self.mgr
        mgr:Schedule(Config.BARREL_CHAIN_DELAY, function()
            for _, other in ipairs(mgr.units) do
                if other.alive and other.node and other ~= self then
                    local d = (other.node:GetWorldPosition() - center):Length()
                    if d <= Config.BARREL_RADIUS then
                        mgr:Damage(other, Config.BARREL_DAMAGE,
                            other.node:GetWorldPosition(), false)
                    end
                end
            end
        end)
    end

    self.units[#self.units + 1] = unit
    return unit
end

--- 按 Config.BARREL_SPOTS 摆死桶（关卡开始时调一次）。
function Enemy:SpawnStaticBarrels()
    for _, spot in ipairs(Config.BARREL_SPOTS) do
        self:SpawnBarrel(Vector3(spot[1], spot[2], spot[3]))
    end
end

----------------------------------------------------------------------
-- 大船炮台：炮弹（抛物线 + 落点预警）
----------------------------------------------------------------------

--- 从 fromPos 向岸上随机落点开一炮：建炮弹实体 + 地面预警盘，登记到 projectiles。
--- 落点在主角可走沙滩矩形内随机（“方向随机”=覆盖式炮击，不瞭准主角）。
---@param fromPos Vector3 炮口世界坐标（甲板）
function Enemy:FireCannon(fromPos)
    local tx = Config.WALK_MIN_X + math.random() * (Config.WALK_MAX_X - Config.WALK_MIN_X)
    -- 靠海一侧的边界按这一列的岸线算（旧写法是读一个写死的 1.5，那是“直水线 -0.5
    -- + 余量”的抄值）：拿常数当统一边界，要么在岬角那侧把炮弹丢进水里（那片玩家根本不
    -- 能站，覆盖白给），要么在湾里盖到岸上来。逐列算才和走位边界一致。
    local zmin = Config.ShoreZ(tx) + Config.WALK_SHORE_MARGIN
    local tz = zmin + math.random() * (Config.WALK_MAX_Z - zmin)
    local toPos = Vector3(tx, Config.SAND_Y, tz)

    -- 开炮反馈：炮口光 + 炮声（专用 cannonShot，由 powerful-cannon-shot 转码而来）
    self.fx:Flash(fromPos, Color(1.0, 0.8, 0.4), 8.0, 0.08)
    self.audio:Play("cannonShot")

    -- 炮弹实体：pirate-kit 的 cannon-ball prefab（自带 kennypirate 材质）。ball 仍是父节点，
    -- 飞行/删除沿用 pr.ball:SetPosition / Remove，模型作子节点挂原点、按需缩放并关掉投影。
    local ball = self.scene:CreateChild("CannonBall")
    ball:SetPosition(fromPos)
    local prefab = GetResource("PrefabResource", Config.PiratePrefab("cannon-ball"))
    if prefab then
        local model = ball:InstantiatePrefab(prefab, Vector3.ZERO, Quaternion.IDENTITY)
        if model then
            model:SetScale(Config.CANNON_BALL_SCALE)
            local smc = model:GetComponent("StaticModel")
            if smc then
                smc:SetCastShadows(false)
            end
        end
    else
        LogError("Enemy: 炮弹 prefab 加载失败 'cannon-ball'")
    end

    -- 落点预警盘：扁 Box 铺在沙滩上，随炮弹逼近放大（提示“别站在这”）。
    -- 尺寸故意不小于杀伤盘：玩家只要离开橙色盘就一定安全。
    local mark = self.scene:CreateChild("CannonWarn")
    mark:SetPosition(Vector3(tx, Config.SAND_Y + 0.05, tz))
    local msm = mark:CreateComponent("StaticModel")
    msm:SetModel(GetResource("Model", Config.BOX_MODEL))
    msm:SetMaterial(Materials.Tint("cannonWarn"))
    msm:SetCastShadows(false)
    mark:SetScale(Vector3(0.5, 0.08, 0.5))

    self.projectiles[#self.projectiles + 1] = {
        ball = ball, mark = mark, from = fromPos, to = toPos, t = 0.0, dur = Config.CANNON_FLIGHT_TIME,
    }
end

--- 每帧推进在飞炮弹：沿抛物线插值 + 预警盘长大；到时结算落地。
---@param dt number
function Enemy:UpdateProjectiles(dt)
    local list = self.projectiles
    for i = #list, 1, -1 do
        local pr = list[i]
        pr.t = pr.t + dt
        local u = pr.t / pr.dur
        if u >= 1.0 then
            self:CannonImpact(pr)
            list[i] = list[#list]
            list[#list] = nil
        else
            -- 水平线性、竖直抛物：4u(1-u) 在 u=0.5 取峰 1，峰值恰为 LOB_HEIGHT，u=1 回到落点 y。
            local x = pr.from.x + (pr.to.x - pr.from.x) * u
            local z = pr.from.z + (pr.to.z - pr.from.z) * u
            local y = pr.from.y + (pr.to.y - pr.from.y) * u + Config.CANNON_LOB_HEIGHT * 4.0 * u * (1.0 - u)
            pr.ball:SetPosition(Vector3(x, y, z))
            local s = 0.3 + 0.7 * u
            pr.mark:SetScale(Vector3(Config.CANNON_SPLASH_RADIUS * 2.0 * s, 0.08, Config.CANNON_SPLASH_RADIUS * 2.0 * s))
        end
    end
end

--- 炮弹落地：爆炸特效 + 音效，并对杀伤半径内的主角结算大伤（XZ 水平距离，对齐项目语义）。
---@param pr table
function Enemy:CannonImpact(pr)
    pr.ball:Remove()
    pr.mark:Remove()
    local ip = pr.to
    self.fx:Spawn("explosion", Vector3(ip.x, Config.SAND_Y + 0.4, ip.z))
    self.fx:Spawn("smoke", Vector3(ip.x, Config.SAND_Y + 0.6, ip.z))
    self.fx:Flash(Vector3(ip.x, Config.SAND_Y + 0.5, ip.z), Color(1.0, 0.6, 0.25), 14.0, 0.14)
    self.audio:Play("cannonBlast")

    local player = self.player
    if player and player.hp and player.hp > 0 and not player.dying and not player:IsInvuln() then
        local dx = player.spot.x - ip.x
        local dz = player.spot.z - ip.z
        if dx * dx + dz * dz <= Config.CANNON_SPLASH_RADIUS * Config.CANNON_SPLASH_RADIUS then
            if self.onPlayerHit then
                self.onPlayerHit(Config.CANNON_PLAYER_DAMAGE)
            end
        end
    end

    -- 同一次爆炸若罩住宠物狗（独立于 units 的友方伴随单位），把狗炸飞上天翻滚。狗不死，
    -- 纯搞笑反馈；判定与上面主角一致用 XZ 水平距离、复用同一杀伤半径。
    if self.dog then
        self.dog:HitByExplosion(Vector3(ip.x, Config.SAND_Y, ip.z), Config.CANNON_SPLASH_RADIUS)
    end
end

----------------------------------------------------------------------
-- 帧更新
----------------------------------------------------------------------

---@param dt number
function Enemy:Update(dt)
    -- 延时队列
    local timers = self.timers
    for i = #timers, 1, -1 do
        local entry = timers[i]
        entry.t = entry.t - dt
        if entry.t <= 0.0 then
            timers[i] = timers[#timers]
            timers[#timers] = nil
            entry.fn()
        end
    end

    -- 单位：倒序遍历，移除时 swap-pop
    local units = self.units
    for i = #units, 1, -1 do
        local unit = units[i]
        unit:Update(dt)
        if unit.removing then
            unit.removeT = unit.removeT - dt
            if unit.removeT <= 0.0 then
                if unit.sprite then
                    self.crowd:Release(unit.sprite)   -- 归还 billboard 槽位（仅海盗有）
                    unit.sprite = nil
                end
                if unit.node then
                    unit.node:Remove()
                end
                units[i] = units[#units]
                units[#units] = nil
            end
        end
    end

    -- 序列帧海盗：在各单位 Update 摆好 node 世界位置后，统一推进 clip 时间并同步 billboard 位置/UV。
    self.crowd:Update(dt)

    -- 大船炮台的在飞炮弹（独立于 units：船开走/沉没，已发射的炮弹照样落地）
    self:UpdateProjectiles(dt)
end

--- 清空所有单位与计时器（换局/停止）。
function Enemy:Clear()
    for _, unit in ipairs(self.units) do
        if unit.sprite then
            self.crowd:Release(unit.sprite)
            unit.sprite = nil
        end
        if unit.node then
            unit.node:Remove()
        end
    end
    self.crowd:Reset()
    self.units = {}
    self.timers = {}
    for _, pr in ipairs(self.projectiles) do
        if pr.ball then pr.ball:Remove() end
        if pr.mark then pr.mark:Remove() end
    end
    self.projectiles = {}
end

return Enemy

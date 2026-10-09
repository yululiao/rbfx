-- Scripts/game/Enemy.lua
--
-- 敌人单位与生成器：船（驶来→靠岸吐海盗→离开/被击沉）、海盗（涉水冲滩→砍防线）、
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

----------------------------------------------------------------------
-- 管理器
----------------------------------------------------------------------

---@param deps { scene: Scene, fx: Fx, audio: Audio, player: Player, onScore: fun(result:any,unit:any,headshot:boolean)|nil, onDefenseHit: fun(dmg:number)|nil }
---@return Enemy
function Enemy.Build(deps)
    local self = setmetatable({}, Enemy)
    self.scene = deps.scene
    self.fx = deps.fx
    self.audio = deps.audio
    self.player = deps.player
    self.onScore = deps.onScore
    self.onDefenseHit = deps.onDefenseHit
    self.root = deps.scene:CreateChild("Enemies")
    ---@type table[]
    self.units = {}
    ---@type { t: number, fn: function }[]
    self.timers = {}
    return self
end

---@return table[] 存活与待移除的单位（Shooting 磁吸只挑 alive 的）
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
            local p = u.node:GetWorldPosition()
            local dx, dz = p.x - fromPos.x, p.z - fromPos.z
            local d = math.sqrt(dx * dx + dz * dz)
            if d <= bestD then
                best, bestD, bestPos = u, d, p
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
        self.onScore(result, unit, headshot)
    end
    return result
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
    local asset = big and "Ship_Large" or "Ship_Small"
    local laneX = Config.LANES[laneIndex] or Config.LANES[1]

    local node = self.root:CreateChild(big and "BigBoat" or "Boat")
    node:SetPosition(Vector3(laneX, Config.SEA_Y - Config.BOAT_DRAFT, Config.BOAT_SPAWN_Z))
    node:SetRotation(Quaternion(Config.BOAT_YAW, Vector3.UP))
    local model = node:CreateComponent("StaticModel")
    model:SetModel(GetResource("Model", Config.PirateModel(asset)))
    model:SetMaterial(Materials.Pirate())
    model:SetCastShadows(true)

    local unit = newUnit(self, kind, node)
    unit.hp = big and Config.BOAT_BIG_HP or Config.BOAT_HP
    unit.speed = big and Config.BOAT_BIG_SPEED or Config.BOAT_SPEED
    unit.carry = big and Config.BOAT_BIG_CARRY or Config.BOAT_CARRY
    unit.remaining = unit.carry
    unit.unloadT = 0.0
    unit.state = "sail"
    unit.laneX = laneX

    function unit:Update(dt)
        if self.state == "sail" then
            local p = self.node:GetPosition()
            p.z = p.z + self.speed * dt
            self.node:SetPosition(p)
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
                    -- 吐完人，船离开（占位阶段直接移除，不做驶离动画）
                    startRemove(self, 0.0)
                end
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
    skin = skin or pickSkin()
    local node = self.root:CreateChild("Pirate")
    local y = Config.SEA_Y + Config.PIRATE_WADE_DEPTH
    node:SetPosition(Vector3(spawnPos.x, y, spawnPos.z))
    node:SetRotation(Quaternion(Config.PIRATE_YAW, Vector3.UP))

    local model = node:CreateComponent("AnimatedModel")
    model:SetModel(GetResource("Model", Config.PirateModel(skin)))
    model:SetMaterial(Materials.Pirate())
    model:SetCastShadows(true)
    -- 远处降频：不可见时不更新蒙皮（同屏上限之外的开销主要在这）
    model:SetUpdateInvisible(false)

    local anim = node:CreateComponent("AnimationController")
    anim:Play(Config.PirateAnim(skin, "Run"), 0, true, 0.2)

    local unit = newUnit(self, "pirate", node)
    unit.hp = Config.PIRATE_HP
    unit.skin = skin
    unit.anim = anim
    unit.speed = randSpread(Config.PIRATE_SPEED, Config.PIRATE_SPEED_SPREAD)
    unit.state = "advance"
    unit.attackT = 0.0

    function unit:Update(dt)
        if self.state == "advance" then
            local p = self.node:GetPosition()
            p.z = p.z + self.speed * dt
            -- 涉水时抬高一点，上岸落到沙面
            p.y = (p.z < Config.SHORE_Z) and (Config.SEA_Y + Config.PIRATE_WADE_DEPTH) or Config.SAND_Y
            self.node:SetPosition(p)
            if p.z >= Config.DEFENSE_Z then
                self.state = "attack"
                self.attackT = Config.PIRATE_ATTACK_TIME
                self.anim:Play(Config.PirateAnim(self.skin, "Sword"), 0, false, 0.1)
            end
        elseif self.state == "attack" then
            self.attackT = self.attackT - dt
            if self.attackT <= 0.0 then
                -- 砍到防线：防线掉血，海盗也随之"阵亡"（占位：不做回撤）
                if self.mgr.onDefenseHit then
                    self.mgr.onDefenseHit(Config.LEAK_DAMAGE)
                end
                self.alive = false
                self:PlayDeath()
            end
        end
        -- state == "dead"：只等移除倒计时，由管理器统一递减，这里不做任何事
    end

    function unit:PlayDeath()
        self.state = "dead"
        self.anim:Play(Config.PirateAnim(self.skin, "Death"), 0, false, 0.1)
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
                if unit.node then
                    unit.node:Remove()
                end
                units[i] = units[#units]
                units[#units] = nil
            end
        end
    end
end

--- 清空所有单位与计时器（换局/停止）。
function Enemy:Clear()
    for _, unit in ipairs(self.units) do
        if unit.node then
            unit.node:Remove()
        end
    end
    self.units = {}
    self.timers = {}
end

return Enemy

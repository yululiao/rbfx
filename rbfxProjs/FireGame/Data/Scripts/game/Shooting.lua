-- Scripts/game/Shooting.lua
--
-- 全自动开火 + 子弹实体。
-- 冷却完毕且射程内有敌人就自动开火，玩家只需走位。子弹是 CoreData 球体，从枪口飞向目标，
-- 到达后结算伤害。弹道速度很快（视觉上能看清飞行）。
--
-- 目标选择：Enemy:NearestTarget(主角位置, ATTACK_RANGE) 取最近存活威胁（船/海盗，跳过爆桶）。
-- 装弹：打空 MAG_SIZE 发后自动装弹，装弹期间不开火。

local Config = require("game.Config")
local Materials = require("game.Materials")

---@class Shooting 全自动开火 + 子弹管理
local Shooting = {}
Shooting.__index = Shooting

-- 命中不同目标的反馈
local FEEDBACK = {
    pirate = { hit = { fx = "hitFlesh", snd = "hitFlesh" }, kill = { fx = "hitFlesh", snd = "killFlesh" } },
    boat = { hit = { fx = "hitWood", snd = "hitWood" }, kill = { fx = "explosion", snd = "explosion" } },
    bigboat = { hit = { fx = "hitWood", snd = "hitWood" }, kill = { fx = "explosion", snd = "explosion" } },
    barrel = { hit = { fx = "hitWood", snd = "hitWood" }, kill = { fx = "explosion", snd = "explosion" } },
}

local BULLET_MODEL = "Models/Sphere.mdl"
local BULLET_SCALE = 0.3
local BULLET_SPEED = 60.0   -- m/s，很快但视觉上能看清轨迹

---@param deps { scene: Scene, player: Player, fx: Fx, audio: Audio, hud: any, enemies: any, onScore: fun(result:any, unit:any, headshot:boolean, fromChain:boolean|nil)|nil }
---@return Shooting
function Shooting.Build(deps)
    local self = setmetatable({}, Shooting)
    self.scene = deps.scene
    self.player = deps.player
    self.fx = deps.fx
    self.audio = deps.audio
    self.hud = deps.hud
    self.enemies = deps.enemies
    self.onScore = deps.onScore

    self.ammo = Config.MAG_SIZE
    self.reloading = false
    self.reloadT = 0.0
    self.cooldown = 0.0
    self.bullets = {}   -- 活跃子弹列表
    -- 武器数值（扇形弹丸数 / 单发伤害）：开局 1 发 1 伤，由 game/Grow.lua 经
    -- SetWeaponStats 随成长爬升（main.lua 接线）。放在 bullets 之后、Reset 之前，
    -- 因为 Reset 不清它（重开由 grow:Reset 重新推 1 级形态）。
    self.pellets = 1
    self.damage = 1
    self.pierce = 1   -- 一颗子弹能贯穿的单位数（1=命中即消亡，旧行为）；随 GROWTH 爬升

    if self.hud and self.hud.SetAmmo then
        self.hud:SetAmmo(self.ammo)
    end
    return self
end

--- 每帧：冷却/装弹计时 + 自动开火判定 + 子弹更新。
---@param dt number
function Shooting:Update(dt)
    -- 冷却与装弹
    if self.cooldown > 0.0 then
        self.cooldown = self.cooldown - dt
        if self.cooldown < 0.0 then self.cooldown = 0.0 end
    end
    if self.reloading then
        self.reloadT = self.reloadT - dt
        if self.hud and self.hud.SetReload then
            self.hud:SetReload(1.0 - math.max(0.0, self.reloadT) / Config.RELOAD_TIME)
        end
        if self.reloadT <= 0.0 then
            self.reloading = false
            self.ammo = Config.MAG_SIZE
            if self.hud then
                if self.hud.SetAmmo then self.hud:SetAmmo(self.ammo) end
                if self.hud.SetReload then self.hud:SetReload(0.0) end
            end
        end
    end

    -- 全自动开火：冷却完毕且非装弹时，射程内有目标就自动射击
    if self.cooldown <= 0.0 and not self.reloading then
        if self.ammo <= 0 then
            self:StartReload()
        else
            local origin = self.player:GetFireOrigin()   -- 步行=枪口，船上=船头（角色在船上时是船子节点，不能用 GetNode 世界坐标）
            local unit, pos = self.enemies:NearestTarget(origin, Config.ATTACK_RANGE)
            if unit and pos then
                self:FireAt(unit, pos)
            end
        end
    end

    -- 子弹飞行更新
    self:UpdateBullets(dt)
end

function Shooting:StartReload()
    if self.reloading or self.ammo >= Config.MAG_SIZE then return end
    self.reloading = true
    self.reloadT = Config.RELOAD_TIME
    self.audio:Play("reload")
end

--- 成长系统推来的武器数值：扇形弹丸数 + 单发伤害 + 穿透。由 game/Grow.lua 的 onWeapon
--- 经 main.lua 调用（开局一次 + 每次升级）。弹匣容量不变（扇形一次只扣 1 发）。
---@param pellets number
---@param damage number
---@param pierce number|nil
function Shooting:SetWeaponStats(pellets, damage, pierce)
    self.pellets = pellets or 1
    self.damage = damage or 1
    self.pierce = pierce or 1
end

--- 对锁定目标开一枪：扣弹/冷却/kick/人物转向/枪口光/音效，生成子弹实体飞向目标。
---@param unit any
---@param targetPos Vector3
function Shooting:FireAt(unit, targetPos)
    self.ammo = self.ammo - 1
    self.cooldown = Config.FIRE_COOLDOWN
    self.player:SetAimTarget(targetPos)
    self.player:PlayShoot()
    self.audio:Play("shot")
    local muzzle = self.player:NextGunPos()   -- 船上从船头左/右炮位轮换出膛；步行=枪口
    self.fx:Flash(muzzle, Color(1.0, 0.85, 0.5), 6.0, 0.05)
    if self.hud and self.hud.SetAmmo then
        self.hud:SetAmmo(self.ammo)
    end

    -- 扇形喷出 pellets 颗（一次扳机只扣 1 发弹药）。单发时沿锁定方向直射（与旧手感
    -- 一致）；多发时以锁定方向为中心、在 SCATTER_SPREAD_DEG 张角内左右均匀铺开，
    -- 每颗各自直线飞出、撞到航道上任一敌人即结算（见 UpdateBullets 的扫掠命中）。
    local dist = (targetPos - muzzle):Length()
    local dir = (targetPos - muzzle):Normalized()
    local pellets = self.pellets or 1
    local pierce = self.pierce or 1
    -- 穿透弹要能一路飞过整条航道，故射程用 ATTACK_RANGE（非穿透时维持旧锁点距离）。
    local bulletRange = pierce > 1 and Config.ATTACK_RANGE or dist
    local spread = Config.SCATTER_SPREAD_DEG
    for i = 1, pellets do
        local dx, dy, dz = dir.x, dir.y, dir.z
        if pellets > 1 then
            -- 绕 Y 轴在 XZ 平面旋转（俯视下的左右扇面）；dy 保持直射仰角不变
            local a = math.rad(-spread * 0.5 + spread * (i - 0.5) / pellets)
            local c, s = math.cos(a), math.sin(a)
            dx = dir.x * c - dir.z * s
            dz = dir.x * s + dir.z * c
        end
        local bulletNode = self.scene:CreateChild("Bullet")
        bulletNode:SetPosition(muzzle)
        local sm = bulletNode:CreateComponent("StaticModel")
        sm:SetModel(GetResource("Model", BULLET_MODEL))
        sm:SetMaterial(Materials.Tint("bullet"))
        bulletNode:SetScale(BULLET_SCALE)
        table.insert(self.bullets, {
            node = bulletNode,
            dir = Vector3(dx, dy, dz),
            speed = BULLET_SPEED,
            traveled = 0.0,
            maxDist = bulletRange,
            hitsLeft = pierce,   -- 还能结算几个单位（降到 0 就销毁这颗子弹）
            hit = {},            -- 本颗已命中过的单位集合，防穿透时对同一单位逐帧重复结算
        })
    end

    if self.ammo <= 0 then
        self:StartReload()
    end
end

--- 子弹飞行与扫掠命中。子弹每帧沿方向推进后，检查航道上是否有未命中过的敌人进入
--- BULLET_HIT_RADIUS——撞到即结算。穿透（hitsLeft>1）时不销毁、继续飞，靠 b.hit 集记
--- 得避免同一单位被重复结算；hitsLeft 耗尽或飞满射程才销毁。于是扇形 + 穿透 =
--- 一次喷出的多颗子弹各串穿一整条线上的敌人（命中广度 × 贯穿深度的乘积）。
--- 无物理模块，故用距离遍历而非射线查询。
---@param dt number
function Shooting:UpdateBullets(dt)
    local r2 = Config.BULLET_HIT_RADIUS * Config.BULLET_HIT_RADIUS
    local units = self.enemies:GetUnits()
    for i = #self.bullets, 1, -1 do
        local b = self.bullets[i]
        local step = b.speed * dt
        local pos = b.node:GetPosition()
        local np = pos + b.dir * step
        b.node:SetPosition(np)
        b.traveled = b.traveled + step

        -- 扫掠：撞到航道上第一个“本颗还未命中过”的非爆桶单位即结算（爆桶不自动当靶，
        -- 保留“只在连锁中炸”的语义）。用 XZ 水平距离（与 NearestTarget 同口径）：子弹平飞
        -- 在枪口高度、敌人节点常记在脚底，若算进 dy 会白白吃掉半径导致漏判。
        local hitUnit = nil
        for _, u in ipairs(units) do
            if u.alive and u.node and u.kind ~= "barrel" and not b.hit[u] then
                local up = u.node:GetWorldPosition()
                local ddx = up.x - np.x
                local ddz = up.z - np.z
                if ddx * ddx + ddz * ddz <= r2 then
                    hitUnit = u
                    break
                end
            end
        end

        if hitUnit then
            self:DamageUnit(hitUnit, np, false)
            b.hit[hitUnit] = true
            b.hitsLeft = (b.hitsLeft or 1) - 1
            if b.hitsLeft <= 0 then
                b.node:Remove()
                table.remove(self.bullets, i)
            end
        elseif b.traveled >= b.maxDist then
            -- 未命中且飞满射程 → 销毁（落空的散射弹视觉上就是“喷过头”）
            b.node:Remove()
            table.remove(self.bullets, i)
        end
    end
end

--- 打中单位：结算伤害 + 反馈。
function Shooting:DamageUnit(unit, hitPos, headshot)
    if not unit.alive then return end
    local result = unit:TakeDamage(self.damage or 1, hitPos, false)
    local fb = FEEDBACK[unit.kind] or FEEDBACK.pirate
    local killed = result and result.killed
    local pick = killed and fb.kill or fb.hit

    self.fx:Spawn(pick.fx, hitPos)
    self.audio:Play(pick.snd)
    if self.hud and self.hud.FlashHit then
        self.hud:FlashHit(killed or headshot)
    end
    if result and result.score and result.score > 0 and self.onScore then
        -- 玩家子弹击杀：fromChain=false，它是唯一允许触发连杀爆炸的来源。
        self.onScore(result, unit, headshot, false)
    end
end

---@return number ammo, boolean reloading
function Shooting:GetAmmoState()
    return self.ammo, self.reloading
end

--- 新一局：补满弹药、清装弹、清理残留子弹。
function Shooting:Reset()
    self.ammo = Config.MAG_SIZE
    self.reloading = false
    self.reloadT = 0.0
    self.cooldown = 0.0
    for _, b in ipairs(self.bullets) do
        b.node:Remove()
    end
    self.bullets = {}
    if self.hud then
        if self.hud.SetAmmo then self.hud:SetAmmo(self.ammo) end
        if self.hud.SetReload then self.hud:SetReload(0.0) end
    end
end

return Shooting

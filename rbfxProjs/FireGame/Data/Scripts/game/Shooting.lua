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

---@param deps { scene: Scene, player: Player, fx: Fx, audio: Audio, hud: any, enemies: any, onScore: fun(result:any, unit:any, headshot:boolean)|nil }
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

--- 对锁定目标开一枪：扣弹/冷却/kick/人物转向/枪口光/音效，生成子弹实体飞向目标。
---@param unit any
---@param targetPos Vector3
function Shooting:FireAt(unit, targetPos)
    self.ammo = self.ammo - 1
    self.cooldown = Config.FIRE_COOLDOWN
    self.player:FaceTowards(targetPos)
    self.player:PlayShoot()
    self.audio:Play("shot")
    local muzzle = self.player:NextGunPos()   -- 船上从船头左/右炮位轮换出膛；步行=枪口
    self.fx:Flash(muzzle, Color(1.0, 0.85, 0.5), 6.0, 0.05)
    if self.hud and self.hud.SetAmmo then
        self.hud:SetAmmo(self.ammo)
    end

    -- 创建子弹实体
    local bulletNode = self.scene:CreateChild("Bullet")
    bulletNode:SetPosition(muzzle)
    local sm = bulletNode:CreateComponent("StaticModel")
    sm:SetModel(GetResource("Model", BULLET_MODEL))
    sm:SetMaterial(Materials.Tint("bullet"))
    bulletNode:SetScale(BULLET_SCALE)
    local dist = (targetPos - muzzle):Length()
    local dir = (targetPos - muzzle):Normalized()

    table.insert(self.bullets, {
        node = bulletNode,
        dir = dir,
        speed = BULLET_SPEED,
        target = unit,
        targetPos = targetPos,
        traveled = 0.0,
        maxDist = dist,
    })

    if self.ammo <= 0 then
        self:StartReload()
    end
end

--- 子弹飞行与命中检测。
---@param dt number
function Shooting:UpdateBullets(dt)
    for i = #self.bullets, 1, -1 do
        local b = self.bullets[i]
        local step = b.speed * dt
        local pos = b.node:GetPosition()
        b.node:SetPosition(pos + b.dir * step)
        b.traveled = b.traveled + step

        -- 到达目标距离：结算伤害并销毁
        if b.traveled >= b.maxDist then
            if b.target and b.target.alive then
                -- 用目标当前位置（敌人可能在动）
                local hitPos = b.target.node:GetPosition()
                self:DamageUnit(b.target, hitPos, false)
            end
            b.node:Remove()
            table.remove(self.bullets, i)
        end
    end
end

--- 打中单位：结算伤害 + 反馈。
function Shooting:DamageUnit(unit, hitPos, headshot)
    if not unit.alive then return end
    local result = unit:TakeDamage(1, hitPos, false)
    local fb = FEEDBACK[unit.kind] or FEEDBACK.pirate
    local killed = result and result.killed
    local pick = killed and fb.kill or fb.hit

    self.fx:Spawn(pick.fx, hitPos)
    self.audio:Play(pick.snd)
    if self.hud and self.hud.FlashHit then
        self.hud:FlashHit(killed or headshot)
    end
    if result and result.score and result.score > 0 and self.onScore then
        self.onScore(result, unit, headshot)
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

-- Scripts/game/Shooting.lua
--
-- 自动吸附开火（王者荣耀射手式普攻）：点一下攻击键，锁定主角周围射程内最近的敌人直接结算
-- 伤害，玩家不需要瞄准。瞄准相关的"拖拽转视角 / 屏幕中心射线 / 磁吸 / 准星 / 爆头"已整体移除——
-- 视角由 Player 固定朝海、人物自动转向，输入只剩"左摇杆移动 + 攻击键开火"。
--
-- 触发：HUD 的攻击按钮走 RmlUi data-event-click，回调调 Shooting:OnAttack()（见 main.lua / Hud.lua）。
-- 目标：Enemy:NearestTarget(主角位置, ATTACK_RANGE) 取最近存活威胁（船/海盗，跳过爆桶）。
--   没目标不放枪、不扣弹药（避免空放与"点了没反应"的困惑）；有目标则扣弹、进冷却、相机 kick、
--   人物转向目标、枪口暖光，再按 kind 播命中/击杀反馈。
--
-- 单位接口（Enemy.lua 实现）：unit = { node, alive, kind, TakeDamage(amount,hitPos,headshot) }
--   TakeDamage 返回 { killed=bool, score=number } 或 nil（已死）。自动吸附恒 headshot=false。

local Config = require("game.Config")

---@class Shooting 自动吸附开火
local Shooting = {}
Shooting.__index = Shooting

-- 命中不同目标的反馈：粒子 key + 音效 key（击杀用另一套更有分量的）
local FEEDBACK = {
    pirate = { hit = { fx = "hitFlesh", snd = "hitFlesh" }, kill = { fx = "hitFlesh", snd = "killFlesh" } },
    boat = { hit = { fx = "hitWood", snd = "hitWood" }, kill = { fx = "explosion", snd = "explosion" } },
    bigboat = { hit = { fx = "hitWood", snd = "hitWood" }, kill = { fx = "explosion", snd = "explosion" } },
    barrel = { hit = { fx = "hitWood", snd = "hitWood" }, kill = { fx = "explosion", snd = "explosion" } },
}

---@param deps { scene: Scene, player: Player, fx: Fx, audio: Audio, hud: any, enemies: any, onScore: fun(result:any, unit:any, headshot:boolean)|nil }
---@return Shooting
function Shooting.Build(deps)
    local self = setmetatable({}, Shooting)
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

    if self.hud and self.hud.SetAmmo then
        self.hud:SetAmmo(self.ammo)
    end
    return self
end

--- 每帧：只跑冷却与装弹计时。开火由 HUD 攻击键触发（OnAttack），这里不读输入。
---@param dt number
function Shooting:Update(dt)
    if self.cooldown > 0.0 then
        self.cooldown = self.cooldown - dt
        if self.cooldown < 0.0 then
            self.cooldown = 0.0
        end
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
end

function Shooting:StartReload()
    if self.reloading or self.ammo >= Config.MAG_SIZE then
        return
    end
    self.reloading = true
    self.reloadT = Config.RELOAD_TIME
    self.audio:Play("reload")
end

--- 攻击键入口：冷却/装弹中忽略；空弹则干火并装弹；射程内没目标不放枪（不浪费弹药）。
function Shooting:OnAttack()
    if self.reloading or self.cooldown > 0.0 then
        return
    end
    if self.ammo <= 0 then
        self.audio:Play("dryFire")
        self:StartReload()
        return
    end

    local origin = self.player:GetNode():GetPosition()
    local unit, pos = self.enemies:NearestTarget(origin, Config.ATTACK_RANGE)
    if not unit or not pos then
        return -- 场上射程内没敌人：不扣弹药、不进冷却
    end
    self:FireAt(unit, pos)
end

--- 对锁定目标开一枪：扣弹/冷却/kick/人物转向/枪口光/命中结算，打完空弹自动装弹。
---@param unit any
---@param hitPos Vector3
function Shooting:FireAt(unit, hitPos)
    self.ammo = self.ammo - 1
    self.cooldown = Config.FIRE_COOLDOWN
    self.player:AddKick()
    self.player:FaceTowards(hitPos)
    self.audio:Play("shot")
    self.fx:Flash(self.player:GetMuzzleWorld(), Color(1.0, 0.85, 0.5), 6.0, 0.05)
    if self.hud and self.hud.SetAmmo then
        self.hud:SetAmmo(self.ammo)
    end

    self:DamageUnit(unit, hitPos, false)

    if self.ammo <= 0 then
        self:StartReload()
    end
end

--- 打中单位：结算伤害 + 反馈（粒子/音效/hitmarker/计分）。
function Shooting:DamageUnit(unit, hitPos, headshot)
    if not unit.alive then
        return
    end
    local result = unit:TakeDamage(headshot and 2 or 1, hitPos, headshot)
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

--- 新一局：补满弹药、清装弹状态。
function Shooting:Reset()
    self.ammo = Config.MAG_SIZE
    self.reloading = false
    self.reloadT = 0.0
    self.cooldown = 0.0
    if self.hud then
        if self.hud.SetAmmo then self.hud:SetAmmo(self.ammo) end
        if self.hud.SetReload then self.hud:SetReload(0.0) end
    end
end

return Shooting

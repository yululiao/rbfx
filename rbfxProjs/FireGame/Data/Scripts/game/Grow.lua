-- Scripts/game/Grow.lua
--
-- 局内成长：击杀攒经验 → 升级 → 扇形弹丸数/单发伤害沿 Config.GROWTH 爬升。
-- 这是"割草爽感"公式里的"力量可见增长"项——开局单发点名，越打越宽，通关前喷成
-- 5 发的扇形弹幕（pellets 恒取奇数 1/3/5，保证正中弹必命中，见 Config.GROWTH）。
--
-- 边界：本模块只管"数值随进程变化"这一件事，不碰任何战斗判定/命中/结算。它对外
-- 只产出两样东西：
--   * 一组武器数值（pellets/damage）经 deps.onWeapon 交给 Shooting（main.lua 接线）；
--   * 一组显示数值（等级 + 经验条进度 + 武器文本）直接推给 Hud。
-- 升级时机用 onWeapon 回调暴露，main.lua 借它做"升级！弹丸 xN"旁白 + 震屏。

local Config = require("game.Config")

---@class Grow 局内成长（经验/等级/武器数值曲线）
local Grow = {}
Grow.__index = Grow

---@param deps { hud: Hud|nil, onWeapon: fun(snapshot: { pellets: number, damage: number }, isInitial: boolean)|nil }
---@return Grow
function Grow.Build(deps)
    local self = setmetatable({}, Grow)
    deps = deps or {}
    self.hud = deps.hud
    self.onWeapon = deps.onWeapon
    self.levels = Config.GROWTH
    self:Reset()
    return self
end

--- 重开一局：清零经验回到 1 级形态，并把初始武器数值推下去。
function Grow:Reset()
    self.xp = 0
    self.level = 1
    self:_Notify(true)
end

--- 当前等级对应的武器数值快照（供 Shooting 读，也供 main 判断是否变强）。
---@return { pellets: number, damage: number, pierce: number }
function Grow:Snapshot()
    local g = self.levels[self.level]
    return { pellets = g.pellets, damage = g.damage, pierce = g.pierce or 1 }
end

--- 一次击杀喂经验（main.lua 的 OnScore 在 killed 分支调）。
--- 一次击杀（尤其大船 / 爆桶连锁群杀）可能跨多级，故 while 一路升到不能再升或封顶。
---@param kind string|nil pirate/boat/bigboat/barrel（unit.kind）
---@return boolean leveled 本次是否发生了升级
function Grow:AddKill(kind)
    local gain = kind and Config.GROW_XP[kind] or 0
    if gain <= 0 then return false end
    self.xp = self.xp + gain

    local leveled = false
    while self.level < #self.levels and self.xp >= self.levels[self.level + 1].xp do
        self.level = self.level + 1
        leveled = true
    end
    if leveled then
        self:_Notify(false)
    end
    self:_RefreshHud()
    return leveled
end

--- 把武器数值推给 onWeapon（Shooting），并刷新 HUD。isInitial 区分"开局"与"升级"，
--- 让 main.lua 只在真正升级时播旁白/震屏，开局不打扰。
function Grow:_Notify(isInitial)
    self:_RefreshHud()
    if self.onWeapon then
        self.onWeapon(self:Snapshot(), isInitial)
    end
end

--- 等级 + 经验条进度推给 HUD。进度是当前级到下一级之间的百分比（封顶后恒满）。
function Grow:_RefreshHud()
    local hud = self.hud
    if not hud then return end
    local cur = self.levels[self.level].xp
    local nextLevel = self.levels[self.level + 1]
    local prog = 1.0
    if nextLevel then
        local span = nextLevel.xp - cur
        prog = span > 0 and (self.xp - cur) / span or 1.0
    end
    if hud.SetLevel then hud:SetLevel(self.level) end
    if hud.SetXpProgress then hud:SetXpProgress(prog) end
end

return Grow

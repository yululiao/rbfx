-- Scripts/game/Waves.lua
--
-- 波次导演 + 计分/combo + 防线血量 + 胜负结算。它是玩法的"时钟"：按 Config.WAVES 的
-- 时间表催 Enemy 生成船，一波清完进间歇，三波打完或防线破了就结算。
--
-- 计分与 combo 放这里而不是 Shooting：一次击杀的分数会同时被"玩家子弹"和"爆桶连锁"
-- 两条路径产生（Shooting.DamageUnit 与 Enemy:Damage 都调 onScore），集中在一处才不会
-- 两套 combo 各算各的。
--
-- 与 HUD 的耦合是单向的：Waves 只调 hud:SetXxx（全部 nil-safe），HUD 不反向依赖 Waves。

local Config = require("game.Config")

---@class Waves 波次导演
local Waves = {}
Waves.__index = Waves

---@param deps { enemy: Enemy, audio: Audio, hud: any, onGameOver: fun(outcome:string, score:number)|nil }
---@return Waves
function Waves.Build(deps)
    local self = setmetatable({}, Waves)
    self.enemy = deps.enemy
    self.audio = deps.audio
    self.hud = deps.hud
    self.onGameOver = deps.onGameOver
    self.totalWaves = #Config.WAVES
    self:Reset()
    return self
end

--- 回到开局状态（新局/重开都走这里）。
function Waves:Reset()
    self.score = 0
    self.combo = 0
    self.comboT = 0.0
    self.defenseHp = Config.DEFENSE_HP
    self.waveIndex = 0                 -- 已开始的波数；当前波 = Config.WAVES[waveIndex]
    self.phase = "intermission"        -- intermission -> active -> ended
    self.phaseT = Config.WAVE_INTERMISSION
    self.waveClock = 0.0
    self.queue = {}
    self.allFired = true

    if self.hud then
        if self.hud.SetScore then self.hud:SetScore(0) end
        if self.hud.SetCombo then self.hud:SetCombo(0) end
        if self.hud.SetWave then self.hud:SetWave(1) end
        if self.hud.SetDefense then self.hud:SetDefense(self.defenseHp, Config.DEFENSE_HP) end
    end
    self.audio:Voice(Config.Voice.prepare)
end

--- 开始下一波：拷一份生成时间表，播旁白，切到 active。
function Waves:BeginWave()
    self.waveIndex = self.waveIndex + 1
    local wave = Config.WAVES[self.waveIndex]
    if not wave then
        self:Win()
        return
    end

    self.queue = {}
    for _, spawn in ipairs(wave.spawns) do
        self.queue[#self.queue + 1] = { t = spawn.t, kind = spawn.kind, lane = spawn.lane, fired = false }
    end
    self.allFired = false
    self.waveClock = 0.0
    self.phase = "active"

    self.audio:Voice(wave.voice)
    if self.hud and self.hud.SetWave then
        self.hud:SetWave(self.waveIndex)
    end
end

---@param dt number
function Waves:Update(dt)
    -- combo 窗口
    if self.comboT > 0.0 then
        self.comboT = self.comboT - dt
        if self.comboT <= 0.0 and self.combo ~= 0 then
            self.combo = 0
            if self.hud and self.hud.SetCombo then
                self.hud:SetCombo(0)
            end
        end
    end

    if self.phase == "ended" then
        return
    elseif self.phase == "intermission" then
        self.phaseT = self.phaseT - dt
        if self.hud and self.hud.SetIntermission then
            self.hud:SetIntermission(math.max(0.0, self.phaseT))
        end
        if self.phaseT <= 0.0 then
            if self.waveIndex >= self.totalWaves then
                self:Win()
            else
                self:BeginWave()
            end
        end
    elseif self.phase == "active" then
        self.waveClock = self.waveClock + dt
        self:ProcessQueue()
        if self.allFired and self.enemy:ThreatCount() == 0 then
            -- 本波清完
            if self.waveIndex >= self.totalWaves then
                self:Win()
            else
                self.phase = "intermission"
                self.phaseT = Config.WAVE_INTERMISSION
            end
        end
    end

    if self.defenseHp <= 0 and self.phase ~= "ended" then
        self:Lose()
    end
end

--- 按时间表催生成。同屏超上限时不标记 fired，下一帧重试（延后生成）。
function Waves:ProcessQueue()
    local pending = false
    for _, entry in ipairs(self.queue) do
        if not entry.fired then
            if self.waveClock >= entry.t then
                if self.enemy:CanSpawn() then
                    self.enemy:SpawnBoat(entry.kind, entry.lane)
                    entry.fired = true
                else
                    pending = true -- 闸门挡住，稍后重试
                end
            else
                pending = true
            end
        end
    end
    self.allFired = not pending
end

----------------------------------------------------------------------
-- 计分 / combo / 防线
----------------------------------------------------------------------

--- 一次击杀的结算（Shooting 与爆桶连锁都调这个）。
---@param result { killed: boolean, score: number }
---@param unit any
---@param headshot boolean
function Waves:AddKill(result, unit, headshot)
    if not result or not result.killed then
        return
    end

    self.combo = self.combo + 1
    self.comboT = Config.COMBO_WINDOW

    local gained = (result.score or 0) + math.max(0, self.combo - 1) * Config.COMBO_BONUS_STEP
    self.score = self.score + gained

    if self.hud then
        if self.hud.SetScore then self.hud:SetScore(self.score) end
        if self.hud.SetCombo then self.hud:SetCombo(self.combo) end
    end

    -- combo 里程碑旁白
    for _, threshold in ipairs(Config.COMBO_VOICE_AT) do
        if self.combo == threshold then
            self.audio:Voice(threshold >= 5 and Config.Voice.multiKill or Config.Voice.combo)
        end
    end
end

--- 海盗砍到防线。
---@param dmg number
function Waves:DamageDefense(dmg)
    if self.phase == "ended" then
        return
    end
    self.defenseHp = math.max(0, self.defenseHp - (dmg or 0))
    if self.hud and self.hud.SetDefense then
        self.hud:SetDefense(self.defenseHp, Config.DEFENSE_HP)
    end
end

----------------------------------------------------------------------
-- 结算
----------------------------------------------------------------------

function Waves:Win()
    if self.phase == "ended" then
        return
    end
    self.phase = "ended"
    self.audio:Voice(Config.Voice.win)
    if self.defenseHp >= Config.DEFENSE_HP then
        self.audio:Voice(Config.Voice.flawless)
    end
    if self.hud and self.hud.ShowEnd then
        self.hud:ShowEnd(true, self.score)
    end
    if self.onGameOver then
        self.onGameOver("win", self.score)
    end
end

function Waves:Lose()
    if self.phase == "ended" then
        return
    end
    self.phase = "ended"
    self.audio:Voice(Config.Voice.lose)
    if self.hud and self.hud.ShowEnd then
        self.hud:ShowEnd(false, self.score)
    end
    if self.onGameOver then
        self.onGameOver("lose", self.score)
    end
end

---@return number score
function Waves:GetScore()
    return self.score
end

return Waves

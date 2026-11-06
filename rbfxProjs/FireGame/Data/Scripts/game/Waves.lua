-- Scripts/game/Waves.lua
--
-- 波次导演 + 计分/combo + 炮塔血量 + 胜负结算。它是玩法的"时钟"：按 Config.WAVES 手调
-- 开局、其后再由 WaveGen.EndlessRecipe 随波次升配额（无尽模式），一波清完进间歇、永不通关，
-- 直到防线/主角被打穿才结算（Lose）。HUD 波次号即"撑到第几波"的战绩。
--
-- 血量都住在这里、只住这里：主角血 playerHp、两座炮塔血 turretHp（每塔一份，
-- 下标对齐 Config.TURRETS）。Enemy 砍塔只递一个 (塔号, 伤害) 过来、Turrets 开火前反过来
-- 读这里的血量，两边都不自己另存一份——否则"塔哑了还在开炮"这种不一致迟早出现。
--
-- 计分与 combo 放这里而不是 Shooting：一次击杀的分数会同时被"玩家子弹"和"爆桶连锁"
-- 两条路径产生（Shooting.DamageUnit 与 Enemy:Damage 都调 onScore），集中在一处才不会
-- 两套 combo 各算各的。
--
-- 与 HUD 的耦合是单向的：Waves 只调 hud:SetXxx（全部 nil-safe），HUD 不反向依赖 Waves。

local Config = require("game.Config")
local WaveGen = require("game.WaveGen")
local Tracker = require("game.Tracker")
local Sidebar = require("game.Sidebar")

-- 首局操作提示只在会话第一次第 1 波显示：重开的玩家已经会了，再弹只是打扰。
local first_run_hint_shown = false

---@class Waves 波次导演
local Waves = {}
Waves.__index = Waves

---@param deps { enemy: Enemy, audio: Audio, hud: any, voice: any|nil,
---               onGameOver: fun(outcome:string, score:number)|nil }
---@return Waves
function Waves.Build(deps)
    local self = setmetatable({}, Waves)
    self.enemy = deps.enemy
    self.audio = deps.audio
    self.hud = deps.hud
    -- 自定义语音（可空）：失守/连杀两句“本可以是你自己录的”，没录过就回落预置旁白。
    -- 不把它写进 audio（那个模块只管预置音效库），两边职责分开。
    self.voice = deps.voice
    self.onGameOver = deps.onGameOver
    self.onWaveCleared = deps.onWaveCleared   -- 一波清完的回调（main.lua 传入：让宠物狗原地休息）
    -- 首次 Reset 静音：Build 时开始画面还盖着战场（worldDt=0，倒计时没走），
    -- “准备”旁白此时响会很怪——它改由 main.lua 的 onStart（点击“开始游戏”）补播；
    -- RestartGame 的 Reset 照旧带语音：重开即播“准备”是对的（玩家刚点完再来一局）。
    self:Reset(true)
    -- game_start 埋点不在这里报：开始画面盖着时 Waves 已装配但倒计时被压着，
    -- “用户主动开始”的时点在 main.lua 的 onStart（点击“开始游戏”）。
    return self
end

--- 回到开局状态（新局/重开都走这里）。
---@param skipVoice boolean|nil true 时跳过“准备”旁白（Build 时开始画面盖着，语音改由点击开始触发）
function Waves:Reset(skipVoice)
    self.score = 0
    self.combo = 0
    self.maxCombo = 0                -- 本局 combo 峰值（game_over 维度，区别于实时 combo）
    self.comboT = 0.0
    self.turretHp = {}
    for i = 1, #Config.TURRETS do
        self.turretHp[i] = Config.TURRET_HP
    end
    -- 主角生命上限（血量唯一真相住本模块）：侧边栏回归增益在这里乘
    -- （game/Sidebar.lua；无宿主/未生效时倍率恒 1.0）。HpBar 的血量刻度也读它。
    self.playerMaxHp = Config.PLAYER_HP * Sidebar.HpMult()
    self.playerHp = self.playerMaxHp          -- 主角生命（突袭型海盗砍的是这条）
    self.reviveUsed = false              -- 本局是否已用「看广告复活」；每局限一次，Reset 复位
    self.waveIndex = 0                 -- 已开始的波数；当前波 = Config.WAVES[waveIndex]
    self.phase = "intermission"        -- intermission -> active -> ended
    self.phaseT = Config.WAVE_INTERMISSION
    self.waveClock = 0.0
    self.queue = {}
    self.allFired = true
    self.blastEvery = Config.COMBO_BLAST_EVERY   -- 动态爆炸间隔（开局低弹丸=基础档）；main 随武器升级下发实际值

    if self.hud then
        if self.hud.SetScore then self.hud:SetScore(0) end
        if self.hud.SetCombo then self.hud:SetCombo(0) end
        if self.hud.SetWave then self.hud:SetWave(1) end
        -- 主角血、炮塔血都不往 HUD 喂：各走头顶世界空间血条（game/HpBar.lua）每帧直读。
    end
    if not skipVoice then
        self.audio:Voice(Config.Voice.prepare)
    end
end

--- 设定连杀爆炸触发间隔（随玩家弹丸数动态变）。main.lua 在 onWeapon 武器升级时调。
---@param every number
function Waves:SetBlastEvery(every)
    self.blastEvery = every
end

--- 开始下一波：拷一份生成时间表，播旁白，切到 active。
function Waves:BeginWave()
    self.waveIndex = self.waveIndex + 1
    -- 无尽模式：前若干波用 Config.WAVES 手调开场，配额用尽后按波次公式生成越来越重的编队。
    local wave = Config.WAVES[self.waveIndex] or WaveGen.EndlessRecipe(self.waveIndex)

    self.queue = {}
    -- 开局按本波配方实时编排一张生成表（总量固定、铺法每局随机）。
    for _, spawn in ipairs(WaveGen.Generate(wave)) do
        self.queue[#self.queue + 1] = { t = spawn.t, kind = spawn.kind, lane = spawn.lane, fired = false }
    end
    self.allFired = false
    self.waveClock = 0.0
    self.phase = "active"

    self.audio:Voice(wave.voice)
    if self.hud and self.hud.SetWave then
        self.hud:SetWave(self.waveIndex)
    end
    -- 新手提示：第一波将到未到时给一次操作提示（本作开火全自动，要教的只有走位）。
    -- 挂在 BeginWave 而不是 Reset：间歇倒计时还在刷 narrate，太早会被盖掉。
    if self.waveIndex == 1 and not first_run_hint_shown and self.hud and self.hud.Narrate then
        first_run_hint_shown = true
        self.hud:Narrate("拖动摇杆走位躲海盗，开火是全自动的", 3.5)
    end
    -- 波次里程碑：第 1 波必报（漏斗起点），其后每 5 波一条；无尽模式一局几十波，
    -- 逐波上报太吵，局终波数由 game_over 兜底。
    if self.waveIndex == 1 or self.waveIndex % 5 == 0 then
        Tracker.Report("wave", { wave = self.waveIndex })
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

    if self.phase == "ended" or self.phase == "freeRoam" then
        return
    elseif self.phase == "intermission" then
        self.phaseT = self.phaseT - dt
        if self.hud and self.hud.SetIntermission then
            self.hud:SetIntermission(math.max(0.0, self.phaseT))
        end
        if self.phaseT <= 0.0 then
            self:BeginWave()
        end
    elseif self.phase == "active" then
        self.waveClock = self.waveClock + dt
        self:ProcessQueue()
        if self.allFired and self.enemy:ThreatCount() == 0 then
            -- 本波清完
            if self.onWaveCleared then
                self.onWaveCleared(self.waveIndex)
            end
            self.phase = "intermission"
            self.phaseT = Config.WAVE_INTERMISSION
        end
    end

    if self.phase == "ended" or self.phase == "freeRoam" then return end
    if self:AnyTurretDown() then
        self:Lose("defense")
    elseif self.playerHp <= 0 then
        self:Lose("hero")
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

--- 播一句"可以是你自己录的"语音：录过就播你的，没录过/宿主不支持→回落预置旁白。
--- 回落只在回放真的失败时发生（见 Voice:Play），所以这里不必事先知道有没有录音。
---@param slot string Config.VOICE_SLOT_*
---@param canned string|nil Config.Voice 里的预置旁白键
function Waves:PlayerVoice(slot, canned)
    local fallback = canned and function() self.audio:Voice(canned) end or nil
    if self.voice then
        self.voice:Play(slot, fallback)
    elseif fallback then
        fallback()
    end
end

--- 一次击杀的结算（Shooting 与爆桶连锁都调这个）。
---@param result { killed: boolean, score: number }
---@param unit any
---@param headshot boolean
---@param fromChain boolean|nil 该击杀是否来自爆炸/连锁（true 则照常计分涨 combo 但不再触发新爆炸，切断自激）
function Waves:AddKill(result, unit, headshot, fromChain)
    if not result or not result.killed then
        return
    end

    self.combo = self.combo + 1
    if self.combo > self.maxCombo then
        self.maxCombo = self.combo
    end
    self.comboT = Config.COMBO_WINDOW

    local gained = (result.score or 0) + math.max(0, self.combo - 1) * Config.COMBO_BONUS_STEP
    self.score = self.score + gained

    if self.hud then
        if self.hud.SetScore then self.hud:SetScore(self.score) end
        if self.hud.SetCombo then self.hud:SetCombo(self.combo) end
    end

    -- combo 里程碑旁白。3 连杀仍用预置音（太频繁，拿来播 3 秒录音会连续相互顶掉），
    -- 5/8 连杀才开始“如果是玩家自己录的就播玩家的”。
    for _, threshold in ipairs(Config.COMBO_VOICE_AT) do
        if self.combo == threshold then
            if threshold >= 5 then
                self:PlayerVoice(Config.VOICE_SLOT_KILLSTREAK, Config.Voice.multiKill)
            else
                self.audio:Voice(Config.Voice.combo)
            end
        end
    end

    -- 连杀正反馈：每 blastEvery 连杀在刚击杀的位置放一次范围爆炸，把 combo 从
    -- “只加分”变成“越杀越炸”的割草闭环。伤害经 Enemy:ComboBlast 递延结算，不与本次同帧递归。
    -- 关键：仅玩家子弹击杀才触发（fromChain 的爆炸/连锁击杀照常计分涨 combo，但不再引爆新爆炸），
    -- 切断“爆炸杀的人回喂触发器”的跨帧自激，EVERY 才回归单调可控。
    local every = self.blastEvery or Config.COMBO_BLAST_EVERY
    if every and every > 0 and not fromChain and (self.combo % every) == 0
        and unit and unit.node and self.enemy and self.enemy.ComboBlast then
        self.enemy:ComboBlast(unit.node:GetWorldPosition())
    end
end

--- 海盗砍到某座塔。index 与 Config.TURRETS 同下标（由 Enemy 选塔时定下来再传过来，
--- 不在这里重做一次"最近塔"的判据，免得两边选到不同的塔）。
---@param index number
---@param dmg number
function Waves:DamageTurret(index, dmg)
    if self.phase == "ended" or not index or not self.turretHp[index] then
        return
    end
    self.turretHp[index] = math.max(0, self.turretHp[index] - (dmg or 0))
end

--- 某塔当前/上限血量（Turrets 读它决定这座塔还能不能开火、要不要把炮管歪下来）。
---@param index number
---@return number, number
function Waves:GetTurretHp(index)
    return (self.turretHp and self.turretHp[index]) or 0, Config.TURRET_HP
end

--- 任一塔被打掉即判防线失守（用户口径：两座塔缺一不可，掉一座就算输）。
--- 只要有一塔 hp<=0 就返回 true；两塔都还站着才 false。
---@return boolean
function Waves:AnyTurretDown()
    for i = 1, #Config.TURRETS do
        if (self.turretHp[i] or 0) <= 0 then
            return true
        end
    end
    return false
end

--- 两塔都满血（即原 flawless 判据 defenseHp >= DEFENSE_HP 的等价式）。
---@return boolean
function Waves:DefenseIntact()
    for i = 1, #Config.TURRETS do
        if (self.turretHp[i] or 0) < Config.TURRET_HP then
            return false
        end
    end
    return true
end

--- 突袭型海盗砍到主角 / 大船炮弹炸到主角。只动数值，不在这里渲染：
--- 主角血唯一的消费者是头顶那 3 格，它每帧来读 self.playerHp（见 game/HpBar.lua）。
---@param dmg number
function Waves:DamagePlayer(dmg)
    if self.phase == "ended" then
        return
    end
    self.playerHp = math.max(0, self.playerHp - (dmg or 0))
end

----------------------------------------------------------------------
-- 结算
----------------------------------------------------------------------

function Waves:Win()
    if self.phase == "ended" or self.phase == "freeRoam" then
        return
    end
    -- 通关不再弹全屏结算面板挡住画面（那会盖住摇杆/登船按钮且不会自动消失），
    -- 而是进入“自由探索”态：停止刷怪，玩家可走动/登船出海（本期载具玩法的入口）。
    -- 分数/combo/防线仍显在 HUD 上；只有防线被攻破（Lose）才弹真正的结算面板。
    self.phase = "freeRoam"
    self.allFired = true
    self.audio:Voice(Config.Voice.win)
    if self:DefenseIntact() then
        self.audio:Voice(Config.Voice.flawless)
    end
    if self.hud and self.hud.Narrate then
        self.hud:Narrate("海盗已清空，出海自由探索吧！", 4.0)
    end
    Tracker.Report("game_over", {
        outcome = "win",
        score = self.score,
        wave = self.waveIndex,
        maxCombo = self.maxCombo,
    })
end

function Waves:Lose(reason)
    if self.phase == "ended" then
        return
    end
    self.phase = "ended"
    -- 失守吐槽：玩家自己录的（没录回落 you_lose）
    self:PlayerVoice(Config.VOICE_SLOT_LOSE, Config.Voice.lose)
    local title = (reason == "hero") and "主角阵亡" or "防线失守"
    if self.hud and self.hud.ShowEnd then
        self.hud:ShowEnd(false, self.score, title)
    end
    if self.onGameOver then
        self.onGameOver("lose", self.score)
    end
    Tracker.Report("game_over", {
        outcome = "lose",
        reason = reason,
        score = self.score,
        wave = self.waveIndex,
        maxCombo = self.maxCombo,
        revived = self.reviveUsed,       -- 本局是否用过复活（false=首死即终局，true=复活后再败）
    })
end

--- 结算面板的复活按钮此刻可不可用：确实处于败局且本局还没用过。
--- 复活资格归 Waves 管（局内状态），广告链路归 main.lua（宿主能力），两层分开。
---@return boolean
function Waves:CanRevive()
    return self.phase == "ended" and not self.reviveUsed
end

--- 看广告复活（main.lua 在激励视频确认看完后调）：战绩/成长全保留，血回满，接着打。
--- 只改波次机自身的状态；清场、主角/炮塔姿态复位等由 main.lua 的 ReviveGame 统一做。
---@return boolean 是否复活成功（非败局或已用过则 false，调用方据此不发放奖励）
function Waves:Revive()
    if not self:CanRevive() then
        return false
    end
    self.reviveUsed = true
    for i = 1, #Config.TURRETS do
        self.turretHp[i] = Config.TURRET_HP
    end
    self.playerHp = self.playerMaxHp
    self.combo = 0
    if self.hud and self.hud.SetCombo then
        self.hud:SetCombo(0)
    end
    -- 当前波作废：生成表清空 + allFired，让 Update 的 active 分支不再刷怪，
    -- 直接走间歇倒计时从下一波重开（waveIndex 不动，BeginWave 自会 +1）。
    -- 场上残敌由 main.lua 调 enemy:Clear() 清场。
    self.queue = {}
    self.allFired = true
    self.phase = "intermission"
    self.phaseT = Config.WAVE_INTERMISSION
    -- 复活后重新走一遍开场播报，给玩家一个“重新准备”的仪式感与缓冲。
    self.audio:Voice(Config.Voice.prepare)
    if self.hud and self.hud.Narrate then
        self.hud:Narrate("满血复活，接着打！", 3.0)
    end
    return true
end

---@return number score
function Waves:GetScore()
    return self.score
end

return Waves

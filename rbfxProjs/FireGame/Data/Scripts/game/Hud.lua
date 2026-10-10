-- Scripts/game/Hud.lua
--
-- 数据绑定的 HUD：包一层 LuaRmlUIComponent + GameHud.rml。
--
-- 机制（见 107_HelloRmlUI 与 LuaRmlUIBindings.cpp）：
--   * SetHandlers{onInit=fn} 必须在 SetResource 之前调用；onInit 只在文档构造那一刻触发，
--     是唯一能 Bind* 的时机。绑定用 BindReadonlyProperty(name, getter)，事件用 BindEvent。
--   * 之后 Lua 改的是本模块的 local 值，再 MarkDirty(name) 通知 RmlUi 重新取 getter。
--   * 所以每个 SetXxx 都是"改 self 字段 + MarkDirty"，getter 只是读 self 字段。
--
-- 所有 SetXxx 都做了组件是否就绪的判断：SetResource 之前（onInit 未跑）调用不会崩，
-- 只是那次 MarkDirty 落空，值仍存在 self 上，onInit 后首次渲染就能读到。

local Config = require("game.Config")

---@class Hud 玩法 HUD
local Hud = {}
Hud.__index = Hud

local function clamp01(v)
    if v < 0.0 then
        return 0.0
    elseif v > 1.0 then
        return 1.0
    end
    return v
end

---@param scene Scene
---@param handlers { onRestart: fun()|nil, onBoard: fun()|nil, onDisembark: fun()|nil, onVoiceOpen: fun()|nil, onVoiceClose: fun()|nil, onVoiceRecord: fun(slot:string)|nil, onVoicePreview: fun()|nil, onVoiceFilter: fun()|nil }|nil
---@return Hud
function Hud.Build(scene, handlers)
    local self = setmetatable({}, Hud)
    handlers = handlers or {}

    -- 数据（getter 读这些；MarkDirty 通知刷新）
    self.score = 0
    self.wave = 1
    self.combo = 0
    self.ammo = Config.MAG_SIZE
    self.reload = 0.0          -- 0..1，绑定 <progress data-value="reload">
    self.defense = 1.0         -- 0..1，绑定 <progress data-value="defense">
    self.narrate = ""
    self.narrateT = 0.0
    -- hitmarker 靠两个 bool + data-if 二选一显示（内联 style 绑不了 opacity/color）
    self.hitNormal = false
    self.hitStrong = false
    self.hitT = 0.0            -- >0 时显示 hitmarker，到点自动灭
    self.gameOver = false      -- data-if="gameOver"：结算面板显隐
    self.endTitle = ""
    self.endScore = 0
    -- 登船/下船按钮显隐（data-if）：main.lua 帧循环里根据交互条件 Show/Hide
    self.showBoard = false
    self.showDisembark = false
    -- 自定义语音面板（data-if）：入口按钮只在宿主 SDK 可用时显示（编辑器隐藏）；
    -- 状态行/录音中提示/音色名都是纯文本或 bool 绑定（不支持内联 style 绑定）
    self.voiceAvailable = false
    self.voicePanel = false
    self.voiceRecording = false
    self.voiceFilterName = "原声"
    self.voiceStatus = ""

    local comp = scene:CreateComponent("LuaRmlUIComponent")
    self.comp = comp

    comp:SetHandlers({
        onInit = function(w)
            self.w = w
            w:BindReadonlyProperty("score", function() return self.score end)
            w:BindReadonlyProperty("wave", function() return self.wave end)
            w:BindReadonlyProperty("ammo", function() return self.ammo end)
            w:BindReadonlyProperty("endScore", function() return self.endScore end)
            w:BindReadonlyProperty("comboText", function()
                return self.combo > 1 and ("x" .. self.combo) or ""
            end)
            -- progress 条：0..1 数值，RmlUi <progress data-value> 直接吃
            w:BindReadonlyProperty("defense", function() return clamp01(self.defense) end)
            w:BindReadonlyProperty("reload", function() return clamp01(self.reload) end)
            -- data-if 显隐：bool
            w:BindReadonlyProperty("hitNormal", function() return self.hitNormal end)
            w:BindReadonlyProperty("hitStrong", function() return self.hitStrong end)
            w:BindReadonlyProperty("gameOver", function() return self.gameOver end)
            w:BindReadonlyProperty("showBoard", function() return self.showBoard end)
            w:BindReadonlyProperty("showDisembark", function() return self.showDisembark end)
            w:BindReadonlyProperty("narrate", function() return self.narrate end)
            w:BindReadonlyProperty("endTitle", function() return self.endTitle end)
            w:BindReadonlyProperty("voiceAvailable", function() return self.voiceAvailable end)
            w:BindReadonlyProperty("voicePanel", function() return self.voicePanel end)
            w:BindReadonlyProperty("voiceRecording", function() return self.voiceRecording end)
            w:BindReadonlyProperty("voiceFilterName", function() return self.voiceFilterName end)
            w:BindReadonlyProperty("voiceStatus", function() return self.voiceStatus end)

            w:BindEvent("restart", function()
                if handlers.onRestart then
                    handlers.onRestart()
                end
            end)
            -- 载具交互：登船/下船
            w:BindEvent("board", function()
                if handlers.onBoard then
                    handlers.onBoard()
                end
            end)
            w:BindEvent("disembark", function()
                if handlers.onDisembark then
                    handlers.onDisembark()
                end
            end)
            -- 自定义语音：入口/面板内按钮。槽位参数直接拆成两个事件（lose/killstreak），
            -- 避免给 BindEvent 传参的额外约定。
            w:BindEvent("voiceOpen", function()
                if handlers.onVoiceOpen then
                    handlers.onVoiceOpen()
                end
            end)
            w:BindEvent("voiceClose", function()
                if handlers.onVoiceClose then
                    handlers.onVoiceClose()
                end
            end)
            w:BindEvent("voiceRecordLose", function()
                if handlers.onVoiceRecord then
                    handlers.onVoiceRecord("lose")
                end
            end)
            w:BindEvent("voiceRecordKillstreak", function()
                if handlers.onVoiceRecord then
                    handlers.onVoiceRecord("killstreak")
                end
            end)
            w:BindEvent("voicePreview", function()
                if handlers.onVoicePreview then
                    handlers.onVoicePreview()
                end
            end)
            w:BindEvent("voiceFilter", function()
                if handlers.onVoiceFilter then
                    handlers.onVoiceFilter()
                end
            end)
        end,
    })

    -- SetResource 打开文档，触发上面的 onInit（顺序不能反）
    comp:SetResource("UI/GameHud.rml")
    return self
end

--- 安全 MarkDirty：组件还没跑完 onInit 时 self.w 为 nil，跳过即可（值已在 self 上）。
---@param name string
function Hud:Dirty(name)
    if self.w then
        self.w:MarkDirty(name)
    end
end

function Hud:SetScore(v)
    self.score = v or 0
    self:Dirty("score")
end

function Hud:SetWave(v)
    self.wave = v or 1
    self:Dirty("wave")
end

function Hud:SetCombo(v)
    self.combo = v or 0
    self:Dirty("comboText")
end

function Hud:SetAmmo(v)
    self.ammo = v or 0
    self:Dirty("ammo")
end

--- 装弹进度 0..1。
function Hud:SetReload(v)
    self.reload = clamp01(v or 0.0)
    self:Dirty("reload")
end

--- 防线血量（当前值 + 上限），内部换算成 0..1。
function Hud:SetDefense(cur, max)
    max = (max and max > 0) and max or Config.DEFENSE_HP
    self.defense = clamp01((cur or 0) / max)
    self:Dirty("defense")
end

--- 命中标记闪一下。strong（爆头/击杀）时显红色那组，否则白色。~0.15s 后由 Update 自动灭。
function Hud:FlashHit(strong)
    self.hitStrong = strong and true or false
    self.hitNormal = not self.hitStrong
    self.hitT = 0.15
    self:Dirty("hitNormal")
    self:Dirty("hitStrong")
end

--- 顶部旁白（波次提示 / 间歇倒计时），几秒后自动清。
---@param text string
---@param duration number|nil
function Hud:Narrate(text, duration)
    self.narrate = text or ""
    self.narrateT = duration or 2.5
    self:Dirty("narrate")
end

--- 波间间歇倒计时文案（Waves 每帧调）。<=0 时留空。
function Hud:SetIntermission(t)
    if t and t > 0.0 then
        local secs = math.floor(t + 0.5)
        local text = "下一波 " .. secs .. "s"
        if text ~= self.narrate then
            self.narrate = text
            self.narrateT = 0.0 -- 由 Waves 持续刷新，不走自动清
            self:Dirty("narrate")
        end
    end
end

--- 结算面板。
---@param win boolean
---@param score number
function Hud:ShowEnd(win, score)
    self.gameOver = true
    self.endTitle = win and "胜利!" or "防线失守"
    self.endScore = score or 0
    self:Dirty("gameOver")
    self:Dirty("endTitle")
    self:Dirty("endScore")
end

function Hud:HideEnd()
    self.gameOver = false
    self:Dirty("gameOver")
end

--- 显/隐登船、下船按钮（id="board"/"disembark"）。main.lua 帧循环每帧调。
---@param id string
function Hud:ShowButton(id)
    if id == "board" then
        if not self.showBoard then
            self.showBoard = true
            self:Dirty("showBoard")
        end
    elseif id == "disembark" then
        if not self.showDisembark then
            self.showDisembark = true
            self:Dirty("showDisembark")
        end
    end
end

---@param id string
function Hud:HideButton(id)
    if id == "board" then
        if self.showBoard then
            self.showBoard = false
            self:Dirty("showBoard")
        end
    elseif id == "disembark" then
        if self.showDisembark then
            self.showDisembark = false
            self:Dirty("showDisembark")
        end
    end
end

----------------------------------------------------------------------
-- 自定义语音面板
----------------------------------------------------------------------

--- 语音入口可用性（编辑器宿主无 SDK，藏入口按钮）。装配后调一次。
function Hud:SetVoiceAvailable(v)
    self.voiceAvailable = v and true or false
    self:Dirty("voiceAvailable")
end

--- 面板开/关（voiceOpen/voiceClose 事件经 main.lua 回调进来）。
function Hud:SetVoicePanel(open)
    self.voicePanel = open and true or false
    if not self.voicePanel then
        -- 关面板时连带清状态，下次打开是干净面板
        self.voiceRecording = false
        self.voiceStatus = ""
        self:Dirty("voiceRecording")
        self:Dirty("voiceStatus")
    end
    self:Dirty("voicePanel")
end

--- 录音中标志（true 时面板提示“录音中”，按钮被 Voice 侧闸住重入）。
function Hud:SetVoiceRecording(v)
    self.voiceRecording = v and true or false
    self:Dirty("voiceRecording")
end

--- 音色名（循环切换按钮的文本）。
function Hud:SetVoiceFilterName(name)
    self.voiceFilterName = name or "原声"
    self:Dirty("voiceFilterName")
end

--- 状态行（录音/试听结果提示，几秒后由 Update 自动清）。
function Hud:SetVoiceStatus(text)
    self.voiceStatus = text or ""
    self.voiceStatusT = 3.0
    self:Dirty("voiceStatus")
end

--- 每帧：hitmarker 到点自动关闭 + 旁白到期清除。
---@param dt number
function Hud:Update(dt)
    if self.hitT > 0.0 then
        self.hitT = self.hitT - dt
        if self.hitT <= 0.0 then
            self.hitT = 0.0
            if self.hitNormal or self.hitStrong then
                self.hitNormal = false
                self.hitStrong = false
                self:Dirty("hitNormal")
                self:Dirty("hitStrong")
            end
        end
    end
    if self.narrateT > 0.0 then
        self.narrateT = self.narrateT - dt
        if self.narrateT <= 0.0 and self.narrate ~= "" then
            self.narrate = ""
            self:Dirty("narrate")
        end
    end
    if self.voiceStatusT and self.voiceStatusT > 0.0 then
        self.voiceStatusT = self.voiceStatusT - dt
        if self.voiceStatusT <= 0.0 and self.voiceStatus ~= "" then
            self.voiceStatus = ""
            self:Dirty("voiceStatus")
        end
    end
end

return Hud

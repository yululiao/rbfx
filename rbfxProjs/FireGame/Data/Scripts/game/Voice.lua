-- Scripts/game/Voice.lua
--
-- 自定义语音（麦克风录一句 → 变声回放）：玩法情绪钩子的执行器。
--   槽位：lose（防线失守播你的吐槽）、killstreak（连杀里程碑播你的战吼）。
--
-- 链路分工（重要）：
--   * 引擎侧（MinigameSDK）只发请求收事件——RecordVoice/PlayUserAudio/StopUserAudio，
--     每个请求返回 id，结果经 "MinigameSdkResult" 事件回来（data.Kind 对照
--     MINIGAME_REQUEST_KIND 表）。
--   * 宿主 JS 侧（minigame_sdk.js）拿 recorderManager 录 mp3 存进用户目录
--     （rbfx_voice_<slot>.mp3，跨局持久），InnerAudioContext 按档位变声回放。
--     引擎的 Sound 解码器全程不参与（它只认 ogg/wav）。
--   * 编辑器/桌面宿主没有 SDK（IsAvailable()=false，请求立即失败）——UI 入口
--     由 Hud 按 voiceAvailable 隐藏，本模块的调用全部静默返回，不会报错。
--
-- 音色：playbackRate 只能取宿主档位（0.5/0.8/1.0/1.25/1.5/2.0），配置在
-- Config.VOICE_FILTERS；循环切换（CycleFilter）给试听/回放共用。

local Config = require("game.Config")

---@class Voice 自定义语音的请求编排与状态
local Voice = {}
Voice.__index = Voice

---@param scene Scene
---@param deps { hud: Hud|nil }|nil
---@return Voice
function Voice.Build(scene, deps)
    local self = setmetatable({}, Voice)
    self.hud = deps and deps.hud
    self.sdk = GetSubsystem("MinigameSDK")

    self.filterIdx = math.min(math.max(Config.VOICE_DEFAULT_FILTER, 1), #Config.VOICE_FILTERS)
    self.recording = false       -- 是否有录音请求在途（防止重入）
    self.previewing = false      -- 是否有试听请求在途（结果回写 HUD 状态行）
    self.lastSlot = nil          -- 最近录成功的槽位（试听默认播它）

    -- 录音/试听结果都从这里回来（BEGINFRAME 边界派发，见 MinigameSDK.cpp）
    SubscribeToEvent("MinigameSdkResult", function(data)
        self:OnSdkResult(data)
    end)
    return self
end

---@return boolean 宿主 SDK 是否可用（编辑器里为 false，UI 据此隐藏入口）
function Voice:IsAvailable()
    return self.sdk ~= nil and self.sdk:IsAvailable()
end

--- 当前音色的 playbackRate（宿主档位值）。
---@return number
function Voice:CurrentRate()
    return Config.VOICE_FILTERS[self.filterIdx].rate
end

--- 循环切换音色（UI 按钮），返回新音色名。
---@return string
function Voice:CycleFilter()
    self.filterIdx = self.filterIdx % #Config.VOICE_FILTERS + 1
    return Config.VOICE_FILTERS[self.filterIdx].name
end

--- 录一句（面板按钮）。到时自动停（上限 Config.VOICE_MAX_SEC），结果走事件。
---@param slot string Config.VOICE_SLOT_*
function Voice:Record(slot)
    if not self:IsAvailable() then
        return
    end
    if self.recording then
        return  -- 录音中再点：静默忽略（shim 侧也有闸）
    end
    self.recording = true
    self.pendingSlot = slot
    if self.hud and self.hud.SetVoiceRecording then
        self.hud:SetVoiceRecording(true)
    end
    self.sdk:RecordVoice(slot, Config.VOICE_MAX_SEC)
end

--- 试听（面板按钮）：优先播最近录的槽位（没录过则播 lose 槽）。
function Voice:Preview()
    if not self:IsAvailable() then
        return
    end
    local slot = self.lastSlot or Config.VOICE_SLOT_LOSE
    self.previewing = true
    self.sdk:PlayUserAudio(slot, self:CurrentRate())
end

--- 玩法触发回放（失败吐槽/连杀战吼）。文件不存在时宿主回失败，这里静默——
--- 玩法触发点不弹 UI（战场上突然冒“没录过音”比没声音更糟）。
---@param slot string
function Voice:Play(slot)
    if not self:IsAvailable() then
        return
    end
    self.sdk:PlayUserAudio(slot, self:CurrentRate())
end

--- 停掉当前回放（换局时调，避免上一局的语音拖进新局）。
function Voice:Stop()
    if not self:IsAvailable() then
        return
    end
    self.sdk:StopUserAudio()
    self.previewing = false
end

--- MinigameSdkResult 事件分发：录音/试听结果回写 HUD 状态行。
function Voice:OnSdkResult(data)
    local kind = data.Kind
    if kind == MINIGAME_REQUEST_KIND.RECORD_VOICE then
        self.recording = false
        if self.hud and self.hud.SetVoiceRecording then
            self.hud:SetVoiceRecording(false)
        end
        if data.Success then
            -- Payload 形如 {"durationMs":2143}；槽位由发起方记在 pendingSlot
            self.lastSlot = self.pendingSlot or self.lastSlot
            if self.hud and self.hud.SetVoiceStatus then
                self.hud:SetVoiceStatus("录好了！点“试听”听听效果")
            end
        else
            if self.hud and self.hud.SetVoiceStatus then
                self.hud:SetVoiceStatus("录音失败（可能是没授权麦克风）")
            end
        end
    elseif kind == MINIGAME_REQUEST_KIND.PLAY_USER_AUDIO then
        if self.previewing then
            self.previewing = false
            if self.hud and self.hud.SetVoiceStatus then
                if data.Success then
                    self.hud:SetVoiceStatus("就是这效果！")
                else
                    self.hud:SetVoiceStatus("还没录过音，先录一句吧")
                end
            end
        end
    end
end

return Voice

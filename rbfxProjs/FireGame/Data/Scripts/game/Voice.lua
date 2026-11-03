-- Scripts/game/Voice.lua
--
-- 自定义语音（麦克风录一句 → 变声回放）：玩法情绪钩子的执行器。
--   槽位：lose（防线失守播你的吐槽）、killstreak（连杀里程碑播你的战吼）。
--
-- 链路分工（重要）：
--   * 引擎侧（MinigameSDK）只发请求收事件——RecordVoice/PlayUserAudio/StopUserAudio，
--     每个请求返回 id，结果经 "MinigameSdkResult" 事件回来（data.Kind 对照
--     MINIGAME_REQUEST_KIND 表）。
--   * 宿主 JS 侧（minigame_sdk.js）拿 recorderManager 录 aac 存进用户目录
--     （rbfx_voice_<slot>.aac，跨局持久），InnerAudioContext 按档位变声回放。
--     引擎的 Sound 解码器全程不参与（它只认 ogg/wav）。
--   * 编辑器/桌面宿主没有 SDK（IsAvailable()=false，请求立即失败）——UI 入口
--     由 Hud 按 voiceAvailable 隐藏，本模块的调用全部静默返回，不会报错。
--
-- 音色：playbackRate 只能取宿主档位（0.5/0.8/1.0/1.25/1.5/2.0），配置在
-- Config.VOICE_FILTERS；循环切换（CycleFilter）给试听/回放共用。

local Config = require("game.Config")
local Tracker = require("game.Tracker")

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
    self.recordDeadline = 0      -- 录音看门狗的剩余秒数（0 = 没有在途请求）
    self.previewing = false      -- 是否有试听请求在途（结果回写 HUD 状态行）
    self.lastSlot = nil          -- 最近录成功的槽位（试听默认播它）
    self.pendingSlot = nil       -- 本次录音请求的槽位（结果回来时才落进 lastSlot）
    self.pendingClearSlot = nil  -- 本次清除请求的槽位（成功后才清 lastSlot）
    -- 玩法触发回放（Play）的回落约定：宿主回放失败 → 播预置旁白。只认“这一次请求”的
    -- 失败：JS 侧新的回放会顶掉旧的并以 success=false("playback stopped") 结算，若不看 id，
    -- 被顶掉的那次会误触发回落，出现“既有你的声音又有 Kenney 旁白”的双响。
    self.pendingPlayId = 0       -- 0 = 没有待回落的玩法回放
    self.pendingFallback = nil   -- 该次回放失败时要补播的预置旁白（fun）

    -- 录音/试听结果都从这里回来（BEGINFRAME 边界派发，见 MinigameSDK.cpp）
    SubscribeToEvent("MinigameSdkResult", function(data)
        self:OnSdkResult(data)
    end)

    -- 录音看门狗：宿主若既不给结果也不报错（shim 侧已见到过一次这种吞回调），面板会永远
    -- 停在“录音中”。shim 自带 duration+30s 的结算，这里再留几秒余量，谁先到谁解锁。
    SubscribeToEvent("Update", function(data)
        self:OnUpdate(data.TimeStep)
    end)

    -- UI 侧初始状态：入口按宿主能力显隐（编辑器/桌面整块不渲染），音色名先显示默认档
    if self.hud then
        if self.hud.SetVoiceAvailable then
            self.hud:SetVoiceAvailable(self:IsAvailable())
        end
        if self.hud.SetVoiceFilterName then
            self.hud:SetVoiceFilterName(Config.VOICE_FILTERS[self.filterIdx].name)
        end
    end
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
    -- 上限秒数 + 余量：shim 侧在 duration+30s 结算，这个只兜“连 shim 都没回”的情况
    self.recordDeadline = Config.VOICE_MAX_SEC + 35
    if self.hud and self.hud.SetVoiceRecording then
        self.hud:SetVoiceRecording(true)
    end
    self.sdk:RecordVoice(slot, Config.VOICE_MAX_SEC)
end

--- 录音看门狗（订阅见 Build）：超时还在挂着就解锁面板，别让玩家对着“录音中”干等。
---@param dt number
function Voice:OnUpdate(dt)
    if not self.recording or self.recordDeadline <= 0 then
        return
    end
    self.recordDeadline = self.recordDeadline - (dt or 0)
    if self.recordDeadline > 0 then
        return
    end
    self.recordDeadline = 0
    self.recording = false
    print("Voice: recording never settled; unlocking the panel")
    if self.hud then
        if self.hud.SetVoiceRecording then
            self.hud:SetVoiceRecording(false)
        end
        if self.hud.SetVoiceStatus then
            self.hud:SetVoiceStatus("录音没响应，再试一次")
        end
    end
end

--- 试听（面板按钮）：优先播最近录的槽位（没录过则播 lose 槽）。
function Voice:Preview()
    if not self:IsAvailable() then
        return
    end
    local slot = self.lastSlot or Config.VOICE_SLOT_LOSE
    self.previewing = true
    -- 新的回放顶掉上一条：那条玩法回放是被主动打断的，不该再回落预置旁白
    self.pendingPlayId = 0
    self.pendingFallback = nil
    self.sdk:PlayUserAudio(slot, self:CurrentRate())
end

--- 玩法触发回放（失守吐槽/连杀战吼）：录过就播你的，没录过回落预置旁白。
--- 触发点不弹 UI（战场上突然冒“没录过音”比没声音更糟），所以回落靠宿主回放失败事件。
---@param slot string Config.VOICE_SLOT_*
---@param fallback fun()|nil 宿主里没有这段录音时补播的那句旁白
function Voice:Play(slot, fallback)
    if not self:IsAvailable() then
        if fallback then
            fallback()
        end
        return
    end
    self.pendingFallback = fallback
    self.pendingPlayId = self.sdk:PlayUserAudio(slot, self:CurrentRate())
end

--- 停掉当前回放（换局时调，避免上一局的语音拖进新局）。
function Voice:Stop()
    self.pendingPlayId = 0
    self.pendingFallback = nil
    self.previewing = false
    if not self:IsAvailable() then
        return
    end
    self.sdk:StopUserAudio()
end

--- 清除某个槽位的录音（面板按钮）：删掉宿主里那句，该槽位从此回到预置旁白。
--- 录过再清、清完再录都行；没录过就点清除也算成功（本来就是没录过的状态）。
---@param slot string Config.VOICE_SLOT_*
function Voice:Clear(slot)
    if not self:IsAvailable() then
        return
    end
    if self.recording then
        return  -- 录音中不清（面板是模态的，正常走不到）
    end
    -- 先停下回放：不去删一个正被宿主音频上下文打开的文件
    self:Stop()
    self.pendingClearSlot = slot
    self.sdk:ClearUserAudio(slot)
end

--- MinigameSdkResult 事件分发：录音/试听结果回写 HUD 状态行。
function Voice:OnSdkResult(data)
    local kind = data.Kind
    if kind == MINIGAME_REQUEST_KIND.RECORD_VOICE then
        self.recording = false
        self.recordDeadline = 0
        if self.hud and self.hud.SetVoiceRecording then
            self.hud:SetVoiceRecording(false)
        end
        if data.Success then
            -- Payload 形如 {"durationMs":2143}；槽位由发起方记在 pendingSlot
            self.lastSlot = self.pendingSlot or self.lastSlot
            if self.hud and self.hud.SetVoiceStatus then
                self.hud:SetVoiceStatus("录好了！听听变声效果")
            end
            Tracker.Report("voice_record", { slot = self.pendingSlot, ok = true })
            -- 录完立刻回放一遍：变声的听感就是这功能的全部，别让玩家自己再点一次
            self:Preview()
        else
            -- 宿主回来的失败原文（errMsg/errCode）进日志：真机上只有这一条线索能把
            -- “没授权”“设备忙”“格式不支持”区分开，状态行只放玩家能动作的那一句。
            local reason = tostring(data.Payload or "")
            print("Voice: recording failed: " .. reason)
            Tracker.Report("voice_record", {
                slot = self.pendingSlot,
                ok = false,
                denied = string.find(reason, "permission denied", 1, true) ~= nil,
            })
            if self.hud and self.hud.SetVoiceStatus then
                if string.find(reason, "permission denied", 1, true) then
                    self.hud:SetVoiceStatus("麦克风没开权限：小游戏和抖音 App 都要允许")
                else
                    -- 日志里已有宿主原话（上面那条 print），状态行给玩家能动作的文案。
                    self.hud:SetVoiceStatus("录音失败，再试一次")
                end
            end
        end
    elseif kind == MINIGAME_REQUEST_KIND.CLEAR_USER_AUDIO then
        local slot = self.pendingClearSlot
        self.pendingClearSlot = nil
        Tracker.Report("voice_clear", { slot = slot, ok = data.Success })
        if data.Success then
            -- 试听的默认槽位不能还指向一个已经没了的文件
            if self.lastSlot == slot then
                self.lastSlot = nil
            end
            if self.hud and self.hud.SetVoiceStatus then
                self.hud:SetVoiceStatus("已清除，改回预置旁白")
            end
        elseif self.hud and self.hud.SetVoiceStatus then
            self.hud:SetVoiceStatus("清除失败，再试一次")
        end
    elseif kind == MINIGAME_REQUEST_KIND.PLAY_USER_AUDIO then
        -- 玩法回放失败（该槽位没录过 / 录音文件被清理）：补播预置旁白。
        -- 按请求 id 认领，被后一次回放顶掉的旧请求在这里对不上，不会误触发。
        if self.pendingPlayId ~= 0 and data.RequestId == self.pendingPlayId then
            local fallback = self.pendingFallback
            self.pendingPlayId = 0
            self.pendingFallback = nil
            if not data.Success and fallback then
                fallback()
            end
        end
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

-- Scripts/game/Audio.lua
--
-- 双后端，照 FireGame/Data/Scripts/game/Audio.lua 的分工：
--   * 编辑器/桌面/web：引擎自己解码 Sound/*.ogg，每声新建一个 SoundSource、播完自动回收。
--     常驻源会留着上一次的 frequency_（SoundSource::Play 只在 frequency_==0 时设采样率），
--     采样率不同的音效共用一个源就串音高。
--   * 抖音真机：宿主不给 WebAudio、引擎没有 PCM 出口，改走 MinigameSDK:PlayGameAudio，
--     由宿主 InnerAudioContext 播打包时转出的 m4a。
--
-- 母带必须是 .ogg：出包的 TranscodeStagedAudio（BuildStepsMinigame.cpp）只扫 *.ogg，
-- 留 .wav 的话它原样进包、真机一声不响。路径两边通用，小游戏侧只换扩展名。

local Audio = {}
Audio.__index = Audio

---@param path string
---@return string
local function ToHostPath(path)
    return (path:gsub("%.ogg$", ".m4a"))
end

function Audio.Build(scene)
    local self = setmetatable({}, Audio)
    self.scene = scene
    self.sdk = GetSubsystem("MinigameSDK")
    self.minigame = self.sdk ~= nil and self.sdk:IsAvailable()
    self.warned = {}
    if self.minigame then
        SubscribeToEvent("MinigameSdkResult", function(data)
            if data.Kind == MINIGAME_REQUEST_KIND.PLAY_GAME_AUDIO and not data.Success then
                local key = tostring(data.Payload)
                if not self.warned[key] then
                    self.warned[key] = true
                    print("FishingGuy MARK host playback failed: " .. key)
                end
            end
        end)
    end
    return self
end

function Audio:Play(path, gain)
    if self.minigame then
        self.sdk:PlayGameAudio(ToHostPath(path), gain or 0.7, false)
        return
    end
    local snd = GetResource("Sound", path)
    if not snd then
        print("FishingGuy MARK missing sound: " .. path)
        return
    end
    local src = self.scene:CreateComponent("SoundSource")
    src:SetAutoRemoveMode(REMOVE.COMPONENT)
    src:SetGain(gain or 0.7)
    src:Play(snd)
end

Audio.BGM_PATH = "Sound/back_ground.ogg"
Audio.BGM_GAIN = 0.30

--- 背景音乐：桌面走常驻的循环源，抖音走 SDK 的 looped 播放。
--- 和上面「一声一源」相反，BGM 必须独占一个源，否则会被下一个音效的 frequency_ 带跑调。
--- 循环标志在 Sound 资源上（SoundSource 没绑 SetLooped，混音时读的是 Sound::IsLooped），
--- 它改的是 ResourceCache 里的共享对象，所以这个文件只能当 BGM 用。
function Audio:BgmStart()
    if self.bgmOn then
        return
    end
    self.bgmOn = true
    if self.minigame then
        self.sdk:PlayGameAudio(ToHostPath(self.BGM_PATH), self.BGM_GAIN, true)
        return
    end
    local snd = GetResource("Sound", self.BGM_PATH)
    if not snd then
        print("FishingGuy MARK missing sound: " .. self.BGM_PATH)
        return
    end
    snd:SetLooped(true)
    local src = self.scene:CreateComponent("SoundSource")
    src:SetGain(self.BGM_GAIN)
    src:Play(snd)
end

function Audio:Bite() self:Play("Sound/bite.ogg", 0.9) end
function Audio:Cast() self:Play("Sound/cast.ogg", 0.6) end
function Audio:Splash() self:Play("Sound/splash.ogg", 0.6) end
function Audio:Catch() self:Play("Sound/catch.ogg", 0.8) end
function Audio:Fail() self:Play("Sound/fail.ogg", 0.7) end
function Audio:Click() self:Play("Sound/click.ogg", 0.4) end

return Audio

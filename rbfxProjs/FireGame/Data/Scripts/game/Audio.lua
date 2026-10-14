-- Scripts/game/Audio.lua
--
-- 音效与旁白播放。
--
-- 为什么每声都新建一个 SoundSource（而不是像计划最初设想的那样常驻一个池轮转）：
--   SoundSource::Play 只在 frequency_==0 时才把频率设成声音自带的采样率（SoundSource.cpp:188），
--   一旦某个 source 播过一首，它的 frequency_ 就非零了，再拿去播另一首采样率不同的 ogg 会沿用
--   旧频率 → 串音高。而 Lua 侧读不到 Sound 的基础频率（Sound 只绑了 SetLooped），没法每次校正。
--   所以这里照 14_SoundEffects 的做法：每声建一个组件 + SetAutoRemoveMode(REMOVE.COMPONENT)，
--   播完引擎自动回收，既没有频率残留，也没有节点堆积。
--
-- 防"机关枪式重复感"靠 Config.Sfx 里每个事件的 5 个变体随机取，而不是靠 SetFrequency 变调
-- （同样是拿不到基础频率、且绝对 Hz 对不同采样率文件会错）。真要变调等确认全部 ogg 同采样率再加。
--
-- 双播放后端：
--   * 桌面/web：引擎 Sound 解码 ogg，SoundSource 出声（上面的论述都是这条路的）。
--   * 抖音小游戏：宿主没有 WebAudio，引擎没有 PCM 出口，改走 MinigameSDK 的
--     PlayGameAudio —— 宿主 InnerAudioContext 直接播打包时由 ogg 转出的 m4a
--     （Build 的 DouyinRuntimeStep 转码，包里没有 ogg），一次一实例、BGM 循环常驻，
--     语义与桌面路一致。路径只做扩展名替换，Config 里的 ogg 路径两边通用。

local Config = require("game.Config")

---@class Audio 音效/旁白/BGM 播放：模块级，持有 scene
local Audio = {}
Audio.__index = Audio

---@param scene Scene
---@return Audio
function Audio.Build(scene)
    local self = setmetatable({}, Audio)
    self.scene = scene
    self.bgmSource = nil
    self.bgmPath = nil             -- 小游戏侧 BGM 幂等标记（宿主实例由 JS 池管）
    self.sdk = GetSubsystem("MinigameSDK")
    self.minigame = self.sdk ~= nil and self.sdk:IsAvailable()
    self.warned = {}               -- 小游戏侧已报过的失败路径，防刷屏
    if self.minigame then
        SubscribeToEvent("MinigameSdkResult", function(data)
            if data.Kind == MINIGAME_REQUEST_KIND.PLAY_GAME_AUDIO and not data.Success then
                local key = tostring(data.Payload)
                if not self.warned[key] then
                    self.warned[key] = true
                    print("Audio: host playback failed: " .. key)
                end
            end
        end)
    end
    return self
end

--- 小游戏侧路径换算：资源路径只换扩展名，m4a 由打包转码生成。
---@param path string
---@return string
local function ToHostPath(path)
    return (path:gsub("%.ogg$", ".m4a"))
end

--- 播放背景音乐（循环，不自动回收，只播一次不叠加）。
---@param path string Sound 资源路径
---@param gain number|nil 音量，缺省 0.3
function Audio:PlayBGM(path, gain)
    if self.minigame then
        if self.bgmPath then return end
        self.bgmPath = path
        self.sdk:PlayGameAudio(ToHostPath(path), gain or 0.3, true)
        return
    end
    if self.bgmSource then return end
    local sound = GetResource("Sound", path)
    if not sound then
        print("Audio: missing BGM " .. tostring(path))
        return
    end
    self.bgmSource = self.scene:CreateComponent("SoundSource")
    self.bgmSource:SetGain(gain or 0.3)
    sound:SetLooped(true)
    self.bgmSource:Play(sound)
end

--- 播一个音效事件（从 Config.Sfx[event] 的变体里随机取一个）。
---@param event string Config.Sfx 的键，如 "shot"
---@param gain number|nil 音量，缺省 1.0
function Audio:Play(event, gain)
    local variants = Config.Sfx[event]
    if not variants or #variants == 0 then
        return
    end
    local path = variants[math.random(#variants)]
    self:Emit(path, gain or 1.0)
end

--- 播一句旁白（单文件，不做变体）。name 是 Config.Voice 里的值。
---@param name string 如 "round_1" / "combo"
---@param gain number|nil
function Audio:Voice(name, gain)
    if not name then
        return
    end
    self:Emit(Config.VoicePath(name), gain or 1.0)
end

--- 底层：按资源路径播一次（桌面建自动回收 SoundSource，小游戏走宿主一次性实例）。
---@param path string Sound 资源名
---@param gain number
function Audio:Emit(path, gain)
    if self.minigame then
        self.sdk:PlayGameAudio(ToHostPath(path), gain or 1.0)
        return
    end
    local sound = GetResource("Sound", path)
    if not sound then
        print("Audio: missing Sound " .. tostring(path))
        return
    end
    local source = self.scene:CreateComponent("SoundSource")
    source:SetAutoRemoveMode(REMOVE.COMPONENT)
    if gain and gain ~= 1.0 then
        source:SetGain(gain)
    end
    source:Play(sound)
end

return Audio

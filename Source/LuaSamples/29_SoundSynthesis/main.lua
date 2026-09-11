-- LuaSamples/29_SoundSynthesis/main.lua
-- Lua port of Source/Samples/29_SoundSynthesis: a two-oscillator sound is
-- synthesized at runtime into a BufferedSoundStream and played through a
-- SoundSource. Cursor up/down adjusts the lowpass filter coefficient.

local app = Sample:new()
app.filter = 0.5
app.accumulator = 0.0
app.osc1 = 0.0
app.osc2 = 180.0

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.FREE)
    input:SetMouseVisible(true)

    self:CreateSound()
    self:CreateInstructions()
end

function app:CreateSound()
    -- Sound source needs a node so that it is considered enabled. The C++
    -- sample uses a detached Node; here we use a minimal scene child node,
    -- which behaves the same for an unpositioned mono source.
    local scene = CreateScene()
    self.scene = scene
    local node = scene:CreateChild("SoundNode")
    local source = node:CreateComponent("SoundSource")
    source:SetIgnoreSceneTimeScale(true)

    self.soundStream = BufferedSoundStream()
    -- Set format: 44100 Hz, sixteen bit, mono
    self.soundStream:SetFormat(44100, true, false)

    -- Start playback. There is no data in the stream yet, but the SoundSource
    -- will wait until there is data, as the stream is by default in the
    -- "don't stop at end" mode
    source:Play(self.soundStream)
end

function app:UpdateSound()
    -- Try to keep 1/10 seconds of sound in the buffer, to avoid both dropouts
    -- and unnecessary latency
    local targetLength = 1.0 / 10.0
    local requiredLength = targetLength - self.soundStream:GetBufferLength()
    if requiredLength < 0.0 then
        return
    end

    local numSamples = math.floor(self.soundStream:GetFrequency() * requiredLength)
    if numSamples <= 0 then
        return
    end

    -- Fill a buffer with a simple two-oscillator algorithm. The sound is
    -- over-amplified (distorted), clamped to the 16-bit range, and finally
    -- lowpass-filtered according to the coefficient. Samples are packed as
    -- little-endian 16-bit pairs into a Lua string.
    local parts = {}
    for i = 1, numSamples do
        self.osc1 = self.osc1 + 1.0
        if self.osc1 >= 360.0 then self.osc1 = self.osc1 - 360.0 end
        self.osc2 = self.osc2 + 1.002
        if self.osc2 >= 360.0 then self.osc2 = self.osc2 - 360.0 end

        local newValue = Clamp((Sin(self.osc1) + Sin(self.osc2)) * 100000.0, -32767.0, 32767.0)
        self.accumulator = Lerp(self.accumulator, newValue, self.filter)

        -- Two-complement little-endian 16-bit encoding
        local v = math.floor(self.accumulator) % 65536
        parts[i] = string.char(v % 256, math.floor(v / 256))
    end

    -- Queue buffer to the stream for playback
    self.soundStream:AddData(table.concat(parts))
end

function app:CreateInstructions()
    local cache = GetSubsystem("ResourceCache")

    self.instructionText = GetUIRoot():CreateChild("Text")
    self.instructionText:SetText("Use cursor up and down to control sound filtering")
    self.instructionText:SetFont(cache:GetResource("Font", "Fonts/Anonymous Pro.ttf"), 15)
    self.instructionText:SetTextAlignment(HA.CENTER)
    self.instructionText:SetHorizontalAlignment(HA.CENTER)
    self.instructionText:SetVerticalAlignment(VA.CENTER)
    self.instructionText:SetPosition(0, GetUIRoot():GetHeight() / 4)
end

function app:Update(timeStep)
    -- Use keys to control the filter constant
    local input = GetSubsystem("Input")
    if input:GetKeyDown(KEY.UP) then
        self.filter = self.filter + timeStep * 0.5
    end
    if input:GetKeyDown(KEY.DOWN) then
        self.filter = self.filter - timeStep * 0.5
    end
    self.filter = Clamp(self.filter, 0.01, 1.0)

    self.instructionText:SetText("Use cursor up and down to control sound filtering\n"
        .. "Coefficient: " .. string.format("%.3f", self.filter))

    self:UpdateSound()
end

app:Run()

-- LuaSamples/14_SoundEffects/main.lua
-- Lua port of Source/Samples/14_SoundEffects: UI-driven audio playback.
-- Buttons play sound effects / streamed music, sliders control master gain,
-- panning and reach, a checkbox routes output to the LFE channel, and a
-- dropdown + record buttons capture from a microphone into a
-- BufferedSoundStream.

local app = Sample:new()
app.pan = 0.0
app.reach = 0.0
app.lfe = false

local soundNames = { "Fist", "Explosion", "Power-up" }
local soundResourceNames = {
    "Sounds/PlayerFistHit.wav",
    "Sounds/BigExplosion.wav",
    "Sounds/Powerup.wav",
}

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.FREE)
    input:SetMouseVisible(true)

    -- Create a scene which will not be actually rendered, but is used to
    -- hold SoundSource components while they play sounds
    local scene = CreateScene()
    self.scene = scene

    -- Create music sound source. Set the sound type to music so that master
    -- volume control works correctly
    local musicSource = scene:CreateComponent("SoundSource")
    musicSource:SetSoundType(SOUND.MUSIC)
    self.musicSource = musicSource

    self:CreateUI()
end

function app:CreateButton(x, y, xSize, ySize, text)
    local root = GetSubsystem("UI"):GetRoot()

    -- Create the button and center the text onto it
    local button = root:CreateChild("Button")
    button:SetStyleAuto()
    button:SetPosition(x, y)
    button:SetSize(xSize, ySize)

    local buttonText = button:CreateChild("Text")
    buttonText:SetAlignment(HA.CENTER, VA.CENTER)
    buttonText:SetFont("Fonts/Anonymous Pro.ttf", 12)
    buttonText:SetText(text)
    return button
end

function app:CreateCheckbox(x, y, text)
    local root = GetSubsystem("UI"):GetRoot()

    local checkbox = root:CreateChild("CheckBox")
    checkbox:SetStyleAuto()
    checkbox:SetPosition(x, y)

    local checkboxText = checkbox:CreateChild("Text")
    checkboxText:SetAlignment(HA.LEFT, VA.CENTER)
    checkboxText:SetPosition(30, 0)
    checkboxText:SetFont("Fonts/Anonymous Pro.ttf", 12)
    checkboxText:SetText(text)
    return checkbox
end

function app:CreateSlider(x, y, xSize, ySize, text)
    local root = GetSubsystem("UI"):GetRoot()

    -- Create text and slider below it
    local sliderText = root:CreateChild("Text")
    sliderText:SetPosition(x, y)
    sliderText:SetFont("Fonts/Anonymous Pro.ttf", 12)
    sliderText:SetText(text)

    local slider = root:CreateChild("Slider")
    slider:SetStyleAuto()
    slider:SetPosition(x, y + 20)
    slider:SetSize(xSize, ySize)
    -- Use 0-1 range for controlling sound/music master volume
    slider:SetRange(1.0)
    return slider
end

function app:CreateUI()
    local ui = GetSubsystem("UI")
    local root = ui:GetRoot()
    local audio = GetSubsystem("Audio")

    -- Set style to the UI root so that elements will inherit it
    root:SetDefaultStyle(GetResource("XMLFile", "UI/DefaultStyle.xml"))

    local y = 20

    -- Create buttons for playing back sounds
    for i, name in ipairs(soundNames) do
        local button = self:CreateButton((i - 1) * 140 + 20, y, 120, 40, name)
        -- Store the sound effect resource name as a custom variable into the
        -- button (the C++ sample reads it back from the event sender; the Lua
        -- port closes over the resource name directly)
        local soundResource = soundResourceNames[i]
        SubscribeToEvent(button, "Pressed", function(data)
            self:PlaySound(soundResource)
        end)
    end

    y = y + 60
    -- Create buttons for playing/stopping music
    local button = self:CreateButton(20, y, 120, 40, "Play Music")
    SubscribeToEvent(button, "Released", function(data)
        local music = GetResource("Sound", "Music/Ninja Gods.ogg")
        -- Set the song to loop
        music:SetLooped(true)
        self.musicSource:Play(music)
    end)

    button = self:CreateButton(160, y, 120, 40, "Stop Music")
    SubscribeToEvent(button, "Released", function(data)
        self.musicSource:Stop()
    end)

    y = y + 60
    -- Create buttons and slider for scene time control
    button = self:CreateButton(20, y, 120, 40, "Pause Scene")
    SubscribeToEvent(button, "Released", function(data)
        self.scene:SetUpdateEnabled(false)
    end)

    button = self:CreateButton(160, y, 120, 40, "Resume Scene")
    SubscribeToEvent(button, "Released", function(data)
        self.scene:SetUpdateEnabled(true)
    end)

    local slider = self:CreateSlider(300, y, 200, 20, "Scene Time Scale")
    slider:SetValue(self.scene:GetTimeScale())
    slider:SetRange(2.0)
    SubscribeToEvent(slider, "SliderChanged", function(data)
        self.scene:SetTimeScale(data.Value)
    end)

    y = y + 60
    -- Create sliders for controlling sound and music master volume
    slider = self:CreateSlider(20, y, 200, 20, "Sound Volume")
    slider:SetValue(audio:GetMasterGain(SOUND.EFFECT))
    SubscribeToEvent(slider, "SliderChanged", function(data)
        audio:SetMasterGain(SOUND.EFFECT, data.Value)
    end)

    y = y + 60
    slider = self:CreateSlider(20, y, 200, 20, "Music Volume")
    slider:SetValue(audio:GetMasterGain(SOUND.MUSIC))
    SubscribeToEvent(slider, "SliderChanged", function(data)
        audio:SetMasterGain(SOUND.MUSIC, data.Value)
    end)

    y = y + 60
    slider = self:CreateSlider(20, y, 200, 20, "Sound Panning")
    slider:SetValue(0.5)
    SubscribeToEvent(slider, "SliderChanged", function(data)
        self.pan = data.Value * 2.0 - 1.0
    end)

    y = y + 60
    slider = self:CreateSlider(20, y, 200, 20, "Sound Reach")
    slider:SetValue(0.5)
    SubscribeToEvent(slider, "SliderChanged", function(data)
        self.reach = data.Value * 2.0 - 1.0
    end)

    y = y + 60
    local checkbox = self:CreateCheckbox(20, y, "Output to LFE")
    checkbox:SetChecked(false)
    SubscribeToEvent(checkbox, "Toggled", function(data)
        self.lfe = data.State
    end)

    y = y + 60
    -- Microphone picker dropdown plus record start/stop buttons
    local micPicker = root:CreateChild("DropDownList")
    micPicker:SetName("MIC_PICKER")
    micPicker:SetStyleAuto()
    micPicker:SetPosition(20, y)
    micPicker:SetSize(300, 20)

    for _, mic in ipairs(audio:EnumerateMicrophones()) do
        local item = GetSubsystem("UI"):GetRoot():CreateChild("Text")
        item:SetText(mic)
        item:SetStyleAuto()
        micPicker:AddItem(item)
    end

    y = y + 60
    button = self:CreateButton(20, y, 120, 40, "Start Record")
    SubscribeToEvent(button, "Released", function(data)
        self:StartMicRecord()
    end)

    button = self:CreateButton(160, y, 120, 40, "Stop Record")
    SubscribeToEvent(button, "Released", function(data)
        self:StopMicRecord()
    end)
end

function app:PlaySound(soundResource)
    local sound = GetResource("Sound", soundResource)
    if not sound then
        return
    end

    -- Create a SoundSource component for playing the sound. The SoundSource
    -- component plays non-positional audio; for positional sounds the
    -- SoundSource3D component would be used instead
    local soundSource = self.scene:CreateComponent("SoundSource")
    -- Component will automatically remove itself when the sound finished
    -- playing
    soundSource:SetAutoRemoveMode(REMOVE.COMPONENT)
    soundSource:Play(sound)
    -- In case we also play music, set the sound volume below maximum so that
    -- we don't clip the output
    soundSource:SetGain(0.75)
    soundSource:SetPanning(self.pan)
    soundSource:SetReach(self.reach)
    soundSource:SetLowFrequency(self.lfe)
end

function app:StartMicRecord()
    if self.activeMic then
        return
    end

    local micPicker = GetSubsystem("UI"):GetRoot():GetChild("MIC_PICKER", true)
    if micPicker and micPicker:GetNumItems() > 0 and micPicker:GetSelectedItem() then
        local micName = micPicker:GetSelectedItem():GetText()
        local audio = GetSubsystem("Audio")
        local mic = audio:CreateMicrophone(micName, false, 16000, 64)
        if mic then
            self.activeMic = mic
            local micStream = BufferedSoundStream()
            micStream:SetFormat(mic:GetFrequency(), true, false)
            mic:Link(micStream)
            self.micStream = micStream
        end
    else
        print("No microphones detected")
    end
end

function app:StopMicRecord()
    self.activeMic = nil

    if self.micStream then
        local soundSource = self.scene:CreateComponent("SoundSource")
        -- Component will automatically remove itself when the sound finished
        -- playing
        soundSource:SetAutoRemoveMode(REMOVE.COMPONENT)
        soundSource:Play(self.micStream)
        soundSource:SetGain(1.0)
        soundSource:SetPanning(0.0)
        soundSource:SetReach(0.0)
        self.micStream = nil
    end
end

app:Run()

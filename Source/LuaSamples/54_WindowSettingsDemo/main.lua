-- LuaSamples/54_WindowSettingsDemo/main.lua
-- Lua port of Source/Samples/54_WindowSettingsDemo: a settings window that
-- controls monitor, resolution, fullscreen/borderless/resizable/vsync flags
-- and MSAA through Graphics:SetDefaultWindowModes. The list of resolutions
-- comes from RenderDevice:GetFullscreenModes and stays synchronized with the
-- actual window state on ScreenMode events.

local M_MAX_UNSIGNED = 0xFFFFFFFF

local function CeilToInt(value)
    return math.ceil(value)
end

local function RoundToInt(value)
    return math.floor(value + 0.5)
end

local app = Sample:new()

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.FREE)
    input:SetMouseVisible(true)

    local root = GetUIRoot()
    root:SetDefaultStyle(GetResource("XMLFile", "UI/DefaultStyle.xml"))

    self:InitSettings()
    self:SynchronizeSettings()
    SubscribeToEvent("ScreenMode", function() self:SynchronizeSettings() end)

    self:CreateScene()
end

function app:CreateScene()
    self.scene = CreateScene()
    local scene = self.scene
    scene:CreateComponent("Octree")

    local zone = scene:CreateComponent("Zone")
    zone:SetAmbientColor(Color.WHITE)
    zone:SetBoundingBox(BoundingBox(-1000.0, 1000.0))

    -- 3D object
    local objectNode = scene:CreateChild("Object")
    objectNode:SetRotation(Quaternion(45.0, 45.0, 45.0))
    local objectModel = objectNode:CreateComponent("StaticModel")
    objectModel:SetModel(GetResource("Model", "Models/Box.mdl"))
    objectModel:SetMaterial(GetResource("Material", "Materials/Stone.xml"))

    -- Camera
    self.cameraNode = scene:CreateChild("Camera")
    self.cameraNode:CreateComponent("Camera")
    self.cameraNode:SetPosition(Vector3(0.0, 0.0, -4.0))

    -- Rotate object
    SubscribeToEvent(scene, "SceneUpdate", function(data)
        objectNode:Rotate(Quaternion(0.0, 20.0 * data.TimeStep, 0.0), TS.WORLD)
    end)

    SetViewport(0, scene, self.cameraNode:GetComponent("Camera"))
end

function app:InitSettings()
    local graphics = GetSubsystem("Graphics")
    local window = GetUIRoot():CreateChild("Window", "Window")

    -- Window size and layout settings
    window:SetPosition(128, 128)
    window:SetMinWidth(300)
    window:SetLayout(LM.VERTICAL, 6, IntRect(6, 6, 6, 6))
    window:SetMovable(true)
    window:SetStyleAuto()

    -- Window title
    local windowTitle = window:CreateChild("Text", "WindowTitle")
    windowTitle:SetText("Window Settings")
    windowTitle:SetStyleAuto()

    -- Monitor selector
    self.monitorControl = window:CreateChild("DropDownList", "Monitor")
    self.monitorControl:SetMinHeight(24)
    self.monitorControl:SetStyleAuto()
    for i = 0, graphics:GetMonitorCount() - 1 do
        local text = Text()
        text:SetText(string.format("Monitor %d", i))
        text:SetMinWidth(CeilToInt(text:GetRowWidth(0) + 10))
        self.monitorControl:AddItem(text)
        text:SetStyleAuto()
    end

    -- Resolution selector
    self.resolutionLabel = window:CreateChild("Text", "Resolution Label")
    self.resolutionLabel:SetText("???")
    self.resolutionLabel:SetStyleAuto()

    self.resolutionControl = window:CreateChild("ListView", "Resolution")
    self.resolutionControl:SetMinHeight(256)
    self.resolutionControl:SetStyleAuto()

    local resolutionPlaceholder = Text()
    resolutionPlaceholder:SetText("[Cannot fill list of resolutions]")
    resolutionPlaceholder:SetMinWidth(CeilToInt(resolutionPlaceholder:GetRowWidth(0) + 10))
    self.resolutionControl:AddItem(resolutionPlaceholder)
    resolutionPlaceholder:SetStyleAuto()

    -- Fullscreen controller
    local fullscreenFrame = window:CreateChild("UIElement", "Fullscreen Frame")
    fullscreenFrame:SetMinHeight(24)
    fullscreenFrame:SetLayout(LM.HORIZONTAL, 6)
    self.fullscreenControl = fullscreenFrame:CreateChild("CheckBox", "Fullscreen Control")
    self.fullscreenControl:SetStyleAuto()
    local fullscreenText = fullscreenFrame:CreateChild("Text", "Fullscreen Label")
    fullscreenText:SetText("Fullscreen")
    fullscreenText:SetMinWidth(CeilToInt(fullscreenText:GetRowWidth(0) + 10))
    fullscreenText:SetStyleAuto()

    -- Borderless controller
    local borderlessFrame = window:CreateChild("UIElement", "Borderless Frame")
    borderlessFrame:SetMinHeight(24)
    borderlessFrame:SetLayout(LM.HORIZONTAL, 6)
    self.borderlessControl = borderlessFrame:CreateChild("CheckBox", "Borderless Control")
    self.borderlessControl:SetStyleAuto()
    local borderlessText = borderlessFrame:CreateChild("Text", "Borderless Label")
    borderlessText:SetText("Borderless")
    borderlessText:SetMinWidth(CeilToInt(borderlessText:GetRowWidth(0) + 10))
    borderlessText:SetStyleAuto()

    -- Resizable controller
    local resizableFrame = window:CreateChild("UIElement", "Resizable Frame")
    resizableFrame:SetMinHeight(24)
    resizableFrame:SetLayout(LM.HORIZONTAL, 6)
    self.resizableControl = resizableFrame:CreateChild("CheckBox", "Resizable Control")
    self.resizableControl:SetStyleAuto()
    local resizableText = resizableFrame:CreateChild("Text", "Resizable Label")
    resizableText:SetText("Resizable")
    resizableText:SetMinWidth(CeilToInt(resizableText:GetRowWidth(0) + 10))
    resizableText:SetStyleAuto()

    -- V-Sync controller
    local vsyncFrame = window:CreateChild("UIElement", "V-Sync Frame")
    vsyncFrame:SetMinHeight(24)
    vsyncFrame:SetLayout(LM.HORIZONTAL, 6)
    self.vsyncControl = vsyncFrame:CreateChild("CheckBox", "V-Sync Control")
    self.vsyncControl:SetStyleAuto()
    local vsyncText = vsyncFrame:CreateChild("Text", "V-Sync Label")
    vsyncText:SetText("V-Sync")
    vsyncText:SetMinWidth(CeilToInt(vsyncText:GetRowWidth(0) + 10))
    vsyncText:SetStyleAuto()

    -- Multi-sample controller from 1 (= 2^0) to 16 (= 2^4)
    self.multiSampleControl = window:CreateChild("DropDownList", "Multi-Sample Control")
    self.multiSampleControl:SetMinHeight(24)
    self.multiSampleControl:SetStyleAuto()
    for i = 0, 4 do
        local text = Text()
        text:SetText(i == 0 and "No MSAA" or string.format("MSAA x%d", 2 ^ i))
        text:SetMinWidth(CeilToInt(text:GetRowWidth(0) + 10))
        self.multiSampleControl:AddItem(text)
        text:SetStyleAuto()
    end

    -- "Apply" button
    local applyButton = window:CreateChild("Button", "Apply")
    applyButton:SetLayout(LM.HORIZONTAL, 6, IntRect(6, 6, 6, 6))
    applyButton:SetStyleAuto()

    local applyButtonText = applyButton:CreateChild("Text", "Apply Text")
    applyButtonText:SetAlignment(HA.CENTER, VA.CENTER)
    applyButtonText:SetText("Apply")
    applyButtonText:SetStyleAuto()

    applyButton:SetFixedWidth(CeilToInt(applyButtonText:GetRowWidth(0) + 20))
    applyButton:SetFixedHeight(30)

    -- Apply settings when "Apply" button is clicked
    SubscribeToEvent(applyButton, "Released", function()
        self:HandleApply()
    end)
end

function app:HandleApply()
    local graphics = GetSubsystem("Graphics")
    local renderDevice = GetSubsystem("RenderDevice")

    local monitor = self.monitorControl:GetSelection()
    if monitor == M_MAX_UNSIGNED then
        return
    end

    local fullscreenModes = renderDevice:GetFullscreenModes(monitor)
    local selectedMode = self.resolutionControl:GetSelection()
    if selectedMode == M_MAX_UNSIGNED or selectedMode >= #fullscreenModes then
        return
    end

    local windowSettings = {}

    if self.fullscreenControl:IsChecked() then
        windowSettings.mode = WMODE.FULLSCREEN
    elseif self.borderlessControl:IsChecked() then
        windowSettings.mode = WMODE.BORDERLESS
    end
    windowSettings.resizable = self.resizableControl:IsChecked()
    windowSettings.vSync = self.vsyncControl:IsChecked()

    local multiSampleSelection = self.multiSampleControl:GetSelection()
    if multiSampleSelection == M_MAX_UNSIGNED then
        windowSettings.multiSample = 1
    else
        windowSettings.multiSample = 2 ^ multiSampleSelection
    end

    local mode = fullscreenModes[selectedMode + 1]
    local dpiScale = 1.0
    if windowSettings.mode == WMODE.FULLSCREEN then
        dpiScale = renderDevice:GetDpiScale()
    end
    windowSettings.width = RoundToInt(mode.width / dpiScale)
    windowSettings.height = RoundToInt(mode.height / dpiScale)
    windowSettings.refreshRate = mode.refreshRate

    graphics:SetDefaultWindowModes(windowSettings)
end

function app:SynchronizeSettings()
    local renderDevice = GetSubsystem("RenderDevice")
    local windowSettings = renderDevice:GetWindowSettings()

    -- Synchronize monitor
    self.monitorControl:SetSelection(windowSettings.monitor)

    -- Synchronize resolution list
    self.resolutionControl:RemoveAllItems()
    local fullscreenModes = renderDevice:GetFullscreenModes(windowSettings.monitor)
    for _, mode in ipairs(fullscreenModes) do
        local resolutionEntry = Text()
        resolutionEntry:SetText(string.format("%dx%d, %d Hz", mode.width, mode.height, mode.refreshRate))
        resolutionEntry:SetMinWidth(CeilToInt(resolutionEntry:GetRowWidth(0) + 10))
        self.resolutionControl:AddItem(resolutionEntry)
        resolutionEntry:SetStyle("FileSelectorListText")
    end

    -- Synchronize selected resolution
    local currentSwapChainSize = renderDevice:GetSwapChainSize()
    local currentModeIndex = renderDevice:GetClosestFullscreenModeIndex(
        fullscreenModes, currentSwapChainSize, windowSettings.refreshRate)
    self.resolutionControl:SetSelection(currentModeIndex)
    self.resolutionLabel:SetText(string.format("Current: %dx%d, %d Hz",
        currentSwapChainSize.x, currentSwapChainSize.y, windowSettings.refreshRate))

    -- Synchronize fullscreen and borderless flags
    self.fullscreenControl:SetChecked(windowSettings.mode == WMODE.FULLSCREEN)
    self.borderlessControl:SetChecked(windowSettings.mode == WMODE.BORDERLESS)
    self.resizableControl:SetChecked(windowSettings.resizable)
    self.vsyncControl:SetChecked(windowSettings.vSync)

    -- Synchronize MSAA
    for i = 0, 4 do
        if windowSettings.multiSample == 2 ^ i then
            self.multiSampleControl:SetSelection(i)
        end
    end
end

app:Run()

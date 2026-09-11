-- LuaSamples/40_Localization/main.lua
-- Lua port of Source/Samples/40_Localization (L10n): loads localized strings
-- from JSON files, switches languages with a button and updates UI / Text3D
-- on the ChangeLanguage event. One button text is auto-localized.

local app = Sample:new()
app.yaw = 0.0
app.pitch = 0.0

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.FREE)
    input:SetMouseVisible(true)
    input:CenterMousePosition()

    self:InitLocalizationSystem()
    self:CreateScene()
    self:CreateGUI()
end

function app:InitLocalizationSystem()
    local l10n = GetSubsystem("Localization")
    -- JSON files must be in UTF8 encoding without BOM.
    -- The first found language becomes current.
    l10n:LoadJSONFile("StringsEnRu.json")
    -- Multiple files can be loaded
    l10n:LoadJSONFile("StringsDe.json")
    l10n:LoadJSONFile("StringsLv.json", "lv")
    -- Hook up to the change language event
    SubscribeToEvent("ChangeLanguage", function(data) self:HandleChangeLanguage() end)
end

function app:CreateScene()
    local scene = CreateScene()
    self.scene = scene
    scene:CreateComponent("Octree")

    local zone = scene:CreateComponent("Zone")
    zone:SetBoundingBox(BoundingBox(-1000.0, 1000.0))
    zone:SetAmbientColor(Color(0.5, 0.5, 0.5))
    zone:SetFogColor(Color(0.4, 0.5, 0.8))
    zone:SetFogStart(1.0)
    zone:SetFogEnd(100.0)

    local planeNode = scene:CreateChild("Plane")
    planeNode:SetScale(Vector3(300.0, 1.0, 300.0))
    local planeObject = planeNode:CreateComponent("StaticModel")
    planeObject:SetModel(GetResource("Model", "Models/Plane.mdl"))
    planeObject:SetMaterial(GetResource("Material", "Materials/StoneTiled.xml"))

    local lightNode = scene:CreateChild("DirectionalLight")
    lightNode:SetDirection(Vector3(0.6, -1.0, 0.8))
    local light = lightNode:CreateComponent("Light")
    light:SetLightType(LIGHT.DIRECTIONAL)
    light:SetColor(Color(0.8, 0.8, 0.8))

    self.cameraNode = scene:CreateChild("Camera")
    self.cameraNode:CreateComponent("Camera")
    self.cameraNode:SetPosition(Vector3(0.0, 10.0, -30.0))

    local l10n = GetSubsystem("Localization")
    local text3DNode = scene:CreateChild("Text3D")
    text3DNode:SetPosition(Vector3(0.0, 0.1, 30.0))
    local text3D = text3DNode:CreateComponent("Text3D")

    -- Manually set text in the current language
    text3D:SetText(l10n:GetString("lang"))
    text3D:SetFont(GetResource("Font", "Fonts/Anonymous Pro.ttf"), 30)
    text3D:SetColor(Color(0.0, 0.0, 0.0))
    text3D:SetAlignment(HA.CENTER, VA.BOTTOM)
    text3DNode:SetScale(15)

    SetViewport(0, scene, self.cameraNode:GetComponent("Camera"))
end

function app:CreateGUI()
    local l10n = GetSubsystem("Localization")
    local root = GetUIRoot()
    root:SetDefaultStyle(GetResource("XMLFile", "UI/DefaultStyle.xml"))

    local window = root:CreateChild("Window")
    window:SetMinSize(384, 192)
    window:SetLayout(LM.VERTICAL, 6, IntRect(6, 6, 6, 6))
    window:SetAlignment(HA.CENTER, VA.CENTER)
    window:SetStyleAuto()

    local windowTitle = window:CreateChild("Text", "WindowTitle")
    windowTitle:SetStyleAuto()

    -- Current language is "en" because it was found first
    local langName = l10n:GetLanguage()
    local langIndex = l10n:GetLanguageIndex() -- == 0 at the beginning
    local localizedString = l10n:GetString("title")

    windowTitle:SetText(localizedString .. " (" .. langIndex .. " " .. langName .. ")")

    local b = window:CreateChild("Button")
    b:SetStyle("Button")
    b:SetMinHeight(24)

    local t = b:CreateChild("Text", "ButtonTextChangeLang")
    -- The shown text changes automatically when the language changes;
    -- the text value acts as the string identifier in this mode
    t:SetAutoLocalizable(true)
    t:SetText("Press this button")
    t:SetAlignment(HA.CENTER, VA.CENTER)
    t:SetStyle("Text")
    SubscribeToEvent(b, "Released", function() self:HandleChangeLangButtonPressed() end)

    b = window:CreateChild("Button")
    b:SetStyle("Button")
    b:SetMinHeight(24)
    t = b:CreateChild("Text", "ButtonTextQuit")
    t:SetAlignment(HA.CENTER, VA.CENTER)
    t:SetStyle("Text")

    -- Manually set text in the current language
    t:SetText(l10n:GetString("quit"))

    SubscribeToEvent(b, "Released", function() self:HandleQuitButtonPressed() end)
end

function app:Update(timeStep)
    local input = GetSubsystem("Input")
    local MOUSE_SENSITIVITY = 0.1
    local mouseMove = input:GetMouseMove()
    self.yaw = self.yaw + MOUSE_SENSITIVITY * mouseMove.x
    self.pitch = Clamp(self.pitch + MOUSE_SENSITIVITY * mouseMove.y, -90.0, 90.0)
    self.cameraNode:SetRotation(Quaternion(self.pitch, self.yaw, 0.0))
end

function app:HandleChangeLangButtonPressed()
    local l10n = GetSubsystem("Localization")
    -- Languages are numbered in the loading order
    local lang = l10n:GetLanguageIndex()
    lang = lang + 1
    if lang >= l10n:GetNumLanguages() then
        lang = 0
    end
    l10n:SetLanguage(lang)
end

function app:HandleQuitButtonPressed()
    GetSubsystem("Engine"):Exit()
end

-- Manually change texts when the language is changed
function app:HandleChangeLanguage()
    local l10n = GetSubsystem("Localization")
    local root = GetUIRoot()

    local windowTitle = root:GetChild("WindowTitle", true)
    windowTitle:SetText(l10n:GetString("title") .. " (" .. l10n:GetLanguageIndex() .. " " .. l10n:GetLanguage() .. ")")

    local buttonText = root:GetChild("ButtonTextQuit", true)
    buttonText:SetText(l10n:GetString("quit"))

    local text3D = self.scene:GetChild("Text3D"):GetComponent("Text3D")
    text3D:SetText(l10n:GetString("lang"))

    -- The "Press this button" text changes automatically
end

app:Run()

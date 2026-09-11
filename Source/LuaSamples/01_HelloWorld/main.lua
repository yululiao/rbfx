-- LuaSamples/01_HelloWorld/main.lua
-- Lua port of Source/Samples/01_HelloWorld: centered "Hello World" text.

local app = Sample:new()

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.FREE)
    input:SetMouseVisible(true)

    local ui = GetSubsystem("UI")
    local helloText = ui:GetRoot():CreateChild("Text")
    helloText:SetText("Hello World from Urho3D Lua!")
    local font = GetResource("Font", "Fonts/Anonymous Pro.ttf")
    helloText:SetFont(font, 30)
    helloText:SetColor(Color(0.0, 1.0, 0.0))
    helloText:SetHorizontalAlignment(HA.CENTER)
    helloText:SetVerticalAlignment(VA.CENTER)
end

app:Run()

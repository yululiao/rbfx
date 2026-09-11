-- LuaSamples/35_SignedDistanceFieldText/main.lua
-- Lua port of Source/Samples/35_SignedDistanceFieldText: 200 mushrooms
-- titled with SDF-font Text3D labels cycling through plain/shadow/stroke
-- text effects.

local app = Sample:new()

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.RELATIVE)
    input:SetMouseVisible(false)

    self:CreateScene()
    self:CreateInstructions()
end

function app:CreateScene()
    local scene = CreateScene()
    self.scene = scene
    scene:CreateComponent("Octree")

    -- Plane with stone material
    local planeNode = scene:CreateChild("Plane")
    planeNode:SetScale(Vector3(100.0, 1.0, 100.0))
    local planeObject = planeNode:CreateComponent("StaticModel")
    planeObject:SetModel(GetResource("Model", "Models/Plane.mdl"))
    planeObject:SetMaterial(GetResource("Material", "Materials/StoneTiled.xml"))

    -- Directional light
    local lightNode = scene:CreateChild("DirectionalLight")
    lightNode:SetDirection(Vector3(0.6, -1.0, 0.8))
    local light = lightNode:CreateComponent("Light")
    light:SetLightType(LIGHT.DIRECTIONAL)

    -- Randomly positioned mushrooms with SDF text titles
    local NUM_OBJECTS = 200
    for i = 0, NUM_OBJECTS - 1 do
        local mushroomNode = scene:CreateChild("Mushroom")
        mushroomNode:SetPosition(Vector3(Random(90.0) - 45.0, 0.0, Random(90.0) - 45.0))
        mushroomNode:SetScale(0.5 + Random(2.0))
        local mushroomObject = mushroomNode:CreateComponent("StaticModel")
        mushroomObject:SetModel(GetResource("Model", "Models/Mushroom.mdl"))
        mushroomObject:SetMaterial(GetResource("Material", "Materials/Mushroom.xml"))

        local mushroomTitleNode = mushroomNode:CreateChild("MushroomTitle")
        mushroomTitleNode:SetPosition(Vector3(0.0, 1.2, 0.0))
        local mushroomTitleText = mushroomTitleNode:CreateComponent("Text3D")
        mushroomTitleText:SetText("Mushroom " .. i)
        mushroomTitleText:SetFont(GetResource("Font", "Fonts/BlueHighway.sdf"), 24)

        mushroomTitleText:SetColor(Color(1.0, 0.0, 0.0))

        if i % 3 == 1 then
            mushroomTitleText:SetColor(Color(0.0, 1.0, 0.0))
            mushroomTitleText:SetTextEffect(TE.SHADOW)
            mushroomTitleText:SetEffectColor(Color(0.5, 0.5, 0.5))
        elseif i % 3 == 2 then
            mushroomTitleText:SetColor(Color(1.0, 1.0, 0.0))
            mushroomTitleText:SetTextEffect(TE.STROKE)
            mushroomTitleText:SetEffectColor(Color(0.5, 0.5, 0.5))
        end

        mushroomTitleText:SetAlignment(HA.CENTER, VA.CENTER)
    end

    -- Camera with WASD free-fly movement
    self.cameraNode = scene:CreateChild("Camera")
    self.cameraNode:CreateComponent("FreeFlyController")
    local camera = self.cameraNode:CreateComponent("Camera")
    camera:SetFarClip(300.0)
    self.cameraNode:SetPosition(Vector3(0.0, 5.0, 0.0))
    SetViewport(0, scene, camera)
end

function app:CreateInstructions()
    local ui = GetSubsystem("UI")
    local root = ui:GetRoot()

    local instructionText = root:CreateChild("Text")
    instructionText:SetText("Use WASD keys and mouse/touch to move")
    instructionText:SetFont("Fonts/Anonymous Pro.ttf", 15)
    instructionText:SetHorizontalAlignment(HA.CENTER)
    instructionText:SetVerticalAlignment(VA.CENTER)
    instructionText:SetPosition(0, root:GetHeight() / 4)
end

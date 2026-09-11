-- LuaSamples/31_MaterialAnimation/main.lua
-- Lua port of Source/Samples/31_MaterialAnimation: the shared Mushroom
-- material's MatSpecColor shader parameter is animated with a
-- ValueAnimation, so every mushroom's specular highlight pulses in sync.

local NUM_OBJECTS = 200

local app = Sample:new()

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.RELATIVE)
    input:SetMouseVisible(false)

    self:CreateScene()
    self:CreateInstructions()
end

function app:CreateScene()
    local cache = GetSubsystem("ResourceCache")

    local scene = CreateScene()
    self.scene = scene
    scene:CreateComponent("Octree")

    -- Large stone plane
    local planeNode = scene:CreateChild("Plane")
    planeNode:SetScale(Vector3(100.0, 1.0, 100.0))
    local planeObject = planeNode:CreateComponent("StaticModel")
    planeObject:SetModel(cache:GetResource("Model", "Models/Plane.mdl"))
    planeObject:SetMaterial(cache:GetResource("Material", "Materials/StoneTiled.xml"))

    -- Directional light
    local lightNode = scene:CreateChild("DirectionalLight")
    lightNode:SetDirection(Vector3(0.6, -1.0, 0.8))
    local light = lightNode:CreateComponent("Light")
    light:SetLightType(LIGHT.DIRECTIONAL)

    -- Animate the shared material's specular color parameter
    local mushroomMat = cache:GetResource("Material", "Materials/Mushroom.xml")
    local specColorAnimation = ValueAnimation()
    specColorAnimation:SetKeyFrame(0.0, Color(0.1, 0.1, 0.1, 16.0))
    specColorAnimation:SetKeyFrame(1.0, Color(1.0, 0.0, 0.0, 2.0))
    specColorAnimation:SetKeyFrame(2.0, Color(1.0, 1.0, 0.0, 2.0))
    specColorAnimation:SetKeyFrame(3.0, Color(0.1, 0.1, 0.1, 16.0))
    -- Associate material with scene so shader parameter animation respects
    -- scene time scale
    mushroomMat:SetScene(scene)
    mushroomMat:SetShaderParameterAnimation("MatSpecColor", specColorAnimation)

    -- Randomly placed mushrooms sharing the animated material
    for i = 1, NUM_OBJECTS do
        local mushroomNode = scene:CreateChild("Mushroom")
        mushroomNode:SetPosition(Vector3(Random(90.0) - 45.0, 0.0, Random(90.0) - 45.0))
        mushroomNode:SetRotation(Quaternion(0.0, Random(360.0), 0.0))
        mushroomNode:SetScale(0.5 + Random(2.0))
        local mushroomObject = mushroomNode:CreateComponent("StaticModel")
        mushroomObject:SetModel(cache:GetResource("Model", "Models/Mushroom.mdl"))
        mushroomObject:SetMaterial(mushroomMat)
    end

    -- Camera
    local cameraNode = scene:CreateChild("Camera")
    cameraNode:CreateComponent("FreeFlyController")
    local camera = cameraNode:CreateComponent("Camera")
    cameraNode:SetPosition(Vector3(0.0, 5.0, 0.0))
    self.cameraNode = cameraNode

    SetViewport(0, scene, camera)
end

function app:CreateInstructions()
    local cache = GetSubsystem("ResourceCache")

    local instructionText = GetUIRoot():CreateChild("Text")
    instructionText:SetText("Use WASD keys and mouse/touch to move")
    instructionText:SetFont(cache:GetResource("Font", "Fonts/Anonymous Pro.ttf"), 15)
    instructionText:SetHorizontalAlignment(HA.CENTER)
    instructionText:SetVerticalAlignment(VA.CENTER)
    instructionText:SetPosition(0, GetUIRoot():GetHeight() / 4)
end

app:Run()

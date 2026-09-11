-- LuaSamples/30_LightAnimation/main.lua
-- Lua port of Source/Samples/30_LightAnimation: a point light moves along an
-- animation track, while two ValueAnimations cycle a Text's string color
-- names and a Sprite's texture through the GoldIcon frames.

local NUM_OBJECTS = 200

local app = Sample:new()

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.RELATIVE)
    input:SetMouseVisible(false)

    self:CreateInstructions()
    self:CreateScene()
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

    -- Point light with a node animation track
    local lightContainer = scene:CreateChild("PointLightContainer")
    local lightNode = lightContainer:CreateChild("PointLight")
    local light = lightNode:CreateComponent("Light")
    light:SetLightType(LIGHT.POINT)
    light:SetRange(10.0)

    local animationController = lightNode:CreateComponent("AnimationController")
    animationController:Play("Animations/LightAnimation.xml", 0, true)

    -- Text attribute animation cycling color names
    local textAnimation = ValueAnimation()
    textAnimation:SetKeyFrame(0.0, "WHITE")
    textAnimation:SetKeyFrame(1.0, "RED")
    textAnimation:SetKeyFrame(2.0, "YELLOW")
    textAnimation:SetKeyFrame(3.0, "GREEN")
    textAnimation:SetKeyFrame(4.0, "WHITE")
    GetUIRoot():GetChild("animatingText"):SetAttributeAnimation("Text", textAnimation)

    -- UI element texture animation
    -- (note: a spritesheet and "Image Rect" attribute should be used in real
    -- use cases for better performance)
    local spriteAnimation = ValueAnimation()
    spriteAnimation:SetKeyFrame(0.0, { type = "Texture2D", name = "Urho2D/GoldIcon/1.png" })
    spriteAnimation:SetKeyFrame(0.1, { type = "Texture2D", name = "Urho2D/GoldIcon/2.png" })
    spriteAnimation:SetKeyFrame(0.2, { type = "Texture2D", name = "Urho2D/GoldIcon/3.png" })
    spriteAnimation:SetKeyFrame(0.3, { type = "Texture2D", name = "Urho2D/GoldIcon/4.png" })
    spriteAnimation:SetKeyFrame(0.4, { type = "Texture2D", name = "Urho2D/GoldIcon/5.png" })
    spriteAnimation:SetKeyFrame(0.5, { type = "Texture2D", name = "Urho2D/GoldIcon/1.png" })
    GetUIRoot():GetChild("animatingSprite"):SetAttributeAnimation("Texture", spriteAnimation)

    -- Randomly placed mushrooms for the light to reveal
    for i = 1, NUM_OBJECTS do
        local mushroomNode = scene:CreateChild("Mushroom")
        mushroomNode:SetPosition(Vector3(Random(90.0) - 45.0, 0.0, Random(90.0) - 45.0))
        mushroomNode:SetRotation(Quaternion(0.0, Random(360.0), 0.0))
        mushroomNode:SetScale(0.5 + Random(2.0))
        local mushroomObject = mushroomNode:CreateComponent("StaticModel")
        mushroomObject:SetModel(cache:GetResource("Model", "Models/Mushroom.mdl"))
        mushroomObject:SetMaterial(cache:GetResource("Material", "Materials/Mushroom.xml"))
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
    local font = cache:GetResource("Font", "Fonts/Anonymous Pro.ttf")
    instructionText:SetFont(font, 15)
    instructionText:SetHorizontalAlignment(HA.CENTER)
    instructionText:SetVerticalAlignment(VA.CENTER)
    instructionText:SetPosition(0, GetUIRoot():GetHeight() / 4)

    -- Animating text
    local text = GetUIRoot():CreateChild("Text", "animatingText")
    text:SetFont(font, 15)
    text:SetHorizontalAlignment(HA.CENTER)
    text:SetVerticalAlignment(VA.CENTER)
    text:SetPosition(0, GetUIRoot():GetHeight() / 4 + 20)

    -- Animating sprite in the top left corner
    local sprite = GetUIRoot():CreateChild("Sprite", "animatingSprite")
    sprite:SetPosition(8, 8)
    sprite:SetSize(64, 64)
end

app:Run()

-- LuaSamples/44_RibbonTrailDemo/main.lua
-- Lua port of Source/Samples/44_RibbonTrailDemo: face-camera ribbon trails
-- behind two moving boxes (1 and 4 columns) and a bone trail attached to
-- the Ninja's sword tip, emitted only during the attack animation window.

local app = Sample:new()
app.swordTrailStartTime = 0.2
app.swordTrailEndTime = 0.46
app.timeStepSum = 0.0

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.RELATIVE)
    input:SetMouseVisible(false)

    self:CreateScene()
    self:CreateInstructions()
    SetViewport(0, self.scene, self.cameraNode:GetComponent("Camera"))
end

function app:CreateScene()
    local scene = CreateScene()
    self.scene = scene
    scene:CreateComponent("Octree")

    -- Static plane
    local planeNode = scene:CreateChild("Plane")
    planeNode:SetScale(Vector3(100.0, 1.0, 100.0))
    local planeObject = planeNode:CreateComponent("StaticModel")
    planeObject:SetModel(GetResource("Model", "Models/Plane.mdl"))
    planeObject:SetMaterial(GetResource("Material", "Materials/StoneTiled.xml"))

    -- Directional light with cascaded shadows
    local lightNode = scene:CreateChild("DirectionalLight")
    lightNode:SetDirection(Vector3(0.6, -1.0, 0.8))
    local light = lightNode:CreateComponent("Light")
    light:SetLightType(LIGHT.DIRECTIONAL)
    light:SetCastShadows(true)
    light:SetShadowBias(0.00005, 0.5)
    light:SetShadowCascade(10.0, 50.0, 200.0, 0.0, 0.8)

    -- First box: face camera trail with 1 column
    local boxContainer = scene:CreateChild("BoxContainer")
    self.boxNode1 = boxContainer:CreateChild("Box1")
    local box1 = self.boxNode1:CreateComponent("StaticModel")
    box1:SetModel(GetResource("Model", "Models/Box.mdl"))
    box1:SetCastShadows(true)
    local boxTrail1 = self.boxNode1:CreateComponent("RibbonTrail")
    boxTrail1:SetMaterial(GetResource("Material", "Materials/RibbonTrail.xml"))
    boxTrail1:SetStartColor(Color(1.0, 0.5, 0.0, 1.0))
    boxTrail1:SetEndColor(Color(1.0, 1.0, 0.0, 0.0))
    boxTrail1:SetWidth(0.5)
    boxTrail1:SetUpdateInvisible(true)

    -- Second box: face camera trail with 4 columns (less distortion)
    self.boxNode2 = boxContainer:CreateChild("Box2")
    local box2 = self.boxNode2:CreateComponent("StaticModel")
    box2:SetModel(GetResource("Model", "Models/Box.mdl"))
    box2:SetCastShadows(true)
    local boxTrail2 = self.boxNode2:CreateComponent("RibbonTrail")
    boxTrail2:SetMaterial(GetResource("Material", "Materials/RibbonTrail.xml"))
    boxTrail2:SetStartColor(Color(1.0, 0.5, 0.0, 1.0))
    boxTrail2:SetEndColor(Color(1.0, 1.0, 0.0, 0.0))
    boxTrail2:SetWidth(0.5)
    boxTrail2:SetTailColumn(4)
    boxTrail2:SetUpdateInvisible(true)

    -- Ninja animated model for the bone trail demo
    local ninjaNode = scene:CreateChild("Ninja")
    ninjaNode:SetPosition(Vector3(5.0, 0.0, 0.0))
    ninjaNode:SetRotation(Quaternion(0.0, 180.0, 0.0))
    local ninja = ninjaNode:CreateComponent("AnimatedModel")
    ninja:SetModel(GetResource("Model", "Models/Ninja.mdl"))
    ninja:SetMaterial(GetResource("Material", "Materials/Ninja.xml"))
    ninja:SetCastShadows(true)

    -- Play the attack animation (the ninja has a single animation, so a
    -- plain looped play matches the C++ PlayNewExclusive behavior)
    self.ninjaAnimCtrl = ninjaNode:CreateComponent("AnimationController")
    self.ninjaAnimCtrl:Play("Models/Ninja_Attack3.ani", 0, true, 0.0)

    -- Ribbon trail on the tip of the sword
    local swordTip = ninjaNode:GetChild("Joint29", true)
    self.swordTrail = swordTip:CreateComponent("RibbonTrail")
    self.swordTrail:SetTrailType(TT.BONE)
    self.swordTrail:SetMaterial(GetResource("Material", "Materials/SlashTrail.xml"))
    self.swordTrail:SetLifetime(0.22)
    self.swordTrail:SetStartColor(Color(1.0, 1.0, 1.0, 0.75))
    self.swordTrail:SetEndColor(Color(0.2, 0.5, 1.0, 0.0))
    self.swordTrail:SetTailColumn(4)
    self.swordTrail:SetUpdateInvisible(true)

    -- Floating info texts
    local boxTextNode1 = scene:CreateChild("BoxText1")
    boxTextNode1:SetPosition(Vector3(-1.0, 2.0, 0.0))
    local boxText1 = boxTextNode1:CreateComponent("Text3D")
    boxText1:SetText("Face Camera Trail (4 Column)")
    boxText1:SetFont(GetResource("Font", "Fonts/BlueHighway.sdf"), 24)

    local boxTextNode2 = scene:CreateChild("BoxText2")
    boxTextNode2:SetPosition(Vector3(-6.0, 2.0, 0.0))
    local boxText2 = boxTextNode2:CreateComponent("Text3D")
    boxText2:SetText("Face Camera Trail (1 Column)")
    boxText2:SetFont(GetResource("Font", "Fonts/BlueHighway.sdf"), 24)

    local ninjaTextNode2 = scene:CreateChild("NinjaText")
    ninjaTextNode2:SetPosition(Vector3(4.0, 2.5, 0.0))
    local ninjaText = ninjaTextNode2:CreateComponent("Text3D")
    ninjaText:SetText("Bone Trail (4 Column)")
    ninjaText:SetFont(GetResource("Font", "Fonts/BlueHighway.sdf"), 24)

    -- Camera with a free-fly controller
    self.cameraNode = scene:CreateChild("Camera")
    self.cameraNode:CreateComponent("FreeFlyController")
    self.cameraNode:CreateComponent("Camera")
    self.cameraNode:SetPosition(Vector3(0.0, 2.0, -14.0))
end

function app:CreateInstructions()
    local root = GetUIRoot()
    local instructionText = root:CreateChild("Text")
    instructionText:SetText("Use WASD keys and mouse/touch to move")
    instructionText:SetFont("Fonts/Anonymous Pro.ttf", 15)
    instructionText:SetHorizontalAlignment(HA.CENTER)
    instructionText:SetVerticalAlignment(VA.CENTER)
    instructionText:SetPosition(0, root:GetHeight() / 4)
end

function app:Update(timeStep)
    -- Sum of timesteps
    self.timeStepSum = self.timeStepSum + timeStep

    -- Move first box with pattern
    self.boxNode1:SetTransform(
        Vector3(-4.0 + 3.0 * Cos(100.0 * self.timeStepSum), 0.5, -2.0 * Cos(400.0 * self.timeStepSum)),
        Quaternion())

    -- Move second box with pattern
    self.boxNode2:SetTransform(
        Vector3(3.0 * Cos(100.0 * self.timeStepSum), 0.5, -2.0 * Cos(400.0 * self.timeStepSum)),
        Quaternion())

    -- Elapsed attack animation time
    local swordAnimTime = self.ninjaAnimCtrl:GetTime("Models/Ninja_Attack3.ani")

    -- Stop emitting trail when the sword is finished slashing
    if not self.swordTrail:IsEmitting() and swordAnimTime > self.swordTrailStartTime and swordAnimTime < self.swordTrailEndTime then
        self.swordTrail:SetEmitting(true)
    elseif self.swordTrail:IsEmitting() and swordAnimTime >= self.swordTrailEndTime then
        self.swordTrail:SetEmitting(false)
    end
end

app:Run()

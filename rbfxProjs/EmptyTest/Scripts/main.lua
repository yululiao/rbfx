-- Scripts/main.lua
-- Editor GameView Play entry point.
-- Loads Kachujin models with random movement (based on 06_SkeletalAnimation).
--
-- The global variable `scene` is set by C++ (the editor's current scene).
-- On Stop, Lua state is reinitialized AND we explicitly remove the nodes
-- we added so the editor scene stays clean.

----------------------------------------------------------------------
-- Track nodes we create so Cleanup can remove them from the editor scene.
----------------------------------------------------------------------
local createdNodes = {}

local function track(node)
    table.insert(createdNodes, node)
end

----------------------------------------------------------------------
-- Scene setup
----------------------------------------------------------------------
local function CreateScene()
    -- Ground plane
    local planeNode = scene:CreateChild("LuaTest_Plane")
    track(planeNode)
    planeNode:SetScale(Vector3(50.0, 1.0, 50.0))
    local planeObject = planeNode:CreateComponent("StaticModel")
    planeObject:SetModel(GetResource("Model", "Models/Plane.mdl"))
    planeObject:SetMaterial(GetResource("Material", "Materials/StoneTiled.xml"))

    -- Zone: ambient lighting & fog
    local zoneNode = scene:CreateChild("LuaTest_Zone")
    track(zoneNode)
    local zone = zoneNode:CreateComponent("Zone")
    zone:SetBoundingBox(BoundingBox(-1000.0, 1000.0))
    zone:SetAmbientColor(Color(0.5, 0.5, 0.5))
    zone:SetFogColor(Color(0.4, 0.5, 0.8))
    zone:SetFogStart(100.0)
    zone:SetFogEnd(300.0)

    -- Directional light with shadows
    local lightNode = scene:CreateChild("LuaTest_DirLight")
    track(lightNode)
    lightNode:SetDirection(Vector3(0.6, -1.0, 0.8))
    local light = lightNode:CreateComponent("Light")
    light:SetLightType(LIGHT.DIRECTIONAL)
    light:SetCastShadows(true)
    light:SetColor(Color(0.5, 0.5, 0.5))
    light:SetShadowBias(0.00025, 0.5)
    light:SetShadowCascade(10.0, 50.0, 200.0, 0.0, 0.8)

    -- Camera with FreeFlyController (WASD + mouse look)
    local cameraNode = scene:CreateChild("LuaTest_Camera")
    track(cameraNode)
    cameraNode:CreateComponent("FreeFlyController")
    local camera = cameraNode:CreateComponent("Camera")
    camera:SetFarClip(300.0)
    cameraNode:SetPosition(Vector3(0.0, 5.0, -15.0))

    SetViewport(0, scene, camera)

    -- Animated Kachujin models with random movement data
    local NUM_MODELS = 10
    local MOVE_SPEED = 2.0
    local ROTATE_SPEED = 100.0
    local bounds = BoundingBox(Vector3(-20.0, 0.0, -20.0), Vector3(20.0, 0.0, 20.0))

    local movers = {}
    for i = 1, NUM_MODELS do
        local modelNode = scene:CreateChild("LuaTest_Kachujin")
        track(modelNode)
        modelNode:SetPosition(Vector3(Random(40.0) - 20.0, 0.0, Random(40.0) - 20.0))
        modelNode:SetRotation(Quaternion(0.0, Random(360.0), 0.0))

        local modelObject = modelNode:CreateComponent("AnimatedModel")
        modelObject:SetModel(GetResource("Model", "Models/Kachujin/Kachujin.mdl"))
        modelObject:SetMaterial(GetResource("Material", "Models/Kachujin/Materials/Kachujin.xml"))
        modelObject:SetCastShadows(true)

        -- Walk animation with random start phase
        local walkAnimation = GetResource("Animation", "Models/Kachujin/Kachujin_Walk.ani")
        local startTime = Random(walkAnimation:GetLength())
        local animationController = modelNode:CreateComponent("AnimationController")
        animationController:PlayNewExclusive(walkAnimation, true, startTime)

        table.insert(movers, {
            node = modelNode,
            moveSpeed = MOVE_SPEED,
            rotateSpeed = ROTATE_SPEED,
            bounds = bounds
        })
    end

    return movers
end

----------------------------------------------------------------------
-- Instructions overlay
----------------------------------------------------------------------
local function CreateInstructions(text)
    local root = GetUIRoot()
    if not root then return end

    local instructions = root:CreateChild("Text", "Instructions")
    instructions:SetFont("Fonts/Anonymous Pro.ttf", 15)
    instructions:SetText(text)
    instructions:SetTextAlignment(HA.LEFT)
    instructions:SetPosition(10, 10)
    instructions:SetWidth(root:GetWidth() - 20)
    instructions:SetColor(Color(0.0, 1.0, 0.0))
    table.insert(createdNodes, instructions)
end

----------------------------------------------------------------------
-- Cleanup: remove everything we added to the editor scene / UI.
-- Called automatically when the user clicks Stop (via ExitEvent) or
-- when the Lua state is about to be reinitialized.
----------------------------------------------------------------------
-- Register global cleanup function. The editor's PlayState destructor
-- calls `__cleanup()` before reinitializing the Lua state, so we can
-- remove the nodes we added to the editor scene.
function __cleanup()
    for _, node in ipairs(createdNodes) do
        if node and node.Remove then
            node:Remove()
        end
    end
    createdNodes = {}
end

----------------------------------------------------------------------
-- Main
----------------------------------------------------------------------
local movers = CreateScene()
CreateInstructions("Kachujin Test - WASD + Mouse to move\nClick Stop to clean up")

-- Per-frame movement: walk forward, yaw when hitting bounds
SubscribeToEvent("Update", function(data)
    local timeStep = data.TimeStep
    for _, mover in ipairs(movers) do
        local node = mover.node
        node:Translate(node.direction * mover.moveSpeed * timeStep, TS.LOCAL)

        local pos = node:GetPosition()
        if pos.x < mover.bounds.min.x or pos.x > mover.bounds.max.x
            or pos.z < mover.bounds.min.z or pos.z > mover.bounds.max.z then
            node:Yaw(mover.rotateSpeed * timeStep)
        end
    end
end)

-- LuaSamples/06_SkeletalAnimation/main.lua
-- Lua port of Source/Samples/06_SkeletalAnimation: 30 animated Kachujin
-- models walking inside bounds. The C++ Mover3D logic component is replaced
-- by a per-frame Lua update loop.

local app = Sample:new()
app.drawDebug = false

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.RELATIVE)
    input:SetMouseVisible(false)

    self.movers = {}
    self:CreateScene()
    self:CreateInstructions("Use WASD keys and mouse/touch to move\nSpace to toggle debug geometry")

    -- Request debug geometry rendering during post-render update
    SubscribeToEvent("PostRenderUpdate", function(data)
        if self.drawDebug then
            GetSubsystem("Renderer"):DrawDebugGeometry(false)
        end
    end)
end

function app:CreateScene()
    local scene = CreateScene()
    self.scene = scene

    -- Create octree, use default volume. Also create a DebugRenderer
    -- component so that we can draw debug geometry
    scene:CreateComponent("Octree")
    scene:CreateComponent("DebugRenderer")

    -- Create scene node & StaticModel component for showing a static plane
    local planeNode = scene:CreateChild("Plane")
    planeNode:SetScale(Vector3(50.0, 1.0, 50.0))
    local planeObject = planeNode:CreateComponent("StaticModel")
    planeObject:SetModel(GetResource("Model", "Models/Plane.mdl"))
    planeObject:SetMaterial(GetResource("Material", "Materials/StoneTiled.xml"))

    -- Create a Zone component for ambient lighting & fog control
    local zoneNode = scene:CreateChild("Zone")
    local zone = zoneNode:CreateComponent("Zone")
    zone:SetBoundingBox(BoundingBox(-1000.0, 1000.0))
    zone:SetAmbientColor(Color(0.5, 0.5, 0.5))
    zone:SetFogColor(Color(0.4, 0.5, 0.8))
    zone:SetFogStart(100.0)
    zone:SetFogEnd(300.0)

    -- Create a directional light to the world. Enable cascaded shadows on it
    local lightNode = scene:CreateChild("DirectionalLight")
    lightNode:SetDirection(Vector3(0.6, -1.0, 0.8))
    local light = lightNode:CreateComponent("Light")
    light:SetLightType(LIGHT.DIRECTIONAL)
    light:SetCastShadows(true)
    light:SetColor(Color(0.5, 0.5, 0.5))
    light:SetShadowBias(0.00025, 0.5)
    -- Set cascade splits at 10, 50 and 200 world units, fade shadows out at
    -- 80% of maximum shadow distance
    light:SetShadowCascade(10.0, 50.0, 200.0, 0.0, 0.8)

    -- Create animated models
    local NUM_MODELS = 30
    local MODEL_MOVE_SPEED = 2.0
    local MODEL_ROTATE_SPEED = 100.0
    local bounds = BoundingBox(Vector3(-20.0, 0.0, -20.0), Vector3(20.0, 0.0, 20.0))

    for i = 1, NUM_MODELS do
        local modelNode = scene:CreateChild("Jill")
        modelNode:SetPosition(Vector3(Random(40.0) - 20.0, 0.0, Random(40.0) - 20.0))
        modelNode:SetRotation(Quaternion(0.0, Random(360.0), 0.0))

        local modelObject = modelNode:CreateComponent("AnimatedModel")
        modelObject:SetModel(GetResource("Model", "Models/Kachujin/Kachujin.mdl"))
        modelObject:SetMaterial(GetResource("Material", "Models/Kachujin/Materials/Kachujin.xml"))
        modelObject:SetCastShadows(true)

        -- Create an AnimationState for a walk animation with random start time
        local walkAnimation = GetResource("Animation", "Models/Kachujin/Kachujin_Walk.ani")
        local startTime = Random(walkAnimation:GetLength())

        local animationController = modelNode:CreateComponent("AnimationController")
        animationController:PlayNewExclusive(walkAnimation, true, startTime)

        -- Mover data (replaces the C++ Mover3D component)
        table.insert(self.movers, {
            node = modelNode,
            moveSpeed = MODEL_MOVE_SPEED,
            rotateSpeed = MODEL_ROTATE_SPEED,
            bounds = bounds
        })
    end

    -- Create the camera. Limit far clip distance to match the fog
    local cameraNode = scene:CreateChild("Camera")
    cameraNode:CreateComponent("FreeFlyController")
    local camera = cameraNode:CreateComponent("Camera")
    camera:SetFarClip(300.0)

    -- Set an initial position for the camera scene node above the plane
    cameraNode:SetPosition(Vector3(0.0, 5.0, 0.0))

    self.cameraNode = cameraNode
    SetViewport(0, scene, camera)
end

-- Per-frame movement, mirroring the C++ Mover3D component: move forward and
-- rotate right when in risk of going outside the bounds.
function app:Update(timeStep)
    for _, mover in ipairs(self.movers) do
        local node = mover.node
        node:Translate(node:GetDirection() * mover.moveSpeed * timeStep, TS.LOCAL)

        local pos = node:GetPosition()
        if pos.x < mover.bounds.min.x or pos.x > mover.bounds.max.x
            or pos.z < mover.bounds.min.z or pos.z > mover.bounds.max.z then
            node:Yaw(mover.rotateSpeed * timeStep)
        end
    end
end

-- Toggle debug geometry with space
function app:OnKeyDown(key)
    if key == KEY.SPACE then
        self.drawDebug = not self.drawDebug
    end
end

app:Run()

-- LuaSamples/05_AnimatingScene/main.lua
-- Lua port of Source/Samples/05_AnimatingScene: 2000 rotating boxes in fog.
-- The C++ Rotator logic component is replaced by a per-frame Lua update loop.

local app = Sample:new()

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.RELATIVE)
    input:SetMouseVisible(false)

    self.rotators = {}
    self:CreateScene()
    self:CreateInstructions("Use WASD keys and mouse/touch to move")
end

function app:CreateScene()
    local scene = CreateScene()
    self.scene = scene

    -- Create the Octree component to the scene so that drawable objects can
    -- be rendered. Use default volume (-1000,-1000,-1000) to (1000,1000,1000)
    scene:CreateComponent("Octree")

    -- Create a Zone component into a child scene node. The Zone controls
    -- ambient lighting and fog settings. Set same volume as the Octree, a
    -- close bluish fog and some ambient light
    local zoneNode = scene:CreateChild("Zone")
    local zone = zoneNode:CreateComponent("Zone")
    zone:SetBoundingBox(BoundingBox(-1000.0, 1000.0))
    zone:SetAmbientColor(Color(0.05, 0.1, 0.15))
    zone:SetFogColor(Color(0.1, 0.2, 0.3))
    zone:SetFogStart(10.0)
    zone:SetFogEnd(100.0)

    -- Create randomly positioned and oriented box StaticModels in the scene
    local NUM_OBJECTS = 2000
    for i = 1, NUM_OBJECTS do
        local boxNode = scene:CreateChild("Box")
        boxNode:SetPosition(Vector3(Random(200.0) - 100.0, Random(200.0) - 100.0, Random(200.0) - 100.0))
        -- Orient using random pitch, yaw and roll Euler angles
        boxNode:SetRotation(Quaternion(Random(360.0), Random(360.0), Random(360.0)))
        local boxObject = boxNode:CreateComponent("StaticModel")
        boxObject:SetModel(GetResource("Model", "Models/Box.mdl"))
        boxObject:SetMaterial(GetResource("Material", "Materials/Stone.xml"))

        -- Same rotation speed for all objects (replaces the C++ Rotator
        -- component: rotation is applied from the Update loop below)
        table.insert(self.rotators, {
            node = boxNode,
            speed = Vector3(10.0, 20.0, 30.0)
        })
    end

    -- Create the camera. Let the starting position be at the world origin.
    -- As the fog limits maximum visible distance, we can bring the far clip
    -- plane closer for more effective culling of distant objects
    local cameraNode = scene:CreateChild("Camera")
    cameraNode:CreateComponent("FreeFlyController")
    local camera = cameraNode:CreateComponent("Camera")
    camera:SetFarClip(100.0)

    -- Create a point light to the camera scene node
    local light = cameraNode:CreateComponent("Light")
    light:SetLightType(LIGHT.POINT)
    light:SetRange(30.0)

    self.cameraNode = cameraNode
    SetViewport(0, scene, camera)
end

-- Per-frame rotation of all boxes, mirroring the C++ Rotator component.
function app:Update(timeStep)
    for _, rotator in ipairs(self.rotators) do
        local node = rotator.node
        node:Pitch(rotator.speed.x * timeStep)
        node:Yaw(rotator.speed.y * timeStep)
        node:Roll(rotator.speed.z * timeStep)
    end
end

app:Run()

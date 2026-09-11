-- LuaSamples/04_StaticScene/main.lua
-- Lua port of Source/Samples/04_StaticScene: ground plane, directional light
-- and 200 randomly placed mushrooms, viewed with a free-fly camera.

local app = Sample:new()

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.RELATIVE)
    input:SetMouseVisible(false)

    self:CreateScene()
    self:CreateInstructions("Use WASD keys and mouse/touch to move")
end

function app:CreateScene()
    local scene = CreateScene()
    self.scene = scene

    -- Create the Octree component to the scene. This is required before
    -- adding any drawable components, or else nothing will show up.
    scene:CreateComponent("Octree")

    -- Create a child scene node (at world origin) and a StaticModel component
    -- into it. Set the StaticModel to show a simple plane mesh with a "stone"
    -- material. Scale the scene node larger (100 x 100 world units)
    local planeNode = scene:CreateChild("Plane")
    planeNode:SetScale(Vector3(100.0, 1.0, 100.0))
    local planeObject = planeNode:CreateComponent("StaticModel")
    planeObject:SetModel(GetResource("Model", "Models/Plane.mdl"))
    planeObject:SetMaterial(GetResource("Material", "Materials/StoneTiled.xml"))

    -- Create a directional light to the world so that we can see something.
    -- The light scene node's orientation controls the light direction
    local lightNode = scene:CreateChild("DirectionalLight")
    lightNode:SetDirection(Vector3(0.6, -1.0, 0.8))
    local light = lightNode:CreateComponent("Light")
    light:SetLightType(LIGHT.DIRECTIONAL)

    -- Create more StaticModel objects to the scene, randomly positioned,
    -- rotated and scaled. Rendering a large number of the same object with the
    -- same material allows instancing to be used, if the GPU supports it.
    local NUM_OBJECTS = 200
    for i = 1, NUM_OBJECTS do
        local mushroomNode = scene:CreateChild("Mushroom")
        mushroomNode:SetPosition(Vector3(Random(90.0) - 45.0, 0.0, Random(90.0) - 45.0))
        mushroomNode:SetRotation(Quaternion(0.0, Random(360.0), 0.0))
        mushroomNode:SetScale(0.5 + Random(2.0))
        local mushroomObject = mushroomNode:CreateComponent("StaticModel")
        mushroomObject:SetModel(GetResource("Model", "Models/Mushroom.mdl"))
        mushroomObject:SetMaterial(GetResource("Material", "Materials/Mushroom.xml"))
    end

    -- Create a scene node for the camera, which we will move around.
    -- FreeFlyController provides WASD + mouse movement.
    local cameraNode = scene:CreateChild("Camera")
    cameraNode:CreateComponent("Camera")
    cameraNode:CreateComponent("FreeFlyController")

    -- Set an initial position for the camera scene node above the plane
    cameraNode:SetPosition(Vector3(0.0, 5.0, 0.0))

    self:CreateCameraViewport(scene, cameraNode)
end

-- Separate viewport hookup so derived samples can reuse it.
function app:CreateCameraViewport(scene, cameraNode)
    self.cameraNode = cameraNode
    SetViewport(0, scene, cameraNode:GetComponent("Camera"))
end

app:Run()

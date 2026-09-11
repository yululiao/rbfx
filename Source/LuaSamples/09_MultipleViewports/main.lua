-- LuaSamples/09_MultipleViewports/main.lua
-- Lua port of Source/Samples/09_MultipleViewports: main view plus a small
-- rear-view-mirror viewport rendered from a second camera.

local app = Sample:new()
app.drawDebug = false

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.RELATIVE)
    input:SetMouseVisible(false)

    self:CreateScene()
    self:CreateInstructions("Use WASD keys and mouse/touch to move\nSpace to toggle debug geometry")
    self:SetupViewports()

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

    -- Create octree + DebugRenderer
    scene:CreateComponent("Octree")
    scene:CreateComponent("DebugRenderer")

    -- Create scene node & StaticModel component for showing a static plane
    local planeNode = scene:CreateChild("Plane")
    planeNode:SetScale(Vector3(100.0, 1.0, 100.0))
    local planeObject = planeNode:CreateComponent("StaticModel")
    planeObject:SetModel(GetResource("Model", "Models/Plane.mdl"))
    planeObject:SetMaterial(GetResource("Material", "Materials/StoneTiled.xml"))

    -- Create a Zone component for ambient lighting & fog control
    local zoneNode = scene:CreateChild("Zone")
    local zone = zoneNode:CreateComponent("Zone")
    zone:SetBoundingBox(BoundingBox(-1000.0, 1000.0))
    zone:SetAmbientColor(Color(0.15, 0.15, 0.15))
    zone:SetFogColor(Color(0.5, 0.5, 0.7))
    zone:SetFogStart(100.0)
    zone:SetFogEnd(300.0)

    -- Create a directional light to the world. Enable cascaded shadows on it
    local lightNode = scene:CreateChild("DirectionalLight")
    lightNode:SetDirection(Vector3(0.6, -1.0, 0.8))
    local light = lightNode:CreateComponent("Light")
    light:SetLightType(LIGHT.DIRECTIONAL)
    light:SetCastShadows(true)
    light:SetShadowBias(0.00025, 0.5)
    light:SetShadowCascade(10.0, 50.0, 200.0, 0.0, 0.8)

    -- Create some mushrooms
    local NUM_MUSHROOMS = 240
    for i = 1, NUM_MUSHROOMS do
        local mushroomNode = scene:CreateChild("Mushroom")
        mushroomNode:SetPosition(Vector3(Random(90.0) - 45.0, 0.0, Random(90.0) - 45.0))
        mushroomNode:SetRotation(Quaternion(0.0, Random(360.0), 0.0))
        mushroomNode:SetScale(0.5 + Random(2.0))
        local mushroomObject = mushroomNode:CreateComponent("StaticModel")
        mushroomObject:SetModel(GetResource("Model", "Models/Mushroom.mdl"))
        mushroomObject:SetMaterial(GetResource("Material", "Materials/Mushroom.xml"))
        mushroomObject:SetCastShadows(true)
    end

    -- Create randomly sized boxes. If boxes are big enough, make them occluders
    local NUM_BOXES = 20
    for i = 1, NUM_BOXES do
        local boxNode = scene:CreateChild("Box")
        local size = 1.0 + Random(10.0)
        boxNode:SetPosition(Vector3(Random(80.0) - 40.0, size * 0.5, Random(80.0) - 40.0))
        boxNode:SetScale(size)
        local boxObject = boxNode:CreateComponent("StaticModel")
        boxObject:SetModel(GetResource("Model", "Models/Box.mdl"))
        boxObject:SetMaterial(GetResource("Material", "Materials/Stone.xml"))
        boxObject:SetCastShadows(true)
        if size >= 3.0 then
            boxObject:SetOccluder(true)
        end
    end

    -- Create the cameras. Limit far clip distance to match the fog
    local cameraNode = scene:CreateChild("Camera")
    cameraNode:CreateComponent("FreeFlyController")
    local camera = cameraNode:CreateComponent("Camera")
    camera:SetFarClip(300.0)

    -- Parent the rear camera node to the front camera node and turn it 180
    -- degrees to face backward. Here, we use the angle-axis constructor for
    -- Quaternion instead of the usual Euler angles
    local rearCameraNode = cameraNode:CreateChild("RearCamera")
    rearCameraNode:Rotate(Quaternion(180.0, Vector3.UP))
    local rearCamera = rearCameraNode:CreateComponent("Camera")
    rearCamera:SetFarClip(300.0)
    -- Because the rear viewport is rather small, disable occlusion culling
    -- from it. Use the camera's "view override flags" for this
    rearCamera:SetViewOverrideFlags(VO.DISABLE_OCCLUSION)

    -- Set an initial position for the front camera scene node above the plane
    cameraNode:SetPosition(Vector3(0.0, 5.0, 0.0))

    self.cameraNode = cameraNode
    self.rearCameraNode = rearCameraNode
end

function app:SetupViewports()
    local graphics = GetSubsystem("Graphics")

    -- Set up the front camera viewport
    SetViewport(0, self.scene, self.cameraNode:GetComponent("Camera"))

    -- Set up the rear camera viewport on top of the front view
    -- ("rear view mirror"). The viewport index must be greater in that case,
    -- otherwise the view would be left behind
    SetViewport(1, self.scene, self.rearCameraNode:GetComponent("Camera"),
        IntRect(graphics:GetWidth() * 2 / 3, 32, graphics:GetWidth() - 32, graphics:GetHeight() / 3))
end

-- Toggle debug geometry with space
function app:OnKeyDown(key)
    if key == KEY.SPACE then
        self.drawDebug = not self.drawDebug
    end
end

app:Run()

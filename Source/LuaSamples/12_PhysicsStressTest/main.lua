-- LuaSamples/12_PhysicsStressTest/main.lua
-- Lua port of Source/Samples/12_PhysicsStressTest: 1000 falling boxes
-- stress-testing the physics simulation, plus prefab-based mushrooms.

local app = Sample:new()
app.drawDebug = false

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.RELATIVE)
    input:SetMouseVisible(false)

    self:CreateScene()
    self:CreateInstructions("Use WASD keys and mouse/touch to move\nLMB to spawn physics objects\nSpace to toggle physics debug geometry")

    SubscribeToEvent("PostRenderUpdate", function(data)
        if self.drawDebug then
            self.scene:GetComponent("PhysicsWorld"):DrawDebugGeometry(true)
        end
    end)
end

function app:CreateScene()
    local scene = CreateScene()
    self.scene = scene

    scene:CreateComponent("Octree")
    scene:CreateComponent("PhysicsWorld")
    scene:CreateComponent("DebugRenderer")

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

    -- Create a floor object, 500 x 500 world units
    do
        local floorNode = scene:CreateChild("Floor")
        floorNode:SetPosition(Vector3(0.0, -0.5, 0.0))
        floorNode:SetScale(Vector3(500.0, 1.0, 500.0))
        local floorObject = floorNode:CreateComponent("StaticModel")
        floorObject:SetModel(GetResource("Model", "Models/Box.mdl"))
        floorObject:SetMaterial(GetResource("Material", "Materials/StoneTiled.xml"))

        floorNode:CreateComponent("RigidBody")
        local shape = floorNode:CreateComponent("CollisionShape")
        shape:SetBox(Vector3.ONE)
    end

    -- Create static mushrooms with triangle mesh collision (via prefab)
    do
        local NUM_MUSHROOMS = 50
        for i = 1, NUM_MUSHROOMS do
            local mushroomNode = scene:CreateChild("Mushroom")
            mushroomNode:SetPosition(Vector3(Random(400.0) - 200.0, 0.0, Random(400.0) - 200.0))
            mushroomNode:SetRotation(Quaternion(0.0, Random(360.0), 0.0))
            mushroomNode:SetScale(5.0 + Random(5.0))
            local prefabReference = mushroomNode:CreateComponent("PrefabReference")
            prefabReference:SetPrefab("Prefabs/Mushroom.prefab")
        end
    end

    -- Create a large amount of falling physics objects
    do
        local NUM_OBJECTS = 1000
        for i = 1, NUM_OBJECTS do
            local boxNode = scene:CreateChild("Box")
            boxNode:SetPosition(Vector3(0.0, i * 2.0 + 100.0, 0.0))
            local boxObject = boxNode:CreateComponent("StaticModel")
            boxObject:SetModel(GetResource("Model", "Models/Box.mdl"))
            boxObject:SetMaterial(GetResource("Material", "Materials/StoneSmall.xml"))
            boxObject:SetCastShadows(true)

            -- Give the RigidBody mass to make it movable and also adjust
            -- friction. Disable collision event signaling to reduce CPU load
            local body = boxNode:CreateComponent("RigidBody")
            body:SetMass(1.0)
            body:SetFriction(1.0)
            body:SetCollisionEventMode(CEM.NEVER)
            local shape = boxNode:CreateComponent("CollisionShape")
            shape:SetBox(Vector3.ONE)
        end
    end

    -- Create the camera. Limit far clip distance to match the fog
    local cameraNode = scene:CreateChild("Camera")
    cameraNode:CreateComponent("FreeFlyController")
    local camera = cameraNode:CreateComponent("Camera")
    camera:SetFarClip(300.0)

    -- Set an initial position for the camera scene node above the floor
    cameraNode:SetPosition(Vector3(0.0, 3.0, -20.0))

    self.cameraNode = cameraNode
    SetViewport(0, scene, camera)
end

function app:Update(timeStep)
    local input = GetSubsystem("Input")
    if GetSubsystem("UI"):GetFocusElement() then
        return
    end

    -- "Shoot" a physics object with left mousebutton
    if input:GetMouseButtonPress(MOUSEB.LEFT) then
        self:SpawnObject()
    end
end

function app:SpawnObject()
    local cameraRotation = self.cameraNode.rotation

    -- Create a smaller box at camera position
    local boxNode = self.scene:CreateChild("SmallBox")
    boxNode:SetPosition(self.cameraNode:GetPosition())
    boxNode:SetRotation(cameraRotation)
    boxNode:SetScale(0.25)
    local boxObject = boxNode:CreateComponent("StaticModel")
    boxObject:SetModel(GetResource("Model", "Models/Box.mdl"))
    boxObject:SetMaterial(GetResource("Material", "Materials/StoneSmall.xml"))
    boxObject:SetCastShadows(true)

    -- Create physics components, use a smaller mass also
    local body = boxNode:CreateComponent("RigidBody")
    body:SetMass(0.25)
    body:SetFriction(0.75)
    local shape = boxNode:CreateComponent("CollisionShape")
    shape:SetBox(Vector3.ONE)

    local OBJECT_VELOCITY = 10.0

    -- Set initial velocity for the RigidBody based on camera forward vector
    body:SetLinearVelocity(cameraRotation * Vector3(0.0, 0.25, 1.0) * OBJECT_VELOCITY)
end

-- Toggle physics debug geometry with space
function app:OnKeyDown(key)
    if key == KEY.SPACE then
        self.drawDebug = not self.drawDebug
    end
end

app:Run()

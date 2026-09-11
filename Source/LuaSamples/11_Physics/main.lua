-- LuaSamples/11_Physics/main.lua
-- Lua port of Source/Samples/11_Physics: pyramid of movable boxes on a
-- static floor. LMB shoots a small box from the camera.

local app = Sample:new()
app.drawDebug = false

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.RELATIVE)
    input:SetMouseVisible(false)

    self:CreateScene()
    self:CreateInstructions("Use WASD keys and mouse/touch to move\nLMB to spawn physics objects\nSpace to toggle physics debug geometry")

    -- Request physics debug geometry during post-render update
    SubscribeToEvent("PostRenderUpdate", function(data)
        if self.drawDebug then
            self.scene:GetComponent("PhysicsWorld"):DrawDebugGeometry(true)
        end
    end)
end

function app:CreateScene()
    local scene = CreateScene()
    self.scene = scene

    -- Create octree + physics simulation world with default parameters,
    -- which will update at 60fps. The PhysicsWorld must exist before
    -- creating physics components. Also a DebugRenderer
    scene:CreateComponent("Octree")
    scene:CreateComponent("PhysicsWorld")
    scene:CreateComponent("DebugRenderer")

    -- Create a Zone component for ambient lighting & fog control
    local zoneNode = scene:CreateChild("Zone")
    local zone = zoneNode:CreateComponent("Zone")
    zone:SetBoundingBox(BoundingBox(-1000.0, 1000.0))
    zone:SetAmbientColor(Color(0.15, 0.15, 0.15))
    zone:SetFogColor(Color(1.0, 1.0, 1.0))
    zone:SetFogStart(300.0)
    zone:SetFogEnd(500.0)

    -- Create a directional light to the world. Enable cascaded shadows on it
    local lightNode = scene:CreateChild("DirectionalLight")
    lightNode:SetDirection(Vector3(0.6, -1.0, 0.8))
    local light = lightNode:CreateComponent("Light")
    light:SetLightType(LIGHT.DIRECTIONAL)
    light:SetCastShadows(true)
    light:SetShadowBias(0.00025, 0.5)
    light:SetShadowCascade(10.0, 50.0, 200.0, 0.0, 0.8)

    -- Create skybox. The Skybox component is used like StaticModel, but it
    -- will be always located at the camera, giving the illusion of the box
    -- planes being far away
    local skyNode = scene:CreateChild("Sky")
    skyNode:SetScale(500.0) -- The scale actually does not matter
    local skybox = skyNode:CreateComponent("Skybox")
    skybox:SetModel(GetResource("Model", "Models/Box.mdl"))
    skybox:SetMaterial(GetResource("Material", "Materials/Skybox.xml"))

    -- Create a floor object, 1000 x 1000 world units. Adjust position so
    -- that the ground is at zero Y
    do
        local floorNode = scene:CreateChild("Floor")
        floorNode:SetPosition(Vector3(0.0, -0.5, 0.0))
        floorNode:SetScale(Vector3(1000.0, 1.0, 1000.0))
        local floorObject = floorNode:CreateComponent("StaticModel")
        floorObject:SetModel(GetResource("Model", "Models/Box.mdl"))
        floorObject:SetMaterial(GetResource("Material", "Materials/StoneTiled.xml"))

        -- Make the floor physical by adding RigidBody and CollisionShape
        -- components. The RigidBody's default parameters make the object
        -- static (zero mass)
        floorNode:CreateComponent("RigidBody")
        local shape = floorNode:CreateComponent("CollisionShape")
        -- Set a box shape of size 1 x 1 x 1 for collision
        shape:SetBox(Vector3.ONE)
    end

    -- Create a pyramid of movable physics objects
    for y = 0, 7 do
        for x = -y, y do
            local boxNode = scene:CreateChild("Box")
            boxNode:SetPosition(Vector3(x, -y + 8.0, 0.0))
            local boxObject = boxNode:CreateComponent("StaticModel")
            boxObject:SetModel(GetResource("Model", "Models/Box.mdl"))
            boxObject:SetMaterial(GetResource("Material", "Materials/StoneSmall.xml"))
            boxObject:SetCastShadows(true)

            -- Create RigidBody and CollisionShape components like above.
            -- Give the RigidBody mass to make it movable and also adjust
            -- friction. The actual mass is not important; only the mass
            -- ratios between colliding objects are significant
            local body = boxNode:CreateComponent("RigidBody")
            body:SetMass(1.0)
            body:SetFriction(0.75)
            local shape = boxNode:CreateComponent("CollisionShape")
            shape:SetBox(Vector3.ONE)
        end
    end

    -- Create the camera. Set far clip to match the fog
    local cameraNode = scene:CreateChild("Camera")
    local camera = cameraNode:CreateComponent("Camera")
    camera:SetFarClip(500.0)

    -- Set an initial position for the camera scene node above the floor
    cameraNode:SetPosition(Vector3(0.0, 5.0, -20.0))

    self.cameraNode = cameraNode
    SetViewport(0, scene, camera)
end

function app:Update(timeStep)
    self:MoveCamera(timeStep)
end

function app:MoveCamera(timeStep)
    -- Do not move if the UI has a focused element (the console)
    if GetSubsystem("UI"):GetFocusElement() then
        return
    end

    local input = GetSubsystem("Input")

    -- Movement speed as world units per second
    local MOVE_SPEED = 20.0
    -- Mouse sensitivity as degrees per pixel
    local MOUSE_SENSITIVITY = 0.1

    -- Use this frame's mouse motion to adjust camera node yaw and pitch.
    -- Clamp the pitch between -90 and 90 degrees
    local mouseMove = input:GetMouseMove()
    self.yaw = self.yaw + MOUSE_SENSITIVITY * mouseMove.x
    self.pitch = self.pitch + MOUSE_SENSITIVITY * mouseMove.y
    self.pitch = Clamp(self.pitch, -90.0, 90.0)

    -- Construct new orientation for the camera scene node from yaw and
    -- pitch. Roll is fixed to zero
    self.cameraNode:SetRotation(Quaternion(self.pitch, self.yaw, 0.0))

    -- Read WASD keys and move the camera scene node to the corresponding
    -- direction if they are pressed
    if input:GetKeyDown(string.byte("w")) then
        self.cameraNode:Translate(Vector3.FORWARD * MOVE_SPEED * timeStep)
    end
    if input:GetKeyDown(string.byte("s")) then
        self.cameraNode:Translate(Vector3.BACK * MOVE_SPEED * timeStep)
    end
    if input:GetKeyDown(string.byte("a")) then
        self.cameraNode:Translate(Vector3.LEFT * MOVE_SPEED * timeStep)
    end
    if input:GetKeyDown(string.byte("d")) then
        self.cameraNode:Translate(Vector3.RIGHT * MOVE_SPEED * timeStep)
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

    -- Set initial velocity for the RigidBody based on camera forward vector.
    -- Add also a slight up component to overcome gravity better
    body:SetLinearVelocity(cameraRotation * Vector3(0.0, 0.25, 1.0) * OBJECT_VELOCITY)
end

-- Toggle physics debug geometry with space
function app:OnKeyDown(key)
    if key == KEY.SPACE then
        self.drawDebug = not self.drawDebug
    end
end

app:Run()

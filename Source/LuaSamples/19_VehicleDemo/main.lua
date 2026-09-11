-- LuaSamples/19_VehicleDemo/main.lua
-- Lua port of Source/Samples/19_VehicleDemo: a four wheel vehicle built
-- from hinge constraints, driving on a heightmap terrain with 1000
-- mushrooms. The C++ Vehicle component (a MoveAndOrbitComponent subclass
-- reading an InputMap) is replaced by plain Lua state: WASD are read
-- directly, steering is smoothed manually and all wheel bodies/constraints
-- are cached in a table.

-- Vehicle constants (Vehicle.h)
local ENGINE_POWER = 10.0
local DOWN_FORCE = 10.0
local MAX_WHEEL_ANGLE = 22.5

-- Camera constants (VehicleDemo.cpp)
local CAMERA_DISTANCE = 10.0
local MOUSE_SENSITIVITY = 0.1

local app = Sample:new()
app.steering = 0.0

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.RELATIVE)
    input:SetMouseVisible(false)

    self:CreateScene()
    self:CreateVehicle()
    self:CreateInstructions()
    self:SubscribeToEvents()
end

function app:CreateScene()
    local scene = CreateScene()
    self.scene = scene

    -- Create scene subsystem components
    scene:CreateComponent("Octree")
    scene:CreateComponent("PhysicsWorld")

    -- Create camera and define viewport
    self.cameraNode = scene:CreateChild("Camera")
    local camera = self.cameraNode:CreateComponent("Camera")
    camera:SetFarClip(500.0)
    SetViewport(0, scene, camera)

    -- Create a zone for ambient lighting and fog control
    local zoneNode = scene:CreateChild("Zone")
    local zone = zoneNode:CreateComponent("Zone")
    zone:SetAmbientColor(Color(0.15, 0.15, 0.15))
    zone:SetFogColor(Color(0.5, 0.5, 0.7))
    zone:SetFogStart(300.0)
    zone:SetFogEnd(500.0)
    zone:SetBoundingBox(BoundingBox(-2000.0, 2000.0))

    -- Create a directional light with cascaded shadow mapping
    local lightNode = scene:CreateChild("DirectionalLight")
    lightNode:SetDirection(Vector3(0.3, -0.5, 0.425))
    local light = lightNode:CreateComponent("Light")
    light:SetLightType(LIGHT.DIRECTIONAL)
    light:SetCastShadows(true)
    light:SetShadowBias(0.00025, 0.5)
    light:SetShadowCascade(10.0, 50.0, 200.0, 0.0, 0.8)
    light:SetSpecularIntensity(0.5)

    -- Create heightmap terrain with collision
    local terrainNode = scene:CreateChild("Terrain")
    local terrain = terrainNode:CreateComponent("Terrain")
    terrain:SetPatchSize(64)
    -- Spacing between vertices and vertical resolution of the height map
    terrain:SetSpacing(Vector3(2.0, 0.1, 2.0))
    terrain:SetSmoothing(true)
    terrain:SetHeightMap(GetResource("Image", "Textures/HeightMap.png"))
    terrain:SetMaterial(GetResource("Material", "Materials/Terrain.xml"))
    -- The terrain consists of large triangles, which fits well for occlusion
    -- rendering, as a hill can occlude all terrain patches behind it
    terrain:SetOccluder(true)
    self.terrain = terrain

    local body = terrainNode:CreateComponent("RigidBody")
    body:SetCollisionLayer(2) -- Use layer bitmask 2 for static geometry
    local shape = terrainNode:CreateComponent("CollisionShape")
    shape:SetTerrain()

    -- Create 1000 mushrooms in the terrain. Always face outward along the
    -- terrain normal
    for _ = 1, 1000 do
        local objectNode = scene:CreateChild("Mushroom")
        local position = Vector3(Random(2000.0) - 1000.0, 0.0, Random(2000.0) - 1000.0)
        position.y = terrain:GetHeight(position) - 0.1
        objectNode:SetPosition(position)
        -- Create a rotation quaternion from up vector to terrain normal
        objectNode:SetRotation(Quaternion(Vector3.UP, terrain:GetNormal(position)))
        objectNode:SetScale(3.0)
        local prefabReference = objectNode:CreateComponent("PrefabReference")
        prefabReference:SetPrefab("Prefabs/Mushroom.prefab")
    end
end

function app:CreateVehicle()
    local scene = self.scene
    local vehicleNode = scene:CreateChild("Vehicle")
    vehicleNode:SetPosition(Vector3(0.0, 5.0, 0.0))
    self.vehicleNode = vehicleNode

    -- Create the hull rendering and physics components
    local hullObject = vehicleNode:CreateComponent("StaticModel")
    self.hullBody = vehicleNode:CreateComponent("RigidBody")
    local hullShape = vehicleNode:CreateComponent("CollisionShape")

    vehicleNode:SetScale(Vector3(1.5, 1.0, 3.0))
    hullObject:SetModel(GetResource("Model", "Models/Box.mdl"))
    hullObject:SetMaterial(GetResource("Material", "Materials/Stone.xml"))
    hullObject:SetCastShadows(true)
    hullShape:SetBox(Vector3.ONE)
    self.hullBody:SetMass(4.0)
    self.hullBody:SetLinearDamping(0.2) -- Some air resistance
    self.hullBody:SetAngularDamping(0.5)
    self.hullBody:SetCollisionLayer(1)

    self.wheels = {}
    self.wheels.frontLeft = self:InitWheel("FrontLeft", Vector3(-0.6, -0.4, 0.3))
    self.wheels.frontRight = self:InitWheel("FrontRight", Vector3(0.6, -0.4, 0.3))
    self.wheels.rearLeft = self:InitWheel("RearLeft", Vector3(-0.6, -0.4, -0.3))
    self.wheels.rearRight = self:InitWheel("RearRight", Vector3(0.6, -0.4, -0.3))
end

function app:InitWheel(name, offset)
    local scene = self.scene

    -- Note: do not parent the wheel to the hull scene node. Instead create
    -- it on the root level and let the physics constraint keep it together
    local wheelNode = scene:CreateChild(name)
    wheelNode:SetPosition(self.vehicleNode:LocalToWorld(offset))
    wheelNode:SetRotation(self.vehicleNode:GetRotation() * (offset.x >= 0.0
        and Quaternion(0.0, 0.0, -90.0) or Quaternion(0.0, 0.0, 90.0)))
    wheelNode:SetScale(Vector3(0.8, 0.5, 0.8))

    local wheelObject = wheelNode:CreateComponent("StaticModel")
    local wheelBody = wheelNode:CreateComponent("RigidBody")
    local wheelShape = wheelNode:CreateComponent("CollisionShape")
    local wheelConstraint = wheelNode:CreateComponent("Constraint")

    wheelObject:SetModel(GetResource("Model", "Models/Cylinder.mdl"))
    wheelObject:SetMaterial(GetResource("Material", "Materials/Stone.xml"))
    wheelObject:SetCastShadows(true)
    wheelShape:SetSphere(1.0)
    wheelBody:SetFriction(1.0)
    wheelBody:SetMass(1.0)
    wheelBody:SetLinearDamping(0.2) -- Some air resistance
    wheelBody:SetAngularDamping(0.75) -- Could also use rolling friction
    wheelBody:SetCollisionLayer(1)
    wheelConstraint:SetConstraintType(CT.HINGE)
    wheelConstraint:SetOtherBody(self.hullBody) -- Connect to the hull body
    -- Set constraint's both ends at wheel's location
    wheelConstraint:SetWorldPosition(wheelNode:GetPosition())
    wheelConstraint:SetAxis(Vector3.UP) -- Wheel rotates around its local Y-axis
    -- Wheel's hull axis points either left or right
    wheelConstraint:SetOtherAxis(offset.x >= 0.0 and Vector3.RIGHT or Vector3.LEFT)
    wheelConstraint:SetLowLimit(Vector2(-180.0, 0.0)) -- Let the wheel rotate freely around the axis
    wheelConstraint:SetHighLimit(Vector2(180.0, 0.0))
    wheelConstraint:SetDisableCollision(true) -- Let the wheel intersect the vehicle hull

    return { node = wheelNode, body = wheelBody, constraint = wheelConstraint }
end

function app:CreateInstructions()
    local ui = GetSubsystem("UI")

    -- Construct new Text object, set string to display and font to use
    local instructionText = ui:GetRoot():CreateChild("Text")
    instructionText:SetText("Use WASD keys to drive, mouse/touch to rotate camera")
    instructionText:SetFont("Fonts/Anonymous Pro.ttf", 15)
    -- The text has multiple rows. Center them in relation to each other
    instructionText:SetTextAlignment(HA.CENTER)

    -- Position the text relative to the screen center
    instructionText:SetHorizontalAlignment(HA.CENTER)
    instructionText:SetVerticalAlignment(VA.CENTER)
    instructionText:SetPosition(0, ui:GetRoot():GetHeight() / 4)
end

function app:SubscribeToEvents()
    -- Subscribe to PostUpdate event for updating the camera position after
    -- physics simulation
    SubscribeToEvent("PostUpdate", function(data) self:HandlePostUpdate() end)

    -- Vehicle steering happens on the fixed physics timestep
    SubscribeToEvent("PhysicsPreStep", function(data) self:HandleFixedUpdate() end)
end

function app:Update(timeStep)
    local input = GetSubsystem("Input")

    -- Mouse look: camera orbits around the vehicle
    local mouseMove = input:GetMouseMove()
    self.yaw = self.yaw + MOUSE_SENSITIVITY * mouseMove.x
    self.pitch = self.pitch + MOUSE_SENSITIVITY * mouseMove.y
    -- Vehicle camera pitch is clamped to a view from above
    -- (Vehicle::SetPitch)
    self.pitch = math.max(0.0, math.min(80.0, self.pitch))
end

-- Lua counterpart of Vehicle::FixedUpdate
function app:HandleFixedUpdate()
    local input = GetSubsystem("Input")
    local ui = GetSubsystem("UI")

    -- Read controls: horizontal steering, vertical acceleration
    local steer = 0.0
    local accelerator = 0.0
    if not ui:GetFocusElement() then
        if input:GetKeyDown(string.byte("a")) then steer = steer - 1.0 end
        if input:GetKeyDown(string.byte("d")) then steer = steer + 1.0 end
        if input:GetKeyDown(string.byte("w")) then accelerator = accelerator + 1.0 end
        if input:GetKeyDown(string.byte("s")) then accelerator = accelerator - 1.0 end
    end
    -- Reverse is slower
    if accelerator < 0.0 then
        accelerator = accelerator * 0.5
    end

    -- When steering, wake up the wheel rigidbodies so that their
    -- orientation is updated
    if steer ~= 0.0 then
        self.wheels.frontLeft.body:Activate()
        self.wheels.frontRight.body:Activate()
        self.steering = self.steering * 0.95 + steer * 0.05
    else
        self.steering = self.steering * 0.8 + steer * 0.2
    end

    -- Set front wheel angles
    local steeringRot = Quaternion(0.0, self.steering * MAX_WHEEL_ANGLE, 0.0)
    self.wheels.frontLeft.constraint:SetOtherAxis(steeringRot * Vector3.LEFT)
    self.wheels.frontRight.constraint:SetOtherAxis(steeringRot * Vector3.RIGHT)

    local hullRot = self.hullBody:GetRotation()
    if accelerator ~= 0.0 then
        -- Torques are applied in world space, so need to take the vehicle &
        -- wheel rotation into account
        local torqueVec = Vector3(ENGINE_POWER * accelerator, 0.0, 0.0)

        self.wheels.frontLeft.body:ApplyTorque(hullRot * steeringRot * torqueVec)
        self.wheels.frontRight.body:ApplyTorque(hullRot * steeringRot * torqueVec)
        self.wheels.rearLeft.body:ApplyTorque(hullRot * torqueVec)
        self.wheels.rearRight.body:ApplyTorque(hullRot * torqueVec)
    end

    -- Apply downforce proportional to velocity
    local localVelocity = hullRot:Inverse() * self.hullBody:GetLinearVelocity()
    self.hullBody:ApplyForce(hullRot * Vector3.DOWN * math.abs(localVelocity.z) * DOWN_FORCE)
end

-- Lua counterpart of VehicleDemo::HandlePostUpdate
function app:HandlePostUpdate()
    local vehicleNode = self.vehicleNode

    -- Physics update has completed. Position camera behind vehicle
    local dir = Quaternion(vehicleNode:GetRotation():YawAngle(), Vector3.UP)
    dir = dir * Quaternion(self.yaw, Vector3.UP)
    dir = dir * Quaternion(self.pitch, Vector3.RIGHT)

    local cameraTargetPos = vehicleNode:GetPosition() - dir * Vector3(0.0, 0.0, CAMERA_DISTANCE)
    local cameraStartPos = vehicleNode:GetPosition()

    -- Raycast camera against static objects (physics collision mask 2) and
    -- move it closer to the vehicle if something in between
    local cameraRay = Ray(cameraStartPos, cameraTargetPos - cameraStartPos)
    local cameraRayLength = (cameraTargetPos - cameraStartPos):Length()
    local hit = self.scene:GetComponent("PhysicsWorld"):RaycastSingle(cameraRay, cameraRayLength, 2)
    if hit then
        cameraTargetPos = cameraStartPos + cameraRay.direction * (hit.distance - 0.5)
    end

    self.cameraNode:SetPosition(cameraTargetPos)
    self.cameraNode:SetRotation(dir)
end

app:Run()

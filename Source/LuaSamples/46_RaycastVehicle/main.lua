-- LuaSamples/46_RaycastVehicle/main.lua
-- Lua port of Source/Samples/46_RaycastVehicle: drivable raycast vehicle on
-- a heightmap terrain. The C++ Vehicle2 logic component is replicated in
-- Lua: a MoveAndOrbitComponent holds the input state (driven by a
-- MoveAndOrbitController), while PhysicsPreStep/PostUpdate handlers apply
-- the vehicle input and toggle dust emitters. WASD drives, F brakes, mouse
-- orbits, Space toggles physics debug.

local CAMERA_DISTANCE = 10.0
local CHASSIS_WIDTH = 2.6

local app = Sample:new()
app.drawDebug = false
app.steering = 0.0
app.wheelRadius = 0.5
app.suspensionRestLength = 0.6
app.wheelWidth = 0.4
app.suspensionStiffness = 14.0
app.suspensionDamping = 2.0
app.suspensionCompression = 4.0
app.wheelFriction = 1000.0
app.rollInfluence = 0.12
app.prevVelocity = Vector3(0.0, 0.0, 0.0)
app.emitterNodes = {}

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.RELATIVE)
    input:SetMouseVisible(false)

    self:CreateScene()
    self:CreateVehicle()
    self:CreateInstructions()

    -- Vehicle input happens every physics step (C++ FixedUpdate)
    SubscribeToEvent("PhysicsPreStep", function(data) self:FixedUpdate(data.TimeStep) end)
    -- Dust emitters + camera follow after the physics step
    SubscribeToEvent("PostUpdate", function(data) self:HandlePostUpdate(data.TimeStep) end)
    -- Debug geometry
    SubscribeToEvent("PostRenderUpdate", function() self:HandlePostRenderUpdate() end)
end

function app:CreateScene()
    local scene = CreateScene()
    self.scene = scene
    scene:CreateComponent("Octree")
    scene:CreateComponent("PhysicsWorld")

    -- Camera outside the vehicle node, far clip matches fog end
    self.cameraNode = scene:CreateChild("Camera")
    local camera = self.cameraNode:CreateComponent("Camera")
    camera:SetFarClip(500.0)
    SetViewport(0, scene, camera)

    -- Zone for ambient lighting and fog control
    local zoneNode = scene:CreateChild("Zone")
    local zone = zoneNode:CreateComponent("Zone")
    zone:SetAmbientColor(Color(0.15, 0.15, 0.15))
    zone:SetFogColor(Color(0.5, 0.5, 0.7))
    zone:SetFogStart(300.0)
    zone:SetFogEnd(500.0)
    zone:SetBoundingBox(BoundingBox(-2000.0, 2000.0))

    -- Directional light with cascaded shadow mapping
    local lightNode = scene:CreateChild("DirectionalLight")
    lightNode:SetDirection(Vector3(0.3, -0.5, 0.425))
    local light = lightNode:CreateComponent("Light")
    light:SetLightType(LIGHT.DIRECTIONAL)
    light:SetCastShadows(true)
    light:SetShadowBias(0.00025, 0.5)
    light:SetShadowCascade(10.0, 50.0, 200.0, 0.0, 0.8)
    light:SetSpecularIntensity(0.5)

    -- Heightmap terrain with collision
    local terrainNode = scene:CreateChild("Terrain")
    terrainNode:SetPosition(Vector3(0.0, 0.0, 0.0))
    local terrain = terrainNode:CreateComponent("Terrain")
    terrain:SetPatchSize(64)
    terrain:SetSpacing(Vector3(3.0, 0.1, 3.0))
    terrain:SetSmoothing(true)
    terrain:SetHeightMap(GetResource("Image", "Textures/HeightMap.png"))
    terrain:SetMaterial(GetResource("Material", "Materials/Terrain.xml"))
    terrain:SetOccluder(true)
    local body = terrainNode:CreateComponent("RigidBody")
    body:SetCollisionLayer(2) -- Layer bitmask 2 for static geometry
    local shape = terrainNode:CreateComponent("CollisionShape")
    shape:SetTerrain()

    -- 1000 mushrooms on the terrain, facing outward along the normal
    local NUM_MUSHROOMS = 1000
    for i = 1, NUM_MUSHROOMS do
        local objectNode = scene:CreateChild("Mushroom")
        local position = Vector3(Random(2000.0) - 1000.0, 0.0, Random(2000.0) - 1000.0)
        position.y = terrain:GetHeight(position) - 0.1
        objectNode:SetPosition(position)
        objectNode:SetRotation(Quaternion(Vector3(0.0, 1.0, 0.0), terrain:GetNormal(position)))
        objectNode:SetScale(3.0)
        local prefabReference = objectNode:CreateComponent("PrefabReference")
        prefabReference:SetPrefab("Prefabs/Mushroom.prefab")
    end
end

function app:CreateVehicle()
    local vehicleNode = self.scene:CreateChild("Vehicle")
    vehicleNode:SetPosition(Vector3(0.0, 25.0, 0.0))
    self.vehicleNode = vehicleNode

    -- Input state holder + controller feeding it
    self.moveAndOrbit = vehicleNode:CreateComponent("MoveAndOrbitComponent")
    local controller = vehicleNode:CreateComponent("MoveAndOrbitController")
    controller:LoadInputMap("Input/MoveAndOrbit.inputmap")
    self.inputMap = controller:GetInputMap()

    -- Rendering and physics components
    self:Init()
end

-- Vehicle2::Init equivalent
function app:Init()
    local vehicleNode = self.vehicleNode
    local vehicle = vehicleNode:CreateComponent("RaycastVehicle")
    vehicle:Init()

    local hullBody = vehicleNode:GetComponent("RigidBody")
    hullBody:SetMass(800.0)
    hullBody:SetLinearDamping(0.2) -- Some air resistance
    hullBody:SetAngularDamping(0.5)
    hullBody:SetCollisionLayer(1)

    -- Collision shape
    local v3BoxExtents = Vector3(2.3, 1.0, 4.0)
    local hullColShape = vehicleNode:CreateComponent("CollisionShape")
    hullColShape:SetBox(v3BoxExtents)

    local box = vehicleNode:CreateChild()
    local hullObject = box:CreateComponent("StaticModel")
    box:SetScale(v3BoxExtents)
    hullObject:SetModel(GetResource("Model", "Models/Box.mdl"))
    hullObject:SetMaterial(GetResource("Material", "Materials/Stone.xml"))
    hullObject:SetCastShadows(true)

    -- Wheels sit at the bottom edges of the chassis
    local connectionHeight = -0.4
    local wheelDirection = Vector3(0.0, -1.0, 0.0)
    local wheelAxle = Vector3(-1.0, 0.0, 0.0)
    local wheelX = CHASSIS_WIDTH / 2.0 - self.wheelWidth
    local r2 = self.wheelRadius * 2.0
    local connectionPoints = {
        Vector3(-wheelX, connectionHeight, 2.5 - r2), -- Front left
        Vector3(wheelX, connectionHeight, 2.5 - r2),  -- Front right
        Vector3(-wheelX, connectionHeight, -2.5 + r2), -- Back left
        Vector3(wheelX, connectionHeight, -2.5 + r2)   -- Back right
    }

    for _, connectionPoint in ipairs(connectionPoints) do
        local wheelNode = vehicleNode:CreateChild()
        -- Front wheels are at z > 0, back wheels at z < 0
        local isFrontWheel = connectionPoint.z > 0.0
        local rot
        if connectionPoint.x >= 0.0 then
            rot = Quaternion(0.0, 0.0, -90.0)
        else
            rot = Quaternion(0.0, 0.0, 90.0)
        end
        wheelNode:SetRotation(rot)
        wheelNode:SetWorldPosition(vehicleNode:GetWorldPosition() + vehicleNode:GetWorldRotation() * connectionPoint)

        local wheel = wheelNode:GetOrCreateComponent("RaycastVehicleWheel")
        wheel:SetConnectionPoint(connectionPoint)
        wheel:SetDirection(wheelDirection)
        wheel:SetRotation(rot)
        wheel:SetAxle(wheelAxle)
        wheel:SetSuspensionRestLength(self.suspensionRestLength)
        wheel:SetRadius(self.wheelRadius)
        if isFrontWheel then
            wheel:SetSteeringFactor(1.0)
            wheel:SetEngineFactor(0.0)
        else
            wheel:SetSteeringFactor(0.0)
            wheel:SetEngineFactor(1.0)
        end
        wheel:SetSuspensionStiffness(self.suspensionStiffness)
        wheel:SetDampingRelaxation(self.suspensionDamping)
        wheel:SetDampingCompression(self.suspensionCompression)
        wheel:SetFrictionSlip(self.wheelFriction)
        wheel:SetRollInfluence(self.rollInfluence)

        wheelNode:SetScale(Vector3(1.0, 0.65, 1.0))
        local pWheel = wheelNode:CreateComponent("StaticModel")
        pWheel:SetModel(GetResource("Model", "Models/Cylinder.mdl"))
        pWheel:SetMaterial(GetResource("Material", "Materials/Stone.xml"))
        pWheel:SetCastShadows(true)

        self:CreateEmitter(connectionPoint)
    end

    vehicle:ResetWheels()
end

-- Vehicle2::CreateEmitter equivalent
function app:CreateEmitter(place)
    local emitter = self.scene:CreateChild()
    emitter:SetWorldPosition(self.vehicleNode:GetWorldPosition() +
        self.vehicleNode:GetWorldRotation() * place + Vector3(0.0, -self.wheelRadius, 0.0))
    local particleEmitter = emitter:CreateComponent("ParticleEmitter")
    particleEmitter:SetEffect(GetResource("ParticleEffect", "Particle/Dust.xml"))
    particleEmitter:SetEmitting(false)
    table.insert(self.emitterNodes, emitter)
end

-- Vehicle2::FixedUpdate equivalent
function app:FixedUpdate(timeStep)
    local vehicle = self.vehicleNode:GetComponent("RaycastVehicle")
    local brakingForce = 0.0

    -- Read controls
    local vel = self.moveAndOrbit:GetVelocity()
    local newSteering = vel.x
    local accelerator = vel.z
    if accelerator < 0.0 then
        accelerator = accelerator * 0.5
    end

    if self.inputMap and self.inputMap:Evaluate("Brake") > 0.5 then
        brakingForce = 1.0
    end

    -- Smooth steering
    if newSteering ~= 0.0 then
        self.steering = self.steering * 0.95 + newSteering * 0.05
    else
        self.steering = self.steering * 0.8 + newSteering * 0.2
    end

    -- Apply forces
    vehicle:UpdateInput(self.steering, accelerator, brakingForce)
end

function app:CreateInstructions()
    local root = GetUIRoot()
    local instructionText = root:CreateChild("Text")
    instructionText:SetText(
        "Use WASD keys to drive, F to brake, mouse/touch to rotate camera\n" ..
        "Space to toggle physics debug geometry")
    instructionText:SetFont("Fonts/Anonymous Pro.ttf", 15)
    instructionText:SetTextAlignment(HA.CENTER)
    instructionText:SetHorizontalAlignment(HA.CENTER)
    instructionText:SetVerticalAlignment(VA.CENTER)
    instructionText:SetPosition(0, root:GetHeight() / 4)
end

function app:Update(timeStep)
    local input = GetSubsystem("Input")
    local ui = GetSubsystem("UI")
    -- Toggle debug geometry with Space when no UI element is focused
    if not ui:GetFocusElement() then
        if input:GetKeyPress(KEY.SPACE) then
            self.drawDebug = not self.drawDebug
        end
    end
end

-- Vehicle2::PostUpdate (dust) + RaycastVehicleDemo::HandlePostUpdate (camera)
function app:HandlePostUpdate(timeStep)
    local vehicleNode = self.vehicleNode
    local vehicle = vehicleNode:GetComponent("RaycastVehicle")
    local vehicleBody = vehicleNode:GetComponent("RigidBody")

    -- Dust emitters follow skidding wheels
    local velocity = vehicleBody:GetLinearVelocity()
    local accel = (velocity - self.prevVelocity) * (1.0 / timeStep)
    local planeAccel = Vector3(accel.x, 0.0, accel.z):Length()
    for i = 0, vehicle:GetNumWheels() - 1 do
        local emitter = self.emitterNodes[i + 1]
        local wheel = vehicle:GetWheel(i)
        local particleEmitter = emitter:GetComponent("ParticleEmitter")
        if wheel:IsInContact()
            and (wheel:GetSkidInfoCumulative() < 0.9 or wheel:GetBrakeValue() > 2.0 or planeAccel > 15.0) then
            emitter:SetWorldPosition(wheel:GetContactPosition())
            if not particleEmitter:IsEmitting() then
                particleEmitter:SetEmitting(true)
            end
        elseif particleEmitter:IsEmitting() then
            particleEmitter:SetEmitting(false)
        end
    end
    self.prevVelocity = velocity

    -- Position camera behind the vehicle
    local dir = Quaternion(vehicleNode:GetRotation():YawAngle(), Vector3(0.0, 1.0, 0.0))
    dir = dir * Quaternion(self.moveAndOrbit:GetYaw(), Vector3(0.0, 1.0, 0.0))
    dir = dir * Quaternion(self.moveAndOrbit:GetPitch(), Vector3(1.0, 0.0, 0.0))
    local cameraTargetPos = vehicleNode:GetPosition() - dir * Vector3(0.0, 0.0, CAMERA_DISTANCE)
    local cameraStartPos = vehicleNode:GetPosition()
    -- Raycast camera against static objects (collision mask 2)
    local cameraRay = Ray(cameraStartPos, cameraTargetPos - cameraStartPos)
    local cameraRayLength = (cameraTargetPos - cameraStartPos):Length()
    local result = self.scene:GetComponent("PhysicsWorld"):RaycastSingle(cameraRay, cameraRayLength, 2)
    if result then
        cameraTargetPos = cameraStartPos + cameraRay.direction * (result.distance - 0.5)
    end
    self.cameraNode:SetPosition(cameraTargetPos)
    self.cameraNode:SetRotation(dir)
end

function app:HandlePostRenderUpdate()
    if self.drawDebug then
        local debug = self.scene:GetOrCreateComponent("DebugRenderer")
        local raycastVehicle = self.vehicleNode:GetComponent("RaycastVehicle")

        local depthTest = false
        raycastVehicle:GetNode():GetComponent("RigidBody"):DrawDebugGeometry(debug, depthTest)

        for i = 0, raycastVehicle:GetNumWheels() - 1 do
            local wheel = raycastVehicle:GetWheel(i)
            wheel:DrawDebugGeometry(debug, depthTest)
        end
    end
end

app:Run()

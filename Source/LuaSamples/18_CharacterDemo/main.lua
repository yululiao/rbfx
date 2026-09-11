-- LuaSamples/18_CharacterDemo/main.lua
-- Lua port of Source/Samples/18_CharacterDemo: a third/first person
-- character controller with physics-driven movement, jumping and animation
-- blending. The C++ Character component (a MoveAndOrbitComponent subclass
-- reading an InputMap) is replaced by plain Lua state: WASD/Space are read
-- directly, mouse look accumulates into yaw/pitch, and the ground flag comes
-- from parsing NodeCollision contacts.

-- Character movement constants (Character.h)
local MOVE_FORCE = 0.8
local INAIR_MOVE_FORCE = 0.02
local BRAKE_FORCE = 0.2
local JUMP_FORCE = 7.0
local INAIR_THRESHOLD_TIME = 0.1

-- Camera zoom limits (CharacterDemo.cpp)
local CAMERA_MIN_DIST = 1.0
local CAMERA_INITIAL_DIST = 5.0
local CAMERA_MAX_DIST = 20.0

-- Mouse sensitivity as degrees per pixel
local MOUSE_SENSITIVITY = 0.1

local app = Sample:new()
app.firstPerson = false
app.onGround = false
app.okToJump = true
app.inAirTimer = 0.0

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.RELATIVE)
    input:SetMouseVisible(false)

    self:CreateScene()
    self:CreateCharacter()
    self:CreateInstructions()
    self:SubscribeToEvents()
end

function app:CreateScene()
    local scene = CreateScene()
    self.scene = scene

    -- Create scene subsystem components
    scene:CreateComponent("Octree")
    scene:CreateComponent("PhysicsWorld")

    -- Create camera and define viewport inside the scene
    self.cameraNode = scene:CreateChild("Camera")
    local camera = self.cameraNode:CreateComponent("Camera")
    camera:SetFarClip(300.0)
    SetViewport(0, scene, camera)

    -- Create a zone for ambient lighting and fog control
    local zoneNode = scene:CreateChild("Zone")
    local zone = zoneNode:CreateComponent("Zone")
    zone:SetAmbientColor(Color(0.15, 0.15, 0.15))
    zone:SetFogColor(Color(0.5, 0.5, 0.7))
    zone:SetFogStart(100.0)
    zone:SetFogEnd(300.0)
    zone:SetBoundingBox(BoundingBox(-1000.0, 1000.0))

    -- Create a directional light with cascaded shadow mapping
    local lightNode = scene:CreateChild("DirectionalLight")
    lightNode:SetDirection(Vector3(0.3, -0.5, 0.425))
    local light = lightNode:CreateComponent("Light")
    light:SetLightType(LIGHT.DIRECTIONAL)
    light:SetCastShadows(true)
    light:SetShadowBias(0.00025, 0.5)
    light:SetShadowCascade(10.0, 50.0, 200.0, 0.0, 0.8)
    light:SetSpecularIntensity(0.5)

    -- Create the floor object
    local floorNode = scene:CreateChild("Floor")
    floorNode:SetPosition(Vector3(0.0, -0.5, 0.0))
    floorNode:SetScale(Vector3(200.0, 1.0, 200.0))
    local floorObject = floorNode:CreateComponent("StaticModel")
    floorObject:SetModel(GetResource("Model", "Models/Box.mdl"))
    floorObject:SetMaterial(GetResource("Material", "Materials/Stone.xml"))

    local body = floorNode:CreateComponent("RigidBody")
    -- Use collision layer bit 2 to mark world scenery. This is what we will
    -- raycast against to prevent camera from going inside geometry
    body:SetCollisionLayer(2)
    local shape = floorNode:CreateComponent("CollisionShape")
    shape:SetBox(Vector3.ONE)

    -- Create swing doors based on physics
    for _, z in ipairs({ -2.0, 2.0 }) do
        local objectNode = scene:CreateChild("Door")
        objectNode:SetPosition(Vector3(2.0, 0.5, z))
        local prefabReference = objectNode:CreateComponent("PrefabReference")
        prefabReference:SetPrefab("Prefabs/Door.prefab")
    end

    -- Create sliding door, inlined so it becomes plain scene content
    do
        local objectNode = scene:CreateChild("SlidingDoor")
        objectNode:SetPosition(Vector3(-3.0, 0.0, -3.0))
        local prefabReference = objectNode:CreateComponent("PrefabReference")
        prefabReference:SetPrefab("Prefabs/SlidingDoor.prefab")
        prefabReference:InlineAggressive()
    end

    -- Create mushrooms of varying sizes
    for _ = 1, 60 do
        local objectNode = scene:CreateChild("Mushroom")
        objectNode:SetPosition(Vector3(Random(180.0) - 90.0, 0.0, Random(180.0) - 90.0))
        objectNode:SetRotation(Quaternion(0.0, Random(360.0), 0.0))
        objectNode:SetScale(2.0 + Random(5.0))
        local prefabReference = objectNode:CreateComponent("PrefabReference")
        prefabReference:SetPrefab("Prefabs/Mushroom.prefab")
    end

    -- Create movable boxes. Let them fall from the sky at first
    for _ = 1, 100 do
        local scale = Random(2.0) + 0.5

        local objectNode = scene:CreateChild("Box")
        objectNode:SetPosition(Vector3(Random(180.0) - 90.0, Random(10.0) + 10.0, Random(180.0) - 90.0))
        objectNode:SetRotation(Quaternion(Random(360.0), Random(360.0), Random(360.0)))
        objectNode:SetScale(scale)
        local object = objectNode:CreateComponent("StaticModel")
        object:SetModel(GetResource("Model", "Models/Box.mdl"))
        object:SetMaterial(GetResource("Material", "Materials/Stone.xml"))
        object:SetCastShadows(true)

        local body = objectNode:CreateComponent("RigidBody")
        body:SetCollisionLayer(2)
        -- Bigger boxes will be heavier and harder to move
        body:SetMass(scale * 2.0)
        local shape = objectNode:CreateComponent("CollisionShape")
        shape:SetBox(Vector3.ONE)
    end
end

function app:CreateCharacter()
    local objectNode = self.scene:CreateChild("Jack")
    objectNode:SetPosition(Vector3(0.0, 1.0, 0.0))
    self.characterNode = objectNode

    -- Spin node: the Mutant model faces away, rotate it towards the camera
    local adjustNode = objectNode:CreateChild("AdjNode")
    adjustNode:SetRotation(Quaternion(180.0, Vector3(0.0, 1.0, 0.0)))

    -- Create the rendering component + animation controller
    local object = adjustNode:CreateComponent("AnimatedModel")
    object:SetModel(GetResource("Model", "Models/Mutant/Mutant.mdl"))
    object:SetMaterial(GetResource("Material", "Models/Mutant/Materials/mutant_M.xml"))
    object:SetCastShadows(true)
    self.animCtrl = adjustNode:CreateComponent("AnimationController")

    -- Set the head bone for manual control
    local headBone = object:GetSkeleton():GetBone("Mutant:Head")
    if headBone then
        headBone.animated = false
    end

    -- Create rigidbody, and set non-zero mass so that the body becomes
    -- dynamic
    local body = objectNode:CreateComponent("RigidBody")
    body:SetCollisionLayer(1)
    body:SetMass(1.0)

    -- Set zero angular factor so that physics doesn't turn the character on
    -- its own. Instead we will control the character yaw manually
    body:SetAngularFactor(Vector3.ZERO)

    -- Set the rigidbody to signal collision also when in rest, so that we
    -- get ground collisions properly
    body:SetCollisionEventMode(CEM.ALWAYS)

    -- Set a capsule shape for collision
    local shape = objectNode:CreateComponent("CollisionShape")
    shape:SetCapsule(0.7, 1.8, Vector3(0.0, 0.9, 0.0))

    -- Cache the animations the character state machine switches between
    self.runAnimation = GetResource("Animation", "Models/Mutant/Mutant_Run.ani")
    self.idleAnimation = GetResource("Animation", "Models/Mutant/Mutant_Idle0.ani")
    self.jumpAnimation = GetResource("Animation", "Models/Mutant/Mutant_Jump1.ani")
end

function app:CreateInstructions()
    local ui = GetSubsystem("UI")

    -- Construct new Text object, set string to display and font to use
    local instructionText = ui:GetRoot():CreateChild("Text")
    instructionText:SetText(
        "Use WASD keys and mouse/touch to move\n" ..
        "Space to jump, F to toggle 1st/3rd person")
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

    -- Character steering happens on the fixed physics timestep
    SubscribeToEvent("PhysicsPreStep", function(data) self:HandleFixedUpdate(data.TimeStep) end)

    -- Ground contacts come through the character node's collision event
    SubscribeToEvent(self.characterNode, "NodeCollision", function(data)
        self:HandleNodeCollision(data)
    end)
end

function app:Update(timeStep)
    local input = GetSubsystem("Input")
    local ui = GetSubsystem("UI")

    -- Mouse look: yaw/pitch accumulate from relative mouse movement
    local mouseMove = input:GetMouseMove()
    self.yaw = self.yaw + MOUSE_SENSITIVITY * mouseMove.x
    self.pitch = self.pitch + MOUSE_SENSITIVITY * mouseMove.y
    self.pitch = math.max(-90.0, math.min(90.0, self.pitch))

    if not ui:GetFocusElement() then
        -- Set rotation already here so that it's updated every rendering
        -- frame instead of every physics frame
        self.characterNode:SetRotation(Quaternion(self.yaw, Vector3.UP))

        -- Switch between 1st and 3rd person
        if input:GetKeyPress(string.byte("f")) then
            self.firstPerson = not self.firstPerson
        end
    end
end

-- Lua counterpart of Character::FixedUpdate
function app:HandleFixedUpdate(timeStep)
    local body = self.characterNode:GetComponent("RigidBody")
    local input = GetSubsystem("Input")
    local ui = GetSubsystem("UI")
    if not body then
        return
    end

    -- Update the in air timer. Reset if grounded
    if not self.onGround then
        self.inAirTimer = self.inAirTimer + timeStep
    else
        self.inAirTimer = 0.0
    end
    -- When character has been in air less than 1/10 second, it's still
    -- interpreted as being on ground
    local softGrounded = self.inAirTimer < INAIR_THRESHOLD_TIME

    -- Collect movement direction from keys, in node's local space.
    -- Normalize so that diagonal strafing is not faster
    local moveDir = Vector3(0.0, 0.0, 0.0)
    if not ui:GetFocusElement() then
        if input:GetKeyDown(string.byte("w")) then moveDir = moveDir + Vector3.FORWARD end
        if input:GetKeyDown(string.byte("s")) then moveDir = moveDir + Vector3.BACK end
        if input:GetKeyDown(string.byte("a")) then moveDir = moveDir + Vector3.LEFT end
        if input:GetKeyDown(string.byte("d")) then moveDir = moveDir + Vector3.RIGHT end
    end
    local moving = moveDir:LengthSquared() > 0.0
    if moving then
        moveDir = moveDir:Normalized()
    end

    -- Update movement & animation
    local rot = self.characterNode:GetRotation()
    local velocity = body:GetLinearVelocity()
    -- Velocity on the XZ plane
    local planeVelocity = Vector3(velocity.x, 0.0, velocity.z)

    -- If in air, allow control, but slower than when on ground
    body:ApplyImpulse(rot * moveDir * (softGrounded and MOVE_FORCE or INAIR_MOVE_FORCE))

    if softGrounded then
        -- When on ground, apply a braking force to limit maximum ground
        -- velocity
        body:ApplyImpulse(planeVelocity * -BRAKE_FORCE)

        -- Jump. Must release jump control between jumps
        if not ui:GetFocusElement() and input:GetKeyDown(KEY.SPACE) then
            if self.okToJump then
                body:ApplyImpulse(Vector3.UP * JUMP_FORCE)
                self.okToJump = false
                self.onGround = false
                self.animCtrl:PlayNewExclusive(self.jumpAnimation, false, nil, nil, 0.2, true)
            end
        else
            self.okToJump = true
        end
    end

    if not self.onGround then
        self.animCtrl:PlayExistingExclusive(self.jumpAnimation, false, true, 0.2)
    else
        -- Play walk animation if moving on ground, otherwise fade it out
        if softGrounded and moving then
            self.animCtrl:PlayExistingExclusive(self.runAnimation, true, false, 0.2)
        else
            self.animCtrl:PlayExistingExclusive(self.idleAnimation, true, false, 0.2)
        end

        -- Set walk animation speed proportional to velocity
        self.animCtrl:SetSpeed("Models/Mutant/Mutant_Run.ani", planeVelocity:Length() * 0.3)
    end

    -- Reset grounded flag for next frame
    self.onGround = false
end

-- Lua counterpart of Character::HandleNodeCollision
function app:HandleNodeCollision(data)
    -- Check collision contacts and see if character is standing on ground
    -- (look for a contact that has near vertical normal)
    local contacts = MemoryBuffer(data.Contacts)

    while not contacts:IsEof() do
        local contactPosition = contacts:ReadVector3()
        local contactNormal = contacts:ReadVector3()
        contacts:ReadFloat() -- contact distance
        contacts:ReadFloat() -- contact impulse

        -- If contact is below node center and pointing up, assume it's a
        -- ground contact
        if contactPosition.y < (self.characterNode:GetPosition().y + 1.0) then
            if contactNormal.y > 0.75 then
                self.onGround = true
            end
        end
    end
end

-- Lua counterpart of CharacterDemo::HandlePostUpdate
function app:HandlePostUpdate()
    local characterNode = self.characterNode

    -- Get camera lookat dir from character yaw + pitch
    local rot = characterNode:GetRotation()
    local dir = rot * Quaternion(self.pitch, Vector3.RIGHT)

    -- Turn head to camera pitch, but limit to avoid unnatural animation
    local headNode = characterNode:GetChild("Mutant:Head", true)
    if headNode then
        local limitPitch = math.max(-45.0, math.min(45.0, self.pitch))
        local headDir = rot * Quaternion(limitPitch, Vector3(1.0, 0.0, 0.0))
        -- This could be expanded to look at an arbitrary target, now just
        -- look at a point in front
        local headWorldTarget = headNode:GetWorldPosition() + headDir * Vector3(0.0, 0.0, -1.0)
        headNode:LookAt(headWorldTarget, Vector3(0.0, 1.0, 0.0), TS.WORLD)
    end

    if self.firstPerson then
        self.cameraNode:SetPosition(headNode:GetWorldPosition() + rot * Vector3(0.0, 0.15, 0.2))
        self.cameraNode:SetRotation(dir)
    else
        -- Third person camera: position behind the character
        local aimPoint = characterNode:GetPosition() + rot * Vector3(0.0, 1.7, 0.0)

        -- Collide camera ray with static physics objects (layer bitmask 2)
        -- to ensure we see the character properly
        local rayDir = dir * Vector3.BACK
        local rayDistance = CAMERA_INITIAL_DIST
        local hit = self.scene:GetComponent("PhysicsWorld"):RaycastSingle(
            Ray(aimPoint, rayDir), rayDistance, 2)
        if hit then
            rayDistance = math.min(rayDistance, hit.distance)
        end
        rayDistance = math.max(CAMERA_MIN_DIST, math.min(CAMERA_MAX_DIST, rayDistance))

        self.cameraNode:SetPosition(aimPoint + rayDir * rayDistance)
        self.cameraNode:SetRotation(dir)
    end
end

app:Run()

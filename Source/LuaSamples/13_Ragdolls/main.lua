-- LuaSamples/13_Ragdolls/main.lua
-- Lua port of Source/Samples/13_Ragdolls: a grid of animated Jack models.
-- Each model has a trigger rigid body; when hit by a moving object, the
-- keyframe animation is disabled and a ragdoll of rigid bodies + constraints
-- is built on its bones. LMB shoots a sphere from the camera.

local app = Sample:new()
app.drawDebug = false

-- Bone physics definitions: { name, isBox, size, offset, rotation }.
-- Mirrors CreateRagdoll.cpp CreateRagdollBone calls.
local BONES = {
    { "Bip01_Pelvis",      true,  Vector3(0.3,   0.2,  0.25 ), Vector3(0.0,  0.0, 0.0), Quaternion(0.0, 0.0, 0.0) },
    { "Bip01_Spine1",      true,  Vector3(0.35,  0.2,  0.3  ), Vector3(0.15, 0.0, 0.0), Quaternion(0.0, 0.0, 0.0) },
    { "Bip01_L_Thigh",     false, Vector3(0.175, 0.45, 0.175), Vector3(0.25, 0.0, 0.0), Quaternion(0.0, 0.0, 90.0) },
    { "Bip01_R_Thigh",     false, Vector3(0.175, 0.45, 0.175), Vector3(0.25, 0.0, 0.0), Quaternion(0.0, 0.0, 90.0) },
    { "Bip01_L_Calf",      false, Vector3(0.15,  0.55, 0.15 ), Vector3(0.25, 0.0, 0.0), Quaternion(0.0, 0.0, 90.0) },
    { "Bip01_R_Calf",      false, Vector3(0.15,  0.55, 0.15 ), Vector3(0.25, 0.0, 0.0), Quaternion(0.0, 0.0, 90.0) },
    { "Bip01_Head",        true,  Vector3(0.2,   0.2,  0.2  ), Vector3(0.1,  0.0, 0.0), Quaternion(0.0, 0.0, 0.0) },
    { "Bip01_L_UpperArm",  false, Vector3(0.15,  0.35, 0.15 ), Vector3(0.1,  0.0, 0.0), Quaternion(0.0, 0.0, 90.0) },
    { "Bip01_R_UpperArm",  false, Vector3(0.15,  0.35, 0.15 ), Vector3(0.1,  0.0, 0.0), Quaternion(0.0, 0.0, 90.0) },
    { "Bip01_L_Forearm",   false, Vector3(0.125, 0.4,  0.125), Vector3(0.2,  0.0, 0.0), Quaternion(0.0, 0.0, 90.0) },
    { "Bip01_R_Forearm",   false, Vector3(0.125, 0.4,  0.125), Vector3(0.2,  0.0, 0.0), Quaternion(0.0, 0.0, 90.0) },
}

-- Constraint definitions: { bone, parent, type, axis, parentAxis, highLimit,
-- lowLimit, disableCollision }. Mirrors CreateRagdoll.cpp calls.
local CONSTRAINTS = {
    { "Bip01_L_Thigh",    "Bip01_Pelvis",     CT.CONETWIST, Vector3.BACK,    Vector3.FORWARD, Vector2(45.0, 45.0), Vector2(0.0, 0.0),   true },
    { "Bip01_R_Thigh",    "Bip01_Pelvis",     CT.CONETWIST, Vector3.BACK,    Vector3.FORWARD, Vector2(45.0, 45.0), Vector2(0.0, 0.0),   true },
    { "Bip01_L_Calf",     "Bip01_L_Thigh",    CT.HINGE,     Vector3.BACK,    Vector3.BACK,    Vector2(90.0, 0.0),  Vector2(0.0, 0.0),   true },
    { "Bip01_R_Calf",     "Bip01_R_Thigh",    CT.HINGE,     Vector3.BACK,    Vector3.BACK,    Vector2(90.0, 0.0),  Vector2(0.0, 0.0),   true },
    { "Bip01_Spine1",     "Bip01_Pelvis",     CT.HINGE,     Vector3.FORWARD, Vector3.FORWARD, Vector2(45.0, 0.0),  Vector2(-10.0, 0.0), true },
    { "Bip01_Head",       "Bip01_Spine1",     CT.CONETWIST, Vector3.LEFT,    Vector3.LEFT,    Vector2(0.0, 30.0),  Vector2(0.0, 0.0),   true },
    { "Bip01_L_UpperArm", "Bip01_Spine1",     CT.CONETWIST, Vector3.DOWN,    Vector3.UP,      Vector2(45.0, 45.0), Vector2(0.0, 0.0),   false },
    { "Bip01_R_UpperArm", "Bip01_Spine1",     CT.CONETWIST, Vector3.DOWN,    Vector3.UP,      Vector2(45.0, 45.0), Vector2(0.0, 0.0),   false },
    { "Bip01_L_Forearm",  "Bip01_L_UpperArm", CT.HINGE,     Vector3.BACK,    Vector3.BACK,    Vector2(90.0, 0.0),  Vector2(0.0, 0.0),   true },
    { "Bip01_R_Forearm",  "Bip01_R_UpperArm", CT.HINGE,     Vector3.BACK,    Vector3.BACK,    Vector2(90.0, 0.0),  Vector2(0.0, 0.0),   true },
}

-- Create a rigid body + collision shape on a bone node (Lua counterpart of
-- CreateRagdoll::CreateRagdollBone).
local function CreateRagdollBone(rootNode, def)
    local boneNode = rootNode:GetChild(def[1], true)
    if not boneNode then
        LogError("Could not find bone " .. def[1] .. " for creating ragdoll physics components")
        return
    end

    local body = boneNode:CreateComponent("RigidBody")
    -- Set mass to make movable
    body:SetMass(1.0)
    -- Set damping parameters to smooth out the motion
    body:SetLinearDamping(0.05)
    body:SetAngularDamping(0.85)
    -- Set rest thresholds so the ragdoll bodies eventually come to rest
    body:SetLinearRestThreshold(1.5)
    body:SetAngularRestThreshold(2.5)

    local shape = boneNode:CreateComponent("CollisionShape")
    -- We use either a box or a capsule shape for all of the bones
    if def[2] then
        shape:SetBox(def[3], def[4], def[5])
    else
        shape:SetCapsule(def[3].x, def[3].y, def[4], def[5])
    end
end

-- Create a constraint between two bones (Lua counterpart of
-- CreateRagdoll::CreateRagdollConstraint).
local function CreateRagdollConstraint(rootNode, def)
    local boneNode = rootNode:GetChild(def[1], true)
    local parentNode = rootNode:GetChild(def[2], true)
    if not boneNode or not parentNode then
        LogError("Could not find bones " .. def[1] .. "/" .. def[2] .. " for creating ragdoll constraint")
        return
    end

    local constraint = boneNode:CreateComponent("Constraint")
    constraint:SetConstraintType(def[3])
    -- Most of the constraints work better when the connected bodies
    -- don't collide against each other
    constraint:SetDisableCollision(def[8])
    -- The connected body must be specified before setting the world position
    constraint:SetOtherBody(parentNode:GetComponent("RigidBody"))
    -- Position the constraint at the child bone we are connecting
    constraint:SetWorldPosition(boneNode:GetWorldPosition())
    -- Configure axes and limits
    constraint:SetAxis(def[4])
    constraint:SetOtherAxis(def[5])
    constraint:SetHighLimit(def[6])
    constraint:SetLowLimit(def[7])
end

-- Convert one animated model into a ragdoll, then unsubscribe (Lua
-- counterpart of the CreateRagdoll component's collision handler).
local function CreateRagdoll(node)
    -- We do not need the physics components in the model's root node anymore
    node:RemoveComponent("RigidBody")
    node:RemoveComponent("CollisionShape")

    -- Create RigidBody & CollisionShape components on the bones
    for _, def in ipairs(BONES) do
        CreateRagdollBone(node, def)
    end

    -- Create Constraints between bones
    for _, def in ipairs(CONSTRAINTS) do
        CreateRagdollConstraint(node, def)
    end

    -- Disable keyframe animation from all bones so they will not interfere
    -- with the ragdoll
    local model = node:GetComponent("AnimatedModel")
    local skeleton = model:GetSkeleton()
    for i = 0, skeleton:GetNumBones() - 1 do
        skeleton:GetBone(i).animated = false
    end
end

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

    -- Create octree, physics simulation world and debug renderer. The
    -- PhysicsWorld must exist before creating physics components
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
    -- Set cascade splits at 10, 50 and 200 world units, fade shadows out at
    -- 80% of maximum shadow distance
    light:SetShadowCascade(10.0, 50.0, 200.0, 0.0, 0.8)

    -- Create a floor object, 500 x 500 world units. Adjust position so that
    -- the ground is at zero Y
    do
        local floorNode = scene:CreateChild("Floor")
        floorNode:SetPosition(Vector3(0.0, -0.5, 0.0))
        floorNode:SetScale(Vector3(500.0, 1.0, 500.0))
        local floorObject = floorNode:CreateComponent("StaticModel")
        floorObject:SetModel(GetResource("Model", "Models/Box.mdl"))
        floorObject:SetMaterial(GetResource("Material", "Materials/StoneTiled.xml"))

        -- Make the floor physical. Non-zero rolling friction so that the
        -- spheres will eventually come to rest
        local body = floorNode:CreateComponent("RigidBody")
        body:SetRollingFriction(0.15)
        local shape = floorNode:CreateComponent("CollisionShape")
        -- Set a box shape of size 1 x 1 x 1 for collision
        shape:SetBox(Vector3.ONE)
    end

    -- Create animated models. Each acts as a trigger: hit by a moving
    -- object, it turns into a ragdoll
    for z = -1, 1 do
        for x = -4, 4 do
            local modelNode = scene:CreateChild("Jack")
            modelNode:SetPosition(Vector3(x * 5.0, 0.0, z * 5.0))
            modelNode:SetRotation(Quaternion(0.0, 180.0, 0.0))
            local modelObject = modelNode:CreateComponent("AnimatedModel")
            modelObject:SetModel(GetResource("Model", "Models/Jack.mdl"))
            modelObject:SetCastShadows(true)
            -- Also update when invisible to avoid staying invisible when the
            -- model should come into view, but does not as the bounding box
            -- is not updated
            modelObject:SetUpdateInvisible(true)

            -- Rigid body + collision shape act as the trigger
            local body = modelNode:CreateComponent("RigidBody")
            -- Trigger mode: only detect collisions, impart no forces
            body:SetTrigger(true)
            local shape = modelNode:CreateComponent("CollisionShape")
            -- Capsule shape with an offset so it aligns with the model,
            -- which has its origin at the feet
            shape:SetCapsule(0.7, 2.0, Vector3(0.0, 1.0, 0.0))

            -- React to collisions: create the ragdoll on the first hit by a
            -- moving object (the C++ sample uses a custom component that
            -- removes itself; Lua uses a closure flag instead)
            local ragdollCreated = false
            SubscribeToEvent(modelNode, "NodeCollision", function(data)
                if ragdollCreated then
                    return
                end
                local otherBody = data.OtherBody
                if otherBody and otherBody:GetMass() > 0.0 then
                    ragdollCreated = true
                    CreateRagdoll(modelNode)
                end
            end)
        end
    end

    -- Create the camera and a free-fly controller for WASD + mouse movement
    local cameraNode = scene:CreateChild("Camera")
    cameraNode:CreateComponent("FreeFlyController")
    local camera = cameraNode:CreateComponent("Camera")
    camera:SetFarClip(300.0)
    cameraNode:SetPosition(Vector3(0.0, 3.0, -20.0))
    self.cameraNode = cameraNode
    SetViewport(0, scene, camera)
end

function app:Update(timeStep)
    -- Do not act if the UI has a focused element
    if GetSubsystem("UI"):GetFocusElement() then
        return
    end

    local input = GetSubsystem("Input")

    -- "Shoot" a physics object with left mouse button
    if input:GetMouseButtonPress(MOUSEB.LEFT) then
        self:SpawnObject()
    end
end

function app:SpawnObject()
    -- Create a small sphere at camera position. Give it rolling friction so
    -- that it will eventually come to rest
    local sphereNode = self.scene:CreateChild("Sphere")
    sphereNode:SetPosition(self.cameraNode:GetPosition())
    sphereNode:SetRotation(self.cameraNode:GetRotation())
    sphereNode:SetScale(0.25)
    local sphereObject = sphereNode:CreateComponent("StaticModel")
    sphereObject:SetModel(GetResource("Model", "Models/Sphere.mdl"))
    sphereObject:SetMaterial(GetResource("Material", "Materials/StoneSmall.xml"))
    sphereObject:SetCastShadows(true)

    local body = sphereNode:CreateComponent("RigidBody")
    body:SetMass(1.0)
    body:SetRollingFriction(0.15)
    local shape = sphereNode:CreateComponent("CollisionShape")
    shape:SetSphere(1.0)

    local OBJECT_VELOCITY = 10.0

    -- Set initial velocity from camera forward vector, plus a slight up
    -- component to overcome gravity better
    local cameraRotation = self.cameraNode:GetRotation()
    body:SetLinearVelocity(cameraRotation * Vector3(0.0, 0.25, 1.0) * OBJECT_VELOCITY)
end

-- Toggle physics debug geometry with space
function app:OnKeyDown(key)
    if key == KEY.SPACE then
        self.drawDebug = not self.drawDebug
    end
end

app:Run()

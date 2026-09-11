-- LuaSamples/28_Physics2DRope/main.lua
-- Lua port of Source/Samples/28_Physics2DRope: a chain of 9 light boxes and a
-- heavy final box hanging from a static ground body, connected with revolute
-- joints and additionally constrained by a rope joint. Joint debug drawing
-- is enabled.

local NUM_OBJECTS = 10

local app = Sample:new()

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.FREE)
    input:SetMouseVisible(true)

    self:CreateScene()
    self:CreateInstructions()
end

function app:CreateScene()
    local scene = CreateScene()
    self.scene = scene
    scene:CreateComponent("Octree")
    scene:CreateComponent("DebugRenderer")

    -- Orthographic camera; 0.05 world units per pixel
    local cameraNode = scene:CreateChild("Camera")
    cameraNode:SetPosition(Vector3(0.0, 5.0, -10.0))
    self.cameraNode = cameraNode

    local graphics = GetSubsystem("Graphics")
    local camera = cameraNode:CreateComponent("Camera")
    camera:SetOrthographic(true)
    camera:SetOrthoSize(graphics:GetHeight() * 0.05)
    camera:SetZoom(1.5 * math.min(graphics:GetWidth() / 1280.0, graphics:GetHeight() / 800.0))
    self.camera = camera

    -- 2D physics world with joint debug drawing
    local physicsWorld = scene:CreateComponent("PhysicsWorld2D")
    physicsWorld:SetDrawJoint(true)

    -- Ground (static body with an edge collider)
    local groundNode = scene:CreateChild("Ground")
    local groundBody = groundNode:CreateComponent("RigidBody2D")
    local groundShape = groundNode:CreateComponent("CollisionEdge2D")
    groundShape:SetVertices(Vector2(-40.0, 0.0), Vector2(40.0, 0.0))

    -- Rope chain
    local y = 15.0
    local prevBody = groundBody

    for i = 0, NUM_OBJECTS - 1 do
        local node = scene:CreateChild("RigidBody")

        local body = node:CreateComponent("RigidBody2D")
        body:SetBodyType(BT2D.DYNAMIC)

        local box = node:CreateComponent("CollisionBox2D")
        box:SetFriction(0.2)
        -- Collide with everything except category 0x0002 (the heavy box)
        box:SetMaskBits(0xFFFF & ~0x0002)

        if i == NUM_OBJECTS - 1 then
            -- The heavy end box
            node:SetPosition(Vector3(1.0 * i, y, 0.0))
            body:SetAngularDamping(0.4)
            box:SetSize(3.0, 3.0)
            box:SetDensity(100.0)
            box:SetCategoryBits(0x0002)
        else
            -- A light chain link
            node:SetPosition(Vector3(0.5 + 1.0 * i, y, 0.0))
            box:SetSize(1.0, 0.25)
            box:SetDensity(20.0)
            box:SetCategoryBits(0x0001)
        end

        -- Revolute joint to the previous body
        local joint = node:CreateComponent("ConstraintRevolute2D")
        joint:SetOtherBody(prevBody)
        joint:SetAnchor(Vector2(i, y))
        joint:SetCollideConnected(false)

        prevBody = body
    end

    -- Rope joint from the ground to the last body, slightly longer than the
    -- chain so it only engages when the chain is stretched
    local constraintRope = groundNode:CreateComponent("ConstraintRope2D")
    constraintRope:SetOtherBody(prevBody)
    constraintRope:SetOwnerBodyAnchor(Vector2(0.0, y))
    constraintRope:SetMaxLength(NUM_OBJECTS - 1.0 + 0.01)

    SetViewport(0, scene, camera)
end

function app:CreateInstructions()
    local cache = GetSubsystem("ResourceCache")

    local instructionText = GetUIRoot():CreateChild("Text")
    instructionText:SetText("Use WASD keys and mouse/touch to move, Use PageUp PageDown to zoom.")
    instructionText:SetFont(cache:GetResource("Font", "Fonts/Anonymous Pro.ttf"), 15)
    instructionText:SetHorizontalAlignment(HA.CENTER)
    instructionText:SetVerticalAlignment(VA.CENTER)
    instructionText:SetPosition(0, GetUIRoot():GetHeight() / 4)
end

function app:MoveCamera(timeStep)
    -- Do not move if the UI has a focused element (the console)
    if GetSubsystem("UI"):GetFocusElement() then
        return
    end

    local input = GetSubsystem("Input")
    local MOVE_SPEED = 4.0 -- world units per second

    if input:GetKeyDown(KEY.W) then
        self.cameraNode:Translate(Vector3.UP * MOVE_SPEED * timeStep)
    end
    if input:GetKeyDown(KEY.S) then
        self.cameraNode:Translate(Vector3.DOWN * MOVE_SPEED * timeStep)
    end
    if input:GetKeyDown(KEY.A) then
        self.cameraNode:Translate(Vector3.LEFT * MOVE_SPEED * timeStep)
    end
    if input:GetKeyDown(KEY.D) then
        self.cameraNode:Translate(Vector3.RIGHT * MOVE_SPEED * timeStep)
    end

    if input:GetKeyDown(KEY.PAGEUP) then
        self.camera:SetZoom(self.camera:GetZoom() * 1.01)
    end
    if input:GetKeyDown(KEY.PAGEDOWN) then
        self.camera:SetZoom(self.camera:GetZoom() * 0.99)
    end
end

function app:Update(timeStep)
    self:MoveCamera(timeStep)

    -- Draw joint debug geometry each frame
    local physicsWorld = self.scene:GetComponent("PhysicsWorld2D")
    if physicsWorld then
        physicsWorld:DrawDebugGeometry()
    end
end

app:Run()

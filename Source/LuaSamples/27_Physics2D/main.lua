-- LuaSamples/27_Physics2D/main.lua
-- Lua port of Source/Samples/27_Physics2D: 100 dynamic 2D rigid bodies
-- (alternating boxes and balls) raining onto a static ground box through
-- Box2D physics, viewed with an orthographic camera.

local NUM_OBJECTS = 100
local PIXEL_SIZE = 0.01 -- world units per pixel

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

    -- Orthographic camera; zoom scaled by resolution for full visibility
    -- (initial zoom 1.2 targets 1280x800)
    local cameraNode = scene:CreateChild("Camera")
    cameraNode:SetPosition(Vector3(0.0, 0.0, -10.0))
    self.cameraNode = cameraNode

    local graphics = GetSubsystem("Graphics")
    local camera = cameraNode:CreateComponent("Camera")
    camera:SetOrthographic(true)
    camera:SetOrthoSize(graphics:GetHeight() * PIXEL_SIZE)
    camera:SetZoom(1.2 * math.min(graphics:GetWidth() / 1280.0, graphics:GetHeight() / 800.0))
    self.camera = camera

    -- 2D physics world
    scene:CreateComponent("PhysicsWorld2D")

    local cache = GetSubsystem("ResourceCache")
    local boxSprite = cache:GetResource("Sprite2D", "Urho2D/Box.png")
    local ballSprite = cache:GetResource("Sprite2D", "Urho2D/Ball.png")

    -- Ground (static body)
    local groundNode = scene:CreateChild("Ground")
    groundNode:SetPosition(Vector3(0.0, -3.0, 0.0))
    groundNode:SetScale(Vector3(200.0, 1.0, 0.0))

    groundNode:CreateComponent("RigidBody2D") -- defaults to static

    local groundSprite = groundNode:CreateComponent("StaticSprite2D")
    groundSprite:SetSprite(boxSprite)

    local groundShape = groundNode:CreateComponent("CollisionBox2D")
    groundShape:SetSize(Vector2(0.32, 0.32))
    groundShape:SetFriction(0.5)

    -- Raining bodies
    for i = 0, NUM_OBJECTS - 1 do
        local node = scene:CreateChild("RigidBody")
        node:SetPosition(Vector3(Random(-0.1, 0.1), 5.0 + i * 0.4, 0.0))

        local body = node:CreateComponent("RigidBody2D")
        body:SetBodyType(BT2D.DYNAMIC)

        local staticSprite = node:CreateComponent("StaticSprite2D")

        if i % 2 == 0 then
            staticSprite:SetSprite(boxSprite)

            local box = node:CreateComponent("CollisionBox2D")
            box:SetSize(Vector2(0.32, 0.32))
            box:SetDensity(1.0)
            box:SetFriction(0.5)
            box:SetRestitution(0.1)
        else
            staticSprite:SetSprite(ballSprite)

            local circle = node:CreateComponent("CollisionCircle2D")
            circle:SetRadius(0.16)
            circle:SetDensity(1.0)
            circle:SetFriction(0.5)
            circle:SetRestitution(0.1)
        end
    end

    SetViewport(0, scene, camera)
end

function app:CreateInstructions()
    local cache = GetSubsystem("ResourceCache")

    local instructionText = GetUIRoot():CreateChild("Text")
    instructionText:SetText("Use WASD keys to move, use PageUp PageDown keys to zoom.")
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
end

app:Run()

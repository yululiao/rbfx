-- LuaSamples/24_Urho2DSprite/main.lua
-- Lua port of Source/Samples/24_Urho2DSprite: 200 randomly moving, rotating
-- and colored static 2D sprites plus one animated Spriter sprite, viewed with
-- an orthographic camera. WASD pans, PageUp/PageDown zooms.

local NUM_SPRITES = 200
local PIXEL_SIZE = 0.01 -- world units per pixel

local app = Sample:new()

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.FREE)
    input:SetMouseVisible(true)

    self.spriteNodes = {}
    self:CreateScene()
    self:CreateInstructions()
    -- 2D sample: no mouse-look, keep base Update only for our own logic
end

function app:CreateScene()
    local scene = CreateScene()
    self.scene = scene
    scene:CreateComponent("Octree")

    -- Orthographic camera
    local cameraNode = scene:CreateChild("Camera")
    cameraNode:SetPosition(Vector3(0.0, 0.0, -10.0))
    self.cameraNode = cameraNode

    local graphics = GetSubsystem("Graphics")
    local camera = cameraNode:CreateComponent("Camera")
    camera:SetOrthographic(true)
    camera:SetOrthoSize(graphics:GetHeight() * PIXEL_SIZE)
    self.camera = camera

    local cache = GetSubsystem("ResourceCache")

    -- Moving static sprites
    local sprite = cache:GetResource("Sprite2D", "Urho2D/Aster.png")
    if not sprite then
        return
    end

    local halfWidth = graphics:GetWidth() * 0.5 * PIXEL_SIZE
    local halfHeight = graphics:GetHeight() * 0.5 * PIXEL_SIZE

    for i = 1, NUM_SPRITES do
        local spriteNode = scene:CreateChild("StaticSprite2D")
        spriteNode:SetPosition(Vector3(Random(-halfWidth, halfWidth), Random(-halfHeight, halfHeight), 0.0))

        local staticSprite = spriteNode:CreateComponent("StaticSprite2D")
        staticSprite:SetColor(Color(Random(1.0), Random(1.0), Random(1.0), 1.0))
        staticSprite:SetBlendMode(BLEND.ALPHA)
        staticSprite:SetSprite(sprite)

        -- Store per-node speeds; GetVar converts variants back to Lua values
        spriteNode:SetVar("MoveSpeed", Vector3(Random(-2.0, 2.0), Random(-2.0, 2.0), 0.0))
        spriteNode:SetVar("RotateSpeed", Random(-90.0, 90.0))

        table.insert(self.spriteNodes, spriteNode)
    end

    -- Animated Spriter sprite on top
    local animationSet = cache:GetResource("AnimationSet2D", "Urho2D/GoldIcon.scml")
    if not animationSet then
        return
    end

    local spriteNode = scene:CreateChild("AnimatedSprite2D")
    spriteNode:SetPosition(Vector3(0.0, 0.0, -1.0))
    local animatedSprite = spriteNode:CreateComponent("AnimatedSprite2D")
    animatedSprite:SetAnimationSet(animationSet)
    animatedSprite:SetAnimation("idle")

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
    -- Do not move if the UI has a focused element
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

    local graphics = GetSubsystem("Graphics")
    local halfWidth = graphics:GetWidth() * 0.5 * PIXEL_SIZE
    local halfHeight = graphics:GetHeight() * 0.5 * PIXEL_SIZE

    for _, node in ipairs(self.spriteNodes) do
        local position = node:GetPosition()
        local moveSpeed = node:GetVar("MoveSpeed")

        -- Bounce off the screen edges
        local newPosition = position + moveSpeed * timeStep
        if newPosition.x < -halfWidth or newPosition.x > halfWidth then
            newPosition.x = position.x
            moveSpeed.x = -moveSpeed.x
            node:SetVar("MoveSpeed", moveSpeed)
        end
        if newPosition.y < -halfHeight or newPosition.y > halfHeight then
            newPosition.y = position.y
            moveSpeed.y = -moveSpeed.y
            node:SetVar("MoveSpeed", moveSpeed)
        end

        node:SetPosition(newPosition)

        local rotateSpeed = node:GetVar("RotateSpeed")
        node:Roll(rotateSpeed * timeStep)
    end
end

app:Run()

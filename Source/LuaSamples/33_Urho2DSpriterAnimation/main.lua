-- LuaSamples/33_Urho2DSpriterAnimation/main.lua
-- Lua port of Source/Samples/33_Urho2DSpriterAnimation: Spriter-implemented
-- 2D animated character. Mouse click cycles animations, WASD pans the
-- orthographic camera, PageUp/PageDown zooms.

local PIXEL_SIZE = 0.01

local app = Sample:new()
app.animationIndex = 0

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.FREE)
    input:SetMouseVisible(true)

    self:CreateScene()
    self:CreateInstructions()

    SubscribeToEvent("MouseButtonDown", function(data)
        self:HandleMouseButtonDown()
    end)
end

function app:CreateScene()
    local scene = CreateScene()
    self.scene = scene
    scene:CreateComponent("Octree")

    -- Create camera node
    self.cameraNode = scene:CreateChild("Camera")
    self.cameraNode:SetPosition(Vector3(0.0, 0.0, -10.0))

    local camera = self.cameraNode:CreateComponent("Camera")
    camera:SetOrthographic(true)

    local graphics = GetSubsystem("Graphics")
    camera:SetOrthoSize(graphics:GetHeight() * PIXEL_SIZE)
    -- Initial zoom (1.5) targets full visibility at 1280x800
    camera:SetZoom(1.5 * math.min(graphics:GetWidth() / 1280.0, graphics:GetHeight() / 800.0))

    local spriterAnimationSet = GetResource("AnimationSet2D", "Urho2D/imp/imp.scml")
    if not spriterAnimationSet then
        return
    end

    self.spriterNode = scene:CreateChild("SpriterAnimation")
    local sprite = self.spriterNode:CreateComponent("AnimatedSprite2D")
    sprite:SetAnimationSet(spriterAnimationSet)
    sprite:SetAnimation(spriterAnimationSet:GetAnimation(self.animationIndex))
end

function app:CreateInstructions()
    local ui = GetSubsystem("UI")
    local root = ui:GetRoot()

    local instructionText = root:CreateChild("Text")
    instructionText:SetText("Mouse click to play next animation, \nUse WASD keys to move, use PageUp PageDown keys to zoom.")
    instructionText:SetFont("Fonts/Anonymous Pro.ttf", 15)
    instructionText:SetTextAlignment(HA.CENTER)

    -- Position the text relative to the screen center
    instructionText:SetHorizontalAlignment(HA.CENTER)
    instructionText:SetVerticalAlignment(VA.CENTER)
    instructionText:SetPosition(0, root:GetHeight() / 4)
end

function app:Update(timeStep)
    -- Do not move if the UI has a focused element (the console)
    if GetSubsystem("UI"):GetFocusElement() then
        return
    end

    local input = GetSubsystem("Input")

    -- Movement speed as world units per second
    local MOVE_SPEED = 4.0

    if input:GetKeyDown(KEY.W) then
        self.cameraNode:Translate(Vector3(0.0, 1.0, 0.0) * MOVE_SPEED * timeStep)
    end
    if input:GetKeyDown(KEY.S) then
        self.cameraNode:Translate(Vector3(0.0, -1.0, 0.0) * MOVE_SPEED * timeStep)
    end
    if input:GetKeyDown(KEY.A) then
        self.cameraNode:Translate(Vector3(-1.0, 0.0, 0.0) * MOVE_SPEED * timeStep)
    end
    if input:GetKeyDown(KEY.D) then
        self.cameraNode:Translate(Vector3(1.0, 0.0, 0.0) * MOVE_SPEED * timeStep)
    end

    local camera = self.cameraNode:GetComponent("Camera")
    if input:GetKeyDown(KEY.PAGEUP) then
        camera:SetZoom(camera:GetZoom() * 1.01)
    end
    if input:GetKeyDown(KEY.PAGEDOWN) then
        camera:SetZoom(camera:GetZoom() * 0.99)
    end
end

function app:HandleMouseButtonDown()
    local sprite = self.spriterNode:GetComponent("AnimatedSprite2D")
    local animationSet = sprite:GetAnimationSet()
    self.animationIndex = (self.animationIndex + 1) % animationSet:GetNumAnimations()
    sprite:SetAnimation(animationSet:GetAnimation(self.animationIndex), true)
end

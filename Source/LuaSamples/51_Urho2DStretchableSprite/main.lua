-- LuaSamples/51_Urho2DStretchableSprite/main.lua
-- Lua port of Source/Samples/51_Urho2DStretchableSprite: a regular
-- StaticSprite2D next to a 9-slice StretchableSprite2D. WASD keys transform
-- both; TAB cycles between Scale, Rotate and Translate modes (CTRL+A/D
-- rotates about Z in Rotate mode).

local PIXEL_SIZE = 0.01

local app = Sample:new()
app.selectTransform = 0 -- 0: scale, 1: rotate, 2: translate

function app:OnStart()
    self:CreateScene()
    self:CreateInstructions()
    self:SubscribeToEvents()

    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.FREE)
    input:SetMouseVisible(true)
end

function app:CreateScene()
    self.scene = CreateScene()
    local scene = self.scene
    scene:CreateComponent("Octree")

    -- Orthographic camera
    self.cameraNode = scene:CreateChild("Camera")
    self.cameraNode:SetPosition(Vector3(0.0, 0.0, -10.0))
    local camera = self.cameraNode:CreateComponent("Camera")
    camera:SetOrthographic(true)

    local graphics = GetSubsystem("Graphics")
    camera:SetOrthoSize(graphics:GetHeight() * PIXEL_SIZE)

    self.refSpriteNode = scene:CreateChild("regular sprite")
    self.stretchSpriteNode = scene:CreateChild("stretchable sprite")

    local sprite = GetResource("Sprite2D", "Urho2D/Stretchable.png")
    if sprite then
        self.refSpriteNode:CreateComponent("StaticSprite2D"):SetSprite(sprite)

        local stretchSprite = self.stretchSpriteNode:CreateComponent("StretchableSprite2D")
        stretchSprite:SetSprite(sprite)
        stretchSprite:SetBorder(IntRect(25, 25, 25, 25))

        self.refSpriteNode:Translate2D(Vector2(-2.0, 0.0))
        self.stretchSpriteNode:Translate2D(Vector2(2.0, 0.0))
    end

    SetViewport(0, scene, camera)
end

function app:CreateInstructions()
    local root = GetUIRoot()
    local instructionText = root:CreateChild("Text")
    instructionText:SetText(
        "Use WASD keys to transform, Tab key to cycle through\n"
        .. "Scale, Rotate, and Translate transform modes. In Rotate\n"
        .. "mode, combine A/D keys with Ctrl key to rotate about\n"
        .. "the Z axis")
    instructionText:SetFont(GetResource("Font", "Fonts/Anonymous Pro.ttf"), 12)

    -- Position the text relative to the screen center
    instructionText:SetHorizontalAlignment(HA.CENTER)
    instructionText:SetVerticalAlignment(VA.CENTER)
    instructionText:SetPosition(0, root:GetHeight() / 4)
end

function app:SubscribeToEvents()
    local input = GetSubsystem("Input")
    SubscribeToEvent(input, "KeyUp", function(data) self:OnKeyUp(data) end)
end

function app:OnKeyUp(data)
    if data.Key == KEY.TAB then
        self.selectTransform = (self.selectTransform + 1) % 3
    end
end

function app:Update(timeStep)
    if self.selectTransform == 0 then
        self:ScaleSprites(timeStep)
    elseif self.selectTransform == 1 then
        self:RotateSprites(timeStep)
    else
        self:TranslateSprites(timeStep)
    end
end

function app:TranslateSprites(timeStep)
    local speed = 1.0
    local input = GetSubsystem("Input")
    local left = input:GetKeyDown(KEY.A)
    local right = input:GetKeyDown(KEY.D)
    local up = input:GetKeyDown(KEY.W)
    local down = input:GetKeyDown(KEY.S)

    if left or right or up or down then
        local quantum = timeStep * speed
        local dx = (left and -quantum or 0.0) + (right and quantum or 0.0)
        local dy = (down and -quantum or 0.0) + (up and quantum or 0.0)
        local translate = Vector2(dx, dy)

        self.refSpriteNode:Translate2D(translate)
        self.stretchSpriteNode:Translate2D(translate)
    end
end

function app:RotateSprites(timeStep)
    local speed = 45.0
    local input = GetSubsystem("Input")
    local left = input:GetKeyDown(KEY.A)
    local right = input:GetKeyDown(KEY.D)
    local up = input:GetKeyDown(KEY.W)
    local down = input:GetKeyDown(KEY.S)
    local ctrl = input:GetKeyDown(KEY.CTRL)

    if left or right or up or down then
        local quantum = timeStep * speed

        local xrot = (up and -quantum or 0.0) + (down and quantum or 0.0)
        local rot2 = (left and -quantum or 0.0) + (right and quantum or 0.0)
        local totalRot = Quaternion(xrot, ctrl and 0.0 or rot2, ctrl and rot2 or 0.0)

        self.refSpriteNode:Rotate(totalRot)
        self.stretchSpriteNode:Rotate(totalRot)
    end
end

function app:ScaleSprites(timeStep)
    local speed = 0.5
    local input = GetSubsystem("Input")
    local left = input:GetKeyDown(KEY.A)
    local right = input:GetKeyDown(KEY.D)
    local up = input:GetKeyDown(KEY.W)
    local down = input:GetKeyDown(KEY.S)

    if left or right or up or down then
        local quantum = timeStep * speed
        local sx = 1.0 + (right and quantum or left and -quantum or 0.0)
        local sy = 1.0 + (up and quantum or down and -quantum or 0.0)
        local scale = Vector2(sx, sy)

        self.refSpriteNode:Scale2D(scale)
        self.stretchSpriteNode:Scale2D(scale)
    end
end

app:Run()

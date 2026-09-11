-- LuaSamples/03_Sprites/main.lua
-- Lua port of Source/Samples/03_Sprites: 100 additive-blended sprites
-- moving and rotating inside the UI layer.

local NUM_SPRITES = 100

local app = Sample:new()

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.FREE)
    input:SetMouseVisible(true)

    self.sprites = {}
    self:CreateSprites()
end

function app:CreateSprites()
    local graphics = GetSubsystem("Graphics")
    local ui = GetSubsystem("UI")

    -- Get rendering window size as floats
    local width = graphics:GetWidth() + 0.0
    local height = graphics:GetHeight() + 0.0

    -- Get the Urho3D fish texture
    local decalTex = GetResource("Texture2D", "Textures/UrhoDecal.dds")

    for i = 1, NUM_SPRITES do
        -- Create a new sprite, set it to use the texture
        local sprite = ui:GetRoot():CreateChild("Sprite")

        -- The UI root element is as big as the rendering window, set random position within it
        sprite:SetTexture(decalTex)
        sprite:SetPosition(IntVector2(Random() * width, Random() * height))

        -- Set sprite size & hotspot in its center
        sprite:SetSize(IntVector2(128, 128))
        sprite:SetHotSpot(IntVector2(64, 64))

        -- Set random rotation in degrees and random scale
        sprite:SetRotation(Random() * 360.0)
        sprite:SetScale(Random(1.0) + 0.5)

        -- Set random color and additive blending mode
        sprite:SetColor(Color(Random(0.5) + 0.5, Random(0.5) + 0.5, Random(0.5) + 0.5))
        sprite:SetBlendMode(BLEND.ADD)

        -- Store sprite's velocity as a custom variable
        sprite:SetVar("Velocity", Vector2(Random(200.0) - 100.0, Random(200.0) - 100.0))

        -- Store sprites to our own container for easy movement update iteration
        table.insert(self.sprites, sprite)
    end
end

function app:Update(timeStep)
    local graphics = GetSubsystem("Graphics")
    local width = graphics:GetWidth() + 0.0
    local height = graphics:GetHeight() + 0.0

    -- Go through all sprites
    for _, sprite in ipairs(self.sprites) do
        -- Rotate
        sprite:SetRotation(sprite:GetRotation() + timeStep * 30.0)

        -- Move, wrap around rendering window edges
        local velocity = sprite:GetVar("Velocity")
        local pos = sprite:GetPosition()
        local newPos = Vector2(pos.x, pos.y) + velocity * timeStep
        if newPos.x < 0.0 then
            newPos.x = newPos.x + width
        end
        if newPos.x >= width then
            newPos.x = newPos.x - width
        end
        if newPos.y < 0.0 then
            newPos.y = newPos.y + height
        end
        if newPos.y >= height then
            newPos.y = newPos.y - height
        end
        sprite:SetPosition(IntVector2(newPos.x, newPos.y))
    end
end

app:Run()

-- LuaSamples/25_Urho2DParticle/main.lua
-- Lua port of Source/Samples/25_Urho2DParticle: two 2D particle effects
-- (sun and green spiral); the sun emitter follows the mouse position through
-- an orthographic camera ScreenToWorldPoint conversion.

local PIXEL_SIZE = 0.01 -- world units per pixel

local app = Sample:new()

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.FREE)
    input:SetMouseVisible(true)

    self:CreateScene()
    self:CreateInstructions()

    SubscribeToEvent("MouseMove", function(data) self:HandleMouseMove(data) end)
end

function app:CreateScene()
    local scene = CreateScene()
    self.scene = scene
    scene:CreateComponent("Octree")

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

    local cache = GetSubsystem("ResourceCache")

    -- Sun emitter (follows the mouse)
    local particleEffect = cache:GetResource("ParticleEffect2D", "Urho2D/sun.pex")
    if not particleEffect then
        return
    end

    self.particleNode = scene:CreateChild("ParticleEmitter2D")
    local particleEmitter = self.particleNode:CreateComponent("ParticleEmitter2D")
    particleEmitter:SetEffect(particleEffect)

    -- Green spiral emitter
    local greenSpiralEffect = cache:GetResource("ParticleEffect2D", "Urho2D/greenspiral.pex")
    if not greenSpiralEffect then
        return
    end

    local greenSpiralNode = scene:CreateChild("GreenSpiral")
    local greenSpiralEmitter = greenSpiralNode:CreateComponent("ParticleEmitter2D")
    greenSpiralEmitter:SetEffect(greenSpiralEffect)

    SetViewport(0, scene, camera)
end

function app:CreateInstructions()
    local cache = GetSubsystem("ResourceCache")

    local instructionText = GetUIRoot():CreateChild("Text")
    instructionText:SetText("Use mouse/touch to move the particle.")
    instructionText:SetFont(cache:GetResource("Font", "Fonts/Anonymous Pro.ttf"), 15)
    instructionText:SetHorizontalAlignment(HA.CENTER)
    instructionText:SetVerticalAlignment(VA.CENTER)
    instructionText:SetPosition(0, GetUIRoot():GetHeight() / 4)
end

function app:HandleMouseMove(data)
    if not self.particleNode then
        return
    end
    local graphics = GetSubsystem("Graphics")
    local world = self.camera:ScreenToWorldPoint(Vector3(
        data.X / graphics:GetWidth(), data.Y / graphics:GetHeight(), 10.0))
    self.particleNode:SetPosition(world)
end

app:Run()

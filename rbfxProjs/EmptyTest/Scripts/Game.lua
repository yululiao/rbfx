-- Scripts/Game.lua
-- Minimal test scene for the editor Game view.
--
-- Uses GetGameScene() to reuse the editor-provided scene (which already
-- has Octree + DebugRenderer). Falls back to CreateScene() in standalone player.

local Game = {}
Game.__index = Game

----------------------------------------------------------------------
-- Game module
----------------------------------------------------------------------
local scene
local cameraNode

function Game:Start()
    self:CreateScene()
    self:CreateInstructions("EmptyTest - Game view is working!\nUse WASD + mouse to move the camera")

    SubscribeToEvent("Update", function(data)
        -- nothing to update yet
    end)
end

----------------------------------------------------------------------
-- Scene setup
----------------------------------------------------------------------
function Game:CreateScene()
    -- Use the editor-provided game scene when available (Play mode),
    -- otherwise create our own scene (standalone player).
    scene = GetGameScene()
    local editorProvidedScene = (scene ~= nil)
    if not editorProvidedScene then
        scene = CreateScene()
    end

    -- Octree and DebugRenderer are already present in the editor-provided scene.
    if not editorProvidedScene then
        scene:CreateComponent("Octree")
        scene:CreateComponent("DebugRenderer")
    end

    -- Static ground plane.
    local planeNode = scene:CreateChild("Plane")
    planeNode:SetScale(Vector3(50.0, 1.0, 50.0))
    local planeObject = planeNode:CreateComponent("StaticModel")
    planeObject:SetModel(GetResource("Model", "Models/Plane.mdl"))
    planeObject:SetMaterial(GetResource("Material", "Materials/StoneTiled.xml"))

    -- Zone: ambient lighting and fog.
    local zoneNode = scene:CreateChild("Zone")
    local zone = zoneNode:CreateComponent("Zone")
    zone:SetBoundingBox(BoundingBox(-1000.0, 1000.0))
    zone:SetAmbientColor(Color(0.5, 0.5, 0.5))
    zone:SetFogColor(Color(0.4, 0.5, 0.8))
    zone:SetFogStart(100.0)
    zone:SetFogEnd(300.0)

    -- Directional light.
    local lightNode = scene:CreateChild("DirectionalLight")
    lightNode:SetDirection(Vector3(0.6, -1.0, 0.8))
    local light = lightNode:CreateComponent("Light")
    light:SetLightType(LIGHT.DIRECTIONAL)
    light:SetCastShadows(true)
    light:SetColor(Color(0.5, 0.5, 0.5))
    light:SetShadowBias(0.00025, 0.5)
    light:SetShadowCascade(10.0, 50.0, 200.0, 0.0, 0.8)

    -- Camera with FreeFlyController (mouse look + WASD movement).
    cameraNode = scene:CreateChild("Camera")
    cameraNode:CreateComponent("FreeFlyController")
    local camera = cameraNode:CreateComponent("Camera")
    camera:SetFarClip(300.0)
    cameraNode:SetPosition(Vector3(0.0, 5.0, -10.0))

    SetViewport(0, scene, camera)
end

----------------------------------------------------------------------
-- UI overlay
----------------------------------------------------------------------
function Game:CreateInstructions(text)
    local root = GetUIRoot()
    if not root then return end

    local instructions = root:CreateChild("Text", "Instructions")
    instructions:SetFont("Fonts/Anonymous Pro.ttf", 15)
    instructions:SetText(text)
    instructions:SetTextAlignment(HA.LEFT)
    instructions:SetPosition(10, 10)
    instructions:SetWidth(root:GetWidth() - 20)
    instructions:SetColor(Color(0.0, 1.0, 0.0))
    return instructions
end

return Game

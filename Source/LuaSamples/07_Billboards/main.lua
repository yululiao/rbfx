-- LuaSamples/07_Billboards/main.lua
-- Lua port of Source/Samples/07_Billboards: animated billboard sets (smoke)
-- lit by rotating colored spotlights.

local app = Sample:new()
app.drawDebug = false

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.RELATIVE)
    input:SetMouseVisible(false)

    self:CreateScene()
    self:CreateInstructions("Use WASD keys and mouse/touch to move\nSpace to toggle debug geometry")

    -- Request debug geometry rendering during post-render update
    SubscribeToEvent("PostRenderUpdate", function(data)
        if self.drawDebug then
            GetSubsystem("Renderer"):DrawDebugGeometry(true)
        end
    end)
end

function app:CreateScene()
    local scene = CreateScene()
    self.scene = scene

    -- Create octree + DebugRenderer
    scene:CreateComponent("Octree")
    scene:CreateComponent("DebugRenderer")

    -- Create a Zone component for ambient lighting & fog control
    local zoneNode = scene:CreateChild("Zone")
    local zone = zoneNode:CreateComponent("Zone")
    zone:SetBoundingBox(BoundingBox(-1000.0, 1000.0))
    zone:SetAmbientColor(Color(0.1, 0.1, 0.1))
    zone:SetFogStart(100.0)
    zone:SetFogEnd(300.0)

    -- Create a directional light without shadows
    local lightNode = scene:CreateChild("DirectionalLight")
    lightNode:SetDirection(Vector3(0.5, -1.0, 0.5))
    local light = lightNode:CreateComponent("Light")
    light:SetLightType(LIGHT.DIRECTIONAL)
    light:SetColor(Color(0.2, 0.2, 0.2))
    light:SetSpecularIntensity(1.0)

    -- Create a "floor" consisting of several tiles
    for y = -5, 5 do
        for x = -5, 5 do
            local floorNode = scene:CreateChild("FloorTile")
            floorNode:SetPosition(Vector3(x * 20.5, -0.5, y * 20.5))
            floorNode:SetScale(Vector3(20.0, 1.0, 20.0))
            local floorObject = floorNode:CreateComponent("StaticModel")
            floorObject:SetModel(GetResource("Model", "Models/Box.mdl"))
            floorObject:SetMaterial(GetResource("Material", "Materials/Stone.xml"))
        end
    end

    -- Create groups of mushrooms, which act as shadow casters
    local NUM_MUSHROOMGROUPS = 25
    local NUM_MUSHROOMS = 25

    for i = 1, NUM_MUSHROOMGROUPS do
        -- First create a scene node for the group. The individual mushrooms
        -- nodes will be created as children
        local groupNode = scene:CreateChild("MushroomGroup")
        groupNode:SetPosition(Vector3(Random(190.0) - 95.0, 0.0, Random(190.0) - 95.0))

        for j = 1, NUM_MUSHROOMS do
            local mushroomNode = groupNode:CreateChild("Mushroom")
            mushroomNode:SetPosition(Vector3(Random(25.0) - 12.5, 0.0, Random(25.0) - 12.5))
            mushroomNode:SetRotation(Quaternion(0.0, Random() * 360.0, 0.0))
            mushroomNode:SetScale(1.0 + Random() * 4.0)
            local mushroomObject = mushroomNode:CreateComponent("StaticModel")
            mushroomObject:SetModel(GetResource("Model", "Models/Mushroom.mdl"))
            mushroomObject:SetMaterial(GetResource("Material", "Materials/Mushroom.xml"))
            mushroomObject:SetCastShadows(true)
        end
    end

    -- Create billboard sets (floating smoke)
    local NUM_BILLBOARDNODES = 25
    local NUM_BILLBOARDS = 10

    for i = 1, NUM_BILLBOARDNODES do
        local smokeNode = scene:CreateChild("Smoke")
        smokeNode:SetPosition(Vector3(Random(200.0) - 100.0, Random(20.0) + 10.0, Random(200.0) - 100.0))

        local billboardObject = smokeNode:CreateComponent("BillboardSet")
        billboardObject:SetNumBillboards(NUM_BILLBOARDS)
        billboardObject:SetMaterial(GetResource("Material", "Materials/LitSmoke.xml"))
        billboardObject:SetSorted(true)

        for j = 0, NUM_BILLBOARDS - 1 do
            local bb = billboardObject:GetBillboard(j)
            bb.position = Vector3(Random(12.0) - 6.0, Random(8.0) - 4.0, Random(12.0) - 6.0)
            bb.size = Vector2(Random(2.0) + 3.0, Random(2.0) + 3.0)
            bb.rotation = Random() * 360.0
            bb.enabled = true
        end

        -- After modifying the billboards, they need to be "committed" so that
        -- the BillboardSet updates its internals
        billboardObject:Commit()
    end

    -- Create shadow casting spotlights
    local NUM_LIGHTS = 9
    for i = 0, NUM_LIGHTS - 1 do
        local lightNode = scene:CreateChild("SpotLight")
        local light = lightNode:CreateComponent("Light")

        local angle = 0.0
        local position = Vector3((i % 3) * 60.0 - 60.0, 45.0, math.floor(i / 3) * 60.0 - 60.0)
        local color = Color(((i + 1) % 2) * 0.5 + 0.5, (math.floor((i + 1) / 2) % 2) * 0.5 + 0.5,
            (math.floor((i + 1) / 4) % 2) * 0.5 + 0.5)

        lightNode:SetPosition(position)
        lightNode:SetDirection(Vector3(math.sin(angle), -1.5, math.cos(angle)))

        light:SetLightType(LIGHT.SPOT)
        light:SetRange(90.0)
        light:SetRampTexture(GetResource("Texture2D", "Textures/RampExtreme.png"))
        light:SetFov(45.0)
        light:SetColor(color)
        light:SetSpecularIntensity(1.0)
        light:SetCastShadows(true)
        light:SetShadowBias(0.00002, 0.0)

        -- Configure shadow fading: when they are far away enough, the lights
        -- eventually become unshadowed for better GPU performance
        light:SetShadowFadeDistance(100.0) -- Fade start distance
        light:SetShadowDistance(125.0)     -- Fade end distance, shadows are disabled
        -- Set half resolution for the shadow maps for increased performance
        light:SetShadowResolution(0.5)
        -- The spot lights will not have anything near them, so move the near
        -- plane of the shadow camera farther for better shadow depth resolution
        light:SetShadowNearFarRatio(0.01)
    end

    -- Create the camera. Limit far clip distance to match the fog
    local cameraNode = scene:CreateChild("Camera")
    cameraNode:CreateComponent("FreeFlyController")
    local camera = cameraNode:CreateComponent("Camera")
    camera:SetFarClip(300.0)

    -- Set an initial position for the camera scene node above the plane
    cameraNode:SetPosition(Vector3(0.0, 5.0, 0.0))

    self.cameraNode = cameraNode
    SetViewport(0, scene, camera)
end

function app:Update(timeStep)
    self:AnimateScene(timeStep)
end

function app:AnimateScene(timeStep)
    -- Get the light and billboard scene nodes
    local lightNodes = self.scene:GetChildrenWithComponent("Light")
    local billboardNodes = self.scene:GetChildrenWithComponent("BillboardSet")

    local LIGHT_ROTATION_SPEED = 20.0
    local BILLBOARD_ROTATION_SPEED = 50.0

    -- Rotate the lights around the world Y-axis
    for _, lightNode in ipairs(lightNodes) do
        lightNode:Rotate(Quaternion(0.0, LIGHT_ROTATION_SPEED * timeStep, 0.0), TS.WORLD)
    end

    -- Rotate the individual billboards within the billboard sets, then
    -- recommit to make the changes visible
    for _, billboardNode in ipairs(billboardNodes) do
        local billboardObject = billboardNode:GetComponent("BillboardSet")
        for j = 0, billboardObject:GetNumBillboards() - 1 do
            local bb = billboardObject:GetBillboard(j)
            bb.rotation = bb.rotation + BILLBOARD_ROTATION_SPEED * timeStep
        end
        billboardObject:Commit()
    end
end

-- Toggle debug geometry with space
function app:OnKeyDown(key)
    if key == KEY.SPACE then
        self.drawDebug = not self.drawDebug
    end
end

app:Run()

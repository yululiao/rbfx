-- LuaSamples/20_HugeObjectCount/main.lua
-- Lua port of Source/Samples/20_HugeObjectCount: renders 250 x 250 (62500)
-- boxes either as individual StaticModels or batched into StaticModelGroups,
-- highlighting the CPU/GPU tradeoff between accurate culling and batching.
-- Space toggles a roll animation, G rebuilds the scene with the other mode.

local app = Sample:new()
app.useGroups = false
app.animate = false
app.boxNodes = {}

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.RELATIVE)
    input:SetMouseVisible(false)

    self:CreateScene()
    self:CreateInstructions()
    self:SetupViewport()
    self:SubscribeToEvents()
end

function app:CreateScene()
    local cache = GetSubsystem("ResourceCache")
    local scene = self.scene
    if scene then
        scene:Clear()
    else
        scene = CreateScene()
        self.scene = scene
    end

    scene:CreateComponent("Octree")

    -- Zone for ambient light & fog control
    local zoneNode = scene:CreateChild("Zone")
    local zone = zoneNode:CreateComponent("Zone")
    zone:SetBoundingBox(BoundingBox(-1000.0, 1000.0))
    zone:SetFogColor(Color(0.2, 0.2, 0.2))
    zone:SetFogStart(200.0)
    zone:SetFogEnd(300.0)

    -- Directional light
    local lightNode = scene:CreateChild("DirectionalLight")
    lightNode:SetDirection(Vector3(-0.6, -1.0, -0.8))
    local light = lightNode:CreateComponent("Light")
    light:SetLightType(LIGHT.DIRECTIONAL)

    local boxModel = cache:GetResource("Model", "Models/Box.mdl")

    self.boxNodes = {}
    if not self.useGroups then
        light:SetColor(Color(0.7, 0.35, 0.0))

        -- Individual box StaticModels
        for y = -124, 124 do
            for x = -124, 124 do
                local boxNode = scene:CreateChild("Box")
                boxNode:SetPosition(Vector3(x * 0.3, 0.0, y * 0.3))
                boxNode:SetScale(0.25)
                local boxObject = boxNode:CreateComponent("StaticModel")
                boxObject:SetModel(boxModel)
                table.insert(self.boxNodes, boxNode)
            end
        end
    else
        light:SetColor(Color(0.6, 0.6, 0.6))
        light:SetSpecularIntensity(1.5)

        -- Batch boxes into StaticModelGroups of 25 x 25 instances. The
        -- group's own transform does not matter; only instance nodes count.
        local lastGroup = nil
        for y = -124, 124 do
            for x = -124, 124 do
                if not lastGroup or lastGroup:GetNumInstanceNodes() >= 25 * 25 then
                    local boxGroupNode = scene:CreateChild("BoxGroup")
                    lastGroup = boxGroupNode:CreateComponent("StaticModelGroup")
                    lastGroup:SetModel(boxModel)
                end

                local boxNode = scene:CreateChild("Box")
                boxNode:SetPosition(Vector3(x * 0.3, 0.0, y * 0.3))
                boxNode:SetScale(0.25)
                table.insert(self.boxNodes, boxNode)
                lastGroup:AddInstanceNode(boxNode)
            end
        end
    end

    -- Create the camera
    local cameraNode = scene:CreateChild("Camera")
    cameraNode:CreateComponent("FreeFlyController")
    cameraNode:SetPosition(Vector3(0.0, 10.0, -100.0))
    local camera = cameraNode:CreateComponent("Camera")
    camera:SetFarClip(300.0)
    self.cameraNode = cameraNode
    SetViewport(0, scene, camera)
end

function app:CreateInstructions()
    local cache = GetSubsystem("ResourceCache")

    local instructionText = GetUIRoot():CreateChild("Text")
    instructionText:SetText("Use WASD keys and mouse/touch to move\nSpace to toggle animation\nG to toggle object group optimization")
    instructionText:SetFont(cache:GetResource("Font", "Fonts/Anonymous Pro.ttf"), 15)
    instructionText:SetTextAlignment(HA.CENTER)
    instructionText:SetHorizontalAlignment(HA.CENTER)
    instructionText:SetVerticalAlignment(VA.CENTER)
    instructionText:SetPosition(0, GetUIRoot():GetHeight() / 4)
end

function app:SetupViewport()
    -- Viewport was already set inside CreateScene.
end

function app:SubscribeToEvents()
    SubscribeToEvent("Update", function(data)
        self:HandleUpdate(data.TimeStep)
    end)
end

function app:HandleUpdate(timeStep)
    local input = GetSubsystem("Input")

    if input:GetKeyPress(KEY.SPACE) then
        self.animate = not self.animate
    end
    if input:GetKeyPress(KEY.G) then
        self.useGroups = not self.useGroups
        self:CreateScene()
    end

    if self.animate then
        local rotateQuat = Quaternion(15.0 * timeStep, Vector3.FORWARD)
        for _, boxNode in ipairs(self.boxNodes) do
            boxNode:Rotate(rotateQuat)
        end
    end
end

app:Run()

-- LuaSamples/39_CrowdNavigation/main.lua
-- Lua port of Source/Samples/39_CrowdNavigation: crowd simulation with
-- dynamic navmesh, obstacles, off-mesh connections, moving agents and
-- navigation mesh streaming (Tab). LMB sets destination, Shift+LMB spawns
-- Jacks, MMB/O adds or removes mushrooms, Space toggles debug geometry.
--
-- Note: the C++ sample modifies the CrowdAgentFormation event's Position
-- parameter to scatter agents. Lua event data is read-only, so agents
-- converge on the exact target point instead (Detour's avoidance keeps
-- them spread out in practice).

local WALKING_ANI = "Models/Jack_Walk.ani"

local app = Sample:new()
app.yaw = 0.0
app.pitch = 80.0
app.drawDebug = false
app.useStreaming = false
app.streamingDistance = 2
app.tileData = {}   -- ["x,z"] = { tile = IntVector2, data = byte table }
app.addedTiles = {} -- ["x,z"] = IntVector2

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.ABSOLUTE)
    input:SetMouseVisible(false)

    self:CreateScene()
    self:CreateUI()
    SetViewport(0, self.scene, self.cameraNode:GetComponent("Camera"))

    SubscribeToEvent("PostRenderUpdate", function(data) self:HandlePostRenderUpdate() end)
    SubscribeToEvent("CrowdAgentFailure", function(data) self:HandleCrowdAgentFailure(data) end)
    SubscribeToEvent("CrowdAgentReposition", function(data) self:HandleCrowdAgentReposition(data) end)
end

function app:CreateScene()
    local scene = CreateScene()
    self.scene = scene

    -- Octree to the root, plus a DebugRenderer for debug geometry
    scene:CreateComponent("Octree")
    scene:CreateComponent("DebugRenderer")

    -- Static plane
    local planeNode = scene:CreateChild("Plane")
    planeNode:SetScale(Vector3(100.0, 1.0, 100.0))
    local planeObject = planeNode:CreateComponent("StaticModel")
    planeObject:SetModel(GetResource("Model", "Models/Plane.mdl"))
    planeObject:SetMaterial(GetResource("Material", "Materials/StoneTiled.xml"))

    -- Zone for ambient lighting & fog control
    local zoneNode = scene:CreateChild("Zone")
    local zone = zoneNode:CreateComponent("Zone")
    zone:SetBoundingBox(BoundingBox(-1000.0, 1000.0))
    zone:SetAmbientColor(Color(0.15, 0.15, 0.15))
    zone:SetFogColor(Color(0.5, 0.5, 0.7))
    zone:SetFogStart(100.0)
    zone:SetFogEnd(300.0)

    -- Directional light with cascaded shadows
    local lightNode = scene:CreateChild("DirectionalLight")
    lightNode:SetDirection(Vector3(0.6, -1.0, 0.8))
    local light = lightNode:CreateComponent("Light")
    light:SetLightType(LIGHT.DIRECTIONAL)
    light:SetCastShadows(true)
    light:SetShadowBias(0.00025, 0.5)
    -- Cascade splits at 10, 50, 200; fade shadows out at 80% of max distance
    light:SetShadowCascade(10.0, 50.0, 200.0, 0.0, 0.8)

    -- Randomly sized boxes; large enough ones become occluders
    local boxGroup = scene:CreateChild("Boxes")
    for i = 0, 19 do
        local boxNode = boxGroup:CreateChild("Box")
        local size = 1.0 + Random(10.0)
        boxNode:SetPosition(Vector3(Random(80.0) - 40.0, size * 0.5, Random(80.0) - 40.0))
        boxNode:SetScale(size)
        local boxObject = boxNode:CreateComponent("StaticModel")
        boxObject:SetModel(GetResource("Model", "Models/Box.mdl"))
        boxObject:SetMaterial(GetResource("Material", "Materials/Stone.xml"))
        boxObject:SetCastShadows(true)
        if size >= 3.0 then
            boxObject:SetOccluder(true)
        end
    end

    -- DynamicNavigationMesh with small tiles to show streaming
    local navMesh = scene:CreateComponent("DynamicNavigationMesh")
    navMesh:SetTileSize(30)
    navMesh:SetDrawObstacles(true)
    navMesh:SetDrawOffMeshConnections(true)
    -- Agent height large enough to exclude the layers under boxes
    navMesh:SetAgentHeight(10.0)
    navMesh:SetCellHeight(0.05)
    -- Navigable tags all scene geometry as navigation geometry
    scene:CreateComponent("Navigable")
    -- Y padding so objects can sit on top of the tallest boxes
    navMesh:SetPadding(Vector3(0.0, 10.0, 0.0))
    -- Build the navigation geometry (takes some time)
    navMesh:Rebuild()

    -- Off-mesh connections to make the boxes climbable
    self:CreateBoxOffMeshConnections(navMesh, boxGroup)

    -- Mushrooms as obstacles (non-walkable areas)
    for i = 0, 99 do
        self:CreateMushroom(Vector3(Random(90.0) - 45.0, 0.0, Random(90.0) - 45.0))
    end

    -- CrowdManager with "High (66)" obstacle avoidance params
    local crowdManager = scene:CreateComponent("CrowdManager")
    local params = crowdManager:GetObstacleAvoidanceParams(0)
    params.velBias = 0.5
    params.adaptiveDivs = 7
    params.adaptiveRings = 3
    params.adaptiveDepth = 3
    crowdManager:SetObstacleAvoidanceParams(0, params)

    -- Movable barrels as crowd agents
    self:CreateMovingBarrels(navMesh)

    -- Jack node as crowd agent
    self:SpawnJack(Vector3(-5.0, 0.0, 20.0), scene:CreateChild("Jacks"))

    -- Camera (far clip matches the fog)
    self.cameraNode = scene:CreateChild("Camera")
    local camera = self.cameraNode:CreateComponent("Camera")
    camera:SetFarClip(300.0)
    self.cameraNode:SetPosition(Vector3(0.0, 50.0, 0.0))
    self.cameraNode:SetRotation(Quaternion(self.pitch, self.yaw, 0.0))
end

function app:CreateUI()
    local ui = GetSubsystem("UI")
    local root = GetUIRoot()

    -- Cursor: hidden -> mouse controls camera, visible -> raycast target
    local style = GetResource("XMLFile", "UI/DefaultStyle.xml")
    local cursor = Cursor()
    cursor:SetStyleAuto(style)
    ui:SetCursor(cursor)
    local graphics = GetSubsystem("Graphics")
    cursor:SetPosition(graphics:GetWidth() / 2, graphics:GetHeight() / 2)

    self.instructionText = root:CreateChild("Text")
    self.instructionText:SetText(
        "Use WASD keys to move, RMB to rotate view\n" ..
        "LMB to set destination, SHIFT+LMB to spawn a Jack\n" ..
        "MMB or O key to add obstacles or remove obstacles/agents\n" ..
        "Tab to toggle navigation mesh streaming\n" ..
        "Space to toggle debug geometry\n" ..
        "F12 to toggle this instruction text")
    self.instructionText:SetFont("Fonts/Anonymous Pro.ttf", 15)
    self.instructionText:SetTextAlignment(HA.CENTER)
    self.instructionText:SetHorizontalAlignment(HA.CENTER)
    self.instructionText:SetVerticalAlignment(VA.CENTER)
    self.instructionText:SetPosition(0, root:GetHeight() / 4)
end

function app:SpawnJack(pos, jackGroup)
    local jackNode = jackGroup:CreateChild("Jack")
    jackNode:SetPosition(pos)
    local modelObject = jackNode:CreateComponent("AnimatedModel")
    modelObject:SetModel(GetResource("Model", "Models/Jack.mdl"))
    modelObject:SetCastShadows(true)
    jackNode:CreateComponent("AnimationController")

    -- CrowdAgent with realistic height and max speed/acceleration
    local agent = jackNode:CreateComponent("CrowdAgent")
    agent:SetHeight(2.0)
    agent:SetMaxSpeed(3.0)
    agent:SetMaxAccel(5.0)
end

function app:CreateMushroom(pos)
    local scene = self.scene
    local mushroomNode = scene:CreateChild("Mushroom")
    mushroomNode:SetPosition(pos)
    mushroomNode:SetRotation(Quaternion(0.0, Random(360.0), 0.0))
    mushroomNode:SetScale(2.0 + Random(0.5))
    local mushroomObject = mushroomNode:CreateComponent("StaticModel")
    mushroomObject:SetModel(GetResource("Model", "Models/Mushroom.mdl"))
    mushroomObject:SetMaterial(GetResource("Material", "Materials/Mushroom.xml"))
    mushroomObject:SetCastShadows(true)

    -- Obstacle component with height & radius proportional to scale
    local obstacle = mushroomNode:CreateComponent("Obstacle")
    obstacle:SetRadius(mushroomNode:GetScale().x)
    obstacle:SetHeight(mushroomNode:GetScale().y)
end

function app:CreateBoxOffMeshConnections(navMesh, boxGroup)
    local boxes = boxGroup:GetChildren()
    for _, box in ipairs(boxes) do
        local boxPos = box:GetPosition()
        local boxHalfSize = box:GetScale().x / 2

        -- Start & end nodes of the connection (order matters for one-way)
        local connectionStart = box:CreateChild("ConnectionStart")
        connectionStart:SetWorldPosition(navMesh:FindNearestPoint(boxPos + Vector3(boxHalfSize, -boxHalfSize, 0.0))) -- Base of box
        local connectionEnd = connectionStart:CreateChild("ConnectionEnd")
        connectionEnd:SetWorldPosition(navMesh:FindNearestPoint(boxPos + Vector3(boxHalfSize, boxHalfSize, 0.0))) -- Top of box

        local connection = connectionStart:CreateComponent("OffMeshConnection")
        connection:SetEndPoint(connectionEnd)
    end
end

function app:CreateMovingBarrels(navMesh)
    local scene = self.scene
    local barrel = scene:CreateChild("Barrel")
    local model = barrel:CreateComponent("StaticModel")
    model:SetModel(GetResource("Model", "Models/Cylinder.mdl"))
    local material = GetResource("Material", "Materials/StoneTiled.xml")
    model:SetMaterial(material)
    material:SetTexture("Albedo", GetResource("Texture2D", "Textures/TerrainDetail2.dds"))
    model:SetCastShadows(true)

    for i = 0, 19 do
        local clone = barrel:Clone()
        local size = 0.5 + Random(1.0)
        clone:SetScale(Vector3(size / 1.5, size * 2.0, size / 1.5))
        clone:SetPosition(navMesh:FindNearestPoint(Vector3(Random(80.0) - 40.0, size * 0.5, Random(80.0) - 40.0)))
        local agent = clone:CreateComponent("CrowdAgent")
        agent:SetRadius(clone:GetScale().x * 0.5)
        agent:SetHeight(size)
        agent:SetNavigationQuality(NAV_QUALITY.LOW)
    end
    barrel:Remove()
end

function app:SetPathPoint(spawning)
    local hitPos, hitDrawable = self:Raycast(250.0)
    if hitPos then
        local scene = self.scene
        local navMesh = scene:GetComponent("DynamicNavigationMesh")
        local pathPos = navMesh:FindNearestPoint(hitPos, Vector3(1.0, 1.0, 1.0))
        local jackGroup = scene:GetChild("Jacks")
        if spawning then
            self:SpawnJack(pathPos, jackGroup)
        else
            scene:GetComponent("CrowdManager"):SetCrowdTarget(pathPos, jackGroup)
        end
    end
end

function app:AddOrRemoveObject()
    -- Raycast: hit a mushroom/jack -> remove it, otherwise create a mushroom
    local hitPos, hitDrawable = self:Raycast(250.0)
    if hitPos then
        local hitNode = hitDrawable:GetNode()

        -- Navmesh rebuild happens when the Obstacle component is removed
        if hitNode:GetName() == "Mushroom" then
            hitNode:Remove()
        elseif hitNode:GetName() == "Jack" then
            hitNode:Remove()
        else
            self:CreateMushroom(hitPos)
        end
    end
end

-- Returns hitPos, hitDrawable or nil when nothing was hit
function app:Raycast(maxDistance)
    local ui = GetSubsystem("UI")
    local pos = ui:GetUICursorPosition()
    -- Check the cursor is visible and no UI element is in front of it
    if not ui:GetCursor():IsVisible() or ui:GetElementAt(pos, true) then
        return nil
    end

    local camera = self.cameraNode:GetComponent("Camera")
    local cameraRay = camera:GetScreenRayFromMouse()
    -- Pick only geometry objects, first (closest) hit
    local octree = self.scene:GetComponent("Octree")
    local result = octree:RaycastSingle(cameraRay, maxDistance)
    if result then
        return result.position, result.drawable
    end
    return nil
end

function app:Update(timeStep)
    self:MoveCamera(timeStep)

    -- Update streaming
    local input = GetSubsystem("Input")
    local scene = self.scene
    if not (scene:GetWorldOrigin() == IntVector3(0, 0, 0)) then
        if self.useStreaming then
            -- Streaming and world offset together are beyond this sample
            self.useStreaming = false
            self:ToggleStreaming(false)
        end
    elseif input:GetKeyPress(KEY.TAB) then
        self.useStreaming = not self.useStreaming
        self:ToggleStreaming(self.useStreaming)
    end

    if self.useStreaming then
        self:UpdateStreaming()
    end
end

function app:MoveCamera(timeStep)
    -- RMB controls cursor visibility: hide when pressed
    local ui = GetSubsystem("UI")
    local input = GetSubsystem("Input")
    ui:GetCursor():SetVisible(not input:GetMouseButtonDown(MOUSEB.RIGHT))

    if ui:GetFocusElement() then
        return
    end

    local MOVE_SPEED = 20.0
    local MOUSE_SENSITIVITY = 0.1

    -- Mouse look only when the cursor is hidden
    if not ui:GetCursor():IsVisible() then
        local mouseMove = input:GetMouseMove()
        self.yaw = self.yaw + MOUSE_SENSITIVITY * mouseMove.x
        self.pitch = Clamp(self.pitch + MOUSE_SENSITIVITY * mouseMove.y, -90.0, 90.0)
        self.cameraNode:SetRotation(Quaternion(self.pitch, self.yaw, 0.0))
    end

    if input:GetKeyDown(KEY.W) then
        self.cameraNode:Translate(Vector3.FORWARD * MOVE_SPEED * timeStep)
    end
    if input:GetKeyDown(KEY.S) then
        self.cameraNode:Translate(Vector3.BACK * MOVE_SPEED * timeStep)
    end
    if input:GetKeyDown(KEY.A) then
        self.cameraNode:Translate(Vector3.LEFT * MOVE_SPEED * timeStep)
    end
    if input:GetKeyDown(KEY.D) then
        self.cameraNode:Translate(Vector3.RIGHT * MOVE_SPEED * timeStep)
    end

    -- Set destination or spawn a new jack with LMB
    if input:GetMouseButtonPress(MOUSEB.LEFT) then
        self:SetPathPoint(input:GetQualifierDown(QUAL.SHIFT))
    -- Add new obstacle or remove obstacle/agent with MMB or O
    elseif input:GetMouseButtonPress(MOUSEB.MIDDLE) or input:GetKeyPress(KEY.O) then
        self:AddOrRemoveObject()
    -- Toggle debug geometry with Space
    elseif input:GetKeyPress(KEY.SPACE) then
        self.drawDebug = not self.drawDebug
    -- Toggle instruction text with F12
    elseif input:GetKeyPress(KEY.F12) then
        if self.instructionText then
            self.instructionText:SetVisible(not self.instructionText:IsVisible())
        end
    end
end

function app:ToggleStreaming(enabled)
    local navMesh = self.scene:GetComponent("DynamicNavigationMesh")
    if enabled then
        self:SaveNavigationData()
        navMesh:Allocate()
    else
        navMesh:Rebuild()
    end
end

function app:UpdateStreaming()
    -- Center the navigation mesh at the crowd of jacks
    local scene = self.scene
    local averageJackPosition = Vector3(0.0, 0.0, 0.0)
    local jackGroup = scene:GetChild("Jacks")
    local numJacks = 0
    if jackGroup then
        local jacks = jackGroup:GetChildren()
        numJacks = #jacks
        for _, jack in ipairs(jacks) do
            averageJackPosition = averageJackPosition + jack:GetWorldPosition()
        end
        if numJacks > 0 then
            averageJackPosition = averageJackPosition * (1.0 / numJacks)
        end
    end

    -- Compute currently loaded area
    local navMesh = scene:GetComponent("DynamicNavigationMesh")
    local jackTile = navMesh:GetTileIndex(averageJackPosition)
    local d = self.streamingDistance
    local beginX = jackTile.x - d
    local beginY = jackTile.y - d
    local endX = jackTile.x + d
    local endY = jackTile.y + d

    -- Remove out-of-range tiles
    local toRemove = {}
    for key, tileIdx in pairs(self.addedTiles) do
        if not (beginX <= tileIdx.x and tileIdx.x <= endX and beginY <= tileIdx.y and tileIdx.y <= endY) then
            table.insert(toRemove, key)
        end
    end
    for _, key in ipairs(toRemove) do
        navMesh:RemoveTile(self.addedTiles[key])
        self.addedTiles[key] = nil
    end

    -- Add tiles inside the range
    for z = beginY, endY do
        for x = beginX, endX do
            local key = x .. "," .. z
            local tileIdx = IntVector2(x, z)
            if not navMesh:HasTile(tileIdx) and self.tileData[key] then
                self.addedTiles[key] = tileIdx
                navMesh:AddTile(self.tileData[key].data)
            end
        end
    end
end

function app:SaveNavigationData()
    local navMesh = self.scene:GetComponent("DynamicNavigationMesh")
    self.tileData = {}
    self.addedTiles = {}
    for _, tileIndex in ipairs(navMesh:GetAllTileIndices()) do
        local key = tileIndex.x .. "," .. tileIndex.y
        self.tileData[key] = { tile = tileIndex, data = navMesh:GetTileData(tileIndex) }
    end
end

function app:HandlePostRenderUpdate()
    if self.drawDebug then
        -- Visualize navigation mesh, obstacles and off-mesh connections
        self.scene:GetComponent("DynamicNavigationMesh"):DrawDebugGeometry(true)
        -- Visualize agents' path and position to reach
        self.scene:GetComponent("CrowdManager"):DrawDebugGeometry(true)
    end
end

function app:HandleCrowdAgentFailure(data)
    local node = data.Node
    local agentState = data.CrowdAgentState

    -- Invalid state (likely spawning on the side of a box): find a point in
    -- a larger area and move the node there; CrowdAgent resets its state
    if agentState == CROWD_STATE.INVALID then
        local navMesh = self.scene:GetComponent("DynamicNavigationMesh")
        local newPos = navMesh:FindNearestPoint(node:GetPosition(), Vector3(5.0, 5.0, 5.0))
        node:SetPosition(newPos)
    end
end

function app:HandleCrowdAgentReposition(data)
    local node = data.Node
    local agent = data.CrowdAgent
    local velocity = data.Velocity
    local timeStep = data.TimeStep

    -- Only the Jack agents have an animation controller
    local animCtrl = node:GetComponent("AnimationController")
    if animCtrl then
        local speed = velocity:Length()
        if animCtrl:IsPlaying(WALKING_ANI) then
            local speedRatio = speed / agent:GetMaxSpeed()
            -- Face the direction of travel, moderate turning speed
            node:SetRotation(node:GetRotation():Slerp(
                Quaternion(Vector3.FORWARD, velocity), 10.0 * timeStep * speedRatio))
            -- Throttle the animation speed based on the speed ratio
            animCtrl:SetSpeed(WALKING_ANI, speedRatio * 1.5)
        else
            animCtrl:Play(WALKING_ANI, 0, true, 0.1)
        end

        -- If speed is too low then stop the animation
        if speed < agent:GetRadius() then
            animCtrl:Stop(WALKING_ANI, 0.5)
        end
    end
end

app:Run()

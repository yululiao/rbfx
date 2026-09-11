-- LuaSamples/15_Navigation/main.lua
-- Lua port of Source/Samples/15_Navigation: navigation mesh construction,
-- pathfinding and streaming. LMB sets a path for Jack to follow,
-- SHIFT+LMB teleports, MMB/O adds or removes mushrooms (rebuilding the
-- affected navmesh tiles), Tab toggles tile streaming, Space shows debug.

local app = Sample:new()
app.drawDebug = false
app.useStreaming = false
app.streamingDistance = 2
app.currentPath = {}
app.endPos = Vector3(0.0, 0.0, 0.0)
-- Streaming bookkeeping, keyed by "x,y" tile index strings.
app.tileData = {}
app.addedTiles = {}

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.RELATIVE)
    input:SetMouseVisible(false)

    self:CreateScene()
    self:CreateUI()

    -- Draw navigation debug geometry during post-render update
    SubscribeToEvent("PostRenderUpdate", function(data)
        self:HandlePostRenderUpdate()
    end)
end

function app:CreateScene()
    local scene = CreateScene()
    self.scene = scene

    -- Create octree and DebugRenderer
    scene:CreateComponent("Octree")
    scene:CreateComponent("DebugRenderer")

    -- Create scene node & StaticModel component for showing a static plane
    local planeNode = scene:CreateChild("Plane")
    planeNode:SetScale(Vector3(100.0, 1.0, 100.0))
    local planeObject = planeNode:CreateComponent("StaticModel")
    planeObject:SetModel(GetResource("Model", "Models/Plane.mdl"))
    planeObject:SetMaterial(GetResource("Material", "Materials/StoneTiled.xml"))

    -- Create a Zone component for ambient lighting & fog control
    local zoneNode = scene:CreateChild("Zone")
    local zone = zoneNode:CreateComponent("Zone")
    zone:SetBoundingBox(BoundingBox(-1000.0, 1000.0))
    zone:SetAmbientColor(Color(0.15, 0.15, 0.15))
    zone:SetFogColor(Color(0.5, 0.5, 0.7))
    zone:SetFogStart(100.0)
    zone:SetFogEnd(300.0)

    -- Create a directional light to the world. Enable cascaded shadows on it
    local lightNode = scene:CreateChild("DirectionalLight")
    lightNode:SetDirection(Vector3(0.6, -1.0, 0.8))
    local light = lightNode:CreateComponent("Light")
    light:SetLightType(LIGHT.DIRECTIONAL)
    light:SetCastShadows(true)
    light:SetShadowBias(0.00025, 0.5)
    light:SetShadowCascade(10.0, 50.0, 200.0, 0.0, 0.8)

    -- Create some mushrooms
    for i = 1, 100 do
        self:CreateMushroom(Vector3(Random(90.0) - 45.0, 0.0, Random(90.0) - 45.0))
    end

    -- Create randomly sized boxes. If boxes are big enough, make them
    -- occluders
    for i = 1, 20 do
        local boxNode = scene:CreateChild("Box")
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

    -- Create Jack node that will follow the path
    local jackNode = scene:CreateChild("Jack")
    jackNode:SetPosition(Vector3(-5.0, 0.0, 20.0))
    local modelObject = jackNode:CreateComponent("AnimatedModel")
    modelObject:SetModel(GetResource("Model", "Models/Jack.mdl"))
    modelObject:SetCastShadows(true)
    self.jackNode = jackNode

    -- Create a NavigationMesh component to the scene root. Set small tiles
    -- to show navigation mesh streaming
    local navMesh = scene:CreateComponent("NavigationMesh")
    navMesh:SetTileSize(30)
    -- Create a Navigable component to the scene root. This tags all of the
    -- geometry in the scene as being part of the navigation mesh
    scene:CreateComponent("Navigable")
    -- Add padding to the navigation mesh in Y-direction so that we can add
    -- objects on top of the tallest boxes and still update the mesh correctly
    navMesh:SetPadding(Vector3(0.0, 10.0, 0.0))
    -- Now build the navigation geometry. This will take some time
    navMesh:Rebuild()

    -- Create the camera. Limit far clip distance to match the fog
    local cameraNode = scene:CreateChild("Camera")
    local camera = cameraNode:CreateComponent("Camera")
    camera:SetFarClip(300.0)
    cameraNode:SetPosition(Vector3(0.0, 50.0, 0.0))
    self.pitch = 80.0
    cameraNode:SetRotation(Quaternion(self.pitch, self.yaw, 0.0))
    self.cameraNode = cameraNode
    SetViewport(0, scene, camera)
end

function app:CreateUI()
    local ui = GetSubsystem("UI")
    local graphics = GetSubsystem("Graphics")

    -- Create a Cursor UI element because we want to be able to hide and show
    -- it at will. When hidden, the mouse cursor will control the camera, and
    -- when visible, it will point the raycast target
    local uiRoot = ui:GetRoot()
    local cursor = uiRoot:CreateChild("Cursor")
    cursor:SetStyleAuto()
    ui:SetCursor(cursor)
    cursor:SetPosition(graphics:GetWidth() / 2, graphics:GetHeight() / 2)

    -- Construct new Text object, set string to display and font to use
    local instructionText = uiRoot:CreateChild("Text")
    instructionText:SetText(
        "Use WASD keys to move, RMB to rotate view\n" ..
        "LMB to set destination, SHIFT+LMB to teleport\n" ..
        "MMB or O key to add or remove obstacles\n" ..
        "Tab to toggle navigation mesh streaming\n" ..
        "Space to toggle debug geometry")
    instructionText:SetFont("Fonts/Anonymous Pro.ttf", 15)
    instructionText:SetTextAlignment(HA.CENTER)
    instructionText:SetHorizontalAlignment(HA.CENTER)
    instructionText:SetVerticalAlignment(VA.CENTER)
    instructionText:SetPosition(0, uiRoot:GetHeight() / 4)
end

function app:Update(timeStep)
    self:MoveCamera(timeStep)

    -- Make Jack follow the Detour path
    self:FollowPath(timeStep)

    -- Toggle tile streaming with Tab. Streaming and world offset can work
    -- together, but it is beyond the scope of this sample
    local input = GetSubsystem("Input")
    if self.scene:GetWorldOrigin() ~= IntVector3.ZERO then
        if self.useStreaming then
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
    -- Right mouse button controls mouse cursor visibility: hide when pressed
    local ui = GetSubsystem("UI")
    local input = GetSubsystem("Input")
    ui:GetCursor():SetVisible(not input:GetMouseButtonDown(MOUSEB.RIGHT))

    -- Do not move if the UI has a focused element (the console)
    if ui:GetFocusElement() then
        return
    end

    local MOVE_SPEED = 20.0
    local MOUSE_SENSITIVITY = 0.1

    -- Use this frame's mouse motion to adjust camera node yaw and pitch.
    -- Only rotate the camera when the cursor is hidden
    if not ui:GetCursor():IsVisible() then
        local mouseMove = input:GetMouseMove()
        self.yaw = self.yaw + MOUSE_SENSITIVITY * mouseMove.x
        self.pitch = Clamp(self.pitch + MOUSE_SENSITIVITY * mouseMove.y, -90.0, 90.0)
        self.cameraNode:SetRotation(Quaternion(self.pitch, self.yaw, 0.0))
    end

    -- Read WASD keys and move the camera scene node to the corresponding
    -- direction if they are pressed
    if input:GetKeyDown(string.byte("w")) then
        self.cameraNode:Translate(Vector3.FORWARD * MOVE_SPEED * timeStep)
    end
    if input:GetKeyDown(string.byte("s")) then
        self.cameraNode:Translate(Vector3.BACK * MOVE_SPEED * timeStep)
    end
    if input:GetKeyDown(string.byte("a")) then
        self.cameraNode:Translate(Vector3.LEFT * MOVE_SPEED * timeStep)
    end
    if input:GetKeyDown(string.byte("d")) then
        self.cameraNode:Translate(Vector3.RIGHT * MOVE_SPEED * timeStep)
    end

    -- Set destination or teleport with left mouse button
    if input:GetMouseButtonPress(MOUSEB.LEFT) then
        self:SetPathPoint()
    end
    -- Add or remove objects with middle mouse button, then rebuild
    -- navigation mesh partially
    if input:GetMouseButtonPress(MOUSEB.MIDDLE) or input:GetKeyPress(string.byte("o")) then
        self:AddOrRemoveObject()
    end
end

-- Raycast from the mouse cursor. Returns the hit table from
-- Octree:RaycastSingle or nil.
function app:Raycast(maxDistance)
    local ui = GetSubsystem("UI")
    local pos = ui:GetUICursorPosition()
    -- Check the cursor is visible and there is no UI element in front of the
    -- cursor
    if not ui:GetCursor():IsVisible() or ui:GetElementAt(pos, true) then
        return nil
    end

    local camera = self.cameraNode:GetComponent("Camera")
    local cameraRay = camera:GetScreenRayFromMouse()
    -- Pick only geometry objects, not eg. zones or lights, only get the
    -- first (closest) hit
    return self.scene:GetComponent("Octree"):RaycastSingle(cameraRay, maxDistance)
end

function app:SetPathPoint()
    local navMesh = self.scene:GetComponent("NavigationMesh")

    local hit = self:Raycast(250.0)
    if hit then
        local pathPos = navMesh:FindNearestPoint(hit.position, Vector3(1.0, 1.0, 1.0))

        if GetSubsystem("Input"):GetQualifierDown(QUAL.SHIFT) then
            -- Teleport
            self.currentPath = {}
            local jackPos = self.jackNode:GetPosition()
            self.jackNode:LookAt(Vector3(pathPos.x, jackPos.y, pathPos.z), Vector3.UP)
            self.jackNode:SetPosition(pathPos)
        else
            -- Calculate path from Jack's current position to the end point
            self.endPos = pathPos
            self.currentPath = navMesh:FindPath(self.jackNode:GetPosition(), self.endPos)
        end
    end
end

function app:AddOrRemoveObject()
    -- Raycast and check if we hit a mushroom node. If yes, remove it, if no,
    -- create a new one
    if self.useStreaming then
        return
    end

    local hit = self:Raycast(250.0)
    if hit then
        -- The part of the navigation mesh we must update, which is the world
        -- bounding box of the associated drawable component
        local updateBox

        local hitNode = hit.drawable:GetNode()
        if hitNode:GetName() == "Mushroom" then
            updateBox = hit.drawable:GetWorldBoundingBox()
            hitNode:Remove()
        else
            local newNode = self:CreateMushroom(hit.position)
            updateBox = newNode:GetComponent("StaticModel"):GetWorldBoundingBox()
        end

        -- Rebuild part of the navigation mesh, then recalculate path if
        -- applicable
        local navMesh = self.scene:GetComponent("NavigationMesh")
        navMesh:BuildTilesInRegion(updateBox)
        if #self.currentPath > 0 then
            self.currentPath = navMesh:FindPath(self.jackNode:GetPosition(), self.endPos)
        end
    end
end

function app:CreateMushroom(pos)
    local mushroomNode = self.scene:CreateChild("Mushroom")
    mushroomNode:SetPosition(pos)
    mushroomNode:SetRotation(Quaternion(0.0, Random(360.0), 0.0))
    mushroomNode:SetScale(2.0 + Random(0.5))
    local mushroomObject = mushroomNode:CreateComponent("StaticModel")
    mushroomObject:SetModel(GetResource("Model", "Models/Mushroom.mdl"))
    mushroomObject:SetMaterial(GetResource("Material", "Materials/Mushroom.xml"))
    mushroomObject:SetCastShadows(true)
    return mushroomNode
end

function app:FollowPath(timeStep)
    if #self.currentPath > 0 then
        local nextWaypoint = self.currentPath[1]

        -- Rotate Jack toward next waypoint to reach and move. Check for not
        -- overshooting the target
        local move = 5.0 * timeStep
        local distance = (self.jackNode:GetPosition() - nextWaypoint):Length()
        if move > distance then
            move = distance
        end

        self.jackNode:LookAt(nextWaypoint, Vector3.UP)
        self.jackNode:Translate(Vector3.FORWARD * move)

        -- Remove waypoint if reached it
        if distance < 0.1 then
            table.remove(self.currentPath, 1)
        end
    end
end

function app:ToggleStreaming(enabled)
    local navMesh = self.scene:GetComponent("NavigationMesh")
    if enabled then
        self:SaveNavigationData()
        navMesh:Allocate()
    else
        navMesh:Rebuild()
    end
end

function app:UpdateStreaming()
    -- Center the navigation mesh at the jack
    local navMesh = self.scene:GetComponent("NavigationMesh")
    local jackTile = navMesh:GetTileIndex(self.jackNode:GetWorldPosition())
    local beginTile = IntVector2(jackTile.x - self.streamingDistance, jackTile.y - self.streamingDistance)
    local endTile = IntVector2(jackTile.x + self.streamingDistance, jackTile.y + self.streamingDistance)

    local function TileKey(tile)
        return tile.x .. "," .. tile.y
    end

    -- Remove tiles outside the streaming range
    for key in pairs(self.addedTiles) do
        local comma = string.find(key, ",")
        local tileX = tonumber(string.sub(key, 1, comma - 1))
        local tileY = tonumber(string.sub(key, comma + 1))
        if beginTile.x <= tileX and tileX <= endTile.x and beginTile.y <= tileY and tileY <= endTile.y then
            -- still inside the range, keep the tile
        else
            self.addedTiles[key] = nil
            navMesh:RemoveTile(IntVector2(tileX, tileY))
        end
    end

    -- Add tiles
    for z = beginTile.y, endTile.y do
        for x = beginTile.x, endTile.x do
            local tileIdx = IntVector2(x, z)
            local key = TileKey(tileIdx)
            if not navMesh:HasTile(tileIdx) and self.tileData[key] then
                self.addedTiles[key] = true
                navMesh:AddTile(self.tileData[key])
            end
        end
    end
end

function app:SaveNavigationData()
    local navMesh = self.scene:GetComponent("NavigationMesh")
    self.tileData = {}
    self.addedTiles = {}
    for _, tileIndex in ipairs(navMesh:GetAllTileIndices()) do
        self.tileData[tileIndex.x .. "," .. tileIndex.y] = navMesh:GetTileData(tileIndex)
    end
end

function app:HandlePostRenderUpdate()
    -- If draw debug mode is enabled, draw navigation mesh debug geometry
    if self.drawDebug then
        self.scene:GetComponent("NavigationMesh"):DrawDebugGeometry(true)
    end

    if #self.currentPath > 0 then
        -- Visualize the current calculated path
        local debug = self.scene:GetComponent("DebugRenderer")
        debug:AddBoundingBox(
            BoundingBox(self.endPos - Vector3(0.1, 0.1, 0.1), self.endPos + Vector3(0.1, 0.1, 0.1)),
            Color(1.0, 1.0, 1.0))

        -- Draw the path with a small upward bias so that it does not clip
        -- into the surfaces
        local bias = Vector3(0.0, 0.05, 0.0)
        debug:AddLine(self.jackNode:GetPosition() + bias, self.currentPath[1] + bias, Color(1.0, 1.0, 1.0))

        if #self.currentPath > 1 then
            for i = 1, #self.currentPath - 1 do
                debug:AddLine(self.currentPath[i] + bias, self.currentPath[i + 1] + bias, Color(1.0, 1.0, 1.0))
            end
        end
    end
end

-- Toggle debug geometry with space
function app:OnKeyDown(key)
    if key == KEY.SPACE then
        self.drawDebug = not self.drawDebug
    end
end

app:Run()

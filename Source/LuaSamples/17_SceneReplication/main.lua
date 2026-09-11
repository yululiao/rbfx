-- LuaSamples/17_SceneReplication/main.lua
-- Lua port of Source/Samples/17_SceneReplication: an authoritative server
-- replicates a controllable physics ball for each client through the rbfx
-- Replica system. The C++ sample's custom NetworkBehavior callbacks
-- (snapshot color + unreliable controls feedback) are replaced by default
-- attribute replication plus a plain network message carrying the controls.

-- UDP port we will use
local SERVER_PORT = 2345

-- Control bits we define
local CTRL_FORWARD = 1
local CTRL_BACK = 2
local CTRL_LEFT = 4
local CTRL_RIGHT = 8

-- Network message id for the client controls feedback (17_SceneReplication)
local MSG_CONTROLS = MSG.USER + 1

-- 2^n flag test that works on every Lua version (no bitwise operators)
local function HasFlag(value, flag)
    return value % (flag * 2) >= flag
end

local app = Sample:new()
app.serverObjects = {}   -- Connection -> player Node (server side)
app.serverControls = {}  -- Connection -> latest controls (server side)
app.overlayTimer = 0.0

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.RELATIVE)
    input:SetMouseVisible(false)

    self:CreateScene()
    self:CreateUI()
    self:SubscribeToEvents()
end

function app:CreateScene()
    local scene = CreateScene()
    self.scene = scene

    -- Create octree and physics world with default settings. Create them as
    -- local so that they are not needlessly replicated when a client
    -- connects. ReplicationManager drives the rbfx Replica system.
    scene:CreateComponent("Octree")
    scene:CreateComponent("PhysicsWorld")
    scene:CreateComponent("ReplicationManager")

    -- All static scene content and the camera are also created as local, so
    -- that they are unaffected by scene replication and are not removed from
    -- the client upon connection. Create a Zone for ambient lighting & fog.
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

    -- Create a "floor" consisting of several tiles. Make the tiles physical
    -- but leave small cracks between them
    for y = -20, 20 do
        for x = -20, 20 do
            local floorNode = scene:CreateChild("FloorTile")
            floorNode:SetPosition(Vector3(x * 20.2, -0.5, y * 20.2))
            floorNode:SetScale(Vector3(20.0, 1.0, 20.0))
            local floorObject = floorNode:CreateComponent("StaticModel")
            floorObject:SetModel(GetResource("Model", "Models/Box.mdl"))
            floorObject:SetMaterial(GetResource("Material", "Materials/Stone.xml"))

            local body = floorNode:CreateComponent("RigidBody")
            body:SetFriction(1.0)
            local shape = floorNode:CreateComponent("CollisionShape")
            shape:SetBox(Vector3.ONE)
        end
    end

    -- The camera is created into a local node so that each client can retain
    -- its own camera, unaffected by network messages. Limit far clip
    -- distance to match the fog
    self.cameraNode = scene:CreateChild("Camera")
    local camera = self.cameraNode:CreateComponent("Camera")
    camera:SetFarClip(300.0)
    self.cameraNode:SetPosition(Vector3(0.0, 5.0, 0.0))

    SetViewport(0, scene, camera)
end

function app:CreateUI()
    local graphics = GetSubsystem("Graphics")
    local ui = GetSubsystem("UI")
    local root = ui:GetRoot()
    -- Set style to the UI root so that elements will inherit it
    root:SetDefaultStyle(GetResource("XMLFile", "UI/DefaultStyle.xml"))

    -- Create a Cursor UI element because we want to be able to hide and show
    -- it at will. When hidden, the mouse cursor controls the camera, and when
    -- visible, it can interact with the login UI
    local cursor = Cursor()
    cursor:SetStyleAuto()
    ui:SetCursor(cursor)
    -- Set starting position of the cursor at the rendering window center
    cursor:SetPosition(graphics:GetWidth() / 2, graphics:GetHeight() / 2)

    -- Instructions text, positioned relative to the screen center and hidden
    -- until connected
    self.instructionsText = root:CreateChild("Text")
    self.instructionsText:SetText("Use WASD keys to move and RMB to rotate view")
    self.instructionsText:SetFont("Fonts/Anonymous Pro.ttf", 15)
    self.instructionsText:SetHorizontalAlignment(HA.CENTER)
    self.instructionsText:SetVerticalAlignment(VA.CENTER)
    self.instructionsText:SetPosition(0, graphics:GetHeight() / 4)
    self.instructionsText:SetVisible(false)

    -- Network traffic statistics overlays
    self.packetsIn = self:CreateOverlayText(10)
    self.packetsOut = self:CreateOverlayText(30)
    self.bytesIn = self:CreateOverlayText(50)
    self.bytesOut = self:CreateOverlayText(70)
    self.connectionsText = self:CreateOverlayText(90)
    self.serverRunningText = self:CreateOverlayText(110)
    self:UpdateOverlay(0, 0, 0, 0, 0)

    -- Connection buttons
    self.buttonContainer = root:CreateChild("UIElement")
    self.buttonContainer:SetFixedSize(500, 20)
    self.buttonContainer:SetPosition(20, 20)
    self.buttonContainer:SetLayoutMode(LM.HORIZONTAL)

    self.textEdit = self.buttonContainer:CreateChild("LineEdit")
    self.textEdit:SetStyleAuto()

    self.connectButton = self:CreateButton("Connect", 90)
    self.disconnectButton = self:CreateButton("Disconnect", 100)
    self.startServerButton = self:CreateButton("Start Server", 110)

    self:UpdateButtons()
end

function app:CreateOverlayText(y)
    local root = GetSubsystem("UI"):GetRoot()
    local textElement = root:CreateChild("Text")
    textElement:SetFont("Fonts/Anonymous Pro.ttf", 15)
    textElement:SetHorizontalAlignment(HA.LEFT)
    textElement:SetVerticalAlignment(VA.CENTER)
    textElement:SetPosition(10, y)
    return textElement
end

function app:CreateButton(text, width)
    local button = self.buttonContainer:CreateChild("Button")
    button:SetStyleAuto()
    button:SetFixedWidth(width)

    local buttonText = button:CreateChild("Text")
    buttonText:SetFont("Fonts/Anonymous Pro.ttf", 12)
    buttonText:SetAlignment(HA.CENTER, VA.CENTER)
    buttonText:SetText(text)
    return button
end

function app:SubscribeToEvents()
    -- Subscribe to fixed timestep physics updates for setting or applying
    -- controls
    SubscribeToEvent("PhysicsPreStep", function(data) self:HandlePhysicsPreStep() end)

    -- Subscribe to PostUpdate instead of the usual Update so that physics
    -- simulation has already proceeded for the frame, and the camera can
    -- accurately follow the ball
    SubscribeToEvent("PostUpdate", function(data) self:HandlePostUpdate(data) end)

    -- Subscribe to button actions
    SubscribeToEvent(self.connectButton, "Released", function(data) self:HandleConnect() end)
    SubscribeToEvent(self.disconnectButton, "Released", function(data) self:HandleDisconnect() end)
    SubscribeToEvent(self.startServerButton, "Released", function(data) self:HandleStartServer() end)

    -- Subscribe to network events
    SubscribeToEvent("ServerConnected", function(data) self:UpdateButtons() end)
    SubscribeToEvent("ServerDisconnected", function(data) self:UpdateButtons() end)
    SubscribeToEvent("ConnectFailed", function(data) self:UpdateButtons() end)
    SubscribeToEvent("ClientConnected", function(data) self:HandleClientConnected(data) end)
    SubscribeToEvent("ClientDisconnected", function(data) self:HandleClientDisconnected(data) end)
    SubscribeToEvent("NetworkMessage", function(data) self:HandleNetworkMessage(data) end)
end

function app:UpdateButtons()
    local network = GetSubsystem("Network")
    local serverConnection = network:GetServerConnection()
    local serverRunning = network:IsServerRunning()

    -- Show and hide buttons so that eg. Connect and Disconnect are never
    -- shown at the same time
    self.connectButton:SetVisible(serverConnection == nil and not serverRunning)
    self.disconnectButton:SetVisible(serverConnection ~= nil or serverRunning)
    self.startServerButton:SetVisible(serverConnection == nil and not serverRunning)
    self.textEdit:SetVisible(serverConnection == nil and not serverRunning)
end

function app:CreateControllableObject(owner)
    local prefab = GetResource("PrefabResource", "Prefabs/SceneReplicationPlayer.prefab")

    -- Instantiate common components from prefab so they will be replicated
    -- on the client
    local position = Vector3(math.random() * 40.0 - 20.0, 5.0, math.random() * 40.0 - 20.0)
    local playerNode = self.scene:InstantiatePrefab(prefab, position, Quaternion.IDENTITY)
    playerNode:SetName("Ball")

    -- NetworkObject should never be a part of client prefab. The random light
    -- color assigned below reaches clients through default attribute
    -- replication.
    local networkObject = playerNode:CreateComponent("BehaviorNetworkObject")
    networkObject:SetClientPrefab(prefab)
    networkObject:SetOwner(owner)

    -- Create the physics components on server only
    local body = playerNode:CreateComponent("RigidBody")
    body:SetMass(1.0)
    body:SetFriction(1.0)
    -- In addition to friction, use motion damping so that the ball can not
    -- accelerate limitlessly
    body:SetLinearDamping(0.5)
    body:SetAngularDamping(0.5)
    local shape = playerNode:CreateComponent("CollisionShape")
    shape:SetSphere(1.0)

    -- Assign a random color to the point light at the ball
    local light = playerNode:GetComponent("Light")
    light:SetColor(Color(0.5 + math.random(0, 1) * 0.5,
                         0.5 + math.random(0, 1) * 0.5,
                         0.5 + math.random(0, 1) * 0.5))

    return playerNode
end

function app:GetPlayerObject()
    local replicationManager = self.scene:GetComponent("ReplicationManager")
    if replicationManager then
        local clientReplica = replicationManager:GetClientReplica()
        if clientReplica then
            local networkObject = clientReplica:GetOwnedNetworkObject()
            if networkObject then
                return networkObject:GetNode()
            end
        end
    end
    return nil
end

function app:MoveCamera()
    -- Right mouse button controls mouse cursor visibility: hide when pressed
    local ui = GetSubsystem("UI")
    local input = GetSubsystem("Input")
    local cursor = ui:GetCursor()
    cursor:SetVisible(not input:GetMouseButtonDown(MOUSEB.RIGHT))

    -- Mouse sensitivity as degrees per pixel
    local MOUSE_SENSITIVITY = 0.1

    -- Use this frame's mouse motion to adjust camera node yaw and pitch.
    -- Clamp the pitch and only move the camera when the cursor is hidden
    if not cursor:IsVisible() then
        local mouseMove = input:GetMouseMove()
        self.yaw = self.yaw + MOUSE_SENSITIVITY * mouseMove.x
        self.pitch = self.pitch + MOUSE_SENSITIVITY * mouseMove.y
        self.pitch = math.max(1.0, math.min(90.0, self.pitch))
    end

    -- Construct new orientation for the camera scene node from yaw and
    -- pitch. Roll is fixed to zero
    self.cameraNode:SetRotation(Quaternion(self.pitch, self.yaw, 0.0))

    -- Only move the camera / show instructions if we have a controllable
    -- object
    local showInstructions = false
    local playerNode = self:GetPlayerObject()
    if playerNode then
        local CAMERA_DISTANCE = 5.0

        -- Move camera some distance away from the ball
        local rotation = self.cameraNode:GetRotation()
        self.cameraNode:SetPosition(playerNode:GetPosition() + rotation * Vector3.BACK * CAMERA_DISTANCE)
        showInstructions = true
    end

    self.instructionsText:SetVisible(showInstructions)
end

function app:HandlePostUpdate(data)
    -- We only rotate the camera according to mouse movement since last
    -- frame, so do not need the time step for that
    self:MoveCamera()

    -- Refresh the traffic statistics overlay once per second
    self.overlayTimer = self.overlayTimer + data.TimeStep
    if self.overlayTimer < 1.0 then
        return
    end
    self.overlayTimer = 0.0

    local network = GetSubsystem("Network")

    local packetsIn, packetsOut, bytesIn, bytesOut = 0, 0, 0, 0
    local connectionCount = 0

    local connectionToServer = network:GetServerConnection()
    if connectionToServer then
        connectionCount = 1
        packetsIn = connectionToServer:GetPacketsInPerSec()
        packetsOut = connectionToServer:GetPacketsOutPerSec()
        bytesIn = connectionToServer:GetBytesInPerSec()
        bytesOut = connectionToServer:GetBytesOutPerSec()
    else
        local connections = network:GetClientConnections()
        connectionCount = #connections
        for _, connection in ipairs(connections) do
            packetsIn = packetsIn + connection:GetPacketsInPerSec()
            packetsOut = packetsOut + connection:GetPacketsOutPerSec()
            bytesIn = bytesIn + connection:GetBytesInPerSec()
            bytesOut = bytesOut + connection:GetBytesOutPerSec()
        end
    end

    self:UpdateOverlay(packetsIn, packetsOut, bytesIn, bytesOut, connectionCount)
end

function app:UpdateOverlay(packetsIn, packetsOut, bytesIn, bytesOut, connections)
    self.packetsIn:SetText("Packets  in: " .. packetsIn)
    self.packetsOut:SetText("Packets out: " .. packetsOut)
    self.bytesIn:SetText("Bytes    in: " .. bytesIn)
    self.bytesOut:SetText("Bytes   out: " .. bytesOut)
    self.connectionsText:SetText("Connections: " .. connections)
    local network = GetSubsystem("Network")
    self.serverRunningText:SetText(network:IsServerRunning() and "Server on" or "")
end

function app:HandlePhysicsPreStep()
    -- This function is different on the client and server. The client
    -- collects controls (WASD controls + yaw angle) and sends them to the
    -- server with a network message, mimicking the C++ sample's unreliable
    -- feedback channel. The server applies the controls (authoritative
    -- simulation).
    local network = GetSubsystem("Network")
    local serverConnection = network:GetServerConnection()

    if serverConnection then
        -- Client: collect controls
        local ui = GetSubsystem("UI")
        local input = GetSubsystem("Input")
        local playerNode = self:GetPlayerObject()
        if playerNode then
            local buttons = 0

            -- Only apply WASD controls if there is no focused UI element
            if not ui:GetFocusElement() then
                if input:GetKeyDown(string.byte("w")) then buttons = buttons + CTRL_FORWARD end
                if input:GetKeyDown(string.byte("s")) then buttons = buttons + CTRL_BACK end
                if input:GetKeyDown(string.byte("a")) then buttons = buttons + CTRL_LEFT end
                if input:GetKeyDown(string.byte("d")) then buttons = buttons + CTRL_RIGHT end
            end

            local msg = VectorBuffer()
            msg:WriteFloat(self.yaw)
            msg:WriteVLE(buttons)
            serverConnection:SendMessage(MSG_CONTROLS, msg)
        end
    elseif network:IsServerRunning() then
        -- Server: apply controls to client objects
        local connections = network:GetClientConnections()
        for _, connection in ipairs(connections) do
            -- Get the object this connection is controlling
            local playerNode = self.serverObjects[connection]
            if playerNode then
                local body = playerNode:GetComponent("RigidBody")
                local controls = self.serverControls[connection]

                if body and controls then
                    -- Get the last controls sent by the client. Torque is
                    -- relative to the forward vector
                    local rotation = Quaternion(0.0, controls.yaw, 0.0)

                    local MOVE_TORQUE = 3.0

                    -- Movement torque is applied before each simulation
                    -- step, which happens at 60 FPS. This makes the
                    -- simulation independent from rendering framerate. We
                    -- could also apply forces (which would enable in-air
                    -- control), but want to emphasize that it's a ball which
                    -- should only control its motion by rolling along the
                    -- ground
                    if HasFlag(controls.buttons, CTRL_FORWARD) then
                        body:ApplyTorque(rotation * Vector3.RIGHT * MOVE_TORQUE)
                    end
                    if HasFlag(controls.buttons, CTRL_BACK) then
                        body:ApplyTorque(rotation * Vector3.LEFT * MOVE_TORQUE)
                    end
                    if HasFlag(controls.buttons, CTRL_LEFT) then
                        body:ApplyTorque(rotation * Vector3.FORWARD * MOVE_TORQUE)
                    end
                    if HasFlag(controls.buttons, CTRL_RIGHT) then
                        body:ApplyTorque(rotation * Vector3.BACK * MOVE_TORQUE)
                    end
                end
            end
        end
    end
end

function app:HandleNetworkMessage(data)
    if data.MessageID == MSG_CONTROLS then
        -- Server: read the latest controls sent by the client
        local connection = data.Connection
        if self.serverObjects[connection] then
            local msg = MemoryBuffer(data.Data)
            local yaw = msg:ReadFloat()
            local buttons = msg:ReadVLE()
            self.serverControls[connection] = { yaw = yaw, buttons = buttons }
        end
    end
end

function app:HandleConnect()
    local network = GetSubsystem("Network")
    local address = self.textEdit:GetText()
    address = address:gsub("^%s*(.-)%s*$", "%1")
    if address == "" then
        address = "localhost" -- Use localhost to connect if nothing else specified
    end
    -- Empty the text edit after reading the address to connect to
    self.textEdit:SetText("")

    -- Connect to server, specify scene to use as a client for replication
    network:Connect(address .. ":" .. SERVER_PORT, self.scene)

    self:UpdateButtons()
end

function app:HandleDisconnect()
    local network = GetSubsystem("Network")
    local serverConnection = network:GetServerConnection()
    -- If we were connected to server, disconnect. Or if we were running a
    -- server, stop it. In both cases the scene keeps its local nodes &
    -- components (the static world + camera)
    if serverConnection then
        serverConnection:Disconnect()
    elseif network:IsServerRunning() then
        network:StopServer()
    end

    self:UpdateButtons()
end

function app:HandleStartServer()
    local network = GetSubsystem("Network")
    network:StartServer(SERVER_PORT)

    self:UpdateButtons()
end

function app:HandleClientConnected(data)
    -- When a client connects, assign the scene to begin scene replication
    local newConnection = data.Connection
    newConnection:SetScene(self.scene)

    -- Then create a controllable object for that client
    local newObject = self:CreateControllableObject(newConnection)
    self.serverObjects[newConnection] = newObject
    self.serverControls[newConnection] = { yaw = 0.0, buttons = 0 }
end

function app:HandleClientDisconnected(data)
    -- When a client disconnects, remove the controlled object
    local connection = data.Connection
    local object = self.serverObjects[connection]
    if object then
        object:Remove()
    end

    self.serverObjects[connection] = nil
    self.serverControls[connection] = nil
end

app:Run()

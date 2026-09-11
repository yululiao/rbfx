-- LuaSamples/53_LANDiscovery/main.lua
-- Lua port of Source/Samples/53_LANDiscovery: LAN server discovery using
-- broadcast beacons. One instance can host a server, another discovers it
-- ("Search..."); discovered servers expire after 10s of silence.

local SERVER_PORT = 54654

local app = Sample:new()
app.serverListItems = {}

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.FREE)
    input:SetMouseVisible(true)

    self:CreateUI()
    self:SubscribeToEvents()
end

function app:CreateUI()
    self:CreateLogo()

    local root = GetUIRoot()
    root:SetDefaultStyle(GetResource("XMLFile", "UI/DefaultStyle.xml"))

    -- No viewports or scene: the default zone's fog color is the fill color
    GetSubsystem("Renderer"):GetDefaultZone():SetFogColor(Color(0.0, 0.0, 0.1))

    local marginTop = 20
    self:CreateLabel("1. Start server", IntVector2(20, marginTop - 20))
    self.startServer = self:CreateButton("Start server", 160, IntVector2(20, marginTop))
    self.stopServer = self:CreateButton("Stop server", 160, IntVector2(20, marginTop))
    self.stopServer:SetVisible(false)

    -- Client connection related fields
    marginTop = marginTop + 80
    self:CreateLabel("2. Discover LAN servers", IntVector2(20, marginTop - 20))
    self.refreshServerList = self:CreateButton("Search...", 160, IntVector2(20, marginTop))

    marginTop = marginTop + 80
    self:CreateLabel("Local servers:", IntVector2(20, marginTop - 20))
    self.serverList = self:CreateLabel("", IntVector2(20, marginTop))
end

function app:SubscribeToEvents()
    -- LAN discovery manager created like the C++ MakeShared call
    self.lanDiscovery = LANDiscoveryManager()

    SubscribeToEvent("NetworkHostDiscovered", function(data)
        self:HandleNetworkHostDiscovered(data)
    end)

    SubscribeToEvent(self.startServer, "Released", function() self:HandleStartServer() end)
    SubscribeToEvent(self.stopServer, "Released", function() self:HandleStopServer() end)
    SubscribeToEvent(self.refreshServerList, "Released", function() self:HandleDoNetworkDiscovery() end)
end

function app:CreateButton(text, width, position)
    local root = GetUIRoot()
    local font = GetResource("Font", "Fonts/Anonymous Pro.ttf")

    local button = root:CreateChild("Button")
    button:SetStyleAuto()
    button:SetFixedWidth(width)
    button:SetFixedHeight(30)
    button:SetPosition(position)

    local buttonText = button:CreateChild("Text")
    buttonText:SetFont(font, 12)
    buttonText:SetAlignment(HA.CENTER, VA.CENTER)
    buttonText:SetText(text)

    return button
end

function app:CreateLabel(text, pos)
    local root = GetUIRoot()
    local font = GetResource("Font", "Fonts/Anonymous Pro.ttf")
    local label = root:CreateChild("Text")
    label:SetFont(font, 12)
    label:SetColor(Color(0.0, 1.0, 0.0))
    label:SetPosition(pos)
    label:SetText(text)
    return label
end

function app:HandleNetworkHostDiscovered(data)
    local beacon = data.Beacon
    local name = beacon.Name

    -- Refresh server that reannounced itself
    local item = self.serverListItems[name]
    if item then
        item.lastSeen = os.time()
    else
        self.serverListItems[name] = {
            name = name,
            players = beacon.Players,
            address = data.Address,
            port = data.Port,
            lastSeen = os.time(),
        }
    end

    self:FormatServerListUI()
end

function app:HandleStartServer()
    -- Data sent to everyone requesting LAN discovery
    self.lanDiscovery:SetBroadcastData({ Name = "Test server", Players = 100 })
    if self.lanDiscovery:Start(SERVER_PORT) then
        self.startServer:SetVisible(false)
        self.stopServer:SetVisible(true)
    end
end

function app:HandleStopServer()
    self.lanDiscovery:Stop()
    self.startServer:SetVisible(true)
    self.stopServer:SetVisible(false)
end

function app:HandleDoNetworkDiscovery()
    -- Pass in the port that should be checked
    self.lanDiscovery:Start(SERVER_PORT)
    self.serverList:SetText("")
end

function app:Update(timeStep)
    -- Delete expired servers after 10s of not seeing an announcement
    local expired = false
    local now = os.time()
    for name, item in pairs(self.serverListItems) do
        if now - item.lastSeen > 10 then
            self.serverListItems[name] = nil
            expired = true
        end
    end
    if expired then
        self:FormatServerListUI()
    end
end

function app:FormatServerListUI()
    local text = ""
    for _, item in pairs(self.serverListItems) do
        text = text .. string.format("\n%s (%s) %s:%s", item.name, item.players, item.address, item.port)
    end
    self.serverList:SetText(text)
end

app:Run()

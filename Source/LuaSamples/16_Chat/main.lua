-- LuaSamples/16_Chat/main.lua
-- Lua port of Source/Samples/16_Chat: a simple networked chat. Start a
-- server on one instance, connect from another, and exchange text messages
-- via unreliable-friendly VectorBuffer packets (sent reliable + ordered).

local MSG_CHAT = MSG.USER + 0
local CHAT_SERVER_PORT = 2345

local app = Sample:new()
app.chatHistory = {}

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.FREE)
    input:SetMouseVisible(true)

    self:CreateUI()
    self:SubscribeToEvents()
end

function app:CreateUI()
    local graphics = GetSubsystem("Graphics")
    local ui = GetSubsystem("UI")
    local root = ui:GetRoot()

    -- Set style to the UI root so that elements will inherit it
    root:SetDefaultStyle(GetResource("XMLFile", "UI/DefaultStyle.xml"))

    self.chatHistoryText = root:CreateChild("Text")
    self.chatHistoryText:SetFont("Fonts/Anonymous Pro.ttf", 12)

    -- Bottom button container
    local buttonContainer = root:CreateChild("UIElement")
    buttonContainer:SetFixedSize(graphics:GetWidth(), 20)
    buttonContainer:SetPosition(0, graphics:GetHeight() - 20)
    buttonContainer:SetLayoutMode(LM.HORIZONTAL)
    self.buttonContainer = buttonContainer

    self.textEdit = buttonContainer:CreateChild("LineEdit")
    self.textEdit:SetStyleAuto()

    self.sendButton = self:CreateButton("Send", 70)
    self.connectButton = self:CreateButton("Connect", 90)
    self.disconnectButton = self:CreateButton("Disconnect", 100)
    self.startServerButton = self:CreateButton("Start Server", 110)

    self:UpdateButtons()

    local rowHeight = self.chatHistoryText:GetRowHeight()
    -- Row height would be zero if the font failed to load
    if rowHeight > 0 then
        local numberOfRows = math.floor((graphics:GetHeight() - 100) / rowHeight)
        for i = 1, numberOfRows do
            self.chatHistory[i] = ""
        end
    end

    -- No viewports or scene is defined. However, the default zone's fog
    -- color controls the fill color
    GetSubsystem("Renderer"):GetDefaultZone():SetFogColor(Color(0.0, 0.0, 0.1))
end

function app:SubscribeToEvents()
    -- Subscribe to UI element events
    SubscribeToEvent(self.textEdit, "TextFinished", function(data) self:HandleSend() end)
    SubscribeToEvent(self.sendButton, "Released", function(data) self:HandleSend() end)
    SubscribeToEvent(self.connectButton, "Released", function(data) self:HandleConnect() end)
    SubscribeToEvent(self.disconnectButton, "Released", function(data) self:HandleDisconnect() end)
    SubscribeToEvent(self.startServerButton, "Released", function(data) self:HandleStartServer() end)

    -- Subscribe to log messages so that we can pipe them to the chat window
    SubscribeToEvent("LogMessage", function(data)
        self:ShowChatText(data.Message)
    end)

    -- Subscribe to network events
    SubscribeToEvent("NetworkMessage", function(data) self:HandleNetworkMessage(data) end)
    SubscribeToEvent("ServerConnected", function(data) self:UpdateButtons() end)
    SubscribeToEvent("ServerDisconnected", function(data) self:UpdateButtons() end)
    SubscribeToEvent("ConnectFailed", function(data) self:UpdateButtons() end)
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

function app:ShowChatText(row)
    table.remove(self.chatHistory, 1)
    self.chatHistory[#self.chatHistory + 1] = row

    -- Concatenate all the rows in history
    local allRows = ""
    for _, line in ipairs(self.chatHistory) do
        allRows = allRows .. line .. "\n"
    end

    self.chatHistoryText:SetText(allRows)
end

function app:UpdateButtons()
    local network = GetSubsystem("Network")
    local serverConnection = network:GetServerConnection()
    local serverRunning = network:IsServerRunning()

    -- Show and hide buttons so that eg. Connect and Disconnect are never
    -- shown at the same time
    self.sendButton:SetVisible(serverConnection ~= nil)
    self.connectButton:SetVisible(not serverConnection and not serverRunning)
    self.disconnectButton:SetVisible(serverConnection ~= nil or serverRunning)
    self.startServerButton:SetVisible(not serverConnection and not serverRunning)
end

function app:HandleSend()
    local text = self.textEdit:GetText()
    if text == "" then
        return -- Do not send an empty message
    end

    local network = GetSubsystem("Network")
    local serverConnection = network:GetServerConnection()

    if serverConnection then
        -- A VectorBuffer object is convenient for constructing a message to
        -- send
        local msg = VectorBuffer()
        msg:WriteString(text)
        -- Send the chat message as in-order and reliable
        serverConnection:SendMessage(MSG_CHAT, msg)
        -- Empty the text edit after sending
        self.textEdit:SetText("")
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

    -- Connect to server, do not specify a client scene as we are not using
    -- scene replication, just messages
    network:Connect(address .. ":" .. CHAT_SERVER_PORT, nil)

    self:UpdateButtons()
end

function app:HandleDisconnect()
    local network = GetSubsystem("Network")
    local serverConnection = network:GetServerConnection()
    -- If we were connected to server, disconnect
    if serverConnection then
        serverConnection:Disconnect()
    -- Or if we were running a server, stop it
    elseif network:IsServerRunning() then
        network:StopServer()
    end

    self:UpdateButtons()
end

function app:HandleStartServer()
    local network = GetSubsystem("Network")
    network:StartServer(CHAT_SERVER_PORT)

    self:UpdateButtons()
end

function app:HandleNetworkMessage(data)
    local network = GetSubsystem("Network")

    if data.MessageID == MSG_CHAT then
        -- Use a MemoryBuffer to read the message data so that there is no
        -- unnecessary copying
        local msg = MemoryBuffer(data.Data)
        local text = msg:ReadString()

        -- If we are the server, prepend the sender's IP address and port and
        -- echo to everyone. If we are a client, just display the message
        if network:IsServerRunning() then
            local sender = data.Connection
            text = sender:ToString() .. " " .. text

            local sendMsg = VectorBuffer()
            sendMsg:WriteString(text)
            -- Broadcast as in-order and reliable
            network:BroadcastMessage(MSG_CHAT, sendMsg)
        end

        self:ShowChatText(text)
    end
end

app:Run()

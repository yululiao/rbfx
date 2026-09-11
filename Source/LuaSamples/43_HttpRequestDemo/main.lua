-- LuaSamples/43_HttpRequestDemo/main.lua
-- Lua port of Source/Samples/43_HttpRequestDemo: performs an asynchronous
-- HTTP GET to httpbin.org and displays the caller's IP parsed from the
-- JSON response.

local app = Sample:new()
app.httpRequest = nil

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.FREE)
    input:SetMouseVisible(true)

    self:CreateUI()
end

function app:CreateUI()
    local root = GetUIRoot()

    -- Construct new Text object
    local text = Text()
    self.text = text

    -- Set font and text color
    text:SetFont("Fonts/Anonymous Pro.ttf", 15)
    text:SetColor(Color(1.0, 1.0, 0.0))

    -- Align Text center-screen
    text:SetHorizontalAlignment(HA.CENTER)
    text:SetVerticalAlignment(VA.CENTER)

    root:AddChild(text)
end

function app:Update(timeStep)
    if not self.httpRequest then
        self.httpRequest = HttpRequest("https://httpbin.org/ip", "GET", { "hello: world" })
    else
        local state = self.httpRequest:GetState()
        -- Initializing HTTP request
        if state == HTTP.INITIALIZING then
            return
        -- An error has occurred
        elseif state == HTTP.ERROR then
            self.text:SetText("An error has occurred: " .. self.httpRequest:GetError())
            UnsubscribeEvent("Update")
        elseif state == HTTP.OPEN then
            self.text:SetText("Processing...")
        elseif state == HTTP.CLOSED then
            local message = self.httpRequest:ReadString()

            local json = JSONFile()
            json:FromString(message)

            local val = json:GetRoot():Get("origin")
            if val:IsNull() then
                self.text:SetText("Invalid JSON response retrieved!")
            else
                self.text:SetText("Your IP is: " .. val:GetString())
            end

            UnsubscribeEvent("Update")
        end
    end
end

app:Run()

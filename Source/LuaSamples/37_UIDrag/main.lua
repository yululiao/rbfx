-- LuaSamples/37_UIDrag/main.lua
-- Lua port of Source/Samples/37_UIDrag: 10 draggable buttons demonstrating
-- UI drag events (Click / DragBegin / DragMove / DragCancel), per-element
-- variables and multi-touch tracking. SPACE toggles tagged elements.

local app = Sample:new()

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.FREE)
    input:SetMouseVisible(true)

    self:CreateGUI()
    self:CreateInstructions()
end

function app:CreateGUI()
    local root = GetUIRoot()
    -- Load the style sheet from xml
    root:SetDefaultStyle(GetResource("XMLFile", "UI/DefaultStyle.xml"))

    for i = 0, 9 do
        local b = root:CreateChild("Button")
        -- Reference a style from the style sheet loaded earlier
        b:SetStyleAuto()
        b:SetMinWidth(250)
        b:SetPosition(50 * i, 50 * i)

        -- Enable the bring-to-front flag and set the initial priority
        b:SetBringToFront(true)
        b:SetPriority(i)

        -- Vertical layout so the child text elements are aligned
        b:SetLayout(LM.VERTICAL, 20, IntRect(40, 40, 40, 40))
        local dragInfos = { "Num Touch", "Text", "Event Touch" }
        for _, name in ipairs(dragInfos) do
            b:CreateChild("Text", name):SetStyleAuto()
        end

        if i % 2 == 0 then
            b:AddTag("SomeTag")
        end

        SubscribeToEvent(b, "Click", function(data) self:HandleClick(data) end)
        SubscribeToEvent(b, "DragMove", function(data) self:HandleDragMove(data) end)
        SubscribeToEvent(b, "DragBegin", function(data) self:HandleDragBegin(data) end)
        SubscribeToEvent(b, "DragCancel", function(data) self:HandleDragCancel(data) end)
    end

    for i = 0, 9 do
        local t = root:CreateChild("Text", "Touch " .. i)
        t:SetStyleAuto()
        t:SetVisible(false)
        t:SetPriority(100) -- Higher priority than the buttons
    end
end

function app:CreateInstructions()
    local root = GetUIRoot()

    local instructionText = root:CreateChild("Text")
    instructionText:SetText("Drag on the buttons to move them around.\n" ..
        "Touch input allows also multi-drag.\n" ..
        "Press SPACE to show/hide tagged UI elements.")
    instructionText:SetFont("Fonts/Anonymous Pro.ttf", 15)
    instructionText:SetTextAlignment(HA.CENTER)

    -- Position the text relative to the screen center
    instructionText:SetHorizontalAlignment(HA.CENTER)
    instructionText:SetVerticalAlignment(VA.CENTER)
    instructionText:SetPosition(0, root:GetHeight() / 4)
end

function app:HandleClick(data)
    local element = data.Element
    element:BringToFront()
end

function app:HandleDragBegin(data)
    local element = data.Element

    local lx = data.X
    local ly = data.Y

    local p = element:GetPosition()
    element:SetVar("START", p)
    element:SetVar("DELTA", IntVector2(p.x - lx, p.y - ly))

    local buttons = data.Buttons
    element:SetVar("BUTTONS", buttons)

    local t = element:GetChild("Text")
    t:SetText("Drag Begin Buttons: " .. buttons)

    t = element:GetChild("Num Touch")
    t:SetText("Number of buttons: " .. data.NumButtons)
end

function app:HandleDragMove(data)
    local element = data.Element
    local buttons = data.Buttons
    local d = element:GetVar("DELTA")
    local X = data.X + d.x
    local Y = data.Y + d.y
    local BUTTONS = element:GetVar("BUTTONS")

    local t = element:GetChild("Event Touch")
    t:SetText("Drag Move Buttons: " .. buttons)

    if buttons == BUTTONS then
        element:SetPosition(IntVector2(X, Y))
    end
end

function app:HandleDragCancel(data)
    local element = data.Element
    local P = element:GetVar("START")
    element:SetPosition(P)
end

function app:Update(timeStep)
    local root = GetUIRoot()
    local input = GetSubsystem("Input")

    local n = input:GetNumTouches()
    for i = 0, n - 1 do
        local t = root:GetChild("Touch " .. i)
        local ts = input:GetTouch(i)
        t:SetText("Touch " .. ts.touchID)

        local pos = ts.position
        pos.y = pos.y - 30

        t:SetPosition(pos)
        t:SetVisible(true)
    end

    for i = n, 9 do
        local t = root:GetChild("Touch " .. i)
        t:SetVisible(false)
    end

    if input:GetKeyPress(KEY.SPACE) then
        local elements = root:GetChildrenWithTag("SomeTag")
        for _, element in ipairs(elements) do
            element:SetVisible(not element:IsVisible())
        end
    end
end

app:Run()

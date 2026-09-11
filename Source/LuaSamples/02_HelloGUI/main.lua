-- LuaSamples/02_HelloGUI/main.lua
-- Lua port of Source/Samples/02_HelloGUI: styled window with controls and a
-- draggable Fish sprite demonstrating drag events.

local app = Sample:new()

function app:OnStart()
    -- Enable OS cursor
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.FREE)
    input:SetMouseVisible(true)

    local ui = GetSubsystem("UI")
    self.uiRoot = ui:GetRoot()

    -- Load XML file containing default UI style sheet
    local style = GetResource("XMLFile", "UI/DefaultStyle.xml")
    self.uiRoot:SetDefaultStyle(style)

    self:InitWindow()
    self:InitControls()
    self:CreateDraggableFish()
end

function app:InitWindow()
    -- Create the Window and add it to the UI's root node
    local window = self.uiRoot:CreateChild("Window", "Window")
    self.window = window

    -- Set Window size and layout settings
    window:SetMinWidth(384)
    window:SetLayout(LM.VERTICAL, 6, IntRect(6, 6, 6, 6))
    window:SetAlignment(HA.CENTER, VA.CENTER)

    -- Create Window 'titlebar' container
    local titleBar = window:CreateChild("UIElement")
    titleBar:SetMinSize(0, 24)
    titleBar:SetVerticalAlignment(VA.TOP)
    titleBar:SetLayoutMode(LM.HORIZONTAL)

    -- Create the Window title Text
    local windowTitle = titleBar:CreateChild("Text", "WindowTitle")
    windowTitle:SetText("Hello GUI!")

    -- Create the Window's close button
    local buttonClose = titleBar:CreateChild("Button", "CloseButton")

    -- Apply styles
    window:SetStyleAuto()
    windowTitle:SetStyleAuto()
    buttonClose:SetStyle("CloseButton")

    -- Subscribe to buttonClose release (following a 'press') events
    SubscribeToEvent(buttonClose, "Released", function(data)
        GetSubsystem("Engine"):Exit()
    end)

    -- Subscribe also to all UI mouse clicks just to see where we have clicked
    SubscribeToEvent("UIMouseClick", function(data)
        self:HandleControlClicked(data)
    end)
end

function app:InitControls()
    -- Create a CheckBox, a Button and a LineEdit
    local checkBox = self.window:CreateChild("CheckBox", "CheckBox")
    local button = self.window:CreateChild("Button", "Button")
    button:SetMinHeight(24)
    local lineEdit = self.window:CreateChild("LineEdit", "LineEdit")
    lineEdit:SetMinHeight(24)

    -- Apply previously set default style
    checkBox:SetStyleAuto()
    button:SetStyleAuto()
    lineEdit:SetStyleAuto()
end

function app:CreateDraggableFish()
    local graphics = GetSubsystem("Graphics")

    -- Create a draggable Fish button
    local draggableFish = self.uiRoot:CreateChild("Button", "Fish")
    draggableFish:SetTexture(GetResource("Texture2D", "Textures/UrhoDecal.dds"))
    draggableFish:SetBlendMode(BLEND.ADD)
    draggableFish:SetSize(128, 128)
    draggableFish:SetPosition((graphics:GetWidth() - draggableFish:GetWidth()) / 2, 200)

    -- Add a tooltip to Fish button
    local toolTip = draggableFish:CreateChild("ToolTip")
    toolTip:SetPosition(IntVector2(draggableFish:GetWidth() + 5, draggableFish:GetWidth() / 2))
    local textHolder = toolTip:CreateChild("BorderImage")
    textHolder:SetStyle("ToolTipBorderImage")
    local toolTipText = textHolder:CreateChild("Text")
    toolTipText:SetStyle("ToolTipText")
    toolTipText:SetText("Please drag me!")

    -- Subscribe draggableFish to Drag Events (in order to make it draggable)
    self.dragBeginPosition = IntVector2(0, 0)
    SubscribeToEvent(draggableFish, "DragBegin", function(data)
        -- Get UIElement relative position where input (touch or click) occurred
        self.dragBeginPosition = IntVector2(data.ElementX, data.ElementY)
    end)
    SubscribeToEvent(draggableFish, "DragMove", function(data)
        local dragCurrentPosition = IntVector2(data.X, data.Y)
        data.Element:SetPosition(dragCurrentPosition - self.dragBeginPosition)
    end)
end

function app:HandleControlClicked(data)
    -- Get the Text control acting as the Window's title
    local windowTitle = self.window:GetChild("WindowTitle", true)

    -- Get control that was clicked
    local clicked = data.Element
    local name = "...?"
    if clicked then
        name = clicked:GetName()
    end

    -- Update the Window's title text
    windowTitle:SetText("Hello " .. name .. "!")
end

app:Run()

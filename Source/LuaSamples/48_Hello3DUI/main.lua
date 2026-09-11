-- LuaSamples/48_Hello3DUI/main.lua
-- Lua port of Source/Samples/48_Hello3DUI: a windowed UI with a list, a
-- draggable fish button, and a 3D UI rendered on a rotating cube. TAB moves
-- the window between screen and cube texture, SPACE toggles rotation,
-- F2 toggles UI debug drawing.

local app = Sample:new()
app.dragBeginPosition = IntVector2(0, 0)
app.animateCube = true
app.renderOnCube = false
app.drawDebug = false
app.currentElement = nil

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.FREE)
    input:SetMouseVisible(true)

    local root = GetUIRoot()
    root:SetDefaultStyle(GetResource("XMLFile", "UI/DefaultStyle.xml"))

    self:InitScene()
    self:InitWindow()
    self:InitControls()
    self:CreateDraggableFish()
    self:Init3DUI()
end

function app:InitScene()
    self.scene = CreateScene()
    local scene = self.scene

    scene:CreateComponent("Octree")
    local zone = scene:CreateComponent("Zone")
    zone:SetBoundingBox(BoundingBox(-1000.0, 1000.0))
    zone:SetFogColor(Color.GRAY)
    zone:SetFogStart(100.0)
    zone:SetFogEnd(300.0)

    -- Child scene node at world origin with a StaticModel, hidden initially
    local boxNode = scene:CreateChild("Box")
    boxNode:SetScale(Vector3(5.0, 5.0, 5.0))
    boxNode:SetRotation(Quaternion(90.0, Vector3.LEFT))
    local boxModel = boxNode:CreateComponent("StaticModel")
    boxModel:SetModel(GetResource("Model", "Models/Box.mdl"))
    boxNode:SetEnabled(false)

    -- Camera
    self.cameraNode = scene:CreateChild("Camera")
    self.cameraNode:CreateComponent("Camera")
    self.cameraNode:SetPosition(Vector3(0.0, 0.0, -10.0))

    SetViewport(0, scene, self.cameraNode:GetComponent("Camera"))
end

function app:InitWindow()
    local root = GetUIRoot()

    -- Window with vertical layout, centered on screen
    self.window = root:CreateChild("Window")
    self.window:SetMinWidth(384)
    self.window:SetLayout(LM.VERTICAL, 6, IntRect(6, 6, 6, 6))
    self.window:SetAlignment(HA.CENTER, VA.CENTER)
    self.window:SetName("Window")

    -- Window 'titlebar' container
    local titleBar = self.window:CreateChild("UIElement")
    titleBar:SetMinSize(0, 24)
    titleBar:SetVerticalAlignment(VA.TOP)
    titleBar:SetLayout(LM.HORIZONTAL)

    -- Window title Text and close button
    local windowTitle = self.window:CreateChild("Text", "WindowTitle")
    windowTitle:SetText("Hello GUI!")
    local buttonClose = self.window:CreateChild("Button", "CloseButton")
    titleBar:AddChild(windowTitle)
    titleBar:AddChild(buttonClose)

    -- Scrollable list with 32 items
    local list = self.window:CreateChild("ListView")
    list:SetSelectOnClickEnd(true)
    list:SetHighlightMode(HM.ALWAYS)
    list:SetMinHeight(200)

    for i = 0, 31 do
        local text = Text()
        list:AddItem(text)
        text:SetStyleAuto()
        text:SetText("List item " .. i)
        text:SetName("Item " .. i)
    end

    -- Apply styles
    self.window:SetStyleAuto()
    list:SetStyleAuto()
    windowTitle:SetStyleAuto()
    buttonClose:SetStyle("CloseButton")

    -- Close button ends the sample
    SubscribeToEvent(buttonClose, "Released", function()
        GetSubsystem("Engine"):Exit()
    end)

    -- Track UI clicks to update the window title
    SubscribeToEvent("UIMouseClick", function(data)
        self:HandleControlClicked(data)
    end)
end

function app:InitControls()
    -- CheckBox, Button and LineEdit added to the window
    local checkBox = self.window:CreateChild("CheckBox", "CheckBox")
    local button = self.window:CreateChild("Button", "Button")
    button:SetMinHeight(24)
    local lineEdit = self.window:CreateChild("LineEdit", "LineEdit")
    lineEdit:SetMinHeight(24)

    checkBox:SetStyleAuto()
    button:SetStyleAuto()
    lineEdit:SetStyleAuto()

    local instructions = Text()
    instructions:SetStyleAuto()
    instructions:SetText("[TAB]   - toggle between rendering on screen or cube.\n"
        .. "[Space] - toggle cube rotation.\n"
        .. "[F2] - toggle UI debug drawing.")
    GetUIRoot():AddChild(instructions)
end

function app:CreateDraggableFish()
    local root = GetUIRoot()
    local graphics = GetSubsystem("Graphics")

    -- Draggable Fish button with additive blending
    local draggableFish = root:CreateChild("Button")
    draggableFish:SetTexture(GetResource("Texture2D", "Textures/UrhoDecal.dds"))
    draggableFish:SetBlendMode(BLEND.ADD)
    draggableFish:SetSize(128, 128)
    draggableFish:SetPosition((graphics:GetWidth() - draggableFish:GetWidth()) / 2, 200)
    draggableFish:SetName("Fish")

    -- Tooltip: bordered image with a text, shown on hover
    local toolTip = draggableFish:CreateChild("ToolTip")
    toolTip:SetPosition(IntVector2(draggableFish:GetWidth() + 5, draggableFish:GetWidth() / 2))
    local textHolder = toolTip:CreateChild("BorderImage")
    textHolder:SetStyle("ToolTipBorderImage")
    local toolTipText = textHolder:CreateChild("Text")
    toolTipText:SetStyle("ToolTipText")
    toolTipText:SetText("Please drag me!")

    -- Drag events make the fish draggable
    SubscribeToEvent(draggableFish, "DragBegin", function(data)
        self.dragBeginPosition = IntVector2(data.ElementX, data.ElementY)
    end)
    SubscribeToEvent(draggableFish, "DragMove", function(data)
        local dragCurrentPosition = IntVector2(data.X, data.Y)
        data.Element:SetPosition(dragCurrentPosition - self.dragBeginPosition)
    end)
end

function app:HandleControlClicked(data)
    -- Update the window title with the name of the clicked control
    local windowTitle = self.window:GetChild("WindowTitle", true)
    local clicked = data.Element
    local name = "...?"
    if clicked then
        name = clicked:GetName()
    end
    windowTitle:SetText("Hello " .. name .. "!")
end

function app:Init3DUI()
    -- UIComponent renders a UI subtree on the cube's material
    local boxNode = self.scene:GetChild("Box")
    local component = boxNode:CreateComponent("UIComponent")
    -- Unlit technique so the cube shows without lights
    component:GetMaterial():SetTechnique(0, GetResource("Technique", "Techniques/DiffUnlit.xml"))
    -- Root element size equals the texture size
    self.textureRoot = component:GetRoot()
    self.textureRoot:SetSize(512, 512)
end

function app:Update(timeStep)
    local input = GetSubsystem("Input")
    local ui = GetSubsystem("UI")
    local node = self.scene:GetChild("Box")

    if self.currentElement and self.drawDebug then
        ui:DebugDraw(self.currentElement)
    end

    if input:GetMouseButtonPress(MOUSEB.LEFT) then
        self.currentElement = ui:GetElementAt(input:GetMousePosition())
    end

    -- Toggle between rendering the window on screen or on the cube
    if input:GetKeyPress(KEY.TAB) then
        self.renderOnCube = not self.renderOnCube
        if self.renderOnCube then
            node:SetEnabled(true)
            self.textureRoot:AddChild(self.window)
        else
            node:SetEnabled(false)
            GetUIRoot():AddChild(self.window)
        end
    end

    if input:GetKeyPress(KEY.SPACE) then
        self.animateCube = not self.animateCube
    end

    if input:GetKeyPress(KEY.F2) then
        self.drawDebug = not self.drawDebug
    end

    if self.animateCube then
        node:Yaw(6.0 * timeStep * 1.5)
        node:Roll(-6.0 * timeStep * 1.5)
        node:Pitch(-6.0 * timeStep * 1.5)
    end
end

app:Run()

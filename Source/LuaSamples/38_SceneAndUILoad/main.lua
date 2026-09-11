-- LuaSamples/38_SceneAndUILoad/main.lua
-- Lua port of Source/Samples/38_SceneAndUILoad: loads a scene and a UI layout
-- from XML files prepared in the editor. RMB hides the cursor for camera
-- control; two UI buttons toggle the scene lights.

local app = Sample:new()

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.RELATIVE)
    input:SetMouseVisible(false)

    self:CreateScene()
    self:CreateUI()
end

function app:CreateScene()
    self.scene = CreateScene()
    local scene = self.scene

    -- Load scene content prepared in the editor (XML format)
    scene:LoadXML("Scenes/SceneLoadExample.xml")

    -- Create the camera (not included in the scene file)
    self.cameraNode = scene:CreateChild("Camera")
    self.cameraNode:CreateComponent("Camera")

    -- Set an initial position for the camera scene node above the plane
    self.cameraNode:SetPosition(Vector3(0.0, 2.0, -10.0))

    SetViewport(0, scene, self.cameraNode:GetComponent("Camera"))
end

function app:CreateUI()
    local ui = GetSubsystem("UI")
    local root = GetUIRoot()

    -- Set up global UI style into the root UI element
    root:SetDefaultStyle(GetResource("XMLFile", "UI/DefaultStyle.xml"))

    -- Create a Cursor UI element: when hidden the mouse controls the camera,
    -- when visible it interacts with the UI
    local cursor = Cursor()
    cursor:SetStyleAuto()
    ui:SetCursor(cursor)
    -- Set starting position of the cursor at the rendering window center
    local graphics = GetSubsystem("Graphics")
    cursor:SetPosition(graphics:GetWidth() / 2, graphics:GetHeight() / 2)

    -- Load UI content prepared in the editor and add to the UI hierarchy
    local layoutRoot = ui:LoadLayout(GetResource("XMLFile", "UI/UILoadExample.xml"))
    root:AddChild(layoutRoot)

    -- Subscribe to button actions (toggle scene lights when pressed then released)
    local button = layoutRoot:GetChild("ToggleLight1", true)
    if button then
        SubscribeToEvent(button, "Released", function() self:ToggleLight("Light1") end)
    end
    button = layoutRoot:GetChild("ToggleLight2", true)
    if button then
        SubscribeToEvent(button, "Released", function() self:ToggleLight("Light2") end)
    end
end

function app:Update(timeStep)
    self:MoveCamera(timeStep)
end

function app:MoveCamera(timeStep)
    -- Right mouse button controls mouse cursor visibility: hide when pressed
    local ui = GetSubsystem("UI")
    local input = GetSubsystem("Input")
    ui:GetCursor():SetVisible(not input:GetMouseButtonDown(MOUSEB.RIGHT))

    -- Do not move if the UI has a focused element
    if ui:GetFocusElement() then
        return
    end

    local MOVE_SPEED = 20.0
    local MOUSE_SENSITIVITY = 0.1

    -- Only rotate the camera when the cursor is hidden
    if not ui:GetCursor():IsVisible() then
        local mouseMove = input:GetMouseMove()
        self.yaw = (self.yaw or 0.0) + MOUSE_SENSITIVITY * mouseMove.x
        self.pitch = (self.pitch or 0.0) + MOUSE_SENSITIVITY * mouseMove.y
        self.pitch = Clamp(self.pitch, -90.0, 90.0)

        -- New orientation from yaw and pitch, roll fixed to zero
        self.cameraNode:SetRotation(Quaternion(self.pitch, self.yaw, 0.0))
    end

    -- Read WASD keys and move the camera scene node
    if input:GetKeyDown(KEY.W) then
        self.cameraNode:Translate(Vector3(0.0, 0.0, 1.0) * MOVE_SPEED * timeStep)
    end
    if input:GetKeyDown(KEY.S) then
        self.cameraNode:Translate(Vector3(0.0, 0.0, -1.0) * MOVE_SPEED * timeStep)
    end
    if input:GetKeyDown(KEY.A) then
        self.cameraNode:Translate(Vector3(-1.0, 0.0, 0.0) * MOVE_SPEED * timeStep)
    end
    if input:GetKeyDown(KEY.D) then
        self.cameraNode:Translate(Vector3(1.0, 0.0, 0.0) * MOVE_SPEED * timeStep)
    end
end

function app:ToggleLight(lightName)
    local lightNode = self.scene:GetChild(lightName, true)
    if lightNode then
        lightNode:SetEnabled(not lightNode:IsEnabled())
    end
end

app:Run()

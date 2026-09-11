-- LuaSamples/08_Decals/main.lua
-- Lua port of Source/Samples/08_Decals: paint decals onto scene geometry
-- with the mouse. RMB hides the cursor and rotates the view, LMB paints.

local app = Sample:new()
app.drawDebug = false

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.RELATIVE)
    input:SetMouseVisible(false)

    self:CreateScene()
    self:CreateUI()
end

function app:CreateScene()
    local scene = CreateScene()
    self.scene = scene

    -- Create octree + DebugRenderer
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
    -- Set cascade splits at 10, 50 and 200 world units, fade shadows out at
    -- 80% of maximum shadow distance
    light:SetShadowCascade(10.0, 50.0, 200.0, 0.0, 0.8)

    -- Create some mushrooms
    local NUM_MUSHROOMS = 240
    for i = 1, NUM_MUSHROOMS do
        local mushroomNode = scene:CreateChild("Mushroom")
        mushroomNode:SetPosition(Vector3(Random(90.0) - 45.0, 0.0, Random(90.0) - 45.0))
        mushroomNode:SetRotation(Quaternion(0.0, Random(360.0), 0.0))
        mushroomNode:SetScale(0.5 + Random(2.0))
        local mushroomObject = mushroomNode:CreateComponent("StaticModel")
        mushroomObject:SetModel(GetResource("Model", "Models/Mushroom.mdl"))
        mushroomObject:SetMaterial(GetResource("Material", "Materials/Mushroom.xml"))
        mushroomObject:SetCastShadows(true)
    end

    -- Create randomly sized boxes. If boxes are big enough, make them
    -- occluders. Occluders will be software rasterized before rendering to a
    -- low-resolution depth-only buffer to test the objects in the view
    -- frustum for visibility
    local NUM_BOXES = 20
    for i = 1, NUM_BOXES do
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

    -- Create the camera manually (no FreeFlyController: this sample drives
    -- the camera itself so the mouse can be shared with the UI cursor)
    local cameraNode = scene:CreateChild("Camera")
    local camera = cameraNode:CreateComponent("Camera")
    camera:SetFarClip(300.0)

    -- Set an initial position for the camera scene node above the plane
    cameraNode:SetPosition(Vector3(0.0, 5.0, 0.0))

    self.cameraNode = cameraNode
    SetViewport(0, scene, camera)
end

function app:CreateUI()
    local ui = GetSubsystem("UI")

    -- Create a Cursor UI element because we want to be able to hide and show
    -- it at will. When hidden, the mouse cursor will control the camera, and
    -- when visible, it will point the raycast target
    local uiRoot = ui:GetRoot()
    uiRoot:SetDefaultStyle(GetResource("XMLFile", "UI/DefaultStyle.xml"))
    local cursor = uiRoot:CreateChild("Cursor")
    cursor:SetStyleAuto()
    ui:SetCursor(cursor)
    -- Set starting position of the cursor at the rendering window center
    local graphics = GetSubsystem("Graphics")
    cursor:SetPosition(graphics:GetWidth() / 2, graphics:GetHeight() / 2)

    self:CreateInstructions("Use WASD keys to move\nLMB to paint decals, RMB to rotate view\n"
        .. "Space to toggle debug geometry")

    -- Request debug geometry rendering during post-render update
    SubscribeToEvent("PostRenderUpdate", function(data)
        if self.drawDebug then
            GetSubsystem("Renderer"):DrawDebugGeometry(false)
        end
    end)
end

function app:Update(timeStep)
    self:MoveCamera(timeStep)
end

function app:MoveCamera(timeStep)
    local ui = GetSubsystem("UI")
    local input = GetSubsystem("Input")

    -- Right mouse button controls mouse cursor visibility: hide when pressed
    local cursor = ui:GetCursor()
    cursor:SetVisible(not input:GetMouseButtonDown(MOUSEB.RIGHT))

    -- Do not move if the UI has a focused element (the console)
    if ui:GetFocusElement() then
        return
    end

    -- Movement speed as world units per second
    local MOVE_SPEED = 20.0
    -- Mouse sensitivity as degrees per pixel
    local MOUSE_SENSITIVITY = 0.1

    -- Use this frame's mouse motion to adjust camera node yaw and pitch.
    -- Clamp the pitch between -90 and 90 degrees.
    -- Only move the camera when the cursor is hidden
    if not cursor:IsVisible() then
        local mouseMove = input:GetMouseMove()
        self.yaw = self.yaw + MOUSE_SENSITIVITY * mouseMove.x
        self.pitch = self.pitch + MOUSE_SENSITIVITY * mouseMove.y
        self.pitch = Clamp(self.pitch, -90.0, 90.0)

        -- Construct new orientation for the camera scene node from yaw and
        -- pitch. Roll is fixed to zero
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

    -- Paint decal with the left mousebutton; cursor must be visible
    if cursor:IsVisible() and input:GetMouseButtonPress(MOUSEB.LEFT) then
        self:PaintDecal()
    end
end

function app:PaintDecal()
    local hit = self:Raycast(250.0)
    if not hit then
        return
    end

    -- Check if target scene node already has a DecalSet component. If not,
    -- create now
    local hitDrawable = hit.drawable
    local targetNode = hitDrawable:GetNode()
    local decal = targetNode:GetComponent("DecalSet")
    if not decal then
        decal = targetNode:CreateComponent("DecalSet")
        decal:SetMaterial(GetResource("Material", "Materials/UrhoDecal.xml"))
    end

    -- Add a square decal to the decal set using the geometry of the drawable
    -- that was hit, orient it to face the camera, use full texture UV's
    -- (0,0) to (1,1)
    decal:AddDecal(hitDrawable, hit.position, self.cameraNode.rotation,
        0.5, 1.0, 1.0, Vector2.ZERO, Vector2.ONE)
end

-- Perform a raycast for painting decals. Returns a hit table or nil.
function app:Raycast(maxDistance)
    local ui = GetSubsystem("UI")
    local pos = ui:GetUICursorPosition()

    -- Check the cursor is visible and there is no UI element in front of the cursor
    local cursor = ui:GetCursor()
    if not cursor:IsVisible() or ui:GetElementAt(pos, true) then
        return nil
    end

    local camera = self.cameraNode:GetComponent("Camera")
    local cameraRay = camera:GetScreenRayFromMouse()

    -- Pick only geometry objects, not eg. zones or lights, only get the
    -- first (closest) hit
    local octree = self.scene:GetComponent("Octree")
    return octree:RaycastSingle(cameraRay, maxDistance)
end

-- Toggle debug geometry with space
function app:OnKeyDown(key)
    if key == KEY.SPACE then
        self.drawDebug = not self.drawDebug
    end
end

app:Run()

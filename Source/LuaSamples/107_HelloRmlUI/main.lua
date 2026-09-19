-- LuaSamples/107_HelloRmlUI/main.lua
-- Lua port of Source/Samples/107_HelloRmlUI: an interactive RmlUI window (a
-- click-counting button, a slider that drives the document font size, an
-- animated progress bar and Variant / VariantVector / VariantMap data-model
-- binds), plus the same window rendered onto a spinning textured cube through an
-- off-screen RmlCanvasComponent.
--
-- The binding-worthy part is the interactive window. The C++ original subclasses
-- RmlUIComponent and binds its data model from the OnDataModelInitialized()
-- virtual. Lua cannot override a C++ virtual, so it instead uses the
-- LuaRmlUIComponent trampoline (Source/LuaScript/LuaRmlUIBindings.cpp), which
-- forwards the data-model callbacks to the handler table supplied below.

local app = Sample:new()

-- Mirror of the C++ SimpleWindow. Creates a LuaRmlUIComponent on `node`, wires
-- its data model through the trampoline, and returns a per-frame tick that
-- animates the progress bar (what the C++ Update() used to do).
local function CreateSimpleWindow(node)
    local window = node:CreateComponent("LuaRmlUIComponent")

    -- The state the C++ object kept in member fields lives in these locals.
    local sliderValue = 0
    local counter = 0
    local progress = 0

    window:SetHandlers({
        -- Invoked by the trampoline exactly while the RmlUi data model is being
        -- constructed, which is the only moment Bind* calls are legal.
        onInit = function(w)
            w:BindReadonlyProperty("counter", function() return counter end)
            w:BindProperty("slider_value",
                function() return sliderValue end,
                function(v) sliderValue = v end)
            w:BindProperty("font_size",
                function() return w:GetEmSize() end,
                function(v) w:SetEmSize(v) end)
            w:BindReadonlyProperty("progress", function() return progress end)

            -- The template's <button data-event-click="count"> fires this.
            w:BindEvent("count", function()
                counter = counter + 1
                w:MarkDirty("counter")
            end)

            -- Urho Variant / VariantVector / VariantMap display binds.
            w:BindVariant("variant")
            w:SetVariant("variant", "variant text")
            w:BindVariantVector("variant_vector")
            w:SetVariantVector("variant_vector", { 42, "vector text" })
            w:BindVariantMap("variant_map")
            w:SetVariantMap("variant_map", { int = 24, str = "map text" })
        end,
    })

    -- SetResource opens the document, which triggers onInit above.
    window:SetResource("UI/HelloRmlUI.rml")

    local function Tick(elapsed)
        progress = (math.sin(elapsed * 50.0) + 1.0) * 0.5
        window:MarkDirty("progress")
    end

    return window, Tick
end

function app:OnStart()
    -- Scene: octree + a box model to hang the cube UI on + a camera. The Scene
    -- constructor already installs a default Zone, so nothing else is needed.
    local scene = CreateScene()
    self.scene = scene
    scene:CreateComponent("Octree")

    local boxNode = scene:CreateChild("Box")
    boxNode:SetScale(Vector3(5.0, 5.0, 5.0))
    boxNode:SetRotation(Quaternion(90.0, Vector3(-1.0, 0.0, 0.0)))
    local boxModel = boxNode:CreateComponent("StaticModel")
    boxModel:SetModel(GetResource("Model", "Models/Box.mdl"))

    local cameraNode = scene:CreateChild("Camera")
    cameraNode:CreateComponent("Camera")
    cameraNode:SetPosition(Vector3(0.0, 0.0, -10.0))
    SetViewport(0, scene, cameraNode:GetComponent("Camera"))

    self.elapsed = 0
    self.ticks = {}

    -- 1) A window rendered straight into the back buffer (attached to the scene).
    local _, mainTick = CreateSimpleWindow(scene)
    self.ticks[#self.ticks + 1] = mainTick

    -- 2) A window rendered onto the cube through an off-screen canvas texture.
    local canvas = boxNode:CreateComponent("RmlCanvasComponent")
    canvas:SetUISize(IntVector2(512, 512))
    canvas:SetRemapMousePos(true)

    local material = Material()
    material:SetTechnique(0, GetResource("Technique", "Techniques/DiffUnlit.xml"))
    material:SetTexture("Albedo", canvas:GetTexture())
    boxNode:GetComponent("StaticModel"):SetMaterial(material)

    local _, cubeTick = CreateSimpleWindow(boxNode)
    self.ticks[#self.ticks + 1] = cubeTick

    -- This sample uses a free cursor to interact with the UI.
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.FREE)
    input:SetMouseVisible(true)
end

function app:Update(timeStep)
    self.elapsed = self.elapsed + timeStep
    for _, tick in ipairs(self.ticks) do
        tick(self.elapsed)
    end

    -- Animate the cube the UI is rendered onto.
    local node = self.scene:GetChild("Box")
    if node then
        node:Yaw(6.0 * timeStep * 1.5)
        node:Roll(-6.0 * timeStep * 1.5)
        node:Pitch(-6.0 * timeStep * 1.5)
    end
end

-- Extra key handling beyond the common ESC/F11/9 handled by Framework.lua.
function app:OnKeyDown(key)
    if key == KEY.F9 then
        local ui = GetSubsystem("RmlUI")
        ui:SetDebuggerVisible(not ui:IsDebuggerVisible())
    end
end

app:Run()

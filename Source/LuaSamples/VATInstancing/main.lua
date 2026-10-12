-- LuaSamples/VATInstancing/main.lua
--
-- Engine-level verification scene for the VAT + GPU-instancing capability.
--
-- It exercises the full pipeline end to end on real shipped assets:
--   1. VATBaker (C++ core) bakes a skinned Model + one Animation clip into a
--      static VAT geometry plus RGBA32F position/normal textures, at startup.
--   2. ONE InstancedVATModel per baked geometry slot holds the whole crowd;
--      every character is a per-instance world transform + a normalized
--      animation time, so the entire group collapses to a single draw call per
--      pass (this is the batching the capability is about).
--   3. A control crowd of N independent AnimatedModel components (toggled with
--      'T') reproduces the classic per-drawable path -- N drawables, N skinned
--      batches -- for a direct A/B comparison of draw calls, framerate and the
--      shadow/lighting result.
--
-- Verification checklist (run on a machine with a GPU; the headless CI cannot
-- open a window):
--   * VAT mode: the whole field animates, casts shadows and is lit correctly
--     while the renderer issues one geometry batch per material/pass.
--   * 'T' to switch to the AnimatedModel control crowd: identical look, but the
--     per-frame cost scales with the instance count (no batching).
--   * The startup LogInfo lines report slot/frame/duration/instance counts so a
--     successful bake + instancing is unambiguous in the engine log.
--   * 'Space' toggles debug geometry (world bounding boxes of the crowd).

local app = Sample:new()
app.drawDebug = false
app.useVAT = true -- false switches to the AnimatedModel control crowd

-- Tunables ---------------------------------------------------------------
local NUM_INSTANCES   = 200
local GRID_COLS       = 14
local GRID_SPACING    = 2.6
local VAT_FPS         = 30.0
local MODEL_PATH      = "Models/Kachujin/Kachujin.mdl"
local ANIM_PATH       = "Models/Kachujin/Kachujin_Walk.ani"
local MATERIAL_PATH   = "Models/Kachujin/Materials/Kachujin.xml"
--------------------------------------------------------------------------

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.RELATIVE)
    input:SetMouseVisible(false)

    self:CreateScene()

    -- Bake the VAT once from the shipped skinned assets.
    if not self:BakeVAT() then
        self.useVAT = false -- fall back to the control crowd if the bake failed
    end

    self:BuildVCrowd()
    self:BuildControlCrowd()
    self:ApplyMode()

    self:CreateInstructions(
        "WASD + mouse to move  |  T: toggle VAT / AnimatedModel control  |  Space: debug geometry\n" ..
        self.statusLine)

    SubscribeToEvent("PostRenderUpdate", function(data)
        if self.drawDebug then
            GetSubsystem("Renderer"):DrawDebugGeometry(false)
        end
    end)
end

function app:CreateScene()
    local scene = CreateScene()
    self.scene = scene

    scene:CreateComponent("Octree")
    scene:CreateComponent("DebugRenderer")

    -- Ground plane to receive shadows.
    local planeNode = scene:CreateChild("Plane")
    planeNode:SetScale(Vector3(60.0, 1.0, 60.0))
    local plane = planeNode:CreateComponent("StaticModel")
    plane:SetModel(GetResource("Model", "Models/Plane.mdl"))
    plane:SetMaterial(GetResource("Material", "Materials/StoneTiled.xml"))
    plane:SetCastShadows(false)

    -- Zone for ambient/fog.
    local zoneNode = scene:CreateChild("Zone")
    local zone = zoneNode:CreateComponent("Zone")
    zone:SetBoundingBox(BoundingBox(-1000.0, 1000.0))
    zone:SetAmbientColor(Color(0.5, 0.5, 0.5))
    zone:SetFogColor(Color(0.4, 0.5, 0.8))
    zone:SetFogStart(100.0)
    zone:SetFogEnd(300.0)

    -- Directional light with cascaded shadows: exercises the VAT path in the
    -- Depth/Shadow passes (baked normals must light and occlude correctly).
    local lightNode = scene:CreateChild("DirectionalLight")
    lightNode:SetDirection(Vector3(0.6, -1.0, 0.8))
    local light = lightNode:CreateComponent("Light")
    light:SetLightType(LIGHT.DIRECTIONAL)
    light:SetCastShadows(true)
    light:SetColor(Color(0.6, 0.6, 0.6))
    light:SetShadowBias(0.00025, 0.5)
    light:SetShadowCascade(10.0, 50.0, 200.0, 0.0, 0.8)

    -- Camera.
    local cameraNode = scene:CreateChild("Camera")
    cameraNode:CreateComponent("FreeFlyController")
    local camera = cameraNode:CreateComponent("Camera")
    camera:SetFarClip(300.0)
    cameraNode:SetPosition(Vector3(0.0, 6.0, -24.0))
    self.cameraNode = cameraNode

    SetViewport(0, scene, camera)
end

-- Grid layout shared by both crowds so the A/B comparison is apples-to-apples.
function app:InstancePose(index)
    local col = (index - 1) % GRID_COLS
    local row = math.floor((index - 1) / GRID_COLS)
    local half = GRID_COLS * GRID_SPACING * 0.5
    local x = col * GRID_SPACING - half
    local z = row * GRID_SPACING
    local yaw = ((index * 37) % 360) - 180 -- deterministic facing spread
    return Vector3(x, 0.0, z), Quaternion(0.0, yaw, 0.0), Vector3(1.0, 1.0, 1.0)
end

-- Run the C++ VATBaker over the skinned model + clip. Produces, per geometry
-- slot, a baked static VAT geometry + position/normal textures + material.
function app:BakeVAT()
    local model = GetResource("Model", MODEL_PATH)
    local animation = GetResource("Animation", ANIM_PATH)
    if not model or not animation then
        LogError("VATInstancing: missing model or animation asset")
        return false
    end

    local baker = VATBaker()
    if not baker:Bake(model, animation, VAT_FPS, 0.0) then
        LogError("VATInstancing: bake failed: " .. baker:GetLastError())
        return false
    end

    self.vatBaker = baker
    self.vatSlots = baker:GetNumSlots()
    self.vatFrames = baker:GetFrames()
    self.vatDuration = baker:GetDuration()
    self.vatBounds = baker:GetLocalBoundingBox()
    self.vatModel = model
    self.sourceMaterial = GetResource("Material", MATERIAL_PATH)

    LogInfo(string.format(
        "VATInstancing: baked %d slot(s), %d frame(s), duration %.2fs (fps %.0f) from '%s' + '%s'",
        self.vatSlots, self.vatFrames, self.vatDuration, VAT_FPS, MODEL_PATH, ANIM_PATH))
    return true
end

-- Build the instanced VAT crowd: one InstancedVATModel per baked geometry slot,
-- each carrying the full instance set -> one geometry batch per pass.
function app:BuildVCrowd()
    if not self.vatBaker then
        return
    end
    self.vatNodes = {}
    for slot = 0, self.vatSlots - 1 do
        local node = self.scene:CreateChild("VATGroup_" .. slot)
        local comp = node:CreateComponent("InstancedVATModel")
        comp:SetGeometry(self.vatBaker:GetGeometry(slot))
        comp:SetMaterial(self.vatBaker:GetMaterial(slot, self.sourceMaterial))
        comp:SetAnimationDuration(self.vatDuration)
        comp:SetLocalBoundingBox(self.vatBounds)
        comp:SetCastShadows(true)

        for i = 1, NUM_INSTANCES do
            local pos, rot, scale = self:InstancePose(i)
            local stagger = ((i - 1) % 10) / 10.0 -- phase offset so the crowd is not in lockstep
            comp:AddInstanceTRS(pos, rot, scale, stagger, 1.0)
        end

        table.insert(self.vatNodes, node)
        LogInfo(string.format("VATInstancing: slot %d -> InstancedVATModel with %d instances (1 batch)",
            slot, comp:GetNumInstances()))
    end
end

-- Control crowd: the classic path, one AnimatedModel + AnimationController per
-- character, for a direct comparison (N skinned drawables, no instancing).
function app:BuildControlCrowd()
    local model = GetResource("Model", MODEL_PATH)
    local animation = GetResource("Animation", ANIM_PATH)
    local material = GetResource("Material", MATERIAL_PATH)
    if not model or not animation or not material then
        return
    end

    self.controlNodes = {}
    for i = 1, NUM_INSTANCES do
        local pos, rot, scale = self:InstancePose(i)
        local node = self.scene:CreateChild("Control_" .. i)
        node:SetPosition(pos)
        node:SetRotation(rot)
        node:SetScale(scale)

        local comp = node:CreateComponent("AnimatedModel")
        comp:SetModel(model)
        comp:SetMaterial(material)
        comp:SetCastShadows(true)

        local controller = node:CreateComponent("AnimationController")
        controller:PlayNewExclusive(animation, true, math.random() * animation:GetLength())

        table.insert(self.controlNodes, node)
    end
    LogInfo(string.format("VATInstancing: control crowd built (%d separate AnimatedModel drawables)", NUM_INSTANCES))
end

function app:ApplyMode()
    for _, node in ipairs(self.vatNodes or {}) do
        node:SetEnabled(self.useVAT)
    end
    for _, node in ipairs(self.controlNodes or {}) do
        node:SetEnabled(not self.useVAT)
    end
    self.statusLine = self.useVAT
        and string.format("Mode: VAT instancing (%d slots, %d frames, %d characters -> %d batch(es))",
            self.vatSlots or 0, self.vatFrames or 0, NUM_INSTANCES, self.vatSlots or 0)
        or string.format("Mode: AnimatedModel control (%d characters -> %d drawables)",
            NUM_INSTANCES, NUM_INSTANCES)
    LogInfo("VATInstancing: " .. self.statusLine)
end

function app:Update(timeStep)
    self:MoveCameraByMouse(0.1)
    self:MoveCamera(timeStep, 12.0)
end

function app:OnKeyDown(key)
    if key == string.byte("t") then
        self.useVAT = not self.useVAT
        self:ApplyMode()
    elseif key == KEY.SPACE then
        self.drawDebug = not self.drawDebug
    end
end

app:Run()

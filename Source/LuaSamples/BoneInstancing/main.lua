-- LuaSamples/BoneInstancing/main.lua
--
-- Engine-level verification scene for the bone-matrix-texture skinning + GPU-instancing
-- capability (the ".bonetex" runtime consumer). It mirrors VATInstancing but drives the
-- alternative skinning path: instead of pre-deformed vertex positions, the original skinned
-- geometry (per-vertex bone indices/weights) is kept and the pose is reconstructed in the
-- vertex shader by sampling a bone-matrix atlas.
--
-- It exercises the full runtime pipeline on the shipped Anne assets:
--   1. BoneMatrixBaker:Load reads a previously baked ".bonetex" archive (bone-matrix atlas
--      texture + clip table) at startup -- no CPU skinning, no live bone nodes.
--   2. ONE InstancedBoneModel per source model geometry holds the whole crowd. Every
--      character is a per-instance world transform + a clip index + phase; the CPU folds
--      those into a per-instance atlas frame row, so the entire group collapses to a single
--      geometry batch per pass. Instances playing *different* clips still batch together
--      because only the sampled atlas row differs.
--   3. The crowd is 30 characters in 3 rows of 10; each row plays a different clip of the
--      same skin (Death / Run / Sword) to prove mixed-clip instancing stays batched.
--
-- Verification checklist (run on a machine with a GPU; the headless CI cannot open a window):
--   * The whole 30-character field animates, casts shadows and is lit correctly while the
--     renderer issues ONE geometry batch per material/pass. Open the statistics overlay
--     (F1 / the engine debug HUD) and confirm "Batches" / "Draws" stay flat regardless of
--     the 30 characters -- this is the "expected 1 draw call" result.
--   * The three rows visibly run three different animations at once while still sharing the
--     one batch, which is the capability this consumer is about.
--   * The startup LogInfo lines report bone/frame/clip/instance counts so a successful
--     atlas load + instancing is unambiguous in the engine log.
--   * 'Space' toggles debug geometry (world bounding boxes of the crowd).

local app = Sample:new()
app.drawDebug = false

-- Tunables ---------------------------------------------------------------
local MODEL_PATH    = "Models/Quaternius_pirate-kit/Characters_Anne.mdl"
local BONETEX_PATH  = "Models/Quaternius_pirate-kit/Characters_Anne.bonetex"
local MATERIAL_PATH = "Materials/PirateAtlas.xml"

local NUM_INSTANCES = 30      -- total crowd size
local PER_CLIP      = 10      -- characters that share one clip -> three clip groups of 10
local GRID_COLS     = PER_CLIP
local GRID_SPACING  = 2.2

-- Clip slots as stored in the ".bonetex" archive (baked in alphabetical order):
--   0 = Death, 1 = Run, 2 = Sword
local CLIP_NAMES = { "Death", "Run", "Sword" }
--------------------------------------------------------------------------

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.RELATIVE)
    input:SetMouseVisible(false)

    self:CreateScene()

    if not self:LoadBoneAtlas() then
        LogError("BoneInstancing: could not load the bone-matrix atlas; aborting crowd")
        self:CreateInstructions("BoneInstancing: FAILED to load '" .. BONETEX_PATH .. "'")
        return
    end

    self:BuildCrowd()

    self:CreateInstructions(
        "WASD + mouse to move  |  Space: debug geometry\n" .. self.statusLine)

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

    -- Directional light with cascaded shadows: exercises the BONE path in the Depth/Shadow
    -- passes (atlas-sampled normals must light and occlude correctly).
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
    cameraNode:SetPosition(Vector3(0.0, 6.0, -30.0))
    self.cameraNode = cameraNode

    SetViewport(0, scene, camera)
end

-- Read the previously baked bone-matrix atlas. This is the pure runtime consumer:
-- texture + clip table only, no source Model/Animation needed for the skinning itself.
function app:LoadBoneAtlas()
    local baker = BoneMatrixBaker()
    if not baker:Load(BONETEX_PATH) then
        LogError("BoneInstancing: atlas load failed: " .. baker:GetLastError())
        return false
    end

    self.boneBaker = baker
    self.boneCount = baker:GetNumBones()
    self.atlasWidth = baker:GetWidth()
    self.atlasFrames = baker:GetFrames()

    LogInfo(string.format(
        "BoneInstancing: loaded atlas '%s' -> %d bones, atlas %dx%d (RGBA32F), %d clip(s): %s",
        BONETEX_PATH, self.boneCount, self.atlasWidth, self.atlasFrames,
        #CLIP_NAMES, table.concat(CLIP_NAMES, ", ")))
    return true
end

-- Grid pose: 10 columns per row, one clip per row (row index == clip index).
function app:InstancePose(index)
    local col = (index - 1) % GRID_COLS
    local row = math.floor((index - 1) / GRID_COLS)
    local half = GRID_COLS * GRID_SPACING * 0.5
    local x = col * GRID_SPACING - half
    local z = row * GRID_SPACING
    return Vector3(x, 0.0, z)
end

-- Build the instanced bone crowd: one InstancedBoneModel per source model geometry,
-- each carrying the full 30-instance set -> one geometry batch per pass.
function app:BuildCrowd()
    local model = GetResource("Model", MODEL_PATH)
    local material = GetResource("Material", MATERIAL_PATH)
    if not model or not material then
        LogError("BoneInstancing: missing model or material asset")
        return
    end

    self.boneModel = model
    self.boneBounds = model:GetBoundingBox()
    local numGeometries = model:GetNumGeometries()

    self.boneNodes = {}
    for g = 0, numGeometries - 1 do
        local node = self.scene:CreateChild("BoneGroup_" .. g)
        local comp = node:CreateComponent("InstancedBoneModel")
        -- The BONE path reuses the source skinned geometry directly (it already carries the
        -- per-vertex bone indices/weights); no rebuilt VAT-style geometry is required.
        comp:SetGeometry(model:GetGeometry(g, 0))
        comp:SetBoneAtlas(self.boneBaker)
        comp:SetMaterial(material)
        comp:SetLocalBoundingBox(self.boneBounds)
        comp:SetCastShadows(true)

        for i = 1, NUM_INSTANCES do
            local pos = self:InstancePose(i)
            local clip = math.floor((i - 1) / PER_CLIP) -- 0..2: Death / Run / Sword
            clip = math.min(clip, #CLIP_NAMES - 1)
            local stagger = ((i - 1) % PER_CLIP) / PER_CLIP -- phase spread within a row
            comp:AddInstanceTRS(pos, Quaternion(0.0, 0.0, 0.0), Vector3(1.0, 1.0, 1.0),
                clip, stagger, 1.0)
        end

        table.insert(self.boneNodes, node)
        LogInfo(string.format(
            "BoneInstancing: geometry %d/%d -> InstancedBoneModel with %d instances (1 batch, clips: %s)",
            g, numGeometries, comp:GetNumInstances(), table.concat(CLIP_NAMES, " / ")))
    end

    self.statusLine = string.format(
        "Mode: bone-matrix instancing | %d char -> %d batch(es) per pass (expected: 1 draw call per geometry/pass)",
        NUM_INSTANCES, numGeometries)
    LogInfo("BoneInstancing: " .. self.statusLine)
end

function app:Update(timeStep)
    self:MoveCameraByMouse(0.1)
    self:MoveCamera(timeStep, 12.0)
end

function app:OnKeyDown(key)
    if key == KEY.SPACE then
        self.drawDebug = not self.drawDebug
    end
end

app:Run()

-- LuaSamples/FlipbookInstancing/main.lua
--
-- Engine-level verification scene for the flipbook (baked sprite-sheet animation) consumer:
-- the ".flipbook" archive produced by the editor's "Bake Animation Flipbook..." on an FBX
-- holds one RGBA8 atlas texture (animation frames rendered offscreen) plus a clip table.
--
-- It exercises the full runtime pipeline:
--   1. FlipbookBaker:Load reads a previously baked ".flipbook" archive (atlas + clip table)
--      at startup -- no Model, no bones, no AnimationController, no skinning at all.
--   2. ONE BillboardSet holds the whole crowd of 30 characters. A BillboardSet is a natural
--      batch: all instances live in one vertex buffer drawn with a single draw call. Each
--      billboard carries its own world position, size and UV rect, so the flipbook player
--      just rewrites billboard.uv every frame to cut a different atlas cell -- mixed clips
--      across the crowd still share the one batch because only the UV differs.
--   3. The crowd is 30 billboards in 3 rows of 10; each row loops a different clip
--      (the first three clips of the archive) to prove mixed-clip playback stays batched.
--
-- Verification checklist (run on a machine with a GPU; the headless CI cannot open a window):
--   * The whole 30-character field animates while the renderer issues ONE batch. Open the
--     statistics overlay (F1 / the engine debug HUD) and confirm "Batches" / "Draws" stay
--     flat regardless of the 30 characters -- this is the "expected 1 draw call" result.
--   * The three rows visibly run three different animations at once while sharing the batch.
--   * The startup LogInfo lines report clip/atlas counts so a successful load is
--     unambiguous in the engine log.
--   * 'Space' toggles debug geometry (world bounding box of the crowd).

local app = Sample:new()
app.drawDebug = false

-- Tunables ---------------------------------------------------------------
local FLIPBOOK_PATH = "Models/Quaternius_pirate-kit/Characters_Anne.flipbook"
local TECHNIQUE_PATH = "Techniques/DiffVColUnlitAlpha.xml"

local NUM_INSTANCES = 30      -- total crowd size
local PER_CLIP      = 10      -- billboards that share one clip -> three clip groups of 10
local GRID_COLS     = PER_CLIP
local GRID_SPACING  = 2.2
local BILLBOARD_W   = 2.0     -- world size of one character card (square atlas cell)
local BILLBOARD_H   = 2.0
--------------------------------------------------------------------------

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.RELATIVE)
    input:SetMouseVisible(false)

    self.flowTime = 0.0

    self:CreateScene()

    if not self:LoadFlipbook() then
        LogError("FlipbookInstancing: could not load the flipbook atlas; aborting crowd")
        self:CreateInstructions("FlipbookInstancing: FAILED to load '" .. FLIPBOOK_PATH
            .. "' -- bake it first (editor: right-click Characters_Anne.fbx -> Bake Animation Flipbook...)")
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

    -- Ground plane (billboards cast no shadows; the cards simply stand on it).
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
    zone:SetAmbientColor(Color(0.9, 0.9, 0.9))
    zone:SetFogColor(Color(0.4, 0.5, 0.8))
    zone:SetFogStart(100.0)
    zone:SetFogEnd(300.0)

    -- Camera looking at the crowd.
    local cameraNode = scene:CreateChild("Camera")
    cameraNode:CreateComponent("FreeFlyController")
    local camera = cameraNode:CreateComponent("Camera")
    camera:SetFarClip(300.0)
    cameraNode:SetPosition(Vector3(0.0, 2.0, -14.0))
    self.cameraNode = cameraNode

    SetViewport(0, scene, camera)
end

-- Runtime consumer entry: atlas texture + clip table only.
function app:LoadFlipbook()
    local baker = FlipbookBaker()
    if not baker:Load(FLIPBOOK_PATH) then
        LogError("FlipbookInstancing: atlas load failed: " .. baker:GetLastError())
        return false
    end

    self.flipbook = baker
    self.fps = baker:GetFps()
    self.numClips = baker:GetNumClips()
    self.rotations = baker:GetRotations()

    local names = {}
    for c = 0, self.numClips - 1 do
        table.insert(names, baker:GetClipName(c))
    end
    self.clipNames = names

    LogInfo(string.format(
        "FlipbookInstancing: loaded atlas '%s' -> %dx%d, %d clip(s) @ %.1f fps, %d rotation(s): %s",
        FLIPBOOK_PATH, baker:GetAtlasWidth(), baker:GetAtlasHeight(),
        self.numClips, self.fps, self.rotations, table.concat(names, ", ")))
    return true
end

-- Grid pose: 10 columns per row, one clip per row (row index == clip index).
function app:InstancePose(index)
    local col = (index - 1) % GRID_COLS
    local row = math.floor((index - 1) / GRID_COLS)
    local half = GRID_COLS * GRID_SPACING * 0.5
    local x = col * GRID_SPACING - half
    local z = row * GRID_SPACING
    return Vector3(x, BILLBOARD_H * 0.5, z)
end

-- Build the billboard crowd: ONE BillboardSet holding all 30 cards -> one draw call.
function app:BuildCrowd()
    local texture = self.flipbook:GetTexture()
    local technique = GetResource("Technique", TECHNIQUE_PATH)
    if not texture or not technique then
        LogError("FlipbookInstancing: missing atlas texture or technique asset")
        return
    end

    -- Unlit + vertex-color + alpha-blended technique: the baked cards carry their own
    -- shading, and billboard.uv/color are the only per-instance knobs the shader reads.
    local material = Material()
    material:SetTechnique(0, technique)
    material:SetTexture("Diffuse", texture)

    local node = self.scene:CreateChild("FlipbookCrowd")
    local set = node:CreateComponent("BillboardSet")
    set:SetMaterial(material)
    set:SetNumBillboards(NUM_INSTANCES)

    self.billboardSet = set
    self.billboards = {}
    self.clipOfInstance = {}

    for i = 1, NUM_INSTANCES do
        local b = set:GetBillboard(i - 1)
        b.position = self:InstancePose(i)
        b.size = Vector2(BILLBOARD_W, BILLBOARD_H)
        b.color = Color(1.0, 1.0, 1.0)
        b.enabled = true
        table.insert(self.billboards, b)

        local clip = math.floor((i - 1) / PER_CLIP) -- 0..2: first three clips
        clip = math.min(clip, self.numClips - 1)
        table.insert(self.clipOfInstance, clip)
    end
    set:Commit()

    -- Clip playback data precomputed on the Lua side; per frame we only rewrite UV rects.
    self.clipFrameCount = {}
    self.clipLoopTime = {}
    for c = 0, self.numClips - 1 do
        local frames = self.flipbook:GetClipFrameCount(c)
        self.clipFrameCount[c] = frames
        self.clipLoopTime[c] = frames / self.fps
    end

    self.statusLine = string.format(
        "Flipbook billboards | %d cards -> %d clip group(s) of %d (%s) | expected: 1 draw call",
        NUM_INSTANCES, 3, PER_CLIP, table.concat(
            { self.clipNames[1] or "?", self.clipNames[2] or "?", self.clipNames[3] or "?" }, " / "))
    LogInfo("FlipbookInstancing: " .. self.statusLine)
end

function app:Update(timeStep)
    self:MoveCameraByMouse(0.1)
    self:MoveCamera(timeStep, 12.0)

    if not self.billboardSet then
        return
    end

    self.flowTime = self.flowTime + timeStep
    for i = 1, NUM_INSTANCES do
        local clip = self.clipOfInstance[i]
        local loop = self.clipLoopTime[clip]
        if loop and loop > 0.0 then
            -- Phase-stagger instances inside a group so the rows do not move in lockstep.
            local phase = ((i - 1) % PER_CLIP) / PER_CLIP * loop
            local t = (self.flowTime + phase) % loop
            local frame = math.floor(t * self.fps)
            if frame >= self.clipFrameCount[clip] then
                frame = self.clipFrameCount[clip] - 1
            end
            self.billboards[i].uv = self.flipbook:GetClipCellUV(clip, frame, 0)
        end
    end
    self.billboardSet:Commit()
end

function app:OnKeyDown(key)
    if key == KEY.SPACE then
        self.drawDebug = not self.drawDebug
    end
end

app:Run()

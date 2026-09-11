-- LuaSamples/34_DynamicGeometry/main.lua
-- Lua port of Source/Samples/34_DynamicGeometry: cloning a model and
-- deforming its vertex data at runtime, plus building a pyramid model from
-- scratch. Space toggles animation.

local app = Sample:new()
app.animate = true
app.time = 0.0
app.originalVertices = {}
app.vertexDuplicates = {}
app.animatingBuffers = {}

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.RELATIVE)
    input:SetMouseVisible(false)

    self:CreateScene()
    self:CreateInstructions()
end

function app:CreateScene()
    local scene = CreateScene()
    self.scene = scene
    scene:CreateComponent("Octree")

    -- Zone for ambient light & fog control
    local zoneNode = scene:CreateChild("Zone")
    local zone = zoneNode:CreateComponent("Zone")
    zone:SetBoundingBox(BoundingBox(-1000.0, 1000.0))
    zone:SetFogColor(Color(0.2, 0.2, 0.2))
    zone:SetFogStart(200.0)
    zone:SetFogEnd(300.0)

    -- Directional light
    local lightNode = scene:CreateChild("DirectionalLight")
    lightNode:SetDirection(Vector3(-0.6, -1.0, -0.8))
    local light = lightNode:CreateComponent("Light")
    light:SetLightType(LIGHT.DIRECTIONAL)
    light:SetColor(Color(0.4, 1.0, 0.4))
    light:SetSpecularIntensity(1.5)

    -- Get the original model and its unmodified vertices, used as source
    -- data for the animation
    local originalModel = GetResource("Model", "Models/Box.mdl")
    if not originalModel then
        error("Model not found, cannot initialize example scene")
    end
    local buffer = originalModel:GetGeometry(0, 0):GetVertexBuffer(0)
    local positions = buffer:GetVertexPositions()
    if #positions == 0 then
        error("Failed to lock the model vertex buffer to get original vertices")
    end

    self.originalVertices = positions
    -- Detect duplicate vertices to allow seamless animation
    for i = 1, #positions do
        self.vertexDuplicates[i] = i -- Assume not a duplicate
        for j = 1, i - 1 do
            local a = positions[i]
            local b = positions[j]
            if math.abs(a.x - b.x) < 0.0001 and math.abs(a.y - b.y) < 0.0001
                and math.abs(a.z - b.z) < 0.0001 then
                self.vertexDuplicates[i] = j
                break
            end
        end
    end

    -- Create StaticModels. Clone the model for each so vertex data can be
    -- modified individually.
    for y = -1, 1 do
        for x = -1, 1 do
            local node = scene:CreateChild("Object")
            node:SetPosition(Vector3(x * 2.0, 0.0, y * 2.0))
            local object = node:CreateComponent("StaticModel")
            local cloneModel = originalModel:Clone()
            object:SetModel(cloneModel)
            -- Store the cloned vertex buffer we will modify when animating
            table.insert(self.animatingBuffers, cloneModel:GetGeometry(0, 0):GetVertexBuffer(0))
        end
    end

    -- Create one model (pyramid shape) from scratch. Duplicated vertices
    -- enable face normals, which are calculated programmatically.
    do
        local numVertices = 18

        local vertexData = {
            -- Position           Normal
            0.0, 0.5, 0.0,        0.0, 0.0, 0.0,
            0.5, -0.5, 0.5,       0.0, 0.0, 0.0,
            0.5, -0.5, -0.5,      0.0, 0.0, 0.0,

            0.0, 0.5, 0.0,        0.0, 0.0, 0.0,
            -0.5, -0.5, 0.5,      0.0, 0.0, 0.0,
            0.5, -0.5, 0.5,       0.0, 0.0, 0.0,

            0.0, 0.5, 0.0,        0.0, 0.0, 0.0,
            -0.5, -0.5, -0.5,     0.0, 0.0, 0.0,
            -0.5, -0.5, 0.5,      0.0, 0.0, 0.0,

            0.0, 0.5, 0.0,        0.0, 0.0, 0.0,
            0.5, -0.5, -0.5,      0.0, 0.0, 0.0,
            -0.5, -0.5, -0.5,     0.0, 0.0, 0.0,

            0.5, -0.5, -0.5,      0.0, 0.0, 0.0,
            0.5, -0.5, 0.5,       0.0, 0.0, 0.0,
            -0.5, -0.5, 0.5,      0.0, 0.0, 0.0,

            0.5, -0.5, -0.5,      0.0, 0.0, 0.0,
            -0.5, -0.5, 0.5,      0.0, 0.0, 0.0,
            -0.5, -0.5, -0.5,     0.0, 0.0, 0.0,
        }

        local indexData = {
            0, 1, 2,
            3, 4, 5,
            6, 7, 8,
            9, 10, 11,
            12, 13, 14,
            15, 16, 17,
        }

        -- Calculate face normals
        for i = 1, numVertices, 3 do
            local v1 = Vector3(vertexData[(i - 1) * 6 + 1], vertexData[(i - 1) * 6 + 2], vertexData[(i - 1) * 6 + 3])
            local v2 = Vector3(vertexData[i * 6 + 1], vertexData[i * 6 + 2], vertexData[i * 6 + 3])
            local v3 = Vector3(vertexData[(i + 1) * 6 + 1], vertexData[(i + 1) * 6 + 2], vertexData[(i + 1) * 6 + 3])
            local normal = (v1 - v2):CrossProduct(v1 - v3):Normalized()
            for k = 0, 2 do
                vertexData[(i + k - 1) * 6 + 4] = normal.x
                vertexData[(i + k - 1) * 6 + 5] = normal.y
                vertexData[(i + k - 1) * 6 + 6] = normal.z
            end
        end

        local fromScratchModel = Model()
        local vb = VertexBuffer()
        local ib = IndexBuffer()
        local geom = Geometry()

        -- Always name your GPU objects, useful when things go wrong
        vb:SetDebugName("DynamicGeometry")
        ib:SetDebugName("DynamicGeometry")

        -- Shadowed buffer needed for raycasts and device-loss restore
        vb:SetShadowed(true)
        -- Define the vertex elements explicitly
        vb:SetSize(numVertices, {
            VertexElement(VET.VECTOR3, VSEM.POSITION),
            VertexElement(VET.VECTOR3, VSEM.NORMAL),
        })
        vb:Update(self:PackFloats(vertexData))

        ib:SetShadowed(true)
        ib:SetSize(numVertices, false)
        ib:Update(self:PackUShorts(indexData))

        geom:SetVertexBuffer(0, vb)
        geom:SetIndexBuffer(ib)
        geom:SetDrawRange(0, 0, numVertices) -- TRIANGLE_LIST

        fromScratchModel:SetNumGeometries(1)
        fromScratchModel:SetGeometry(0, 0, geom)
        fromScratchModel:SetBoundingBox(BoundingBox(Vector3(-0.5, -0.5, -0.5), Vector3(0.5, 0.5, 0.5)))

        -- Buffers must be listed in the model so it can be saved properly
        fromScratchModel:SetVertexBuffers({ vb }, { 0 }, { 0 })
        fromScratchModel:SetIndexBuffers({ ib })

        local node = scene:CreateChild("FromScratchObject")
        node:SetPosition(Vector3(0.0, 3.0, 0.0))
        local object = node:CreateComponent("StaticModel")
        object:SetModel(fromScratchModel)
    end

    -- Camera with WASD free-fly movement
    self.cameraNode = scene:CreateChild("Camera")
    self.cameraNode:CreateComponent("FreeFlyController")
    self.cameraNode:SetPosition(Vector3(0.0, 2.0, -20.0))
    local camera = self.cameraNode:CreateComponent("Camera")
    camera:SetFarClip(300.0)
    SetViewport(0, scene, camera)
end

-- Pack a Lua array of floats into a binary string for buffer updates.
function app:PackFloats(values)
    local packed = {}
    for i = 1, #values do
        packed[i] = string.pack("<f", values[i])
    end
    return table.concat(packed)
end

-- Pack a Lua array of unsigned shorts into a binary string.
function app:PackUShorts(values)
    local packed = {}
    for i = 1, #values do
        packed[i] = string.pack("<I2", values[i])
    end
    return table.concat(packed)
end

function app:CreateInstructions()
    local ui = GetSubsystem("UI")
    local root = ui:GetRoot()

    local instructionText = root:CreateChild("Text")
    instructionText:SetText("Use WASD keys and mouse/touch to move\nSpace to toggle animation")
    instructionText:SetFont("Fonts/Anonymous Pro.ttf", 15)
    instructionText:SetTextAlignment(HA.CENTER)
    instructionText:SetHorizontalAlignment(HA.CENTER)
    instructionText:SetVerticalAlignment(VA.CENTER)
    instructionText:SetPosition(0, root:GetHeight() / 4)
end

function app:AnimateObjects(timeStep)
    self.time = self.time + timeStep * 100.0

    -- Repeat for each of the cloned vertex buffers
    for i = 1, #self.animatingBuffers do
        local startPhase = self.time + (i - 1) * 30.0
        local buffer = self.animatingBuffers[i]

        -- Rewrite positions with sine wave modulated ones, preserving the
        -- other elements (normals, UVs)
        local numVertices = buffer:GetVertexCount()
        local positions = {}
        for j = 1, numVertices do
            -- Duplicate vertices animate in phase of the original
            local phase = startPhase + (self.vertexDuplicates[j] - 1) * 10.0
            local src = self.originalVertices[j]
            positions[j] = Vector3(
                src.x * (1.0 + 0.1 * Sin(phase)),
                src.y * (1.0 + 0.1 * Sin(phase + 60.0)),
                src.z * (1.0 + 0.1 * Sin(phase + 120.0)))
        end
        buffer:UpdateVertexPositions(positions)
    end
end

function app:Update(timeStep)
    -- Toggle animation with space
    if GetSubsystem("Input"):GetKeyPress(KEY.SPACE) then
        self.animate = not self.animate
    end

    if self.animate then
        self:AnimateObjects(timeStep)
    end
end

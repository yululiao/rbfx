-- LuaSamples/23_Water/main.lua
-- Lua port of Source/Samples/23_Water: heightmap terrain with a planar
-- reflection water plane. A second camera (child of the main camera, mirrored
-- across the water plane and clipping geometry below it) renders the scene
-- minus the water plane into a texture the water material samples.

local NUM_OBJECTS = 1000

local app = Sample:new()

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.RELATIVE)
    input:SetMouseVisible(false)

    self:CreateScene()
    self:CreateInstructions()
    self:SetupViewport()
end

function app:CreateScene()
    local cache = GetSubsystem("ResourceCache")

    -- Clone the water material so shader defines can be extended safely
    self.waterMaterial = cache:GetResource("Material", "Materials/Showcase/LitWaterTiled.xml"):Clone()

    local scene = CreateScene()
    self.scene = scene

    -- Octree with default volume (-1000, -1000, -1000) to (1000, 1000, 1000)
    scene:CreateComponent("Octree")

    -- Zone for ambient lighting & fog control
    local zoneNode = scene:CreateChild("Zone")
    local zone = zoneNode:CreateComponent("Zone")
    zone:SetBoundingBox(BoundingBox(-1000.0, 1000.0))
    zone:SetAmbientColor(Color(0.15, 0.15, 0.15))
    zone:SetFogColor(Color(1.0, 1.0, 1.0))
    zone:SetFogStart(500.0)
    zone:SetFogEnd(750.0)

    -- Directional light with cascaded shadows, slightly overbright to match
    -- the skybox
    local lightNode = scene:CreateChild("DirectionalLight")
    lightNode:SetDirection(Vector3(0.6, -1.0, 0.8))
    local light = lightNode:CreateComponent("Light")
    light:SetLightType(LIGHT.DIRECTIONAL)
    light:SetCastShadows(true)
    light:SetShadowBias(0.00025, 0.5)
    light:SetShadowCascade(10.0, 50.0, 200.0, 0.0, 0.8)
    light:SetSpecularIntensity(0.5)
    light:SetColor(Color(1.2, 1.2, 1.2))

    -- Skybox: always located at the camera, giving the illusion of distance
    local skyNode = scene:CreateChild("Sky")
    skyNode:SetScale(500.0) -- scale actually does not matter
    local skybox = skyNode:CreateComponent("Skybox")
    skybox:SetModel(cache:GetResource("Model", "Models/Box.mdl"))
    skybox:SetMaterial(cache:GetResource("Material", "Materials/Skybox.xml"))

    -- Heightmap terrain; large triangles fit occlusion rendering well
    local terrainNode = scene:CreateChild("Terrain")
    local terrain = terrainNode:CreateComponent("Terrain")
    terrain:SetPatchSize(64)
    terrain:SetSpacing(Vector3(2.0, 0.5, 2.0)) -- vertex spacing & vertical resolution
    terrain:SetSmoothing(true)
    terrain:SetHeightMap(cache:GetResource("Image", "Textures/HeightMap.png"))
    terrain:SetMaterial(cache:GetResource("Material", "Materials/Terrain.xml"))
    terrain:SetOccluder(true)

    -- 1000 boxes on the terrain, always facing outward along the normal
    for i = 1, NUM_OBJECTS do
        local objectNode = scene:CreateChild("Box")
        local position = Vector3(Random(2000.0) - 1000.0, 0.0, Random(2000.0) - 1000.0)
        position.y = terrain:GetHeight(position) + 2.25
        objectNode:SetPosition(position)
        objectNode:SetRotation(Quaternion(Vector3(0.0, 1.0, 0.0), terrain:GetNormal(position)))
        objectNode:SetScale(5.0)
        local object = objectNode:CreateComponent("StaticModel")
        object:SetModel(cache:GetResource("Model", "Models/Box.mdl"))
        object:SetMaterial(cache:GetResource("Material", "Materials/Stone.xml"))
        object:SetCastShadows(true)
    end

    -- Water plane as large as the terrain. Viewmask has only bit 31 set so the
    -- reflection camera (which uses 0x7fffffff) can hide it
    self.waterNode = scene:CreateChild("Water")
    self.waterNode:SetScale(Vector3(2048.0, 1.0, 2048.0))
    self.waterNode:SetPosition(Vector3(0.0, 5.0, 0.0))
    local water = self.waterNode:CreateComponent("StaticModel")
    water:SetModel(cache:GetResource("Model", "Models/Plane.mdl"))
    water:SetMaterial(self.waterMaterial)
    water:SetViewMask(0x80000000)

    -- Camera
    local cameraNode = scene:CreateChild("Camera")
    cameraNode:CreateComponent("FreeFlyController")
    local camera = cameraNode:CreateComponent("Camera")
    camera:SetFarClip(750.0) -- matches the fog end
    cameraNode:SetPosition(Vector3(0.0, 7.0, -20.0))
    self.cameraNode = cameraNode
    self.camera = camera
end

function app:CreateInstructions()
    local cache = GetSubsystem("ResourceCache")

    local instructionText = GetUIRoot():CreateChild("Text")
    instructionText:SetText("Use WASD keys and mouse/touch to move")
    instructionText:SetFont(cache:GetResource("Font", "Fonts/Anonymous Pro.ttf"), 15)
    instructionText:SetTextAlignment(HA.CENTER)
    instructionText:SetHorizontalAlignment(HA.CENTER)
    instructionText:SetVerticalAlignment(VA.CENTER)
    instructionText:SetPosition(0, GetUIRoot():GetHeight() / 4)
end

function app:SetupViewport()
    local graphics = GetSubsystem("Graphics")

    SetViewport(0, self.scene, self.camera)

    -- Mathematical planes for reflection and view clipping. The clip plane is
    -- biased slightly downward to avoid clipping too aggressively
    local planeNormal = self.waterNode:GetWorldRotation() * Vector3(0.0, 1.0, 0.0)
    self.waterPlane = Plane(planeNormal, self.waterNode:GetWorldPosition())
    self.waterClipPlane = Plane(planeNormal, self.waterNode:GetWorldPosition() - Vector3(0.0, 0.1, 0.0))

    -- Reflection camera: child of the main camera, mirrored across the water
    -- plane via SetUseReflection, with geometry behind the plane clipped
    local reflectionCameraNode = self.cameraNode:CreateChild()
    local reflectionCamera = reflectionCameraNode:CreateComponent("Camera")
    reflectionCamera:SetFarClip(750.0)
    reflectionCamera:SetViewMask(0x7fffffff) -- hide the water plane (bit 31 only)
    reflectionCamera:SetAutoAspectRatio(false)
    reflectionCamera:SetUseReflection(true)
    reflectionCamera:SetReflectionPlane(self.waterPlane)
    reflectionCamera:SetUseClipping(true)
    reflectionCamera:SetClipPlane(self.waterClipPlane)
    -- Reflection texture is rectangular: match the screen aspect ratio
    reflectionCamera:SetAspectRatio(graphics:GetWidth() / graphics:GetHeight())
    self.reflectionCamera = reflectionCamera

    -- Reflection render target assigned to the water material's Reflection0
    -- unit, plus the shader define that switches to planar reflection
    local renderTexture = Texture2D()
    renderTexture:SetSize(1024, 1024, TEXF.RGBA8_UNORM, 2, 4) -- BindRenderTarget flag
    renderTexture:SetFilterMode(FILTER.BILINEAR)
    local surface = renderTexture:GetRenderSurface()
    surface:SetViewport(0, CreateViewport(self.scene, reflectionCamera))
    self.waterMaterial:SetTexture("Reflection0", renderTexture)
    self.waterMaterial:SetVertexShaderDefines(self.waterMaterial:GetVertexShaderDefines() .. "PLANEREFLECTION ")
    self.waterMaterial:SetPixelShaderDefines(self.waterMaterial:GetPixelShaderDefines() .. "PLANEREFLECTION ")
end

function app:Update(timeStep)
    -- Skip if the UI has a focused element
    if GetSubsystem("UI"):GetFocusElement() then
        return
    end

    -- Feed the water distortion shader constants derived from the camera
    -- orientation (unrolled Vector4(Vector3, w) constructor)
    local distortionBias = 0.01
    local distortionStrength = 0.1
    local planeNormal = Vector3.UP
    local planeRight = self.cameraNode:GetWorldRight()
    local planeForward = planeRight:CrossProduct(planeNormal):Normalized()
    self.waterMaterial:SetShaderParameter("ReflectionPlaneX", Vector4(
        distortionStrength * planeRight.x, distortionStrength * planeRight.y,
        distortionStrength * planeRight.z, 0.0))
    self.waterMaterial:SetShaderParameter("ReflectionPlaneY", Vector4(
        distortionStrength * planeForward.x, distortionStrength * planeForward.y,
        distortionStrength * planeForward.z, distortionBias))

    -- Adjust the reflection camera aspect ratio in case resolution changed
    local graphics = GetSubsystem("Graphics")
    self.reflectionCamera:SetAspectRatio(graphics:GetWidth() / graphics:GetHeight())
end

app:Run()

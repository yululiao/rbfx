-- LuaSamples/10_RenderToTexture/main.lua
-- Lua port of Source/Samples/10_RenderToTexture: a second scene rendered
-- into a texture, displayed on a "screen" object in the main scene.

local app = Sample:new()

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.RELATIVE)
    input:SetMouseVisible(false)

    self.rotators = {}
    self:CreateScene()
    self:CreateInstructions("Use WASD keys and mouse/touch to move")
end

function app:CreateScene()
    -- Create the scene which will be rendered to a texture
    local rttScene = CreateScene()
    self.rttScene = rttScene

    rttScene:CreateComponent("Octree")

    -- Create a Zone for ambient light & fog control
    local zoneNode = rttScene:CreateChild("Zone")
    local zone = zoneNode:CreateComponent("Zone")
    zone:SetBoundingBox(BoundingBox(-1000.0, 1000.0))
    zone:SetAmbientColor(Color(0.05, 0.1, 0.15))
    zone:SetFogColor(Color(0.1, 0.2, 0.3))
    zone:SetFogStart(10.0)
    zone:SetFogEnd(100.0)

    -- Create randomly positioned and oriented box StaticModels in the scene
    local NUM_OBJECTS = 2000
    for i = 1, NUM_OBJECTS do
        local boxNode = rttScene:CreateChild("Box")
        boxNode:SetPosition(Vector3(Random(200.0) - 100.0, Random(100.0), Random(200.0) - 100.0))
        -- Orient using random pitch, yaw and roll Euler angles
        boxNode:SetRotation(Quaternion(Random(360.0), Random(360.0), Random(360.0)))
        local boxObject = boxNode:CreateComponent("StaticModel")
        boxObject:SetModel(GetResource("Model", "Models/Box.mdl"))
        boxObject:SetMaterial(GetResource("Material", "Materials/Stone.xml"))

        -- Rotation applied from the Update loop (replaces C++ Rotator)
        table.insert(self.rotators, {
            node = boxNode,
            speed = Vector3(10.0, 20.0, 30.0)
        })
    end

    -- Create a camera for the render-to-texture scene. Simply leave it at
    -- the world origin and let it observe the scene
    local rttCameraNode = rttScene:CreateChild("Camera")
    local rttCamera = rttCameraNode:CreateComponent("Camera")
    rttCamera:SetFarClip(100.0)

    -- Create a point light to the camera scene node
    local light = rttCameraNode:CreateComponent("Light")
    light:SetLightType(LIGHT.POINT)
    light:SetRange(30.0)

    -- Create the scene in which we move around
    local scene = CreateScene()
    self.scene = scene

    scene:CreateComponent("Octree")

    local zoneNode2 = scene:CreateChild("Zone")
    local zone2 = zoneNode2:CreateComponent("Zone")
    zone2:SetBoundingBox(BoundingBox(-1000.0, 1000.0))
    zone2:SetAmbientColor(Color(0.1, 0.1, 0.1))
    zone2:SetFogStart(100.0)
    zone2:SetFogEnd(300.0)

    -- Create a directional light without shadows
    local lightNode = scene:CreateChild("DirectionalLight")
    lightNode:SetDirection(Vector3(0.5, -1.0, 0.5))
    local dirLight = lightNode:CreateComponent("Light")
    dirLight:SetLightType(LIGHT.DIRECTIONAL)
    dirLight:SetColor(Color(0.2, 0.2, 0.2))
    dirLight:SetSpecularIntensity(1.0)

    -- Create a "floor" consisting of several tiles
    for y = -5, 5 do
        for x = -5, 5 do
            local floorNode = scene:CreateChild("FloorTile")
            floorNode:SetPosition(Vector3(x * 20.5, -0.5, y * 20.5))
            floorNode:SetScale(Vector3(20.0, 1.0, 20.0))
            local floorObject = floorNode:CreateComponent("StaticModel")
            floorObject:SetModel(GetResource("Model", "Models/Box.mdl"))
            floorObject:SetMaterial(GetResource("Material", "Materials/Stone.xml"))
        end
    end

    -- Create a "screen" like object for viewing the second scene. Construct
    -- it from two StaticModels, a box for the frame and a plane for the view
    do
        local boxNode = scene:CreateChild("ScreenBox")
        boxNode:SetPosition(Vector3(0.0, 10.0, 0.0))
        boxNode:SetScale(Vector3(21.0, 16.0, 0.5))
        local boxObject = boxNode:CreateComponent("StaticModel")
        boxObject:SetModel(GetResource("Model", "Models/Box.mdl"))
        boxObject:SetMaterial(GetResource("Material", "Materials/Stone.xml"))

        local screenNode = scene:CreateChild("Screen")
        screenNode:SetPosition(Vector3(0.0, 10.0, -0.27))
        screenNode:SetRotation(Quaternion(-90.0, 0.0, 0.0))
        screenNode:SetScale(Vector3(20.0, 0.0, 15.0))
        local screenObject = screenNode:CreateComponent("StaticModel")
        screenObject:SetModel(GetResource("Model", "Models/Plane.mdl"))

        -- Create a renderable texture (1024x768, RGB format), enable
        -- bilinear filtering on it
        local renderTexture = Texture2D()
        renderTexture:SetSize(1024, 768, TEXF.RGBA8_UNORM, 2, 4) -- BindRenderTarget flag
        renderTexture:SetFilterMode(FILTER.BILINEAR)

        -- Create a new material from scratch, use the diffuse unlit
        -- technique, assign the render texture as its diffuse texture, then
        -- assign the material to the screen plane object
        local renderMaterial = Material()
        renderMaterial:SetTechnique(0, GetResource("Technique", "Techniques/DiffUnlit.xml"))
        renderMaterial:SetTexture("Albedo", renderTexture)
        -- Since the screen material is on top of the box model and may
        -- Z-fight, use negative depth bias to push it forward
        renderMaterial:SetDepthBias(-0.001, 0.0)
        screenObject:SetMaterial(renderMaterial)

        -- Get the texture's RenderSurface object and define the viewport for
        -- rendering the second scene. By default the texture viewport will
        -- be updated when the texture is visible in the main view
        local surface = renderTexture:GetRenderSurface()
        local rttViewport = CreateViewport(rttScene, rttCamera)
        surface:SetViewport(0, rttViewport)
    end

    -- Create the camera which we will move around
    local cameraNode = scene:CreateChild("Camera")
    cameraNode:CreateComponent("FreeFlyController")
    local camera = cameraNode:CreateComponent("Camera")
    camera:SetFarClip(300.0)

    -- Set an initial position for the camera scene node above the plane
    cameraNode:SetPosition(Vector3(0.0, 7.0, -30.0))

    self.cameraNode = cameraNode
    SetViewport(0, scene, camera)
end

-- Per-frame rotation of the RTT boxes, mirroring the C++ Rotator component.
function app:Update(timeStep)
    for _, rotator in ipairs(self.rotators) do
        local node = rotator.node
        node:Pitch(rotator.speed.x * timeStep)
        node:Yaw(rotator.speed.y * timeStep)
        node:Roll(rotator.speed.z * timeStep)
    end
end

app:Run()

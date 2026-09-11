-- LuaSamples/Framework.lua
-- Lua counterpart of Source/Samples/Sample.h: shared plumbing so each
-- sample's main.lua only implements OnStart / Update / OnKeyDown.

Sample = {}
Sample.__index = Sample

-- Constructor. Override hooks on the returned table:
--   app:OnStart()          -- build scene & UI
--   app:Update(timeStep)   -- per-frame logic (optional)
--   app:OnKeyDown(key)     -- extra keys beyond the common ones (optional)
function Sample:new(overrides)
    local o = overrides or {}
    o.yaw = o.yaw or 0.0
    o.pitch = o.pitch or 0.0
    o.scene = o.scene
    o.cameraNode = o.cameraNode
    setmetatable(o, self)
    return o
end

-- Entry point invoked after main.lua loaded. Wires common events.
function Sample:Run()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.ABSOLUTE)
    input:SetMouseVisible(false)
    SubscribeToEvent("KeyDown", function(data) self:HandleKeyDown(data) end)
    SubscribeToEvent("Update", function(data) self:Update(data.TimeStep) end)
    self:OnStart()
end

function Sample:OnStart() end
function Sample:Update(timeStep) end
function Sample:OnKeyDown(key) end

-- Common keys, mirroring the C++ samples: ESC quits, F11 fullscreen,
-- 9 takes a screenshot next to the executable.
function Sample:HandleKeyDown(data)
    local key = data.Key
    if key == KEY.ESC then
        GetSubsystem("Engine"):Exit()
    elseif key == KEY.F11 then
        GetSubsystem("Graphics"):ToggleFullscreen()
    elseif key == string.byte("9") then
        local graphics = GetSubsystem("Graphics")
        local image = graphics:TakeScreenShot()
        if image then
            local name = "Screenshot_" .. os.date("%Y%m%d_%H%M%S") .. ".png"
            image:SavePNG(name)
        end
    else
        self:OnKeyDown(key)
    end
end

-- Standard camera node + viewport setup. Returns the camera node.
function Sample:CreateCamera(scene, farClip)
    self.scene = scene
    self.cameraNode = scene:CreateChild("Camera")
    local camera = self.cameraNode:CreateComponent("Camera")
    camera:SetFarClip(farClip or 300.0)
    SetViewport(0, scene, camera)
    return self.cameraNode
end

-- Classic first-person mouse look (yaw/pitch accumulated from mouse moves).
function Sample:MoveCameraByMouse(sensitivity)
    sensitivity = sensitivity or 0.1
    local mouseMove = GetSubsystem("Input"):GetMouseMove()
    self.yaw = self.yaw + mouseMove.x * sensitivity
    self.pitch = self.pitch + mouseMove.y * sensitivity
    self.pitch = math.max(-90.0, math.min(90.0, self.pitch))
    self.cameraNode.rotation = Quaternion(self.pitch, self.yaw, 0.0)
end

-- Classic WASD movement, speed doubled while CTRL held.
function Sample:MoveCamera(timeStep, moveSpeed)
    moveSpeed = moveSpeed or 20.0
    local input = GetSubsystem("Input")
    if input:GetKeyDown(KEY.CTRL) then
        moveSpeed = moveSpeed * 2.0
    end
    local translation = Vector3(0.0, 0.0, 0.0)
    local dir = self.cameraNode.direction
    if input:GetKeyDown(string.byte("w")) or input:GetKeyDown(KEY.UP) then
        translation = translation + dir
    end
    if input:GetKeyDown(string.byte("s")) or input:GetKeyDown(KEY.DOWN) then
        translation = translation - dir
    end
    if input:GetKeyDown(string.byte("a")) or input:GetKeyDown(KEY.LEFT) then
        translation = translation + Vector3(-dir.z, 0.0, dir.x)
    end
    if input:GetKeyDown(string.byte("d")) or input:GetKeyDown(KEY.RIGHT) then
        translation = translation + Vector3(dir.z, 0.0, -dir.x)
    end
    if translation:LengthSquared() > 0.0 then
        translation = translation:Normalized() * moveSpeed * timeStep
        self.cameraNode:Translate(translation, TS.WORLD)
    end
end

-- Instruction text overlay shown by most samples.
function Sample:CreateInstructions(text)
    local ui = GetSubsystem("UI")
    local root = ui:GetRoot()
    local instructions = root:CreateChild("Text", "Instructions")
    instructions:SetFont("Fonts/Anonymous Pro.ttf", 15)
    instructions:SetText(text)
    instructions:SetTextAlignment(HA.LEFT)
    instructions:SetPosition(10, 10)
    instructions:SetWidth(ui:GetRoot():GetWidth() - 20)
    instructions:SetColor(Color(0.0, 1.0, 0.0))
    return instructions
end

-- Corner logo sprite, same texture and placement as the C++ samples.
function Sample:CreateLogo()
    local ui = GetSubsystem("UI")
    local logoTexture = GetResource("Texture2D", "Textures/FishBoneLogo.png")
    if not logoTexture then
        return nil
    end
    local logo = ui:GetRoot():CreateChild("Sprite", "Logo")
    logo:SetTexture(logoTexture)
    local width = logoTexture:GetWidth()
    local height = logoTexture:GetHeight()
    logo:SetScale(256.0 / width)
    logo:SetSize(width, height)
    logo:SetHotSpot(width, height)
    logo:SetAlignment(HA.RIGHT, VA.BOTTOM)
    logo:SetOpacity(0.9)
    return logo
end

-- Default skybox used by the C++ samples.
function Sample:SetDefaultSkybox(scene)
    local skybox = scene:CreateComponent("Skybox")
    skybox:SetModel(GetResource("Model", "Models/Box.mdl"))
    skybox:SetMaterial(GetResource("Material", "Materials/DefaultSkybox.xml"))
    return skybox
end

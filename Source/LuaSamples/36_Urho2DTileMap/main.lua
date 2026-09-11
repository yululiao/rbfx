-- LuaSamples/36_Urho2DTileMap/main.lua
-- Lua port of Source/Samples/36_Urho2DTileMap: loads an isometric TMX tile
-- map. WASD pans, PageUp/PageDown zooms, LMB removes a tile, RMB swaps
-- grass and water sprites.

local PIXEL_SIZE = 0.01

local app = Sample:new()

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.FREE)
    input:SetMouseVisible(true)

    self:CreateScene()
    self:CreateInstructions()

    SubscribeToEvent("MouseButtonDown", function(data)
        self:HandleMouseButtonDown()
    end)
end

function app:CreateScene()
    local scene = CreateScene()
    self.scene = scene
    scene:CreateComponent("Octree")

    -- Create camera node
    self.cameraNode = scene:CreateChild("Camera")
    self.cameraNode:SetPosition(Vector3(0.0, 0.0, -10.0))

    local camera = self.cameraNode:CreateComponent("Camera")
    camera:SetOrthographic(true)

    local graphics = GetSubsystem("Graphics")
    camera:SetOrthoSize(graphics:GetHeight() * PIXEL_SIZE)
    -- Initial zoom (1.0) targets full visibility at 1280x800
    camera:SetZoom(1.0 * math.min(graphics:GetWidth() / 1280.0, graphics:GetHeight() / 800.0))

    local tmxFile = GetResource("TmxFile2D", "Urho2D/isometric_grass_and_water.tmx")
    if not tmxFile then
        return
    end

    local tileMapNode = scene:CreateChild("TileMap")
    tileMapNode:SetPosition(Vector3(0.0, 0.0, -1.0))
    self.tileMapNode = tileMapNode

    local tileMap = tileMapNode:CreateComponent("TileMap2D")
    tileMap:SetTmxFile(tmxFile)

    -- Center camera on the map
    local info = tileMap:GetInfo()
    local x = info:GetMapWidth() * 0.5
    local y = info:GetMapHeight() * 0.5
    self.cameraNode:SetPosition(Vector3(x, y, -10.0))
end

function app:CreateInstructions()
    local ui = GetSubsystem("UI")
    local root = ui:GetRoot()

    local instructionText = root:CreateChild("Text")
    instructionText:SetText("Use WASD keys to move, use PageUp PageDown keys to zoom.\n LMB to remove a tile, RMB to swap grass and water.")
    instructionText:SetFont("Fonts/Anonymous Pro.ttf", 15)
    instructionText:SetHorizontalAlignment(HA.CENTER)
    instructionText:SetVerticalAlignment(VA.CENTER)
    instructionText:SetPosition(0, root:GetHeight() / 4)
end

function app:Update(timeStep)
    -- Do not move if the UI has a focused element (the console)
    if GetSubsystem("UI"):GetFocusElement() then
        return
    end

    local input = GetSubsystem("Input")
    local MOVE_SPEED = 4.0

    if input:GetKeyDown(KEY.W) then
        self.cameraNode:Translate(Vector3(0.0, 1.0, 0.0) * MOVE_SPEED * timeStep)
    end
    if input:GetKeyDown(KEY.S) then
        self.cameraNode:Translate(Vector3(0.0, -1.0, 0.0) * MOVE_SPEED * timeStep)
    end
    if input:GetKeyDown(KEY.A) then
        self.cameraNode:Translate(Vector3(-1.0, 0.0, 0.0) * MOVE_SPEED * timeStep)
    end
    if input:GetKeyDown(KEY.D) then
        self.cameraNode:Translate(Vector3(1.0, 0.0, 0.0) * MOVE_SPEED * timeStep)
    end

    local camera = self.cameraNode:GetComponent("Camera")
    if input:GetKeyDown(KEY.PAGEUP) then
        camera:SetZoom(camera:GetZoom() * 1.01)
    end
    if input:GetKeyDown(KEY.PAGEDOWN) then
        camera:SetZoom(camera:GetZoom() * 0.99)
    end
end

function app:HandleMouseButtonDown()
    local input = GetSubsystem("Input")

    local tileMapNode = self.scene:GetChild("TileMap", true)
    local map = tileMapNode:GetComponent("TileMap2D")
    local layer = map:GetLayer(0)

    local pos = self:GetMousePositionXY() - tileMapNode:GetPosition2D()
    local x, y, ok = map:PositionToTileIndex(pos)
    if ok then
        -- Tile sprite is read-only, so get the sprite through the tile node
        local n = layer:GetTileNode(x, y)
        if not n then
            return
        end
        local sprite = n:GetComponent("StaticSprite2D")

        if input:GetMouseButtonDown(MOUSEB.RIGHT) then
            -- Swap grass and water: first 8 sprites of the tileset are
            -- mostly grass, 9-24 mostly water
            if layer:GetTile(x, y):GetGid() < 9 then
                sprite:SetSprite(layer:GetTile(0, 0):GetSprite())
            else
                sprite:SetSprite(layer:GetTile(24, 24):GetSprite())
            end
        else
            sprite:SetSprite(nil) -- 'Remove' sprite
        end
    end
end

function app:GetMousePositionXY()
    local input = GetSubsystem("Input")
    local graphics = GetSubsystem("Graphics")
    local camera = self.cameraNode:GetComponent("Camera")
    local mousePos = input:GetMousePosition()
    local screenPoint = Vector3(mousePos.x / graphics:GetWidth(), mousePos.y / graphics:GetHeight(), 10.0)
    local worldPoint = camera:ScreenToWorldPoint(screenPoint)
    return Vector2(worldPoint.x, worldPoint.y)
end

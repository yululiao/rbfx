-- LuaSamples/49_Urho2DIsometricDemo/main.lua
-- Lua port of Source/Samples/49_Urho2DIsometricDemo plus the shared
-- Utilities2D helpers (Sample2D, Mover) and the CharacterIsometric logic
-- component, all inlined:
--   - Isometric tile map scene with physics from tmx objects
--   - Spriter Imp character, coins to collect, Orcs to fight
--   - Moving entities follow polyline paths (Mover logic in Lua tables,
--     driven from SceneUpdate so scene pausing behaves like LogicComponent)
-- Reload note: the C++ sample re-loads a serialized scene snapshot; the Lua
-- port rebuilds the scene from the tmx resource instead, because the Mover
-- and character state live in Lua tables that cannot be serialized into
-- the scene. End result is equivalent.

local PIXEL_SIZE = 0.01
local CAMERA_MIN_DIST = 0.1
local CAMERA_MAX_DIST = 6.0
local MOVE_SPEED_X = 4.0
local LIFES = 3

local app = Sample:new()
app.drawDebug = false
app.movers = {}

function app:OnStart()
    self:CreateScene()

    -- UI content (coins/lifes counters, fullscreen start screen)
    self:CreateUIContent("ISOMETRIC 2.5D DEMO", self.character.remainingLifes,
        self.character.remainingCoins)
    local playButton = GetUIRoot():GetChild("PlayButton", true)
    SubscribeToEvent(playButton, "Released", function() self:HandlePlayButton() end)

    SubscribeToEvent("PostUpdate", function() self:HandlePostUpdate() end)
    SubscribeToEvent("PostRenderUpdate", function() self:HandlePostRenderUpdate() end)
    SubscribeToEvent("PhysicsBeginContact2D", function(data)
        self:HandleCollisionBegin(data)
    end)
end

function app:CreateScene()
    self:BuildScene()

    -- Pause the scene as long as the fullscreen UI is hiding it. The
    -- fullscreen check keeps a reloaded scene running, like the C++ flow.
    SubscribeToEvent("EndRendering", function()
        UnsubscribeFromEvent("EndRendering")
        if GetUIRoot():GetChild("FullUI", true) then
            self.scene:SetUpdateEnabled(false)
        end
    end)
end

function app:BuildScene()
    self.scene = CreateScene()
    local scene = self.scene
    self.movers = {}

    scene:CreateComponent("Octree")
    scene:CreateComponent("DebugRenderer")
    local physicsWorld = scene:CreateComponent("PhysicsWorld2D")
    -- Neutralize gravity: the character is always grounded in this demo
    physicsWorld:SetGravity(Vector2(0.0, 0.0))

    -- Orthographic camera; zoom adapts to the user's resolution
    self.cameraNode = scene:CreateChild("Camera")
    local camera = self.cameraNode:CreateComponent("Camera")
    camera:SetOrthographic(true)
    local graphics = GetSubsystem("Graphics")
    camera:SetOrthoSize(graphics:GetHeight() * PIXEL_SIZE)
    camera:SetZoom(2.0 * Min(graphics:GetWidth() / 1280.0, graphics:GetHeight() / 800.0))
    SetViewport(0, scene, camera)

    -- Tile map from tmx file
    local tileMapNode = scene:CreateChild("TileMap")
    local tileMap = tileMapNode:CreateComponent("TileMap2D")
    tileMap:SetTmxFile(GetResource("TmxFile2D", "Urho2D/Tilesets/atrium.tmx"))
    local info = tileMap:GetInfo()

    -- Spriter Imp character with a Lua state table standing in for the
    -- CharacterIsometric logic component
    local spriteNode = self:CreateCharacter(info, 0.0, Vector3(-5.0, 11.0, 0.0), 0.15)
    self.character = {
        spriteNode = spriteNode,
        wounded = false,
        killed = false,
        timer = 0.0,
        maxCoins = 0,
        remainingCoins = 0,
        remainingLifes = LIFES,
        -- Scale speed on Y by the tiles' aspect ratio
        moveSpeedScale = info.tileHeight / info.tileWidth,
        zoom = camera:GetZoom(),
    }

    -- Physics shapes from the tmx objects in the top "Physics" layer
    local tileMapLayer = tileMap:GetLayer(tileMap:GetNumLayers() - 1)
    self:CreateCollisionShapesFromTMXObjects(tileMapNode, tileMapLayer, info)

    -- Enemies at the "MovingEntities" placeholders (polyline paths)
    self:PopulateMovingEntities(tileMap:GetLayer(tileMap:GetNumLayers() - 2))

    -- Coins at the "Coins" placeholders (rectangles)
    local coinsLayer = tileMap:GetLayer(tileMap:GetNumLayers() - 3)
    self:PopulateCoins(coinsLayer)
    self.character.remainingCoins = coinsLayer:GetNumObjects()
    self.character.maxCoins = coinsLayer:GetNumObjects()

    -- Character and Mover logic run on the scene's update, mirroring C++
    -- LogicComponents: suspended while the scene update is disabled
    SubscribeToEvent(scene, "SceneUpdate", function(data)
        self:UpdateCharacter(data.TimeStep)
        self:UpdateMovers(data.TimeStep)
    end)
end

--------------------------------------------------------------------
-- Sample2D helpers (Utilities2D/Sample2D.cpp)
--------------------------------------------------------------------

function app:CreateCollisionShapesFromTMXObjects(tileMapNode, tileMapLayer, info)
    -- Static body on the root node
    local body = tileMapNode:CreateComponent("RigidBody2D")
    body:SetBodyType(BT2D.STATIC)

    for i = 0, tileMapLayer:GetNumObjects() - 1 do
        local tileMapObject = tileMapLayer:GetObject(i)

        local objectType = tileMapObject:GetObjectType()
        if objectType == OT2D.RECTANGLE then
            self:CreateRectangleShape(tileMapNode, tileMapObject, tileMapObject:GetSize(), info)
        elseif objectType == OT2D.ELLIPSE then
            -- Ellipses are built as circles (no ellipse in Box2D)
            self:CreateCircleShape(tileMapNode, tileMapObject, tileMapObject:GetSize().x / 2, info)
        elseif objectType == OT2D.POLYGON then
            self:CreatePolygonShape(tileMapNode, tileMapObject)
        elseif objectType == OT2D.POLYLINE then
            self:CreatePolyLineShape(tileMapNode, tileMapObject)
        end
    end
end

function app:CreateRectangleShape(node, object, size, info)
    local shape = node:CreateComponent("CollisionBox2D")
    shape:SetSize(size)
    if info.orientation == ORIENT2D.ORTHOGONAL then
        shape:SetCenter(object:GetPosition() + size / 2)
    else
        shape:SetCenter(object:GetPosition() + Vector2(info.tileWidth / 2, 0.0))
        shape:SetAngle(45.0) -- Isometric maps get diamond-shaped boxes
    end
    shape:SetFriction(0.8)
    if object:HasProperty("Friction") then
        shape:SetFriction(tonumber(object:GetProperty("Friction")))
    end
    return shape
end

function app:CreateCircleShape(node, object, radius, info)
    local shape = node:CreateComponent("CollisionCircle2D")
    local size = object:GetSize()
    if info.orientation == ORIENT2D.ORTHOGONAL then
        shape:SetCenter(object:GetPosition() + size / 2)
    else
        shape:SetCenter(object:GetPosition() + Vector2(info.tileWidth / 2, 0.0))
    end
    shape:SetRadius(radius)
    shape:SetFriction(0.8)
    if object:HasProperty("Friction") then
        shape:SetFriction(tonumber(object:GetProperty("Friction")))
    end
    return shape
end

function app:CreatePolygonShape(node, object)
    local shape = node:CreateComponent("CollisionPolygon2D")
    local numVertices = object:GetNumPoints()
    shape:SetVertexCount(numVertices)
    for i = 0, numVertices - 1 do
        shape:SetVertex(i, object:GetPoint(i))
    end
    shape:SetFriction(0.8)
    if object:HasProperty("Friction") then
        shape:SetFriction(tonumber(object:GetProperty("Friction")))
    end
    return shape
end

function app:CreatePolyLineShape(node, object)
    local shape = node:CreateComponent("CollisionChain2D")
    local numVertices = object:GetNumPoints()
    shape:SetVertexCount(numVertices)
    for i = 0, numVertices - 1 do
        shape:SetVertex(i, object:GetPoint(i))
    end
    shape:SetFriction(0.8)
    if object:HasProperty("Friction") then
        shape:SetFriction(tonumber(object:GetProperty("Friction")))
    end
    return shape
end

function app:CreateCharacter(info, friction, position, scale)
    local scene = self.scene
    local characterNode = scene:CreateChild("Characters")
    local spriteNode = characterNode:CreateChild("Imp")
    spriteNode:SetPosition(position)
    spriteNode:SetScale(scale)

    local animatedSprite = spriteNode:CreateComponent("AnimatedSprite2D")
    local animationSet = GetResource("AnimationSet2D", "Urho2D/imp/imp.scml")
    animatedSprite:SetAnimationSet(animationSet)
    animatedSprite:SetAnimation("idle")
    animatedSprite:SetLayer(3) -- Over the tile map and the Orcs

    local impBody = spriteNode:CreateComponent("RigidBody2D")
    impBody:SetBodyType(BT2D.DYNAMIC)
    impBody:SetAllowSleep(false)

    local shape = spriteNode:CreateComponent("CollisionCircle2D")
    shape:SetRadius(1.1)
    shape:SetFriction(friction)
    shape:SetRestitution(0.1)

    return spriteNode
end

function app:CreateEnemy()
    local scene = self.scene
    local node = scene:CreateChild("Enemy")
    local staticSprite = node:CreateComponent("StaticSprite2D")
    staticSprite:SetSprite(GetResource("Sprite2D", "Urho2D/Aster.png"))

    local body = node:CreateComponent("RigidBody2D")
    body:SetBodyType(BT2D.STATIC)

    local shape = node:CreateComponent("CollisionCircle2D")
    shape:SetRadius(0.25)
    return node
end

function app:CreateOrc()
    local scene = self.scene
    local node = scene:CreateChild("Orc")
    node:SetScale(scene:GetChild("Imp", true):GetScale())

    local animatedSprite = node:CreateComponent("AnimatedSprite2D")
    local animationSet = GetResource("AnimationSet2D", "Urho2D/Orc/Orc.scml")
    animatedSprite:SetAnimationSet(animationSet)
    animatedSprite:SetAnimation("run")
    animatedSprite:SetLayer(2)

    node:CreateComponent("RigidBody2D")
    local shape = node:CreateComponent("CollisionCircle2D")
    shape:SetRadius(1.3)
    shape:SetTrigger(true)
    return node
end

function app:CreateCoin()
    local scene = self.scene
    local node = scene:CreateChild("Coin")
    node:SetScale(0.5)

    local animatedSprite = node:CreateComponent("AnimatedSprite2D")
    local animationSet = GetResource("AnimationSet2D", "Urho2D/GoldIcon.scml")
    animatedSprite:SetAnimationSet(animationSet)
    animatedSprite:SetAnimation("idle")
    animatedSprite:SetLayer(4)

    local body = node:CreateComponent("RigidBody2D")
    body:SetBodyType(BT2D.STATIC)

    local shape = node:CreateComponent("CollisionCircle2D")
    shape:SetRadius(0.32)
    shape:SetTrigger(true)
    return node
end

function app:CreateMovingPlatform()
    local scene = self.scene
    local node = scene:CreateChild("MovingPlatform")
    node:SetScale(Vector3(3.0, 1.0, 0.0))

    local staticSprite = node:CreateComponent("StaticSprite2D")
    staticSprite:SetSprite(GetResource("Sprite2D", "Urho2D/Box.png"))

    local body = node:CreateComponent("RigidBody2D")
    body:SetBodyType(BT2D.STATIC)

    local shape = node:CreateComponent("CollisionBox2D")
    shape:SetSize(Vector2(0.32, 0.32))
    shape:SetFriction(0.8)
    return node
end

function app:PopulateMovingEntities(movingEntitiesLayer)
    local scene = self.scene
    -- Prototypes, cloned at each placeholder then removed
    local enemyNode = self:CreateEnemy()
    local orcNode = self:CreateOrc()
    local platformNode = self:CreateMovingPlatform()

    local movingEntities = scene:CreateChild("Moving Entities")
    enemyNode:SetParent(movingEntities)
    orcNode:SetParent(movingEntities)
    platformNode:SetParent(movingEntities)

    for i = 0, movingEntitiesLayer:GetNumObjects() - 1 do
        local movingObject = movingEntitiesLayer:GetObject(i)
        if movingObject:GetObjectType() == OT2D.POLYLINE then
            local movingClone
            local offset = Vector2(0.0, 0.0)
            local objectType = movingObject:GetType()
            if objectType == "Enemy" then
                movingClone = enemyNode:Clone()
                offset = Vector2(0.0, -0.32)
            elseif objectType == "Orc" then
                movingClone = orcNode:Clone()
            elseif objectType == "MovingPlatform" then
                movingClone = platformNode:Clone()
            else
                goto continue
            end
            movingClone:SetPosition2D(movingObject:GetPoint(0) + offset)

            -- Mover state table replaces the C++ Mover component
            local mover = {
                node = movingClone,
                path = self:CreatePathFromPoints(movingObject, offset),
                speed = 0.8,
                currentPathID = 2, -- 1-based index, C++ starts at 1 (0-based)
                emitTime = 0.0,
                fightTimer = 0.0,
                flip = 0.0,
            }
            table.insert(self.movers, mover)

            if movingObject:HasProperty("Speed") then
                mover.speed = tonumber(movingObject:GetProperty("Speed"))
            end
            ::continue::
        end
    end

    -- Remove the prototypes
    enemyNode:Remove()
    orcNode:Remove()
    platformNode:Remove()
end

function app:PopulateCoins(coinsLayer)
    local scene = self.scene
    local coinNode = self:CreateCoin()

    for i = 0, coinsLayer:GetNumObjects() - 1 do
        local coinObject = coinsLayer:GetObject(i)
        local coinClone = coinNode:Clone()
        coinClone:SetPosition2D(coinObject:GetPosition() + coinObject:GetSize() / 2 + Vector2(0.0, 0.16))
    end

    coinNode:Remove()
end

function app:CreatePathFromPoints(object, offset)
    local path = {}
    for i = 0, object:GetNumPoints() - 1 do
        path[i + 1] = object:GetPoint(i) + offset
    end
    return path
end

function app:CreateUIContent(demoTitle, remainingLifes, remainingCoins)
    local root = GetUIRoot()
    local font = GetResource("Font", "Fonts/Anonymous Pro.ttf")
    root:SetDefaultStyle(GetResource("XMLFile", "UI/DefaultStyle.xml"))

    -- Coins counter
    local coinsUI = root:CreateChild("BorderImage", "Coins")
    coinsUI:SetTexture(GetResource("Texture2D", "Urho2D/GoldIcon.png"))
    coinsUI:SetSize(50, 50)
    coinsUI:SetImageRect(IntRect(0, 64, 60, 128))
    coinsUI:SetAlignment(HA.LEFT, VA.TOP)
    coinsUI:SetPosition(5, 5)
    local coinsText = coinsUI:CreateChild("Text", "CoinsText")
    coinsText:SetAlignment(HA.CENTER, VA.CENTER)
    coinsText:SetFont(font, 24)
    coinsText:SetTextEffect(TE.SHADOW)
    coinsText:SetText(tostring(remainingCoins))

    -- Lifes counter
    local lifeUI = root:CreateChild("BorderImage", "Life")
    lifeUI:SetTexture(GetResource("Texture2D", "Urho2D/imp/imp_all.png"))
    lifeUI:SetSize(70, 80)
    lifeUI:SetAlignment(HA.RIGHT, VA.TOP)
    lifeUI:SetPosition(-5, 5)
    local lifeText = lifeUI:CreateChild("Text", "LifeText")
    lifeText:SetAlignment(HA.CENTER, VA.CENTER)
    lifeText:SetFont(font, 24)
    lifeText:SetTextEffect(TE.SHADOW)
    lifeText:SetText(tostring(remainingLifes))

    -- Fullscreen start/end UI
    local fullUI = root:CreateChild("Window", "FullUI")
    fullUI:SetStyleAuto()
    fullUI:SetSize(root:GetWidth(), root:GetHeight())
    fullUI:SetEnabled(false) -- Only the buttons react to input

    local title = fullUI:CreateChild("BorderImage", "Title")
    title:SetMinSize(fullUI:GetWidth(), 50)
    title:SetTexture(GetResource("Texture2D", "Textures/HeightMap.png"))
    title:SetFullImageRect()
    title:SetAlignment(HA.CENTER, VA.TOP)
    local titleText = title:CreateChild("Text", "TitleText")
    titleText:SetAlignment(HA.CENTER, VA.CENTER)
    titleText:SetFont(font, 24)
    titleText:SetText(demoTitle)

    local spriteUI = fullUI:CreateChild("BorderImage", "Sprite")
    spriteUI:SetTexture(GetResource("Texture2D", "Urho2D/imp/imp_all.png"))
    spriteUI:SetSize(238, 271)
    spriteUI:SetAlignment(HA.CENTER, VA.CENTER)
    spriteUI:SetPosition(0, -root:GetHeight() / 4)

    -- 'EXIT' button
    local exitButton = root:CreateChild("Button", "ExitButton")
    exitButton:SetStyleAuto()
    exitButton:SetFocusMode(FM.RESETFOCUS)
    exitButton:SetSize(100, 50)
    exitButton:SetAlignment(HA.CENTER, VA.CENTER)
    exitButton:SetPosition(-100, 0)
    local exitText = exitButton:CreateChild("Text", "ExitText")
    exitText:SetAlignment(HA.CENTER, VA.CENTER)
    exitText:SetFont(font, 24)
    exitText:SetText("EXIT")
    SubscribeToEvent(exitButton, "Released", function()
        GetSubsystem("Engine"):Exit()
    end)

    -- 'PLAY' button
    local playButton = root:CreateChild("Button", "PlayButton")
    playButton:SetStyleAuto()
    playButton:SetFocusMode(FM.RESETFOCUS)
    playButton:SetSize(100, 50)
    playButton:SetAlignment(HA.CENTER, VA.CENTER)
    playButton:SetPosition(100, 0)
    local playText = playButton:CreateChild("Text", "PlayText")
    playText:SetAlignment(HA.CENTER, VA.CENTER)
    playText:SetFont(font, 24)
    playText:SetText("PLAY")

    -- Instructions
    local instructionText = root:CreateChild("Text", "Instructions")
    instructionText:SetText("Use WASD keys or Arrows to move\n"
        .. "PageUp/PageDown/MouseWheel to zoom\n"
        .. "'Z' to toggle debug geometry\n"
        .. "Space to fight")
    instructionText:SetFont(font, 15)
    instructionText:SetTextAlignment(HA.CENTER)
    instructionText:SetAlignment(HA.CENTER, VA.CENTER)
    instructionText:SetPosition(0, root:GetHeight() / 4)

    GetSubsystem("Input"):SetMouseVisible(true)
end

function app:SpawnEffect(node)
    local particleNode = node:CreateChild("Emitter")
    particleNode:SetScale(0.5 / node:GetScale().x)
    local particleEmitter = particleNode:CreateComponent("ParticleEmitter2D")
    particleEmitter:SetLayer(2)
    particleEmitter:SetEffect(GetResource("ParticleEffect2D", "Urho2D/sun.pex"))
end

function app:PlaySoundEffect(soundName)
    local source = self.scene:CreateComponent("SoundSource")
    local sound = GetResource("Sound", "Sounds/" .. soundName)
    if sound then
        source:SetAutoRemoveMode(REMOVE.COMPONENT)
        source:Play(sound)
    end
end

--------------------------------------------------------------------
-- CharacterIsometric logic (49_Urho2DIsometricDemo/Character2D.cpp)
--------------------------------------------------------------------

function app:UpdateCharacter(timeStep)
    local ch = self.character
    if ch.killed then
        return
    end
    if ch.wounded then
        self:HandleWoundedState(timeStep)
        return
    end

    local node = ch.spriteNode
    local animatedSprite = node:GetComponent("AnimatedSprite2D")
    local input = GetSubsystem("Input")

    -- Direction, speed scaled by camera zoom
    local moveDir = Vector3(0.0, 0.0, 0.0)
    local speedX = Clamp(MOVE_SPEED_X / ch.zoom, 0.4, 1.0)
    local speedY = speedX

    if input:GetKeyDown(KEY.A) or input:GetKeyDown(KEY.LEFT) then
        moveDir = moveDir + Vector3.LEFT * speedX
        animatedSprite:SetFlipX(false)
    end
    if input:GetKeyDown(KEY.D) or input:GetKeyDown(KEY.RIGHT) then
        moveDir = moveDir + Vector3.RIGHT * speedX
        animatedSprite:SetFlipX(true)
    end

    if moveDir:LengthSquared() > 0.0 then
        speedY = speedX * ch.moveSpeedScale
    end

    if input:GetKeyDown(KEY.W) or input:GetKeyDown(KEY.UP) then
        moveDir = moveDir + Vector3.UP * speedY
    end
    if input:GetKeyDown(KEY.S) or input:GetKeyDown(KEY.DOWN) then
        moveDir = moveDir + Vector3.DOWN * speedY
    end

    -- Move
    if moveDir:LengthSquared() > 0.0 then
        node:Translate(moveDir * timeStep)
    end

    -- Animate
    if input:GetKeyDown(KEY.SPACE) then
        if animatedSprite:GetAnimation() ~= "attack" then
            animatedSprite:SetAnimation("attack", true)
        end
    elseif moveDir:LengthSquared() > 0.0 then
        if animatedSprite:GetAnimation() ~= "run" then
            animatedSprite:SetAnimation("run")
        end
    elseif animatedSprite:GetAnimation() ~= "idle" then
        animatedSprite:SetAnimation("idle")
    end
end

function app:HandleWoundedState(timeStep)
    local ch = self.character
    local node = ch.spriteNode
    local body = node:GetComponent("RigidBody2D")
    local animatedSprite = node:GetComponent("AnimatedSprite2D")

    if animatedSprite:GetAnimation() ~= "hit" then
        animatedSprite:SetAnimation("hit", true)
    end

    ch.timer = ch.timer + timeStep
    if ch.timer > 2.0 then
        ch.timer = 0.0

        -- Clear forces
        body:SetLinearVelocity(Vector2(0.0, 0.0))
        body:SetAwake(false)
        body:SetAwake(true)

        -- Remove particle emitter
        local emitter = node:GetChild("Emitter", true)
        if emitter then
            emitter:Remove()
        end

        -- Update lifes UI and counter
        ch.remainingLifes = ch.remainingLifes - 1
        GetUIRoot():GetChild("LifeText", true):SetText(tostring(ch.remainingLifes))

        ch.wounded = false

        if ch.remainingLifes == 0 then
            self:HandleDeath()
            return
        end

        -- Re-position to the nearest checkpoint
        if node:GetPosition().x < 15.0 then
            node:SetPosition(Vector3(-5.0, 11.0, 0.0))
        else
            node:SetPosition(Vector3(18.8, 9.2, 0.0))
        end
    end
end

function app:HandleDeath()
    local ch = self.character
    local node = ch.spriteNode
    local animatedSprite = node:GetComponent("AnimatedSprite2D")

    ch.killed = true

    -- Show game over UI
    local root = GetUIRoot()
    root:GetChild("Instructions", true):SetText("!!! GAME OVER !!!")
    root:GetChild("ExitButton", true):SetVisible(true)
    root:GetChild("PlayButton", true):SetVisible(true)
    GetSubsystem("Input"):SetMouseVisible(true)

    -- Put the character outside of the scene and magnify it
    node:SetPosition(Vector3(-20.0, 0.0, 0.0))
    node:SetScale(1.2)

    if animatedSprite:GetAnimation() ~= "dead" then
        animatedSprite:SetAnimation("dead")
    end
end

--------------------------------------------------------------------
-- Mover logic (Utilities2D/Mover.cpp)
--------------------------------------------------------------------

function app:UpdateMovers(timeStep)
    local dead = {}
    for i, mover in ipairs(self.movers) do
        if self:UpdateMover(mover, timeStep) then
            table.insert(dead, i)
        end
    end
    for i = #dead, 1, -1 do
        table.remove(self.movers, dead[i])
    end
end

-- Returns true when the mover's node has been removed
function app:UpdateMover(mover, timeStep)
    local node = mover.node
    local path = mover.path
    if #path < 2 then
        return false
    end

    -- Orc states (idle/wounded/fighting)
    if node:GetName() == "Orc" then
        local animatedSprite = node:GetComponent("AnimatedSprite2D")
        local anim = "run"

        if mover.emitTime > 0.0 then
            mover.emitTime = mover.emitTime + timeStep
            anim = "dead"
            if mover.emitTime >= 3.0 then
                node:Remove()
                return true
            end
        else
            if mover.fightTimer > 0.0 then
                anim = "attack"
                local imp = self.scene:GetChild("Imp", true)
                mover.flip = imp:GetPosition().x - node:GetPosition().x
                mover.fightTimer = mover.fightTimer + timeStep
                if mover.fightTimer >= 3.0 then
                    mover.fightTimer = 0.0
                end
            end
            animatedSprite:SetFlipX(mover.flip >= 0.0)
        end

        if animatedSprite:GetAnimation() ~= anim then
            animatedSprite:SetAnimation(anim)
        end
    end

    -- Don't move while fighting or wounded
    if mover.fightTimer > 0.0 or mover.emitTime > 0.0 then
        return false
    end

    -- Move toward the current waypoint
    local dir = path[mover.currentPathID] - node:GetPosition2D()
    local dirNormal = dir:Normalized()
    node:Translate(Vector3(dirNormal.x, dirNormal.y, 0.0) * math.abs(mover.speed) * timeStep)
    mover.flip = dir.x

    if math.abs(dir:Length()) < 0.1 then
        if mover.speed > 0.0 then
            if mover.currentPathID < #path then
                mover.currentPathID = mover.currentPathID + 1
            else
                -- Looping path: go back to the second waypoint
                if path[mover.currentPathID] == path[1] then
                    mover.currentPathID = 2
                    return false
                end
                -- Reverse otherwise
                mover.currentPathID = mover.currentPathID - 1
                mover.speed = -mover.speed
            end
        else
            if mover.currentPathID > 1 then
                mover.currentPathID = mover.currentPathID - 1
            else
                mover.currentPathID = 2
                mover.speed = -mover.speed
            end
        end
    end
    return false
end

--------------------------------------------------------------------
-- Main sample logic
--------------------------------------------------------------------

function app:Update(timeStep)
    if self.cameraNode then
        self:Zoom()
    end
    if GetSubsystem("Input"):GetKeyPress(KEY.Z) then
        self.drawDebug = not self.drawDebug
    end
end

function app:Zoom()
    local input = GetSubsystem("Input")
    local camera = self.cameraNode:GetComponent("Camera")
    local zoom = camera:GetZoom()

    if input:GetMouseMoveWheel() ~= 0 then
        zoom = Clamp(zoom + input:GetMouseMoveWheel() * 0.1, CAMERA_MIN_DIST, CAMERA_MAX_DIST)
        camera:SetZoom(zoom)
    end
    if input:GetKeyDown(KEY.PAGEUP) then
        zoom = Clamp(zoom * 1.01, CAMERA_MIN_DIST, CAMERA_MAX_DIST)
        camera:SetZoom(zoom)
    end
    if input:GetKeyDown(KEY.PAGEDOWN) then
        zoom = Clamp(zoom * 0.99, CAMERA_MIN_DIST, CAMERA_MAX_DIST)
        camera:SetZoom(zoom)
    end
end

function app:HandlePostUpdate()
    if not self.character then
        return
    end
    -- Camera tracks the character
    self.cameraNode:SetPosition(self.character.spriteNode:GetWorldPosition()
        + Vector3(0.0, 0.0, -10.0))
end

function app:HandlePostRenderUpdate()
    if not self.drawDebug then
        return
    end
    self.scene:GetComponent("PhysicsWorld2D"):DrawDebugGeometry()
    local tileMapNode = self.scene:GetChild("TileMap", true)
    tileMapNode:GetComponent("TileMap2D"):DrawDebugGeometry(
        self.scene:GetComponent("DebugRenderer"), false)
end

function app:HandleCollisionBegin(data)
    -- Get colliding node
    local hitNode = data.NodeA
    if hitNode:GetName() == "Imp" then
        hitNode = data.NodeB
    end
    local nodeName = hitNode:GetName()
    local character2DNode = self.scene:GetChild("Imp", true)

    -- Coins picking
    if nodeName == "Coin" then
        hitNode:Remove()
        self.character.remainingCoins = self.character.remainingCoins - 1
        if self.character.remainingCoins == 0 then
            GetUIRoot():GetChild("Instructions", true):SetText("!!! You have all the coins !!!")
        end
        GetUIRoot():GetChild("CoinsText", true):SetText(tostring(self.character.remainingCoins))
        self:PlaySoundEffect("Powerup.wav")
    end

    -- Interactions with Orcs
    if nodeName == "Orc" then
        local animatedSprite = character2DNode:GetComponent("AnimatedSprite2D")
        local deltaX = character2DNode:GetPosition().x - hitNode:GetPosition().x

        -- Orc killed when fighting in its direction
        if animatedSprite:GetAnimation() == "attack" and (deltaX < 0) == animatedSprite:GetFlipX() then
            local mover = self:GetMover(hitNode)
            if mover then
                mover.emitTime = 1
            end
            if not hitNode:GetChild("Emitter", true) then
                hitNode:GetComponent("RigidBody2D"):Remove() -- Remove Orc's body
                self:SpawnEffect(hitNode)
                self:PlaySoundEffect("BigExplosion.wav")
            end
        else
            -- Player killed when not fighting in the Orc's direction
            if not character2DNode:GetChild("Emitter", true) then
                self.character.wounded = true
                local mover = self:GetMover(hitNode)
                if mover then
                    mover.fightTimer = 1
                end
                self:SpawnEffect(character2DNode)
                self:PlaySoundEffect("BigExplosion.wav")
            end
        end
    end
end

function app:GetMover(node)
    for _, mover in ipairs(self.movers) do
        if mover.node == node then
            return mover
        end
    end
    return nil
end

function app:ReloadScene(reInit)
    -- Rebuild the scene content (see header note about the C++ snapshot
    -- reload); character counters survive in the Lua state table
    local lifes = self.character.remainingLifes
    local coins = self.character.remainingCoins
    if reInit then
        lifes = LIFES
        coins = self.character.maxCoins
    end

    self:BuildScene()

    self.character.remainingLifes = lifes
    self.character.remainingCoins = coins

    GetUIRoot():GetChild("LifeText", true):SetText(tostring(lifes))
    GetUIRoot():GetChild("CoinsText", true):SetText(tostring(coins))
end

function app:HandlePlayButton()
    local root = GetUIRoot()
    -- Remove fullscreen UI and unfreeze the scene
    local fullUI = root:GetChild("FullUI", true)
    if fullUI then
        fullUI:Remove()
        self.scene:SetUpdateEnabled(true)
    else
        self:ReloadScene(true)
    end

    -- Hide Instructions and Play/Exit buttons
    root:GetChild("Instructions", true):SetText("")
    root:GetChild("ExitButton", true):SetVisible(false)
    root:GetChild("PlayButton", true):SetVisible(false)

    -- Hide mouse cursor
    GetSubsystem("Input"):SetMouseVisible(false)
end

app:Run()

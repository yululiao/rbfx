-- LuaSamples/32_Physics2DConstraints/main.lua
-- Lua port of Source/Samples/32_Physics2DConstraints: a grid of cells, each
-- demonstrating one Box2D constraint type (Distance, Friction, Gear, Wheel,
-- Motor, Mouse-pick, Prismatic, Pulley, Revolute, Rope, Weld). Click and drag
-- sprites with the mouse via a temporary ConstraintMouse2D. Space toggles
-- debug geometry.

local PIXEL_SIZE = 0.01 -- world units per pixel

local app = Sample:new()
app.drawDebug = true

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.FREE)
    input:SetMouseVisible(true)

    self:CreateScene()
    self:CreateInstructions()
    SubscribeToEvent("PostRenderUpdate", function() self:HandlePostRenderUpdate() end)
    SubscribeToEvent("MouseButtonDown", function() self:HandleMouseButtonDown() end)
end

function app:CreateScene()
    local scene = CreateScene()
    self.scene = scene
    scene:CreateComponent("Octree")
    scene:CreateComponent("DebugRenderer")

    local physicsWorld = scene:CreateComponent("PhysicsWorld2D")
    physicsWorld:SetDrawJoint(true) -- Display the joints

    -- Orthographic camera
    local cameraNode = scene:CreateChild("Camera")
    cameraNode:SetPosition(Vector3(0.0, 0.0, 0.0))
    self.cameraNode = cameraNode

    local graphics = GetSubsystem("Graphics")
    local camera = cameraNode:CreateComponent("Camera")
    camera:SetOrthographic(true)
    camera:SetOrthoSize(graphics:GetHeight() * PIXEL_SIZE)
    camera:SetZoom(1.2 * math.min(graphics:GetWidth() / 1280.0, graphics:GetHeight() / 800.0))
    self.camera = camera

    SetViewport(0, scene, camera)

    -- Background color through the default zone
    local renderer = GetSubsystem("Renderer")
    renderer:GetDefaultZone():SetFogColor(Color(0.1, 0.1, 0.1))

    local cache = GetSubsystem("ResourceCache")

    -- 4x3 grid of static edges
    self.dummyBody = nil
    for i = 0, 4 do
        local edgeNode = scene:CreateChild("VerticalEdge")
        local edgeBody = edgeNode:CreateComponent("RigidBody2D")
        if not self.dummyBody then
            self.dummyBody = edgeBody -- used by mouse pick
        end
        local edgeShape = edgeNode:CreateComponent("CollisionEdge2D")
        edgeShape:SetVertices(Vector2(i * 2.5 - 5.0, -3.0), Vector2(i * 2.5 - 5.0, 3.0))
        edgeShape:SetFriction(0.5)
    end

    for j = 0, 3 do
        local edgeNode = scene:CreateChild("HorizontalEdge")
        edgeNode:CreateComponent("RigidBody2D")
        local edgeShape = edgeNode:CreateComponent("CollisionEdge2D")
        edgeShape:SetVertices(Vector2(-5.0, j * 2.0 - 3.0), Vector2(5.0, j * 2.0 - 3.0))
        edgeShape:SetFriction(0.5)
    end

    -- Box template (cloned for each demo cell)
    local box = scene:CreateChild("Box")
    box:SetPosition(Vector3(0.8, -2.0, 0.0))
    local boxSprite = box:CreateComponent("StaticSprite2D")
    boxSprite:SetSprite(cache:GetResource("Sprite2D", "Urho2D/Box.png"))
    local boxBody = box:CreateComponent("RigidBody2D")
    boxBody:SetBodyType(BT2D.DYNAMIC)
    boxBody:SetLinearDamping(0.0)
    boxBody:SetAngularDamping(0.0)
    local shape = box:CreateComponent("CollisionBox2D")
    shape:SetSize(Vector2(0.32, 0.32))
    shape:SetDensity(1.0)
    shape:SetFriction(0.5)
    shape:SetRestitution(0.1)

    -- Ball template (cloned for each demo cell)
    local ball = scene:CreateChild("Ball")
    ball:SetPosition(Vector3(1.8, -2.0, 0.0))
    local ballSprite = ball:CreateComponent("StaticSprite2D")
    ballSprite:SetSprite(cache:GetResource("Sprite2D", "Urho2D/Ball.png"))
    local ballBody = ball:CreateComponent("RigidBody2D")
    ballBody:SetBodyType(BT2D.DYNAMIC)
    ballBody:SetLinearDamping(0.0)
    ballBody:SetAngularDamping(0.0)
    local ballShape = ball:CreateComponent("CollisionCircle2D")
    ballShape:SetRadius(0.16)
    ballShape:SetDensity(1.0)
    ballShape:SetFriction(0.5)
    ballShape:SetRestitution(0.6)

    -- Polygon
    local polygon = scene:CreateChild("Polygon")
    polygon:SetPosition(Vector3(1.6, -2.0, 0.0))
    polygon:SetScale(0.7)
    local polygonSprite = polygon:CreateComponent("StaticSprite2D")
    polygonSprite:SetSprite(cache:GetResource("Sprite2D", "Urho2D/Aster.png"))
    polygon:CreateComponent("RigidBody2D"):SetBodyType(BT2D.DYNAMIC)
    local polygonShape = polygon:CreateComponent("CollisionPolygon2D")
    polygonShape:SetVertices({
        Vector2(-0.8, -0.3), Vector2(0.5, -0.8), Vector2(0.8, -0.3),
        Vector2(0.8, 0.5), Vector2(0.5, 0.9), Vector2(-0.5, 0.7)
    })
    polygonShape:SetDensity(1.0)
    polygonShape:SetFriction(0.3)
    polygonShape:SetRestitution(0.0)

    -- ConstraintDistance2D
    self:CreateFlag("ConstraintDistance2D", -4.97, 3.0)
    local boxDistanceNode = box:Clone()
    local ballDistanceNode = ball:Clone()
    local ballDistanceBody = ballDistanceNode:GetComponent("RigidBody2D")
    boxDistanceNode:SetPosition(Vector3(-4.5, 2.0, 0.0))
    ballDistanceNode:SetPosition(Vector3(-3.0, 2.0, 0.0))

    local constraintDistance = boxDistanceNode:CreateComponent("ConstraintDistance2D")
    constraintDistance:SetOtherBody(ballDistanceBody)
    constraintDistance:SetOwnerBodyAnchor(boxDistanceNode:GetPosition2D())
    constraintDistance:SetOtherBodyAnchor(ballDistanceNode:GetPosition2D())
    -- Make the constraint soft
    constraintDistance:SetFrequencyHz(4.0)
    constraintDistance:SetDampingRatio(0.5)

    -- ConstraintFriction2D
    self:CreateFlag("ConstraintFriction2D", 0.03, 1.0)
    local boxFrictionNode = box:Clone()
    local ballFrictionNode = ball:Clone()
    boxFrictionNode:SetPosition(Vector3(0.5, 0.0, 0.0))
    ballFrictionNode:SetPosition(Vector3(1.5, 0.0, 0.0))

    local constraintFriction = boxFrictionNode:CreateComponent("ConstraintFriction2D")
    constraintFriction:SetOtherBody(ballFrictionNode:GetComponent("RigidBody2D"))

    -- ConstraintGear2D
    self:CreateFlag("ConstraintGear2D", -4.97, -1.0)
    local baseNode = box:Clone()
    baseNode:GetComponent("RigidBody2D"):SetBodyType(BT2D.STATIC)
    baseNode:SetPosition(Vector3(-3.7, -2.5, 0.0))
    local ball1Node = ball:Clone()
    ball1Node:SetPosition(Vector3(-4.5, -2.0, 0.0))
    local ball1Body = ball1Node:GetComponent("RigidBody2D")
    local ball2Node = ball:Clone()
    ball2Node:SetPosition(Vector3(-3.0, -2.0, 0.0))
    local ball2Body = ball2Node:GetComponent("RigidBody2D")

    local gear1 = baseNode:CreateComponent("ConstraintRevolute2D")
    gear1:SetOtherBody(ball1Body)
    gear1:SetAnchor(ball1Node:GetPosition2D())
    local gear2 = baseNode:CreateComponent("ConstraintRevolute2D")
    gear2:SetOtherBody(ball2Body)
    gear2:SetAnchor(ball2Node:GetPosition2D())

    local constraintGear = ball1Node:CreateComponent("ConstraintGear2D")
    constraintGear:SetOtherBody(ball2Body)
    constraintGear:SetOwnerConstraint(gear1)
    constraintGear:SetOtherConstraint(gear2)
    constraintGear:SetRatio(1.0)

    ball1Body:ApplyAngularImpulse(0.015, true) -- Animate

    -- Vehicle from a compound of 2 ConstraintWheel2Ds
    self:CreateFlag("ConstraintWheel2Ds compound", -2.45, -1.0)
    local car = box:Clone()
    car:SetScale(Vector3(4.0, 1.0, 0.0))
    car:SetPosition(Vector3(-1.2, -2.3, 0.0))
    car:GetComponent("StaticSprite2D"):SetOrderInLayer(0) -- draw car on top
    local ball1WheelNode = ball:Clone()
    ball1WheelNode:SetPosition(Vector3(-1.6, -2.5, 0.0))
    local ball2WheelNode = ball:Clone()
    ball2WheelNode:SetPosition(Vector3(-0.8, -2.5, 0.0))

    local wheel1 = car:CreateComponent("ConstraintWheel2D")
    wheel1:SetOtherBody(ball1WheelNode:GetComponent("RigidBody2D"))
    wheel1:SetAnchor(ball1WheelNode:GetPosition2D())
    wheel1:SetAxis(Vector2(0.0, 1.0))
    wheel1:SetMaxMotorTorque(20.0)
    wheel1:SetFrequencyHz(4.0)
    wheel1:SetDampingRatio(0.4)

    local wheel2 = car:CreateComponent("ConstraintWheel2D")
    wheel2:SetOtherBody(ball2WheelNode:GetComponent("RigidBody2D"))
    wheel2:SetAnchor(ball2WheelNode:GetPosition2D())
    wheel2:SetAxis(Vector2(0.0, 1.0))
    wheel2:SetMaxMotorTorque(10.0)
    wheel2:SetFrequencyHz(4.0)
    wheel2:SetDampingRatio(0.4)

    -- ConstraintMotor2D
    self:CreateFlag("ConstraintMotor2D", 2.53, -1.0)
    local boxMotorNode = box:Clone()
    boxMotorNode:GetComponent("RigidBody2D"):SetBodyType(BT2D.STATIC)
    local ballMotorNode = ball:Clone()
    boxMotorNode:SetPosition(Vector3(3.8, -2.1, 0.0))
    ballMotorNode:SetPosition(Vector3(3.8, -1.5, 0.0))

    local constraintMotor = boxMotorNode:CreateComponent("ConstraintMotor2D")
    constraintMotor:SetOtherBody(ballMotorNode:GetComponent("RigidBody2D"))
    constraintMotor:SetLinearOffset(Vector2(0.0, 0.8))
    constraintMotor:SetAngularOffset(0.1)
    constraintMotor:SetMaxForce(5.0)
    constraintMotor:SetMaxTorque(10.0)
    constraintMotor:SetCorrectionFactor(1.0)
    constraintMotor:SetCollideConnected(true)

    -- ConstraintMouse2D is demonstrated in HandleMouseButtonDown
    self:CreateFlag("ConstraintMouse2D", 0.03, -1.0)

    -- ConstraintPrismatic2D
    self:CreateFlag("ConstraintPrismatic2D", 2.53, 3.0)
    local boxPrismaticNode = box:Clone()
    boxPrismaticNode:GetComponent("RigidBody2D"):SetBodyType(BT2D.STATIC)
    local ballPrismaticNode = ball:Clone()
    boxPrismaticNode:SetPosition(Vector3(3.3, 2.5, 0.0))
    ballPrismaticNode:SetPosition(Vector3(4.3, 2.0, 0.0))

    local constraintPrismatic = boxPrismaticNode:CreateComponent("ConstraintPrismatic2D")
    constraintPrismatic:SetOtherBody(ballPrismaticNode:GetComponent("RigidBody2D"))
    constraintPrismatic:SetAxis(Vector2(1.0, 1.0)) -- slide from [0,0] to [1,1]
    constraintPrismatic:SetAnchor(Vector2(4.0, 2.0))
    constraintPrismatic:SetLowerTranslation(-1.0)
    constraintPrismatic:SetUpperTranslation(0.5)
    constraintPrismatic:SetEnableLimit(true)
    constraintPrismatic:SetMaxMotorForce(1.0)
    constraintPrismatic:SetMotorSpeed(0.0)

    -- ConstraintPulley2D
    self:CreateFlag("ConstraintPulley2D", 0.03, 3.0)
    local boxPulleyNode = box:Clone()
    local ballPulleyNode = ball:Clone()
    boxPulleyNode:SetPosition(Vector3(0.5, 2.0, 0.0))
    ballPulleyNode:SetPosition(Vector3(2.0, 2.0, 0.0))

    local constraintPulley = boxPulleyNode:CreateComponent("ConstraintPulley2D")
    constraintPulley:SetOtherBody(ballPulleyNode:GetComponent("RigidBody2D"))
    constraintPulley:SetOwnerBodyAnchor(boxPulleyNode:GetPosition2D())
    constraintPulley:SetOtherBodyAnchor(ballPulleyNode:GetPosition2D())
    constraintPulley:SetOwnerBodyGroundAnchor(boxPulleyNode:GetPosition2D() + Vector2(0.0, 1.0))
    constraintPulley:SetOtherBodyGroundAnchor(ballPulleyNode:GetPosition2D() + Vector2(0.0, 1.0))
    constraintPulley:SetRatio(1.0)

    -- ConstraintRevolute2D
    self:CreateFlag("ConstraintRevolute2D", -2.45, 3.0)
    local boxRevoluteNode = box:Clone()
    boxRevoluteNode:GetComponent("RigidBody2D"):SetBodyType(BT2D.STATIC)
    local ballRevoluteNode = ball:Clone()
    boxRevoluteNode:SetPosition(Vector3(-2.0, 1.5, 0.0))
    ballRevoluteNode:SetPosition(Vector3(-1.0, 2.0, 0.0))

    local constraintRevolute = boxRevoluteNode:CreateComponent("ConstraintRevolute2D")
    constraintRevolute:SetOtherBody(ballRevoluteNode:GetComponent("RigidBody2D"))
    constraintRevolute:SetAnchor(Vector2(-1.0, 1.5))
    constraintRevolute:SetLowerAngle(-1.0) -- radians
    constraintRevolute:SetUpperAngle(0.5) -- radians
    constraintRevolute:SetEnableLimit(true)
    constraintRevolute:SetMaxMotorTorque(10.0)
    constraintRevolute:SetMotorSpeed(0.0)
    constraintRevolute:SetEnableMotor(true)

    -- ConstraintRope2D
    self:CreateFlag("ConstraintRope2D", -4.97, 1.0)
    local boxRopeNode = box:Clone()
    boxRopeNode:GetComponent("RigidBody2D"):SetBodyType(BT2D.STATIC)
    local ballRopeNode = ball:Clone()
    boxRopeNode:SetPosition(Vector3(-3.7, 0.7, 0.0))
    ballRopeNode:SetPosition(Vector3(-4.5, 0.0, 0.0))

    local constraintRope = boxRopeNode:CreateComponent("ConstraintRope2D")
    constraintRope:SetOtherBody(ballRopeNode:GetComponent("RigidBody2D"))
    constraintRope:SetOwnerBodyAnchor(Vector2(0.0, -0.5))
    constraintRope:SetMaxLength(0.9)
    constraintRope:SetCollideConnected(true)

    -- ConstraintWeld2D
    self:CreateFlag("ConstraintWeld2D", -2.45, 1.0)
    local boxWeldNode = box:Clone()
    local ballWeldNode = ball:Clone()
    boxWeldNode:SetPosition(Vector3(-0.5, 0.0, 0.0))
    ballWeldNode:SetPosition(Vector3(-2.0, 0.0, 0.0))

    local constraintWeld = boxWeldNode:CreateComponent("ConstraintWeld2D")
    constraintWeld:SetOtherBody(ballWeldNode:GetComponent("RigidBody2D"))
    constraintWeld:SetAnchor(boxWeldNode:GetPosition2D())
    constraintWeld:SetFrequencyHz(4.0)
    constraintWeld:SetDampingRatio(0.5)

    -- ConstraintWheel2D
    self:CreateFlag("ConstraintWheel2D", 2.53, 1.0)
    local boxWheelNode = box:Clone()
    local ballWheelNode = ball:Clone()
    boxWheelNode:SetPosition(Vector3(3.8, 0.0, 0.0))
    ballWheelNode:SetPosition(Vector3(3.8, 0.9, 0.0))

    local constraintWheel = boxWheelNode:CreateComponent("ConstraintWheel2D")
    constraintWheel:SetOtherBody(ballWheelNode:GetComponent("RigidBody2D"))
    constraintWheel:SetAnchor(ballWheelNode:GetPosition2D())
    constraintWheel:SetAxis(Vector2(0.0, 1.0))
    constraintWheel:SetEnableMotor(true)
    constraintWheel:SetMaxMotorTorque(1.0)
    constraintWheel:SetMotorSpeed(0.0)
    constraintWheel:SetFrequencyHz(4.0)
    constraintWheel:SetDampingRatio(0.5)
    constraintWheel:SetCollideConnected(true)
end

-- Text3D flags stick to the 2D plane (affected by zoom)
function app:CreateFlag(text, x, y)
    local cache = GetSubsystem("ResourceCache")
    local flagNode = self.scene:CreateChild("Flag")
    flagNode:SetPosition(Vector3(x, y, 0.0))
    local flag3D = flagNode:CreateComponent("Text3D")
    flag3D:SetText(text)
    flag3D:SetFont(cache:GetResource("Font", "Fonts/Anonymous Pro.ttf"), 15)
end

function app:CreateInstructions()
    local cache = GetSubsystem("ResourceCache")

    local instructionText = GetUIRoot():CreateChild("Text")
    instructionText:SetText("Use WASD keys and mouse to move, Use PageUp PageDown to zoom.\n Space to toggle debug geometry and joints - F5 to save the scene.")
    instructionText:SetFont(cache:GetResource("Font", "Fonts/Anonymous Pro.ttf"), 15)
    instructionText:SetTextAlignment(HA.CENTER)
    instructionText:SetHorizontalAlignment(HA.CENTER)
    instructionText:SetVerticalAlignment(VA.CENTER)
    instructionText:SetPosition(0, GetUIRoot():GetHeight() / 4)
end

function app:MoveCamera(timeStep)
    -- Do not move if the UI has a focused element (the console)
    if GetSubsystem("UI"):GetFocusElement() then
        return
    end

    local input = GetSubsystem("Input")
    local MOVE_SPEED = 4.0

    if input:GetKeyDown(KEY.W) then
        self.cameraNode:Translate(Vector3.UP * MOVE_SPEED * timeStep)
    end
    if input:GetKeyDown(KEY.S) then
        self.cameraNode:Translate(Vector3.DOWN * MOVE_SPEED * timeStep)
    end
    if input:GetKeyDown(KEY.A) then
        self.cameraNode:Translate(Vector3.LEFT * MOVE_SPEED * timeStep)
    end
    if input:GetKeyDown(KEY.D) then
        self.cameraNode:Translate(Vector3.RIGHT * MOVE_SPEED * timeStep)
    end

    if input:GetKeyDown(KEY.PAGEUP) then
        self.camera:SetZoom(self.camera:GetZoom() * 1.01)
    end
    if input:GetKeyDown(KEY.PAGEDOWN) then
        self.camera:SetZoom(self.camera:GetZoom() * 0.99)
    end
end

function app:GetMousePositionXY()
    local input = GetSubsystem("Input")
    local graphics = GetSubsystem("Graphics")
    local pos = input:GetMousePosition()
    local worldPoint = self.camera:ScreenToWorldPoint(Vector3(
        pos.x / graphics:GetWidth(), pos.y / graphics:GetHeight(), 0.0))
    return Vector2(worldPoint.x, worldPoint.y)
end

function app:Update(timeStep)
    self:MoveCamera(timeStep)

    local input = GetSubsystem("Input")
    if input:GetKeyPress(KEY.SPACE) then
        self.drawDebug = not self.drawDebug
    end
end

function app:HandlePostRenderUpdate()
    local physicsWorld = self.scene:GetComponent("PhysicsWorld2D")
    if self.drawDebug and physicsWorld then
        physicsWorld:DrawDebugGeometry()
    end
end

function app:HandleMouseButtonDown()
    local input = GetSubsystem("Input")
    local physicsWorld = self.scene:GetComponent("PhysicsWorld2D")
    local pos = input:GetMousePosition()
    local rigidBody = physicsWorld:GetRigidBody(pos.x, pos.y)
    if rigidBody then
        self.pickedNode = rigidBody:GetNode()
        self.pickedNode:GetComponent("StaticSprite2D"):SetColor(Color(1.0, 0.0, 0.0, 1.0))

        -- Temporary mouse constraint on the picked node for grasping
        local constraintMouse = self.pickedNode:CreateComponent("ConstraintMouse2D")
        constraintMouse:SetTarget(self:GetMousePositionXY())
        constraintMouse:SetMaxForce(1000.0 * rigidBody:GetMass())
        constraintMouse:SetCollideConnected(true)
        constraintMouse:SetOtherBody(self.dummyBody)
    end
    SubscribeToEvent("MouseMove", function() self:HandleMouseMove() end)
    SubscribeToEvent("MouseButtonUp", function() self:HandleMouseButtonUp() end)
end

function app:HandleMouseButtonUp()
    if self.pickedNode then
        self.pickedNode:GetComponent("StaticSprite2D"):SetColor(Color(1.0, 1.0, 1.0, 1.0))
        self.pickedNode:RemoveComponent("ConstraintMouse2D")
        self.pickedNode = nil
    end
    UnsubscribeEvent("MouseMove")
    UnsubscribeEvent("MouseButtonUp")
end

function app:HandleMouseMove()
    if self.pickedNode then
        local constraintMouse = self.pickedNode:GetComponent("ConstraintMouse2D")
        if constraintMouse then
            constraintMouse:SetTarget(self:GetMousePositionXY())
        end
    end
end

app:Run()

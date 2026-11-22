-- Scripts/game/World.lua
-- 背景整图、水缸、相机适配与屏幕→图像坐标换算。
-- 背景是一整张 1024x1792 的图，不做瓦片：省掉接缝/棋盘/岸边过渡三类问题，
-- 可行走区域改由 game/Config.lua 的多边形描述。

local Config = require("game.Config")

local World = {}

local cache_ = GetSubsystem("ResourceCache")

function World.Spr(path)
    local sp = cache_:GetResource("Sprite2D", path)
    if not sp then
        print("FishingGuy MARK missing sprite: " .. path)
    end
    return sp
end

--- 建一个 2D 精灵节点。hotSpot 用「距图片底边的比例」理解：
--- (0.5, 0) 是底边中心，(0.5, 1) 是顶边中心（见 Sprite2D::GetDrawRectangle）。
--- 不设 BlendMode：StaticSprite2D 构造默认就是 BLEND_ALPHA（StaticSprite2D.cpp:42），
--- 而 BLEND 常量表挂在 UI 模块的绑定里，小游戏包 URHO3D_UI=OFF，真机上 BLEND 是 nil。
function World.NewSprite(scene, name, path, layer, hotX, hotY)
    local node = scene:CreateChild(name)
    local comp = node:CreateComponent("StaticSprite2D")
    local sp = World.Spr(path)
    if sp then
        comp:SetSprite(sp)
    end
    comp:SetColor(Color(1, 1, 1, 1))
    comp:SetUseHotSpot(true)
    comp:SetHotSpot(Vector2(hotX or 0.5, hotY or 0.5))
    comp:SetLayer(layer or 0)
    node:SetPosition(Vector3(0, 0, 0))
    return node, comp
end

--- 节点位置：图像像素 → 世界坐标
function World.SetNodePos(node, px, py)
    node:SetPosition(Config.ToWorld(px, py))
end

--- contain 适配：整张图一定看得见（编辑器 GameView 比例任意），
--- 真机竖屏 9:16 与图 4:7 几乎同比例，上下留白不足 2%。
--- 注意 rbfx 的正交尺寸是「可视全高」（Camera::UpdateProjection 用 1/(size*0.5)），
--- 宽 = 高 × 视口比例，且每帧由 CameraProcessor 按真实视口刷新，所以只传高度一个数。
function World.FitCamera(camera, canvasW, canvasH)
    local viewH = Config.IMG_H * Config.PIXEL_SIZE
    local viewW = Config.IMG_W * Config.PIXEL_SIZE
    local aspect = canvasW / math.max(canvasH, 1.0)
    camera:SetOrthoSize(math.max(viewH, viewW / math.max(aspect, 0.0001)))
end

--- 画布像素（左上原点、y 向下）→ 图像像素
function World.ScreenToImage(camera, canvasW, canvasH, sx, sy)
    local p = camera:ScreenToWorldPoint(Vector3(sx / math.max(canvasW, 1.0), sy / math.max(canvasH, 1.0), 10.0))
    return Config.ToImage(p)
end

function World.Build(scene)
    World.NewSprite(scene, "Yard", "Textures/Background/fishing_yard.png", 0, 0.5, 0.5)
    local jarNode = World.NewSprite(scene, "FishJar", "Textures/Prop/fish_jar.png", 1, 0.5, 0.10)
    World.SetNodePos(jarNode, Config.JAR.x, Config.JAR.y)
end

return World

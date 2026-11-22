-- Scripts/game/Line.lua
-- 鱼线：竿尖到浮漂之间的一条二次贝塞尔曲线，用若干段 1×1 白图精灵（Textures/Prop/line_px.png）拼出来。
--
-- 为什么不用 DebugRenderer：它的几何只在编辑器 debug view 的那个 pass 里提交，
-- 游戏 renderpath（passes 为空、走 Default）根本不画它。
-- 为什么不用 CustomGeometry：没进 Lua 绑定。
--
-- 每段 = 一个节点：位置放在段起点，绕 Z 转到段方向，X 拉到段长。
-- Urho2D 的 PIXEL_SIZE 是 0.01（Sprite2D.cpp:153），和 Config.PIXEL_SIZE 同一个数，
-- 所以 1 精灵像素 = 1 图像像素，scale.x 直接填段长的图像像素数就行。
-- 抛竿时浮漂沿抛物线飞，线的末端跟着它走，线自然就"一点点放出去"了。

local Config = require("game.Config")
local World = require("game.World")

local Line = {}
Line.__index = Line

function Line.Build(scene)
    local self = setmetatable({}, Line)
    self.segs = {}
    for i = 1, Config.LINE_SEGMENTS do
        local node, comp = World.NewSprite(scene, "Line" .. i,
            "Textures/Prop/line_px.png", Config.LINE_LAYER, 0, 0.5)
        comp:SetColor(Config.LINE_COLOR)
        comp:SetEnabled(false)
        self.segs[i] = { node = node, comp = comp }
    end
    self.shown = false
    return self
end

--- 图像像素坐标（y 向下）画 A→B 的垂曲线。sagPx = 控制点相对中点往下坠的像素数。
function Line:Draw(ax, ay, bx, by, sagPx)
    local n = Config.LINE_SEGMENTS
    local cx, cy = (ax + bx) * 0.5, (ay + by) * 0.5 + sagPx
    local px, py = ax, ay
    for i = 1, n do
        local t = i / n
        local u = 1.0 - t
        local qx = u * u * ax + 2 * u * t * cx + t * t * bx
        local qy = u * u * ay + 2 * u * t * cy + t * t * by
        local dx, dy = qx - px, qy - py
        local lenPx = math.sqrt(dx * dx + dy * dy)
        local seg = self.segs[i]
        if lenPx >= 0.5 then
            -- Config.ToWorld 返回的是 Vector3，不是两个数
            seg.node:SetPosition(Config.ToWorld(px, py))
            -- 图像 y 向下、世界 y 向上，角度要用世界增量算
            seg.node:SetRotation2D(math.deg(math.atan(-dy, dx)))
            seg.node:SetScale(Vector3(lenPx, Config.LINE_THICK_PX, 1.0))
            seg.comp:SetEnabled(true)
        else
            seg.comp:SetEnabled(false)
        end
        px, py = qx, qy
    end
    self.shown = true
end

function Line:Hide()
    if not self.shown then
        return
    end
    self.shown = false
    for i = 1, #self.segs do
        self.segs[i].comp:SetEnabled(false)
    end
end

return Line

-- Scripts/game/PowerBar.lua
-- 蓄力条：滑动抛竿时挂在人物头顶的横条，从左往右跟手涨。
--
-- 和鱼线一样用 1×1 白图精灵（Textures/Prop/line_px.png）当矩形刷：
-- 1 精灵像素 = 1 图像像素，所以 scale 直接填像素尺寸。
-- 三层分别是外框/底槽/绿色填充，各占一层保证压叠顺序（同层排序不保证）。
-- 填充的 hotSpot 是 (0, 0.5)，节点位置就是条身的左端中点，往右长。

local Config = require("game.Config")
local World = require("game.World")

local PowerBar = {}
PowerBar.__index = PowerBar

local PX = "Textures/Prop/line_px.png"

function PowerBar.Build(scene)
    local B = Config.POWER_BAR
    local self = setmetatable({}, PowerBar)
    self.w, self.h = B.w, B.h

    self.frameNode, self.frame = World.NewSprite(scene, "PowerFrame", PX, B.layerFrame, 0.5, 0.5)
    self.backNode, self.back = World.NewSprite(scene, "PowerBack", PX, B.layerBack, 0.5, 0.5)
    self.fillNode, self.fill = World.NewSprite(scene, "PowerFill", PX, B.layerFill, 0.0, 0.5)

    self.frame:SetColor(Config.POWER_BAR_FRAME)
    self.back:SetColor(Config.POWER_BAR_BACK)
    self.fill:SetColor(Config.POWER_BAR_FILL)

    local fw, fh = B.w + B.pad * 2, B.h + B.pad * 2
    self.frameNode:SetScale(Vector3(fw, fh, 1.0))
    self.backNode:SetScale(Vector3(B.w, B.h, 1.0))
    self.fillNode:SetScale(Vector3(B.w, B.h, 1.0))

    self:Hide()
    return self
end

--- px, py = 人物脚底（图像像素）；t = 0..1 力度
function PowerBar:Show(px, py, t)
    local B = Config.POWER_BAR
    local cy = py - B.aboveHead - B.gap - self.h * 0.5
    self.frameNode:SetPosition(Config.ToWorld(px, cy))
    self.backNode:SetPosition(Config.ToWorld(px, cy))
    self.fillNode:SetPosition(Config.ToWorld(px - self.w * 0.5, cy))
    local w = self.w * math.min(math.max(t, 0.0), 1.0)
    self.fill:SetEnabled(w > 0.5)
    if w > 0.5 then
        self.fillNode:SetScale(Vector3(w, self.h, 1.0))
    end
    self.frame:SetEnabled(true)
    self.back:SetEnabled(true)
end

function PowerBar:Hide()
    self.frame:SetEnabled(false)
    self.back:SetEnabled(false)
    self.fill:SetEnabled(false)
end

return PowerBar

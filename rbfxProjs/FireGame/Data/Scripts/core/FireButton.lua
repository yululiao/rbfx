--
-- core/FireButton.lua —— 右下角开火键的输入判定
--
-- 职责与 core/Joystick.lua 同一模式：本模块不画任何东西——开火键的视觉（软边外圈 + 圆内
-- 一颗子弹图标）在 Data/UI/GameHud.rml 的 #fire_btn 里，这里只回答一件事：
-- "玩家的手指现在有没有按着它"。
--
-- 为什么不用 RmlUi 的 data-event-mousedown / data-event-mouseup：RmlUi 的 mouseup 派发给
-- "抬起那一刻指针下的元素"，手指按住开火键滑出圆外再抬，元素收不到释放事件，扳机就永久
-- 卡在按下态（松开也不会停火）。摇杆早就是这个理由自己读 Input，开火键跟它保持一致。
--
-- 坐标系：全部是画布像素（= rml 的 dp 基础值 × UI_SCALE），由持有者换算后 SetCenter。
-- 与 GameHud.rml 的同步点：#fire_btn 按 right/bottom 锚边，
--   圆心 = (画布宽 - (right + width/2), 画布高 - (bottom + height/2))，
--   随画布尺寸变，所以 main.lua 每帧 SetCenter（与 StickCenter 同一手法）。
--
-- 按住后不按区域续判：手指滑到圆外（甚至拖到屏幕另一头）仍算按住，抬起才停火——走位
-- 时手指本来就会漂，中途断火比按不准更难受。
--

---@class FireButton 圆形开火键的按住判定
local FireButton = {}
FireButton.__index = FireButton

-- 鼠标充当指针时的哨兵 ID（真实 touchID 从 0 起，与 core/Joystick.lua 同值）。
local MOUSE_ID = -1

--- 构造。config 字段全部可选，缺省值即 GameHud.rml 当前的布局。
---@param config? { cx?: number, cy?: number, radius?: number }
---@return FireButton
function FireButton:New(config)
    config = config or {}
    local self = setmetatable({}, FireButton)
    self.cx_ = config.cx or 1000.0       -- 圆心 X（画布像素）
    self.cy_ = config.cy or 600.0        -- 圆心 Y（画布像素，原点左上）
    self.radius_ = config.radius or 118.0  -- 起手判定半径（画布像素）
    self.ptrId_ = nil                    -- 当前按住的指针（手指 ID，或鼠标哨兵）
    self.held_ = false
    return self
end

--- 圆心随画布尺寸重定位（底/右锚元素必须跟着走，否则判定的圆和画出来的圆错位）。
---@param cx number
---@param cy number
function FireButton:SetCenter(cx, cy)
    self.cx_ = cx
    self.cy_ = cy
end

--- 判定半径重设（UI_SCALE 变化时由 main.lua 同步）。
---@param radius number 画布像素
function FireButton:SetRadius(radius)
    self.radius_ = radius
end

--- 采样点（画布像素）是否落在开火键上。
---@param dx number
---@param dy number
---@return boolean
function FireButton:Contains(dx, dy)
    local ox = dx - self.cx_
    local oy = dy - self.cy_
    return math.sqrt(ox * ox + oy * oy) <= self.radius_
end

--- 每帧采样：返回扳机是否按着。
---@param input Input 引擎 Input 子系统
---@param excludeId number|nil 已经被别的控件锁定的指针（摇杆那根），不抢
---@return boolean held
function FireButton:Update(input, excludeId)
    local numTouches = input:GetNumTouches()
    local tracked = false
    local fresh = false
    for i = 0, numTouches - 1 do
        local touch = input:GetTouch(i)
        if touch then
            if touch.touchID == self.ptrId_ then
                tracked = true
            elseif not fresh and touch.touchID ~= excludeId
                and self:Contains(touch.position.x, touch.position.y) then
                fresh = true
                self.ptrId_ = touch.touchID
            end
        end
    end

    local held = tracked or fresh

    -- 无手指时桌面/编辑器用鼠标左键顶上（与摇杆同一套退化路径）。
    -- 已锁定时不按区域续判；未锁定时要求按下落在圆内。
    if not held then
        local MOUSEB_LEFT = MOUSEB and MOUSEB.LEFT or 0
        if input:GetMouseButtonDown(MOUSEB_LEFT) then
            local mouse = input:GetMousePosition()
            if self.ptrId_ == MOUSE_ID
                or (self.ptrId_ == nil and self:Contains(mouse.x, mouse.y)) then
                self.ptrId_ = MOUSE_ID
                held = true
            end
        end
    end

    if not held then
        self.ptrId_ = nil
    end
    self.held_ = held
    return held
end

--- 清掉按住态（切到自动开火、或模态面板吞输入时调）。
function FireButton:Reset()
    self.ptrId_ = nil
    self.held_ = false
end

---@return number|nil 当前锁定的指针 ID（无则 nil）。供同屏控件（摇杆）避让：
--- 开火键与摇杆都在右下角落附近时，摇杆要排掉这里已认领的那根手指。
function FireButton:GetPointerId()
    return self.ptrId_
end

---@return boolean 当前是否按着
function FireButton:IsHeld()
    return self.held_
end

return FireButton

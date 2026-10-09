--
-- core/Joystick.lua —— 虚拟摇杆输入节点
--
-- 职责：把一次触摸（桌面/编辑器 Play 下退化为鼠标左键拖拽）换算成归一化方向向量。
-- 它不画任何东西——摇杆的视觉在 Data/UI/Hud.rml（底环）与 Data/UI/Knob.rml（帽）
-- 里，由 UI 编辑器维护；本模块只负责输入判定，帽的位置由 main.lua 每帧回写。
--
-- 坐标系约定
--   * 所有几何量用"设计像素"表达（与 Hud.rml 里 author 的 px 同一坐标系）：
--     RmlUi 的 context scale 把设计像素整体缩放到真实分辨率，所以这里不需要
--     知道屏幕尺寸，只需要知道 screen->design 的换算系数 1/scale。
--   * 触摸/鼠标坐标是真实屏幕像素（原点在左上，y 向下），先除以 scale 落到设计像素。
--   * 输出的 y_ 沿用屏幕语义（向上为负），由调用方决定怎么映射到世界轴。
--
-- 与 Hud.rml 的同步点：环心 = #stick_ring 的 left + width/2 与
--   "可见设计高度 - bottom - height/2"。高度那项随画布宽高比变化（body 是
--   .fullscreen，环按 bottom 锚边），所以由持有者调 SetCenter 每帧写入，
--   缺省值只是 Hud.rml 在 768x1024 档下的读数。
--

---@class Joystick 单指浮动摇杆（固定环心 + 抓取半径内按下即生效）
local Joystick = {}
Joystick.__index = Joystick

--- 构造。config 字段全部可选，缺省值即 Hud.rml 当前的布局。
---@param config? { cx?: number, cy?: number, radius?: number, grab?: number, dead?: number }
---@return self
function Joystick:New(config)
    config = config or {}
    local self = setmetatable({}, Joystick)
    self.cx_ = config.cx or 152.0      -- 环心 X（设计像素）
    self.cy_ = config.cy or 872.0      -- 环心 Y（设计像素，原点左上，随 SetCenter 变）
    self.radius_ = config.radius or 64.0  -- 帽可移动的最大半径（设计像素，留在环内）
    self.grab_ = config.grab or 148.0  -- 多远的按下算"摸到摇杆"：环半径 112 + 36 容差
    self.dead_ = config.dead or 0.12   -- 死区，避免静止时帽微抖
    self.x_ = 0.0
    self.y_ = 0.0
    self.active_ = false
    self.touchId_ = nil                -- 锁定的手指，避免多指时跳变
    return self
end

--- 环心随画布宽高比重定位（底锚元素必须跟着走，否则判定的圆和画出来的圆错位）。
---@param cx number
---@param cy number
function Joystick:SetCenter(cx, cy)
    self.cx_ = cx
    self.cy_ = cy
end

--- 把真实屏幕像素换算到设计像素后，判断是否落在抓取范围内。
---@param dx number 设计像素 X
---@param dy number 设计像素 Y
---@return boolean
function Joystick:InGrabArea(dx, dy)
    local ox = dx - self.cx_
    local oy = dy - self.cy_
    return math.sqrt(ox * ox + oy * oy) <= self.grab_
end

--- 写入一次采样点（设计像素），得到归一化方向。
---@param dx number
---@param dy number
function Joystick:Apply(dx, dy)
    local ox = (dx - self.cx_) / self.radius_
    local oy = (dy - self.cy_) / self.radius_
    local len = math.sqrt(ox * ox + oy * oy)
    if len > 1.0 then
        ox = ox / len
        oy = oy / len
        len = 1.0
    end
    if len < self.dead_ then
        ox = 0.0
        oy = 0.0
        len = 0.0
    end
    self.x_ = ox
    self.y_ = oy
    self.length_ = len
    self.active_ = len > 0.0
end

--- 每帧采样。input 是引擎 Input 子系统，scale 是"真实像素 / 设计像素"的比例。
---@param input Input
---@param scale number
function Joystick:Update(input, scale)
    local inv = 1.0 / (scale > 0.0 and scale or 1.0)

    -- 1) 优先手指：先找回已锁定的那根，找不到再看有没有新手指落在抓取区。
    local numTouches = input:GetNumTouches()
    local tracked = nil
    local fresh = nil
    for i = 0, numTouches - 1 do
        local touch = input:GetTouch(i)
        if touch then
            local dx = touch.position.x * inv
            local dy = touch.position.y * inv
            if touch.touchID == self.touchId_ then
                tracked = { dx = dx, dy = dy, id = touch.touchID }
            elseif not fresh and self:InGrabArea(dx, dy) then
                fresh = { dx = dx, dy = dy, id = touch.touchID }
            end
        end
    end

    local hit = tracked or fresh
    if hit then
        self.touchId_ = hit.id
        self:Apply(hit.dx, hit.dy)
        return
    end

    -- 2) 没有手指：桌面与编辑器 Play 下用鼠标左键拖拽顶上，方便不开模拟器也能验证。
    local MOUSEB_LEFT = MOUSEB and MOUSEB.LEFT or 0
    if input:GetMouseButtonDown(MOUSEB_LEFT) then
        local mouse = input:GetMousePosition()
        local dx = mouse.x * inv
        local dy = mouse.y * inv
        if self:InGrabArea(dx, dy) then
            self.touchId_ = nil
            self:Apply(dx, dy)
            return
        end
    end

    self.touchId_ = nil
    self:Release()
end

--- 归位：方向清零，等下一次按下。
function Joystick:Release()
    self.x_ = 0.0
    self.y_ = 0.0
    self.length_ = 0.0
    self.active_ = false
end

---@return number, number 归一化方向（x 向右为正，y 向下为正）
function Joystick:GetVector()
    return self.x_, self.y_
end

---@return boolean 当前是否有有效输入
function Joystick:IsActive()
    return self.active_
end

return Joystick

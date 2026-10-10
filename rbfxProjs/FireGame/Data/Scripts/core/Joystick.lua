--
-- core/Joystick.lua —— 虚拟摇杆输入节点
--
-- 职责：把一次触摸（桌面/编辑器 Play 下退化为鼠标左键拖拽）换算成归一化方向向量。
-- 它不画任何东西——摇杆的视觉在 Data/UI/GameHud.rml（底环）与 Data/UI/Knob.rml（帽）
-- 里，由 UI 编辑器维护；本模块只负责输入判定，帽的位置由 main.lua 每帧回写。
--
-- 坐标系约定
--   * 所有几何量用"画布像素"表达（与 GameHud.rml 里的 px 同一坐标系）。
--     RmlUi 的 px 不随 SetScale 缩放（SetDensityIndependentPixelRatio 只影响
--     dp 单位与媒体查询，见 RmlUi ComputeProperty.cpp 的 ComputeLength），
--     布局空间恒等于画布尺寸，不存在"设计空间/缩放系数"这一层。
--   * 触摸/鼠标坐标本来就是画布像素（原点在左上，y 向下），拿来即用。
--   * 输出的 y_ 沿用屏幕语义（向上为负），由调用方决定怎么映射到世界轴。
--
-- 与 GameHud.rml 的同步点：环心 = #stick_ring 的 left + width/2 与
--   "画布高度 - bottom - height/2"。高度那项随画布尺寸变化（body 是
--   .fullscreen，环按 bottom 锚边），所以由持有者调 SetCenter 每帧写入，
--   缺省值只是 768x1024 档下的读数。
--

---@class Joystick 浮动摇杆（按下即在该处生成原点，拖拽量=当前指针-原点）
local Joystick = {}
Joystick.__index = Joystick

-- 鼠标充当摇杆指针时的哨兵 ID（真实 touchID 从 0 起）。
local MOUSE_ID = -1

--- 构造。config 字段全部可选，缺省值即 GameHud.rml 当前的布局。
---@param config? { cx?: number, cy?: number, radius?: number, grab?: number, dead?: number }
---@return self
function Joystick:New(config)
    config = config or {}
    local self = setmetatable({}, Joystick)
    self.cx_ = config.cx or 152.0      -- 环心 X（画布像素）
    self.cy_ = config.cy or 872.0      -- 环心 Y（画布像素，原点左上，随 SetCenter 变）
    self.radius_ = config.radius or 64.0  -- 帽可移动的最大半径（画布像素，留在环内）
    self.grab_ = config.grab or 260.0  -- 起手判定半径（画布像素）：浮动原点式，放宽到覆盖整块左下移动区，好按
    self.dead_ = config.dead or 0.12   -- 死区，避免静止时帽微抖
    self.x_ = 0.0
    self.y_ = 0.0
    self.active_ = false
    self.ptrId_ = nil                  -- 当前锁定指针（手指 ID，或鼠标 MOUSE_ID 哨兵）
    self.acx_ = self.cx_               -- 浮动摇杆原点（起手瞬间落在哪就以哪为中心）
    self.acy_ = self.cy_
    return self
end

--- 环心随画布宽高比重定位（底锚元素必须跟着走，否则判定的圆和画出来的圆错位）。
---@param cx number
---@param cy number
function Joystick:SetCenter(cx, cy)
    self.cx_ = cx
    self.cy_ = cy
end

--- 判断采样点（画布像素）是否落在抓取范围内。
---@param dx number 画布像素 X
---@param dy number 画布像素 Y
---@return boolean
function Joystick:InGrabArea(dx, dy)
    local ox = dx - self.cx_
    local oy = dy - self.cy_
    return math.sqrt(ox * ox + oy * oy) <= self.grab_
end

--- 写入一次采样点（画布像素），得到归一化方向。ocx/ocy 是本次拖拽的浮动原点。
---@param ocx number
---@param ocy number
---@param dx number
---@param dy number
function Joystick:Apply(ocx, ocy, dx, dy)
    local ox = (dx - ocx) / self.radius_
    local oy = (dy - ocy) / self.radius_
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

--- 每帧采样。input 是引擎 Input 子系统，坐标全是画布像素，无需换算。
---@param input Input
function Joystick:Update(input)
    -- 1) 手指优先：找回已锁定的那根（整屏任意位置都继续跟）；否则看新手指是否落在移动区。
    local numTouches = input:GetNumTouches()
    local tracked = nil
    local fresh = nil
    for i = 0, numTouches - 1 do
        local touch = input:GetTouch(i)
        if touch then
            local dx = touch.position.x
            local dy = touch.position.y
            if touch.touchID == self.ptrId_ then
                tracked = { dx = dx, dy = dy, id = touch.touchID }
            elseif not fresh and self:InGrabArea(dx, dy) then
                fresh = { dx = dx, dy = dy, id = touch.touchID }
            end
        end
    end

    local sample = tracked or fresh

    -- 2) 无手指时桌面/编辑器用鼠标左键顶上。鼠标坐标是轮询系统全局态，聚焦 GameView 即可用。
    --    已锁定为鼠标时不按区域门（按住拖到哪都跟）；仅未激活时要求按下落在移动区起手。
    if not sample then
        local MOUSEB_LEFT = MOUSEB and MOUSEB.LEFT or 0
        if input:GetMouseButtonDown(MOUSEB_LEFT) then
            local mouse = input:GetMousePosition()
            local dx = mouse.x
            local dy = mouse.y
            if self.ptrId_ == MOUSE_ID then
                sample = { dx = dx, dy = dy, id = MOUSE_ID }
            elseif self.ptrId_ == nil and self:InGrabArea(dx, dy) then
                sample = { dx = dx, dy = dy, id = MOUSE_ID }
            end
        end
    end

    if sample then
        -- 只有新指针（与当前锁定不同）才重设浮动原点；同一指针按住续拖则沿用原点，
        -- 避免一起手就因离固定环心太远而直接打满（之前“勉强能动/很难拖”的根因）。
        if sample.id ~= self.ptrId_ then
            self.acx_ = sample.dx
            self.acy_ = sample.dy
            self.ptrId_ = sample.id
        end
        self:Apply(self.acx_, self.acy_, sample.dx, sample.dy)
        return
    end

    self.ptrId_ = nil
    self.active_ = false
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

--- 当前是否有指针按住摇杆（不要求已产生偏转）。
---@return boolean
function Joystick:IsEngaged()
    return self.ptrId_ ~= nil
end

--- 当前拖拽原点（画布像素）：激活时是按下点，否则是静止环心。供 HUD 摇杆帽跟随指针。
---@return number, number
function Joystick:GetOrigin()
    if self.ptrId_ ~= nil then
        return self.acx_, self.acy_
    end
    return self.cx_, self.cy_
end

---@return boolean 当前是否有有效输入
function Joystick:IsActive()
    return self.active_
end

return Joystick

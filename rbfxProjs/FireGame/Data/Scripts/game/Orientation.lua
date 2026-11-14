-- Scripts/game/Orientation.lua
--
-- 屏幕方向切换（竖屏/横屏）。链路分工与 Voice / Sidebar 同一模式：
--   * 本模块只发请求收事件：sdk:SetDeviceOrientation(landscape)，结果经
--     "MinigameSdkResult"（Kind = MINIGAME_REQUEST_KIND.SET_DEVICE_ORIENTATION）回来。
--   * 宿主 JS 侧（minigame_sdk.js）调 tt/wx.setDeviceOrientation，转屏成功后把新画布尺寸
--     压回引擎（Module.JSCanvasSize）。这一下是必需的：小游戏平台没有浏览器那条 resize
--     通路（Graphics::OnWindowResized 在 Web 平台直接 return），不推就继续按旧尺寸渲染。
--   * 尺寸一变，main.lua 帧循环的重算分支会重算 UI 缩放、摇杆几何与相机
--     （Player:SetAspect ⇒ Config.CameraForAspect）。所以横屏相机参数不需要在这里另写一套：
--     aspect >= CAMERA_LANDSCAPE_ASPECT 时 CameraForAspect 取的就是桌面那组值。
--
-- 方向状态不信宿主的回答，信画布尺寸：转屏完成与否，只有宽高变了才是硬证据。
-- 所以本模块不维护"当前是不是横屏"，只把请求在途/失败写进 HUD 的状态行；
-- 当前方向由 main.lua 按画布 aspect 推（Hud:SetOrientationLandscape）。
--
-- 宿主没有该 API（编辑器/桌面/旧版宿主）时 IsAvailable() 恒 false，设置面板里那一行
-- 整块不渲染——与语音/分享入口同一处理，不给点了没反应的假入口。

local Tracker = require("game.Tracker")

---@class Orientation 屏幕方向切换
local Orientation = {}
Orientation.__index = Orientation

---@param deps { hud: Hud|nil }|nil
---@return Orientation
function Orientation.Build(deps)
    local self = setmetatable({}, Orientation)
    self.hud = deps and deps.hud
    self.sdk = GetSubsystem("MinigameSDK")
    self.requestInFlight = false
    self.pendingLandscape = nil   -- 在途请求的目标方向；nil = 没有请求
    self.deadline = 0.0           -- 在途超时倒计时（秒）；宿主不回调时用它解锁按钮
    self.lastFailure_ = ""        -- 上一次失败原文（真机上只有日志这一条线索）

    SubscribeToEvent("MinigameSdkResult", function(data)
        self:OnSdkResult(data)
    end)
    -- 看门狗：宿主应必回 success/fail，但转屏动画期间被切后台之类的情况下可能吞掉回调。
    -- 不给个上限，设置面板里那颗切换按钮就永久点不动（inFlight 永远为 true）。
    SubscribeToEvent("Update", function(data)
        self:OnUpdate(data.TimeStep)
    end)
    return self
end

--- 宿主是否具备切屏能力。
--- 只判 SDK 通不通：具体机型/基础库不支持时宿主会回失败，那时状态行给提示。
---@return boolean
function Orientation:IsAvailable()
    return self.sdk ~= nil and self.sdk:IsAvailable()
end

--- 请求切到横屏（true）或竖屏（false）。在途时忽略后续点击：宿主转屏本身有动画时间，
--- 连点只会让两次旋转互相打断。
---@param landscape boolean
function Orientation:Request(landscape)
    landscape = landscape and true or false
    if not self:IsAvailable() or self.requestInFlight then
        return
    end
    self.requestInFlight = true
    self.pendingLandscape = landscape
    self.deadline = 5.0
    if self.hud and self.hud.SetOrientationStatus then
        self.hud:SetOrientationStatus(landscape and "正在切到横屏…" or "正在切回竖屏…")
    end
    self.sdk:SetDeviceOrientation(landscape)
end

--- 帧驱动：只跑在途超时（帧率无关，用引擎的 TimeStep）。
---@param dt number
function Orientation:OnUpdate(dt)
    if not self.requestInFlight then
        return
    end
    self.deadline = self.deadline - (dt or 0.0)
    if self.deadline > 0.0 then
        return
    end
    self.requestInFlight = false
    self.pendingLandscape = nil
    print("Orientation: setDeviceOrientation timed out")
    if self.hud and self.hud.SetOrientationStatus then
        self.hud:SetOrientationStatus("切换超时，请重试")
    end
end

--- MinigameSdkResult 事件分发（BEGINFRAME 边界）：SET_DEVICE_ORIENTATION 的回包裁决。
function Orientation:OnSdkResult(data)
    if data.Kind ~= MINIGAME_REQUEST_KIND.SET_DEVICE_ORIENTATION then
        return
    end
    self.requestInFlight = false
    local landscape = self.pendingLandscape
    self.pendingLandscape = nil
    self.deadline = 0.0

    if not data.Success then
        self.lastFailure_ = tostring(data.Payload or "")
        print("Orientation: setDeviceOrientation failed: " .. self.lastFailure_)
        if self.hud and self.hud.SetOrientationStatus then
            self.hud:SetOrientationStatus("本机不支持切换屏幕方向")
        end
        return
    end

    Tracker.Report("orientation", { landscape = (landscape and 1 or 0) })
    -- 成功不回写方向：画布尺寸紧接着会变，main.lua 的重算分支会把真实方向推进状态行。
    -- 在那里写才和相机/UI 真正生效的那一刻同帧，否则按钮先"跳"到横屏、画面下一帧才转。
end

return Orientation

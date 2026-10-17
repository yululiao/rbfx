-- Scripts/game/CamDebug.lua
--
-- 相机参数真机调试面板：一颗"相机"按钮 + 一块滑条面板，实时改 game/Config.lua 里的相机
-- 常量并立刻反映到画面。只在 Config.DEBUG_CAM_PANEL=true 时被 main.lua 创建——上线态下这份
-- UI 根本不存在（不是隐藏）。
--
-- 机制（照 107_HelloRmlUI 与 Hud.lua 的同一套）：
--   * SetHandlers{onInit} 必须先于 SetResource；onInit 是唯一能 Bind* 的时机。
--   * 滑条走 BindProperty（读写双向）：RML 里 <input type="range" data-value="name">，rbfx 的
--     DataControllerValue 监听元素 change 事件把新值写回 model ⇒ 落到这里的 setter（官方样例
--     的 slider_value 就是这个用法）。rmlUi 传回的是字符串，故 tonumber 兜一层。
--   * 每行的数值文本另开一个只读绑定 name.."Text"：数字要带单位/精度，且绝不在内联 style 里
--     绑 {{}}（RCSS 解析器会崩），所以格式化后的字符串单独喂给 <td>。
--   * 拖动过程中不回写 data-value（不 Dirty 滑条自己的绑定名），否则 model→element 方向会把
--     正在拖的把手按旧值重设，手感变成"拖不动"。
--
-- 改参数后为什么还要 Player:ApplyCameraFit()：距离/俯角/前视是 Player 每帧读的缓存成员，
-- FOV/远裁剪面是写死在相机组件上的，两者都不会自己感知 Config 变了。
--
-- "打印"是这条调试链的出口：手机上看不到 Config.lua，调好的值以可粘贴的 Lua 语句打到日志，
-- 回桌面照抄即可。

local Config = require("game.Config")

---@class CamDebug 相机参数调试面板
local CamDebug = {}
CamDebug.__index = CamDebug

--- 可调参数表。RML 的绑定名必须与 key 一一对应（UI/CamDebug.rml 的 data-value 与 {{...Text}}）。
--- src 是"打印"按钮输出时对应的 Config 写法。
local PARAMS = {
    -- 竖屏端：真机（抖音竖屏 t=1）实际生效的就是这三行
    { key = "distPort", src = "Config.CAMERA_PORTRAIT.distance", fmt = "%.2f",
      get = function() return Config.CAMERA_PORTRAIT.distance end,
      set = function(v) Config.CAMERA_PORTRAIT.distance = v end },
    { key = "pitchPort", src = "Config.CAMERA_PORTRAIT.pitch", fmt = "%.1f",
      get = function() return Config.CAMERA_PORTRAIT.pitch end,
      set = function(v) Config.CAMERA_PORTRAIT.pitch = v end },
    { key = "lookPort", src = "Config.CAMERA_PORTRAIT.lookAhead", fmt = "%.2f",
      get = function() return Config.CAMERA_PORTRAIT.lookAhead end,
      set = function(v) Config.CAMERA_PORTRAIT.lookAhead = v end },

    -- 横屏端：t=0 时生效（编辑器 GameView / 桌面）
    { key = "distLand", src = "Config.CAMERA_DISTANCE", fmt = "%.2f",
      get = function() return Config.CAMERA_DISTANCE end,
      set = function(v) Config.CAMERA_DISTANCE = v end },
    { key = "pitchLand", src = "Config.CAMERA_PITCH", fmt = "%.1f",
      get = function() return Config.CAMERA_PITCH end,
      set = function(v) Config.CAMERA_PITCH = v end },
    { key = "lookLand", src = "Config.CAMERA_LOOK_AHEAD", fmt = "%.2f",
      get = function() return Config.CAMERA_LOOK_AHEAD end,
      set = function(v) Config.CAMERA_LOOK_AHEAD = v end },

    -- 视场
    { key = "fovH", src = "Config.CAMERA_H_FOV", fmt = "%.1f",
      get = function() return Config.CAMERA_H_FOV end,
      set = function(v) Config.CAMERA_H_FOV = v end },
    { key = "vfovCap", src = "Config.CAMERA_VFOV_CAP", fmt = "%.1f",
      get = function() return Config.CAMERA_VFOV_CAP end,
      set = function(v) Config.CAMERA_VFOV_CAP = v end },

    -- 取景与手感
    { key = "targetY", src = "Config.CAMERA_TARGET_Y", fmt = "%.2f",
      get = function() return Config.CAMERA_TARGET_Y end,
      set = function(v) Config.CAMERA_TARGET_Y = v end },
    { key = "farClip", src = "Config.CAMERA_FAR_CLIP", fmt = "%.0f",
      get = function() return Config.CAMERA_FAR_CLIP end,
      set = function(v) Config.CAMERA_FAR_CLIP = v end },
    { key = "kick", src = "Config.CAMERA_KICK", fmt = "%.2f",
      get = function() return Config.CAMERA_KICK end,
      set = function(v) Config.CAMERA_KICK = v end },
    { key = "kickDecay", src = "Config.CAMERA_KICK_DECAY", fmt = "%.1f",
      get = function() return Config.CAMERA_KICK_DECAY end,
      set = function(v) Config.CAMERA_KICK_DECAY = v end },

    -- 插值区间端点（动它们等于移动 t 本身，故单列一组，避免和端值行混在一起看不出因果）
    { key = "aspPort", src = "Config.CAMERA_PORTRAIT_ASPECT", fmt = "%.2f",
      get = function() return Config.CAMERA_PORTRAIT_ASPECT end,
      set = function(v) Config.CAMERA_PORTRAIT_ASPECT = v end },
    { key = "aspLand", src = "Config.CAMERA_LANDSCAPE_ASPECT", fmt = "%.2f",
      get = function() return Config.CAMERA_LANDSCAPE_ASPECT end,
      set = function(v) Config.CAMERA_LANDSCAPE_ASPECT = v end },
}

--- 当前画布宽高比。问 RmlUI 要 backbuffer 尺寸（与 main.lua 的 CanvasSize 同一来源：
--- 编辑器 GameView 下 Graphics:GetWidth 是整个编辑器窗口，只有 RmlUi 的 canvas 才对得上画面）。
---@return number|nil
local function CanvasAspect()
    local ui = GetSubsystem("RmlUI")
    if ui and ui.GetCanvasSize then
        local size = ui:GetCanvasSize()
        if size.y > 0 then
            return size.x / size.y
        end
    end
    return nil
end

---@param scene Scene
---@param player Player 被调参的相机宿主（Player:ApplyCameraFit 落回相机）
---@return CamDebug
function CamDebug.Build(scene, player)
    local self = setmetatable({}, CamDebug)
    self.player = player
    self.showTrigger = true
    self.showPanel = false
    self.statusLine = ""
    self.statusLine2 = ""
    self.statusT_ = 0.0

    -- 复位基准：取面板创建那一刻的值（即 Config.lua 里的出厂值）。
    -- 存快照而不是把默认值再抄一遍，避免两处默认值随时间走偏。
    self.defaults = {}
    for _, p in ipairs(PARAMS) do
        self.defaults[p.key] = p.get()
    end

    local comp = scene:CreateComponent("LuaRmlUIComponent")
    self.comp = comp

    comp:SetHandlers({
        onInit = function(w)
            self.w = w
            w:BindReadonlyProperty("showTrigger", function() return self.showTrigger end)
            w:BindReadonlyProperty("showPanel", function() return self.showPanel end)
            w:BindReadonlyProperty("statusLine", function() return self.statusLine end)
            w:BindReadonlyProperty("statusLine2", function() return self.statusLine2 end)

            for _, p in ipairs(PARAMS) do
                local textKey = p.key .. "Text"
                -- 只读那一份是格式化文本；读写那份是滑条本身
                w:BindReadonlyProperty(textKey, function()
                    return string.format(p.fmt, p.get())
                end)
                w:BindProperty(p.key,
                    function() return p.get() end,
                    function(v)
                        local n = tonumber(v)
                        if n then
                            p.set(n)
                            self:Apply(p.key)
                        end
                    end)
            end

            w:BindEvent("open", function() self:SetOpen(true) end)
            w:BindEvent("close", function() self:SetOpen(false) end)
            w:BindEvent("reset", function() self:Reset() end)
            w:BindEvent("dump", function() self:Dump() end)
        end,
        -- 每秒刷一次状态行（含 FPS）：面板只在这期间占屏，每帧 MarkDirty 是白付的开销。
        update = function(_, dt)
            if not self.showPanel then
                return
            end
            self.statusT_ = self.statusT_ + (dt or 0.0)
            if self.statusT_ >= 1.0 then
                self.statusT_ = 0.0
                self:RefreshStatus()
            end
        end,
    })

    comp:SetResource("UI/CamDebug.rml")
    self:RefreshStatus()
    return self
end

---@private
function CamDebug:Dirty(name)
    if self.w then
        self.w:MarkDirty(name)
    end
end

--- 参数变了：重跑相机适配 + 刷面板文本。
--- skipKey 是刚被拖动的那个滑条——它的 data-value 不回写（见文件头），其余滑条要刷，
--- 因为改插值区间端点会连带移动 t，别的行可能确实换了生效值。
---@param skipKey string|nil
function CamDebug:Apply(skipKey)
    if self.player and self.player.ApplyCameraFit then
        self.player:ApplyCameraFit()
    end
    for _, p in ipairs(PARAMS) do
        if p.key ~= skipKey then
            self:Dirty(p.key)
        end
        self:Dirty(p.key .. "Text")
    end
    self:RefreshStatus()
end

---@private
function CamDebug:RefreshStatus()
    local fit = Config.CameraForAspect(CanvasAspect())
    -- 直接报"本端实际生效值"而不是 Config 上的字面值：t 落在中间时两者不同，
    -- 只看字面值会让人按 Config 去推算画面，越算越乱。
    self.statusLine = string.format(
        "生效 距离%.2f 俯角%.1f 前视%.2f 垂直FOV%.1f", fit.distance, fit.pitch, fit.lookAhead, fit.vfov)
    -- FPS 自己拿：面板展开时 GameHud 的性能 Overlay 被让位（两者都想占屏幕上部，同屏只会
    -- 互相盖掉），调构图时不能连帧数都没得看。
    local fps = -1
    local tm = GetSubsystem("Time")
    if tm and tm.GetFramesPerSecond then
        fps = tm:GetFramesPerSecond()
    end
    self.statusLine2 = string.format("aspect%.2f t%.2f | FPS %.0f",
        fit.aspect or 0.0, fit.t or 0.0, fps)
    self:Dirty("statusLine")
    self:Dirty("statusLine2")
end

--- 面板是否占着屏（main.lua 据此吞掉走位输入）。
---@return boolean
function CamDebug:IsModal()
    return self.showPanel
end

---@param b boolean
function CamDebug:SetOpen(b)
    self.showPanel = b and true or false
    self.showTrigger = not self.showPanel
    self:Dirty("showPanel")
    self:Dirty("showTrigger")
    if self.showPanel then
        -- 展开瞬间整体刷一遍：面板收起期间数据模型没被拉取过，确保看到的都是当前值
        self:Apply(nil)
    end
end

--- 回到面板创建那一刻的值。
function CamDebug:Reset()
    for _, p in ipairs(PARAMS) do
        p.set(self.defaults[p.key])
    end
    self:Apply(nil)
end

--- 把当前值以可直接粘回 Config.lua 的形式打到日志——这是手机上调参的出口。
function CamDebug:Dump()
    local lines = { "-- FireGame CamDebug dump" }
    for _, p in ipairs(PARAMS) do
        lines[#lines + 1] = string.format("%s = %s", p.src, string.format(p.fmt, p.get()))
    end
    local fit = Config.CameraForAspect(CanvasAspect())
    lines[#lines + 1] = string.format("-- 生效: distance=%.2f pitch=%.1f lookAhead=%.2f vfov=%.1f (aspect=%.2f t=%.2f)",
        fit.distance, fit.pitch, fit.lookAhead, fit.vfov, fit.aspect or 0.0, fit.t or 0.0)
    print(table.concat(lines, "\n"))
end

return CamDebug

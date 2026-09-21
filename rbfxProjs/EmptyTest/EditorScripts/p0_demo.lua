--
-- 编辑器 Lua 扩展 —— P0 能力演示插件
--
-- 用途：演示并自检 P0 新增的四个 Editor 子表 API：
--   Editor.project.*   脏标记 / 保存
--   Editor.selection.* 读写选区 + 选区变更回调
--   Editor.tick.*      下一帧 / 延时 / 周期调度
--   Editor.ui.*        toast 通知 / 模态确认 / 单行输入
-- （Editor.undo 本期仅占位，未绑定任何方法。）
--
-- 用法：
--   1. 打开 EmptyTest 工程，编辑器自动加载 <工程根>/EditorScripts/*.lua
--   2. 主菜单栏顶层出现 "Demo" 菜单，逐项点击即可（详见每一项下面的说明）
--
-- 约定（与 tools_test.lua 一致）：
--   * 只有 EditorScripts 根目录下的 *.lua 会被自动执行；子目录当模块 require。
--   * 编辑器 ImGui 字体图集不含中文字形，所有会显示到屏幕上或写进 Console 的
--     运行期字符串一律用 ASCII；注释可以中文。
--   * 每个绑定都现取当前活动页 / 子系统，没有工程或没有活动场景时安全返回默认值。
--

-- 安全读取对象名字（不同绑定里字段名可能是 Name/name，取不到就回退）
local function nameOf(o)
    if not o then return "(none)" end
    local ok, r = pcall(function() return o.Name end)
    if ok and r ~= nil then return tostring(r) end
    return "?"
end

-- ---------------------------------------------------------------------------
-- Demo/Selection echo —— 订阅选区变更
--
-- Editor.selection.onChanged(fn) 注册一个回调，活动选区每发生变化就触发一次。
-- 编辑器每帧比一次打包后的选区，所以无论改动来自用户点选、其它 Tab 还是 Lua，
-- 都会走同一条路径。回调里用 getter 现读新选区。这里用一个 flag 防止重复点菜单
-- 时注册出多份回调（真正的注销留待更完整的回调管理，本期 reload 会整体重置）。
-- ---------------------------------------------------------------------------
local watchingSelection = false
Editor.addMenuItem("Demo/Selection echo", function()
    if watchingSelection then
        Editor.ui.notify("already watching selection", 2)
        return
    end
    local ok = Editor.selection.onChanged(function()
        Editor.log("selection changed -> active: " .. nameOf(Editor.selection.activeNode()))
    end)
    watchingSelection = ok
    if ok then
        Editor.ui.notify("watching selection, pick nodes to see the log", 3)
    else
        Editor.logError("Editor.selection.onChanged was refused")
    end
end)

-- ---------------------------------------------------------------------------
-- Demo/Edit and save —— 设选区 + 下一帧日志 + 脏标记保存 + toast
--
-- 一步串起四个子表：
--   Editor.selection.set(target, activate?)  把选区替换成给定对象（单个或数组）
--   Editor.tick.defer(fn)                    下一帧再执行（这里打印变更后的活动节点）
--   Editor.project.markDirty() / save()      标脏并保存活动场景（与 Ctrl+S 同源）
--   Editor.ui.notify(text, seconds?)         右下角 toast
-- 注意：Lua 直接改引擎对象属性（例如 node.x = ..）不进撤销栈，也不由这里演示。
-- ---------------------------------------------------------------------------
Editor.addMenuItem("Demo/Edit and save", function()
    -- 选一个可选项：优先当前活动节点，否则退回场景根的第一个子节点
    local node = Editor.selection.activeNode()
    if not node then
        local scene = Editor.selection.scene()
        if scene then
            local ok, kids = pcall(function() return scene.children end)
            if ok and kids and kids[1] then node = kids[1] end
        end
    end

    if node then
        Editor.selection.set(node, true)
        Editor.log("set selection to: " .. nameOf(node))
    else
        Editor.logWarning("nothing available to select (open a scene first)")
    end

    -- 下一帧再读一次，验证 tick.defer 与 selection getter 的配合
    Editor.tick.defer(function()
        Editor.log("next frame active node: " .. nameOf(Editor.selection.activeNode()))
    end)

    -- 标脏 + 保存
    if Editor.project.hasProject() then
        Editor.project.markDirty()
        local saved = Editor.project.save()
        Editor.log("dirty=" .. tostring(Editor.project.isDirty()) .. "  saved=" .. tostring(saved))
        Editor.ui.notify(saved and "scene saved" or "save failed (no scene view?)", 3)
    else
        Editor.ui.notify("no project open", 3)
    end
end)

-- ---------------------------------------------------------------------------
-- Demo/Confirm —— 模态确认框，按钮分别走 onYes / onNo
-- ---------------------------------------------------------------------------
Editor.addMenuItem("Demo/Confirm", function()
    Editor.ui.confirm(
        "P0 Demo",
        "Mark the project dirty?",
        function()
            Editor.project.markDirty()
            Editor.ui.notify("confirmed -> dirty", 3)
        end,
        function()
            Editor.ui.notify("cancelled", 3)
        end)
end)

-- ---------------------------------------------------------------------------
-- Demo/Rename (input) —— 单行输入模态，回调拿到文本
--
-- Editor.ui.input(title, label, defaultText, onDone)：确认后 onDone 收到 EventData，
-- 文本在 data.Text 字段（取消 / ESC 不回调）。这里演示改名，并额外验证
-- Editor.tick.after / every / cancel 三个调度接口。
-- ---------------------------------------------------------------------------
Editor.addMenuItem("Demo/Rename (input)", function()
    local node = Editor.selection.activeNode()
    if not node then
        Editor.ui.notify("select a node first", 3)
        return
    end

    local oldName = nameOf(node)
    Editor.ui.input("P0 Demo", "New name", oldName, function(data)
        local text = data and data.Text
        if text == nil or text == "" then
            Editor.ui.notify("empty name, skipped", 3)
            return
        end
        local ok = pcall(function() node:SetName(text) end)
        Editor.log(ok and ("renamed -> " .. tostring(text) .. "  (direct write, not undoable)")
                        or ("rename failed for " .. tostring(text)))
        Editor.project.markDirty()
        Editor.ui.notify("renamed (undo unavailable in P0)", 3)
    end)

    -- 顺便演示：2 秒后打一条延时日志；一个每秒心跳、5 秒后用 cancel 停掉的计时器
    Editor.tick.after(2.0, function() Editor.log("tick.after fired (2s)") end)
    local count = 0
    local handle = Editor.tick.every(1.0, function()
        count = count + 1
        Editor.log("tick.every heartbeat #" .. count)
    end)
    Editor.tick.after(5.0, function()
        Editor.tick.cancel(handle)
        Editor.log("tick.every cancelled after ~5 heartbeats")
    end)
end)

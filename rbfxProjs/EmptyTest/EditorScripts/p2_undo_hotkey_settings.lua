--
-- 编辑器 Lua 扩展 —— P2 Undo / Toolbar+Hotkey / Settings-Page 演示插件
--
-- 用途：演示并自检本里程碑新增的三个 Editor 子表：
--   Editor.undo.*      把插件改动汇入编辑器统一撤销栈（引擎属性/结构 + 通用 Lua 闭包 + batch 分组）
--   Editor.toolbar.*   往工程工具栏（Save 按钮那一行）加按钮
--   Editor.hotkey.*    用编辑器自带的 HotkeyManager 绑快捷键（与内建快捷键共存）
--   Editor.settings.registerPage(title, drawFn)  在 Settings 窗口注册一个插件自绘页
--
-- 用法：
--   1. 打开 EmptyTest 工程，编辑器自动加载 <工程根>/EditorScripts/*.lua
--   2. 顶层出现 "P2" 菜单；工具栏出现一颗 "bolt" 图标按钮；Ctrl+Alt+K 可触发；
--      Settings 窗口左侧树 Editor > Lua > "P2 Demo" 出现自绘页。
--
-- 约定（与 p0_demo.lua / p1_settings_demo.lua 一致）：
--   * 只有 EditorScripts 根目录下的 *.lua 会被自动执行；子目录当模块 require。
--   * 运行期字符串一律 ASCII（编辑器 ImGui 字体图集不含中文字形）；注释可中文。
--   * 无工程 / 无活动场景时接口安全降级（setter 返回 false、结构操作返回 nil）。
--   * reload（Tools/Reload Plugins 或重开工程）会清掉本插件注册的工具栏按钮、快捷键、
--     选区/定时回调等瞬态登记项；settings 页因由 SettingsManager 长期持有，只会把绘制
--     回调重新指向新脚本里的函数（不会叠加重复页）。
--

local NS = "p2demo."

-- 一块纯 Lua 侧状态：用来演示 Editor.undo.perform 撤销“引擎对象之外的插件自身数据”。
local luaCounter = 0

-- 自增一次并把这次改动登记为可撤销步骤（do/undo 两个闭包）。菜单和工具栏按钮共用它。
local function bumpCounter()
    local old = luaCounter
    local new = old + 1
    -- perform 会：注册两个回调 -> 立即执行 do 闭包(new) -> 压入一条 undo 记录。
    local ok = Editor.undo.perform("P2 bump counter",
        function() luaCounter = new end,
        function() luaCounter = old end)
    Editor.log("perform ok=" .. tostring(ok) .. "  luaCounter=" .. tostring(luaCounter))
    return ok
end

-- ---------------------------------------------------------------------------
-- P2/Undo —— 引擎属性可撤销修改 + batch 分组（Ctrl+Z 一次回退整组）
--
--   Editor.undo.setNodeAttribute(node, "Name", value)      改一个节点属性并进栈
--   Editor.undo.setComponentAttribute(comp, attr, value)   改一个组件属性并进栈
--   Editor.undo.batch(label, fn)                           fn 里的所有改动合成“一步”撤销
-- 需要活动场景（SceneViewTab 有打开的页）；否则 setter 返回 false。
-- ---------------------------------------------------------------------------
Editor.addMenuItem("P2/Undo: rename active node (undoable)", function()
    local node = Editor.selection.activeNode()
    if not node then
        Editor.ui.notify("select a node first", 3)
        return
    end
    local stamp = tostring(os.time())
    local ok = Editor.undo.setNodeAttribute(node, "Name", "P2Node_" .. stamp)
    Editor.log("setNodeAttribute ok=" .. tostring(ok))
    Editor.ui.notify(ok and "renamed (Ctrl+Z reverts)" or "no active scene?", 3)
end)

Editor.addMenuItem("P2/Undo: batch rename all selected (one undo)", function()
    local nodes = Editor.selection.nodes()
    local count = 0
    for _ in ipairs(nodes) do count = count + 1 end
    if count == 0 then
        Editor.ui.notify("select some nodes first", 3)
        return
    end
    -- batch 内每次 setNodeAttribute 不再各自入栈，而是并进一个复合动作；一次 Ctrl+Z 全回退。
    local base = os.time()
    local ok = Editor.undo.batch("P2 batch rename", function()
        for i, n in ipairs(nodes) do
            Editor.undo.setNodeAttribute(n, "Name", "P2Batch_" .. tostring(base) .. "_" .. tostring(i))
        end
    end)
    Editor.log("batch ok=" .. tostring(ok) .. " renamed=" .. tostring(count))
    Editor.ui.notify("renamed " .. tostring(count) .. " nodes in one undo step", 3)
end)

Editor.addMenuItem("P2/Undo: create node + add component (undoable)", function()
    local node = Editor.undo.createNode(nil, "P2Created")
    if not node then
        Editor.ui.notify("no active scene to create in", 3)
        return
    end
    local comp = Editor.undo.addComponent(node, "StaticModel")
    Editor.log("created node=" .. tostring(node ~= nil) .. "  component=" .. tostring(comp ~= nil))
    Editor.ui.notify("created node (+component); Ctrl+Z reverts", 3)
end)

-- ---------------------------------------------------------------------------
-- P2/Perform —— 撤销纯 Lua 状态（不依赖场景），并演示栈查询 / 手动撤销
-- ---------------------------------------------------------------------------
Editor.addMenuItem("P2/Perform: bump Lua counter (undoable)", function()
    bumpCounter()
end)

Editor.addMenuItem("P2/Query undo stack + Undo once", function()
    Editor.log("luaCounter=" .. tostring(luaCounter)
        .. "  canUndo=" .. tostring(Editor.undo.canUndo())
        .. "  canRedo=" .. tostring(Editor.undo.canRedo()))
    local done = Editor.undo.undo()
    Editor.log("undo() -> " .. tostring(done) .. "  luaCounter=" .. tostring(luaCounter))
    Editor.ui.notify("popped one undo step (see console)", 3)
end)

-- reload 安全演示：改一下 Lua 闭包状态后 reload，旧记录不会卡栈也绝不崩。
Editor.addMenuItem("P2/NOTE: reload-safety check", function()
    Editor.log("Before reload: bump a few times, then Tools/Reload Plugins.")
    Editor.log("After reload: the old perform() records still sit in the shared undo stack, but")
    Editor.log("their callbacks were dropped, so undoing them is a safe no-op (never wedges the stack).")
    Editor.ui.notify("see console for the reload-safety note", 5)
end)

-- ---------------------------------------------------------------------------
-- P2/Toolbar —— 往工程工具栏加一颗按钮（icon 用 Editor.toolbar.iconNames() 里的名字）
--
-- 重复注册（每次加载都跑一遍）不会叠加：编辑器每帧只画当前登记的那批按钮，reload 会清空。
-- ---------------------------------------------------------------------------
Editor.toolbar.add("P2 Bump", function()
    bumpCounter()
end, { icon = "bolt", tooltip = "Bump the Lua counter (undoable)" })

-- 第二个按钮：把当前栈状态打印出来，方便观察 undo/redo。
Editor.toolbar.add("P2 State", function()
    Editor.log("toolbar click: luaCounter=" .. tostring(luaCounter))
    Editor.ui.notify("luaCounter = " .. tostring(luaCounter), 3)
end, { icon = "info" })

-- 列一下可用图标名（首次加载打一条日志即可，方便你换 icon 值）。
Editor.tick.defer(function()
    local names = Editor.toolbar.iconNames()
    local line = "toolbar icon names: "
    for i = 1, math.min(#names, 12) do
        line = line .. names[i] .. (i < math.min(#names, 12) and ", " or "")
    end
    Editor.log(line .. " ...")
end)

-- ---------------------------------------------------------------------------
-- P2/Hotkey —— Ctrl+Alt+K 触发自增；回显规范化组合串
--
-- 所有 Lua 快捷键共用一个“本代”owner 对象；reload 释放它 -> 旧绑定被 HotkeyManager 自动清理，
-- 再绑新脚本里的回调不会和旧的重复触发。
-- ---------------------------------------------------------------------------
local bound = Editor.hotkey.bind("ctrl+alt+k", function()
    Editor.ui.notify("Ctrl+Alt+K -> bump", 2)
    bumpCounter()
end)
Editor.log("hotkey bind ctrl+alt+k ok=" .. tostring(bound)
    .. "  label=" .. tostring(Editor.hotkey.comboLabel("ctrl+alt+k")))

-- ---------------------------------------------------------------------------
-- P2/Settings page —— 注册一个由本插件自绘的 Settings 页（值仍走 Editor.settings 持久化）
--
-- 位置：Settings 窗口左侧树 Editor > Lua > "P2 Demo"。drawFn 每帧该页可见时被调用，处在
-- settings 标签页的 ImGui 上下文里，可直接用 imgui.*。registerPage 对同一 title 只会加一次页，
-- reload 只重指向回调，因此勾选/滑块的持久值不会因重复注册而丢失。
-- ---------------------------------------------------------------------------
Editor.settings.registerPage("P2 Demo", function()
    imgui.Text("This page is drawn by a Lua plugin callback (Editor.settings.registerPage).")
    imgui.TextWrapped("Toggle the widgets below, then Tools/Reload Plugins or reopen the project: the values persist because they are stored with Editor.settings.")
    imgui.Separator()

    local enabled = Editor.settings.get(NS .. "enabled", false)
    local changed, v = imgui.Checkbox("Enable thing", enabled)
    if changed then Editor.settings.set(NS .. "enabled", v) end

    local slider = Editor.settings.get(NS .. "slider", 25)
    changed, v = imgui.SliderInt("Some value", slider, 0, 100)
    if changed then Editor.settings.set(NS .. "slider", v) end

    local gain = Editor.settings.get(NS .. "gain", 1.0)
    changed, v = imgui.SliderFloat("Gain", gain, 0.0, 4.0)
    if changed then Editor.settings.set(NS .. "gain", v) end

    imgui.BulletText("enabled = " .. tostring(Editor.settings.get(NS .. "enabled", false)))
    imgui.BulletText("slider  = " .. tostring(Editor.settings.get(NS .. "slider", 25)))
    imgui.BulletText("gain    = " .. tostring(Editor.settings.get(NS .. "gain", 1.0)))
end)

Editor.ui.notify("P2 demo loaded: see the P2 menu, toolbar, and Ctrl+Alt+K", 4)

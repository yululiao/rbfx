--
-- 编辑器 Lua 扩展 —— 控件测试插件
--
-- 用法：
--   1. 打开 EmptyTest 工程后，编辑器会加载 <工程根>/EditorScripts/*.lua
--   2. 主菜单栏顶层的 "Tools" 菜单（与内置 Profiler 并列）里点 "Test Widgets"
--      -> 弹出一个独立浮动窗口，逐帧绘制下面这些常用控件
--      （控件覆盖：文本类/按钮/输入数值/下拉/颜色/树列表/选中上下文/Image 系列）
--   3. 关掉窗口用标题栏右侧的 X（由编辑器托管），下次点菜单会重新打开
--
-- 说明：这里用 Editor.addWindow 注册“常驻窗口”，编辑器每帧在顶层统一 Begin/End 托管它，
-- 因此窗口不再依赖任何 Dock Tab 是否可见；菜单回调只需 Editor.showWindow 把它显示出来。
-- 注意：addWindow 的绘制回调里只画控件本身，不要再自己调用 imgui.Begin/imgui.End。
--
-- 约定：只有 EditorScripts 根目录下的 *.lua 会被自动执行（子目录不会）；子目录当模块用，
-- 需要时自己 require，例如 require("subdir.subdir_files_no_auto_run")。EditorScripts 已在
-- package.path 里，所以点号与斜杠两种写法都能解析。
--
-- 重要：编辑器 ImGui 的字体图集不含中文字形，所有会显示到屏幕上的字符串（窗口标题、
-- 控件标签、菜单项，以及 Editor.log 输出到 Console Tab 的内容）都必须用 ASCII。
-- 因此本文件里的注释可以中文，但运行期字符串一律英文；多语言后续由翻译系统统一实现。
--

-- 窗口标题既是显示文本也是 showWindow/hideWindow 的键，必须保持一致

--require("LuaPanda").start("127.0.0.1", 8818)
local WINDOW_TITLE = "Widget Test Window"

-- 跨帧保持的控件状态
local state = {
    check    = true,
    radio    = 1,
    text     = "Edit me",
    pathFile = "",         -- InputPath 文件字段
    pathDir  = "",         -- InputPath 目录字段
    num      = 1.5,
    i        = 3,
    slider   = 20.0,
    combo    = 0,          -- Combo 返回的是 0 基索引
    listSel  = 1,
    r = 0.80, g = 0.20, b = 0.30, a = 1.0,
}

-- 安全读取对象名字（不同绑定里字段名可能是 Name/name，取不到就回退）
local function nameOf(o)
    if not o then return "(none)" end
    local ok, r = pcall(function() return o.Name end)
    if ok and r ~= nil then return tostring(r) end
    return "?"
end

-- 纹理预览用的跨帧状态。工程资源根目录的约定不在脚本里硬编码，
-- 所以加载时先把候选路径逐个试一遍，第一个能取到的就用；不行再在窗口里手填。
local texPath = "Textures/test.png"
local texture = nil
local lastButtonHit = "(none)"

do
    local candidates = {
        "Textures/test.png",
        "Data/Textures/test.png",
        "Textures/StoneDiffuse.dds",
        "Data/Textures/StoneDiffuse.dds",
        "Models/Kachujin/Textures/Kachujin_diffuse.png",
    }
    for i = 1, #candidates do
        local t = GetResource("Texture2D", candidates[i])
        if t then
            texture = t
            texPath = candidates[i]
            break
        end
    end
end

-- 绘制各类已导出的常用控件（无需 Begin/End，编辑器已包好）
local function render_controls()
    imgui.Text("Hello, editor Lua")
    imgui.TextDisabled("Disabled text (TextDisabled)")
    imgui.TextColored(0.2, 0.9, 0.4, 1.0, "Colored text (TextColored)")
    imgui.TextWrapped("Wrapping long text (TextWrapped): this line is intentionally made long enough to check how it wraps inside the current window width.")
    imgui.BulletText("A bullet line (BulletText)")

    imgui.Separator()
    imgui.Text("== Buttons ==")

    if imgui.Button("Click me (Button)") then
        Editor.log("Button clicked")
    end
    imgui.SameLine()
    if imgui.SmallButton("SmallButton") then
        Editor.log("SmallButton clicked")
    end

    imgui.Separator()
    imgui.Text("== Input / Numeric ==")

    local changed, v

    -- Checkbox -> changed, newValue
    changed, v = imgui.Checkbox("Checkbox", state.check)
    if changed then state.check = v end

    -- RadioButton（bool 版）
    if imgui.RadioButton("Option A", state.radio == 1) then state.radio = 1 end
    imgui.SameLine()
    if imgui.RadioButton("Option B", state.radio == 2) then state.radio = 2 end

    -- InputText -> changed, newText
    changed, v = imgui.InputText("InputText", state.text)
    if changed then state.text = v end

    -- InputPath -> changed, newPath：输入框 + 原生选择按钮 + 系统文件管理器定位按钮三合一。
    -- kind: "dir" 选文件夹（缺省选文件）；filter: 扩展名过滤（如 "png,jpg"，仅文件模式）。
    -- 选择结果为正斜杠绝对路径；定位按钮在值指向磁盘上真实存在的东西时才可点。
    -- 原生对话框是编辑器能力：脱离编辑器运行时选择按钮不绘制，PickPath 返回 nil。
    changed, v = imgui.InputPath("InputPath (file)", state.pathFile, "file", "png")
    if changed then state.pathFile = v end
    changed, v = imgui.InputPath("InputPath (dir)", state.pathDir, "dir")
    if changed then state.pathDir = v end

    -- PickPath 单独弹原生选择框（不经输入框），返回路径或 nil（取消）。
    -- 参数与 InputPath 的 kind/filter 一致，另可传第三个参数指定初始目录。
    if imgui.Button("Pick a folder (PickPath)") then
        local picked = imgui.PickPath("dir")
        if picked then
            state.pathDir = picked
            Editor.log("Picked folder: " .. picked)
        end
    end

    -- InputFloat / InputInt
    changed, v = imgui.InputFloat("InputFloat", state.num)
    if changed then state.num = v end
    changed, v = imgui.InputInt("InputInt", state.i)
    if changed then state.i = v end

    -- DragFloat / DragInt
    changed, v = imgui.DragFloat("DragFloat", state.num, 0.1, 0, 100)
    if changed then state.num = v end
    changed, v = imgui.DragInt("DragInt", state.i, 1, 0, 50)
    if changed then state.i = v end

    -- SliderFloat / SliderInt
    changed, v = imgui.SliderFloat("SliderFloat", state.slider, 0, 100)
    if changed then state.slider = v end
    changed, v = imgui.SliderInt("SliderInt", state.i, 0, 50)
    if changed then state.i = v end

    -- Combo：传入数组表，返回 changed 与 0 基索引
    changed, v = imgui.Combo("Combo", state.combo, { "Apple", "Banana", "Orange" })
    if changed then state.combo = v end

    -- ColorEdit4 -> changed, r, g, b, a
    changed, state.r, state.g, state.b, state.a =
        imgui.ColorEdit4("ColorEdit4", state.r, state.g, state.b, state.a)
    imgui.ColorButton("swatch", state.r, state.g, state.b, state.a)
    imgui.SameLine()
    imgui.Text("ColorButton")

    -- ProgressBar
    imgui.Text("ProgressBar:")
    imgui.ProgressBar(state.slider / 100.0)

    imgui.Separator()
    imgui.Text("== Tree / Collapsing / List ==")

    if imgui.TreeNode("A TreeNode") then
        imgui.Text("Content inside the tree node")
        imgui.TreePop()
    end

    if imgui.CollapsingHeader("A CollapsingHeader") then
        imgui.Text("Content inside the collapsing header")
    end

    if imgui.BeginListBox("ListBox (BeginListBox + Selectable)") then
        local items = { "Item one", "Item two", "Item three" }
        for i = 1, #items do
            local clicked
            clicked = imgui.Selectable(items[i], state.listSel == i)
            if clicked then state.listSel = i end
        end
        imgui.EndListBox()
    end

    imgui.Separator()
    imgui.Text("== Editor context (selection / scene) ==")

    local scene = Editor.getActiveScene()
    imgui.Text("Active scene: " .. nameOf(scene))
    local active = Editor.getActiveNode()
    imgui.Text("Active node: " .. nameOf(active))

    local sel = Editor.getSelection()
    imgui.Text("Selection count: nodes=" .. tostring(#sel.nodes) .. "  components=" .. tostring(#sel.components))
    for i = 1, #sel.nodes do
        imgui.BulletText("Node[" .. i .. "] " .. nameOf(sel.nodes[i]))
    end
    for i = 1, #sel.components do
        imgui.BulletText("Component[" .. i .. "] " .. nameOf(sel.components[i]))
    end

    imgui.Separator()
    imgui.Text("== Image / ImageItem / ImageButton ==")

    local changed2, pathValue = imgui.InputText("Texture path", texPath)
    if changed2 then
        texPath = pathValue
        texture = nil -- 改了路径就失效，重新 Load
    end
    imgui.SameLine()
    if imgui.Button("Load") then
        texture = GetResource("Texture2D", texPath)
        if texture then
            Editor.log("Loaded " .. texPath .. " " .. tostring(texture:GetWidth()) .. "x" .. tostring(texture:GetHeight()))
        else
            Editor.log("Failed to load " .. tostring(texPath))
        end
    end

    if texture then
        imgui.Text(string.format("texture size: %dx%d", texture:GetWidth(), texture:GetHeight()))

        -- 1) 纯绘制：不注册 Item，也不能 hover。省略 w/h 则用纹理原始尺寸。
        imgui.Image(texture, 128, 128)
        imgui.SameLine()

        -- 2) ImageItem 会 ItemAdd，所以能配合 IsItemHovered / SetTooltip
        imgui.ImageItem(texture, 128, 128)
        if imgui.IsItemHovered() then
            imgui.SetTooltip("hovered ImageItem")
        end
        imgui.SameLine()

        -- 3) ImageButton 可点击。它的 id 是从纹理指针推出来的，所以同一个纹理
        --    做多个按钮时必须用 PushID/PopID 区分，否则两个按钮会相互干扰。
        imgui.PushID("imageBtnA")
        if imgui.ImageButton(texture, 64, 64) then
            lastButtonHit = "A (full uv)"
        end
        imgui.PopID()
        imgui.SameLine()

        imgui.PushID("imageBtnB")
        if imgui.ImageButton(texture, 64, 64, 0, 0, 0.5, 0.5) then
            lastButtonHit = "B (top-left quarter uv)"
        end
        imgui.PopID()

        imgui.Text("last button hit: " .. lastButtonHit)
    else
        imgui.TextDisabled("no texture loaded - fix the path above and press Load")
    end

    imgui.Separator()
    if imgui.Button("Hide this window (Hide)") then
        Editor.hideWindow(WINDOW_TITLE)
    end
end

-- 注册常驻窗口（初始隐藏）。flags 传给编辑器的 Begin，这里用自适应尺寸。
Editor.addWindow(WINDOW_TITLE, render_controls, imgui.WindowFlags.AlwaysAutoResize)

-- label 里的第一段是【主菜单栏顶层菜单名】：编辑器已有 Tools（URHO3D_PROFILING 开着，
-- 里面有 Profiler）就把条目并进去；没有该名字则自动新建顶层菜单。后续段则是再嵌套的子菜单。
Editor.addMenuItem("Tools/Test Widgets", function()
    Editor.showWindow(WINDOW_TITLE)
    Editor.log("Tools > Test Widgets clicked -> showing widget test window")
end)

-- Tools 菜单里的第二项：同样归到顶层 "Tools" 下，只是不弹窗口、打个日志
Editor.addMenuItem("Tools/About This Plugin", function()
    Editor.log("This is the editor Lua ImGui widget test plugin (tools_test.lua)")
end)

Editor.addMenuItem("Tools/Test EditorScripts subdir", function()
    require("subdir.subdir_files_no_auto_run").Run()
end)

-- 顶层 "WidgetDemo" 菜单不存在 -> 自动创建；第二段 "Nested" 是它下面的子菜单（验证多层嵌套）
Editor.addMenuItem("WidgetDemo/Nested/Show Window", function()
    Editor.showWindow(WINDOW_TITLE)
    Editor.log("WidgetDemo > Nested > Show Window clicked")
end)

-- 不带 '/' 的条目仍然放在 Project 下拉菜单里（作为对照）
Editor.addMenuItem("Show Widget Test Window", function()
    Editor.showWindow(WINDOW_TITLE)
end)

-- ---------------------------------------------------------------------------
-- 构建管线（出包）
--
-- profile 名来自工程根目录的 Build.json，由编辑器维护，脚本里不硬编码：换一个工程、
-- 改一个 profile，下面的代码不用动。
--
-- 出包是异步的：Editor.build 只回答「有没有接受这次请求」，结果稍后通过
-- buildFinished 事件（所有观察者）或那次调用自带的回调（只此一次）送达。
-- 想连续出多个平台就在回调里链下一次 build，这样任一环节失败都不会串错顺序。
-- ---------------------------------------------------------------------------

local profiles = Editor.buildPlatforms()
Editor.log("Build profiles: " .. table.concat(profiles, ", "))

-- 事件里的字段就是构建结束时那四个值，跟 BuildTab 显示的是同一份数据
Editor.subscribe("buildFinished", function(data)
    Editor.log(string.format("buildFinished event: profile=%s success=%s output=%s message=%s",
        tostring(data.Profile), tostring(data.Success), tostring(data.OutputDir), tostring(data.Message)))
end)

Editor.addMenuItem("Tools/Build First Profile (Lua)", function()
    local names = Editor.buildPlatforms()
    if #names == 0 then
        Editor.logWarning("No build profile in Build.json - open Project > Build Settings first")
        return
    end

    local started = Editor.build(names[1], function(data)
        local status = Editor.buildStatus()
        Editor.log(string.format("build callback: %s success=%s stage=%s problems=%d",
            tostring(data.Profile), tostring(data.Success), status.stage, #status.errors))
        for i = 1, #status.errors do
            Editor.logError(status.errors[i])
        end
    end)

    if not started then
        Editor.logError("Editor.build was refused; the reason is in the log above")
    else
        local status = Editor.buildStatus()
        Editor.log("Building " .. status.profile .. " into " .. status.outputDir)
    end
end)

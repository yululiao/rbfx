--
-- 编辑器 Lua 扩展 —— P1 资产系统 API 演示插件
--
-- 用途：演示并自检 P1 新增的 Editor.assets 子表：
--   Editor.assets.list(opts)      枚举工程资产（可选按 dir / type / extension 过滤）
--   Editor.assets.info(name)      单个资源的元数据（含类型）
--   Editor.assets.exists(name)    廉价存在性检查
--   Editor.assets.open/reveal     在编辑器里打开 / 仅定位资源
--   Editor.assets.reimport(path)  重导入一个资源或整目录（走 AssetManager 管线）
--   Editor.assets.status()        导入进度快照
--   Editor.assets.onProcessed     一轮导入完成（processing -> idle）时回调
--
-- 用法：
--   1. 打开 EmptyTest 工程，编辑器自动加载 <工程根>/EditorScripts/*.lua
--   2. 主菜单栏顶层出现 "Assets" 菜单，逐项点击即可
--
-- 约定（与 p0_demo.lua / tools_test.lua 一致）：
--   * 资源路径一律是相对 Data 的资源名，例如 "Textures/foo.png"。
--   * 运行期字符串全 ASCII；注释可中文。
--   * 无工程时所有接口安全降级（list 空表 / info nil / exists false / 动作返回 false）。
--   * 原始文件读写不在本接口内——插件已可用引擎的 fileSystem / file / io。
--

-- 把一张数组表里的资源名打印出来，最多 max 条，返回总条数
local function logNames(list, max)
    local count = 0
    for _ in ipairs(list) do count = count + 1 end
    local shown = 0
    for _, entry in ipairs(list) do
        if shown >= (max or 5) then break end
        Editor.log("  - " .. tostring(entry.name) .. "  [" .. tostring(entry.extension) .. "]")
        shown = shown + 1
    end
    return count
end

-- ---------------------------------------------------------------------------
-- Assets/Scan textures —— 两种枚举：廉价全量计数 + 按类型过滤
--
-- list() 不传 opts 只做目录扫描，不解析类型（快）；
-- list({ type = "Texture2D" }) 会对每个候选调 GetResourceDescriptor 判定类型（较慢，
-- 建议配 dir / extension 缩小范围），命中的 entry 额外带 type / types。
-- ---------------------------------------------------------------------------
Editor.addMenuItem("Assets/Scan textures", function()
    if not Editor.project.hasProject() then
        Editor.ui.notify("no project open", 3)
        return
    end

    local all = Editor.assets.list()
    Editor.log("total files under Data: " .. tostring(logNames(all, 3)) .. " (showing 3)")

    local textures = Editor.assets.list({ type = "Texture2D" })
    local n = logNames(textures, 5)
    Editor.log("Texture2D count: " .. tostring(n))
    Editor.ui.notify("found " .. tostring(n) .. " textures (see console)", 3)
end)

-- ---------------------------------------------------------------------------
-- Assets/Inspect (input) —— 输入资源名，查 exists + info
--
-- info 返回 entry：{ name, path, extension, isDirectory, type, types = {...} }；
-- 文件不存在返回 nil。exists 只做文件系统探测，不解析类型。
-- ---------------------------------------------------------------------------
Editor.addMenuItem("Assets/Inspect (input)", function()
    Editor.ui.input("P1 Assets", "resource name", "Textures/", function(data)
        local name = data and data.Text
        if name == nil or name == "" then
            Editor.ui.notify("empty name, skipped", 3)
            return
        end

        Editor.log("exists(" .. tostring(name) .. ") = " .. tostring(Editor.assets.exists(name)))

        local info = Editor.assets.info(name)
        if not info then
            Editor.log("info: (not found)")
            return
        end
        local types = {}
        if info.types then
            for _, t in ipairs(info.types) do types[#types + 1] = tostring(t) end
        end
        Editor.log("info: type=" .. tostring(info.type)
            .. "  types={" .. table.concat(types, ",") .. "}"
            .. "  dir=" .. tostring(info.isDirectory))
        Editor.log("       abs=" .. tostring(info.path))
    end)
end)

-- ---------------------------------------------------------------------------
-- Assets/Open first texture —— 打开 / 仅定位
--
-- open 会像双击一样在对应 Tab 打开资源；reveal 只在资源浏览器里高亮，不抢 Inspector。
-- ---------------------------------------------------------------------------
Editor.addMenuItem("Assets/Open first texture", function()
    local textures = Editor.assets.list({ type = "Texture2D", extension = "png" })
    local first = textures[1]
    if not first then
        Editor.ui.notify("no .png texture to open", 3)
        return
    end
    local ok = Editor.assets.open(first.name)
    Editor.ui.notify((ok and "opening " or "open refused: ") .. tostring(first.name), 3)

    -- 第二张只定位，不抢走当前 Inspector
    local second = textures[2]
    if second then
        Editor.assets.reveal(second.name)
        Editor.log("revealed: " .. tostring(second.name))
    end
end)

-- ---------------------------------------------------------------------------
-- Assets/Reimport Textures/ —— 重导入整目录 + 完成回调 + 进度快照
--
-- reimport 只是把匹配前缀的资产标脏，真正的重处理在 AssetManager 下一次 update 排队进行。
-- 先注册 onProcessed（处理从进行中 -> 空闲时触发），再标脏；期间用 tick.every 采样 status()
-- 打印进度。onProcessed 是单槽注册：再次调用会替换。这里用 reimportWatch 标志把回调约束成
-- "只对本次排队负责"，回调内不去注销自己（正在执行的回调不应当场被 Drop）。
-- ---------------------------------------------------------------------------
local reimportWatch = false
Editor.addMenuItem("Assets/Reimport Textures/", function()
    if not Editor.assets.exists("Textures/") then
        Editor.ui.notify("no Textures/ dir in this project", 3)
        return
    end
    if reimportWatch then
        Editor.ui.notify("already waiting for an import to finish", 3)
        return
    end

    reimportWatch = true
    Editor.assets.onProcessed(function()
        if not reimportWatch then return end
        reimportWatch = false
        local s = Editor.assets.status()
        Editor.ui.notify("reimport done (processing=" .. tostring(s.processing) .. ")", 4)
        Editor.log("asset import run finished")
    end)

    local ok = Editor.assets.reimport("Textures/")
    Editor.ui.notify(ok and "reimport queued, waiting..." or "reimport refused (no project?)", 3)

    -- 采样进度直到空闲
    local ticks = 0
    local handle
    handle = Editor.tick.every(0.5, function()
        ticks = ticks + 1
        local s = Editor.assets.status()
        Editor.log(string.format("status: processing=%s  %d/%d",
            tostring(s.processing), tonumber(s.processed) or 0, tonumber(s.total) or 0))
        if not s.processing or ticks > 40 then
            Editor.tick.cancel(handle)
        end
    end)
end)

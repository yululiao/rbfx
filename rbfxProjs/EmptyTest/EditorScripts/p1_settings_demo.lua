--
-- 编辑器 Lua 扩展 —— P1 B6 设置读写 API 演示插件
--
-- 用途：演示并自检 P1 B6 新增的 Editor.settings 子表（插件自有的持久化 key->Variant 存储）：
--   Editor.settings.set(key, value)   写入（bool / int / float / string 最稳）
--   Editor.settings.get(key, def?)    读取，缺省返回 def（无 def 则 nil）
--   Editor.settings.has(key)          是否存在
--   Editor.settings.erase(key)        删除
--   Editor.settings.keys(prefix?)     列出全部 / 前缀过滤后的键（字典序）
--   Editor.settings.path()            背盘 JSON 的绝对路径（无工程为空串）
--
-- 落盘：write-behind。set/erase 只改内存并置脏，每帧至多写盘一次到
--   <工程根>/EditorScripts/plugin-settings.json
-- 载入：工程打开 / 插件重载（含 Tools/Reload）时先于插件脚本从盘读回，故 reload 后 get 能拿回上次写入的值。
--
-- 用法：
--   1. 打开 EmptyTest 工程，编辑器自动加载 <工程根>/EditorScripts/*.lua
--   2. 主菜单栏顶层出现 "Settings" 菜单，逐项点击即可
--
-- 约定（与 p0_demo.lua / p1_assets_demo.lua 一致）：
--   * 键用点分的 ASCII 字符串，本样例统一以 "p1demo." 命名空间，避免和其它插件撞键。
--   * 运行期字符串全 ASCII；注释可中文。
--   * 无工程时接口安全降级（get->default、set->false、keys->空、path->空串）。
--   * 复杂结构请自行 JSON 字符串化后作为一个 string 值存，或拆成多个标量键。
--

local NS = "p1demo."

-- ---------------------------------------------------------------------------
-- Settings/Write & read back —— 写入若干标量并立刻读回，含 default 语义
-- ---------------------------------------------------------------------------
Editor.addMenuItem("Settings/Write & read back", function()
    local okNum  = Editor.settings.set(NS .. "counter", 42)
    local okStr  = Editor.settings.set(NS .. "lastDir", "Textures/")
    local okBool = Editor.settings.set(NS .. "enabled", true)
    local okFloat = Editor.settings.set(NS .. "scale", 1.5)
    Editor.log("set ok: " .. tostring(okNum) .. " " .. tostring(okStr) .. " "
        .. tostring(okBool) .. " " .. tostring(okFloat))

    -- 读回；不存在的键返回给的 default（这里演示 "missing" -> "??"）
    Editor.log(NS .. "counter  = " .. tostring(Editor.settings.get(NS .. "counter")))
    Editor.log(NS .. "lastDir  = " .. tostring(Editor.settings.get(NS .. "lastDir")))
    Editor.log(NS .. "enabled  = " .. tostring(Editor.settings.get(NS .. "enabled")))
    Editor.log(NS .. "scale    = " .. tostring(Editor.settings.get(NS .. "scale")))
    Editor.log(NS .. "missing  = " .. tostring(Editor.settings.get(NS .. "missing", "??")))

    -- set(nil) 不可转换，返回 false 且不改动存储
    Editor.log("set(nil) ok = " .. tostring(Editor.settings.set(NS .. "counter", nil)))

    Editor.ui.notify("wrote sample values (see console)", 3)
end)

-- ---------------------------------------------------------------------------
-- Settings/List keys —— 全量键 + 本插件命名空间前缀过滤
-- ---------------------------------------------------------------------------
Editor.addMenuItem("Settings/List keys", function()
    local all = Editor.settings.keys()
    local n = 0
    for _ in ipairs(all) do n = n + 1 end
    Editor.log("total keys: " .. tostring(n))

    local mine = Editor.settings.keys(NS)
    Editor.log("keys under '" .. NS .. "':")
    for _, key in ipairs(mine) do
        Editor.log("  - " .. tostring(key) .. " = " .. tostring(Editor.settings.get(key)))
    end
    Editor.ui.notify("found " .. tostring(#mine) .. " '" .. NS .. "' keys (see console)", 3)
end)

-- ---------------------------------------------------------------------------
-- Settings/Persist roundtrip —— 验证 write-behind 落盘 + 读回绝对路径
--
-- 写一个带时间戳的值；等一帧泵把脏数据落盘后，path() 指向的 JSON 里应能看到该键。
-- 真正验证持久化：点完这里 -> Tools/Reload Plugins（或重开工程）-> "Write & read back"
-- 之外再跑 "List keys"，时间戳值仍在即说明读盘生效。
-- ---------------------------------------------------------------------------
Editor.addMenuItem("Settings/Persist roundtrip", function()
    local marker = NS .. "persistedAt"
    local stamp = tostring(os.time())
    if not Editor.settings.set(marker, stamp) then
        Editor.ui.notify("set failed (no project?)", 3)
        return
    end
    Editor.log("queued write: " .. marker .. " = " .. stamp)
    Editor.log("backing file: " .. tostring(Editor.settings.path()))

    -- 落盘发生在每帧末尾；下一帧再确认 dirty 已清、值仍在内存
    Editor.tick.after(0.2, function()
        local back = Editor.settings.get(marker)
        Editor.ui.notify("persisted value = " .. tostring(back), 4)
        Editor.log("read back after flush: " .. tostring(back))
    end)
end)

-- ---------------------------------------------------------------------------
-- Settings/Erase namespace —— 删掉本插件所有键，清理演示数据
-- ---------------------------------------------------------------------------
Editor.addMenuItem("Settings/Erase my keys", function()
    local mine = Editor.settings.keys(NS)
    local removed = 0
    for _, key in ipairs(mine) do
        if Editor.settings.erase(key) then
            removed = removed + 1
        end
        -- 删除后 has 应为 false
        if Editor.settings.has(key) then
            Editor.logWarning("erase did not take: " .. tostring(key))
        end
    end
    Editor.ui.notify("erased " .. tostring(removed) .. " '" .. NS .. "' keys", 3)
end)

-- Scripts/game/Tracker.lua
--
-- 埋点：把玩法事件送到宿主 analytics 后台（抖音 reportAnalytics / 微信 reportEvent，
-- 由 JS shim 分派，两家都不支持时静默丢弃）。
--
-- 设计约束：
--   * 埋点绝不影响游戏：调用全部 pcall 包裹；宿主 SDK 不可用（编辑器/桌面）时
--     模块整体静默，不打印不报错。
--   * 字段只收扁平一层的 string / number / boolean：宿主事件面板只认扁平值，
--     嵌套表序列化了也没法看，干脆不收。
--   * 事件名即漏斗的一级：boot / game_start / wave / game_over / restart /
--     voice_record / voice_clear；字段是二级维度。
--
-- 与 JS 侧的分工：boot（冷启动毫秒、平台）由 loading.js 在引擎首帧时直报，
-- 不经本模块——那时 Lua 还没跑起来；其余玩法事件都从这里走。
-- js_error 由 minigame_runtime.js 的 onError/onUnhandledRejection 直报，同理。

local Tracker = {}

local sdk = nil          -- MinigameSDK 子系统（宿主可用时才非空）
local resolved = false   -- 只解析一次：main.lua 加载早于子系统注册完成也没关系，
                         -- 首次 Report 发生在 Build 之后，那时子系统必然就绪

local function Resolve()
    if resolved then
        return
    end
    resolved = true
    sdk = GetSubsystem("MinigameSDK")
end

--- JSON 字符串转义：只处理会破坏 JSON 结构的字符。事件字段全部由游戏侧
--- 控制（不含玩家自由文本），控制字符不做全量转义。
local ESCAPE_PATTERN = '[' .. '"' .. '\\' .. '\n' .. '\r' .. '\t' .. ']'

local function Escape(text)
    return (tostring(text):gsub(ESCAPE_PATTERN, {
        ['"'] = '\\"',
        ['\\'] = '\\\\',
        ['\n'] = '\\n',
        ['\r'] = '\\r',
        ['\t'] = '\\t',
    }))
end

--- 单个字段值 → JSON 字面量。不支持的类型（nil/表/函数）返回 nil，字段被跳过。
local function EncodeValue(value)
    local t = type(value)
    if t == "number" then
        -- inf/nan 不是合法 JSON 数字，落到 0 比丢掉整条事件好
        if value ~= value or value == math.huge or value == -math.huge then
            return "0"
        end
        return tostring(value)
    elseif t == "boolean" then
        return tostring(value)
    elseif t == "string" then
        return '"' .. Escape(value) .. '"'
    end
    return nil
end

--- 扁平字段表 → JSON 对象字符串；空表/全不支持返回 nil（该事件不带参数上报）。
local function EncodeParams(fields)
    if type(fields) ~= "table" then
        return nil
    end
    local parts = {}
    for k, v in pairs(fields) do
        local encoded = EncodeValue(v)
        if encoded then
            parts[#parts + 1] = '"' .. Escape(k) .. '":' .. encoded
        end
    end
    if #parts == 0 then
        return nil
    end
    return "{" .. table.concat(parts, ",") .. "}"
end

--- 上报一个事件。fields 为扁平表（string/number/boolean），可空。
---@param event string 事件名（漏斗一级）
---@param fields table|nil 事件维度
function Tracker.Report(event, fields)
    Resolve()
    if not sdk or not sdk:IsAvailable() then
        return
    end
    local params = EncodeParams(fields)
    pcall(function()
        if params then
            sdk:ReportAnalytics(event, params)
        else
            sdk:ReportAnalytics(event)
        end
    end)
end

return Tracker

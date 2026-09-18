--
-- core/Counter.lua —— 派生类样例，演示"继承传播"
--
-- 关键写法：---@class Counter : Object —— 声明基类后，LuaLS 会把 Object 的成员
-- （New / Log / Test ...）一并算到 Counter 头上，实例 c = Counter:New() 里
-- c:Log(...) / c:Test() 都能补全，运行时也沿类链解析（见 Object:New 的 self 语义）。
--

local Object = require("core.Object")

---@class Counter : Object 可计数基建样例（继承自 Object）
local Counter = setmetatable({}, Object)
Counter.__index = Counter

--- 覆写构造钩子：计数从 0 起。
---@param start integer? 起始值，缺省 0
function Counter:Init(start)
    self.value = start or 0
end

--- 加一。
---@return integer 自增后的值
function Counter:Increment()
    self.value = (self.value or 0) + 1
    return self.value
end

--- 当前计数值。
---@return integer
function Counter:GetValue()
    return self.value or 0
end

return Counter

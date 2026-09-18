--
-- core/Object.lua —— 基建类体系根节点(也是"规范写法"模板 / 约定锚点)
--
-- ── 基建 lua 写法约定(照抄本文件与 core/Counter.lua 即可)────────────────
-- 目录与命名
--   * 基建模块放 Data/Scripts/ 下，按域分子目录(core/ ui/ ...)；文件名 = 类名，PascalCase。
--   * 只有 Scripts 根下的 main.lua 作为入口被执行；其余都是模块，靠 require 引入。
-- require 解析(运行期)
--   * 游戏 VM 已配 "Scripts/" require 前缀(见 Source/LuaScript/EngineLuaVM.cpp)，所以
--     require("core.Object") 解析到 Scripts/core/Object.lua；点号自动转斜杠，.lua/.luc 皆可。
--     空前缀仍保留做兜底，require("LuaPanda") 这类根级模块照旧可用。
--   * 模块一律 return 一个类表(或函数表)，调用方 local X = require("core.X") 取回。
-- 要"被补全 + 被继承"，模块只须三件事：
--   1) 类表上挂 ---@class，LuaLS 据此把它当成一个类型；require 回来即得该类型。
--   2) 构造/工厂方法标 ---@return self，调用方 local o = Object:New() 里 o 就被推成 Object。
--   3) 方法用冒号定义(self 隐式)，参数/返回值用 ---@param / ---@return 标注；
--      派生类写 ---@class Child : Base，成员沿继承链自动传播到子类型。
-- 补全生效(LuaLS 侧，与运行期无关)
--   * 引擎 API 补全来自 _apidocs/*.d.lua，已由 Data/Scripts/.luarc.json 的 workspace.library 挂上。
--   * 手写模块的补全来自 LuaLS 直接分析工作区源码：让 VSCode 打开的工作区根覆盖到 Scripts，
--     require("core.Object") 才会被解析到 core/Object.lua(名对不上路径时可用 ---@module 指认)。
-- 运行期字符串一律 ASCII：ImGui 字体图集不含中文字形，会显示到屏幕 / Console 的字符串都别用中文
--   (注释可中文)。多语言后续由翻译系统统一处理。
-- ---------------------------------------------------------------------------
--

---@class Object 所有基建类的基类
local Object = {}
Object.__index = Object

--- 创建一个本类的新实例。子类用 Counter:New() 调用时 self 即子类，
--- 得到的是 Counter 实例（元表指向子类，方法沿类链解析）。
---@return self
function Object:New()
    return setmetatable({}, self)
end

--- 生命周期钩子：子类覆写。默认空实现，保证父类可安全调用。
---@param ... any
function Object:Init(...) end -- luacheck: ignore ...

--- 打印一条带类名的调试信息（演示用，运行期字符串一律 ASCII）。
---@param message string
function Object:Log(message)
    print("[Object] " .. tostring(message))
end

-- 简单演示方法（保留最初 stub 的 Test 语义）
function Object:Test()
    print("call Object:Test")
end

return Object

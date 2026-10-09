-- Scripts/game/Materials.lua
--
-- 运行时的材质工厂 + 缓存。
--
-- 为什么要有这个模块：海岸上同一批纯色（沙、木、塔、防线、海面）会被几十个节点共用，
-- 每次都 Clone 一份既浪费内存，又让"把沙子调黄一点"变成改 N 处；而 rbfx 的材质是资源
-- 对象，多个 StaticModel 指向同一个实例就是共享。所以这里按 key 缓存一份。
--
-- 纯色为什么走 "Clone DefaultWhite + SetShaderParameter" 而不是新建一堆 XML：
-- DefaultWhite 是 PBR（LitOpaque + PBR defines），和场景里其余部分同一条光照路径，
-- 克隆后改 MatDiffColor / Roughness 就够；颜色与粗糙度都在 Config 里，改数值不碰资源文件。
-- 需要贴图或换技术的才落成 XML（见 Data/Materials/PirateAtlas.xml、VertexColor.xml）。

local Config = require("game.Config")

---@class Materials 材质工厂：模块级缓存，不实例化
local Materials = {}

---@type table<string, Material>
local tintCache = {}

--- 按 Config.Tint 的 key 取纯色材质（PBR，参与光照与阴影）。
---@param key string Config.Tint 里的键，如 "sand"
---@return Material
function Materials.Tint(key)
    local cached = tintCache[key]
    if cached then
        return cached
    end

    local color = Config.Tint[key]
    if not color then
        -- 洋红是最容易被看见的"配错了"信号，比返回 nil 让调用点崩掉好排查
        print("Materials.Tint: unknown tint key " .. tostring(key))
        color = Color(1.0, 0.0, 1.0)
    end

    local mat = GetResource("Material", Config.MAT_WHITE):Clone()
    mat:SetShaderParameter("MatDiffColor", color)
    local roughness = Config.Roughness[key]
    if roughness then
        mat:SetShaderParameter("Roughness", roughness)
    end

    tintCache[key] = mat
    return mat
end

--- pirate-kit 的图集材质：海盗、船、栈桥、棕榈、岩石全部共用这一份。
--- 导入走 -nm -nt 不产材质，所以材质是手写的（见 Data/Materials/PirateAtlas.xml）。
---@return Material
function Materials.Pirate()
    return GetResource("Material", Config.MAT_PIRATE)
end

--- 顶点色材质：给只有 UBYTE4_NORM:COLOR 没有 UV 的 geometry 用（Soldier 的散落道具）。
---@return Material
function Materials.VertexColor()
    return GetResource("Material", Config.MAT_VERTEX_COLOR)
end

--- 海面材质：低粗糙度纯色，靠太阳高光与雾做出"远处融进天光"的效果。
---
--- 没有用 CoreData 的 Materials/Showcase/LitWaterTiled.xml：它要 Textures/WaterNoise.dds
--- （本工程 Data 里没有这张图）外加一台反射相机与 ReflectionPlaneX/Y 参数（见 23_Water），
--- 对占位阶段太重，且在 wasm 上要多一个 render target。真要升级水面时再走那条路。
---@return Material
function Materials.Sea()
    return Materials.Tint("water")
end

--- 全透明材质：把不想看见的 geometry 藏起来。
---
--- 只用于 Character_Soldier.mdl 的 8..66 号 geometry（散落在角色前方的道具，挡狙击视线）。
--- 注意两点：一是它只是不画，射线照样能命中，所以 Shooting.lua 要做"命中主角就穿过去续查"；
--- 二是 UnlitTransparent 技术带 shadow pass，挂了它的模型必须 SetCastShadows(false)，
--- 否则地上会有一圈看不见的影子。详见 Data/Materials/Hidden.xml 的注释。
---@return Material
function Materials.Hidden()
    return GetResource("Material", Config.MAT_HIDDEN)
end

return Materials

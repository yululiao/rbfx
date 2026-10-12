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

--- 主角 Characters_Lis 的图集材质：主体 + 肩甲 + 手持道具共用一张 Zombie_Atlas.png。
--- 与 pirate 同因（导入 -nm -nt 不产材质）手写，配方见 Data/Materials/CharacterAtlas.xml。
---@return Material
function Materials.Character()
    return GetResource("Material", Config.MAT_CHARACTER)
end

--- 顶点色材质：给带 UBYTE4_NORM:COLOR 的 geometry 用。Soldier 的 FBX 是无贴图
--- 纯色材质方案，导入器已把各材质 Diffuse 烘进顶点色，全身统一用它渲染。
---@return Material
function Materials.VertexColor()
    return GetResource("Material", Config.MAT_VERTEX_COLOR)
end

--- 海面材质：PBR 不透明 + 法线贴图波纹（配方在 Data/Materials/Water.xml）。
---
--- 不用引擎的 LitWater/M_Water：它把 albedo 置零，可见色全靠环境立方图反射、
--- 水下背景折射、菲涅尔高光——本场景三者皆缺，实测整片漆黑。
--- 本配方走漫反射底色 + 法线扰动调制的太阳镜面高光（俯视角可见），
--- 波纹流动由 main.lua 每帧滚动 UOffset/VOffset。水色内嵌 Water.xml
--- （与旧 Config.Tint.water 同值），改水色去改那边。
---@return Material
function Materials.Sea()
    return GetResource("Material", Config.MAT_WATER)
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

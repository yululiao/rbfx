-- Scripts/game/Coast.lua
--
-- 海岸关卡的静态布景：天空、海、沙滩、瞭望塔、防线栅栏、栈桥与点缀。
--
-- 为什么全部程序化生成而不是在编辑器里手摆：
--   * 这批 Quaternius / Kenney 资产是占位，后面会整套换掉，手摆的场景到时候要重摆一遍；
--   * 布局是"数值"而不是"手工状态"，改 Config 里的坐标表就能调关卡，可 diff 可回滚；
--   * 敌人航道（Config.LANES）、防线（DEFENSE_Z）、塔位（TOWER_POS）都要和布景对齐，
--     同一份 Config 出两边，才不会改了一边忘了另一边。
--
-- 所有落位 y 都按实测包围盒算过（python_utils/parse_mdl.py 量 Cache 里的 .mdl）：
--   House2 3.78x9.38x3.74 y[0,9.38]、Dock/Dock_Pole y[-2.00,1.01]（桥面 1.01、桩到 -2）、
--   PalmTree_1 高 3.58 y[0.01,3.59]、Rock_* 高 0.6 上下、Cliff2 8.0x6.9x7.1 y[-0.78,6.13]、
--   Skulls 高 3.81、Prop_Barrel 0.72x0.66x0.72 y[0,0.66]、Ship_Small 5.14x1.99x3.60。
-- 模型大多自带"底部略低于 y=0"的埋地量，所以按 y=0 摆就是站在地上，不要再手动抬。

local Config = require("game.Config")
local Materials = require("game.Materials")

---@class Coast 海岸布景：模块级函数集，不实例化
local Coast = {}

local ROOT_NAME = "Coast"

--- 建一个静态模型节点。这是本模块唯一的落件入口，所有布景都走它。
---
--- scale 只接受 Vector3：C++ 侧 SetScale 有 float 与 Vector3 两个重载，但 Lua 绑定不一定
--- 两个都注册，统一传三分量最稳（工程里 prefab 的 Scale 写成单个数字曾被解析成 ZERO）。
---@param parent Node
---@param name string
---@param modelName string 资源名，如 "Models/Box.mdl"
---@param material Material|nil nil 表示不指定，用模型自带的
---@param position Vector3
---@param scale Vector3|nil
---@param yawDegrees number|nil 绕 y 的角度
---@param castShadows boolean|nil
---@return Node
local function Place(parent, name, modelName, material, position, scale, yawDegrees, castShadows)
    local node = parent:CreateChild(name)
    node:SetPosition(position)
    if scale then
        node:SetScale(scale)
    end
    if yawDegrees and yawDegrees ~= 0.0 then
        node:SetRotation(Quaternion(yawDegrees, Vector3.UP))
    end

    local model = node:CreateComponent("StaticModel")
    model:SetModel(GetResource("Model", modelName))
    if material then
        model:SetMaterial(material)
    end
    if castShadows ~= nil then
        model:SetCastShadows(castShadows)
    end
    return node
end

--- 摆一个 pirate-kit 的资产（共用图集材质，不投影的写 false）。
---@param parent Node
---@param assetName string 如 "Environment_PalmTree_1"
---@param position Vector3
---@param yawDegrees number|nil
---@param scale Vector3|nil
---@return Node
local function PlacePirate(parent, assetName, position, yawDegrees, scale)
    return Place(parent, assetName, Config.PirateModel(assetName), Materials.Pirate(),
        position, scale, yawDegrees, true)
end

----------------------------------------------------------------------
-- 天空 / 海 / 沙滩
----------------------------------------------------------------------

--- 天空盒。Skybox 组件的 ProcessRayQuery 是空实现（Skybox.cpp:59），所以它既不会挡住
--- 狙击射线，也不需要为了"别被射线打到"而缩到远裁剪面外。
---@param root Node
local function BuildSky(root)
    local node = root:CreateChild("Sky")
    node:SetScale(Vector3(Config.SKY_SCALE, Config.SKY_SCALE, Config.SKY_SCALE))
    local sky = node:CreateComponent("Skybox")
    sky:SetModel(GetResource("Model", Config.SKY_MODEL))
    sky:SetMaterial(GetResource("Material", Config.MAT_SKY))
    return node
end

--- 海面：一张放大到 400x400 的 Plane，+z 边贴住水线，所以内陆方向不会露出蓝面。
--- 低粗糙度纯色（Materials.Sea），靠太阳高光和雾做远近层次；真正的波浪水面不在 MVP 里。
---@param root Node
local function BuildSea(root)
    local node = Place(root, "Sea", Config.PLANE_MODEL, Materials.Sea(),
        Vector3(0.0, Config.SEA_Y, Config.SEA_CENTER_Z),
        Vector3(Config.SEA_SIZE, 1.0, Config.SEA_SIZE), nil, false)
    -- surface 变量：Shooting 打中没挂 unit 的东西时，靠它决定是水花还是尘土
    node:SetVar("surface", "water")
    return node
end

--- 沙滩：从水线往内陆铺满的一张大 Plane。远端不封边，交给雾（FOG_END 210）吃掉。
---@param root Node
local function BuildSand(root)
    local centerZ = Config.SHORE_Z + Config.SAND_SIZE * 0.5
    local node = Place(root, "Sand", Config.PLANE_MODEL, Materials.Tint("sand"),
        Vector3(0.0, Config.SAND_Y, centerZ),
        Vector3(Config.SAND_SIZE, 1.0, Config.SAND_SIZE), nil, false)
    node:SetVar("surface", "sand")
    return node
end

----------------------------------------------------------------------
-- 光照与雾
----------------------------------------------------------------------

--- 复用场景里的 "Arena Sun"（编辑器手搭的那盏），改名 Sun 并按 Config 定向定色。
--- 场景里没有就自己建一盏，保证 Lua 单独跑也亮。
---@param scene Scene
---@return Node
local function BuildSun(scene)
    local node = scene:GetChild("Arena Sun", true) or scene:GetChild("Sun", true)
    if not node then
        node = scene:CreateChild("Sun")
        node:CreateComponent("Light")
    elseif not node:GetComponent("Light") then
        node:CreateComponent("Light")
    end
    -- 不改名：rbfx 的 Node:SetName 绑定收的是 eastl String（usertype），Lua 字符串
    -- 字面量喂不进去（会报 expected userdata）。改名纯装饰，节点引用已在手，跳过。
    node:SetDirection(Config.SUN_DIRECTION)
    local light = node:GetComponent("Light")
    light:SetColor(Config.SUN_COLOR)
    light:SetBrightness(Config.SUN_BRIGHTNESS)
    light:SetCastShadows(true)
    return node
end

--- 雾与环境光。海面没有遮挡物，全靠雾把 200 米外的船融进天光里做出距离感；
--- 雾色与 Sky.png 的地平线色对齐，海天线才看不出接缝。
---@param scene Scene
---@return Component|nil
local function BuildZone(scene)
    local node = scene:GetChild("Global Zone", true)
    if not node then
        node = scene:CreateChild("Global Zone")
    end
    local zone = node:GetComponent("Zone")
    if not zone then
        zone = node:CreateComponent("Zone")
    end

    zone:SetFogColor(Config.FOG_COLOR)
    zone:SetFogStart(Config.FOG_START)
    zone:SetFogEnd(Config.FOG_END)
    if Config.AMBIENT_COLOR then
        zone:SetAmbientColor(Config.AMBIENT_COLOR)
    end
    return zone
end

----------------------------------------------------------------------
-- 主角阵地：木桩高台
----------------------------------------------------------------------

--- 瞭望塔 = 四根 Dock_Pole + 一块 Box 平台。
---
--- 不用 Environment_House2 当塔身：它有 9.38m 高，而主角站在 4m 的平台上，会被整个
--- 包在屋子里，第三人称相机也在屋内。占位阶段用高台最省事，且主角剪影清楚。
---@param root Node
---@return Node 平台节点（主角的父节点，站上去就是 PLATFORM_Y + 半厚）
local function BuildTower(root)
    local tower = root:CreateChild("Tower")
    tower:SetPosition(Config.TOWER_POS)

    local half = Config.PLATFORM_SIZE.x * 0.5
    local inset = Config.TOWER_POLE_INSET
    if inset > half then
        -- 桩子跑到平台外面就成了四根孤零零的柱子，这是配错了，必须能看见
        print(string.format("Coast: TOWER_POLE_INSET %.2f 大于平台半宽 %.2f，桩子在平台外", inset, half))
    end

    local poleScale = Vector3(1.0, Config.TOWER_POLE_SCALE_Y, 1.0)
    local corners = {
        Vector3(-inset, 0.0, -inset), Vector3(inset, 0.0, -inset),
        Vector3(-inset, 0.0, inset), Vector3(inset, 0.0, inset),
    }
    for _, corner in ipairs(corners) do
        -- 桩子本地 y[-2,1.01]，缩 y 之后下半截埋进沙里，看不见也不用管
        local pole = PlacePirate(tower, "Environment_Dock_Pole", corner, 0.0, poleScale)
        pole:SetVar("noShoot", true)
    end

    local deck = Place(tower, "Deck", Config.BOX_MODEL, Materials.Tint("wood"),
        Vector3(0.0, Config.PLATFORM_Y - Config.PLATFORM_SIZE.y * 0.5, 0.0),
        Config.PLATFORM_SIZE, nil, true)
    deck:SetVar("noShoot", true)

    -- 平台底面加一层交叉梁，仰视时不至于看见一块悬空的薄板
    local beam = Vector3(Config.PLATFORM_SIZE.x * 0.9, 0.16, 0.22)
    local beam1 = Place(tower, "Beam1", Config.BOX_MODEL, Materials.Tint("woodDark"),
        Vector3(0.0, Config.PLATFORM_Y - Config.PLATFORM_SIZE.y - 0.12, 0.0), beam, 45.0, false)
    local beam2 = Place(tower, "Beam2", Config.BOX_MODEL, Materials.Tint("woodDark"),
        Vector3(0.0, Config.PLATFORM_Y - Config.PLATFORM_SIZE.y - 0.12, 0.0), beam, -45.0, false)
    beam1:SetVar("noShoot", true)
    beam2:SetVar("noShoot", true)

    return deck
end

--- 防线栅栏：一排木桩横贯滩头，海盗走到 DEFENSE_Z 就砍它。纯视觉 + 位置标记，
--- 血量在 Waves.lua 里；桩子不挂 unit 变量，所以打中它们算"打木头"。
---@param root Node
---@return Node
local function BuildPalisade(root)
    local line = root:CreateChild("DefenseLine")
    local count = Config.PALISADE_COUNT
    local span = Config.PALISADE_SPAN_X
    local post = Config.PALISADE_POST
    for i = 0, count - 1 do
        local x = -span * 0.5 + span * (i / (count - 1))
        -- 高低错落一点，一排完全等高的桩子看着像栅栏贴图而不是工事
        local height = post.y * (0.82 + 0.36 * ((i * 7) % 5) / 4.0)
        local p = Place(line, "Post", Config.BOX_MODEL, Materials.Tint("woodDark"),
            Vector3(x, height * 0.5, Config.DEFENSE_Z),
            Vector3(post.x, height, post.z), (i % 3) * 7.0 - 7.0, true)
        -- 防线是自己的工事，不能挡住射向它后面敌人/桶的子弹
        p:SetVar("noShoot", true)
    end
    return line
end

--- 栈桥：Dock 模块沿 -z 一节节排到水里，桥面落在 SEA_Y + 1.01。
---@param root Node
---@return Node
local function BuildDock(root)
    local dock = root:CreateChild("Dock")
    local y = Config.SEA_Y
    for i = 0, Config.DOCK_COUNT - 1 do
        local z = Config.DOCK_Z_START - i * Config.DOCK_STEP
        -- 最后一节换成残破版，桥尽头有个缺口，比一条整齐的桥更像海盗据点
        local asset = (i == Config.DOCK_COUNT - 1) and "Environment_Dock_Broken" or "Environment_Dock"
        PlacePirate(dock, asset, Vector3(Config.DOCK_X, y, z), (i % 2) * 3.0 - 1.5)
    end
    return dock
end

----------------------------------------------------------------------
-- 点缀：崖壁、棕榈、岩石、 Skulls、村落
----------------------------------------------------------------------

---@param root Node
local function BuildScenery(root)
    local scenery = root:CreateChild("Scenery")
    local groundY = Config.SAND_Y

    for _, entry in ipairs(Config.CLIFFS) do
        local name, x, z, yaw = entry[1], entry[2], entry[3], entry[4]
        PlacePirate(scenery, name, Vector3(x, groundY, z), yaw)
    end

    for i, entry in ipairs(Config.PALMS) do
        local x, z, yaw = entry[1], entry[2], entry[3]
        -- 三种棕榈轮着用，一片全同一种树看着像复制粘贴
        local name = "Environment_PalmTree_" .. tostring(1 + (i % 3))
        PlacePirate(scenery, name, Vector3(x, groundY, z), yaw)
    end

    for i, entry in ipairs(Config.ROCKS) do
        local x, z, yaw, scale = entry[1], entry[2], entry[3], entry[4] or 1.0
        local name = "Environment_Rock_" .. tostring(1 + (i % 5))
        PlacePirate(scenery, name, Vector3(x, groundY + 0.08, z), yaw,
            Vector3(scale, scale, scale))
    end

    for _, entry in ipairs(Config.DECOR) do
        local name, x, z, yaw = entry[1], entry[2], entry[3], entry[4]
        PlacePirate(scenery, name, Vector3(x, groundY, z), yaw)
    end

    for _, entry in ipairs(Config.VILLAGE) do
        local name, x, z, yaw = entry[1], entry[2], entry[3], entry[4]
        PlacePirate(scenery, name, Vector3(x, groundY, z), yaw)
    end

    return scenery
end

----------------------------------------------------------------------
-- 对外接口
----------------------------------------------------------------------

--- 清掉上一次生成的布景。编辑器 Play 停止时会快照回滚，但脚本热重载与桌面
--- LuaGamePlayer 反复起停不会，留个显式清理免得场景里叠出两份海岸。
---@param scene Scene
function Coast.Clear(scene)
    local root = scene:GetChild(ROOT_NAME, true)
    if root then
        root:Remove()
    end
end

--- 生成整套海岸。
---@param scene Scene
---@return CoastHandles
function Coast.Build(scene)
    Coast.Clear(scene)

    local root = scene:CreateChild(ROOT_NAME)
    BuildSky(root)
    local sea = BuildSea(root)
    local sand = BuildSand(root)
    local deck = BuildTower(root)
    BuildPalisade(root)
    BuildDock(root)
    BuildScenery(root)

    local sun = BuildSun(scene)
    BuildZone(scene)

    print(string.format("Coast MARK built: sea=%.0fm sand=%.0fm tower=(%.1f,%.1f,%.1f) deckY=%.2f defenseZ=%.1f",
        Config.SEA_SIZE, Config.SAND_SIZE, Config.TOWER_POS.x, Config.TOWER_POS.y, Config.TOWER_POS.z,
        Config.PLATFORM_Y, Config.DEFENSE_Z))

    ---@class CoastHandles
    return {
        root = root,
        sea = sea,
        sand = sand,
        sun = sun,
        deck = deck,
        -- 主角站位：平台上表面中心。脚底 y = PLATFORM_Y，别把角色原点当脚底用。
        playerSpot = Config.TOWER_POS + Vector3(0.0, Config.PLATFORM_Y, 0.0),
    }
end

return Coast

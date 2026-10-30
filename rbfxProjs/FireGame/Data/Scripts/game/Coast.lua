-- Scripts/game/Coast.lua
--
-- 海岸关卡的静态布景：天空、海、瞭望塔、两座炮塔、栈桥与点缀（沙滩不再程序化生成，改由场景里手摆的 prefab 提供）。
--
-- 沙滩（BuildSand）已删除：海岛地面改由场景里手摆的 patch-sand prefab 提供。
-- Config.ShoreZ(x) 那条曲线仍保留——它还是玩法判据（涉水/靠岸/走位下界）与海面铺位的
-- 真源，只是不再驱动一张程序化的沙网格。
--
-- 为什么全部程序化生成而不是在编辑器里手摆：
--   * 这批 Quaternius / Kenney 资产是占位，后面会整套换掉，手摆的场景到时候要重摆一遍；
--   * 布局是"数值"而不是"手工状态"，改 Config 里的坐标表就能调关卡，可 diff 可回滚；
--   * 敌人航道（Config.LANES）、防线（DEFENSE_Z）、塔位（TOWER_POS）都要和布景对齐，
--     同一份 Config 出两边，才不会改了一边忘了另一边。
--
-- 所有落位 y 都按实测包围盒算过（解析 Cache 里 .mdl 的顶点缓冲求 AABB：UMD3 头部后面的
-- vertex buffer 块按元素描述子算 stride 与 position 偏移，逐个顶点取 min/max，与引擎
-- Geometry 在 EndLoad 里算的盒子同源）：
--   House2 3.78x9.38x3.74 y[0,9.38]、Dock/Dock_Pole y[-2.00,1.01]（桥面 1.01、桩到 -2）、
--   PalmTree_1 高 3.58 y[0.01,3.59]、Rock_* 高 0.6 上下、Cliff2 8.0x6.9x7.1 y[-0.78,6.13]、
--   Skulls 高 3.81、Prop_Barrel 0.72x0.66x0.72 y[0,0.66]、Prop_Cannon 1.04x1.12x1.48
--   y[0.0009,1.12]、z[-0.87,0.61]（炮管往 -z 伸更长，即原生炮口朝海）。
--   船族：ship-row-large 2.75x0.85x2.85 y[0,0.85]、ship-pirate-large 4.80x9.96x13.10 y[0,9.96]
--   （旧 Ship_Small 5.14x1.99x3.60 / Ship_Large 已连资产删除，不再由本模块的 Place 落位）。
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
-- 天空 / 海
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

--- 海面：一张放大到 400x400 的 Plane，+z 边贴到岸线的最低那一处，所以曲线低谷里也不会
--- 冒出蓝面（岸线见 Config.ShoreZ：它比沙滩高/低的那几段由沙滩网格自己跟着弯）。
--- 水面材质见 Materials.Sea → Data/Materials/Water.xml（PBR + 法线波纹），
--- 波纹流动由 main.lua 每帧滚动 UOffset/VOffset（海面只有一个用户，直接改共享材质）。
---@param root Node
local function BuildSea(root)
    local node = Place(root, "Sea", Config.PLANE_MODEL, Materials.Sea(),
        Vector3(0.0, Config.SEA_Y, Config.SEA_CENTER_Z),
        Vector3(Config.SEA_SIZE, 1.0, Config.SEA_SIZE), nil, false)
    -- surface 变量：Shooting 打中没挂 unit 的东西时，靠它决定是水花还是尘土
    node:SetVar("surface", "water")
    return node
end

-- （BuildSand 已删除：沙滩网格不再于运行期生成。）

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

--- 防线 = 两座会自己开炮的炮塔（海盗淌到滩头就会挨它打，摸过来就砍它）。
---
--- 本函数只负责摆：血量住在 Waves、开火住在 Turrets，两边都读 Config.TURRETS 这一张表。
--- 每塔一个空节点当逻辑中心（放在 (x, 0, DEFENSE_Z)），炮口世界坐标就是「塔位 + Config.TURRET_MUZZLE」。
--- 塔身改用 pirate-kit 瞭望塔 prefab（tower-complete-small）缩小一档坐地，塔顶架一门 Prop_Cannon 当炮
--- （失能时 Turrets 把这根炮管歪下来）。原塔高 6.78m 会横切相机 35°俯视朝海的视线，故按 Config 缩放。
--- 塔身与炮都挂 noShoot：它们在主角（z≈11~22）与敌人（z<9）之间的枪线上，不挂就会吃掉自己人的子弹。
---@param root Node
---@return { node: Node, gun: Node, x: number }[] 每塔句柄（下标与 Config.TURRETS 对齐）
local function BuildTurrets(root)
    local line = root:CreateChild("DefenseLine")
    local handles = {}
    for i, x in ipairs(Config.TURRETS) do
        local tower = line:CreateChild("Turret_" .. tostring(i))
        tower:SetPosition(Vector3(x, 0.0, Config.TURRET_Z))

        -- 塔身：prefab 缩小后坐地（模型底 y=0）。scale 走 Vector3 三分量，单值曾被解析成 ZERO。
        local prefab = GetResource("PrefabResource", Config.PiratePrefab(Config.TURRET_TOWER_PREFAB))
        if prefab then
            local hull = tower:InstantiatePrefab(prefab, Vector3.ZERO, Quaternion.IDENTITY)
            if hull then
                local s = Config.TURRET_TOWER_SCALE
                hull:SetScale(Vector3(s, s, s))
                hull:SetVar("noShoot", true)
            end
        else
            LogError("Coast: 炮塔 prefab 加载失败 '" .. Config.PiratePrefab(Config.TURRET_TOWER_PREFAB) .. "'")
        end
        
        -- 炮身：架在塔顶平台上（炮座比尖顶塔顶低 0.2 免得悬空；Prop_Cannon 原点在炮架底面），
        -- yaw=0 即炮口朝海。Turrets 失能时把它歪下来。
        local gun = PlacePirate(tower, "Prop_Cannon", Vector3(0.0, Config.TURRET_CANNON_SEAT_Y, 0.0), 0.0)
        gun:SetVar("noShoot", true)

        handles[#handles + 1] = { node = tower, gun = gun, x = x }
    end
    return handles
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

    -- 自检：这批表里的 z 全部写在直线水线（旧 -0.5）以内，而岸线现在是一条曲线。
    -- 曲线最靠陆那处才 0.7m，按现有表没人会落到海里；但以后往表里加一个贴水线的件
    -- 就会漂在水上（这些都是纯静态摆件，不报错也不响），所以这里把它变成看得见的。
    -- z 的下标逐表不同（CLIFFS/VILLAGE 是 name,x,z,yaw；PALMS/ROCKS/DECOR 是 x,z,...
    -- 或 name,x,z），靠猜会猜错（VILLAGE 的 [3] 是 yaw），以这里显式声明为准。
    local shoreMax = Config.ShoreZMax()
    local scatter = {
        { list = Config.CLIFFS, zi = 3 },
        { list = Config.PALMS, zi = 2 },
        { list = Config.ROCKS, zi = 2 },
        { list = Config.DECOR, zi = 3 },
        { list = Config.VILLAGE, zi = 3 },
    }
    for _, group in ipairs(scatter) do
        for _, entry in ipairs(group.list) do
            local z = entry[group.zi]
            if type(z) == "number" and z < shoreMax + 0.5 then
                print(string.format("Coast: 摆件落在岸线以内 (%s z=%.1f < 岸线最大 %.1f)，会漂在水上",
                    tostring(entry[1]), z, shoreMax))
            end
        end
    end

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
-- 玩家专属船（载具）
----------------------------------------------------------------------

--- 在码头旁放一条属于玩家的船（与敌人小船同一张 ship-row-large prefab），供 Player:BoardBoat 接管驱动。
--- 与敌人船无关：这条标 noShoot，不会被命中结算/爆炸波及。
---@param root Node
---@return Node
local function BuildPlayerBoat(root)
    -- 与 Enemy:SpawnBoat 同构：外层空节点当逻辑真值（位置/朝向/子节点挂主角），
    -- 船体从 prefab 实例化作子节点；Place 只能吃 modelName，所以这里不走它。
    local boat = root:CreateChild("PlayerBoat")
    boat:SetPosition(Config.BOAT_SPAWN)
    boat:SetRotation(Quaternion(Config.BOAT_YAW, Vector3.UP))
    local prefab = GetResource("PrefabResource", Config.PiratePrefab("ship-row-large"))
    if prefab then
        local hull = boat:InstantiatePrefab(prefab, Vector3.ZERO, Quaternion.IDENTITY)
        if hull then
            hull:SetScale(Config.BOAT_SMALL_SCALE)
        end
    else
        LogError("Coast: 玩家船 prefab 加载失败 '" .. Config.PiratePrefab("ship-row-large") .. "'")
    end
    boat:SetVar("noShoot", true)
    return boat
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
    local seaMaterial = Materials.Sea()
    local deck = BuildTower(root)
    local turrets = BuildTurrets(root)
    BuildDock(root)
    BuildScenery(root)
    local playerBoat = BuildPlayerBoat(root)

    local sun = BuildSun(scene)
    BuildZone(scene)

    print(string.format("Coast MARK built: sea=%.0fm shore[%.2f,%.2f] tower=(%.1f,%.1f,%.1f) deckY=%.2f turrets=%d at z=%.1f",
        Config.SEA_SIZE,
        Config.ShoreZMin(), Config.ShoreZMax(),
        Config.TOWER_POS.x, Config.TOWER_POS.y, Config.TOWER_POS.z,
        Config.PLATFORM_Y, #turrets, Config.DEFENSE_Z))

    ---@class CoastHandles
    return {
        root = root,
        sea = sea,
        -- 海面材质（与 Sea 节点同源）：main.lua 每帧滚动它的 UOffset/VOffset 做波纹流动
        seaMaterial = seaMaterial,
        sun = sun,
        deck = deck,
        -- 主角站位：平台上表面中心。脚底 y = PLATFORM_Y，别把角色原点当脚底用。
        playerSpot = Config.TOWER_POS + Vector3(0.0, Config.PLATFORM_Y, 0.0),
        -- 玩家专属船节点（载具），main.lua 传给 Player
        playerBoat = playerBoat,
        -- 两座炮塔句柄（{ node, gun, x }），main.lua 传给 Turrets；下标即塔号
        turrets = turrets,
    }
end

return Coast

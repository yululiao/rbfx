-- Scripts/game/Coast.lua
--
-- 海岸关卡的静态布景：天空、海、沙滩网格、瞭望塔、两座炮塔、栈桥与点缀。
--
-- 沙滩不是摆在场景里的资产，而是按 Config.ShoreZ(x) 现算的一张网格：岸线既是图形也是
-- 判据（涉水/靠岸/走位下界），所以它只能从那张控制点表里长出来，不能两块矩形对对接。
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
-- 本模块手写网格用的索引打包助手（沙滩是唯一一处手写网格）
--
-- 顶点数据不再走“按属性分块”的助手：VertexBuffer 用 element map 声明的是交织（AoS）
-- 布局，Update 吃的字节流必须逐顶点按元素顺序排（见 BuildSand 里的 string.pack），
-- 不能先铺完所有位置再铺法线——那样引擎会把法线字节当位置读，整张网格坐标全乱。
----------------------------------------------------------------------

--- 索引表按 ushort 打包（顶点数远小于 65535，不用 largeIndices）。
---@param values number[]
---@return string
local function PackUShorts(values)
    local parts = {}
    for i = 1, #values do
        parts[i] = string.pack("<I2", values[i])
    end
    return table.concat(parts, "", 1, #values)
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

--- 沙滩：从 Config.ShoreZ(x) 那条曲线往内陆铺的一张手写网格。
---
--- 为什么不再是整块 Plane：轴对齐的矩形边就是屏幕上那条直岸线——岸线要弯，几何就必须
--- 有顶点跟着弯。但整张网格不需要到处都细：只有贴水线那一条带是“岸线本体”，往内陆
--- 是三级逐渐拉长的条带（密/中/稀），铺到雾外总共才几百个顶点。
---
--- 顶点布局跟引擎 Box.mdl/kenney 导入件对齐（POSITION/NORMAL/TEXCOORD0/TANGENT，逐顶点交织 stride 36）：
--- 纯色材质是 DefaultWhite（LitOpaque+PBR）的 Clone，跟现在那块 Box 同一条渲染路径，
--- 不会因为少个属性而在某些 pass 里取到未定义数据。UV 按“1 单 = 1 米”给，现在沙是纯色
--- 用不上，但日后想换一张沙贴图只要调重复率，不用重烘网格。
--- 三行分工：首行铺平到“最低水线再往海 0.6”（盖住海水矩形的 +z 边，整行都在曲线海侧），
--- 第 2 行逐列贴 shoreZ(x)（它就是屏幕上那条岸线），第 3 行抬到整条曲线的包络之上。
--- 为什么不能让首行去贴曲线：那它在低谷列只比海边靠陆 0.02（零头，抹不掉那条蓝边），
--- 在岬角列反而退到海里 1.4m。为什么第 3 行不能铺平：海面 +z 边是个全局常数，而 x=0 处
--- 岸线比它高 1.4m，平面行基址在局部岸线之下，会在岬角那侧漏出一条蓝楔子。
--- 包络抬法还有个好处：行间间距在所有 x 处都非零，不会塌成退化四边形。
---@param root Node
---@return Node
local function BuildSand(root)
    local spanX = Config.BEACH_SPAN_X
    local nCols = Config.SHORE_SAMPLES + 1
    local zShoreMin = Config.ShoreZMin()

    -- x 列：等分（岸线方向要均匀，否则曲线会被分段不均拉得不对称）
    local xs = {}
    for i = 1, nCols do
        xs[i] = -spanX + (2.0 * spanX) * (i - 1) / (nCols - 1)
    end

    -- z 行基址：贴水线的密带 + 一条中带 + 几条大条带。首行是最低水线再往海一点，
    -- 给共面闪烁留余量（那一段埋在海水里，看不见）。
    local denseRows = 8
    local zs = { zShoreMin - 0.6 }
    for i = 1, denseRows do
        zs[#zs + 1] = zShoreMin + Config.SHORE_BAND_Z * i / denseRows
    end
    for i = 1, 6 do
        zs[#zs + 1] = (zShoreMin + Config.SHORE_BAND_Z)
            + (Config.SHORE_BAND_MID_Z - zShoreMin - Config.SHORE_BAND_Z) * i / 6.0
    end
    for i = 1, Config.SHORE_FAR_ROWS do
        zs[#zs + 1] = Config.SHORE_BAND_MID_Z
            + (Config.SHORE_FOG_END_Z - Config.SHORE_BAND_MID_Z) * i / Config.SHORE_FAR_ROWS
    end
    local nRows = #zs

    local verts, idx = {}, {}
    -- 贴曲线那一行的下界：整条曲线的包络再往内陆一个行间距。再往内的行基址（≥ 5.3）
    -- 本来就高于岸线最大值，铺平 z 不会穿水。
    local envelope = Config.ShoreZMax() + Config.SHORE_BAND_Z / denseRows
    for r = 1, nRows do
        local zBase = zs[r]
        for c = 1, nCols do
            local x = xs[c]
            local z = zBase
            if r == 2 then
                z = Config.ShoreZ(x) + 0.02       -- 岸线本体：逐列贴水线
            elseif r == 3 and zBase < envelope then
                z = envelope
            end
            local k = (r - 1) * nCols + c
            -- 逐顶点交织（AoS），字段顺序必须和下面 SetSize 的 VertexElement 完全一致：
            -- POSITION(3 float) NORMAL(3 float) TEXCOORD(2 float) TANGENT(4 byte)，共 36 字节/顶点。
            -- 平铺地面的切线 TX=(1,0,0)、TY=(0,1,0)，w=255 即正手性（引擎的约定）。
            verts[k] = string.pack("<ffffff", x, Config.SAND_Y, z, 0.0, 1.0, 0.0)
                .. string.pack("<ff", x, z)
                .. string.pack("<BBBB", 255, 128, 128, 255)
        end
    end
    -- 绕序：从上往下看 A(x0,z0) B(x1,z0) C(x0,z1) 是逆时针，即正面朝上。
    -- 万一在这台引擎/这套后端上是反的，改 Config.SAND_TWIST=1 即可，不必动这里。
    local twist = (Config.SAND_TWIST or 0) ~= 0
    for r = 1, nRows - 1 do
        for c = 1, nCols - 1 do
            local a = (r - 1) * nCols + c        -- (x0, z0)
            local b = a + 1                      -- (x1, z0)
            local cc = a + nCols                 -- (x0, z1)
            local d = cc + 1                     -- (x1, z1)
            if twist then
                idx[#idx + 1] = a; idx[#idx + 1] = cc; idx[#idx + 1] = b
                idx[#idx + 1] = b; idx[#idx + 1] = cc; idx[#idx + 1] = d
            else
                idx[#idx + 1] = a; idx[#idx + 1] = b; idx[#idx + 1] = cc
                idx[#idx + 1] = b; idx[#idx + 1] = d; idx[#idx + 1] = cc
            end
        end
    end

    local vb = VertexBuffer()
    vb:SetDebugName("BeachMesh.vb")
    vb:SetShadowed(true)
    vb:SetSize(#xs * #zs, {
        VertexElement(VET.VECTOR3, VSEM.POSITION),
        VertexElement(VET.VECTOR3, VSEM.NORMAL),
        VertexElement(VET.VECTOR2, VSEM.TEXCOORD),
        VertexElement(VET.UBYTE4_NORM, VSEM.TANGENT),
    })
    -- 一次 Update 喂满交织好的整段顶点流（每个顶点 36 字节，顺序见上面 string.pack）。
    vb:Update(table.concat(verts))

    local ib = IndexBuffer()
    ib:SetDebugName("BeachMesh.ib")
    ib:SetShadowed(true)
    ib:SetSize(#idx, false, false)   -- 第三参 dynamic：C++ 有默认值，但这里是原生 LUA_MEMBER_FUNC 绑定，
                                     -- sol3 不透传 C++ 默认参，Lua 必须显式喂满（漏了会报栈索引 4 expected boolean）
    ib:Update(PackUShorts(idx))

    local geom = Geometry()
    geom:SetVertexBuffer(0, vb)
    geom:SetIndexBuffer(ib)
    geom:SetDrawRange(0, 0, #idx)      -- 0 == TRIANGLE_LIST

    -- buffer 要列回 Model：不列也不会错画，但这份网格是运行期造的，没得序列化回去
    local model = Model()
    model:SetNumGeometries(1)
    model:SetGeometry(0, 0, geom)
    model:SetVertexBuffers({ vb }, { 0 }, { 0 })
    model:SetIndexBuffers({ ib })
    -- 包围盒：几何是按世界坐标写的（node 在原点），z 下界取首行基址（比“贴曲线那行”
    -- 更靠海）、上界取末行——宁大不小：盒子小了会被距离/视锥误剔除，整片沙突然消失。
    model:SetBoundingBox(BoundingBox(
        Vector3(-spanX, Config.SAND_Y - 0.05, zs[1]),
        Vector3(spanX, Config.SAND_Y + 0.05, zs[nRows])))

    local node = root:CreateChild("Sand")
    node:SetPosition(Vector3(0.0, 0.0, 0.0))
    local sm = node:CreateComponent("StaticModel")
    sm:SetModel(model)
    sm:SetMaterial(Materials.Tint("sand"))
    sm:SetCastShadows(false)
    -- surface 变量：Shooting 打中没挂 unit 的东西时，靠它决定是水花还是尘土
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

--- 防线 = 两座会自己开炮的炮塔（海盗淌到滩头就会挨它打，摸过来就砍它）。
---
--- 本函数只负责摆：血量住在 Waves、开火住在 Turrets，两边都读 Config.TURRETS 这一张表。
--- 每塔一个空节点当逻辑中心（放在 (x, 0, DEFENSE_Z)），台面/腿/炮都挂它下面，所以
--- Turrets 算炮口世界坐标就是「塔位 + Config.TURRET_MUZZLE」，不再重算一遍几何。
--- 构型与瞭望塔同源（四根 Dock_Pole 架一块 Box 台面），但刻意矮：相机从主角后上方
--- 35 度俯视朝海，塔一高就横切视线。
--- 所有件都挂 noShoot：它们在主角（z≈11~22）与敌人（z<9）之间的枪线上，
--- 不挂就会吃掉自己人的子弹（原栅栏就是这条理由）。
---@param root Node
---@return { node: Node, gun: Node, x: number }[] 每塔句柄（下标与 Config.TURRETS 对齐）
local function BuildTurrets(root)
    local line = root:CreateChild("DefenseLine")
    local handles = {}
    for i, x in ipairs(Config.TURRETS) do
        local tower = line:CreateChild("Turret_" .. tostring(i))
        tower:SetPosition(Vector3(x, 0.0, Config.TURRET_Z))

        local inset = Config.TURRET_POLE_INSET
        local poleScale = Vector3(1.0, Config.TURRET_POLE_SCALE_Y, 1.0)
        local corners = {
            Vector3(-inset, 0.0, -inset), Vector3(inset, 0.0, -inset),
            Vector3(-inset, 0.0, inset), Vector3(inset, 0.0, inset),
        }
        for _, corner in ipairs(corners) do
            -- 桩子本地 y[-2,1.01]，下半截埋进沙里，和瞭望塔同理
            local pole = PlacePirate(tower, "Environment_Dock_Pole", corner, 0.0, poleScale)
            pole:SetVar("noShoot", true)
        end

        local deck = Place(tower, "Deck", Config.BOX_MODEL, Materials.Tint("wood"),
            Vector3(0.0, Config.TURRET_DECK_Y - Config.TURRET_DECK.y * 0.5, 0.0),
            Config.TURRET_DECK, nil, true)
        deck:SetVar("noShoot", true)

        -- 台底交叉梁：少一层从低角度看就是一块悬空薄板（与瞭望塔同一处理）
        local beam = Vector3(Config.TURRET_DECK.x * 0.9, 0.16, 0.22)
        local beamY = Config.TURRET_DECK_Y - Config.TURRET_DECK.y - 0.12
        local beam1 = Place(tower, "Beam1", Config.BOX_MODEL, Materials.Tint("woodDark"),
            Vector3(0.0, beamY, 0.0), beam, 45.0, false)
        local beam2 = Place(tower, "Beam2", Config.BOX_MODEL, Materials.Tint("woodDark"),
            Vector3(0.0, beamY, 0.0), beam, -45.0, false)
        beam1:SetVar("noShoot", true)
        beam2:SetVar("noShoot", true)

        -- 炮身：坐台面上表面（原点在炮架底），yaw=0 即炮口朝海。Turrets 失能时把它歪下来。
        local gun = PlacePirate(tower, "Prop_Cannon", Vector3(0.0, Config.TURRET_DECK_Y, 0.0), 0.0)
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
    local sand = BuildSand(root)
    local deck = BuildTower(root)
    local turrets = BuildTurrets(root)
    BuildDock(root)
    BuildScenery(root)
    local playerBoat = BuildPlayerBoat(root)

    local sun = BuildSun(scene)
    BuildZone(scene)

    print(string.format("Coast MARK built: sea=%.0fm beach=%.0fx%.0f shore[%.2f,%.2f] tower=(%.1f,%.1f,%.1f) deckY=%.2f turrets=%d at z=%.1f",
        Config.SEA_SIZE, 2.0 * Config.BEACH_SPAN_X, Config.SHORE_FOG_END_Z,
        Config.ShoreZMin(), Config.ShoreZMax(),
        Config.TOWER_POS.x, Config.TOWER_POS.y, Config.TOWER_POS.z,
        Config.PLATFORM_Y, #turrets, Config.DEFENSE_Z))

    ---@class CoastHandles
    return {
        root = root,
        sea = sea,
        -- 海面材质（与 Sea 节点同源）：main.lua 每帧滚动它的 UOffset/VOffset 做波纹流动
        seaMaterial = seaMaterial,
        sand = sand,
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

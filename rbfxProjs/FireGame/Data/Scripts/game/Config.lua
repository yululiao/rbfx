-- Scripts/game/Config.lua
--
-- 全游戏的数值、关卡几何与资源路径单点。调难度、调手感、挪布景都只改这一个文件，
-- 其它模块一律从这里读，不写魔数。
--
-- 坐标约定：+z 是内陆（主角与防线在这侧），-z 是外海（敌人从这侧来），y 向上。
-- 水线在 SHORE_Z，沙滩从水线往 +z 铺到 BEACH_END_Z，防线在 DEFENSE_Z，塔在 TOWER_POS。
-- 单位是米（rbfx 约定 1 单位 = 1 米，FBX 导入已按 unit_meters 自动换算）。

---@class GameConfig 只读配置表，不实例化
local Config = {}

----------------------------------------------------------------------
-- 关卡几何
----------------------------------------------------------------------
Config.SEA_Y = -0.20                 -- 海平面高度
Config.SEA_SIZE = 400.0              -- 海面 Plane 边长，够大到雾里消失
Config.SEA_TILE = 24.0               -- 法线噪声平铺密度（400m 海面分 24 格，约 17m/格，舒缓涌浪；
                                     -- 越大波纹越细碎，改动会同步 Water.xml 与 main.lua 滚动公式）
Config.SEA_SCROLL_U = 0.03           -- 海面波纹 UV 滚动速度（UV/秒；米速 = 值×SEA_SIZE，现 12m/s）
Config.SEA_SCROLL_V = 0.02           -- 两个轴取不同速避免波纹整体平移的“传送带”感
-- 海面的 +z 边正好落在水线上（SHORE_Z - SEA_SIZE/2）。写成"往海里偏一点"的 -60 会让
-- 海面一直铺到 z=+140，从塔上回头看内陆是一片蓝，所以这里必须贴住水线算。
Config.SEA_CENTER_Z = -200.5
Config.SHORE_Z = -0.5                -- 水线：沙滩从这里往 +z 铺
Config.SAND_SIZE = 400.0             -- 沙滩从水线往内陆铺满，远端交给雾吃掉
Config.BEACH_END_Z = 28.0            -- 装饰带内边界：棕榈/岩石/点缀只摆在这条线以内
Config.SAND_Y = 0.02                 -- 沙滩面高度，略高于海面以免共面闪烁
Config.DEFENSE_Z = 9.0               -- 防线：海盗走到这里就攻击，掉防线血

-- 主角阵地：四根 Dock_Pole 架一块木平台（不用 House2 当塔身——它 9.38m 高，
-- 站在 4m 的平台上会被整个包在屋子里看不见）。塔后另摆几栋木屋当村落剪影。
Config.TOWER_POS = Vector3(-12.0, 0.0, 15.0)
Config.PLATFORM_Y = 4.0              -- 木平台上表面高度，主角站在这
Config.PLATFORM_SIZE = Vector3(3.2, 0.22, 3.2)
Config.TOWER_POLE_INSET = 1.30       -- 四根腿相对平台中心的内缩量
-- Dock_Pole 本地 y 范围是 [-2.00, 1.01]，只缩 y（等比缩会让 0.36 的桩粗成 1.5m）：
-- 顶面要齐平台略高一点点当栏杆头，故 scaleY = (PLATFORM_Y + 0.15) / 1.01。
Config.TOWER_POLE_SCALE_Y = 4.11

Config.FOG_COLOR = Color(0.65, 0.74, 0.82)   -- 与 Sky.png 的地平线色 (166,188,208) 对齐
Config.FOG_START = 55.0
Config.FOG_END = 210.0
-- 环境光偏蓝（天光）而不是中性灰：沙子和木头受光面暖、背光面冷，画面才有早晚户外感。
-- 场景里原本写的是 0.35 0.35 0.4，这里接管过来，调光不用再去改 .scene。
Config.AMBIENT_COLOR = Color(0.32, 0.37, 0.46)
Config.SUN_DIRECTION = Vector3(0.45, -0.75, 0.28)
Config.SUN_COLOR = Color(1.0, 0.95, 0.86)    -- 略暖，海面才不像阴天
Config.SUN_BRIGHTNESS = 1.0

-- 天空盒：Skybox 组件 + CoreData 的 Box.mdl（已在抖音包里）。Skybox 的
-- ProcessRayQuery 是空实现，所以它永远不会挡住狙击射线，也不受远裁剪面影响。
Config.SKY_MODEL = "Models/Box.mdl"
Config.SKY_SCALE = 200.0

-- 布景件的落位（x, z, 绕 y 的角度, 可选缩放）。数量与位置随意改，Coast.lua 只按表摆。
Config.PALMS = {
    { 6.5, 21.0, 20.0 }, { -7.5, 22.5, -35.0 }, { 13.0, 17.5, 70.0 },
    { -14.0, 19.0, 15.0 }, { 19.5, 11.0, -60.0 }, { -20.0, 12.5, 40.0 },
}
Config.ROCKS = {
    { 3.5, 5.0, 15.0, 1.0 }, { -5.0, 3.0, -40.0, 1.4 }, { 8.0, 7.5, 80.0, 0.9 },
    { -10.5, 6.0, 25.0, 1.2 }, { 15.0, 4.0, -15.0, 1.6 }, { -17.0, 8.0, 55.0, 1.1 },
    { 1.5, 1.5, 100.0, 0.8 }, { -2.5, 2.5, -70.0, 1.3 },
}
Config.CLIFFS = {
    { "Environment_Cliff1", -31.0, 9.0, 25.0, 1.0 },
    { "Environment_Cliff2", 33.0, 5.0, -40.0, 1.0 },
    { "Environment_Cliff4", -35.0, -8.0, 60.0, 1.0 },
    { "Environment_Cliff3", 30.0, 16.0, 15.0, 1.0 },
}
Config.DECOR = {
    { "Environment_Skulls", 11.0, 6.5, -20.0 },
    { "Environment_LargeBones", -18.0, 4.5, 35.0 },
    { "Prop_Anchor", 6.0, 2.0, 110.0 },
    { "Prop_Chest_Gold", -6.5, 7.0, -25.0 },
    { "Prop_Cannon", 4.0, 11.0, 180.0 },
}
-- 村落：摆在塔的两侧而不是正后方。相机的偏航被限制在 ±62 度（朝着海），
-- 放在塔后（z > 19）的屋子根本进不了画面，白摆。
Config.VILLAGE = {
    { "Environment_House2", -16.5, 18.0, -25.0 },   -- 9.38m 的窄高楼，左侧剑影
    { "Environment_House1", 17.5, 20.5, 30.0 },     -- 13.5m 长屋，右侧
    { "Environment_House3", -24.0, 12.0, 60.0 },
}
-- 栈桥：Dock 模块沿 -z 排开，桥面落在 y≈1.0（模型自身 yMin=-2、高 3.0）
Config.DOCK_X = -8.5
Config.DOCK_Z_START = 1.6
Config.DOCK_STEP = 2.62
Config.DOCK_COUNT = 5

-- 可打爆的桶：前 3 个摆死，后 2 个是每条船甲板上带的（随船移动）
-- 栈桥那个的 y 是桥面高度：Dock 模块本地 y[-2.00, 1.01]，节点放在 SEA_Y 上，
-- 所以桥面 = SEA_Y + 1.01 = 0.81。
Config.BARREL_SPOTS = {
    { -8.5, 0.81, -3.6 },            -- 栈桥上
    { 4.5, 0.02, 6.5 },              -- 滩头
    { -3.5, 0.02, 8.5 },             -- 防线附近
}

-- 防线的视觉：一排木桩栅栏，横贯 x，桩高与间距在这里调
Config.PALISADE_COUNT = 15
Config.PALISADE_SPAN_X = 40.0        -- 总宽，桩位在 ±SPAN_X/2 之间等分
Config.PALISADE_POST = Vector3(0.26, 1.70, 0.26)

----------------------------------------------------------------------
-- 相机（第三人称塔后越肩，绕主角球面轨道）
----------------------------------------------------------------------
Config.CAMERA_DISTANCE = 20.0
Config.CAMERA_TARGET_Y = 1.45        -- 相机关注点相对主角脚底的高度
Config.CAMERA_PITCH = 35.0           -- 俯角（度），斜后方朝海面倾斜俯视
Config.CAMERA_LOOK_AHEAD = 6.0       -- 视线中心往海方向偏移，看到更大范围战场
Config.CAMERA_PITCH_MIN = 30.0
Config.CAMERA_PITCH_MAX = 70.0
Config.CAMERA_YAW_LIMIT = 0.0        -- 俯视角固定朝海，不允许水平旋转
Config.CAMERA_FOV = 42.0
Config.CAMERA_FAR_CLIP = 260.0
Config.CAMERA_KICK = 0.3             -- 开火相机微小震动幅度
Config.CAMERA_KICK_DECAY = 12.0
-- 越肩侧移：相机向主角右侧偏这么多，把人物挪到画面左侧，屏幕中心的准星射线
-- 才不会被主角自己挡住（否则从相机到准星的第一命中永远是主角）。设为 0 即回到肩后正中。
Config.CAMERA_SHOULDER_OFFSET = 0.95

----------------------------------------------------------------------
-- 敌人
----------------------------------------------------------------------
Config.LANES = { -13.0, -4.5, 4.5, 13.0 }
-- Ship_Small 的长边在 x 轴（5.14 x 1.99 x 3.60），所以船头默认朝着 ±x，必须偏航 90 度
-- 才能沿 z 航行。R_y(-90) 把 +x 转到 +z（内陆），若进游戏发现船是倒着开的，
-- 把这个数改成 90.0 即可（说明模型船头在 -x）。
Config.BOAT_YAW = -90.0
Config.BOAT_DRAFT = 0.45             -- 吃水：船体底面沉到水面下这么多，看着才像浮着
Config.BOAT_SPAWN_Z = -44.0
Config.BOAT_STOP_Z = -6.5            -- 停船位置，海盗从这里下水
Config.BOAT_SPEED = 4.0
Config.BOAT_BIG_SPEED = 2.9
Config.BOAT_UNLOAD_TIME = 0.55       -- 每吐一个海盗的间隔
Config.BOAT_HP = 3
Config.BOAT_BIG_HP = 8
Config.BOAT_CARRY = 3
Config.BOAT_BIG_CARRY = 5
Config.BOAT_SINK_TIME = 2.2          -- 沉船动画时长，之后移除节点

Config.PIRATE_SPAWN_Z = -4.5         -- 下水点（浅水里，看着像涉水冲滩）
-- Quaternius 角色模型原生朝 -z（Soldier 的散落道具在 z 负侧就是"正前方"），
-- 海盗要朝 +z（防线/塔）行进，所以偏航 180 度。若进游戏发现海盗倒着走，改成 0。
Config.PIRATE_YAW = 180.0
Config.PIRATE_SPEED = 2.3
Config.PIRATE_SPEED_SPREAD = 0.5     -- 每个海盗速度随机 ±，避免排队齐步走
Config.PIRATE_HP = 1
Config.PIRATE_ATTACK_TIME = 1.1      -- 到防线后挥刀的时长，然后结算掉血
Config.PIRATE_DEATH_TIME = 1.6       -- 死亡动画播多久后移除
Config.PIRATE_HITREACT_TIME = 0.28
Config.PIRATE_WADE_DEPTH = 0.35      -- 涉水时把模型抬高一点，别半截埋在水里
Config.MAX_ALIVE_UNITS = 14          -- 同屏单位闸：超了就延后生成（58 骨骼蒙皮不便宜）
Config.FAR_UNIT_DISTANCE = 55.0      -- 超过这个距离的单位关掉不可见时也更新

Config.PIRATE_SKINS = { "Characters_Anne", "Characters_Henry", "Characters_Mako", "Characters_Sharky" }
Config.PIRATE_SKIN_RARE = { "Characters_Skeleton", "Characters_Captain_Barbarossa" }

----------------------------------------------------------------------
-- 武器与命中
----------------------------------------------------------------------
Config.MAG_SIZE = 5
Config.RELOAD_TIME = 1.2
Config.FIRE_COOLDOWN = 0.22          -- 两发之间的最小间隔
Config.FIRE_WINDOW = 0.6             -- 开火后维持射击姿态/上半身对准目标的窗口（秒）：大于冷却
                                     -- 保证连发期间不断档，装弹期间自然回落举枪
Config.RAY_MAX_DISTANCE = 240.0
Config.AIM_SENSITIVITY = 0.155       -- 每画布像素拖拽转多少度
Config.TAP_MOVE_THRESHOLD = 15.0     -- 位移小于这个（画布像素）算点按开火，否则算转视角
Config.MAGNET_RADIUS_PX = 48.0       -- 磁吸瞄准：射线打空时，准星这个半径内最近的敌人算命中
Config.MAGNET_MAX_DISTANCE = 110.0
Config.HEADSHOT_TOP_RATIO = 0.16     -- 命中点落在包围盒顶部这么高的比例内算爆头
Config.RECOIL_SPREAD_DEG = 0.0       -- 预留：连发散布，MVP 不开
Config.ATTACK_RANGE = 45.0           -- 普攻自动吸附：主角周围这个半径内取最近敌人（王者射手式，无需手动对准）

----------------------------------------------------------------------
-- 移动（左摇杆走位，相机相对方向；抖音包无物理，靠矩形边界钳制，不做碰撞）
----------------------------------------------------------------------
Config.MOVE_SPEED = 4.2              -- 米/秒
Config.TURN_SPEED = 8.0              -- 人物转向速度（弧度/秒），朝移动方向转
Config.AIM_MAX_TWIST = 1.75          -- 上半身相对身体朝向的最大扭转（弧度，约 100 度）
Config.AIM_TURN_SPEED = 12.0         -- 上半身扭转的平滑速度（弧度/秒）
Config.PLAYER_SPAWN = Vector3(0.0, Config.SAND_Y, 11.0)   -- 脚底出生点：防线前一点的地面，朝向海
Config.WALK_MIN_X = -18.0
Config.WALK_MAX_X = 18.0
Config.WALK_MIN_Z = 1.5              -- 别走进水里（水线 z=-0.5，留余量）
Config.WALK_MAX_Z = 22.0             -- 别绕到塔后村子里

----------------------------------------------------------------------
-- 载具（玩家专属船：靠近码头按交互键登船，摇杆改驱动船在海域自由探索）
----------------------------------------------------------------------
Config.BOAT_SPAWN = Vector3(-6.0, Config.SEA_Y, 5.0)   -- 船停在码头旁水面上
-- 角色坐船的局部高度（相对船节点）。Ship_Small 实测包围盒 y[-0.01,1.98]（原点在船底、甲板靠顶），
-- 坐太低会被船舷从俯视相机角度看进“舱里”看不见，故抬到甲板附近。
Config.BOAT_SEAT_Y = 1.6
-- 船上双炮（船头左右两侧）：每发在两舷间轮换，子弹从对应炮位出膛
Config.BOAT_GUN_FORWARD = 2.4      -- 沿船头方向前探（米）
Config.BOAT_GUN_LATERAL = 1.1      -- 沿左右舷横向偏移（米）
Config.BOAT_GUN_Y = 2.0            -- 炮口高度（相对船节点）
-- 注意：不要复用 Config.BOAT_SPEED（那是敌人船速，见 Enemy.lua），玩家船速单独命名
Config.PLAYER_BOAT_SPEED = 8.0       -- 米/秒，比走路快
Config.PLAYER_BOAT_TURN = 3.0        -- 船转向速率（弧度/秒），比人物慢，做出惯性
Config.BOAT_INTERACT_DIST = 5.0      -- 角色与船水平距离小于此值可登船
-- 海域可行走矩形（驾船时）：比沙滩大得多，但不能开上沙滩
Config.SEA_MIN_X = -60.0
Config.SEA_MAX_X = 60.0
Config.SEA_MIN_Z = -80.0             -- 往海方向能开很远
Config.SEA_MAX_Z = Config.SHORE_Z + 3.0   -- 靠岸余量，可下船
-- 下船条件：船头 z 大于此值（靠岸）才允许弃船
Config.LANDING_Z = Config.SHORE_Z + 3.0

----------------------------------------------------------------------
-- 计分、连击、防线
----------------------------------------------------------------------
Config.SCORE_BOAT = 100
Config.SCORE_BOAT_BIG = 260
Config.SCORE_PIRATE = 50
Config.SCORE_BARREL = 25
Config.HEADSHOT_MULTIPLIER = 2
Config.COMBO_WINDOW = 2.0            -- 两次击杀超过这个间隔，combo 断
Config.COMBO_BONUS_STEP = 10         -- 每级 combo 额外加的分
Config.COMBO_VOICE_AT = { 3, 5, 8 }  -- 达到这些连击数播 combo / multi_kill 旁白
Config.DEFENSE_HP = 100
Config.LEAK_DAMAGE = 10              -- 一个海盗摸到防线扣多少

-- 爆桶：无物理模块（抖音包禁用 URHO3D_PHYSICS），所以用距离遍历而不是球形查询
Config.BARREL_HP = 1
Config.BARREL_RADIUS = 4.6
Config.BARREL_DAMAGE = 2
Config.BARREL_CHAIN_DELAY = 0.12     -- 连锁引爆的递延，让爆炸看起来是一串而不是一帧

----------------------------------------------------------------------
-- 波次：每波一张 {t=秒, kind=船型, lane=航道下标} 的生成时间表
-- kind: "boat" 小艇（3 人）/ "bigboat" 大船（5 人）
----------------------------------------------------------------------
Config.WAVE_INTERMISSION = 5.0
Config.WAVES = {
    {
        voice = "round_1",
        spawns = {
            -- 先头 1
            { t = 1.0, kind = "boat", lane = 2 },
            -- 第二批 3
            { t = 3.0, kind = "boat", lane = 1 },
            { t = 3.0, kind = "boat", lane = 3 },
            { t = 3.0, kind = "boat", lane = 4 },
            -- 第三批 3
            { t = 5.5, kind = "boat", lane = 1 },
            { t = 5.5, kind = "boat", lane = 2 },
            { t = 5.5, kind = "boat", lane = 3 },
            -- 第四批 3
            { t = 8.0, kind = "boat", lane = 2 },
            { t = 8.0, kind = "boat", lane = 3 },
            { t = 8.0, kind = "boat", lane = 4 },
            -- 第五批 3
            { t = 10.5, kind = "boat", lane = 1 },
            { t = 10.5, kind = "boat", lane = 2 },
            { t = 10.5, kind = "boat", lane = 4 },
            -- 第六批 2
            { t = 13.0, kind = "boat", lane = 1 },
            { t = 13.0, kind = "boat", lane = 3 },
        },
    },
}

----------------------------------------------------------------------
-- 资源路径
----------------------------------------------------------------------
local PIRATE_DIR = "Models/Quaternius_pirate-kit/Fbx/"
local PLAYER_DIR = "Models/Quaternius_Player/Fbx/"

Config.MAT_PIRATE = "Materials/PirateAtlas.xml"
Config.MAT_VERTEX_COLOR = "Materials/VertexColor.xml"
Config.MAT_WHITE = "Materials/DefaultWhite.xml"
Config.MAT_SKY = "Materials/Sky.xml"
Config.MAT_HIDDEN = "Materials/Hidden.xml"
Config.MAT_WATER = "Materials/Water.xml"
Config.PLANE_MODEL = "Models/Plane.mdl"
Config.BOX_MODEL = "Models/Box.mdl"

-- Character_Soldier.mdl 有 67 个 geometry：主体是带顶点色的那批（躯干/四肢），装备件带 TEXCOORD。
-- 全显示、不做隐藏/裁剪（占位资源，体积优化不在本次范围）；逐 geometry 的材质分派见 Player.lua。

--- pirate-kit 的模型资源名。这个包没有 prefab（导入时只产了 mdl/ani），一律直接取 Model。
---@param name string 不含扩展名的资产名，如 "Ship_Small"
---@return string
function Config.PirateModel(name)
    return PIRATE_DIR .. name .. ".fbx.d/Models/" .. name .. ".mdl"
end

--- pirate-kit 角色的动画资源名。栈名见导入产物，如 Walk / Run / Sword / HitReact / Death / Idle。
---@param name string 资产名，如 "Characters_Anne"
---@param stack string 动画栈名，如 "Run"
---@return string
function Config.PirateAnim(name, stack)
    return PIRATE_DIR .. name .. ".fbx.d/Models/" .. name .. "_CharacterArmature" .. stack .. ".ani"
end

---@return string
function Config.PlayerModel()
    return PLAYER_DIR .. "Character_Soldier.fbx.d/Models/Character_Soldier.mdl"
end

--- 主角动画栈：Idle / Idle_Shoot / Run / Run_Gun / Duck / HitReact / Death / Jump 系列（没有 Walk）
---@param stack string
---@return string
function Config.PlayerAnim(stack)
    return PLAYER_DIR .. "Character_Soldier.fbx.d/Models/Character_Soldier_CharacterArmature" .. stack .. ".ani"
end

----------------------------------------------------------------------
-- 音效
----------------------------------------------------------------------
local IMPACT = "Sound/kenney_impact-sounds/Audio/"
local VOICE = "Sound/kenney_voiceover-pack-fighter/Audio/"

--- Kenney 的音效按 _000.._NNN 编号成组，批量列出变体路径。
---@param prefix string
---@param count integer
---@return string[]
local function Variants(prefix, count)
    local list = {}
    for i = 0, count - 1 do
        list[#list + 1] = IMPACT .. prefix .. string.format("%03d", i) .. ".ogg"
    end
    return list
end

--- 事件名 -> 变体列表。播放时随机取一个并微调音高，避免机关枪式的重复感。
Config.Sfx = {
    shot = { "Sound/gun_shot/shooting-single.ogg" },
    shotTail = Variants("impactMining_", 5),
    hitFlesh = Variants("impactPunch_medium_", 5),
    killFlesh = Variants("impactPunch_heavy_", 5),
    hitWood = Variants("impactWood_medium_", 5),
    breakWood = Variants("impactWood_heavy_", 5),
    hitMetal = Variants("impactMetal_medium_", 5),
    breakMetal = Variants("impactMetal_heavy_", 5),
    splash = Variants("impactSoft_medium_", 5),
    reload = Variants("impactTin_medium_", 5),
    dryFire = { IMPACT .. "impactGlass_light_000.ogg" },
    explosion = Variants("impactBell_heavy_", 5),
    footstep = Variants("footstep_grass_", 5),
    uiClick = Variants("impactGeneric_light_", 5),
}

--- 旁白：单文件，不做变体。名字对应 kenney_voiceover-pack-fighter 里的文件名。
Config.Voice = {
    round = { "round_1", "round_2", "round_3" },
    combo = "combo",
    multiKill = "multi_kill",
    win = "you_win",
    lose = "you_lose",
    flawless = "flawless_victory",
    prepare = "prepare_yourself",
    fight = "fight",
    gameOver = "game_over",
}

---@param name string Config.Voice 里的值
---@return string
function Config.VoicePath(name)
    return VOICE .. name .. ".ogg"
end

----------------------------------------------------------------------
-- 特效：一律用工程自带的 Particle/*.xml（配套材质 Materials/ParticleSoft|ParticleAdd.xml
-- 与贴图 Textures/Dot.png）。
--
-- 不要用引擎示例里的 Particle/Dust.xml、Burst.xml、SnowExplosion*.xml：它们引用
-- Materials/Particle.xml + Textures/Flare.dds/Smoke.dds，全都在 msvc/bin/Data 下，
-- 打包时只带 CoreData 与工程 Data，于是编辑器里看得见、真机上静默消失。
----------------------------------------------------------------------
Config.Fx = {
    hitFlesh = "Particle/HitFlesh.xml",
    hitWood = "Particle/HitWood.xml",
    hitMetal = "Particle/HitMetal.xml",
    splash = "Particle/Splash.xml",
    explosion = "Particle/Explosion.xml",
    smoke = "Particle/Smoke.xml",
    sink = "Particle/Splash.xml",
}

----------------------------------------------------------------------
-- 纯色（运行时 Clone DefaultWhite 后染色，省掉一堆 XML 材质文件）
----------------------------------------------------------------------
Config.Tint = {
    sand = Color(0.86, 0.78, 0.56),
    wood = Color(0.45, 0.31, 0.19),
    woodDark = Color(0.30, 0.20, 0.12),
    tower = Color(0.52, 0.36, 0.22),
    defense = Color(0.75, 0.22, 0.18),
    water = Color(0.10, 0.28, 0.40),
    playerBody = Color(0.36, 0.42, 0.30),
    bullet = Color(1.0, 0.12, 0.08),
}

-- 每种纯色的粗糙度（PBR）。DefaultWhite 的默认值 0.3 对沙子和木头太亮；
-- 海面反过来要低粗糙度，否则太阳打上去没有高光，看上去就是一块蓝地板。
Config.Roughness = {
    sand = 0.95,
    wood = 0.80,
    woodDark = 0.80,
    tower = 0.80,
    defense = 0.85,
    water = 0.10,
    bullet = 0.4,
    playerBody = 0.70,
}

return Config

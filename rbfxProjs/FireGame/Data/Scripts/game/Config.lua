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
-- 相机（俯视角固定朝海，吊在主角正后上方；抖音小游戏刻意不需要玩家手动转视角）
----------------------------------------------------------------------
Config.CAMERA_DISTANCE = 20.0
Config.CAMERA_TARGET_Y = 1.45        -- 相机关注点相对主角脚底的高度
Config.CAMERA_PITCH = 35.0           -- 俯角（度），斜后方朝海面倾斜俯视
Config.CAMERA_LOOK_AHEAD = 6.0       -- 视线中心往海方向偏移，看到更大范围战场
Config.CAMERA_FOV = 42.0
Config.CAMERA_FAR_CLIP = 260.0
Config.CAMERA_KICK = 0.3             -- 开火相机微小震动幅度
Config.CAMERA_KICK_DECAY = 12.0

----------------------------------------------------------------------
-- 敌人
----------------------------------------------------------------------
Config.LANES = { -13.0, -4.5, 4.5, 13.0 }
-- Ship_Small 的长边在 x 轴（5.14 x 1.99 x 3.60），所以船头默认朝着 ±x，必须偏航 90 度
-- 才能沿 z 航行。R_y(-90) 把 +x 转到 +z（内陆），若进游戏发现船是倒着开的，
-- 把这个数改成 90.0 即可（说明模型船头在 -x）。
Config.BOAT_YAW = -90.0
Config.BOAT_DRAFT = 0.45             -- 吃水：船体底面沉到水面下这么多，看着才像浮着
-- 船的 combat 瞄点抬高度（相对船 node 原点=船底）。船 node 在水面下，需抬到甲板附近：
-- Ship_Small 包围盒 y[-0.01,1.98]，1.2 落在上层建筑附近，且保证命中点高于水面（SEA_Y）。
Config.BOAT_HIT_HEIGHT = 1.2
Config.BOAT_SPAWN_Z = -44.0
Config.BOAT_STOP_Z = -6.5            -- 停船位置，海盗从这里下水
-- 船速/血：维持基线（不靠堆血量制造压力，已定案教训），只用略快的航速减少在射程里空转的时间。
Config.BOAT_SPEED = 4.2
Config.BOAT_BIG_SPEED = 3.0
Config.BOAT_UNLOAD_TIME = 0.55       -- 每吐一个海盗的间隔
Config.BOAT_HP = 3
Config.BOAT_BIG_HP = 8
Config.BOAT_CARRY = 3
Config.BOAT_BIG_CARRY = 5
Config.BOAT_SINK_TIME = 2.2          -- 沉船动画时长，之后移除节点

----------------------------------------------------------------------
-- 大船炮台（仅 bigboat）：靠岸卸完兵后不立即离开，而是滞留往沙滩上抛炮弹。
-- 落点在主角可走的矩形内随机（“方向随机”=覆盖式炮击，不是瞄准主角），
-- 抛物线慢速 + 地面预警盘，给玩家看见并走位躲的时间；炸到主角掉大血。
----------------------------------------------------------------------
Config.BOAT_CANNON_FIRST = 1.0           -- 停船后首发射迟（秒）
Config.BOAT_CANNON_INTERVAL = 3.0        -- 两发间隔（秒）
Config.BOAT_CANNON_INTERVAL_SPREAD = 0.8 -- 间隔随机 ±
Config.BOAT_CANNON_LINGER = 9.0          -- 卸完兵后大船滞留炮击的时长（秒），到点开走
Config.CANNON_BALL_SCALE = 0.55          -- 炮弹实体大小（复用 Sphere.mdl）
Config.CANNON_FLIGHT_TIME = 2.2          -- 炮弹飞行时长（秒）：慢，留躲避时间
Config.CANNON_LOB_HEIGHT = 9.0           -- 抛物线拱高（米，峰在轨迹中点上方）
Config.CANNON_SPLASH_RADIUS = 3.2        -- 落点杀伤半径（米）
Config.CANNON_PLAYER_DAMAGE = 30         -- 炸到主角的大血量（对比突袭型 melee 10）

Config.PIRATE_SPAWN_Z = -4.5         -- 下水点（浅水里，看着像涉水冲滩）
-- Quaternius 角色模型原生朝 -z（Soldier 的散落道具在 z 负侧就是"正前方"），
-- 海盗要朝 +z（防线/塔）行进，所以偏航 180 度。若进游戏发现海盗倒着走，改成 0。
Config.PIRATE_YAW = 180.0
Config.PIRATE_SPEED = 2.6
Config.PIRATE_SPEED_SPREAD = 0.5     -- 每个海盗速度随机 ±，避免排队齐步走
Config.PIRATE_HP = 1                 -- 一发死：保住割草爽感的根。压迫靠数量/来速，不靠肉。
-- 海盗 combat 瞄点抬高度（相对 node 原点=脚底）。角色模型原点在脚底，抬到躯干中段
-- 才能让子弹打身子、命中特效不喷在脚底。Quaternius 角色约 1.5~1.8 高，0.9 落在胸口附近。
Config.PIRATE_HIT_HEIGHT = 0.9
Config.PIRATE_ATTACK_TIME = 1.1      -- 到防线后挥刀的时长，然后结算掉血
Config.PIRATE_DEATH_TIME = 1.6       -- 死亡动画播多久后移除
Config.PIRATE_HITREACT_TIME = 0.28
Config.PIRATE_WADE_DEPTH = 0.35      -- 涉水时把模型抬高一点，别半截埋在水里
-- 突袭型海盗：不按航道直冲防线，而是转追主角、近身后反复砍主角（走位压力的来源）。
-- 生成时按概率标记，用稀有皮（骷髅/船长）做视觉区分；hp 更肉一点避免一发带走。
Config.PIRATE_RAIDER_CHANCE = 0.6    -- 每个海盗是突袭型（追主角）的概率。分流阀：调高→追你的多、冲海岸的自动少（总数不变）
Config.PIRATE_RAIDER_HP = 3          -- 突袭型血量（普通型走 PIRATE_HP=1）：追身的略肉一点避免一发带走
Config.PIRATE_MELEE_RANGE = 1.7      -- 与主角水平距离小于此值即停下挥刀
Config.PIRATE_PLAYER_DAMAGE = 10     -- 突袭型一刀砍主角多少血
Config.MAX_ALIVE_UNITS = 16          -- 同屏单位闸：超了就延后生成。比原 14 略高制造潮水感（真机卡就回调）
Config.FAR_UNIT_DISTANCE = 55.0      -- 超过这个距离的单位关掉不可见时也更新

Config.PIRATE_SKINS = { "Characters_Anne", "Characters_Henry", "Characters_Mako", "Characters_Sharky" }
Config.PIRATE_SKIN_RARE = { "Characters_Skeleton", "Characters_Captain_Barbarossa" }

----------------------------------------------------------------------
-- 武器（全自动开火：射程内取最近敌人自动射击，玩家只管走位——抖音小游戏的刻意简化）
-- 注：没有手动射线/准星/磁吸/爆头——这些都是早期“手动狙击”设想留下的残留配置，已随之清除。
----------------------------------------------------------------------
Config.MAG_SIZE = 5
Config.RELOAD_TIME = 1.2
Config.FIRE_COOLDOWN = 0.22          -- 两发之间的最小间隔
Config.FIRE_WINDOW = 0.6             -- 开火后维持射击姿态/上半身对准目标的窗口（秒）：大于冷却
                                     -- 保证连发期间不断档，装弹期间自然回落举枪
Config.ATTACK_RANGE = 45.0           -- 自动吸附半径：主角周围这个半径内取最近敌人开火（无需手动对准）

----------------------------------------------------------------------
-- 扇形散射 + 局内成长（割草爽感的两个相乘因子：命中广度 × 力量可见增长）。
-- 一次扳机扣 1 发弹药（弹匣是“扳机数”不是“弹丸数”），沿 SCATTER_SPREAD_DEG 扇形
-- 喷出 pellets 颗；子弹改为沿线扫掠命中（撞到航道上第一个敌人即结算并消耗），
-- 所以散射 = 一次清一条航道若干人，多点同时炸。pellets/damage 由 game/Grow.lua
-- 按 GROWTH 曲线随击杀经验爬升。
----------------------------------------------------------------------
Config.SCATTER_SPREAD_DEG = 26.0     -- 扇形总张角（度，多弹沿此弧均匀铺开）
Config.BULLET_HIT_RADIUS = 1.3       -- 子弹扫掠命中半径（米）
Config.GROW_XP = { pirate = 1, boat = 6, bigboat = 20, barrel = 2 }  -- 各类击杀喂的经验
-- 成长曲线：每项 { xp=达到该级所需累计经验, pellets=扇形弹丸数, damage=单发伤害 }。
-- 首项是开局形态（xp 恒 0）；到达某级即采用该项，之后只增不减。数值按三波全清的
-- 经验总量（约 380）标定，通关刚好爬到 5 发附近，不会溢出到封顶。
-- pellets 永远取奇数（1/3/5）：扇形按 spread*(i-0.5)/pellets 均匀铺开，只有奇数发才
-- 有一颗处于 a=0° 的正中弹直射锁定目标。偶数发（如 2）正中是空的，两颗对称夹住
-- 小船（船体仅 ~1.3m 宽）会刚好从缝里漏掉——故不用 2/4。
Config.GROWTH = {
    { xp = 0,   pellets = 1, damage = 1 },
    { xp = 60,  pellets = 3, damage = 1 },
    { xp = 140, pellets = 3, damage = 2 },
    { xp = 240, pellets = 5, damage = 2 },
    { xp = 360, pellets = 5, damage = 3 },
}

----------------------------------------------------------------------
-- 打击感三件套（hit-stop / 震屏 / 飘字）：统一由 game/Juice.lua 驱动，
-- 触发点集中在 main.lua 的 OnScore（玩家子弹与爆桶连锁都汇入那里）。
----------------------------------------------------------------------
Config.HITSTOP_SCALE = 0.05       -- 击杀定格期间的世界时间流速（5%：近停不停，
                                   -- 全停会让连续击杀变成卡顿感）
Config.SHAKE_DECAY = 2.8          -- 震屏强度（trauma）每秒线性衰减量
Config.SHAKE_AMPLITUDE = 0.55     -- 相机位置抖动幅度（米，trauma=1 峰值时）
Config.SHAKE_ROLL = 2.5           -- 相机横滚抖动幅度（度，trauma=1 峰值时；
                                   -- 俯视角下小滚角让“海面倾斜”，比纯平移震更带感）
Config.POPSCORE_LIFE = 0.9        -- 飘字寿命（秒）
Config.POPSCORE_RISE = 90.0       -- 飘字上浮速度（画布像素/秒，随寿命减速到 30%）

----------------------------------------------------------------------
-- 自定义语音（麦克风录音→宿主侧存 mp3→变声回放）：全链路在 MinigameSDK/
-- minigame_sdk.js（引擎只发请求收事件，不碰音频解码）。音色 playbackRate 必须
-- 取宿主支持的档位（0.5/0.8/1.0/1.25/1.5/2.0），非档位值会被平台钳到最近档。
----------------------------------------------------------------------
Config.VOICE_MAX_SEC = 3          -- 单句录音上限（秒），到时自动停
Config.VOICE_FILTERS = {
    { name = "海盗低音", rate = 0.8 },
    { name = "原声",     rate = 1.0 },
    { name = "哈士奇",   rate = 1.5 },
}
Config.VOICE_DEFAULT_FILTER = 2   -- 默认原声（VOICES_FILTERS 下标）
-- 槽位：宿主侧存为 rbfx_voice_<slot>.mp3，Lua 与 JS 共享的唯一契约
Config.VOICE_SLOT_LOSE = "lose"        -- 防线失守时播
Config.VOICE_SLOT_KILLSTREAK = "killstreak"  -- 连杀里程碑播（阈值同 COMBO_VOICE_AT）

----------------------------------------------------------------------
-- 移动（左摇杆走位，相机相对方向；抖音包无物理，靠矩形边界钳制，不做碰撞）
----------------------------------------------------------------------
Config.MOVE_SPEED = 4.2              -- 米/秒
Config.TURN_SPEED = 8.0              -- 人物转向速度（弧度/秒），朝移动方向转
Config.AIM_MAX_TWIST = 1.75          -- 上半身相对身体朝向的最大扭转（弧度，约 100 度）
Config.AIM_TURN_SPEED = 12.0         -- 上半身扭转的平滑速度（弧度/秒）

-- 右手扶枪（程序化 IK）：Characters_Lis 的 _Gun 动画（Idle_Gun/Run_Gun）里右臂是自然下垂的
-- （这个姿态烘在 FBX 资片里，不是接线引入的）。这里在自定义动画时点（ApplyAim→GunHold）把
-- 右臂两节骨骼 UpperArm.R/LowerArm.R 解析地摆到步枪握把（左手中指骨 Middle1.L，步枪的挂点）。
-- 调参全靠目视：手不到位→加大 strength；肘朝外/穿模→改 pole 侧或调 gripOffset；拖动→降 ramp。
Config.GUN_HOLD = {
    enable = true,
    strength = 0.9,        -- 0=完全跟随动画(手垂着)，1=手精确贴到枪上
    ramp = 12.0,           -- 进/出枪态时权重淡入淡出速度（每秒）
    minBend = 0.10,        -- 肘部最小余量(占满臂长比例)，防完全伸直奇点
    gripOffset = Vector3(0.0, 0.0, 0.0),  -- 相对 Middle1.L 的世界系握把目标微调
}
Config.PLAYER_SPAWN = Vector3(0.0, Config.SAND_Y, 11.0)   -- 脚底出生点：防线前一点的地面，朝向海
-- 主角模型整体放大倍数（对 Player 节点 SetScale）。模型/骨骼都在脚底原点之下，以脚为锚
-- 往上长，不影响出生点与走位；GunHold 的 IK 读骨骼世界坐标（同比缩放）故仍自洽。
Config.PLAYER_MODEL_SCALE = 1.5
Config.WALK_MIN_X = -18.0
Config.WALK_MAX_X = 18.0
Config.WALK_MIN_Z = 1.5              -- 别走进水里（水线 z=-0.5，留余量）
Config.WALK_MAX_Z = 22.0             -- 别绕到塔后村子里

-- 宠物狗（见 game/Dog.lua）：友方伴随单位，独立于 enemy.units（玩家子弹扫不到它、波次
-- 清怪不含它、爆桶连锁不误伤它）。平时在主角身边溜达，视野内有海盗就冲上去咬（静默击杀，
-- 不进 onScore）；海盗不主动攻击狗；只有大船炮弹能把狗炸飞上天翻滚，狗不死（无血量）。
Config.DOG = {
    spawnOffset = Vector3(1.6, 0.0, 1.2),  -- 出生/重开时相对主角脚底的偏移
    speed = 4.0,           -- 追击/回家的移动速度（米/秒，须快过海盗 2.3~2.6 才追得上）
    wanderSpeed = 1.7,     -- 身边溜达的速度
    followRadius = 6.0,    -- 离主角超过这个距离就先跑回身边
    wanderRadius = 4.0,    -- 在主角周围这么大范围内随机乱逛
    aggroRadius = 11.0,    -- 看到这个半径内的存活海盗就去咬
    biteRange = 1.1,       -- 与海盗水平距离小于此值即下口
    biteDamage = 2,        -- 每口伤害（普通海盗 HP=1 一口秒，突袭型 HP=3 需两口）
    biteInterval = 0.7,    -- 两次下口的间隔（秒）
    biteAnim = "Attack",   -- 咬击动画栈名
    barkCooldown = 1.1,     -- 追击期间两次吠叫的最小间隔（秒）
    idleBarkMin = 5.0,      -- 闲逛时随机吠叫的最小间隔（秒）
    idleBarkMax = 11.0,     -- 闲逛时随机吠叫的最大间隔（秒）
    restDuration = 10.0,    -- 一波清完后狗原地趴卧休息的时长（秒），期间播喘气、不移动不索敌
    restArriveRadius = 2.5, -- 波次清完“跑回主角身边”后、判定已到位可休息的水平半径（米）
    -- 被炮弹炸飞：纯 Lua 抛物线积分 + 多轴翻滚（抖音包无物理，绝不做刚体）
    launch = {
        upSpeed = 14.0,      -- 初始上升速度（米/秒）
        gravity = 20.0,      -- 回落加速度（米/秒²）：比真重力大，飞得起落得快、节奏紧凑
        spinSpeed = 720.0,   -- 翻滚角速度（度/秒），绕斜轴拧着翻
        forwardKick = 3.2,   -- 沿“爆炸→狗”水平方向的弹出初速（米/秒），带阻尼衰减
        recoverTime = 0.6,   -- 落地后滚回四脚着地的起身时长（秒）
    },
}

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
Config.SEA_MAX_Z = Config.SHORE_Z + 3.0   -- 靠岸余量（下船不限制位置，随时可下）

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
-- 主角生命：突袭型海盗砍的是这条血。掉到 0 = 主角阵亡（与防线失守同为败局）。
Config.PLAYER_HP = 100
Config.PLAYER_INVULN = 0.6           -- 受击后无敌帧（秒），防贴身多刀在同一帧秒融

-- 爆桶：无物理模块（抖音包禁用 URHO3D_PHYSICS），所以用距离遍历而不是球形查询
Config.BARREL_HP = 1
Config.BARREL_RADIUS = 4.6
Config.BARREL_DAMAGE = 2
Config.BARREL_CHAIN_DELAY = 0.12     -- 连锁引爆的递延，让爆炸看起来是一串而不是一帧

----------------------------------------------------------------------
-- 波次：每波一张 {t=秒, kind=船型, lane=航道下标} 的生成时间表
-- kind: "boat" 小艇（3 人）/ "bigboat" 大船（5 人）
-- lane: Config.LANES 的下标（1..4）。同一 t 用不同 lane 错开，直线推进无碰撞，
--       同航道同时刻会互相穿模。
----------------------------------------------------------------------
Config.WAVE_INTERMISSION = 5.0
Config.WAVES = {
    {
        voice = "round_1",
        spawns = {
            -- 先头 2
            { t = 1.0, kind = "boat", lane = 2 },
            { t = 1.0, kind = "boat", lane = 3 },
            -- 第一批三航道
            { t = 3.0, kind = "boat", lane = 1 },
            { t = 3.0, kind = "boat", lane = 3 },
            { t = 3.0, kind = "boat", lane = 4 },
            -- 四航道齐射（一次扳机只能扫一条道，其余趁隙逼近）
            { t = 5.5, kind = "boat", lane = 1 },
            { t = 5.5, kind = "boat", lane = 2 },
            { t = 5.5, kind = "boat", lane = 3 },
            { t = 5.5, kind = "boat", lane = 4 },
            -- 收尾
            { t = 8.0, kind = "boat", lane = 2 },
            { t = 8.0, kind = "boat", lane = 4 },
            { t = 10.0, kind = "boat", lane = 1 },
            { t = 10.0, kind = "boat", lane = 3 },
        },
    },
    -- 第二波：首次投入大船（吐 5 人 / 慢速），小艇护航。双大船同压逼玩家分兵。
    {
        voice = "round_2",
        spawns = {
            -- 开场双大船压中、右两路
            { t = 1.0, kind = "bigboat", lane = 2 },
            { t = 1.0, kind = "bigboat", lane = 3 },
            -- 第一梯队小艇铺两侧
            { t = 3.5, kind = "boat", lane = 1 },
            { t = 3.5, kind = "boat", lane = 4 },
            -- 四航道小艇齐射
            { t = 6.5, kind = "boat", lane = 1 },
            { t = 6.5, kind = "boat", lane = 2 },
            { t = 6.5, kind = "boat", lane = 3 },
            { t = 6.5, kind = "boat", lane = 4 },
            -- 第二梯队
            { t = 9.5, kind = "boat", lane = 2 },
            { t = 9.5, kind = "boat", lane = 4 },
            -- 收尾零敲
            { t = 12.0, kind = "boat", lane = 1 },
            { t = 12.0, kind = "boat", lane = 3 },
        },
    },
    -- 第三波（终局高潮）：三艘大船 + 一次四航道齐射。密度比前两波高，但不再堆到打不过。
    {
        voice = "round_3",
        spawns = {
            -- 双大船开局（中、右）
            { t = 1.0, kind = "bigboat", lane = 2 },
            { t = 1.0, kind = "bigboat", lane = 3 },
            -- 四航道小艇齐射
            { t = 4.0, kind = "boat", lane = 1 },
            { t = 4.0, kind = "boat", lane = 2 },
            { t = 4.0, kind = "boat", lane = 3 },
            { t = 4.0, kind = "boat", lane = 4 },
            -- 第三艘大船压左路
            { t = 7.0, kind = "bigboat", lane = 1 },
            -- 中段补杂兵
            { t = 9.5, kind = "boat", lane = 2 },
            { t = 9.5, kind = "boat", lane = 4 },
            { t = 12.0, kind = "boat", lane = 1 },
            { t = 12.0, kind = "boat", lane = 3 },
            -- 末段
            { t = 14.5, kind = "boat", lane = 2 },
            { t = 14.5, kind = "boat", lane = 3 },
        },
    },
}

----------------------------------------------------------------------
-- 资源路径
----------------------------------------------------------------------
local PIRATE_DIR = "Models/Quaternius_pirate-kit/Fbx/"
local PLAYER_DIR = "Models/Characters-001/Fbx/"
local PLAYER_MODEL = "Characters_Lis"
local DOG_MODEL = "Characters_Dog"

Config.MAT_PIRATE = "Materials/PirateAtlas.xml"
Config.MAT_CHARACTER = "Materials/CharacterAtlas.xml"
Config.MAT_VERTEX_COLOR = "Materials/VertexColor.xml"
Config.MAT_WHITE = "Materials/DefaultWhite.xml"
Config.MAT_SKY = "Materials/Sky.xml"
Config.MAT_HIDDEN = "Materials/Hidden.xml"
Config.MAT_WATER = "Materials/Water.xml"
Config.PLANE_MODEL = "Models/Plane.mdl"
Config.BOX_MODEL = "Models/Box.mdl"

-- Characters_Lis.mdl 有 12 个 geometry：主体蒙皮 Lis + 左肩甲 Shoulder + 一堆挂在左手 Middle1.L 的
-- 手持道具（Guitar/Axe/Spear/Pistol/Rifle/Shotgun/SMG/WoodenBat_Barbed/WoodenBat_Saw/Knife）。全身
-- 共用一张 Zombie_Atlas 图集（导入 -nm -nt 无材质，材质手写见 Materials/CharacterAtlas.xml）；只留
-- Rifle、其余道具按 geometry 名隐藏，逐段材质分派见 Player.lua。

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
    return PLAYER_DIR .. PLAYER_MODEL .. ".fbx.d/Models/" .. PLAYER_MODEL .. ".mdl"
end

--- 主角动画栈（Characters_Lis）：Idle / Idle_Gun / Run / Run_Gun / Walk / Walk_Gun / HitReact /
--- Death / Duck / Jump 系列 / Punch / Slash / Stab 等（没有 Idle_Shoot，举枪待机用 Idle_Gun）
---@param stack string
---@return string
function Config.PlayerAnim(stack)
    return PLAYER_DIR .. PLAYER_MODEL .. ".fbx.d/Models/" .. PLAYER_MODEL .. "_CharacterArmature" .. stack .. ".ani"
end

--- 宠物狗模型（Characters-001/Characters_Dog，与主角同目录、共用 Zombie_Atlas 图集，
--- 材质沿用 Materials.Character()）。狗是单一体网格，无手持道具，不需按名隐藏。
---@return string
function Config.DogModel()
    return PLAYER_DIR .. DOG_MODEL .. ".fbx.d/Models/" .. DOG_MODEL .. ".mdl"
end

--- 狗动画栈（骨架名是 AnimalArmature，非主角的 CharacterArmature）：
--- Idle / Idle_2 / Walk / Run / Run_Jump / Attack / Death / Idle_HitReact_Left|Right。
---@param stack string
---@return string
function Config.DogAnim(stack)
    return PLAYER_DIR .. DOG_MODEL .. ".fbx.d/Models/" .. DOG_MODEL .. "_AnimalArmature" .. stack .. ".ani"
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
    cannonShot = { "Sound/gun_shot/powerful-cannon-shot.ogg" },   -- 大船炮台开炮专用
    cannonBlast = { "Sound/gun_shot/cannon-blast.ogg" },          -- 大船炮弹落地爆炸专用
    dogBark = { "Sound/gun_shot/dog-bark.ogg" },                  -- 宠物狗吠（追击时反复叫、被弹炸飞时叫一声）
    dogPant = { "Sound/gun_shot/dog-panting.ogg" },               -- 宠物狗喘气（一波清完原地休息时播）
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
    cannon = Color(0.14, 0.14, 0.16),       -- 炮弹：深铁色
    cannonWarn = Color(1.0, 0.42, 0.10),    -- 落点预警盘：醒目橙
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
    cannon = 0.5,
    cannonWarn = 0.9,
    playerBody = 0.70,
}

return Config

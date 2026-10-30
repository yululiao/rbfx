-- Scripts/game/Config.lua
--
-- 全游戏的数值、关卡几何与资源路径单点。调难度、调手感、挪布景都只改这一个文件，
-- 其它模块一律从这里读，不写魔数。
--
-- 坐标约定：+z 是内陆（主角与防线在这侧），-z 是外海（敌人从这侧来），y 向上。
-- 水线不是一个 z，而是 Config.ShoreZ(x) 一条曲线（见下方 SHORE 段）：它是涉水/靠岸/走位
-- 判据与海面铺位的真源，防线在 DEFENSE_Z，塔在 TOWER_POS。（沙滩网格已删，改由场景手摆 prefab。）
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
----------------------------------------------------------------------
-- 海岸线：一个 z 标量 → Config.ShoreZ(x) 一条曲线（海岸线的唯一真源）
--
-- 之前水线是常量 SHORE_Z=-0.5，海与沙是两块轴对齐的大 Plane 在 z=-0.5 对接，
-- 所以水陆交界在屏幕上是一条字面的直线——一眼就是“程序生成的占位”。岸线本来就不直，
-- 把它做成曲线不是为了好看而好看，是因为它同时是 5 个玩法判据的输入（涉水 y、狗活动
-- 下界、船靠岸线、玩家走位下界、炮塔警戒线）：几何与玩法同源才不会出现“看着已上岸、
-- 判据还在水里”的那类脱节。
--
-- 唯一真源纪律：控制点只有 SHORE_PTS 这一张表，上面那 5 个消费点与 Coast 的沙滩网格
-- 全从 Config.ShoreZ(x) 读，任何地方都不许再抄一个“-0.5”。
-- 控制点按 x 递增；区间外按端值平推（钳制而不是外推斜率，否则沙滩边界会在远端翘飞）。
-- 岬（凸向海）与湾（凹向陆）交替，峰谷差约 4m——幅度调大到这个量级才能在俯视镜头下
-- 真正看出“弯曲”，之前 ±0.7m 的单一缓拱只会被当成一条斜直线。幅度受两条边界约束：
-- 上界 ShoreZMax 须远小于 DEFENSE_Z（走位下限 ShoreZ(x)+WALK_SHORE_MARGIN 不能顶到防线）、
-- 且 < BEACH_END_Z（BuildScenery 摆件落海自检断言）；下界别太靠海免得沙滩过宽。
Config.SHORE_PTS = {
    { x = -34.0, z = -1.0 },
    { x = -26.0, z =  2.2 },
    { x = -17.0, z = -1.6 },
    { x = -8.0,  z =  2.6 },
    { x =  0.0,  z = -0.6 },
    { x =  8.0,  z =  2.2 },
    { x = 17.0,  z = -1.6 },
    { x = 26.0,  z =  2.2 },
    { x = 34.0,  z = -1.0 },
}

--- 给定 x 处的水线 z（控制点间 Catmull-Rom 样条插值，区间外按端值平推）。
--- 过全部控制点、切线连续，拐角圆滑；几何与玩法都读这一个函数，曲线全链路一致。
---@param x number
---@return number
function Config.ShoreZ(x)
    local pts = Config.SHORE_PTS
    local n = #pts
    if x <= pts[1].x then
        return pts[1].z
    end
    if x >= pts[n].x then
        return pts[n].z
    end
    for i = 1, n - 1 do
        if x <= pts[i + 1].x then
            -- 本段 [p1,p2]，取前后邻居 p0/p3（端点处夹取自身）做 Catmull-Rom
            local p0 = pts[math.max(i - 1, 1)]
            local p1 = pts[i]
            local p2 = pts[i + 1]
            local p3 = pts[math.min(i + 2, n)]
            local t = (x - p1.x) / (p2.x - p1.x)
            local z0, z1, z2, z3 = p0.z, p1.z, p2.z, p3.z
            local t2 = t * t
            local t3 = t2 * t
            return 0.5 * ((2 * z1)
                + (-z0 + z2) * t
                + (2 * z0 - 5 * z1 + 4 * z2 - z3) * t2
                + (-z0 + 3 * z1 - 3 * z2 + z3) * t3)
        end
    end
    return pts[n].z
end

-- 样条在控制点之间可能有微小过冲，极值不能只扫控制点，得密集采样曲线本身，
-- 否则海直边/走位下限按“控制点极值”少留余量，过冲处会露出空洞或蓝面。
local SHORE_EXTREMA_SAMPLES = 64

--- 岸线的最靠海一处（下界）。海的矩形只能贴着它铺，否则曲线低谷里会露出蓝面。
---@return number
function Config.ShoreZMin()
    local pts = Config.SHORE_PTS
    local x0, x1 = pts[1].x, pts[#pts].x
    local minZ = math.huge
    for i = 0, SHORE_EXTREMA_SAMPLES do
        local z = Config.ShoreZ(x0 + (x1 - x0) * i / SHORE_EXTREMA_SAMPLES)
        if z < minZ then
            minZ = z
        end
    end
    return minZ
end

--- 岸线的最靠陆一处（上界）。所有“确保已上岸”的判据都按它算，不按平均值算。
---@return number
function Config.ShoreZMax()
    local pts = Config.SHORE_PTS
    local x0, x1 = pts[1].x, pts[#pts].x
    local maxZ = -math.huge
    for i = 0, SHORE_EXTREMA_SAMPLES do
        local z = Config.ShoreZ(x0 + (x1 - x0) * i / SHORE_EXTREMA_SAMPLES)
        if z > maxZ then
            maxZ = z
        end
    end
    return maxZ
end

-- 岸线的两个极值不另存常量：ShoreZMin()/ShoreZMax() 就是唯一算法，
-- 存一份副本只会多一个“改了控制点但忘了改它”的对账点。

-- 海面中心：+z 直边要探到“岸线最大值再往内陆一点”，水才漫得到那条曲线岸边的每一处
-- （否则曲线比 ShoreZMin 靠陆的列会出现“该有水却没水”的空洞）。沙滩网格已删，这条直线
-- 边不再由程序化沙盖住——它是否露出取决于场景里手摆的 patch-sand prefab 铺到哪。
-- （必须在 ShoreZMax 定义之后：文件级赋值时函数要是 nil，得到的是算术错误崩掉整个 Config）
Config.SEA_SHORE_TUCK = 0.4        -- 海边探过岸线最大值的量，保证曲线低谷列也有水覆盖
-- 海面整体朝内陆(+z)平移量：水线从岸线再往里推 100m（只平移、不扩海面，SEA_SIZE 不变）。
-- 注意：原本 +z 直边只 tuck 0.4 藏在沙底；这一推会让直边落到岸线以内 100m 处，
-- 水会漫过那一段沙滩（直边仍是直线，不再是曲线岸线）。
Config.SEA_INLAND_PUSH = 100.0
Config.SEA_CENTER_Z = (Config.ShoreZMax() + Config.SEA_SHORE_TUCK) - Config.SEA_SIZE * 0.5 + Config.SEA_INLAND_PUSH

-- 沙滩网格（Coast.BuildSand）已删除，海岛地面改由场景手摆 patch-sand prefab 提供。
-- 下面只保留仍被玩法消费的量：BEACH_END_Z=装饰带内边界，SAND_Y=全局地面高度（Dog/Enemy/出生点读）。
Config.BEACH_END_Z = 28.0        -- 装饰带内边界：棕榈/岩石/点缀只摆在这条线以内（必须大于岸线最大值，见 Coast.BuildScenery 的自检）
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
    -- Prop_Cannon 不在这里了：它是炮塔的炮身（见下方 TURRET 段）。同一个模型既是纯装饰
    -- 又能开炮就是"一个功能两个入口"，之前那颗还挂不到 noShoot、能被玩家打爆。
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

----------------------------------------------------------------------
-- 滩头防线 = 两座会自己开炮的友方炮塔（取代原来那排 15 根木桩的栅栏）
--
-- 为什么布点是表而不是写死两颗：塔数/塔位改了，血量池（Waves）、索敌（Turrets）、
-- 视觉（Coast）都从这一张表出，不留第二份真相。下标即塔号。
-- 塔位 x=±9 配 RANGE=10 正好把 LANES 四条航道（±4.5 / ±13）全覆盖。
-- 台面刻意压成矮炮座而不是高塔：相机吊在主角后上方 35 度俯视朝海，塔一高就横切视线
-- （瞭望塔被挪到 x=-12 就是同一个原因）。
----------------------------------------------------------------------
Config.TURRETS = { -9.0, 9.0 }       -- 每座塔的 x，z 恒取 DEFENSE_Z
Config.TURRET_Z = Config.DEFENSE_Z
Config.TURRET_HP = 150               -- 每塔血量：两座合计 300 = 原 DEFENSE_HP，总抗漏量一格没变（难度基线不飘）
Config.TURRET_STAND_OFF = 1.6        -- 海盗砍塔时与塔中心的水平距离（停在台座外沿，不钻进模型里）
-- 炮台 = pirate-kit 瞭望塔 prefab（tower-complete-small）缩小一档坐地，塔顶再架一门 Prop_Cannon 当炮
-- （失能时 Turrets 把那根炮管歪下来）。塔身实测高 6.78m（解析 Cache 里 .mdl 顶点缓冲，同 Coast 头部
-- 那套方法），原尺寸会横切相机 35°俯视朝海的视线，所以按 Config.TURRET_TOWER_SCALE 缩到约 4.4m。
-- Prop_Cannon 实测盒 x[-0.52,0.52] y[0.0009,1.1217] z[-0.8665,0.6138]：炮管往 -z 伸得更长，即原生
-- 炮口就朝海，落位 yaw=0 不用转；原点在炮架底面，所以直接放塔顶高度即可。
Config.TURRET_TOWER_PREFAB = "tower-complete-small"
Config.TURRET_TOWER_SCALE = 0.65                            -- 6.78m 的塔缩一档 ≈4.4m，别挡视线
Config.TURRET_TOP_Y = 6.78 * Config.TURRET_TOWER_SCALE      -- 塔顶最高点（失能烟/爆闪、血条悬浮的基准）
-- 炮座：塔模型 6.78 是尖顶，能架炮的平台比它低一截，按塔顶放炮会悬空，故下压 0.2 坐进平台。
Config.TURRET_CANNON_SEAT_Y = Config.TURRET_TOP_Y - 0.2     -- 炮架底面落点（比塔顶低 0.2）
Config.TURRET_MUZZLE = Vector3(0.0, Config.TURRET_CANNON_SEAT_Y + 0.47, -0.85)  -- 炮口相对塔中心（炮座 + 炮身中段 0.47，朝海侧）
Config.TURRET_GUN_PITCH_DEAD = 35.0               -- 失能时炮管歪下去的角度，一眼看出这座塔已哑

-- 火控：定位是"漏防兜底"，且已收紧成只打海盗船——单发才 2 点，打 HP=1 的涌岸海盗是一炮一个、
-- 把玩家该割的草全抢了（用户反馈"海盗被完虐"），故只咬血厚的船（小船 3 / 大船 8），让它退回
-- "帮忙拆船、不抢人头"的位置。船停在海面 BOAT_STOP_Z、塔在 z=9，直线≈16m；旧的"上岸警戒线"
-- 那道 z 门槛本是给海盗设的、船永远在其外，所以删掉它，改成纯粹按 XZ 射程索敌，半径够到停船的航道。
-- 要再调炮台强弱只动 DAMAGE / INTERVAL 两个旋钮，别再把它放回打海盗。
Config.TURRET_RANGE = 20.0                        -- 对船 XZ 索敌半径（米）：够到停船处最近两列航道
Config.TURRET_FIRST = 0.5                         -- 开局首发迟（秒）
Config.TURRET_INTERVAL = 2.2                      -- 两发间隔（秒）
Config.TURRET_INTERVAL_SPREAD = 0.5               -- 间隔随机 ±，别让两塔齐射像节拍器
Config.TURRET_DAMAGE = 2                          -- 只咬船：小船 3 两炮、大船 8 四炮（对走路海盗已不出手）
Config.TURRET_SPLASH_RADIUS = 2.2
Config.TURRET_SHELL_SCALE = 0.65                  -- 弹丸大小：cannon-ball.prefab 原生≈0.64m，0.65 复刻旧弹≈0.42m 观感
Config.TURRET_FLIGHT_TIME = 0.45                  -- 飞行时长：短弧，友方射击不需要预警盘
Config.TURRET_LOB_HEIGHT = 2.6                    -- 抛物线拱高（米）

----------------------------------------------------------------------
-- 相机（俯视角固定朝海，吊在主角正后上方；抖音小游戏刻意不需要玩家手动转视角）
--
-- 竖屏适配：水平视场角恒定（CAMERA_H_FOV）推出垂直 FOV，但竖屏被 VFOV_CAP 封顶；
-- 距离/俯角/前视按 t 从桌面值插值到 PORTRAIT 端值。竖屏端值的规格（可复解）：
--   * 人物在距屏幕底 1/5 屏高（中心下 0.3 屏高，占半屏 k=0.6）：
--     k = tan(pitch - atan(dist·sinP/(dist·cosP+lookAhead))) / tan(vfov/2)
--   * 人物占屏高 ≈11%（与桌面相当）：tan(1.8/dist) / (2·tan(vfov/2))
--   * 视线中心(lookAt)落在人物前方 lookAhead 处 ≈ 登陆带，画面上 4/5 全给海岸。
-- t=0 即桌面观感（1024x768 时垂直 FOV≈42，与改动前一致）。
----------------------------------------------------------------------
Config.CAMERA_H_FOV = 54.0           -- 水平视场角（度），各端恒定
Config.CAMERA_DISTANCE = 20.0
Config.CAMERA_TARGET_Y = 2.85        -- 相机关注点相对主角脚底的高度
Config.CAMERA_PITCH = 35.0           -- 俯角（度），斜后方朝海面倾斜俯视
Config.CAMERA_LOOK_AHEAD = 6.0       -- 视线中心往海方向偏移，看到更大范围战场
Config.CAMERA_FAR_CLIP = 260.0
Config.CAMERA_KICK = 0.3             -- 开火相机微小震动幅度
Config.CAMERA_KICK_DECAY = 12.0
-- 竖屏端值与插值区间：aspect<=PORTRAIT_ASPECT 取满竖屏值，>=LANDSCAPE_ASPECT 用桌面原值
Config.CAMERA_PORTRAIT = { distance = 12.0, pitch = 30.0, lookAhead = 14.0 }
Config.CAMERA_PORTRAIT_ASPECT = 0.55
Config.CAMERA_LANDSCAPE_ASPECT = 1.3
Config.CAMERA_VFOV_CAP = 70.0        -- 垂直 FOV 上限，竖屏构图旋钮：96° 全视野人物只剩 2.4% 屏高；
                                     -- 70° 时人物 ≈11% 与桌面相当——横向覆盖收窄换主体清晰

--- 按画布宽高比（宽/高）派生本端相机参数。
---@param aspect number|nil 画布宽/高；nil 按桌面横屏
---@return { vfov: number, distance: number, pitch: number, lookAhead: number }
function Config.CameraForAspect(aspect)
    aspect = (aspect and aspect > 0.0) and aspect or Config.CAMERA_LANDSCAPE_ASPECT
    local t = (Config.CAMERA_LANDSCAPE_ASPECT - aspect)
        / (Config.CAMERA_LANDSCAPE_ASPECT - Config.CAMERA_PORTRAIT_ASPECT)
    t = math.min(math.max(t, 0.0), 1.0)
    local function lerp(a, b) return a + (b - a) * t end
    -- 水平恒定 ⇒ 垂直 FOV = 2·atan(tan(H/2)/aspect)
    local vfov = 2.0 * math.deg(math.atan(math.tan(math.rad(Config.CAMERA_H_FOV * 0.5)) / aspect))
    return {
        vfov = math.min(vfov, Config.CAMERA_VFOV_CAP),
        distance = lerp(Config.CAMERA_DISTANCE, Config.CAMERA_PORTRAIT.distance),
        pitch = lerp(Config.CAMERA_PITCH, Config.CAMERA_PORTRAIT.pitch),
        lookAhead = lerp(Config.CAMERA_LOOK_AHEAD, Config.CAMERA_PORTRAIT.lookAhead),
        -- 本端落在插值的哪一格（0=桌面原值，1=满竖屏端值）。调试面板显示它，否则在手机上
        -- 调“横屏端”那三行会毫无反应，看起来像面板坏了。
        t = t,
        aspect = aspect,
    }
end

----------------------------------------------------------------------
-- UI 缩放（dp 方案）：rml 里全部用 dp 单位，1 dp = UI_SCALE 画布像素，
-- main.lua 启动时按画布短边算出并 rmlUi:SetScale 生效。基准短边 700px 时 1:1；
-- 小屏按比例缩（下限保可读/可点），大屏不放大（桌面观感与历史一致）。
-- 真机 393x852 → 393/700 ≈ 0.56。
----------------------------------------------------------------------
Config.UI_DESIGN_SHORT_SIDE = 700.0
Config.UI_SCALE_MIN = 0.55
Config.UI_SCALE = 1.0   -- 占位：main.lua 启动时写入，Juice/摇杆几何读它

--- 按画布尺寸派生 UI 缩放系数。
---@param canvasW number
---@param canvasH number
---@return number
function Config.ComputeUIScale(canvasW, canvasH)
    local shortSide = math.min(canvasW or 0.0, canvasH or 0.0)
    if shortSide <= 0.0 then
        return 1.0
    end
    return math.min(math.max(shortSide / Config.UI_DESIGN_SHORT_SIDE, Config.UI_SCALE_MIN), 1.0)
end

----------------------------------------------------------------------
-- 敌人
----------------------------------------------------------------------
Config.LANES = { -13.0, -4.5, 4.5, 13.0 }
-- 船族现在全是 kenney prefab：小船/玩家船 ship-row-large、大船 ship-pirate-large
-- （旧的 Quaternius Ship_Small/Ship_Large 已连资产一起删除）。这批沿 z 建模，静置时船头
-- 朝海（= 退着开），故统一用 180 把船头拧向内陆（航行方向 +z）。玩家船驾船公式也以此为
-- 基准偏置（Player.lua），所以改这一处两侧同步生效；若某艘反而了，再按 kind 拆档。
Config.BOAT_YAW = 180.0
-- 船体视觉缩放，只作用于实例化出来的船体子节点（见 Enemy:SpawnBoat / Coast:BuildPlayerBoat），
-- 不缩逻辑节点：吃水、瞄点、坐船高度、沉船翻滚都是相对逻辑节点定义的。
Config.BOAT_SMALL_SCALE = 1.0        -- ship-row-large（小船 + 玩家船，实测 2.75宽 x 0.85高 x 2.85长；旧船是 3.60 长，要更像旧体型可往上加）
Config.BOAT_BIG_SCALE = 0.8          -- ship-pirate-large（大船，真机量过：原尺寸过大、0.6 又偏小）
Config.BOAT_DRAFT = 0.15             -- 吃水：船体底面沉到水面下这么多，看着才像浮着
-- 船的 combat 瞄点抬高度（相对船 node 原点=船底）。船 node 在水面下，需抬到甲板附近，
-- 且保证命中点高于水面（SEA_Y）。数值按 .mdl 实测包围盒定：ship-row-large 高 0.85
-- （旧 Ship_Small 高 1.98，故旧值 1.2 已高于新船顶，子弹会打到船上方空气）。
Config.BOAT_HIT_HEIGHT = 0.45
-- 大船（ship-pirate-large）单独一档：原生高 9.96，×BOAT_BIG_SCALE=0.8 后船壳约 8m，
-- 与小船的 0.85 差一个数量级，共用会把大船的瞄点/炮口压到水线上。2.5 落在船壳中段。
Config.BOAT_BIG_HIT_HEIGHT = 2.5
Config.BOAT_SPAWN_Z = -44.0
Config.BOAT_STOP_Z = -6.5            -- 停船位置，海盗从这里下水
-- 船速/血：维持基线（不靠堆血量制造压力，已定案教训），只用略快的航速减少在射程里空转的时间。
Config.BOAT_SPEED = 4.2
Config.BOAT_BIG_SPEED = 3.0
Config.BOAT_UNLOAD_TIME = 0.55       -- 每吐一个海盗的间隔
Config.BOAT_HP = 3                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      
Config.BOAT_BIG_HP = 8
Config.BOAT_CARRY = 7
Config.BOAT_BIG_CARRY = 12
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
Config.CANNON_BALL_SCALE = 0.85          -- 炮弹大小：cannon-ball.prefab 原生≈0.64m，0.85 复刻旧弹≈0.55m 观感
Config.CANNON_FLIGHT_TIME = 2.2          -- 炮弹飞行时长（秒）：慢，留躲避时间
Config.CANNON_LOB_HEIGHT = 9.0           -- 抛物线拱高（米，峰在轨迹中点上方）
Config.CANNON_SPLASH_RADIUS = 3.2        -- 落点杀伤半径（米）
Config.CANNON_PLAYER_DAMAGE = 2          -- 炸到主角掉几格血（3 格口径：一发掉 2 格，与旧 100 血/30 伤同比例）

Config.PIRATE_SPAWN_Z = -4.5         -- 下水点（浅水里，看着像涉水冲滩）
-- Quaternius 角色模型原生朝 -z（Soldier 的散落道具在 z 负侧就是"正前方"），
-- 海盗要朝 +z（防线/塔）行进，所以偏航 180 度。若进游戏发现海盗倒着走，改成 0。
Config.PIRATE_YAW = 180.0
Config.PIRATE_SPEED = 2.0            -- 调慢：海盗滩头滞留更久，给玩家更多“割”的时间与参与感
Config.PIRATE_SPEED_SPREAD = 0.5     -- 每个海盗速度随机 ±，避免排队齐步走
Config.PIRATE_HP = 1                 -- 一发死：保住割草爽感的根。压迫靠数量/来速，不靠肉。
-- 海盗 combat 瞄点抬高度（相对 node 原点=脚底）。角色模型原点在脚底，抬到躯干中段
-- 才能让子弹打身子、命中特效不喷在脚底。离屏形象缩到 0.5 倍后，胸口高度同比减半。
Config.PIRATE_HIT_HEIGHT = 0.45
Config.PIRATE_ATTACK_TIME = 1.1      -- 到防线后挥刀的时长，然后结算掉血
Config.PIRATE_DEATH_TIME = 1.6       -- 死亡动画播多久后移除
Config.PIRATE_HITREACT_TIME = 0.28
Config.PIRATE_WADE_DEPTH = 0.35      -- 涉水时把模型抬高一点，别半截埋在水里
-- 突袭型海盗：不按航道直冲防线，而是转追主角、近身后反复砍主角（走位压力的来源）。
-- 生成时按概率标记，用稀有皮（骷髅/船长）做视觉区分；hp 更肉一点避免一发带走。
Config.PIRATE_RAIDER_CHANCE = 0.6    -- 每个海盗是突袭型（追主角）的概率。分流阀：调高→追你的多、冲海岸的自动少（总数不变）
Config.PIRATE_RAIDER_HP = 3          -- 突袭型血量（普通型走 PIRATE_HP=1）：追身的略肉一点避免一发带走
Config.PIRATE_MELEE_RANGE = 1.7      -- 与主角水平距离小于此值即停下挥刀
Config.PIRATE_PLAYER_DAMAGE = 1      -- 突袭型一刀砍主角几格血（3 格口径：一刀掉 1 格）
Config.MAX_ALIVE_UNITS = 32          -- 同屏单位闸：超了就延后生成。船能整船靠岸后上岸量猛增，拉到 32 保证潮水不断流（真机卡就回调）
Config.FAR_UNIT_DISTANCE = 55.0      -- 超过这个距离的单位关掉不可见时也更新

-- 性能Overlay（调试开关）：true 时屏幕顶中叠加一行 FPS/drawcall/三角面/同屏单位数（每 3 秒刷）。
-- 上线前必须置 false（关掉就不显不刷）。真机（抖音）没有引擎 DebugHud（SystemUI 已禁用），靠这个看指标。
Config.PERF_OVERLAY = true

-- 相机参数调试面板（调试开关）：true 时 HUD 右上角多一颗“相机”按钮，点开可在真机上实时拖
-- 本节全部相机常量并打印可直接粘回本文件的数值（见 game/CamDebug.lua + UI/CamDebug.rml）。
-- 上线前必须置 false：为 false 时 main.lua 根本不创建那个 RmlUI 组件，按钮与面板都不存在，
-- 不是“隐藏了”而是没有这份 UI。
Config.DEBUG_CAM_PANEL = true

-- 帧率上限（省电/降热）。真机（抖音 wasm）由引擎主循环按此值节流；桌面/编辑器同步生效。
-- 0 或 nil = 不改引擎默认（wasm 默认 30）。改这个值只需重打包（不碰 wasm），main.lua 启动时
-- 经 engine:SetWebFrameRate(Config.MAX_FPS) 落地。
Config.MAX_FPS = 40

Config.PIRATE_SKINS = { "Characters_Anne", "Characters_Henry", "Characters_Mako", "Characters_Sharky" }
Config.PIRATE_SKIN_RARE = { "Characters_Skeleton", "Characters_Captain_Barbarossa" }

----------------------------------------------------------------------
-- 武器（全自动开火：射程内取最近敌人自动射击，玩家只管走位——抖音小游戏的刻意简化）
-- 注：没有手动射线/准星/磁吸/爆头——这些都是早期“手动狙击”设想留下的残留配置，已随之清除。
----------------------------------------------------------------------
Config.MAG_SIZE = 5
Config.RELOAD_TIME = 1.2
Config.FIRE_COOLDOWN = 0.22          -- 两发之间的最小间隔（不降扳机频率，总伤害靠穿透/爆炸来收）
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
-- 曳光弹造型：Box.mdl（12 三角面）拉成细长亮芯 + 加色发光材质（Materials/BulletTracer.xml），
-- 替换旧的 54KB Sphere.mdl 实心球；微火花拖尾是常驻发射器，随弹销毁，false 可整关。
Config.BULLET_BOLT_THICK = 0.055     -- 亮芯截面边长（米）
Config.BULLET_BOLT_LEN = 0.85        -- 亮芯长度（米）
Config.BULLET_TRAIL = true           -- 子弹微火花拖尾（每颗弹 1 个 draw call，可关）
Config.GROW_XP = { pirate = 1, boat = 6, bigboat = 20, barrel = 2 }  -- 各类击杀喂的经验
-- 成长曲线：每项 { xp=达到该级所需累计经验, pellets=扇形弹丸数, damage=单发伤害, pierce=子弹可贯穿的单位数 }。
-- 首项是开局形态（xp 恒 0）；到达某级即采用该项，之后只增不减。pierce=1 即一发只杀一个、命中即消亡；
-- >1 时一颗子弹能沿航道串穿一排单位（路A 后已全线收回 1，若日后要做“后期穿透奖励”再单独抬顶档）。
-- 经验阈值按加密后的三波总击杀量标定，使成长贯穿全局而不早早封顶（真机体感不对就改 xp 间距）。
-- pellets 永远取奇数（当前表 1/3/5/7）：扇形按 spread*(i-0.5)/pellets 均匀铺开，只有奇数发才
-- 有一颗处于 a=0° 的正中弹直射锁定目标。偶数发（如 2）正中是空的，两颗对称夹住
-- 小船（船体仅 ~1.3m 宽）会刚好从缝里漏掉——故不用偶数。
Config.GROWTH = {
    { xp = 0,    pellets = 1, damage = 1, pierce = 1 },
    { xp = 50,   pellets = 3, damage = 1, pierce = 1 },
    { xp = 140,  pellets = 3, damage = 1, pierce = 1 },
    { xp = 300,  pellets = 5, damage = 1, pierce = 1 },   -- 路A：pellets≥5 段取消穿透（穿 2→1），不再一梭贯穿整列纵队
    { xp = 550,  pellets = 5, damage = 1, pierce = 1 },   -- 路A：同上，单扣命中上限 = 弹丸数（5）
    { xp = 900,  pellets = 5, damage = 1, pierce = 1 },   -- 路A：穿 2→1（A1 削顶仍留 2 穿，实测仍打爆，故连穿透一起收）
    { xp = 1400, pellets = 7, damage = 1, pierce = 1 },   -- 路A：穿 2→1，扇形 7 弹但每发只杀 1，保住散射爽感不刮穿
    { xp = 2100, pellets = 7, damage = 2, pierce = 1 },   -- 路A：穿 2→1，满级单扣命中上限 14→7
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
-- 走位下界不是一个常量，而是“那个 x 处的岸线 + 余量”（见 Player:Move）：岸线是曲线，
-- 写死一个 z 就会出现“在湾里已经踩进水里、判据还说你在沙上”。余量沿旧口径：旧版把
-- 下界写死在 1.5，配直线水线 -0.5 等于留 2.0m，这里保持同一个 2.0m。
Config.WALK_SHORE_MARGIN = 2.0       -- 别走进水里：相对岸线的余量
Config.WALK_MAX_Z = 22.0             -- 别绕到塔后村子里

-- 宠物狗（见 game/Dog.lua）：友方伴随单位，独立于 enemy.units（玩家子弹扫不到它、波次
-- 清怪不含它、爆桶连锁不误伤它）。平时在主角身边溜达，视野内有海盗就冲上去咬（静默击杀，
-- 不进 onScore）；海盗不主动攻击狗；只有大船炮弹能把狗炸飞上天翻滚，狗不死（无血量）。
Config.DOG = {
    spawnOffset = Vector3(1.6, 0.0, 1.2),  -- 出生/重开时相对主角脚底的偏移
    speed = 4.0,           -- 追击/回家的移动速度（米/秒，须快过海盗 2.3~2.6 才追得上）
    wanderSpeed = 1.7,     -- 身边溜达的速度
    followRadius = 6.0,    -- 离主角超过这个距离就先跑回身边
    followStopRadius = 2.5, -- “跑回身边”的迟滞：一旦启动回家，就一直跑到距主角小于此值才改回溜达（防在 followRadius 边界逐帧横跳→逐帧朝内/朝外翻面）
    wanderRadius = 4.0,    -- 在主角周围这么大范围内随机乱逛
    wanderMinDist = 1.5,   -- 重选漫游目标点时、新点需离狗至少这么远（否则走两步即“到达”→几乎立刻翻面重走）
    wanderArrive = 0.5,    -- 距漫游目标点小于此值判定到达，原地待机到计时结束再挑下一个
    aggroRadius = 11.0,    -- 看到这个半径内的存活海盗就去咬
    biteRange = 1.1,       -- 与海盗水平距离小于此值即下口
    faceMinDist = 0.35,    -- 与目标水平距离小于此值时不更新朝向（近距 atan 输入是亚分米噪声向量，逐帧会甩头）
    faceTurnRate = 540.0,  -- 朝向最大角速度（度/秒）：把“瞬时翻面”摊成有限速的平滑转身，边界跳变不再逐帧反向
    faceTurnEps = 4.0,     -- 转向死区（度）：已对准目标时不再反复微调，消除对准后的微幅摆抖
    stickFactor = 0.7,     -- 目标粘滞：当前锁定的目标距离×此系数 ≤ 新最佳距离时不切换，防等距 A↔B 横跳
    biteDamage = 2,        -- 每口伤害（普通海盗 HP=1 一口秒，突袭型 HP=3 需两口）
    biteInterval = 0.7,    -- 两次下口的间隔（秒）
    biteAnim = "Attack",   -- 咬击动画栈名
    barkCooldown = 1.1,     -- 追击/攻击途中两次吠叫的最小间隔（秒）
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
-- 角色坐船的局部高度（相对船节点）。坐太低会被船舷从俯视相机角度看进“舱里”看不见，故抬到甲板
-- 附近。新船 ship-row-large 实测高 0.85（y[0,0.85]，旧 Ship_Small 是 1.98），主角髋部大约在甲板。
Config.BOAT_SEAT_Y = 0.45
-- 船上双炮（船头左右两侧）：每发在两舷间轮换，子弹从对应炮位出膛
-- 数值按新船实测盒同比折算（旧船 5.14宽 x 3.60长 x 1.98高 → 新船 2.75 x 2.85 x 0.85）：
-- 前探不能超出船头 1.42，横移不能超出船舷 1.37，炮口不能高过船顶 0.85。
Config.BOAT_GUN_FORWARD = 1.3      -- 沿船头方向前探（米）
Config.BOAT_GUN_LATERAL = 0.8      -- 沿左右舷横向偏移（米）
Config.BOAT_GUN_Y = 0.6            -- 炮口高度（相对船节点）
-- 注意：不要复用 Config.BOAT_SPEED（那是敌人船速，见 Enemy.lua），玩家船速单独命名
Config.PLAYER_BOAT_SPEED = 8.0       -- 米/秒，比走路快
Config.PLAYER_BOAT_TURN = 3.0        -- 船转向速率（弧度/秒），比人物慢，做出惯性
Config.BOAT_INTERACT_DIST = 5.0      -- 角色与船水平距离小于此值可登船
-- 海域可行走矩形（驾船时）：比沙滩大得多，但不能开上沙滩
Config.SEA_MIN_X = -60.0
Config.SEA_MAX_X = 60.0
Config.SEA_MIN_Z = -80.0             -- 往海方向能开很远
-- 靠岸线按“岸线最靠陆那处 + 余量”算：按平均值或按某个 x 算，船会在两个湾里搁浅。
Config.SEA_MAX_Z = Config.ShoreZMax() + 3.0   -- 靠岸余量（下船不限制位置，随时可下）

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
-- combo 不再只加分：每 COMBO_BLAST_EVERY 连杀在击杀点放一次范围爆炸（割草的“杀→更多杀”正反馈闭环）。
Config.COMBO_BLAST_EVERY = 3         -- 基础档：弹丸数 < 阈值时每几连杀触发一次爆炸（0/nil = 关闭）
-- 动态补偿：弹丸越多→combo 涨得越快→同一段爆发里爆炸更频繁→后期滚雪球碾压。弹丸数达到
-- 阈值后改用 BIG 档（更疏）抵消。实际生效值由 main.lua 经 Waves:SetBlastEvery 随武器升级下发。
Config.COMBO_BLAST_EVERY_BIG = 15      -- 弹丸数 ≥ 阈值时的爆炸间隔（更疏，压后期清屏）
Config.COMBO_BLAST_PELLETS_SWITCH = 5 -- 弹丸数达到此值（含）起改用 BIG 档
--- 按当前弹丸数返回应使用的连杀爆炸间隔（>=阈值走 BIG 档，否则基础档）。
---@param pellets number
---@return number
function Config.ComboBlastEvery(pellets)
    if pellets and pellets >= Config.COMBO_BLAST_PELLETS_SWITCH then
        return Config.COMBO_BLAST_EVERY_BIG
    end
    return Config.COMBO_BLAST_EVERY
end
Config.COMBO_BLAST_RADIUS = 6      -- combo 爆炸杀伤半径（米），略大于爆桶连锁 4.6；非主要调平衡旋钮（后期无敌的靶心在弹丸×穿透而非此半径）
Config.COMBO_BLAST_DAMAGE = 1        -- combo 爆炸伤害（降到只扫过一片、不再一发清屏；秒普通海盗靠 damage=1 就够）
-- 防线的总血量不再是一个标量：它住在 Config.TURRETS 每塔一份（见上方 TURRET 段），
-- HUD 那一条读的是两塔合计，不存在第二份真相。
Config.LEAK_DAMAGE = 10              -- 一个海盗砍到塔一刀扣多少
-- 主角生命：突袭型海盗砍的是这条血。掉到 0 = 主角阵亡（与防线失守同为败局）。
-- 口径从 100 改成 3 点整数：头顶血条（见下方 HP_BAR）就是 3 格，一格 = 1 点，不存在半格。
-- 伤害同步重标（melee 10→1、炮弹 30→2），耐打次数与旧口径一致，难度没变。
Config.PLAYER_HP = 3
Config.PLAYER_INVULN = 0.6           -- 受击后无敌帧（秒），防贴身多刀在同一帧秒融

-- 主角头顶血条（3 格，RmlUI 挂在 3D 节点上，见 game/HpBar.lua + UI/HpBar.rml）。
Config.HP_BAR = {
    -- 血条中心离脚底的高度（米）。Characters_Lis.mdl 实测原生 y 上限 1.712（.mdl 顶点盒解析，
    -- 同法已复现 boat-row-large 的 2.75x0.85x2.85），乘 PLAYER_MODEL_SCALE 1.5 = 2.568，
    -- 再加 0.18 余量让条子压在头顶空气上而不是埋进头发。
    overhead = 2.75,
    -- 面板世界尺寸（米）。1.9 x 0.38 恰为 5:1，与下面纹理 320x64 同比例 → 格子不会被拉扁。
    worldW = 1.9,
    worldH = 0.38,
    -- 离屏画布的逻辑尺寸（UI 像素，同时是分配的纹理大小）。RmlUI 按它排版，别改一边忘另一边。
    -- 硬约束：两轴都必须在 64..4096（RmlCanvasComponent::SetUISize 里校验）。踩过的坑：
    -- 先前写的 200x40 因 40 < 64 被整条拒收，纹理停在默认的 512x512，血条的 200x40 排版
    -- 只占那张纹理左上 39%x8%，UV 覆满面板后就被拉成一条几乎看不见的细缝。
    texW = 320,
    texH = 64,
    -- 贴图技术：引擎 Contents 里的 DiffUnlitAlpha（RmlWorldCanvas 内部用的同一个，无光照 + alpha 混合，
    -- 血条不会被太阳打暗、透明处不糊成一块黑）。
    technique = "Techniques/DiffUnlitAlpha.xml",
}

-- 炮塔头顶血条：与主角共用 HpBar.lua 那套世界面板，但走 ratio 模式——连续进度条
-- （见 UI/TurretHpBar.rml），因为砍塔是缓慢持续掉血，3 格会把“还剩多少”量化得太粗。
-- 锚点换成每座塔节点、血量上限换成 TURRET_HP（进度 = cur/TURRET_HP）。颜色绿色，与主角格血条同色系。
-- 面板尺寸保持 5:1（与纹理 320x64 同比例）以免透视拉扁；悬浮高度按炮台实高定
-- （瞭望塔缩小后塔顶 TURRET_TOP_Y ≈4.4m，炮身上沿再高出约 1.1），压在炮口上方一点。
Config.TURRET_HP_BAR = {
    overhead = Config.TURRET_TOP_Y + 1.3,
    worldW = 1.4,
    worldH = 0.28,
}

-- 爆桶：无物理模块（抖音包禁用 URHO3D_PHYSICS），所以用距离遍历而不是球形查询
Config.BARREL_HP = 1
Config.BARREL_RADIUS = 4.6
Config.BARREL_DAMAGE = 2
Config.BARREL_CHAIN_DELAY = 0.12     -- 连锁引爆的递延，让爆炸看起来是一串而不是一帧

----------------------------------------------------------------------
-- 波次：每波是一份"配方"，真正的生成表在开局时由 game/WaveGen.lua 依配额随机编排。
-- 铁律： boats/volleys/bigboats 的配额与旧手调表逐一等量（总压力/经验不变=难度不飘），
-- 但航道分配、精确时刻、间距每局随机（铺法新鲜）。
--   boats：本波小艇总数（含被齐射消耗的部分）；bigboats：大船数。
--   volleys："同刻铺满 4 航道"的齐射次数，每次消耗 4 艘小艇（从 boats 里扣）。
--   window：把船铺到的时间跨度（秒）；ramp：前松后紧曲线档（warmup/steady/heavy）。
--   firstT（可选）：本波第一艘的最早时刻，缺省 1.0。lane 由 WaveGen 轮转，无需指定。
----------------------------------------------------------------------
Config.WAVE_INTERMISSION = 5.0
Config.WAVES = {
    -- 第一波：纯小艇、无齐射，一路路错开预热（12 艘）。
    { voice = "round_1", boats = 12, bigboats = 0, volleys = 0, window = 16.6, ramp = "warmup" },
    -- 第二波：首次投入大船，两波四航道齐射护航（13 小 + 4 大）。
    { voice = "round_2", boats = 13, bigboats = 4, volleys = 2, window = 12.5, ramp = "steady" },
    -- 第三波（终局高潮）：更多大船 + 密集齐射，堆尸潮（15 小 + 5 大）。
    { voice = "round_3", boats = 15, bigboats = 5, volleys = 2, window = 15.0, ramp = "heavy", firstT = 0.8 },
}

----------------------------------------------------------------------
-- 无尽模式：Config.WAVES 的手调开场用尽后（第 from 波起），由 WaveGen.EndlessRecipe(n)
-- 按下面的配方随波次升配额。只加"数量 / 密度 / 收紧窗口"，绝不动任何单位血量与速度
-- ——这是"遭遇战压力优先调波次结构而非堆血量"的铁律。一局没有通关，直到防线/主角被
-- 打穿才结束（Waves:Lose），HUD 波次号即"撑到第几波"的战绩。旋钮集中在此，Play 直调。
----------------------------------------------------------------------
Config.ENDLESS = {
    from = 4,                  -- 前 3 波走 Config.WAVES 手调开场，第 4 波起进公式
    boatsBase = 15,            -- 小艇起始配额（接住第 3 波的 15，无缝衔接）
    boatsPerWave = 2,          -- 每波小艇 +2（曾提到 3 直接打不过，退回原值；这是压力主旋钮）
    boatsCap = 42,             -- 小艇配额上限（到顶后靠窗口收紧继续加压）
    bigBase = 5,
    bigEveryWaves = 2,         -- 每 2 波多 1 条大船
    bigCap = 12,
    volleyBase = 2,
    volleyEveryWaves = 3,      -- 每 3 波多 1 次四航道齐射（曾提到 2 过猛，退回原值）
    volleyCap = 4,
    windowStart = 15.0,
    windowShrinkPerWave = 0.35, -- 出怪窗口随波次收紧（0.45 过密，收到略高于原 0.3）
    windowMin = 10.5,          -- 窗口下限（再密就撞同屏闸 MAX_ALIVE_UNITS）
    ramp = "heavy",            -- 沿用终局的"前松后紧"曲线
    firstT = 0.8,
    voiceEvery = 5,            -- 每 5 波播一次 round_3 旁白作里程碑（其余波静音）
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
Config.MAT_BULLET_TRACER = "Materials/BulletTracer.xml"  -- 曳光弹亮芯（加色发光）
Config.PLANE_MODEL = "Models/Plane.mdl"
Config.BOX_MODEL = "Models/Box.mdl"

-- 主角 prefab：模型引用 + 逐槽材质（hero.material×3）+ 整副骨骼层级都在里面，用户在枪骨
-- （PoleTarget.R_end）下挂了个空节点 ShootingPos 作为出膛点，运行期读它的世界坐标。见 Player.lua。
-- prefab 根节点名 CharacterArmature，自带 100× 骨骼单位缩放（FBX 导入残留），实例化时保持 identity。
Config.PLAYER_PREFAB = "Models/Characters-001/Characters_Lis.prefab"

-- Characters_Lis.mdl 有 12 个 geometry：主体蒙皮 Lis + 左肩甲 Shoulder + 一堆挂在左手 Middle1.L 的
-- 手持道具（Guitar/Axe/Spear/Pistol/Rifle/Shotgun/SMG/WoodenBat_Barbed/WoodenBat_Saw/Knife）。全身
-- 共用一张 Zombie_Atlas 图集（导入 -nm -nt 无材质，材质手写见 Materials/CharacterAtlas.xml）；只留
-- Rifle、其余道具按 geometry 名隐藏，逐段材质分派见 Player.lua。

--- pirate-kit 的模型资源名。
---@param name string 不含扩展名的资产名，如 "Prop_Barrel"
---@return string
function Config.PirateModel(name)
    return PIRATE_DIR .. name .. ".fbx.d/Models/" .. name .. ".mdl"
end

--- pirate-kit 的 prefab 资源名（prefabs/ 目录）。prefab 里已配好模型引用与逐槽材质，
--- 运行期只需 Node:InstantiatePrefab，不再手动 SetMaterial。
---@param name string 不含扩展名的 prefab 名，如 "ship-pirate-large"
---@return string
function Config.PiratePrefab(name)
    return "Models/Quaternius_pirate-kit/prefabs/" .. name .. ".prefab"
end

--- pirate-kit 角色的动画资源名。栈名见导入产物，如 Walk / Run / Sword / HitReact / Death / Idle。
---@param name string 资产名，如 "Characters_Anne"
---@param stack string 动画栈名，如 "Run"
---@return string
function Config.PirateAnim(name, stack)
    return PIRATE_DIR .. name .. ".fbx.d/Models/" .. name .. "_CharacterArmature" .. stack .. ".ani"
end

----------------------------------------------------------------------
-- 海盗序列帧（flipbook）billboard 群体渲染：把 3D 蒙皮小海盗降级成相机朝向的
-- 序列帧卡片，一个 BillboardSet 合批整群。烘焙产物 "<name>.flipbook.meta/.png"
-- 落在源 fbx 同目录（PIRATE_DIR），运行期 FlipbookBaker:Load 吃 "<base>.flipbook"。
----------------------------------------------------------------------
--- pirate-kit 序列帧图集资源基名（不含 .meta/.png 后缀，Load 内部拼接）。
---@param name string 资产名，如 "Characters_Anne"
---@return string
function Config.PirateFlipbook(name)
    return PIRATE_DIR .. name .. ".flipbook"
end

Config.FLIPBOOK_TECHNIQUE = "Techniques/DiffVColUnlitAlpha.xml"  -- Unlit+顶点色+alpha 混合
-- 单张卡片的尺寸与竖直摆放：billboard 是正方形图集格的 world 映射，position 为卡片中心。
-- 烘焙取景按全姿态并集加了留白，故卡片边长大于角色净高（~1.8m）；中心抬高把脚底近似落到 node 原点。
-- 进游戏按实际观感微调这两个值即可（改这里、不碰逻辑）。
Config.PIRATE_CARD_SIZE = 1.3      -- 卡片边长（world 单位，square）；原 2.6，整组离屏形象缩到 0.5 倍
Config.PIRATE_CARD_CENTER = 0.5    -- 卡片中心相对 node 原点（脚底）的抬高；随尺寸同比减半，脚底仍贴 node 原点
-- 逻辑动画名 → 烘焙 clip 名尾缀（烘只有这三条在用动画）。
Config.PIRATE_CLIP_STACKS = { "Run", "Sword", "Death" }

---@return string
function Config.PlayerModel()
    return PLAYER_DIR .. PLAYER_MODEL .. ".fbx.d/Models/" .. PLAYER_MODEL .. ".mdl"
end

--- 主角动画栈（Characters_Lis）：Idle / Idle_Gun / Run / Run_Gun / Walk / Walk_Gun / HitReact /
--- Death / Duck / Jump 系列 / Punch / Slash / Stab 等（没有 Idle_Shoot，举枪待机用 Idle_Gun）
--- 注意：重导后的 Characters_Lis.fbx 把骨骼名烘进了动画 take 名（Blender 里 action 命名成了
--- "CharacterArmature|Idle"，导出时 "|" 并入名字），于是 FbxImporter 按 <base>_<骨骼><take> 拼出的
--- .ani 变成双前缀 Characters_Lis_CharacterArmatureCharacterArmature<Stack>.ani。这里前缀相应写两层
--- _CharacterArmature 以匹配现有产物；若日后回 Blender 把 take 名清成 Idle 等干净名，改回一层即可。
---@param stack string
---@return string
function Config.PlayerAnim(stack)
    return PLAYER_DIR .. PLAYER_MODEL .. ".fbx.d/Models/" .. PLAYER_MODEL .. "_CharacterArmatureCharacterArmature" .. stack .. ".ani"
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
    -- 友方炮塔专用事件键（暂复用同一批素材）：不复用 cannonShot/cannonBlast，否则日后想
    -- 把敌方重炮换成长音时会被连带改掉，而两个事件早就该分家了。
    turretShot = { "Sound/gun_shot/powerful-cannon-shot.ogg" },
    turretBlast = { "Sound/gun_shot/cannon-blast.ogg" },
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

-- 常驻拖尾效果：发射器直接挂在飞行物节点上（activetime=0 永久发射、relative=false 在
-- 世界空间留痕），随节点 Remove 自动消失，不走 Fx:Spawn 的一次性生命周期表。
Config.FxTrail = {
    bullet = "Particle/BulletTrail.xml",       -- 曳光弹微火花残迹（Shooting.lua）
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
    cannon = 0.5,
    cannonWarn = 0.9,
    playerBody = 0.70,
}

return Config

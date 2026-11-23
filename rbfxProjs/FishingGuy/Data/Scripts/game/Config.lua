-- Scripts/game/Config.lua
-- 全部可调数值与地图几何。几何一律用「背景图像素」表达（x 向右、y 向下，原点图左上），
-- 世界坐标只在 World/Player 的边界处换算一次，避免两套坐标在业务代码里混用。

local Config = {}

Config.PIXEL_SIZE = 0.01
Config.IMG_W = 1024
Config.IMG_H = 1792

----------------------------------------------------------------------
-- UI：dp 基准值（× UI_SCALE = 画布像素）。UI/Hud.rml 里的同名数值必须与此一致，
-- 命中判定用的是这张表（输入不过 RmlUi，见 game/Hud.lua）。
-- BAG.rowTop 是刘海安全区留白；抖音侧要按真机 safeArea 再校，先给可改常量。
----------------------------------------------------------------------
Config.UI_DESIGN_SHORT_SIDE = 700
Config.SLOT_COUNT = 6
-- 竖屏短边折算成 dp 恒等于 UI_DESIGN_SHORT_SIDE（=700dp），所以横排总宽必须留够余量：
-- 背包整行 644dp、竖条 96dp，都不许顶到 700dp 才算安全。
Config.BAG = {
    rowTop = 88,              -- 距屏幕顶（刘海安全区）
    rowW = 644,               -- 整行宽：袋标 68 + 间距 + 6 格
    slot = 88,                -- 单格外框
    slotLefts = { 76, 172, 268, 364, 460, 556 },
}
Config.CARD = { w = 460, h = 300, close = 72 }
-- 竖条按屏幕垂直居中锚定（不是 top 锚）：编辑器 GameView 往往接近方形，
-- dp 表示的高度只有 ~700，top:300 + 高 620 会直接拖出下边界。
Config.REEL = {
    right = 40,               -- 距屏幕右边
    w = 96,
    h = 560,
    pad = 14,                 -- 外框到内槽的留白
    innerW = 68,
    innerH = 532,
    barH = 128,               -- 绿色捕获区高度（≈ REEL_BAR_RATIO × innerH）
    mark = 60,                -- 鱼标记边长
}

Config.COLOR_SPACE = 0        -- RenderPipeline Color Space：0=LDR Gamma，像素画原色直出
Config.RENDER_PATH = "RenderPaths/NoPostprocess.renderpath"

----------------------------------------------------------------------
-- 手感
----------------------------------------------------------------------
Config.WALK_SPEED = 280          -- 图像像素/秒
Config.WALK_STRIDE_PX = 36       -- 步态：每切一帧前进这么多，一循环 4 帧 = 144 像素
Config.TAP_MAX_PX = 42           -- 位移小于这个量算点击（走位），否则算滑动（抛竿）
Config.CAST_MIN_DIST = 150       -- 抛竿落点最近距离
Config.CAST_MAX_DIST = 560       -- 最远距离
Config.CAST_NEAR_WATER = 150     -- 距水面多远内可下竿
-- 落点必须留在图内：contain 适配下整图就是可视区，出图＝出屏。
-- 水塘多边形是故意描出画面边的（x 到 1064、y 到 1900），不卡这一条就能把漂甩到屏外。
-- 24 = 浮漂半宽 15.5 加一点余量，保证漂身完整可见。
Config.CAST_MARGIN = 24
Config.BITE_WAIT_MIN = 1.8
Config.BITE_WAIT_MAX = 5.5
Config.BITE_WINDOW = 1.5         -- 叹号出现后多长时间内点屏可开始拉杆
Config.FLIGHT_TIME = 0.45        -- 浮漂抛物线飞行时长
Config.MARK_FADE = 0.12          -- 走位高亮框的淡出时长

----------------------------------------------------------------------
-- 蓄力条（滑动抛竿时头顶那条，从左往右跟手涨），见 game/PowerBar.lua
-- 单位都是图像像素：条身 w×h，外框四边各粗 pad。aboveHead = 头顶距脚底的高度
-- （walk_*.png 是 256 画布，不透明区顶行 34、脚底线 244，故 244-34=210）。
----------------------------------------------------------------------
Config.POWER_BAR = {
    w = 150, h = 14, pad = 3,
    aboveHead = 210, gap = 14,
    layerFrame = 6, layerBack = 7, layerFill = 8,
}
Config.POWER_BAR_FRAME = Color(0.92, 0.86, 0.72, 0.92)
Config.POWER_BAR_BACK = Color(0.10, 0.08, 0.06, 0.80)
Config.POWER_BAR_FILL = Color(0.47, 0.76, 0.41, 1.0)

----------------------------------------------------------------------
-- 鱼线（竿尖→浮漂），画法是 N 段 1×1 白图精灵拼贝塞尔曲线，见 game/Line.lua
----------------------------------------------------------------------
Config.LINE_SEGMENTS = 14        -- 段数：越多越顺滑，每段一个组件
Config.LINE_THICK_PX = 2         -- 线宽（图像像素）
Config.LINE_LAYER = 5            -- 盖在角色(3)/叹号(4)之上：同层精灵靠排序距离分先后，单独给一层最稳
Config.LINE_COLOR = Color(0.92, 0.88, 0.78, 0.95)
-- 控制点相对中点往下坠的像素数：等口最松、咬钩被拽住、飞行中最紧
Config.LINE_SAG = { fly = 4, wait = 26, bite = 20, reeled = 14 }

-- 竿尖锚点：图像像素，相对角色脚底（x 向右、y 向上为负）。
-- 数值是照着 256px 素材一格一格量的（walk 四朝向各一个，cast/reel 两个姿势各一个）。
-- Fishing poses have front/back resources; left mirrors the right image and endpoint.
Config.ROD_TIP = {
    down = { 66, -154 },
    right = { 73, -165 },
    up = { 59, -146 },
    left = { -78, -159 },
    idle = {
        down = { 55, -155 }, right = { 63, -159 },
        up = { 56, -154 }, left = { -61, -156 },
    },
    cast = {
        down = { 84, -22 }, right = { 82, -203 },
        up = { 87, -176 }, left = { -82, -203 },
    },
    reel = {
        down = { 84, -21 }, right = { 114, -152 },
        up = { 81, -163 }, left = { -114, -152 },
    },
}

Config.REEL_BAR_RATIO = 0.24     -- 绿区占竖条高度
-- 起手 REEL_START=0.45，所以：全程框住 (1-0.45)/0.22 = 2.5 秒上鱼，全程脱钩 0.45/0.225 = 2.0 秒失败；
-- 持平线是 LOSS/(GAIN+LOSS) = 50.6% 的框住率。改这三个数任意一个，这三条跟着变。
Config.REEL_GAIN = 0.22          -- 鱼在绿区内：进度每秒
Config.REEL_LOSS = 0.225         -- 鱼在绿区外：进度每秒
Config.REEL_START = 0.45

-- 鱼标记自己乱窜：目标速度在 ±(FISH_SPEED_MIN + SPAN×power) 里换，换向间隔随 power 缩短，
-- 惯性追目标速度，外加一点随 power 增大的下沉。power 是 Config.FISH 里的每鱼种字段。
Config.FISH_SPEED_MIN = 0.30
Config.FISH_SPEED_SPAN = 0.85
Config.FISH_DART_HOLD = 0.55
Config.FISH_DART_POWER = 0.30
Config.FISH_INERTIA = 3.5
Config.FISH_SINK = 0.10

----------------------------------------------------------------------
-- 区域几何
----------------------------------------------------------------------
Config.LAND = {
    { 120, 250 }, { 900, 250 }, { 930, 700 }, { 975, 1010 },
    { 880, 1130 }, { 700, 1205 }, { 600, 1185 }, { 430, 1185 },
    { 300, 1215 }, { 120, 1120 }, { 55, 1000 }, { 95, 700 },
}

Config.WATER = {
    { -40, 1040 }, { 55, 1000 }, { 120, 1120 }, { 300, 1215 },
    { 430, 1185 }, { 600, 1185 }, { 700, 1205 }, { 880, 1130 },
    { 975, 1010 }, { 1064, 1040 }, { 1064, 1900 }, { -40, 1900 },
}

-- 木栈桥：可以站上去（也就能站到水中央下竿），但它本身不是水面
Config.PIER = { x0 = 398, y0 = 1132, x1 = 582, y1 = 1318 }

-- 障碍：房子、矮石墙、树、石头堆、水缸
Config.BLOCK_RECTS = {
    { x0 = 295, y0 = 120, x1 = 790, y1 = 585 },   -- 屋身含屋檐
    { x0 = 418, y0 = 530, x1 = 582, y1 = 588 },   -- 门前台阶
    { x0 = 135, y0 = 375, x1 = 195, y1 = 720 },   -- 左院墙
    { x0 = 805, y0 = 375, x1 = 870, y1 = 720 },   -- 右院墙
    { x0 = 170, y0 = 685, x1 = 445, y1 = 775 },   -- 左下矮墙
    { x0 = 590, y0 = 685, x1 = 865, y1 = 775 },   -- 右下矮墙
}

Config.BLOCK_CIRCLES = {
    { x = 245, y = 240, r = 60 },    -- 树
    { x = 95, y = 585, r = 38 },
    { x = 55, y = 700, r = 30 },
    { x = 115, y = 725, r = 28 },
    { x = 250, y = 860, r = 95 },
    { x = 150, y = 945, r = 42 },
    { x = 215, y = 995, r = 45 },
    { x = 275, y = 1005, r = 40 },
    { x = 430, y = 782, r = 28 },
    { x = 600, y = 790, r = 32 },
    { x = 740, y = 855, r = 62 },
    { x = 805, y = 815, r = 42 },
    { x = 875, y = 935, r = 75 },
    { x = 755, y = 1005, r = 38 },
    { x = 900, y = 785, r = 42 },
    { x = 855, y = 700, r = 32 },
    { x = 345, y = 1075, r = 52 },   -- 水缸
}

Config.JAR = { x = 345, y = 1075 }
Config.START = { x = 512, y = 880 }

----------------------------------------------------------------------
-- 鱼种
----------------------------------------------------------------------
Config.FISH = {
    { id = "crucian", name = "鲫鱼",
      minW = 0.2, maxW = 1.2, weight = 1.0, power = 0.35,
      flavor = "塘里最多的一种，老实好拉。" },
    { id = "koi", name = "锦鲤",
      minW = 1.5, maxW = 6.0, weight = 0.30, power = 0.62,
      flavor = "院子水缸里的贵客，力气不小。" },
    { id = "puffer", name = "河豚",
      minW = 0.6, maxW = 2.4, weight = 0.45, power = 0.85,
      flavor = "一受惊就乱窜，手别停。" },
}

----------------------------------------------------------------------
-- 坐标换算
----------------------------------------------------------------------
function Config.ToWorld(px, py)
    return Vector3((px - Config.IMG_W * 0.5) * Config.PIXEL_SIZE,
        (Config.IMG_H * 0.5 - py) * Config.PIXEL_SIZE, 0.0)
end

-- 世界坐标（Vector3 或 x,y 两个数）→ 图像像素
function Config.ToImage(a, b)
    local x, y
    if type(a) == "number" then
        x, y = a, b
    else
        x, y = a.x, a.y
    end
    return x / Config.PIXEL_SIZE + Config.IMG_W * 0.5,
        Config.IMG_H * 0.5 - y / Config.PIXEL_SIZE
end

local function inPoly(px, py, poly)
    local hit = false
    local n = #poly
    for i = 1, n do
        local a, b = poly[i], poly[(i % n) + 1]
        if (a[2] > py) ~= (b[2] > py) then
            local t = (py - a[2]) / (b[2] - a[2])
            if px < a[1] + t * (b[1] - a[1]) then
                hit = not hit
            end
        end
    end
    return hit
end

--- 落在画面内（留 CAST_MARGIN 边距）才算看得见的落点
function Config.InBounds(px, py)
    local m = Config.CAST_MARGIN
    return px >= m and px <= Config.IMG_W - m and py >= m and py <= Config.IMG_H - m
end

function Config.IsWater(px, py)
    return inPoly(px, py, Config.WATER)
end

function Config.IsPier(px, py)
    local p = Config.PIER
    return px >= p.x0 and px <= p.x1 and py >= p.y0 and py <= p.y1
end

function Config.IsLand(px, py)
    return inPoly(px, py, Config.LAND)
end

--- 可行走 = 在陆地上或站在栈桥上，且没撞上任何障碍
function Config.IsWalkable(px, py)
    if not (Config.IsLand(px, py) or Config.IsPier(px, py)) then
        return false
    end
    for _, r in ipairs(Config.BLOCK_RECTS) do
        if px >= r.x0 and px <= r.x1 and py >= r.y0 and py <= r.y1 then
            return false
        end
    end
    for _, c in ipairs(Config.BLOCK_CIRCLES) do
        local dx, dy = px - c.x, py - c.y
        if dx * dx + dy * dy < c.r * c.r then
            return false
        end
    end
    return true
end

--- 脚下附近有没有水（决定这一竿能不能抛）
function Config.NearWater(px, py)
    local r = Config.CAST_NEAR_WATER
    for i = 0, 15 do
        local a = i * math.pi * 0.125
        if Config.IsWater(px + math.cos(a) * r, py + math.sin(a) * r)
            and not Config.IsPier(px + math.cos(a) * r, py + math.sin(a) * r) then
            return true
        end
    end
    return false
end

--- 一次拖拽（图像坐标）折算成抛竿方向与力度：返回 dirX, dirY, power(0..1)；长度不足返回 nil。
--- 出手力度和头顶蓄力条共用这一份算法，两处各写一遍迟早对不上。
function Config.CastPower(ax, ay, bx, by)
    local dx, dy = bx - ax, by - ay
    local len = math.sqrt(dx * dx + dy * dy)
    if len < 1.0 then
        return nil
    end
    local t = (len - Config.CAST_MIN_DIST) / (Config.CAST_MAX_DIST - Config.CAST_MIN_DIST)
    return dx / len, dy / len, math.min(math.max(t, 0.05), 1.0)
end

--- 沿射线找最远的可落点：水面、不在栈桥上、不超出射程、且在画面内
function Config.CastTarget(px, py, dx, dy, power)
    local maxD = Config.CAST_MIN_DIST + (Config.CAST_MAX_DIST - Config.CAST_MIN_DIST) * power
    local bestX, bestY = nil, nil
    local d = 8.0
    while d <= maxD do
        local x, y = px + dx * d, py + dy * d
        if Config.InBounds(x, y) and Config.IsWater(x, y) and not Config.IsPier(x, y) then
            bestX, bestY = x, y
        end
        d = d + 8.0
    end
    return bestX, bestY
end

--- 点击目标裁剪：沿点击方向退回到最后一个可行走点（星露谷的“走到跟前停下”）
function Config.SnapWalkable(px, py, tx, ty)
    if Config.IsWalkable(tx, ty) then
        return tx, ty
    end
    local dx, dy = tx - px, ty - py
    local dist = math.sqrt(dx * dx + dy * dy)
    if dist < 1 then
        return px, py
    end
    dx, dy = dx / dist, dy / dist
    local lastX, lastY = nil, nil
    local s = 6.0
    while s < dist do
        local x, y = px + dx * s, py + dy * s
        if Config.IsWalkable(x, y) then
            lastX, lastY = x, y
        end
        s = s + 6.0
    end
    return lastX, lastY
end

--- 纯比例缩放：不做 1.0 上限。背景图是 contain 适配、随窗口一起放大的，
--- UI 若封顶在 1dp=1px 就会和大世界脱节（窗口越大，背包看起来越小）。
--- 去掉上下限后「短边折算 dp 恒等于 700」这条不变量才真正成立，dp 布局也就永远不会顶出屏幕。
function Config.ComputeUIScale(canvasW, canvasH)
    local shortSide = math.min(canvasW or 0.0, canvasH or 0.0)
    if shortSide <= 0.0 then
        return 1.0
    end
    return shortSide / Config.UI_DESIGN_SHORT_SIDE
end

return Config

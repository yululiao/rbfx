-- Scripts/game/Reel.lua
-- 星露谷式拉杆：竖条里「玩家拖的绿色捕获区」与「自己乱窜的鱼标记」两套独立运动，
-- 鱼在区内涨进度、出区掉进度。这里只算归一化物理（0=条顶，1=条底），
-- 像素映射与渲染全在 Hud.lua，方便调手感不碰表现层。
--
-- 绿区跟手而不学星露谷的「按住上升/松手下坠」：那套要一直举着手指、松手就掉，
-- 两版真机对比过，用户判它不友好，这里保留拖拽定位。

local Config = require("game.Config")

local Reel = {}
Reel.__index = Reel

local HALF = Config.REEL_BAR_RATIO * 0.5

function Reel.Build()
    local self = setmetatable({}, Reel)
    self.active = false
    return self
end

--- fish.power 越大：窜得越快、改向越勤、绿区跟随越沉
function Reel:Start(fishIdx)
    local fish = Config.FISH[fishIdx]
    if not fish then
        return false
    end
    self.fishIdx = fishIdx
    self.power = fish.power
    -- 两者都从条底起手（和星露谷一致）：绿区夹在 [HALF, 1-HALF]，所以底部是 1-HALF。
    -- 鱼先静止 0.35 秒再开始乱窜，免得开局就甩出去看不清。
    self.zone = 1.0 - HALF
    self.zoneTarget = 1.0 - HALF
    self.fish = 1.0
    self.fishV = 0.0
    self.fishTargetV = 0.0
    self.fishAccT = 0.35
    self.progress = Config.REEL_START
    self.active = true
    return true
end

--- 绿区中心对齐画布 Y 折算出的 0..1 位置（Hud 负责把手指像素折成归一化位置）
--- 中心夹在 [HALF, 1-HALF]，保证绿区不出内槽。
function Reel:SetZoneCenter(c)
    if not self.active then
        return
    end
    self.zoneTarget = math.min(math.max(c, HALF), 1.0 - HALF)
end

function Reel:ZoneRect()
    return self.zone - HALF, Config.REEL_BAR_RATIO
end

function Reel:Fish01()
    return self.fish
end

function Reel:Progress()
    return self.progress
end

function Reel:Update(dt)
    if not self.active then
        return nil
    end

    -- 绿区：跟手指，限速后仍然跟得上但有一点「竿子有重量」的滞后
    local follow = 9.0 - 4.0 * self.power
    self.zone = self.zone + (self.zoneTarget - self.zone) * math.min(1.0, dt * follow)

    -- 鱼：随机挑一个目标速度，按惯性逼近；power 越大的鱼速度带越宽、换向越频繁
    local speed = Config.FISH_SPEED_MIN + Config.FISH_SPEED_SPAN * self.power
    self.fishAccT = self.fishAccT - dt
    if self.fishAccT <= 0.0 then
        self.fishTargetV = (math.random() * 2.0 - 1.0) * speed
        self.fishAccT = Config.FISH_DART_HOLD - Config.FISH_DART_POWER * self.power + math.random() * 0.5
    end
    self.fishV = self.fishV + (self.fishTargetV - self.fishV) * math.min(1.0, dt * Config.FISH_INERTIA)
    -- 微重力：鱼自己往下沉，玩家必须一直抬着竿
    self.fish = self.fish + (self.fishV + Config.FISH_SINK * self.power) * dt
    if self.fish < 0.0 then
        -- 撞壁要连目标速度一起翻向：只反射当前速度的话，鱼在壁上被 0.35 衰减压成
        -- 亚像素抖动，只要下一次随机目标还朝壁外，整条鱼就粘在条底不动了。
        self.fish, self.fishV, self.fishTargetV = 0.0, speed * 0.6, speed * 0.5
        self.fishAccT = Config.FISH_DART_HOLD
    elseif self.fish > 1.0 then
        self.fish, self.fishV, self.fishTargetV = 1.0, -speed * 0.6, -speed * 0.5
        self.fishAccT = Config.FISH_DART_HOLD
    end

    local inside = math.abs(self.fish - self.zone) <= HALF
    if inside then
        self.progress = self.progress + Config.REEL_GAIN * dt
    else
        self.progress = self.progress - Config.REEL_LOSS * dt
    end

    if self.progress >= 1.0 then
        self.active = false
        return "win"
    elseif self.progress <= 0.0 then
        self.active = false
        return "lose"
    end
    return nil
end

--- 按权重抽一条鱼（权重之和不必为 1）
function Reel.RollFish()
    local total = 0.0
    for _, f in ipairs(Config.FISH) do
        total = total + f.weight
    end
    local r = math.random() * total
    for i, f in ipairs(Config.FISH) do
        r = r - f.weight
        if r <= 0.0 then
            return i
        end
    end
    return 1
end

--- 中钩后决定这条鱼的体重（进背包用）
function Reel.RollWeight(fishIdx)
    local f = Config.FISH[fishIdx]
    local w = f.minW + math.random() * (f.maxW - f.minW)
    return math.floor(w * 10 + 0.5) / 10
end

return Reel

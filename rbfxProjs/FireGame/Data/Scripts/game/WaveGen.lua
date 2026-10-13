-- Scripts/game/WaveGen.lua
--
-- 程序化波次编排：给一份"配方"（每波的小艇/大船配额 + 时间窗 + 齐射次数 + 渐密曲线），
-- 实时生成一张 {t, kind, lane} 的生成时间表。设计铁律是"总量与手调基线逐一相等、只把
-- 铺法随机化"——所以每局难度不飘，但哪条航道来船、什么时候来、齐射铺几条道每次都不同。
--
-- 三处真随机（早期版本这里做得太"确定"，编队看着局局一样，已重写）：
--   1. 时刻：反 CDF 全程随机采样（不是等分桶 + 微抖动），按 ramp 曲线决定疏密形状；
--   2. 航道：从"当前空闲"（距上次占用 >= minLaneGap）的道里均匀随机挑，而非固定轮转；
--   3. 齐射：宽度随机（2..4 条道）+ 随机道集 + 可选级联错帧，而非永远铺满全部航道。
-- 另外每次生成前用引擎 Time 播种，保证同一编辑器会话里连续 Play 的随机流也不同。
--
-- 只依赖 Config（读航道表），不依赖 Enemy/Waves，避免循环引用。

local Config = require("game.Config")

local WaveGen = {}

local FIRST_T = 1.0     -- 未指定时，每波第一艘的最早时刻（秒）
local MIN_GAP = 1.1     -- 同航道两艘船的最小时间间隔（秒）：防穿模、保可读

-- 各波的"前松后紧"曲线指数：>1 使采样更多落在后半段（越往后越密）。
local RAMP = { warmup = 1.7, steady = 1.2, heavy = 1.0 }

local function clamp(v, lo, hi)
    if v < lo then return lo end
    if v > hi then return hi end
    return v
end

--- 用引擎运行时长给 math.random 播种：进程启动至今的秒数跨 Play 递增，故每次 Play 随机流不同。
local function reseed()
    local ok = pcall(function()
        local t = GetSubsystem("Time")
        local e = t and t:GetElapsedTime() or 0.0
        math.randomseed(math.floor(e * 1000.0))
    end)
    if not ok then
        math.randomseed(os.time and os.time() or 0)
    end
    -- 丢弃播种后的头几个随机数（部分实现在刚 seed 时低位相关性较强）。
    for _ = 1, 4 do math.random() end
end

--- 把 1..nLanes 洗牌成一条航道下标序列（Fisher-Yates）。
local function shuffledLanes(nLanes)
    local arr = {}
    for i = 1, nLanes do arr[i] = i end
    for i = nLanes, 2, -1 do
        local j = math.random(i)
        arr[i], arr[j] = arr[j], arr[i]
    end
    return arr
end

--- 发牌式给 count 个航道下标：装袋洗牌、抽干再洗一轮 → 任意连续 nLanes 个必互不重复。
--- 大船用它铺满不同航道，杜绝"多艘大船挤同一条道前后串成纵列"。
local function dealLanes(count, nLanes)
    local out, bag = {}, {}
    for i = 1, count do
        if #bag == 0 then bag = shuffledLanes(nLanes) end
        out[i] = table.remove(bag)
    end
    return out
end

---@param recipe table { voice, boats, bigboats, volleys, window, ramp, firstT?, minLaneGap? }
---@return table[] 生成表（{t, kind, lane}），已按 t 升序
function WaveGen.Generate(recipe)
    reseed()

    local nLanes = #Config.LANES
    local firstT = recipe.firstT or FIRST_T
    local window = math.max(firstT + 1.0, recipe.window or 15.0)
    local minGap = recipe.minLaneGap or MIN_GAP
    local exp = RAMP[recipe.ramp] or 1.0
    local span = window - firstT
    local spawns = {}

    -- 记录每条航道最近被占用的时刻，用于"空闲道"判定与同道间隔约束。
    local lastLane = {}
    for i = 1, nLanes do lastLane[i] = -1e9 end

    -- 反 CDF 采样一个时刻：u 均匀、映射 u^exp 后按 exp>1 让落点后偏（前松后紧）。
    local function randTime()
        local u = math.random()
        return clamp(firstT + span * (u ^ exp), firstT, window)
    end

    -- 从空闲航道里均匀随机挑一条（没有空闲的就退而选"闲置最久"的，仍更新记账）。
    local function chooseLane(t)
        local free = {}
        for i = 1, nLanes do
            if t - lastLane[i] >= minGap then free[#free + 1] = i end
        end
        local lane
        if #free > 0 then
            lane = free[math.random(#free)]
        else
            local best, bg = 1, -1e9
            for i = 1, nLanes do
                local g = t - lastLane[i]
                if g > bg then bg, best = g, i end
            end
            lane = best
        end
        lastLane[lane] = t
        return lane
    end

    local small = recipe.boats or 0
    local big = recipe.bigboats or 0

    -- 1) 齐射：某时刻在同一条时间轴上铺满 2..nLanes 条不同航道（各道不穿模），每次从 boats 扣。
    --    宽度与选道随机、并可给每艘一点级联错帧 → 复刻旧表"齐射墙"的冲击但每局队形不同。
    for _ = 1, (recipe.volleys or 0) do
        if small > 0 then
            local vt = randTime()
            local want = 2 + math.random(math.max(1, nLanes - 2))   -- 2..nLanes
            local width = math.min(small, want)
            local order = shuffledLanes(nLanes)
            for k = 1, width do
                local t = clamp(vt + (k - 1) * (math.random() * 0.25), firstT, window)
                spawns[#spawns + 1] = { t = t, kind = "boat", lane = order[k] }
                lastLane[order[k]] = t
                small = small - 1
            end
        end
    end

    -- 2) 大船：单独"占位"分配——把窗口按大船数均分时间槽、各船各占一槽（槽内随机取点，
    --    彼此不会挤在相近时间横排成墙），航道用发牌给互不重复的道（不会同道串成纵列），
    --    时刻与道再随机配对。既分散又每局不同。
    if big > 0 then
        local slot = span / big
        local times = {}
        for i = 1, big do
            times[i] = clamp(firstT + (i - 1) * slot + math.random() * slot, firstT, window)
        end
        table.sort(times)
        local lanes = dealLanes(big, nLanes)
        for i = 1, big do
            local t, lane = times[i], lanes[i]
            spawns[#spawns + 1] = { t = t, kind = "bigboat", lane = lane }
            lastLane[lane] = t
        end
    end

    -- 3) 剩余小艇单发：同上。
    for _ = 1, small do
        local t = randTime()
        spawns[#spawns + 1] = { t = t, kind = "boat", lane = chooseLane(t) }
    end

    table.sort(spawns, function(a, b) return a.t < b.t end)
    return spawns
end

--- 无尽模式：第 n 波（n 从 Config.ENDLESS.from 起）的配额配方，形状同 Config.WAVES 条目，
--- 可直接喂 Generate。只按波次升"数量 + 密度 + 收紧窗口"，绝不动单位血量/速度（结构优先）。
---@param n number
---@return table
function WaveGen.EndlessRecipe(n)
    local e = Config.ENDLESS
    local k = n - e.from
    if k < 0 then
        k = 0
    end
    local boats = math.min(e.boatsCap, e.boatsBase + e.boatsPerWave * k)
    local big = math.min(e.bigCap, e.bigBase + math.floor(k / e.bigEveryWaves))
    local volleys = math.min(e.volleyCap, e.volleyBase + math.floor(k / e.volleyEveryWaves))
    local window = math.max(e.windowMin, e.windowStart - e.windowShrinkPerWave * k)
    local voice = (e.voiceEvery and e.voiceEvery > 0 and (n % e.voiceEvery == 0)) and "round_3" or nil
    return {
        voice = voice,
        boats = boats,
        bigboats = big,
        volleys = volleys,
        window = window,
        ramp = e.ramp,
        firstT = e.firstT,
    }
end

return WaveGen

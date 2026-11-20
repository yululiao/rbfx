local Config = require("game.Config")
local Telemetry = {}
Telemetry.__index = Telemetry

function Telemetry.Build(scene, deps)
    local self = setmetatable({}, Telemetry)
    self.scene, self.deps = scene, deps
    self.time = GetSubsystem("Time")
    self.renderer = GetSubsystem("Renderer")
    self.graphics = GetSubsystem("Graphics")
    self.frames, self.totalMs, self.maxMs, self.maxGap = 0, 0, 0, 0
    self.round = 1
    self.stats = {}
    self.detailStats, self.detailCounts, self.detailFrames = {}, {}, 0
    self.detailEvery = math.max(2, math.floor(Config.PERF_DETAIL_EVERY or 40))
    self.sampleEvery = math.max(1, math.floor(Config.PERF_SAMPLE_EVERY or 4))
    self.sequence, self.sampledFrames = 0, 0
    self.since = self.time:GetSystemTime()
    -- Only these known methods (at most two return values) are wrapped.
    -- Nested timings are inclusive, so NEVER add parent and child totals together.
    self:Watch(deps.shooting, "Shooting", { "Update", "UpdateBullets", "FireAt" })
    self:Watch(deps.enemy, "Enemy", { "Update", "NearestTarget", "UpdateProjectiles", "ComboBlast", "SpawnBoat", "SpawnPirate" })
    self:Watch(deps.enemy.crowd, "Crowd", { "Update" })
    self:Watch(deps.turrets, "Turrets", { "Update", "UpdateAim", "UpdateProjectiles" })
    self:Watch(deps.dog, "Dog", { "Update", "FindPirate" })
    self:Watch(deps.waves, "Waves", { "Update", "ProcessQueue" })
    self:Watch(deps.player, "Player", { "Move", "Update", "UpdateCamera", "ApplyAim", "GunHold" })
    self:Watch(deps.fx, "Fx", { "Update", "Spawn", "Flash", "Muzzle" })
    self:Watch(deps.juice, "Juice", { "EndFrame" })
    self:Watch(deps.hud, "Hud", { "Update" })
    self:Watch(deps.audio, "Audio", { "Emit" })
    self:Watch(deps.hpBar, "PlayerHpBar", { "Update" })
    for i, bar in ipairs(deps.turretBars or {}) do
        self:Watch(bar, "TurretHpBar" .. i, { "Update" })
    end
    print("FireGame PROFILE enabled: clock=1ms sampleEvery=" .. self.sampleEvery
        .. " window=5s inclusive=true; CPU/GPU/native audio outside Lua are not measured")
    return self
end

function Telemetry:Watch(object, prefix, methods)
    if not object then return end
    for _, method in ipairs(methods) do
        local original = object[method]
        if type(original) == "function" then
            local label = prefix .. "." .. method
            local stat = { label = label, calls = 0, total = 0, max = 0 }
            self.stats[#self.stats + 1] = stat
            object[method] = function(receiver, ...)
                if not self.sampling then return original(receiver, ...) end
                local started = self.time:GetSystemTime()
                local a, b = original(receiver, ...)
                local elapsed = math.max(0, self.time:GetSystemTime() - started)
                stat.calls = stat.calls + 1
                stat.total = stat.total + elapsed
                stat.max = math.max(stat.max, elapsed)
                return a, b
            end
        end
    end
end

function Telemetry:BeginFrame()
    self.sequence = self.sequence + 1
    local detail = Config.PERF_ENEMY_DETAIL and self.sequence % self.detailEvery == 1
    self.deps.enemy.detailProfile = detail and self or nil
    if detail then self.detailFrames = self.detailFrames + 1 end
    -- Keep intrusive per-unit probes out of the regular function samples.
    self.sampling = not detail and (self.sequence % self.sampleEvery == 0)
    if self.sampling then self.sampledFrames = self.sampledFrames + 1 end
    local now = self.time:GetSystemTime()
    if self.previous then self.maxGap = math.max(self.maxGap, now - self.previous) end
    self.previous, self.started = now, now
end

-- Called only on detailed sample frames. Nested spans are inclusive.
function Telemetry:DetailBegin()
    return self.time:GetSystemTime()
end

function Telemetry:DetailEnd(label, started)
    local elapsed = math.max(0, self.time:GetSystemTime() - started)
    local stat = self.detailStats[label]
    if not stat then
        stat = { total = 0, calls = 0, max = 0 }
        self.detailStats[label] = stat
    end
    stat.total = stat.total + elapsed
    stat.calls = stat.calls + 1
    stat.max = math.max(stat.max, elapsed)
end

-- Operation counts never read the native clock.
function Telemetry:DetailCount(label)
    self.detailCounts[label] = (self.detailCounts[label] or 0) + 1
end

function Telemetry:EndFrame()
    local now = self.time:GetSystemTime()
    local elapsed = now - self.started
    self.frames = self.frames + 1
    self.totalMs = self.totalMs + elapsed
    self.maxMs = math.max(self.maxMs, elapsed)
end

-- Called after animation/ApplyAim, so its samples stay in the same reporting window.
function Telemetry:Report()
    local now = self.time:GetSystemTime()
    local window = now - self.since
    if window < 5000 or self.frames == 0 then return end
    local d = self.deps
    -- Native batch runs inside Enemy.Update; do not add this timing to Lua totals.
    local spriteBatch = self.scene:GetComponent("FrameSpriteBatch")
    if spriteBatch then
        local updates = spriteBatch:GetUpdateCount()
        print(string.format("FireGame FRAME_SPRITE round=%d wave=%d frames=%d cpuMs/frame=%.3f maxMs=%.3f sprites=%d batches=%d frameChanges/frame=%.1f insideLuaUpdate=true",
            self.round, d.waves.waveIndex, updates,
            spriteBatch:GetTotalUpdateMs() / math.max(1, updates), spriteBatch:GetMaxUpdateMs(),
            spriteBatch:GetNumSprites(), spriteBatch:GetNumBatches(),
            spriteBatch:GetFrameChanges() / math.max(1, updates)))
        spriteBatch:ResetStatistics()
    end
    local live = d.enemy:AliveCount()
    local pipeline = self.scene:GetComponent("RenderPipeline")
    local color = pipeline and pipeline:GetAttribute("Color Space") or -1
    local shadows = pipeline and pipeline:GetAttribute("Enable Shadows")
    -- Wall-clock milliseconds around main.lua's Update only: NOT total CPU/GPU time.
    -- Lua KB excludes native resources, JS and host audio. Draws/tris are one frame.
    print(string.format(
        "FireGame THERMAL round=%d phase=%s fps=%.1f luaUpdateMs=%.2f luaMaxMs=%d gapMaxMs=%d luaKB=%.0f nodes=%d units=%d live=%d bullets=%d shells=%d fx=%d canvas=%dx%d draws=%d tris=%d color=%s shadows=%s wave=%d waveClock=%.1f",
        self.round, d.waves.phase, self.frames * 1000 / window,
        self.totalMs / self.frames, self.maxMs, self.maxGap, collectgarbage("count"),
        self.scene:GetNumChildren(true), #d.enemy.units, live, #d.shooting.bullets,
        #d.enemy.projectiles + #d.turrets.projectiles, #d.fx.active,
        self.graphics:GetWidth(), self.graphics:GetHeight(),
        self.renderer:GetNumDrawCalls(), self.renderer:GetNumPrimitives(),
        tostring(color), tostring(shadows), d.waves.waveIndex, d.waves.waveClock))
    table.sort(self.stats, function(a, b) return a.total > b.total end)
    local parts = {}
    for _, stat in ipairs(self.stats) do
        if stat.calls > 0 then
            parts[#parts + 1] = string.format("%s[calls=%d,ms/frame=%.3f,ms/call=%.3f,max=%d]",
                stat.label, stat.calls, stat.total / math.max(1, self.sampledFrames),
                stat.total / stat.calls, stat.max)
        end
        stat.calls, stat.total, stat.max = 0, 0, 0
    end
    print(string.format("FireGame PROFILE round=%d sampledFrames=%d inclusive=true %s",
        self.round, self.sampledFrames, table.concat(parts, " | ")))
    if self.detailFrames > 0 then
        local details = {}
        for label, stat in pairs(self.detailStats) do
            details[#details + 1] = string.format("%s[calls/frame=%.1f,ms/frame=%.3f,max=%d]",
                label, stat.calls / self.detailFrames, stat.total / self.detailFrames, stat.max)
        end
        for label, count in pairs(self.detailCounts) do
            details[#details + 1] = string.format("%s[count/frame=%.1f]", label, count / self.detailFrames)
        end
        table.sort(details)
        print(string.format("FireGame ENEMY_DETAIL round=%d wave=%d sampledFrames=%d mode=coarse+counts clock=1ms inclusive=true probeOverheadIncluded=true %s",
            self.round, d.waves.waveIndex, self.detailFrames, table.concat(details, " | ")))
        self.detailStats, self.detailCounts, self.detailFrames = {}, {}, 0
    end
    self.sampledFrames = 0
    self.frames, self.totalMs, self.maxMs, self.maxGap = 0, 0, 0, 0
    self.since = now
end

return Telemetry

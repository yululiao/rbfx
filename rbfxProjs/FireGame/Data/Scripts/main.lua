-- Scripts/main.lua
--
-- FireGame 入口：第三人称塔后越肩"狙击海盗"垂直切片。
-- 本文件不含玩法逻辑，只负责按依赖顺序装配各模块并驱动帧循环：
--   Config → Coast（海岸布景）→ Player（主角+越肩相机）→ Fx/Audio（反馈）
--   → Hud（数据绑定 HUD）→ Enemy（单位管理）→ Waves（波次导演）→ Shooting（瞄准开火）
--
-- 宿主约定：三个宿主（编辑器 GameView Play / MinigamePlayer / LuaGamePlayer）都在 C++ 侧
--   注入全局 `scene` 后再执行本脚本；场景根节点必须挂 LuaGameScript 且 Script Path 指向
--   本文件，否则 main.lua 永不执行（见 Default.scene）。脚本不创建场景，只往里补东西。
--
-- require 以 Scripts/ 为基准（EngineLuaVM 的 requirePrefixes_），故写 game.Config / core.Joystick。

local Joystick = require("core.Joystick")
local Config = require("game.Config")
local Coast = require("game.Coast")
local Player = require("game.Player")
local Fx = require("game.Fx")
local Audio = require("game.Audio")
local Hud = require("game.Hud")
local Enemy = require("game.Enemy")
local Waves = require("game.Waves")
local Shooting = require("game.Shooting")

----------------------------------------------------------------------
-- HUD 几何：设计分辨率与摇杆环（与 GameHud.rml 的 #stick_ring 同一套常量）
----------------------------------------------------------------------
local DESIGN = { w = 768.0, h = 1024.0 }
local STICK = {
    inset = 40.0,        -- GameHud.rml: #stick_ring 的 left / bottom
    size = 224.0,        -- width / height
    travel = 64.0,       -- 帽行程
    knobHalf = 48.0,     -- Knob.rml 是 96x96
}
STICK.cx = STICK.inset + STICK.size * 0.5

-- 画布尺寸必须问 RmlUI 要：编辑器 GameView 里 RmlUi 的 context 是那块 backbuffer，
-- 而 Graphics:GetWidth 是整个编辑器窗口。用窗口尺寸算 scale 会把 HUD 放大到画面外，
-- 换算出来的设计坐标也够不到底锚的环（浏览器/抖音里两者相等，兜底分支才用窗口）。
local uiScale = 1.0
local designW = DESIGN.w
local designH = DESIGN.h

local function CanvasSize()
    local ui = GetSubsystem("RmlUI")
    if ui and ui.GetCanvasSize then
        local size = ui:GetCanvasSize()
        if size.x > 0 and size.y > 0 then
            return size.x, size.y
        end
    end
    local graphics = GetSubsystem("Graphics")
    if graphics and graphics:GetWidth() > 0 and graphics:GetHeight() > 0 then
        return graphics:GetWidth(), graphics:GetHeight()
    end
    return 0.0, 0.0
end

-- 等比缩放（横竖取小的那个，圆环才不会变椭圆）+ 可见设计尺寸。
local function SyncUiScale()
    local width, height = CanvasSize()
    if width <= 0 or height <= 0 then
        return false
    end
    local scale = math.min(width / DESIGN.w, height / DESIGN.h)
    local ui = GetSubsystem("RmlUI")
    if ui and math.abs(ui:GetScale() - scale) > 0.0005 then
        ui:SetScale(scale)
    end
    uiScale = scale
    designW = width / scale
    designH = height / scale
    return true
end

-- 环心：与 GameHud.rml 里 #stick_ring 的 left/bottom 锚位同一个公式
local function StickCenter()
    return STICK.cx, designH - STICK.cx
end

----------------------------------------------------------------------
-- 装配
----------------------------------------------------------------------
-- 先声明，RestartGame / 回调是闭包，运行时才解引用这些 upvalue
local coast, player, fx, audio, hud, enemy, waves, shooting, knob, joystick

local function RestartGame()
    if enemy then
        enemy:Clear()
        enemy:SpawnStaticBarrels()
    end
    if shooting then
        shooting:Reset()
    end
    if waves then
        waves:Reset()
    end
    if hud then
        hud:HideEnd()
    end
end

-- 计分/combo 集中到 Waves：玩家子弹（Shooting）与爆桶连锁（Enemy:Damage）两条路径共用
local function OnScore(result, unit, headshot)
    if waves then
        waves:AddKill(result, unit, headshot)
    end
end

local function OnDefenseHit(dmg)
    if waves then
        waves:DamageDefense(dmg)
    end
end

local function OnGameOver(outcome, score)
    print(string.format("FireGame MARK game over: %s score=%d", tostring(outcome), score or 0))
end

coast = Coast.Build(scene)
player = Player.Build(scene, coast.playerSpot)
fx = Fx.Build(scene)
audio = Audio.Build(scene)
hud = Hud.Build(scene, {
    onRestart = RestartGame,
    -- 王者式普攻：HUD 攻击键回调；shooting 在后面才装配，闭包运行时才解引用
    onAttack = function()
        if shooting then
            shooting:OnAttack()
        end
    end,
})
enemy = Enemy.Build({
    scene = scene, fx = fx, audio = audio, player = player,
    onScore = OnScore, onDefenseHit = OnDefenseHit,
})
enemy:SpawnStaticBarrels()
waves = Waves.Build({ enemy = enemy, audio = audio, hud = hud, onGameOver = OnGameOver })
shooting = Shooting.Build({
    scene = scene, player = player, fx = fx, audio = audio, hud = hud,
    enemies = enemy, onScore = OnScore,
})

-- 摇杆帽：单独一份文档，元素级样式 Lua 改不了，文档级 SetPosition 可以。
-- 左摇杆驱动主角走位（见下方 player:Move）；该区域的触摸不参与瞄准，所以拖它只动人不转视角。
knob = scene:CreateComponent("RmlUIComponent")
knob:SetResource("UI/Knob.rml")

SyncUiScale()
local stickCx, stickCy = StickCenter()
joystick = Joystick:New({ cx = stickCx, cy = stickCy, radius = STICK.travel })

local input = GetSubsystem("Input")
local time = GetSubsystem("Time")

local canvasW, canvasH = CanvasSize()
print(string.format("FireGame MARK boot: scale=%.3f canvas=%.0fx%.0f design=%.0fx%.0f tower=(%.1f,%.1f,%.1f)",
    uiScale, canvasW, canvasH, designW, designH,
    Config.TOWER_POS.x, Config.TOWER_POS.y, Config.TOWER_POS.z))

----------------------------------------------------------------------
-- 帧循环
----------------------------------------------------------------------
local elapsed = 0.0
local firstFrame = true
-- 真实时间基准（GetTimeSinceEpoch 走 C time()，与 dt 的 emscripten 时钟是两套），用于分诊“计时高估”还是“真低帧”
local baseEpoch, baseGame, baseFrame = nil, 0.0, 0

SubscribeToEvent("Update", function(data)
    local dt = data.TimeStep
    -- 【临时限制】抖音 emscripten 计时被高估约 1000x（frequency 或 get_now 单位不匹配）。
    -- 在引擎层根治之前，先限制到 1/60 让游戏速度正常。真机低帧场景可考虑放宽到 1/30。
    if dt > 0.01667 then dt = 0.01667 end
    elapsed = elapsed + dt

    if SyncUiScale() then
        joystick:SetCenter(StickCenter())
    end

    -- 摇杆（视觉 + 抓取区判定）
    joystick:Update(input, uiScale)
    local stickX, stickY = joystick:GetVector()

    -- 开火/装弹计时（开火由 HUD 攻击键触发 OnAttack，不再读拖拽/点按输入）
    shooting:Update(dt)

    -- 玩法与世界
    enemy:Update(dt)
    waves:Update(dt)
    fx:Update(dt)
    -- 左摇杆走位（相机相对），再重算越肩相机（相机跟着 self.spot 走）
    player:Move(stickX, stickY, dt)
    player:Update(dt)
    hud:Update(dt)

    -- 摇杆帽回写
    local cx, cy = StickCenter()
    knob:SetPosition(Vector2(
        cx + stickX * STICK.travel - STICK.knobHalf,
        cy + stickY * STICK.travel - STICK.knobHalf))

    if firstFrame then
        firstFrame = false
        baseEpoch = time:GetTimeSinceEpoch()
        baseGame = elapsed
        baseFrame = time:GetFrameNumber()
        print(string.format("FireGame MARK first frame at t=%.3fs", time:GetElapsedTime()))
    end

    -- 稳态观测 + 分诊：按帧号每 90 帧一条。用 epoch 秒（真实时钟）与 elapsed（=Σdt 游戏时钟）对比：
    --   ratio = game/real：≈ 1 说明速度正常（“快”只是低帧跳步错觉）；明显 >1 说明 dt 被高估。
    --   rfps  = 真实帧率 = Δframe/real：若≈60 而 ratio≈6 => 计时高估（应修引擎 emscripten 时钟）；若≈10 且 ratio≈1 => 纯性能问题。
    local frameNumber = time:GetFrameNumber()
    if baseEpoch and frameNumber % 90 == 0 then
        local ammo = (shooting:GetAmmoState())
        local real = time:GetTimeSinceEpoch() - baseEpoch
        local game = elapsed - baseGame
        local ratio = (real > 0) and (game / real) or 0
        local rfps = (real > 0) and ((frameNumber - baseFrame) / real) or 0
        print(string.format("FireGame MARK dt=%.5f fps=%.1f frame=%d real=%.0fs game=%.2fs ratio=%.2f rfps=%.1f units=%d threats=%d ammo=%d score=%d",
            dt, time:GetFramesPerSecond(), frameNumber, real, game, ratio, rfps, enemy:AliveCount(), enemy:ThreatCount(), ammo, waves:GetScore()))
    end
end)

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
local Juice = require("game.Juice")

----------------------------------------------------------------------
-- HUD 几何：摇杆环（与 GameHud.rml 的 #stick_ring 同一套常量，单位都是画布像素）
-- 注意：RmlUi 的 px 不随 SetScale 缩放（SetDensityIndependentPixelRatio 只影响
-- dp 单位与媒体查询，见 RmlUi ComputeProperty.cpp 的 ComputeLength），布局空间
-- 恒等于画布尺寸，所以不存在"设计分辨率/缩放系数"这一层，全部直接用画布像素。
----------------------------------------------------------------------
local STICK = {
    inset = 40.0,        -- GameHud.rml: #stick_ring 的 left / bottom
    size = 176.0,        -- width / height（半径 88 = 行程 40 + 帽半宽 48，满偏时帽外缘恰贴环边）
    travel = 40.0,       -- 帽行程
    knobHalf = 48.0,     -- Knob.rml 是 96x96
}
STICK.cx = STICK.inset + STICK.size * 0.5

-- 画布尺寸必须问 RmlUI 要：编辑器 GameView 里 RmlUi 的 context 是那块 backbuffer，
-- 而 Graphics:GetWidth 是整个编辑器窗口（浏览器/抖音里两者相等，兜底分支才用窗口）。
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

-- 环心（画布像素）：与 GameHud.rml 里 #stick_ring 的 left/bottom 锚位同一个公式。
-- 环按 bottom 锚边，Y 随画布高度变，调用方每帧重算。
local function StickCenter(canvasH)
    return STICK.cx, canvasH - STICK.cx
end

----------------------------------------------------------------------
-- 中文字体：默认 Noto Sans 无 CJK 字形，中文画不出来。
-- 把 Noto Sans SC 注册为全局回退字体（fallback=true）：主字体缺字时自动落到它，
-- 英文数字仍用 Noto Sans，中英混排自然。
-- 出包前再子集化（扫描用字 → pyftsubset，子集是构建派生物，源库永远放全量），
-- 开发期直接用全量，UI 随便加字不会缺字。
-- 必须在 HUD 文档打开前完成。
----------------------------------------------------------------------
local rmlUi = GetSubsystem("RmlUI")
if rmlUi and not rmlUi:LoadFont("Fonts/NotoSansSC-Regular.otf", true) then
    print("FireGame MARK font fallback FAILED: Fonts/NotoSansSC-Regular.otf")
end

----------------------------------------------------------------------
-- 装配
----------------------------------------------------------------------
-- 先声明，RestartGame / 回调是闭包，运行时才解引用这些 upvalue
local coast, player, fx, audio, hud, enemy, waves, shooting, juice, knob, joystick

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
    if juice then
        juice:Reset()
    end
    if hud then
        hud:HideEnd()
    end
end

-- 计分/combo 集中到 Waves：玩家子弹（Shooting）与爆桶连锁（Enemy:Damage）两条路径共用；
-- 同一个汇入点也是打击感三件套的唯一触发口（击杀 = hit-stop + 震屏 + 飘字）。
local function OnScore(result, unit, headshot)
    if waves then
        waves:AddKill(result, unit, headshot)
    end
    if juice and result and result.killed then
        juice:OnKill(unit, result.score)
    end
end

local function OnDefenseHit(dmg)
    if waves then
        waves:DamageDefense(dmg)
    end
    -- 防线被砍：给一记轻震提示“有人在摸防线”，但不做 hit-stop（玩家没杀东西，
    -- 停时间只会困惑）。
    if juice then
        juice:AddTrauma(0.3)
    end
end

local function OnGameOver(outcome, score)
    print(string.format("FireGame MARK game over: %s score=%d", tostring(outcome), score or 0))
end

-- 载具交互（HUD 按钮回调）：登船/下船（player 在下方装配后才存在，这里是闭包运行时才解引用）
local function OnBoard()
    if player then player:BoardBoat() end
end

local function OnDisembark()
    if player then player:LeaveBoat() end
end

coast = Coast.Build(scene)
player = Player.Build(scene, coast.playerSpot, coast.playerBoat)
-- 打击感三件套：要主角的相机（飘字投影 + 震屏偏移的宿主）与画布尺寸
juice = Juice.Build(scene, {
    camera = player.camera,
    cameraNode = player.cameraNode,
    canvasSize = CanvasSize,
})
fx = Fx.Build(scene)
audio = Audio.Build(scene)
audio:PlayBGM("Sound/back_ground/mixkit-dirty-thinkin-989.ogg", 0.25)
hud = Hud.Build(scene, {
    onRestart = RestartGame,
    onBoard = OnBoard,
    onDisembark = OnDisembark,
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

-- 摇杆帽：单独一份文档，元素级样式 Lua 改不了，只能用文档级 SetPosition。
-- 注意：SetPosition 实质是写文档根元素的 left/top，所以 Knob.rml 的 body 必须带
-- position: absolute；static 下 left/top 会被布局直接忽略（每帧 SetPosition 静默失效，帽不跟手）。
-- 左摇杆驱动主角走位（见下方 player:Move）；该区域的触摸不参与射击锁定，所以拖它只动人不转视角。
knob = scene:CreateComponent("RmlUIComponent")
knob:SetResource("UI/Knob.rml")

local canvasW, canvasH = CanvasSize()
local stickCx, stickCy = StickCenter(canvasH)
joystick = Joystick:New({ cx = stickCx, cy = stickCy, radius = STICK.travel })

local input = GetSubsystem("Input")
local time = GetSubsystem("Time")

print(string.format("FireGame MARK boot: canvas=%.0fx%.0f tower=(%.1f,%.1f,%.1f)",
    canvasW, canvasH,
    Config.TOWER_POS.x, Config.TOWER_POS.y, Config.TOWER_POS.z))

----------------------------------------------------------------------
-- PC / 编辑器验证用键盘走位：WASD + 方向键，与左摇杆同一出口。
-- 注意：编辑器 GameView 必须先在其内部点一下抢占输入，游戏才收得到键鼠。
-- 触屏/抖音真机无键盘，KeyboardVector 恒返回 0，不影响摇杆。
----------------------------------------------------------------------
local function KeyboardVector()
    if not (KEY and input) then return 0.0, 0.0 end
    local x, y = 0.0, 0.0
    if input:GetKeyDown(KEY.D) or input:GetKeyDown(KEY.RIGHT) then x = x + 1 end
    if input:GetKeyDown(KEY.A) or input:GetKeyDown(KEY.LEFT) then x = x - 1 end
    if input:GetKeyDown(KEY.S) or input:GetKeyDown(KEY.DOWN) then y = y + 1 end
    if input:GetKeyDown(KEY.W) or input:GetKeyDown(KEY.UP) then y = y - 1 end
    return x, y
end

----------------------------------------------------------------------
-- 帧循环
----------------------------------------------------------------------
local seaScrollT = 0.0

SubscribeToEvent("Update", function(data)
    local dt = data.TimeStep
    -- 打击感帧首：推进 hit-stop 真实倒计时，拿到本帧世界 dt（定格期间被缩到 5%）。
    -- 引擎侧骨骼动画/粒子不受它影响（走自己的 Frame::timeStep），定格里死亡动画
    -- 照常播——这正是要的效果。世界 dt 只眼世界逻辑：海面滚动/开火/敌人/波次/主角。
    local worldDt = juice:BeginFrame(dt)

    -- 海面波纹流动：滚动法线贴图 UV（xy 的平铺密度与 Water.xml 的 UOffset/VOffset 同源，
    -- 提成了 Config.SEA_TILE，改密度只动那一处；w 是平移量）。
    -- M_Default 没有内置滚动，这里是给“静态平铺”加上时间维度的全部代码。
    seaScrollT = seaScrollT + worldDt
    coast.seaMaterial:SetShaderParameter("UOffset", Vector4(Config.SEA_TILE, 0.0, 0.0, seaScrollT * Config.SEA_SCROLL_U))
    coast.seaMaterial:SetShaderParameter("VOffset", Vector4(0.0, Config.SEA_TILE, 0.0, seaScrollT * Config.SEA_SCROLL_V))

    -- 环底锚，画布高度可能随 GameView 拖拽变化，每帧重算环心
    local cw, ch = CanvasSize()
    if cw > 0 then
        joystick:SetCenter(StickCenter(ch))
    end

    -- 摇杆（视觉 + 抓取区判定）
    joystick:Update(input)
    local stickX, stickY = joystick:GetVector()
    -- 键盘走位优先覆盖摇杆（PC/编辑器验证用）
    local kx, ky = KeyboardVector()
    if kx ~= 0.0 or ky ~= 0.0 then
        stickX, stickY = kx, ky
    end

    -- 全自动开火 + 装弹计时（世界 dt：定格期间不吐弹，弹与定格是同一套时间）
    shooting:Update(worldDt)

    -- 玩法与世界
    enemy:Update(worldDt)
    waves:Update(worldDt)
    -- 粒子的模拟在引擎侧走真实 timeStep（Lua 改不了），所以寿命回收也用真实 dt 对齐，
    -- 否则定格期间粒子播完了节点还被扣着不收。
    fx:Update(dt)
    -- 左摇杆走位（屏幕绝对方向），重算俯视相机位置。
    -- 相机看向 -z，屏幕右对应世界 -x，故水平输入取反；竖直不反（屏幕上=朝海 -z，与 stickY<0 一致）。
    player:Move(-stickX, stickY, worldDt)
    player:Update(worldDt)

    -- 打击感帧尾：飘字投影上屏 + 震屏叠加到相机（必须在 player:Update 摆好相机
    -- 基础姿态之后，否则偏移被覆盖）。
    juice:EndFrame(dt)

    -- 载具交互：两个按钮独立判定、互斥显示（条件本身互斥，显式双保险防止
    -- 遗留显示——旧版 if/elseif 只在 else 里隐藏，下船瞬间走不到 else，
    -- disembark 会和弹出的 board 同屏）。步行且靠近船→登船；在船上→下船（
    -- 不限制位置，随时可下）。
    if (not player.inBoat) and player:IsNearBoat() then
        hud:ShowButton("board")
    else
        hud:HideButton("board")
    end
    if player.inBoat then
        hud:ShowButton("disembark")
    else
        hud:HideButton("disembark")
    end

    hud:Update(dt)

    -- 摇杆帽回写：浮动原点式，帽锚在“按下点”并按偏转量跟随指针（用未取反的 stickX，保证与鼠标同向）。
    -- SetPosition 写的是帽文档左上角的画布像素，减去半宽让圆心落在“原点 + 偏转量*行程”上。
    local ox, oy = joystick:GetOrigin()
    knob:SetPosition(Vector2(ox + stickX * STICK.travel - STICK.knobHalf,
        oy + stickY * STICK.travel - STICK.knobHalf))
end)

----------------------------------------------------------------------
-- 上半身扭转（Player:ApplyAim）：必须踩在“动画已应用、蒙皮未计算”的窗口上，
-- 即 Octree 更新完 drawable 的 SceneDrawableUpdateFinished——引擎注释钦定的
-- 自定义动画/IK 时点（事件带 TimeStep）。放进上面的普通 Update 里会被动画每帧
-- 覆写骨骼，扭转永远看不见。
----------------------------------------------------------------------
SubscribeToEvent("SceneDrawableUpdateFinished", function(_data)
    if player then
        player:ApplyAim()
    end
end)

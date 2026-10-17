-- Scripts/main.lua
--
-- FireGame 入口：第三人称俯视角"狙击海盗"垂直切片（全自动开火，玩家只负责走位）。
-- 本文件不含玩法逻辑，只负责按依赖顺序装配各模块并驱动帧循环：
--   Config → Coast（海岸布景）→ Player（主角+俯视相机）→ Fx/Audio（反馈）
--   → Hud（数据绑定 HUD）→ Enemy（单位管理）→ Waves（波次导演）→ Shooting（自动开火）
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
local CamDebug = require("game.CamDebug")
local Enemy = require("game.Enemy")
local Waves = require("game.Waves")
local Shooting = require("game.Shooting")
local Juice = require("game.Juice")
local Grow = require("game.Grow")
local Dog = require("game.Dog")
local HpBar = require("game.HpBar")

----------------------------------------------------------------------
-- 帧率上限：真机（抖音 wasm）主循环按 Config.MAX_FPS 节流，省电/降热。
-- 必须在脚本加载期（顶层）同步设置：MinigamePlayer 在 Start() 里跑完本脚本后
-- 才去注册主循环（Application::Run 读 Engine::GetWebFrameRate），此时写入方能生效。
----------------------------------------------------------------------
do
    local engine = GetSubsystem("Engine")
    if engine and engine.SetWebFrameRate and Config.MAX_FPS and Config.MAX_FPS > 0 then
        engine:SetWebFrameRate(Config.MAX_FPS)
    end
end

----------------------------------------------------------------------
-- HUD 几何与 UI 缩放：rml 全部用 dp 单位（1dp = Config.UI_SCALE 画布像素），
-- 本文件里凡与 rml 几何同步的量（摇杆环/帽/行程/抓取半径）= rml 的 dp 基础值
-- × UI_SCALE。缩放系数按画布短边派生（Config.ComputeUIScale），启动时算一次
-- 经 rmlUi:SetScale 生效；编辑器拖 GameView 改尺寸时帧循环里重算（真机恒定）。
----------------------------------------------------------------------
local STICK_BASE = {
    inset = 40.0,        -- GameHud.rml: #stick_ring 的 left / bottom（dp）
    size = 224.0,        -- width / height（dp；半径 112 = 行程 64 + 帽半宽 48，满偏时帽外缘恰贴环边）
    travel = 64.0,       -- 帽行程（dp）：40dp 在真机只有 22px，轻碰即满偏没量程；64dp≈36px
    knobHalf = 48.0,     -- Knob.rml 是 96x96（dp）
    grab = 260.0,        -- 起手判定半径（dp）：浮动原点式，放宽到覆盖整块左下移动区，好按
}
-- 画布像素量（= 基础值 × UI_SCALE），ApplyUIScale 每次缩放变化重算
local STICK = {}
local function ApplyUIScale(scale)
    Config.UI_SCALE = scale
    STICK.inset = STICK_BASE.inset * scale
    STICK.size = STICK_BASE.size * scale
    STICK.travel = STICK_BASE.travel * scale
    STICK.knobHalf = STICK_BASE.knobHalf * scale
    STICK.grab = STICK_BASE.grab * scale
    STICK.cx = STICK.inset + STICK.size * 0.5
end

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
-- UI 缩放与画布量：必须在 HUD/Knob 文档打开与 Player/Juice 装配前完成
-- （rml 的 dp 按 SetScale 折像素，相机垂直 FOV 按宽高比派生，两者都吃这对输入）。
----------------------------------------------------------------------
local canvasW, canvasH = CanvasSize()
local uiScale = Config.ComputeUIScale(canvasW, canvasH)
ApplyUIScale(uiScale)
if rmlUi then
    rmlUi:SetScale(uiScale)
end
local canvasAspect = (canvasH > 0.0) and (canvasW / canvasH) or nil

----------------------------------------------------------------------
-- 装配
----------------------------------------------------------------------
-- 先声明，RestartGame / 回调是闭包，运行时才解引用这些 upvalue
local coast, player, fx, audio, hud, enemy, waves, shooting, juice, knob, joystick, grow, dog, camDebug, hpBar

local function RestartGame()
    if enemy then
        enemy:Clear()
        enemy:SpawnStaticBarrels()
    end
    if shooting then
        shooting:Reset()
    end
    if player then
        player:Reset()   -- 满血、解除死亡/无敌态（阵亡动画是终态，不自动恢复）
    end
    if grow then
        grow:Reset()
    end
    if dog then
        dog:Reset()   -- 宠物狗回身边、清掉在空中/起身态（狗不死，不重生）
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
-- fromChain 区分击杀来源（子弹 false / 爆炸连锁 true），只用来闸住“是否触发新爆炸”（切断自激）。
local function OnScore(result, unit, headshot, fromChain)
    if waves then
        waves:AddKill(result, unit, headshot, fromChain)
    end
    if juice and result and result.killed then
        juice:OnKill(unit, result.score)
    end
    -- 击杀喂经验→驱动局内成长（扇形弹丸数/伤害爬升）。grow 内部会经 onWeapon
    -- 把新武器数值推回 shooting、并升级时播旁白/震屏，这里只管喂。
    if grow and result and result.killed then
        grow:AddKill(unit and unit.kind)
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

-- 突袭型海盗砍到主角：扣主角血（经 Waves 统一裁决败局）+ 受击反馈。这一击是玩家自己
-- 遭殃，给足创伤（比防线受击重）。“砍死”则定格主角阵亡动画，否则播受击后仰。
local function OnPlayerHit(dmg)
    if waves then
        waves:DamagePlayer(dmg)
    end
    if juice then
        juice:AddTrauma(0.7)
    end
    if audio then
        audio:Play("hitFlesh")
    end
    if player then
        if waves and waves.playerHp > 0 then
            player:OnHurt()
        else
            player:Die()
        end
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
player = Player.Build(scene, coast.playerSpot, coast.playerBoat, canvasAspect)
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
-- 相机参数调试面板（仅 Config.DEBUG_CAM_PANEL=true）：右上角一颗按钮，真机上实时拖相机常量
-- 并把可粘回 Config.lua 的数值打到日志。必须在 player 之后建（它要调 player:ApplyCameraFit）。
-- 开关为 false 时下面整块不存在——上线包里连 UI 组件都不会创建。
if Config.DEBUG_CAM_PANEL then
    camDebug = CamDebug.Build(scene, player)
end
enemy = Enemy.Build({
    scene = scene, fx = fx, audio = audio, player = player,
    onScore = OnScore, onDefenseHit = OnDefenseHit, onPlayerHit = OnPlayerHit,
})
enemy:SpawnStaticBarrels()
-- 宠物狗：友方伴随单位，独立于 enemy.units（不被玩家子弹/波次/爆桶波及）。需 player+enemy，
-- 故在两者之后建；建完回填 enemy.dog，使大船炮弹落地时能把狗炸飞（Enemy:CannonImpact）。
dog = Dog.Build({ scene = scene, player = player, enemies = enemy, audio = audio })
enemy.dog = dog
waves = Waves.Build({ enemy = enemy, audio = audio, hud = hud, onGameOver = OnGameOver,
    onWaveCleared = function() if dog then dog:GoRest() end end })
-- 主角头顶 3 格血条：一份 RmlUI 文档渲染进离屏纹理、贴在跟随主角的 3D 面板节点上
-- （引擎原生的 UI 挂 3D 通路，不是世界坐标转屏幕坐标）。必须在 waves 之后建：
-- 血量的唯一真相在 waves.playerHp（Player.hp 只是个旧快照，别接上去）。
hpBar = HpBar.Build(scene, {
    cameraNode = player.cameraNode,
    player = player,
    getHp = function() return waves.playerHp end,
})
shooting = Shooting.Build({
    scene = scene, player = player, fx = fx, audio = audio, hud = hud,
    enemies = enemy, onScore = OnScore,
})
-- 局内成长：Build 会调 Reset 推一次开局武器数值（1 发 1 伤）；之后每次升级回调
grow = Grow.Build({
    hud = hud,
    onWeapon = function(snapshot, isInitial)
        shooting:SetWeaponStats(snapshot.pellets, snapshot.damage, snapshot.pierce)
        -- 弹丸越多爆炸会越频繁（combo 靠击杀数涨），达阈值后把间隔抬到 BIG 档补偿，防后期滚雪球。
        if waves then waves:SetBlastEvery(Config.ComboBlastEvery(snapshot.pellets)) end
        if hud then
            hud:SetWeaponText("弹丸 x" .. snapshot.pellets)
            -- 开局不提醒；真正升级时把"变强"做成一次可听可见的反馈（飘字旁白 + 轻震）
            if not isInitial and grow and grow.level > 1 then
                hud:Narrate("升级！弹丸 x" .. snapshot.pellets, 2.0)
                if juice then juice:AddTrauma(0.4) end
            end
        end
    end,
})

-- 摇杆帽：单独一份文档，元素级样式 Lua 改不了，只能用文档级 SetPosition。
-- 注意：SetPosition 实质是写文档根元素的 left/top，所以 Knob.rml 的 body 必须带
-- position: absolute；static 下 left/top 会被布局直接忽略（每帧 SetPosition 静默失效，帽不跟手）。
-- 左摇杆驱动主角走位（见下方 player:Move）；该区域的触摸不参与射击锁定，所以拖它只动人不转视角。
knob = scene:CreateComponent("RmlUIComponent")
knob:SetResource("UI/Knob.rml")

local stickCx, stickCy = StickCenter(canvasH)
joystick = Joystick:New({ cx = stickCx, cy = stickCy, radius = STICK.travel, grab = STICK.grab })

local input = GetSubsystem("Input")
local time = GetSubsystem("Time")

print(string.format("FireGame MARK boot: canvas=%.0fx%.0f uiScale=%.2f aspect=%.2f tower=(%.1f,%.1f,%.1f)",
    canvasW, canvasH, uiScale, canvasAspect or 0.0,
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

-- 性能诊断：由 Config.PERF_OVERLAY 总开关（默认 false = 上线态，不显不刷日志）。
-- 真机（抖音）无引擎 DebugHud（SystemUI 已禁用），靠屏幕上的 perfText Overlay + 每 3 秒一条 print 看指标。
local perfDiagOn = Config.PERF_OVERLAY
local perfAccum = 0.0
local perfRenderer = nil
local function PerfCountUnits()
    local pirates, boats = 0, 0
    if enemy and enemy.units then
        for _, u in ipairs(enemy.units) do
            if u.alive then
                if u.kind == "pirate" then
                    pirates = pirates + 1
                elseif u.kind == "boat" or u.kind == "bigboat" then
                    boats = boats + 1
                end
            end
        end
    end
    return pirates, boats
end

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
        if cw ~= canvasW or ch ~= canvasH then
            -- 尺寸变了（编辑器拖 GameView；真机恒定不进这分支）：重算 UI 缩放并同步。
            -- 引擎只在 ApplyScreenMode 时按 dpiScale 重设 SetScale（Engine.cpp），
            -- GameView 拖拽不走那条路，这里的重设不会被盖掉。
            canvasW, canvasH = cw, ch
            local scale = Config.ComputeUIScale(cw, ch)
            ApplyUIScale(scale)
            if rmlUi then
                rmlUi:SetScale(scale)
            end
            joystick:SetRadius(STICK.travel, STICK.grab)
        end
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
    -- 相机调试面板展开时吞掉走位（摇杆与键盘一起吞，放在两者合成之后）：小画布（编辑器
    -- GameView）下面板会盖住左下抓取区，否则拖滑条的同时人物在走，构图边变边看，调不准。
    if camDebug and camDebug:IsModal() then
        stickX, stickY = 0.0, 0.0
    end

    -- 全自动开火 + 装弹计时（世界 dt：定格期间不吐弹，弹与定格是同一套时间）
    shooting:Update(worldDt)

    -- 玩法与世界
    enemy:Update(worldDt)
    dog:Update(worldDt)
    waves:Update(worldDt)
    -- 粒子的模拟在引擎侧走真实 timeStep（Lua 改不了），所以寿命回收也用真实 dt 对齐，
    -- 否则定格期间粒子播完了节点还被扣着不收。
    fx:Update(dt)
    -- 左摇杆走位（屏幕绝对方向），重算俯视相机位置。
    -- 阵亡后锁住走位（player:Move 会把人拉回直立，与阵亡定格冲突）。
    -- 相机看向 -z，屏幕右对应世界 -x，故水平输入取反；竖直不反（屏幕上=朝海 -z，与 stickY<0 一致）。
    if not player.dying then
        player:Move(-stickX, stickY, worldDt)
    end
    player:Update(worldDt)

    -- 打击感帧尾：飘字投影上屏 + 震屏叠加到相机（必须在 player:Update 摆好相机
    -- 基础姿态之后，否则偏移被覆盖）。
    juice:EndFrame(dt)

    -- 头顶血条：排在震屏之后，才能拿到本帧最终的相机姿态做朝向对齐
    -- （放前面会用抖之前的姿态，血条比画面慢一帧）。
    hpBar:Update(dt)

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

    -- 性能诊断（Config.PERF_OVERLAY 开启时）：每 3 秒一次。drawcalls/tris 是引擎设备级真值
    -- （上一帧提交量），pirates/boats 是当前存活同屏数——同时喂给屏幕 Overlay 与日志。
    if perfDiagOn then
        perfAccum = perfAccum + dt
        if perfAccum >= 1.0 then
            perfAccum = 0.0
            if not perfRenderer then
                perfRenderer = GetSubsystem("Renderer")
            end
            local pirates, boats = PerfCountUnits()
            local draws, tris, fps = -1, -1, -1
            if perfRenderer then
                if perfRenderer.GetNumDrawCalls then draws = perfRenderer:GetNumDrawCalls() end
                if perfRenderer.GetNumPrimitives then tris = perfRenderer:GetNumPrimitives() end
            end
            if time and time.GetFramesPerSecond then fps = time:GetFramesPerSecond() end
            -- Overlay 一行：FPS / drawcall / 三角面(百万) / 同屏海盗・船
            if hud and hud.ShowPerf then
                -- 相机调试面板展开时让位：两者都想占屏幕上部，同屏只会互相盖掉；
                -- 帧数在那块面板自己的状态行里看（见 game/CamDebug.lua）。
                hud:ShowPerf(not (camDebug and camDebug:IsModal()))
                hud:SetPerf(string.format("FPS: %.0f | DC: %d | TRI: %.1fM | P:%d B:%d",
                    fps, draws, tris / 1048576.0, pirates, boats))
            end
            -- print(string.format("FireGame PERF: pirates=%d boats=%d drawcalls=%d tris=%d fps=%.1f",
            --     pirates, boats, draws, tris, fps))
        end
    end
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

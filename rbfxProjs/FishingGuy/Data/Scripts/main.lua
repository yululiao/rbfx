-- Scripts/main.lua
--
-- FishingGuy 入口：一屏固定的国风小院 + 水塘，点击走位、滑动抛竿、星露谷式拉杆、六格背包。
-- 本文件不含玩法逻辑，只做装配 + 指针手势路由（点/滑/拖三选一）+ 帧循环。
--
-- 宿主约定（与 FireGame 相同）：编辑器 GameView Play / LuaGamePlayer 都在 C++ 侧注入全局
--   `scene` 后再执行本脚本；场景根节点挂 LuaGameScript 且 Script Path = Scripts/main.lua。
-- require 以 Scripts/ 为基准，故写 game.Config。
--
-- 坐标：rml 与输入是画布像素（左上原点，y 向下）；玩法是背景图像素（同样 y 向下）。
--   两者的换算只发生在 World.ScreenToImage 一处，业务代码不碰世界坐标（除写节点位置）。

local Config = require("game.Config")
local World = require("game.World")
local Audio = require("game.Audio")
local Inventory = require("game.Inventory")
local Line = require("game.Line")
local PowerBar = require("game.PowerBar")
local Player = require("game.Player")
local Cast = require("game.Cast")
local Reel = require("game.Reel")
local Hud = require("game.Hud")

local renderer = GetSubsystem("Renderer")
local input = GetSubsystem("Input")
local rmlUi = GetSubsystem("RmlUI")

-- 像素画必须近邻采样；Sprite2D 自带的贴图不吃逐张过滤设置，只能改全局默认，
-- 且要在素材加载之前定下来。
if renderer then
    renderer:SetTextureFilterMode(FILTER.NEAREST)
end

local pipeline = scene:GetComponent("RenderPipeline")
if pipeline then
    pipeline:SetAttribute("Color Space", Config.COLOR_SPACE)
    pipeline:SetAttribute("Render Path", { type = "RenderPath", name = Config.RENDER_PATH })
end

local camNode = scene:GetChild("Camera")
local camera = camNode and camNode:GetComponent("Camera")
if not camera then
    print("FishingGuy MARK no Camera component on node 'Camera'：视口建不起来，画面会是空的")
end

-- 画布尺寸只能问 RmlUI 要：编辑器 GameView 里它的 context 才是那块 backbuffer
local function CanvasSize()
    local ui = GetSubsystem("RmlUI")
    if ui and ui.GetCanvasSize then
        local size = ui:GetCanvasSize()
        if size.x > 0 and size.y > 0 then
            return size.x, size.y
        end
    end
    local graphics = GetSubsystem("Graphics")
    if graphics and graphics:GetWidth() > 0 then
        return graphics:GetWidth(), graphics:GetHeight()
    end
    return 0.0, 0.0
end

if rmlUi and not rmlUi:LoadFont("Fonts/NotoSansSC-Regular.otf", true) then
    print("FishingGuy MARK font fallback FAILED: Fonts/NotoSansSC-Regular.otf")
end

local canvasW, canvasH = CanvasSize()
local uiScale = Config.ComputeUIScale(canvasW, canvasH)

----------------------------------------------------------------------
-- 装配
----------------------------------------------------------------------
local audio = Audio.Build(scene)
local inv = Inventory.Build()
World.Build(scene)
local player = Player.Build(scene)
local line = Line.Build(scene)
local powerBar = PowerBar.Build(scene)
local caster = Cast.Build(scene, audio, line)
local reel = Reel.Build()
local hud = Hud.Build(scene, inv)

if rmlUi then
    rmlUi:SetScale(uiScale)
end
World.FitCamera(camera, canvasW, canvasH)
hud:Layout(canvasW, canvasH, uiScale)
hud:SyncInventory()
audio:BgmStart()
if camera then
    SetViewport(0, scene, camera)
end

-- 等口时长、抽鱼、鱼重全靠随机；不播种的话每次 Play 都是同一串
math.randomseed(os.time and os.time() or 0)

local HINT_FAR = "点击地面走到水塘边"
local HINT_NEAR = "到了 · 朝水面滑动即可抛竿"
local HINT_WAIT = "等口… 叹号一亮就点屏"
local HINT_BITE = "上钩了！点屏开始拉杆"
local HINT_REEL = "按住屏幕上下拖：绿区罩住鱼标就能收线"
local HINT_FULL = "背包满了（同种鱼叠一格），点顶部的格子看看收获"

--- 站定后的提示：只有真能下竿时才提抛竿，否则新人会对着院子乱滑
local function WalkHint()
    return Config.NearWater(player.x, player.y) and HINT_NEAR or HINT_FAR
end

hud:SetHint(WalkHint())

----------------------------------------------------------------------
-- 拉杆起止
----------------------------------------------------------------------
local fishIdx = nil   -- 这一竿正在拉的鱼

local function EndReel()
    hud:ShowReel(false)
    fishIdx = nil
    caster:End()
    player.locked = false
    player:SetPose("idle")
    hud:SetHint(WalkHint())
end

local function StartReel()
    fishIdx = Reel.RollFish()
    reel:Start(fishIdx)
    hud:ShowReel(true, fishIdx)
    caster:Hook()
    player:SetPose("reel")
    hud:SetHint(HINT_REEL)
end

local function OnCastFail()
    hud:Toast("这个方向够不着水面，走近水边再滑一次", 1.8)
    audio:Click()
end

----------------------------------------------------------------------
-- 指针手势路由
-- 一根手指/鼠标左键一轮：按下先认领（UI 命中→吞掉；咬钩→拉杆），
-- 抬起时按位移量分「点地面走」与「滑动抛竿」。
----------------------------------------------------------------------
local MOUSE_ID = -1
local ptr = nil   -- { id, mode = "ui"|"world"|"reel", x0, y0, x, y }

--- 当前按住的指针（画布像素）。触摸优先，没有触摸时用鼠标左键顶上（编辑器/PC 验证）。
local function SamplePointer()
    local n = input:GetNumTouches()
    for i = 0, n - 1 do
        local t = input:GetTouch(i)
        if t then
            return t.touchID, t.position.x, t.position.y
        end
    end
    if input:GetMouseButtonDown(MOUSEB.LEFT) then
        local m = input:GetMousePosition()
        return MOUSE_ID, m.x, m.y
    end
    return nil
end

local function FindPointer(id)
    local n = input:GetNumTouches()
    for i = 0, n - 1 do
        local t = input:GetTouch(i)
        if t and t.touchID == id then
            return t.position.x, t.position.y
        end
    end
    if id == MOUSE_ID then
        if input:GetMouseButtonDown(MOUSEB.LEFT) then
            local m = input:GetMousePosition()
            return m.x, m.y
        end
        return nil
    end
    return nil
end

local function OnWorldTap(x, y)
    if caster:IsActive() then
        -- 没咬钩时点屏＝收线（星露谷里空竿/不想等了的退出方式）
        hud:Toast("收线", 1.0)
        EndReel()
        return
    end
    local ix, iy = World.ScreenToImage(camera, canvasW, canvasH, x, y)
    if not player:SetGoal(ix, iy) then
        hud:Toast("这儿过不去", 1.2)
    end
end

local function OnWorldSwipe(x0, y0, x1, y1)
    if caster:IsActive() then
        return
    end
    local ax, ay = World.ScreenToImage(camera, canvasW, canvasH, x0, y0)
    local bx, by = World.ScreenToImage(camera, canvasW, canvasH, x1, y1)
    local dx, dy, power = Config.CastPower(ax, ay, bx, by)
    if not dx then
        return
    end
    if caster:Start(player, dx, dy, power) then
        hud:SetHint(HINT_WAIT)
    else
        OnCastFail()
    end
end

local function OnSlotTap(i)
    local rec = inv:Slot(i)
    if not rec then
        hud:Toast("空格子", 1.0)
        audio:Click()
        return
    end
    audio:Click()
    hud:ShowCard(rec.fish, rec.count, rec.maxW)
end

local function PointerDown(x, y)
    local hit = hud:HitTest(x, y)
    if hit == "close" or hit == "card" then
        hud:HideCard()
        audio:Click()
        return "ui"
    elseif hit == "reel" then
        reel:SetZoneCenter(hud:ZoneCenterFromCanvasY(y))
        return "reel"
    elseif hit and hit:sub(1, 4) == "slot" then
        OnSlotTap(tonumber(hit:sub(5)) + 1)
        return "ui"
    end
    if caster:IsBiting() then
        StartReel()
        return "reel"
    end
    return "world"
end

local function UpdatePointer()
    if ptr then
        local x, y = FindPointer(ptr.id)
        if not x then
            -- 抬起：world 模式在这里才决定是走还是抛
            if ptr.mode == "world" then
                powerBar:Hide()
                local dx, dy = ptr.x - ptr.x0, ptr.y - ptr.y0
                if math.sqrt(dx * dx + dy * dy) < Config.TAP_MAX_PX then
                    OnWorldTap(ptr.x, ptr.y)
                else
                    OnWorldSwipe(ptr.x0, ptr.y0, ptr.x, ptr.y)
                end
            end
            ptr = nil
            return
        end
        ptr.x, ptr.y = x, y
        if ptr.mode == "reel" and reel.active then
            reel:SetZoneCenter(hud:ZoneCenterFromCanvasY(y))
        elseif ptr.mode == "world" then
            -- 拖过点击阈值就开始显蓄力条，力度和出手用同一份算法算
            local dx, dy = x - ptr.x0, y - ptr.y0
            if math.sqrt(dx * dx + dy * dy) < Config.TAP_MAX_PX or caster:IsActive() then
                powerBar:Hide()
            else
                local ax, ay = World.ScreenToImage(camera, canvasW, canvasH, ptr.x0, ptr.y0)
                local bx, by = World.ScreenToImage(camera, canvasW, canvasH, x, y)
                local _, _, power = Config.CastPower(ax, ay, bx, by)
                if power then
                    powerBar:Show(player.x, player.y, power)
                else
                    powerBar:Hide()
                end
            end
        end
    else
        local id, x, y = SamplePointer()
        if id then
            local mode = PointerDown(x, y)
            ptr = { id = id, mode = mode, x0 = x, y0 = y, x = x, y = y }
        end
    end
end

----------------------------------------------------------------------
-- 帧循环
----------------------------------------------------------------------
local function Update(dt)
    local w, h = CanvasSize()
    if w ~= canvasW or h ~= canvasH then
        canvasW, canvasH = w, h
        uiScale = Config.ComputeUIScale(canvasW, canvasH)
        if rmlUi then
            rmlUi:SetScale(uiScale)
        end
        World.FitCamera(camera, canvasW, canvasH)
        hud:Layout(canvasW, canvasH, uiScale)
    end

    UpdatePointer()

    player:Update(dt)
    local ev = caster:Update(dt)
    if ev == "escaped" then
        audio:Fail()
        hud:Toast("手慢了，鱼跑了", 1.6)
        player.locked = false
        player:SetPose("idle")
    end

    -- 底部提示：咬钩 > 等口 > 拉杆 > 站定后的走位/下竿建议
    if caster:IsBiting() then
        hud:SetHint(HINT_BITE)
    elseif caster:IsActive() then
        hud:SetHint(HINT_WAIT)
    elseif reel.active or hud:IsCardOpen() then
        -- 拉杆中/HUD 自己会持续写提示，这里不抢
    elseif not player:IsMoving() then
        hud:SetHint(WalkHint())
    end

    if reel.active then
        local outcome = reel:Update(dt)
        hud:DrawReel(reel)
        if outcome == "win" then
            local idx = fishIdx
            local weight = Reel.RollWeight(idx)
            if inv:Add(idx, weight) then
                audio:Catch()
                hud:SyncInventory()
                hud:Toast(string.format("钓上 %s %.1f 斤", Config.FISH[idx].name, weight), 2.2)
                EndReel()
            else
                audio:Fail()
                hud:Toast(HINT_FULL, 2.4)
                EndReel()
            end
        elseif outcome == "lose" then
            audio:Fail()
            hud:Toast("脱钩了…", 1.6)
            EndReel()
        end
    end

    hud:Update(dt)
end

SubscribeToEvent("Update", function(data)
    Update(data.TimeStep)
end)

print(string.format("FishingGuy started: canvas=%.0fx%.0f uiScale=%.2f slots=%d",
    canvasW, canvasH, uiScale, Config.SLOT_COUNT))

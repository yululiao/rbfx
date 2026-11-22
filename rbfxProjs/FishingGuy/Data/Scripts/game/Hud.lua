-- Scripts/game/Hud.lua
--
-- 三份 RmlUi 文档 + 一套自算的命中几何：
--   UI/Hud.rml      背包行 / 飘字 / 拉杆竖条与进度灯 / 详情卡（LuaRmlUIComponent，数据绑定）
--   UI/ReelBar.rml  绿区（普通 RmlUIComponent，每帧 SetPosition 画布像素）
--   UI/FishMark.rml 鱼标记（要按鱼种换图，所以也是绑定文档 + SetPosition）
--
-- 输入不经 RmlUi：本文档树里 pointer-events 全关，点哪儿由 HitTest 用 Config 的
-- dp 几何 × UI_SCALE 算出来。理由在 Hud.rml 头部注释里。

local Config = require("game.Config")

local Hud = {}
Hud.__index = Hud

local SPECIES_KEY = { crucian = "c", koi = "k", puffer = "p" }
local SEGMENTS = 10

local function clamp(v, lo, hi)
    return math.min(math.max(v, lo), hi)
end

function Hud.Build(scene, inventory)
    local self = setmetatable({}, Hud)
    self.inv = inventory
    self.canvasW, self.canvasH, self.scale = 0.0, 0.0, 1.0

    -- 绑定读的值：槽位、分段进度、飘字、提示、卡片、竖条显隐
    self.slots = {}
    for i = 0, Config.SLOT_COUNT - 1 do
        self.slots[i] = { has = false, n = "", kind = nil }
    end
    self.segOn = 0
    self.showToast = false
    self.toast = ""
    self.toastT = 0.0
    self.hint = ""
    self.showReel = false
    self.showCard = false
    self.cardName = ""
    self.cardDesc = ""
    self.cardKind = nil
    self.markKind = nil

    local comp = scene:CreateComponent("LuaRmlUIComponent")
    self.comp = comp
    comp:SetHandlers({
        onInit = function(w)
            self.w = w
            for i = 0, Config.SLOT_COUNT - 1 do
                local slot = self.slots[i]
                w:BindReadonlyProperty("s" .. i .. "has", function() return slot.has end)
                w:BindReadonlyProperty("s" .. i .. "n", function() return slot.n end)
                for _, key in pairs(SPECIES_KEY) do
                    local k = key
                    w:BindReadonlyProperty("s" .. i .. k, function() return slot.kind == k end)
                end
            end
            for s = 0, SEGMENTS - 1 do
                local idx = s
                w:BindReadonlyProperty("seg" .. s, function() return idx < self.segOn end)
            end
            w:BindReadonlyProperty("showToast", function() return self.showToast end)
            w:BindReadonlyProperty("toast", function() return self.toast end)
            w:BindReadonlyProperty("hint", function() return self.hint end)
            w:BindReadonlyProperty("showReel", function() return self.showReel end)
            w:BindReadonlyProperty("showCard", function() return self.showCard end)
            w:BindReadonlyProperty("cardName", function() return self.cardName end)
            w:BindReadonlyProperty("cardDesc", function() return self.cardDesc end)
            for _, key in pairs(SPECIES_KEY) do
                local k = key
                w:BindReadonlyProperty("card" .. k:upper(), function() return self.cardKind == k end)
            end
        end,
    })
    comp:SetResource("UI/Hud.rml")

    -- 后建的文档 z 序更高：绿区压在竖条上，鱼标记压在绿区上
    self.bar = scene:CreateComponent("RmlUIComponent")
    self.bar:SetResource("UI/ReelBar.rml")
    self.mark = scene:CreateComponent("LuaRmlUIComponent")
    self.mark:SetHandlers({
        onInit = function(w)
            self.markW = w
            for _, key in pairs(SPECIES_KEY) do
                local k = key
                w:BindReadonlyProperty("mark" .. k:upper(), function() return self.markKind == k end)
            end
        end,
    })
    self.mark:SetResource("UI/FishMark.rml")
    self.markParentOn = true
    self:ReelVisible(false)
    self:Layout(self.canvasW, self.canvasH, self.scale)
    return self
end

function Hud:Dirty(name)
    if self.w then
        self.w:MarkDirty(name)
    end
end

----------------------------------------------------------------------
-- 布局与命中
----------------------------------------------------------------------

function Hud:Layout(canvasW, canvasH, scale)
    self.canvasW, self.canvasH, self.scale = canvasW or 0.0, canvasH or 0.0, scale or 1.0
    local B, R, C = Config.BAG, Config.REEL, Config.CARD
    local s = scale

    local rowLeft = canvasW * 0.5 - B.rowW * 0.5 * s
    self.slotRects = {}
    for i = 1, Config.SLOT_COUNT do
        self.slotRects[i] = {
            x = rowLeft + B.slotLefts[i] * s, y = B.rowTop * s, w = B.slot * s, h = B.slot * s,
        }
    end

    local trackLeft = canvasW - (R.right + R.w) * s
    local trackTop = canvasH * 0.5 - R.h * 0.5 * s
    self.track = { x = trackLeft, y = trackTop, w = R.w * s, h = R.h * s }
    self.inner = {
        x = trackLeft + R.pad * s, y = trackTop + R.pad * s,
        w = R.innerW * s, h = R.innerH * s,
    }
    self.barPx = R.barH * s
    self.markPx = R.mark * s

    self.cardRect = {
        x = canvasW * 0.5 - C.w * 0.5 * s, y = canvasH * 0.5 - C.h * 0.5 * s,
        w = C.w * s, h = C.h * s,
    }
    self.closeRect = {
        x = canvasW * 0.5 + C.w * 0.5 * s - C.close * s,
        y = canvasH * 0.5 - C.h * 0.5 * s, w = C.close * s, h = C.close * s,
    }
end

local function inside(r, x, y)
    return x >= r.x and x <= r.x + r.w and y >= r.y and y <= r.y + r.h
end

--- 返回 "card"（卡片开着，整屏吞掉）、"close"、"reel"、"slot{i}"，否则 nil（交给世界）
--- 顺序即优先级：拉杆中整屏都是拖拽面（竖条只有 68dp 宽，要求按住它等于逼玩家
--- 盯着小条条操作），此时背包格子不该再抢同一根手指。
function Hud:HitTest(x, y)
    if self.showCard then
        return inside(self.closeRect, x, y) and "close" or "card"
    end
    if self.showReel then
        return "reel"
    end
    for i = 1, Config.SLOT_COUNT do
        if inside(self.slotRects[i], x, y) then
            return "slot" .. (i - 1)
        end
    end
    return nil
end

--- 画布 Y → 绿区中心（0..1，超出内槽范围会被 Reel 夹住）
--- 抬升 0.35×绿区高：让绿块中心比手指高一点，别被指头挡住。
function Hud:ZoneCenterFromCanvasY(y)
    local lift = self.barPx * 0.35 / math.max(self.inner.h, 1.0)
    return (y - self.inner.y) / math.max(self.inner.h, 1.0) - lift
end

----------------------------------------------------------------------
-- 显示状态
----------------------------------------------------------------------

--- 背包内容变了就整体推一遍（六格，成本可忽略），只 MarkDirty 真变过的字段
function Hud:SyncInventory()
    for i = 0, Config.SLOT_COUNT - 1 do
        local slot = self.slots[i]
        local rec = self.inv:Slot(i + 1)
        local has = rec ~= nil
        local kind = has and SPECIES_KEY[Config.FISH[rec.fish].id] or nil
        local n = has and tostring(rec.count) or ""
        local changed = slot.has ~= has or slot.kind ~= kind or slot.n ~= n
        if changed then
            slot.has, slot.kind, slot.n = has, kind, n
            self:Dirty("s" .. i .. "has")
            self:Dirty("s" .. i .. "n")
            for _, key in pairs(SPECIES_KEY) do
                self:Dirty("s" .. i .. key)
            end
        end
    end
end

function Hud:Toast(text, duration)
    self.toast = text or ""
    self.showToast = text ~= nil and text ~= ""
    self.toastT = duration or 2.2
    self:Dirty("showToast")
    self:Dirty("toast")
end

function Hud:SetHint(text)
    text = text or ""
    if text == self.hint then
        return
    end
    self.hint = text
    self:Dirty("hint")
end

function Hud:ReelVisible(on)
    if self.markParentOn == on then
        return
    end
    self.markParentOn = on
    self.bar:SetEnabled(on and true or false)
    self.mark:SetEnabled(on and true or false)
end

function Hud:ShowReel(on, fishIdx)
    if self.showReel ~= on then
        self.showReel = on
        self:Dirty("showReel")
    end
    self:ReelVisible(on)
    if on then
        local kind = SPECIES_KEY[Config.FISH[fishIdx].id]
        if kind ~= self.markKind then
            self.markKind = kind
            if self.markW then
                for _, key in pairs(SPECIES_KEY) do
                    self.markW:MarkDirty("mark" .. key:upper())
                end
            end
        end
        self.segOn = math.floor(Config.REEL_START * SEGMENTS + 0.5)
        self:DirtySegs()
    end
end

--- 每帧把拉杆物理量映射成像素（归一化 0..1 → 内槽矩形）
function Hud:DrawReel(reel)
    local top01 = reel:ZoneRect()
    self.bar:SetPosition(Vector2(self.inner.x, self.inner.y + top01 * self.inner.h))
    -- 鱼标记按中心对齐归一化位置，再夹进内槽：0/1 两端图标不许被裁掉一半
    local innerH = math.max(self.inner.h, 1.0)
    local markTop = clamp(reel:Fish01() * innerH - self.markPx * 0.5, 0.0, math.max(innerH - self.markPx, 0.0))
    self.mark:SetPosition(Vector2(
        self.inner.x + (self.inner.w - self.markPx) * 0.5,
        self.inner.y + markTop))
    self:SetProgress(reel:Progress())
end

function Hud:SetProgress(p)
    local on = clamp(math.floor(p * SEGMENTS + 0.5), 0, SEGMENTS)
    if on ~= self.segOn then
        self.segOn = on
        self:DirtySegs()
    end
end

function Hud:DirtySegs()
    for s = 0, SEGMENTS - 1 do
        self:Dirty("seg" .. s)
    end
end

function Hud:ShowCard(fishIdx, count, maxW)
    local f = Config.FISH[fishIdx]
    if not f then
        return
    end
    self.cardName = f.name
    self.cardDesc = string.format("共 %d 条，最大 %.1f 斤。%s", count or 1, maxW or 0, f.flavor or "")
    self.cardKind = SPECIES_KEY[f.id]
    self.showCard = true
    self:Dirty("showCard")
    self:Dirty("cardName")
    self:Dirty("cardDesc")
    for _, key in pairs(SPECIES_KEY) do
        self:Dirty("card" .. key:upper())
    end
end

function Hud:HideCard()
    if not self.showCard then
        return
    end
    self.showCard = false
    self:Dirty("showCard")
end

function Hud:IsCardOpen()
    return self.showCard
end

function Hud:Update(dt)
    if self.toastT > 0.0 then
        self.toastT = self.toastT - dt
        if self.toastT <= 0.0 and self.showToast then
            self.showToast = false
            self:Dirty("showToast")
        end
    end
end

return Hud

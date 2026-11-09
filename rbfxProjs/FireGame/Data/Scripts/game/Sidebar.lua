-- Scripts/game/Sidebar.lua
--
-- 抖音侧边栏复访（平台必接合规，2023-11-24 起不接直接驳回）：识别进入场景 +
-- 老用户回归奖励 + 新用户引导，三件事都在这里。
--
-- 链路分工（与 Voice 同一模式）：
--   * 引擎侧（MinigameSDK）只发请求收事件——GetLaunchInfo / NavigateToSidebar，
--     结果经 "MinigameSdkResult" 事件回来（data.Kind 对照 MINIGAME_REQUEST_KIND 表）。
--   * 宿主 JS 侧（minigame_sdk.js）管 scene 判定（抖音侧边栏 scene=021236）、
--     sidebarSupported 能力位与 guideDone 持久化（宿主 storage），payload 形如
--     {"sidebar":true,"guideDone":false,"sidebarSupported":true}，
--     JSON 布尔用 string.find 认领（同 Voice 的做法，不引 JSON 库）。
--
-- 行为：
--   * 冷启动查一次场景。从侧边栏进入 → 本会话增益（攻击 xSIDEBAR_DMG_MULT、
--     生命上限 xSIDEBAR_HP_MULT，Waves/Shooting 各自乘）+ 顶部旁白播报；
--     提示每会话只发一次（防 onShow 重查反复刷旁白），数值整会话有效。
--   * 非侧边栏首次进入 → 引导弹窗。弹窗等开始画面收起再出（两层全屏模态叠着
--     只会更黑更吵）；玩家点过「去侧边栏」或「知道了」后宿主 storage 记 guideDone，
--     以后再也不弹（本会话也即时关门，防 storage 写失败时循环打扰）。
--   * 宿主 onShow（引擎 "MinigameShow" 事件）后重查一次：玩家从侧边栏面板点回
--     游戏（进程保活、不冷启动）也走 onShow options.scene，这条路也能领到奖励。
--   * 编辑器/桌面宿主没有 SDK → 整个模块静默（增益恒 1.0、引导不弹）；
--     微信宿主没有侧边栏（回包 sidebarSupported:false）→ 同样整个模块静默，
--     不弹抖音专属引导、不发增益。要在编辑器里验证两条路径用 Config.DEBUG_SIDEBAR。
--
local Config = require("game.Config")
local Tracker = require("game.Tracker")

---@class Sidebar 侧边栏复访：进入场景识别 + 回归奖励 + 引导
local Sidebar = {}
Sidebar.__index = Sidebar

-- 增益查询口：Waves（生命上限）与 Shooting（每发伤害）各调一次。
-- 模块单例未建（编辑器异常加载顺序）或增益未生效时恒 1.0，乘了等于没乘。
function Sidebar.DmgMult()
    return (Sidebar.instance and Sidebar.instance.buff) and Config.SIDEBAR_DMG_MULT or 1.0
end

function Sidebar.HpMult()
    return (Sidebar.instance and Sidebar.instance.buff) and Config.SIDEBAR_HP_MULT or 1.0
end

---@param scene Scene
---@param deps { hud: Hud|nil, getWaves: fun():Waves|nil }|nil
---@return Sidebar
function Sidebar.Build(scene, deps)
    local self = setmetatable({}, Sidebar)
    Sidebar.instance = self
    self.hud = deps and deps.hud
    self.getWaves = deps and deps.getWaves
    self.sdk = GetSubsystem("MinigameSDK")

    self.buff = false           -- 侧边栏回归增益是否生效（会话级，重入播报也由它挡）
    self.narratePending = false -- 增益到账时开始画面还盖着 → 改到收起后再播
    self.pendingGuide = false   -- 查到要引导但开始画面还盖着 → 等收起再弹
    self.guideMarked = false    -- 本会话已引导过（宿主 storage 写失败时的兜底闸）
    self.requestInFlight = false

    SubscribeToEvent("MinigameSdkResult", function(data)
        self:OnSdkResult(data)
    end)
    -- 宿主回前台（含从侧边栏面板点回来）：重查场景。真机才有此事件，编辑器不触发。
    SubscribeToEvent("MinigameShow", function(_data)
        self:Query(false)
    end)

    -- 编辑器调试模拟（Config.DEBUG_SIDEBAR）：真机链路走不到这里。
    local dbg = Config.DEBUG_SIDEBAR
    if dbg == "reward" then
        self:GrantBuff()
    elseif dbg == "guide" then
        self.pendingGuide = true
    end

    self:Query(false)
    return self
end

---@return boolean 宿主 SDK 是否可用（编辑器里为 false，整条链路静默）
function Sidebar:IsAvailable()
    return self.sdk ~= nil and self.sdk:IsAvailable()
end

--- 发一次场景查询（mark=true 时顺带持久化「引导已做」）。在途时静默忽略：
--- 回包自带全量状态，旧查询的回答与新一轮意图一致，丢一次无害。
---@param mark boolean|nil
function Sidebar:Query(mark)
    if not self:IsAvailable() or self.requestInFlight then
        return
    end
    self.requestInFlight = true
    self.sdk:GetLaunchInfo(mark or false)
end

--- MinigameSdkResult 事件分发（BEGINFRAME 边界）：GET_LAUNCH_INFO 的回包裁决。
function Sidebar:OnSdkResult(data)
    if data.Kind ~= MINIGAME_REQUEST_KIND.GET_LAUNCH_INFO then
        return
    end
    self.requestInFlight = false
    if not data.Success then
        -- 桌面/编辑器（无宿主）也走这里：静默即可，不刷屏。
        return
    end
    local payload = tostring(data.Payload or "")
    -- 宿主没有侧边栏（微信等）：整条链路静默——不弹抖音专属引导，增益也永不生效。
    -- 旧版宿主 JS 的回包没有这个字段，缺省按「支持」处理，抖音侧行为不变。
    if string.find(payload, '"sidebarSupported":false', 1, true) then
        return
    end
    local fromSidebar = string.find(payload, '"sidebar":true', 1, true) ~= nil
    local guideDone = string.find(payload, '"guideDone":true', 1, true) ~= nil
    if fromSidebar then
        self:GrantBuff()
    elseif not guideDone and not self.guideMarked then
        -- 引导等开始画面收起再弹；这里只记意图。
        self.pendingGuide = true
    end
end

--- 开始画面收起（main.lua 的 onStart 回调）后调：把攒下的引导/奖励播报放出来。
function Sidebar:OnGameStart()
    if self.narratePending then
        self.narratePending = false
        self:AnnounceBuff()
    end
    if self.pendingGuide then
        self.pendingGuide = false
        if self.hud and self.hud.OpenSidebarGuide then
            self.hud:OpenSidebarGuide()
        end
    end
end

--- 侧边栏回归增益到账（数值立即生效，播报看开始画面状态）。重入静默：
--- 增益是会话级布尔，第二次调用只可能来自 onShow 重查，数值早已在账上。
function Sidebar:GrantBuff()
    if self.buff then
        return
    end
    self.buff = true
    Tracker.Report("sidebar_revisit", { dmg = Config.SIDEBAR_DMG_MULT, hp = Config.SIDEBAR_HP_MULT })
    -- 生命增益即时落到当前局：血量唯一真相在 waves.playerHp/playerMaxHp。注意当前局的
    -- playerMaxHp 是增益生效【前】算的（冷启动时 SDK 结果最早下一帧回来，Waves.Reset 早跑完），
    -- 所以这里直接按新上限重算两个字段；后续 Waves:Reset 会经 Sidebar.HpMult() 带上倍率。
    -- 只在满血时抬（冷启动 boot 时必然满血，这是主路径）；局中掉着血回来不白补，下一局
    -- Reset 自然生效——行为可解释，也不给「掉血时切侧边栏白赚回复」留空子。
    local waves = self.getWaves and self.getWaves()
    if waves and waves.playerMaxHp and waves.playerHp == Config.PLAYER_HP then
        waves.playerMaxHp = Config.PLAYER_HP * Config.SIDEBAR_HP_MULT
        waves.playerHp = waves.playerMaxHp
    end
    -- 播报：开始画面盖着会被全屏遮罩吃掉（DOM 在前），改到收起后再播。
    if self.hud and self.hud.IsStartModal and self.hud:IsStartModal() then
        self.narratePending = true
    else
        self:AnnounceBuff()
    end
end

function Sidebar:AnnounceBuff()
    if self.hud and self.hud.Narrate then
        self.hud:Narrate("侧边栏回归奖励：攻击 +20% 生命 +25%", 4.0)
    end
end

--- 引导弹窗「去侧边栏」（main.lua 的 onSidebarGo 转发）：拉起宿主侧边栏面板 +
--- 持久化已引导。面板拉起后游戏退后台，玩家从侧边栏点回来走 onShow 重查发奖。
function Sidebar:GoSidebar()
    Tracker.Report("sidebar_guide_go", {})
    self:MarkGuideDone()
    if self.hud and self.hud.CloseSidebarGuide then
        self.hud:CloseSidebarGuide()
    end
    if self:IsAvailable() then
        self.sdk:NavigateToSidebar()
    end
end

--- 引导弹窗「知道了」（main.lua 的 onSidebarClose 转发）：只持久化 + 关面板。
function Sidebar:DismissGuide()
    Tracker.Report("sidebar_guide_close", {})
    self:MarkGuideDone()
    if self.hud and self.hud.CloseSidebarGuide then
        self.hud:CloseSidebarGuide()
    end
end

--- 记「引导已做」：宿主 storage 持久化 + 本会话闸（storage 写失败时也不重复弹）。
--- 不走 Query 的在途闸：mark 这一次丢了不是无害（下次冷启动还会再弹引导）。
function Sidebar:MarkGuideDone()
    self.guideMarked = true
    if not self:IsAvailable() then
        return
    end
    self.sdk:GetLaunchInfo(true)
end

return Sidebar

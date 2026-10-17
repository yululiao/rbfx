-- Scripts/game/HpBar.lua
--
-- 主角头顶的 3 格血条：一份 RmlUI 文档渲染进离屏纹理，贴在一个跟随主角的 3D 面板节点上。
-- 走的是引擎原生的"UI 挂 3D 节点"通路（107_HelloRmlUI 里贴旋转方块的同一条），不是把世界
-- 坐标投影成屏幕坐标再摆 UI——差别在于这块面板现在是场景里的一个 Drawable：跟着节点变换走、
-- 进八叉树裁剪、参与深度，以后你自己摆场景时它和关卡几何是同一套坐标语言。
--
-- 三条硬约束（都是踩过或读过源码才定下来的）：
--   * 同一个 Node 上必须同时有 RmlCanvasComponent 与 LuaRmlUIComponent。RmlUIComponent
--     在 OnNodeSet/Enable 里调 UpdateConnectedCanvas 找兄弟 RmlCanvasComponent，找到就把
--     文档 Close 再 Open 到那块离屏 UI 上——屏幕那份随之消失，所以不会画双份。
--   * SetHandlers 必须在 SetResource 之前：onInit 只在数据模型构造的那一瞬间回调，
--     那是唯一能 Bind* 的时机（与 Hud.lua / CamDebug.lua 同一套时序）。
--   * 面板是平面几何，必须朝向相机，否则从俯视相机看下去就成了一条边。这里用
--     camRot * RotX(-90)：Plane.mdl 躺在局部 XZ 上、法线 +Y，绕局部 X 轴转 -90 后法线
--     落到 -Z_cam（正对相机），同时原来的局部 Z 抬成面板的"竖直方向"，所以节点缩放的
--     y 分量给不出高度，高度要写在 z 分量上（见下方 SetScale 的注释）。

local Config = require("game.Config")

---@class HpBar 主角头顶血条（3D 世界面板 + RmlUI 离屏文档）
local HpBar = {}
HpBar.__index = HpBar

--- 把当前血量换算成三格的显隐。掉血从右往左灭，左端那格最后没。
local function PipsOf(cur)
    return cur >= 1, cur >= 2, cur >= 3
end

---@param scene Scene
---@param deps { cameraNode: Node, player: Player, getHp: fun():(number, number) }
---@return HpBar
function HpBar.Build(scene, deps)
    local self = setmetatable({}, HpBar)
    self.cameraNode = deps.cameraNode
    self.player = deps.player
    self.getHp = deps.getHp

    local c = Config.HP_BAR
    self.pip1, self.pip2, self.pip3 = PipsOf(Config.PLAYER_HP)
    self.visible = true       -- 面板整块在不在（归零/阵亡时摘掉），免得与 pip 状态混在一起
    self.w = nil

    -- 面板节点：位置每帧同步到主角头顶，朝向每帧对齐相机（相机姿态现在恒定，写成每帧
    -- 是为了以后相机改成可旋转时这里不用改）。
    local node = scene:CreateChild("PlayerHpBar")
    -- Plane.mdl 是单位 1x1 的水平面，所以缩放的两个可用轴是 x（宽）与 z（旋转后的高度）；
    -- y 是面板厚度方向，转 90 度后指向前后，取 1 即可（取小数会连带影响包围盒但没有视觉作用）。
    node:SetScale(Vector3(c.worldW, 1.0, c.worldH))
    self.node = node

    local model = node:CreateComponent("StaticModel")
    -- 几何与材质在下面那段一起装配：贴图要用到画布纹理，两件是同一次装配的产物，拆开写反而要绕回去

    -- 离屏画布：自己开一份 RmlUI 实例，把文档渲染进它那张纹理。血条不接收点击，
    -- 所以关掉输入重映射（RmlCanvasComponent 的 remap 只对兄弟 StaticModel 生效，
    -- 但血条本来就盖在画面中央，宁可显式关掉）。
    local canvas = node:CreateComponent("RmlCanvasComponent")
    canvas:SetUISize(IntVector2(c.texW, c.texH))
    canvas:SetRemapMousePos(false)
    canvas:SetClearColor(Color(0.0, 0.0, 0.0, 0.0))

    -- 材质：无光照 + alpha 混合，纹理就是上面那张画布的产物。不投影（血条不该在地上
    -- 拖出一块方影子——海盗的投影已经因为 drawcall 关过一轮，这里没理由再添一笔）。
    local material = Material()
    local tech = GetResource("Technique", c.technique)
    local plane = GetResource("Model", Config.PLANE_MODEL)
    local canvasTex = canvas:GetTexture()
    model:SetModel(plane)
    material:SetTechnique(0, tech)
    material:SetTexture("Albedo", canvasTex)
    -- 双面：血条是平面，朝向符号（那个 ±90）一旦错就会因背面剔除而整块看不见。
    -- 平面平行于屏幕时正反两面观感一致，所以这不是妥协；引擎自己给 3D UI（Text3D/
    -- 天空盒/Urho2D）也是统一 CULL_NONE。注意 Lua 侧枚举表叫 CULL（CULL.NONE），
    -- 写成 C++ 宏名 CULL_NONE 在 Lua 里是 nil，SetCullMode 会直接抛错中断装配。
    material:SetCullMode(CULL.NONE)
    model:SetMaterial(material)
    model:SetCastShadows(false)

    local comp = node:CreateComponent("LuaRmlUIComponent")
    comp:SetHandlers({
        onInit = function(w)
            self.w = w
            w:BindReadonlyProperty("pip1", function() return self.pip1 end)
            w:BindReadonlyProperty("pip2", function() return self.pip2 end)
            w:BindReadonlyProperty("pip3", function() return self.pip3 end)
        end,
    })
    comp:SetResource("UI/HpBar.rml")
    return self
end

--- 每帧跟一次位置/朝向/血量。必须在 player:Update 与 juice:EndFrame 之后调，
--- 否则用的是上一帧的相机姿态，震屏期间血条会滞后一帧抖动。
---@param dt number
function HpBar:Update(dt)
    local player = self.player
    local hp = self.getHp()

    -- 阵亡或血量归零：整块摘掉。只把三格灭掉的话，头顶会留一条常驻暗槽挂在尸体上方，
    -- 而结算面板这时候已经占了屏，那条暗槽没有意义。
    local visible = hp > 0 and not player.dying
    if visible ~= self.visible then
        self.visible = visible
        self.node:SetEnabled(visible)
    end
    if not visible then
        return
    end

    -- 位置取节点世界坐标（不是 player.spot）：主角登船后人是被船带着走的，
    -- spot 不反映船体抬升，血条会掉到甲板下面去。
    local wp = player.node:GetWorldPosition()
    self.node:SetPosition(wp + Vector3(0.0, Config.HP_BAR.overhead, 0.0))
    self.node:SetRotation(self.cameraNode:GetRotation() * Quaternion(-90.0, Vector3(1.0, 0.0, 0.0)))

    local p1, p2, p3 = PipsOf(hp)
    if p1 ~= self.pip1 or p2 ~= self.pip2 or p3 ~= self.pip3 then
        self.pip1, self.pip2, self.pip3 = p1, p2, p3
        self:Dirty("pip1")
        self:Dirty("pip2")
        self:Dirty("pip3")
    end
end

--- 安全 MarkDirty：文档还没构造完（onInit 未跑）时跳过，值已经在 self 上等着。
---@param name string
function HpBar:Dirty(name)
    if self.w then
        self.w:MarkDirty(name)
    end
end

return HpBar

-- Scripts/game/Player.lua
--
-- 主角 + 俯视角相机（王者荣耀式）。主角是 Quaternius_Player 的 Character_Soldier，站在沙滩上
-- 用左摇杆四下走动（屏幕绝对方向：上=朝海、右=+x，矩形边界内活动）。全自动开火。
-- 相机在主角斜上方以高俯角俯瞰，固定屏幕上方=朝海，不随角色转向而旋转。
--
-- 朝向与姿态分三层管，别混：
--   身体朝向 nodeYaw —— 移动时朝移动方向（Move 里平滑转），站定且在开火窗口内朝目标（Update 里转）；
--   上半身扭转 aimTwist_ —— 身体与目标方向的残差由 Abdomen/Torso 两节骨骼分摊扭转（腿不动、
--       枪口指向敌人），窗口外平滑回正；施加时机见 ApplyAim；
--   动画状态机 animState_ —— idle / idle_shoot / run / run_shoot 四态，见文件顶部 STATE_ANIM。
--
-- 朝向约定：
--   Soldier 模型原生朝 -z。nodeYaw=0 时主角面朝海面。相机永远在 +z 侧上方看回主角。

local Config = require("game.Config")
local Materials = require("game.Materials")

---@class Player 主角与俯视角相机的运行时状态
local Player = {}
Player.__index = Player

local function clamp(v, lo, hi)
    if v < lo then
        return lo
    elseif v > hi then
        return hi
    end
    return v
end

-- 最短角差 (b-a)，归一到 (-pi, pi]。Lua 的 % 对负数返回正，d 落在 [0,2pi)。
local function angDiff(a, b)
    local d = (b - a) % (2 * math.pi)
    if d > math.pi then
        d = d - 2 * math.pi
    end
    return d
end

----------------------------------------------------------------------
-- 动画状态机：四态
--   idle / idle_shoot / run / run_shoot
-- 输入 = (是否移动, 是否处于开火窗口)，驾船时恒 idle。
-- 映射动画栈：Idle / Idle_Shoot / Run / Run_Gun。
-- 全文件只有 SetAnimState 一处碰动画：状态跳变才 PlayExclusive。
--（旧版是 PlayShoot 每枪重播 Idle_Shoot 顶掉 Run_Gun、开火窗口结束又没人恢复，
--  多入口抢播就是“边跑边僵直”的根源。）
----------------------------------------------------------------------
local STATE_ANIM = {
    idle       = "Idle",
    idle_shoot = "Idle_Shoot",
    run        = "Run",
    run_shoot  = "Run_Gun",
}

--- 给 Soldier 上材质。导入器已把 FBX 材质色烘进顶点色（FbxImporter 的
--- FbxBakeMaterialColors：所有材质槽都无 diffuse 贴图的 mesh，每段顶点色 =
--- 材质 Diffuse 色），因此全身一个顶点色材质即可，显示 Quaternius 原厂纯色
--- 设计（Skin 肤色、Character_Main 军绿、Grey/Black 装备……）。
--- 例外：FBX 里十余件武器道具（Revolver/Sniper/Pistol/SMG/Shotgun/
--- RocketLauncher/Shovel/Knife 等）全部绑在右手食指链下同一位置堆叠，
--- 全显出来就是一身兵器库。只留一把 AK（GEOM46-49，与全自动开火最配），
--- 其余枪械、铲子、两把刀全部藏掉。段号来自 mdl 内嵌的 geometry 名
--- （AssetImporter 的 SetGeometryName 写入“AK #0”等，二进制可直接读出），
--- 并与顶点色指纹交叉验证（VB46-49 = Grey/DarkGrey/Grey2/Wood 恰为
--- AK 四槽材质）。
---@param model AnimatedModel
local function ApplyBodyMaterials(model)
    model:SetMaterial(Materials.VertexColor())
    -- 藏 8..45（AK 之前的全部枪械）与 50..60（铲子 + 两把刀），保留
    -- 0..7（身体/肩垫）、46..49（AK）、61..66（Body 躯干）
    local hidden = Materials.Hidden()
    for i = 8, 45 do
        model:SetMaterial(i, hidden)
    end
    for i = 50, 60 do
        model:SetMaterial(i, hidden)
    end
end

--- 在 Coast 搭好的阵地上生成主角，并接管场景里的 Camera Rig 做俯视相机。
---@param scene Scene
---@param spot Vector3 主角脚底世界坐标（Coast.Build 返回的 playerSpot）
---@param playerBoat Node|nil 玩家专属船节点（载具），由 main.lua 从 Coast 传入
---@return Player
function Player.Build(scene, spot, playerBoat)
    local self = setmetatable({}, Player)

    -- 主角落地在沙滩上可走动（不再是塔上固定站位）；忽略传入的平台 spot，用配置的地面出生点
    spot = Config.PLAYER_SPAWN
    self.groundY = spot.y

    -- 主角节点
    local node = scene:GetChild("Player", true)
    if not node then
        node = scene:CreateChild("Player")
    end
    node:SetPosition(spot)
    node:SetRotation(Quaternion(0.0, Vector3.UP))
    -- noShoot：Shooting 的射线打到自己/塔面时要穿过去续查，靠这个变量识别（见 Shooting.lua）
    node:SetVar("noShoot", true)

    local model = node:GetComponent("AnimatedModel")
    if not model then
        -- 复用场景里旧的 Player 节点时，它可能挂着一个 StaticModel（占位球），
        -- RemoveComponent 收的是类型名字符串而不是组件对象，先探再删。
        if node:GetComponent("StaticModel") then
            node:RemoveComponent("StaticModel")
        end
        model = node:CreateComponent("AnimatedModel")
    end
    model:SetModel(GetResource("Model", Config.PlayerModel()))
    ApplyBodyMaterials(model)
    -- 远处降频用不上（主角永远在近处），但显式关掉不可见时更新，省一点蒙皮开销
    model:SetUpdateInvisible(false)

    -- 上半身扭转用的骨骼节点：SetModel 时引擎把骨架建成真实子节点
    -- （AnimatedModel::SetSkeleton 逐骨 CreateChild，层级 Root→Hips→Abdomen→Torso→Neck…），
    -- 扭 Abdomen+Torso 两节即“腰以上带枪转向敌人”，腿不动。
    local aimBoneNodes = {}
    for _, boneName in ipairs({ "Abdomen", "Torso" }) do
        local boneNode = node:GetChild(boneName, true)
        if boneNode then
            table.insert(aimBoneNodes, boneNode)
        else
            print("FireGame MARK aim bone missing: " .. boneName)
        end
    end

    local anim = node:GetComponent("AnimationController")
    if not anim then
        anim = node:CreateComponent("AnimationController")
    end

    -- 相机：复用场景里编辑器摆的 Camera Rig（有 Camera 组件与既定 viewport），
    -- 没有就自己建一个，保证 Lua 单独跑也有画面。
    local cameraNode = scene:GetChild("Camera Rig", true)
    if not cameraNode then
        cameraNode = scene:CreateChild("Camera Rig")
    end
    local camera = cameraNode:GetComponent("Camera")
    if not camera then
        camera = cameraNode:CreateComponent("Camera")
    end
    camera:SetFov(Config.CAMERA_FOV)
    camera:SetFarClip(Config.CAMERA_FAR_CLIP)
    cameraNode:SetVar("noShoot", true)
    SetViewport(0, scene, camera)

    self.node = node
    self.model = model
    self.anim = anim
    self.cameraNode = cameraNode
    self.camera = camera
    self.spot = spot
    self.aimBoneNodes_ = aimBoneNodes

    -- 状态机与射击姿态输入
    self.animState_ = nil        -- 当前动画状态（nil 保证首次判定必然触发播放）
    self.moving_ = false         -- 摇杆是否在驱动走位（驾船时恒 false，动画恒 idle）
    self.fireT_ = 0.0            -- 开火窗口倒计时：窗口内姿态用 *_shoot、上半身朝目标扭
    self.moveYaw_ = 0.0          -- 移动方向（弧度）
    self.aimYaw_ = 0.0           -- 最近一次开火的目标方向（弧度）
    self.aimValid_ = false
    self.aimTwist_ = 0.0         -- 上半身当前扭转量（弧度，平滑值）

    self.aim = 0.0                              -- 度，正=右转（俯视角下固定 0，不再动态变化）
    self.pitch = Config.CAMERA_PITCH            -- 度，相机俯角（俯视约 55）
    self.kick = 0.0                             -- 开火相机微小震动（度），指数回落
    self.facing = Vector3(0.0, 0.0, -1.0)
    self.nodeYaw = 0.0                          -- 人物自身航向（弧度），朝目标转；相机不用它

    self.scene = scene
    self.boatNode = playerBoat                  -- 玩家专属船节点（Coast 建，main 传入）
    self.inBoat = false                         -- 载具状态：false=步行，true=驾船
    self.boatYaw = 0.0                          -- 船航向（弧度），0=朝海(-z)
    self.gunSide_ = -1                          -- 船上双炮轮换：每次开火翻到另一舷（+1/-1）

    self:SetAnimState("idle")                   -- 进游戏默认待机
    self:UpdateCamera(0.0)
    return self
end

--- 记录开火目标方向。不做瞬移转身：站定时身体在 Update 里平滑转向它，
--- 移动中身体保持移动方向，残差交给上半身扭转（ApplyAim）补齐。
---@param worldPos Vector3
function Player:SetAimTarget(worldPos)
    -- 船上：保持行驶朝向，子弹方向由炮位→目标自行计算，不参与扭转
    if self.inBoat then
        return
    end
    local dx = worldPos.x - self.spot.x
    local dz = worldPos.z - self.spot.z
    if dx * dx + dz * dz < 1e-4 then
        return
    end
    self.aimYaw_ = math.atan(dx, -dz)
    self.aimValid_ = true
end

--- 开火后座：给相机一个瞬时上抬，随后 Update 里指数回落。
function Player:AddKick()
    self.kick = self.kick + Config.CAMERA_KICK
end

--- 左摇杆走位：屏幕绝对方向（上=朝海 -z，右=+x），限制在可行走矩形内。
--- 俯视角下相机固定不转，所以方向不依赖任何相机基准。
---@param stickX number -1..1，右为正
---@param stickY number -1..1，屏幕下为正（=朝岸 +z）
---@param dt number
function Player:Move(stickX, stickY, dt)
    -- 驾船分支：摇杆驱动船在海域矩形内移动，船朝移动方向缓慢转向（模拟排水惯性）
    if self.inBoat then
        self.moving_ = false    -- 人坐船不动，动画状态机恒 idle
        local boatMoving = (stickX * stickX + stickY * stickY) > 0.0001
        if boatMoving then
            local dirX = stickX
            local dirZ = stickY   -- stickY正=屏幕下=朝岸(+z)
            local len = math.sqrt(dirX * dirX + dirZ * dirZ)
            if len > 1.0 then dirX, dirZ = dirX / len, dirZ / len end

            local nx = clamp(self.spot.x + dirX * Config.PLAYER_BOAT_SPEED * dt, Config.SEA_MIN_X, Config.SEA_MAX_X)
            local nz = clamp(self.spot.z + dirZ * Config.PLAYER_BOAT_SPEED * dt, Config.SEA_MIN_Z, Config.SEA_MAX_Z)
            self.spot = Vector3(nx, Config.SEA_Y, nz)
            self.boatNode:SetPosition(self.spot)

            -- 船朝移动方向转，速率比人物慢，做出惯性
            local step = Config.PLAYER_BOAT_TURN * dt
            local d = angDiff(self.boatYaw, math.atan(dirX, -dirZ))
            self.boatYaw = self.boatYaw + clamp(d, -step, step)
        end
        -- Ship_Small 长轴沿模型原生 x，Coast 以 Config.BOAT_YAW 落位；boatYaw=0 时旋转回落到 Config.BOAT_YAW
        -- 与出生姿态连续（不突跳）。若发现船是“倒着开”（船尾朝行驶方向），翻转本式的符号即可。
        self.boatNode:SetRotation(Quaternion(Config.BOAT_YAW - math.deg(self.boatYaw), Vector3.UP))
        return
    end

    local moving = (stickX * stickX + stickY * stickY) > 0.0001
    self.moving_ = moving
    if moving then
        -- 屏幕绝对方向：上推(stickY<0)=朝海(-z)，右推(stickX>0)=+x
        local dirX = stickX
        local dirZ = stickY   -- stickY正=屏幕下=朝岸(+z)
        local len = math.sqrt(dirX * dirX + dirZ * dirZ)
        if len > 1.0 then dirX, dirZ = dirX / len, dirZ / len end

        local nx = clamp(self.spot.x + dirX * Config.MOVE_SPEED * dt, Config.WALK_MIN_X, Config.WALK_MAX_X)
        local nz = clamp(self.spot.z + dirZ * Config.MOVE_SPEED * dt, Config.WALK_MIN_Z, Config.WALK_MAX_Z)
        self.spot = Vector3(nx, self.groundY, nz)
        self.node:SetPosition(self.spot)

        -- 身体朝移动方向平滑转（动画状态在 Update 的状态机里管，这里不碰动画）
        self.moveYaw_ = math.atan(dirX, -dirZ)
        local step = Config.TURN_SPEED * dt
        local d = angDiff(self.nodeYaw, self.moveYaw_)
        self.nodeYaw = self.nodeYaw + clamp(d, -step, step)
    end
end

--- 开火脉冲：延长开火窗口。窗口内状态机自动切 *_shoot 姿态、上半身朝目标扭，
--- 窗口结束自然回落。不在这里碰动画——动画只归 SetAnimState 一个出口管。
function Player:PlayShoot()
    self.fireT_ = Config.FIRE_WINDOW
end

--- 状态机唯一动画出口：状态跳变才 PlayExclusive（同层其余动画淡出），平时不动。
---@param state string idle|idle_shoot|run|run_shoot
function Player:SetAnimState(state)
    if self.animState_ == state then
        return
    end
    self.animState_ = state
    self.anim:PlayExclusive(Config.PlayerAnim(STATE_ANIM[state]), 0, true, 0.15)
end

--- 把 aimTwist_ 施加到上半身骨骼。必须在 SceneDrawableUpdateFinished 事件里调：
--- 那是动画已应用到骨骼、蒙皮尚未计算的官方自定义动画时点（Octree.cpp 原注释
--- "Custom animation (eg. IK) can be done at this point"）。动画每帧重写骨骼，
--- 这里的修改永远建立在当帧动画姿态之上，不会跨帧累积。
function Player:ApplyAim()
    if self.inBoat or math.abs(self.aimTwist_) < 0.001 then
        return
    end
    -- Abdomen、Torso 各扭一半：Torso 是 Abdomen 的子节点，前者的扭转会传导进后者的
    -- 世界旋转，依次再扭一半后，肩/臂/头合成完整 aimTwist_，腰部过渡自然。
    -- 符号：与节点朝向同约定——atan(dx,-dz) 系的角增加对应绕 UP 的旋转角减少。
    local half = Quaternion(-math.deg(self.aimTwist_ * 0.5), Vector3.UP)
    for _, boneNode in ipairs(self.aimBoneNodes_) do
        boneNode:SetWorldRotation(half * boneNode:GetWorldRotation())
    end
end

----------------------------------------------------------------------
-- 载具（玩家专属船）
----------------------------------------------------------------------

--- 登船：角色重父到船节点下“坐”在船中部，相机/射击后续用 self.spot（已同步为船位）。
--- 先重父再定变换：SetPosition 在重父后走局部坐标，角色才会跟着船走/转。
function Player:BoardBoat()
    if self.inBoat or not self.boatNode then return end
    self.inBoat = true
    self.node:SetParent(self.boatNode)
    self.node:SetPosition(Vector3(0.0, Config.BOAT_SEAT_Y, 0.0))   -- 坐在甲板高度（纯 Y 偏移，不受船 yaw 影响）
    self.node:SetRotation(Quaternion(0.0, Vector3.UP))
    self:SetAnimState("idle")       -- 登船后进入 idle（船上恒 idle）
    self.spot = self.boatNode:GetPosition()
end

--- 下船：角色回到场景根，落在船旁的岸上（落在可步行矩形之内）。
--- Scene 本身就是根 Node（Lua 绑定没有 GetRoot，直接挂回 scene）。
function Player:LeaveBoat()
    if not self.inBoat then return end
    self.inBoat = false
    self.node:SetParent(self.scene)
    local bp = self.boatNode:GetPosition()
    local x = clamp(bp.x + 3.0, Config.WALK_MIN_X, Config.WALK_MAX_X)
    local z = clamp(math.max(bp.z, Config.WALK_MIN_Z), Config.WALK_MIN_Z, Config.WALK_MAX_Z)
    self.spot = Vector3(x, self.groundY, z)
    self.node:SetPosition(self.spot)
    self.nodeYaw = 0.0
    self.node:SetRotation(Quaternion(0.0, Vector3.UP))
end

--- 角色与船的水平距离是否小于交互阈值（可登船）。
---@return boolean
function Player:IsNearBoat()
    if self.inBoat or not self.boatNode then return false end
    local bp = self.boatNode:GetPosition()
    local dx = self.spot.x - bp.x
    local dz = self.spot.z - bp.z
    return (dx * dx + dz * dz) < Config.BOAT_INTERACT_DIST * Config.BOAT_INTERACT_DIST
end

--- 射击原点（锁定/射程中心）：船上=船头中轴，步行=枪口。
---@return Vector3
function Player:GetFireOrigin()
    if self.inBoat then
        local f = Vector3(math.sin(self.boatYaw), 0.0, -math.cos(self.boatYaw))
        return self.spot + Vector3(0.0, 2.2, 0.0) + f * 2.5
    end
    return self:GetMuzzleWorld()
end

--- 实弹炮口（轮换）：船上时每次调用翻一舷，从左/右船头炮位轮流出一发；步行时就是枪口。
--- 与 GetFireOrigin 分开：锁定/射程用中心轴，子弹出膛用本函数的侧向炮位。
---@return Vector3
function Player:NextGunPos()
    if not self.inBoat then
        return self:GetMuzzleWorld()
    end
    self.gunSide_ = -self.gunSide_
    local f = Vector3(math.sin(self.boatYaw), 0.0, -math.cos(self.boatYaw))       -- 船头方向
    local r = Vector3(math.cos(self.boatYaw), 0.0, math.sin(self.boatYaw))        -- 左右舷（垂直于航向）
    return self.spot + Vector3(0.0, Config.BOAT_GUN_Y, 0.0)
        + f * Config.BOAT_GUN_FORWARD + r * (self.gunSide_ * Config.BOAT_GUN_LATERAL)
end

--- 每帧：开火窗口计时、站定时身体转向目标、上半身扭转平滑、衰减 kick、
--- 重算相机轨道位置与朝向、推进动画状态机。
---@param dt number 秒
function Player:Update(dt)
    -- 开火窗口倒计时
    if self.fireT_ > 0.0 then
        self.fireT_ = self.fireT_ - dt
        if self.fireT_ < 0.0 then
            self.fireT_ = 0.0
        end
    end

    -- 站定且在开火窗口内：身体平滑转向目标（移动中的转身由 Move 管）
    if not self.inBoat and not self.moving_ and self.fireT_ > 0.0 and self.aimValid_ then
        local step = Config.TURN_SPEED * dt
        local d = angDiff(self.nodeYaw, self.aimYaw_)
        self.nodeYaw = self.nodeYaw + clamp(d, -step, step)
    end

    -- 上半身扭转量（弧度）：窗口内补齐“身体朝向→目标方向”的残差（限幅），窗口外回正。
    -- 只在这里平滑，施加到骨骼在 ApplyAim（动画应用之后，见其注释）。
    local targetTwist = 0.0
    if not self.inBoat and self.fireT_ > 0.0 and self.aimValid_ then
        targetTwist = clamp(angDiff(self.nodeYaw, self.aimYaw_),
            -Config.AIM_MAX_TWIST, Config.AIM_MAX_TWIST)
    end
    local twistStep = Config.AIM_TURN_SPEED * dt
    self.aimTwist_ = self.aimTwist_ + clamp(targetTwist - self.aimTwist_, -twistStep, twistStep)

    if self.kick ~= 0.0 then
        -- 指数回落：kick *= e^(-decay*dt)，用一阶近似避免每帧 exp
        self.kick = self.kick - self.kick * Config.CAMERA_KICK_DECAY * dt
        if math.abs(self.kick) < 0.001 then
            self.kick = 0.0
        end
    end
    self:UpdateCamera(dt)

    -- 动画状态机（放最后，用当帧最新的 moving_/fireT_ 判定）
    local state
    if self.inBoat then
        state = "idle"                                    -- 驾船恒 idle
    elseif self.moving_ then
        state = (self.fireT_ > 0.0) and "run_shoot" or "run"
    else
        state = (self.fireT_ > 0.0) and "idle_shoot" or "idle"
    end
    self:SetAnimState(state)
end

--- 把俯视角相机放到主角斜上方，LookAt 主角位置。屏幕上方=朝海(-z)。
function Player:UpdateCamera()
    local pitchRad = math.rad(self.pitch + self.kick)
    local sinP, cosP = math.sin(pitchRad), math.cos(pitchRad)
    local dist = Config.CAMERA_DISTANCE

    -- 相机在主角 +z 后方上方（俯视时屏幕上方=朝海 -z）
    local target = self.spot + Vector3(0.0, Config.CAMERA_TARGET_Y, 0.0)
    local camPos = target + Vector3(0.0, dist * sinP, dist * cosP)

    self.cameraNode:SetPosition(camPos)
    -- 视线中心朝海(-z)前推 LOOK_AHEAD，看到更大范围战场而不是只盯脚下
    local lookAt = target + Vector3(0.0, 0.0, -Config.CAMERA_LOOK_AHEAD)
    self.cameraNode:LookAt(lookAt, Vector3.UP, TS.WORLD)

    -- 人物节点朝 nodeYaw（船上时人物是船的子节点，由船带动旋转，不单独设）。
    -- Quaternion(angle, axis) 的 angle 是“度”，而 nodeYaw 全程是弧度——必须 math.deg
    --（旧版直接塞弧度，视觉旋转被缩小 57 倍，人物几乎从不转身，等于一直朝前）。
    if not self.inBoat then
        self.node:SetRotation(Quaternion(-math.deg(self.nodeYaw), Vector3.UP))
    end
    -- facing 保持朝海（给旧接口 GetFacing 兼容）
    self.facing = Vector3(0.0, 0.0, -1.0)
end

--- 枪口世界坐标：胸口沿“身体朝向 + 上半身扭转”前探一点（开火窗口内即指向目标），
--- 给枪口光/子弹出膛/音效定位用，保证出弹方向与视觉一致。
---@return Vector3
function Player:GetMuzzleWorld()
    local yaw = self.nodeYaw + self.aimTwist_
    local f = Vector3(math.sin(yaw), 0.0, -math.cos(yaw))
    return self.spot + Vector3(0.0, 1.30, 0.0) + f * 0.7
end

---@return Vector3 当前水平朝向（单位向量，y=0）
function Player:GetFacing()
    return self.facing
end

---@return Node
function Player:GetNode()
    return self.node
end

---@return Camera
function Player:GetCamera()
    return self.camera
end

return Player

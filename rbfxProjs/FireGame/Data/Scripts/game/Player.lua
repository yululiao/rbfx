-- Scripts/game/Player.lua
--
-- 主角 + 俯视角相机（王者荣耀式）。主角是 Quaternius_Player 的 Character_Soldier，站在沙滩上
-- 用左摇杆四下走动（屏幕绝对方向：上=朝海、右=+x，矩形边界鈐制）。全自动开火时人物自动转向目标。
-- 相机在主角斜上方以高俯角俯瞰，固定屏幕上方=朝海，不随角色转向而旋转。
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

--- 逐 geometry 给 Soldier 上材质。全显示，一个都不藏。
---
--- Character_Soldier.mdl 有 67 个 geometry（GEOM i 用 VB i，1:1）。按 parse_mdl 实测的顶点语义分派：
---   0..7 与 61..66 带 TEXCOORD → 贴图集材质（头盔/头/靴等装备件）
---   8..45 与 50..52 带 UBYTE4_NORM:COLOR → 顶点色材质（躯干/衣服/四肢，身体主体）
---   46..49 与 53..60 两者皆无（皮肤/手持件）→ 也归贴图集，缺 UV 时采样图集角标，至少可见
---   空缓冲（0/2/3/26/33/55/62，无顶点）不上材质，SetMaterial 也无害。
---@param model AnimatedModel
local function ApplyBodyMaterials(model)
    local pirate = Materials.Pirate()
    local vcol = Materials.VertexColor()
    local total = model:GetModel() and model:GetModel():GetNumGeometries() or 0
    if total == 0 then
        model:SetMaterial(pirate)
        return
    end

    for i = 0, total - 1 do
        -- 身体主体是顶点色那批；其余（贴图集装备件 + 无 color 无 UV 的件）走贴图集
        local isVCol = (i >= 8 and i <= 45) or (i >= 50 and i <= 52)
        model:SetMaterial(i, isVCol and vcol or pirate)
    end
end

--- 在 Coast 搭好的阵地上生成主角，并接管场景里的 Camera Rig 做越肩相机。
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

    local anim = node:GetComponent("AnimationController")
    if not anim then
        anim = node:CreateComponent("AnimationController")
    end
    anim:Play(Config.PlayerAnim("Idle"), 0, true, 0.0)

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
    self.running_ = false
    self.shooting_ = false
    self.shootT_ = 0

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

    self:UpdateCamera(0.0)
    return self
end

--- 开火时把人物瞬间转向目标（相机不动，仍固定朝海）。王者式自动吸附下玩家无需手动对准，
--- 但人物要朝打的方向，看着才像自己在开枪。
---@param worldPos Vector3
function Player:FaceTowards(worldPos)
    -- 船上：保持行驶朝向，子弹方向由枪口→目标自行计算，不改船头指向
    if self.inBoat then
        return
    end
    local dx = worldPos.x - self.spot.x
    local dz = worldPos.z - self.spot.z
    if dx * dx + dz * dz < 1e-4 then
        return
    end
    self.nodeYaw = math.atan(dx, -dz)
end

--- 开火后座：给相机一个瞬时上抬，随后 Update 里指数回落。
function Player:AddKick()
    self.kick = self.kick + Config.CAMERA_KICK
end

--- 左摇杆走位：屏幕绝对方向（上=朝海 -z，右=+x），鈐在可行走矩形内。
--- 俯视角下相机固定不转，所以方向不依赖任何相机基准。
---@param stickX number -1..1，右为正
---@param stickY number -1..1，屏幕下为正（=朝岸 +z）
---@param dt number
function Player:Move(stickX, stickY, dt)
    -- 驾船分支：摇杆驱动船在海域矩形内移动，船朝移动方向缓慢转向（模拟排水惯性）
    if self.inBoat then
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

        -- 人物朝移动方向转
        local step = Config.TURN_SPEED * dt
        local d = angDiff(self.nodeYaw, math.atan(dirX, -dirZ))
        self.nodeYaw = self.nodeYaw + clamp(d, -step, step)
    end

    -- 移动/待机动画
    if moving and not self.running_ then
        self.anim:Play(Config.PlayerAnim("Run_Gun"), 0, true, 0.15)
        self.running_ = true
    elseif not moving and self.running_ then
        self.running_ = false
        if not self.shooting_ then
            self.anim:Play(Config.PlayerAnim("Idle"), 0, true, 0.15)
        end
    end
end

--- 开枪时切到持枪射击动画，播完自动回 Idle。
function Player:PlayShoot()
    self.anim:Play(Config.PlayerAnim("Idle_Shoot"), 0, true, 0.05)
    self.shooting_ = true
    self.shootT_ = 0.5
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
    self.running_ = false
    self.anim:Play(Config.PlayerAnim("Idle"), 0, true, 0.15)
    self.spot = self.boatNode:GetPosition()
end

--- 下船：角色回到场景根，落在船旁的岸上（落在可步行矩形之内）。
function Player:LeaveBoat()
    if not self.inBoat then return end
    self.inBoat = false
    self.node:SetParent(self.scene:GetRoot())
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

--- 每帧：衰减 kick + 重算相机轨道位置与朝向 + 同步 facing。
---@param dt number 秒
function Player:Update(dt)
    -- 开枪动画计时：播完回 Idle
    if self.shooting_ then
        self.shootT_ = self.shootT_ - dt
        if self.shootT_ <= 0 then
            self.shooting_ = false
            if not self.running_ then
                self.anim:Play(Config.PlayerAnim("Idle"), 0, true, 0.15)
            end
        end
    end
    if self.kick ~= 0.0 then
        -- 指数回落：kick *= e^(-decay*dt)，用一阶近似避免每帧 exp
        self.kick = self.kick - self.kick * Config.CAMERA_KICK_DECAY * dt
        if math.abs(self.kick) < 0.001 then
            self.kick = 0.0
        end
    end
    self:UpdateCamera(dt)
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

    -- 人物节点朝 nodeYaw（船上时人物是船的子节点，由船带动旋转，不单独设）
    if not self.inBoat then
        self.node:SetRotation(Quaternion(-self.nodeYaw, Vector3.UP))
    end
    -- facing 保持朝海（给旧接口 GetFacing 兼容）
    self.facing = Vector3(0.0, 0.0, -1.0)
end

--- 枪口世界坐标：主角胸口沿 facing 前探一点，给枪口光/枪口粒子/音效定位用。
---@return Vector3
function Player:GetMuzzleWorld()
    local f = Vector3(math.sin(self.nodeYaw), 0.0, -math.cos(self.nodeYaw))
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

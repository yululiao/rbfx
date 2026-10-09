-- Scripts/game/Player.lua
--
-- 主角 + 第三人称越肩相机。主角是 Quaternius_Player 的 Character_Soldier，站在沙滩上可用左
-- 摇杆四下走动（相机相对方向，矩形边界钳制），默认 Idle_Shoot、走动切 Run_Gun（这个包没有
-- “单次射击”动画，后座感交给相机 kick 与枪口光）。
-- 相机绕主角胸口做球面轨道：aim 左右摆、pitch 上下俯，开火时 kick 上抬再指数回落。
--
-- 为什么不用编辑器里的 Camera Rig 直接摆死：越肩相机每帧都要按 aim/pitch/kick 重算位置
-- 并 LookAt 主角，是运行时的东西；Camera Rig 节点只是拿来挂 Camera 组件与 viewport。
--
-- 朝向约定（照 C++ 的 R_y 推过，别再手推一遍）：
--   Soldier 模型原生朝 -z。定义 aim 正值为"向右转"，节点旋转 = Quaternion(-aim, UP)，
--   于是 facing = (sin aim, 0, -cos aim)（aim=0 时正对海面 -z，aim=+90° 时朝 +x）。
--   behind = -facing = (-sin aim, 0, cos aim)，相机在主角背后上方。

local Config = require("game.Config")
local Materials = require("game.Materials")

---@class Player 主角与越肩相机的运行时状态
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
---@return Player
function Player.Build(scene, spot)
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
    anim:Play(Config.PlayerAnim("Idle_Shoot"), 0, true, 0.0)

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

    self.aim = 0.0                              -- 度，正=右转
    self.pitch = Config.CAMERA_PITCH            -- 度，相机在目标上方的俯角
    self.kick = 0.0                             -- 开火上抬的瞬时量（度），指数回落
    self.facing = Vector3(0.0, 0.0, -1.0)
    self.nodeYaw = 0.0                          -- 人物自身航向（弧度），朝移动方向/开火目标转；相机不用它

    self:UpdateCamera(0.0)
    return self
end

--- 开火时把人物瞬间转向目标（相机不动，仍固定朝海）。王者式自动吸附下玩家无需手动对准，
--- 但人物要朝打的方向，看着才像自己在开枪。
---@param worldPos Vector3
function Player:FaceTowards(worldPos)
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

--- 左摇杆走位：沿"相机相对"的水平方向平移主角（摇杆上推=朝瞄准方向前进），
--- 钳在可行走矩形内。越肩相机每帧按 self.spot 重算轨道，所以相机会自动跟着人走。
--- 无物理模块，不做碰撞：棕榈/岩石会穿过去，占位阶段可接受。
---@param stickX number -1..1，右为正
---@param stickY number -1..1，屏幕下为正（=向后）
---@param dt number
function Player:Move(stickX, stickY, dt)
    local moving = (stickX * stickX + stickY * stickY) > 0.0001
    if moving then
        local aimRad = math.rad(self.aim)
        -- facing=瞄准水平方向，right=facing 右手侧；摇杆上推(stickY<0)沿 facing 前进
        local dirX = self.facing.x * (-stickY) + math.cos(aimRad) * stickX
        local dirZ = self.facing.z * (-stickY) + math.sin(aimRad) * stickX
        local len = math.sqrt(dirX * dirX + dirZ * dirZ)
        if len > 1.0 then dirX, dirZ = dirX / len, dirZ / len end

        local nx = clamp(self.spot.x + dirX * Config.MOVE_SPEED * dt, Config.WALK_MIN_X, Config.WALK_MAX_X)
        local nz = clamp(self.spot.z + dirZ * Config.MOVE_SPEED * dt, Config.WALK_MIN_Z, Config.WALK_MAX_Z)
        self.spot = Vector3(nx, self.groundY, nz)
        self.node:SetPosition(self.spot)

        -- 人物朝移动方向转（相机固定朝海，不跟着转），短弧缓动避免瞬跳
        local step = Config.TURN_SPEED * dt
        local d = angDiff(self.nodeYaw, math.atan(dirX, -dirZ))
        self.nodeYaw = self.nodeYaw + clamp(d, -step, step)
    end

    -- 移动/待机动画：只在状态跳变时 Play，避免每帧重启；0.15s 交叉淡化
    if moving and not self.running_ then
        self.anim:Play(Config.PlayerAnim("Run_Gun"), 0, true, 0.15)
        self.running_ = true
    elseif not moving and self.running_ then
        self.anim:Play(Config.PlayerAnim("Idle_Shoot"), 0, true, 0.15)
        self.running_ = false
    end
end

--- 每帧：衰减 kick + 重算相机轨道位置与朝向 + 同步 facing。
---@param dt number 秒
function Player:Update(dt)
    if self.kick ~= 0.0 then
        -- 指数回落：kick *= e^(-decay*dt)，用一阶近似避免每帧 exp
        self.kick = self.kick - self.kick * Config.CAMERA_KICK_DECAY * dt
        if math.abs(self.kick) < 0.001 then
            self.kick = 0.0
        end
    end
    self:UpdateCamera(dt)
end

--- 把 aim/pitch/kick 换算成相机世界位置与朝向。
---
--- 关键：相机不是 LookAt 主角（那样屏幕中心的准星射线永远指向主角自己），而是
--- 沿"瞄准方向"（facing 下俯 pitch）看向前方，位置在主角背后上方并向右侧移（越肩）。
--- 于是 GetScreenRay(0.5,0.5) 得到的射线就是玩家真正瞄准的方向，能打到远处的敌人。
function Player:UpdateCamera()
    local aimRad = math.rad(self.aim)
    local pitchRad = math.rad(self.pitch + self.kick)
    local sinA, cosA = math.sin(aimRad), math.cos(aimRad)
    local sinP, cosP = math.sin(pitchRad), math.cos(pitchRad)

    -- 水平朝向（给枪口/磁吸用）与相机轨道基向量
    self.facing = Vector3(sinA, 0.0, -cosA)
    local behind = Vector3(-sinA, 0.0, cosA)     -- 主角背后
    local right = Vector3(cosA, 0.0, sinA)       -- 主角右手侧（越肩偏移方向）
    -- 瞄准方向 = facing 下俯 pitch（从塔上往下打地/海面的敌人）
    local forward = Vector3(sinA * cosP, -sinP, -cosA * cosP)

    -- 人物节点朝 nodeYaw（移动/开火转向），相机轨道仍用固定的 aim(=朝海)
    self.node:SetRotation(Quaternion(-self.nodeYaw, Vector3.UP))

    local target = self.spot + Vector3(0.0, Config.CAMERA_TARGET_Y, 0.0)
    local dist = Config.CAMERA_DISTANCE
    local camPos = target
        + behind * (dist * cosP)
        + Vector3(0.0, dist * sinP, 0.0)
        + right * Config.CAMERA_SHOULDER_OFFSET

    self.cameraNode:SetPosition(camPos)
    -- LookAt 绑定要三个参数（target, up, space）：朝瞄准方向的远处一点看，不给 space 会报参数个数不对。
    self.cameraNode:LookAt(camPos + forward * 100.0, Vector3.UP, TS.WORLD)
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

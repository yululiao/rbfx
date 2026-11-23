-- Scripts/game/Player.lua
-- 主角：星露谷式点击移动 + 四向两帧行走 + 抛竿/拉杆姿势。
-- 位置一律记「图像像素」，只在写节点坐标时换算一次。

local Config = require("game.Config")
local World = require("game.World")

local Player = {}
Player.__index = Player

local DIRS = { "down", "right", "up", "left" }
-- 节拍跟着速度走：每帧前进 WALK_STRIDE_PX，换算成秒就是 FRAME_TIME。
-- 写死秒数的话一改 WALK_SPEED 就滑步。
local FRAME_TIME = Config.WALK_STRIDE_PX / Config.WALK_SPEED
-- 4 帧循环是 contact/passing/contact/passing，身体在并腿帧最高、跨步帧最低。
-- 以并腿帧为基准位（站着就是它），所以只给跨步帧 +2 下沉，停步不会停在偏移上。
local BOB_PX = { 2, 0, 2, 0 }
-- 脚底线在 256 画布里距底边 12px（切图脚本 vibe_images/slice_walk.py 的 FEET_Y 同一口径）
local FEET_HOTSPOT = 12.0 / 256.0

function Player.Build(scene)
    local self = setmetatable({}, Player)
    self.x, self.y = Config.START.x, Config.START.y
    self.dir = "down"
    self.pose = "idle"
    self.goalX, self.goalY = nil, nil
    self.animT = 0.0
    self.frame = 2 -- walking phase; idle uses dedicated standing sprites
    self.locked = false

    self.frames = {}
    self.idleSprites = {}
    for _, d in ipairs(DIRS) do
        local cycle = {}
        for i = 0, 3 do
            cycle[i + 1] = World.Spr("Textures/Char/walk_" .. d .. "_" .. i .. ".png")
        end
        self.frames[d] = cycle
        self.idleSprites[d] = World.Spr("Textures/Char/idle_" .. d .. ".png")
    end
    self.poseSprites = {
        cast = {},
        reel = {},
    }
    for _, pose in ipairs({ "cast", "reel" }) do
        local sprites = self.poseSprites[pose]
        sprites.right = World.Spr("Textures/Char/pose_" .. pose .. ".png")
        sprites.left = sprites.right -- mirror both the image and its rod endpoint
        sprites.down = World.Spr("Textures/Char/pose_" .. pose .. "_down.png")
        sprites.up = World.Spr("Textures/Char/pose_" .. pose .. "_up.png")
    end

    self.node, self.comp = World.NewSprite(scene, "Player",
        "Textures/Char/idle_down.png", 3, 0.5, FEET_HOTSPOT)
    self:RefreshVisual()
    World.SetNodePos(self.node, self.x, self.y)

    -- 走位高亮框：点地时出现在落点，走到（或改道/被锁住）时淡出。
    -- 在 Player 之下、水缸同层：它是地上的标记，不该压住道具和角色。
    self.markNode, self.mark = World.NewSprite(scene, "TapMark",
        "Textures/Prop/tap_mark.png", 1, 0.5, 0.5)
    self.markFade = -1.0
    self.mark:SetEnabled(false)
    return self
end

--- 淡出中的高亮框每帧推进；放在 locked 判断之前，否则抛竿锁住时框会停在半透明不消失
function Player:UpdateMark(dt)
    if self.markFade < 0.0 then
        return
    end
    self.markFade = self.markFade - dt
    if self.markFade <= 0.0 then
        self.markFade = -1.0
        self.mark:SetEnabled(false)
    else
        local a = self.markFade / Config.MARK_FADE
        self.mark:SetColor(Color(1, 1, 1, a))
    end
end

function Player:IsMoving()
    return self.goalX ~= nil
end

--- 点击目标：走不到的点会退回到该方向上最后一个可行走点（星露谷的“走到跟前停下”）
function Player:SetGoal(px, py)
    if self.locked then
        return false
    end
    local gx, gy = Config.SnapWalkable(self.x, self.y, px, py)
    if not gx then
        return false
    end
    self.goalX, self.goalY = gx, gy
    self.markFade = -1.0
    self.mark:SetColor(Color(1, 1, 1, 1))
    self.mark:SetEnabled(true)
    World.SetNodePos(self.markNode, gx, gy)
    self:SetPose("walk")
    return true
end

function Player:StopWalking()
    self.goalX, self.goalY = nil, nil
    if self.markFade < 0.0 and self.mark:IsEnabled() then
        self.markFade = Config.MARK_FADE
    end
    if self.pose == "walk" then
        self:SetPose("idle")
    end
end

function Player:SetSprite(sp)
    if sp and sp ~= self.shown then
        self.comp:SetSprite(sp)
        self.shown = sp
    end
end

-- Resolve pose and direction together, including turns within the same pose.
function Player:RefreshVisual()
    local fishing = self.pose == "cast" or self.pose == "reel"
    local flipped = fishing and self.dir == "left"
    if self.flipped ~= flipped then
        self.comp:SetFlipX(flipped)
        self.flipped = flipped
    end
    if fishing then
        self:SetSprite(self.poseSprites[self.pose][self.dir])
    elseif self.pose == "walk" then
        self:SetSprite(self.frames[self.dir][self.frame])
    else
        self:SetSprite(self.idleSprites[self.dir])
    end
end

function Player:SyncPosition()
    local bob = self.pose == "walk" and BOB_PX[self.frame] or 0
    World.SetNodePos(self.node, self.x, self.y + bob)
end

function Player:SetPose(pose)
    if self.pose == pose then
        self:RefreshVisual()
        self:SyncPosition()
        return
    end
    self.pose = pose
    -- Reset the walk cycle; idle/fishing images are independent of this phase.
    self.frame = 2
    self.animT = 0.0
    self:RefreshVisual()
    self:SyncPosition()
end

local function PickDir(dx, dy)
    if math.abs(dx) >= math.abs(dy) then
        return dx >= 0 and "right" or "left"
    end
    return dy >= 0 and "down" or "up"
end

function Player:Update(dt)
    self:UpdateMark(dt)
    if self.locked then
        return
    end

    if self.pose == "walk" and self.goalX then
        local dx, dy = self.goalX - self.x, self.goalY - self.y
        local dist = math.sqrt(dx * dx + dy * dy)
        if dist < 3.0 then
            self:StopWalking()
            return
        end
        self.dir = PickDir(dx, dy)

        local step = math.min(dist, Config.WALK_SPEED * dt)
        local nx, ny = self.x + dx / dist * step, self.y + dy / dist * step
        if Config.IsWalkable(nx, ny) then
            self.x, self.y = nx, ny
        elseif Config.IsWalkable(nx, self.y) then
            self.x = nx
        elseif Config.IsWalkable(self.x, ny) then
            self.y = ny
        else
            self:StopWalking()
            return
        end

        self.animT = self.animT + dt
        if self.animT >= FRAME_TIME then
            self.animT = self.animT - FRAME_TIME
            self.frame = self.frame % 4 + 1
        end
    end
    self:RefreshVisual()

    -- 位置每帧都写：跨步帧的 2px 下沉靠它落地，换帧/换姿势后也不会残留旧偏移
    self:SyncPosition()
end

--- 朝向水面/某个点站定（抛竿前用，避免背对鱼钩）
function Player:FaceTowards(px, py)
    self.dir = PickDir(px - self.x, py - self.y)
    self:RefreshVisual()
end

--- 竿尖的图像像素坐标：抛竿/拉杆姿势用姿势锚点，其余用当前朝向锚点。
function Player:RodTip()
    local tips = Config.ROD_TIP[self.pose]
    local off = tips and tips[self.dir] or Config.ROD_TIP[self.dir]
    return self.x + off[1], self.y + off[2]
end

return Player

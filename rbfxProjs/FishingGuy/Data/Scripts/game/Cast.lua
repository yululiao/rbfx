-- Scripts/game/Cast.lua
-- 抛竿→等口→咬钩的三段状态机。浮漂与叹号都是世界精灵，位置用图像像素算。
-- 谁都能看出来「叹号在浮漂头上」比在屏幕上贴一个 HUD 元素更贴近星露谷的读法。

local Config = require("game.Config")
local World = require("game.World")

local Cast = {}
Cast.__index = Cast

function Cast.Build(scene, audio, line)
    local self = setmetatable({}, Cast)
    self.audio = audio
    self.line = line
    self.state = "none"
    self.t = 0.0
    self.biteWait = 0.0
    self.biteLeft = 0.0

    self.bobNode, self.bob = World.NewSprite(scene, "Bobber", "Textures/Char/bobber.png", 2, 0.5, 0.35)
    self.alertNode, self.alert = World.NewSprite(scene, "Alert", "Textures/Prop/alert.png", 4, 0.5, 0.21)
    -- Node:SetVisible 没进 Lua 绑定，隐藏一律走组件 SetEnabled
    self.bob:SetEnabled(false)
    self.alert:SetEnabled(false)
    return self
end

function Cast:IsActive()
    return self.state ~= "none"
end

function Cast:IsBiting()
    return self.state == "bite"
end

--- dirX/dirY 是图像坐标下的单位向量（y 向下为正，指向屏幕下方）
function Cast:Start(player, dirX, dirY, power)
    local tx, ty = Config.CastTarget(player.x, player.y, dirX, dirY, power)
    if not tx then
        return false
    end
    player:FaceTowards(tx, ty)
    player.locked = true
    player:StopWalking()
    player:SetPose("cast")

    self.fromX, self.fromY = player.x + dirX * 26, player.y + dirY * 26
    self.tgtX, self.tgtY = tx, ty
    self.x, self.y = self.fromX, self.fromY
    self.dispX, self.dispY = self.fromX, self.fromY
    self.player = player
    self.t = 0.0
    self.state = "fly"
    self.bob:SetEnabled(true)
    self.alert:SetEnabled(false)
    World.SetNodePos(self.bobNode, self.x, self.y)
    self.audio:Cast()
    return true
end

function Cast:Update(dt)
    if self.state == "fly" then
        self.t = self.t + dt / Config.FLIGHT_TIME
        local k = math.min(self.t, 1.0)
        -- 抛物线抬高只作用于飞行中的显示位置；self.x/self.y 一直是浮漂的静止点
        local x = self.fromX + (self.tgtX - self.fromX) * k
        local y = self.fromY + (self.tgtY - self.fromY) * k - math.sin(math.pi * k) * 46
        self.dispX, self.dispY = x, y
        World.SetNodePos(self.bobNode, x, y)
        if k >= 1.0 then
            -- 落水点才是浮漂的静止点：不写这一句，wait/bite 拿 self.x/self.y 定位会退回出手点，
            -- 看着就是「一咬钩线和漂一起缩回脚边」。
            self.x, self.y = self.tgtX, self.tgtY
            self.dispX, self.dispY = self.x, self.y
            self.state = "wait"
            self.biteWait = Config.BITE_WAIT_MIN
                + math.random() * (Config.BITE_WAIT_MAX - Config.BITE_WAIT_MIN)
            self.audio:Splash()
        end

    elseif self.state == "wait" then
        self.biteWait = self.biteWait - dt
        if self.biteWait <= 0.0 then
            self.state = "bite"
            self.biteLeft = Config.BITE_WINDOW
            self.alert:SetEnabled(true)
            World.SetNodePos(self.alertNode, self.x, self.y - 40)
            self.audio:Bite()
        end

    elseif self.state == "bite" then
        -- 咬钩时浮漂上下抖，给一个「快点」的视觉节拍
        self.biteLeft = self.biteLeft - dt
        local jiggle = math.sin(self.biteLeft * 26.0) * 4.0
        self.dispX, self.dispY = self.x, self.y + jiggle
        World.SetNodePos(self.bobNode, self.x, self.y + jiggle)
        if self.biteLeft <= 0.0 then
            self:End()
            return "escaped"
        end
    end

    if self.line and self.player and self.state ~= "none" then
        local tx, ty = self.player:RodTip()
        local sag = Config.LINE_SAG[self.state] or Config.LINE_SAG.wait
        self.line:Draw(tx, ty, self.dispX, self.dispY, sag)
    end

    return nil
end

--- 点屏中钩：返回 true 表示这次咬钩被抓到了
function Cast:Hook()
    if self.state ~= "bite" then
        return false
    end
    self.alert:SetEnabled(false)
    self.state = "reeled"
    return true
end

--- 收线：拉杆结束、鱼跑了、玩家主动放弃，都走这里
function Cast:End()
    self.state = "none"
    self.bob:SetEnabled(false)
    self.alert:SetEnabled(false)
    if self.line then
        self.line:Hide()
    end
end

return Cast

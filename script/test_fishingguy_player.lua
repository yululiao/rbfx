local dataRoot = FISHINGGUY_DATA_ROOT or "rbfxProjs/FishingGuy/Data/"
package.path = dataRoot .. "Scripts/?.lua;" .. package.path
Color = function(...) return {...} end
local Config = require("game.Config")
local World = {}
function World.Spr(path)
    local f = assert(io.open(dataRoot .. path, "rb"), path)
    f:close()
    return path
end
function World.NewSprite(_, _, path)
    local c = {sprite=path, enabled=true}
    function c:SetSprite(sp) self.sprite=sp end
    function c:SetFlipX(value) self.flip=value end
    function c:SetEnabled(value) self.enabled=value end
    function c:IsEnabled() return self.enabled end
    function c:SetColor(_) end
    return {}, c
end
function World.SetNodePos(node,x,y) node.x,node.y=x,y end
package.loaded["game.World"] = World
local Player = require("game.Player")
local p = Player.Build({})
assert(p.comp.sprite:match("idle_down.png$") and p.comp.flip == false)
local directions={down={0,100},right={100,0},up={0,-100},left={-100,0}}
for dir,delta in pairs(directions) do
    p:SetPose("walk")
    p:FaceTowards(p.x+delta[1],p.y+delta[2])
    p.frame=1
    p:SyncPosition()
    assert(p.node.y == p.y+2)
    p:StopWalking()
    assert(p.pose=="idle" and p.comp.sprite:match("idle_"..dir..".png$"))
    assert(p.node.y==p.y and not p.comp.flip)
    p:StopWalking() -- idempotent
end
for _,pose in ipairs({"cast","reel"}) do
    p.locked=true
    p:SetPose(pose)
    for dir,delta in pairs(directions) do
        p:FaceTowards(p.x+delta[1],p.y+delta[2])
        local suffix=(dir=="up" or dir=="down") and "_"..dir or ""
        assert(p.comp.sprite:match("pose_"..pose..suffix..".png$"))
        assert(p.comp.flip==(dir=="left"))
        local x,y=p:RodTip()
        assert(x==p.x+Config.ROD_TIP[pose][dir][1])
        assert(y==p.y+Config.ROD_TIP[pose][dir][2])
        p:SetPose(pose) -- same pose must preserve direction
        p:Update(.1)
        assert(p.node.y==p.y)
    end
    p:FaceTowards(p.x-100,p.y)
    p.locked=false
    p:SetPose("idle")
    assert(not p.comp.flip and p.comp.sprite:match("idle_left.png$"))
end
-- Arrival and blocked movement both immediately restore idle and clear the bob.
for _,blocked in ipairs({false,true}) do
    p:SetPose("walk")
    p.frame=1
    p:SyncPosition()
    p.goalX,p.goalY=p.x+(blocked and 100 or 1),p.y
    Config.IsWalkable=function() return not blocked end
    p:Update(.01)
    assert(p.pose=="idle" and p.goalX==nil and p.node.y==p.y)
end
-- Direction changes need not wait for the next animation frame.
Config.IsWalkable=function() return true end
p:SetPose("walk")
p.dir="down"
p.goalX,p.goalY=p.x+100,p.y
p:Update(.0001)
assert(p.dir=="right" and p.comp.sprite:match("walk_right_1.png$"))
print("FishingGuy player regressions passed: idle, stopping, turning, four-way fishing and rod endpoints")

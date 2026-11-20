-- Run from repository root with Lua 5.4: lua script/test_firegame_hotpaths.lua
package.path = "rbfxProjs/FireGame/Data/Scripts/?.lua;" .. package.path
local vec = {}
vec.__index = vec
Vector3 = setmetatable({}, { __call = function(_, x, y, z)
    return setmetatable({ x = x or 0, y = y or 0, z = z or 0 }, vec)
end })
vec.__add = function(a,b) return Vector3(a.x+b.x,a.y+b.y,a.z+b.z) end
vec.__mul = function(a,b) return Vector3(a.x*b,a.y*b,a.z*b) end
Vector3.UP, Vector3.ZERO = Vector3(0,1,0), Vector3()
Vector2 = function(x,y) return {x=x,y=y} end
Quaternion = function() return {} end
Color = function() return {} end
local C = {
    PIRATE_CARD_CENTER=1, PIRATE_CARD_SIZE=2, PIRATE_HIT_HEIGHT=1.5,
    BOAT_HIT_HEIGHT=1, BOAT_BIG_HIT_HEIGHT=3, DOG_TARGET_SCAN_INTERVAL=0.15,
    TURRET_RANGE=30, TURRET_AIM_TURN_RATE=0, MODEL_FORWARD_YAW=0,
    TURRET_AIM_FIRE_TOL=1, BULLET_HIT_RADIUS=1,
    PIRATE_RAIDER_CHANCE=0, PIRATE_YAW=0, SEA_Y=-1, PIRATE_WADE_DEPTH=0.1,
    PIRATE_HP=1, PIRATE_SPEED=3, PIRATE_SPEED_SPREAD=0, DEFENSE_Z=50,
    TURRET_STAND_OFF=0, SAND_Y=0, ShoreZ=function() return 0 end,
}
package.loaded["game.Config"] = C
package.loaded["game.Materials"] = {}
package.loaded["game.Sidebar"] = {}
local Crowd = require("game.FlipbookCrowd")
local Enemy = require("game.Enemy")
local Dog = require("game.Dog")
local Turrets = require("game.Turrets")
local Shooting = require("game.Shooting")
local function noRead() error("hot path read an enemy node position") end

-- Real SpawnPirate/Update keep the shared position current and publish the same value to node.
local node = { pos=Vector3(), SetRotation=function() end }
function node:SetPosition(p) self.pos=Vector3(p.x,p.y,p.z) end
function node:GetPosition() return Vector3(self.pos.x,self.pos.y,self.pos.z) end
local manager = setmetatable({units={},root={CreateChild=function() return node end},
    crowd={Acquire=function() return {} end,Play=function() end}},Enemy)
manager:SpawnPirate(Vector3(0,0,-10),"test")
local u = manager.units[1]
node.GetPosition, node.GetWorldPosition = noRead, noRead
local shared = u.position
u:Update(0.5)
assert(shared == u.position)
assert(shared.z == -8.5 and node.pos.z == shared.z and node.pos.y == shared.y)
local target, aim = manager:NearestTarget(Vector3(),30)
assert(target == u and aim.y == shared.y + C.PIRATE_HIT_HEIGHT)
aim.y=999
assert(shared.y ~= 999, "aim position must not alias shared enemy coordinates")

-- Fixed x reuses shoreline, while z crossings still update water/land height.
local shoreReads = 0
C.ShoreZ = function(x) shoreReads=shoreReads+1; return x end
u.shoreX = nil
u.position.x, u.position.z = 0, -1
u:Update(0)
assert(shoreReads==1 and u.position.y==C.SEA_Y+C.PIRATE_WADE_DEPTH)
u.position.z = 1
u:Update(0)
assert(shoreReads==1 and u.position.y==C.SAND_Y)
u.position.x = 2
u:Update(0)
assert(shoreReads==2 and u.position.y==C.SEA_Y+C.PIRATE_WADE_DEPTH)
-- Raider movement changes x, refreshes the cache, and never rotates its logical node.
u.raider = true
C.PIRATE_MELEE_RANGE = 0.1
manager.player = {spot=Vector3(10,0,10),hp=1}
node.SetRotation = function() error("billboard pirate should not rotate its node") end
u:Update(0.1)
assert(shoreReads==3 and u.shoreX==u.position.x)

-- Native FrameSprite playback/batching is covered by Tests/Graphics/FrameSprite.cpp.
local Telemetry = require("game.Telemetry")
local probe = setmetatable({detailCounts={},time={GetSystemTime=function()
    error("operation counts must not read native clock")
end}},Telemetry)
probe:DetailCount("test")
assert(probe.detailCounts.test==1)

local function unit(x,z,kind)
    return {alive=true,kind=kind or "pirate",position=Vector3(x,0,z),
        node={GetWorldPosition=noRead,GetPosition=noRead}}
end
-- Dog retains live positions, rescans on death/range exit, and honors sticky-target selection.
local a,b=unit(2,0),unit(3,0)
local scans=0
local enemies={GetUnits=function() scans=scans+1; return {a,b} end}
local dog=setmetatable({x=0,z=0,cfg={aggroRadius=10,stickFactor=0.8},enemies=enemies},Dog)
assert(dog:FindPirate()==a and scans==1)
a.position.x=4
local found,pos=dog:FindPirate()
assert(found==a and pos.x==4 and scans==1)
a.alive=false
assert(dog:FindPirate()==b and scans==2)
a.alive=true; a.position.x=1; b.position.x=20
assert(dog:FindPirate()==a and scans==3)
dog.targetScanT=0; b.position.x=0.9
assert(dog:FindPirate()==a, "hysteresis should keep a near-equal locked target")
dog.targetScanT=0; b.position.x=0.2
assert(dog:FindPirate()==b)

-- Turret aim and ready-to-fire check share one scan, while target position updates each frame.
local boat=unit(0,5,"boat")
scans=0
local turret=setmetatable({towers={{x=0,z=0,aimYaw=0,cd=0,gun={SetRotation=function() end}}},
    enemies={GetUnits=function() scans=scans+1; return {boat} end},
    SyncTowerState=function() end,UpdateRecoil=function() end,UpdateProjectiles=function() end},Turrets)
turret:Update(0.025)
assert(scans==1 and turret.towers[1].target==boat)
boat.position.z=6; turret:Update(0.025)
assert(scans==2 and turret.towers[1].targetPos.z==6)
boat.alive=false; turret:Update(0.025)
assert(scans==3 and turret.towers[1].target==nil)

-- Bullet collisions use the current shared coordinate without reading the target node.
local victim=unit(9,0)
victim.position.x=2
local bulletNode={pos=Vector3(),GetPosition=function(self) return self.pos end,
    SetPosition=function(self,p) self.pos=p end,Remove=function(self) self.removed=true end}
local hits=0
local shooting=setmetatable({bullets={{node=bulletNode,dir=Vector3(1,0,0),speed=2,
    traveled=0,maxDist=10,hit={},hitsLeft=1}},enemies={GetUnits=function() return {victim} end},
    DamageUnit=function(_,v) assert(v==victim); hits=hits+1 end},Shooting)
shooting:UpdateBullets(1)
assert(hits==1 and #shooting.bullets==0 and bulletNode.removed)
print("FireGame hot-path regressions passed: shared coordinates, shoreline cache, targeting and collision")

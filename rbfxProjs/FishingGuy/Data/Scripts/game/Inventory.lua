-- Scripts/game/Inventory.lua
-- 背包：固定 6 格，同种鱼叠一格（记数量与最大体重），满了就钓不上来。

local Config = require("game.Config")

local Inventory = {}
Inventory.__index = Inventory

function Inventory.Build()
    local self = setmetatable({}, Inventory)
    -- 不预填 nil：表里根本没这个下标时 ipairs 会在第一个洞就停
    self.slots = {}
    return self
end

function Inventory:Find(fishIndex)
    for i = 1, Config.SLOT_COUNT do
        local s = self.slots[i]
        if s and s.fish == fishIndex then
            return i
        end
    end
    return nil
end

function Inventory:FreeSlot()
    for i = 1, Config.SLOT_COUNT do
        if not self.slots[i] then
            return i
        end
    end
    return nil
end

--- 收一条鱼；成功返回 true，没格子返回 false
function Inventory:Add(fishIndex, weight)
    local i = self:Find(fishIndex)
    if not i then
        i = self:FreeSlot()
        if not i then
            return false
        end
        self.slots[i] = { fish = fishIndex, count = 0, maxW = weight }
    end
    local s = self.slots[i]
    s.count = s.count + 1
    s.maxW = math.max(s.maxW, weight)
    return true
end

function Inventory:Slot(i)
    return self.slots[i]
end

return Inventory

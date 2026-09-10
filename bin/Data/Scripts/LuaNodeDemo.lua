-- LuaNodeDemo.lua
-- Example script demonstrating rbfx Node bindings via sol3.
-- The host application should set the global 'scene' variable to a Scene instance
-- before executing this script.

assert(scene, "Global 'scene' must be set by the host application")

-- Create a node hierarchy entirely from Lua
local root = scene:CreateChild("LuaRoot")
root.position = Vector3(0, 1, 0)

local child = root:CreateChild("LuaChild")
child:setPositionXYZ(10, 0, 5)

-- Demonstrate component creation
local sm = root:CreateStaticModel()
assert(sm, "StaticModel component creation failed")

-- Vector3 math
local offset = Vector3(1, 2, 3)
local target = child.position + offset
print(string.format("Child target position: %.1f, %.1f, %.1f", target.x, target.y, target.z))

print("LuaNodeDemo completed successfully")

-- Scripts/main.lua
-- Editor Game view Play entry point.
-- Automatically loaded and executed by GameViewTab when Play is pressed.

-- Add Scripts directory to Lua module search path so require() can find sibling .lua files.
-- debug.getinfo source may or may not have '@' prefix depending on how the
-- chunk was loaded (loadfile adds '@', sol3 load() does not).
local scriptsDir = debug.getinfo(1, "S").source:match("^@?(.*)[/\\]")
package.path = scriptsDir .. "/?.lua;" .. package.path

-- Load game logic module and start.
local Game = require("Game")
Game:Start()

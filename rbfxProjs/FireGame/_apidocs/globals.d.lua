---@meta
-- Hand-written declarations for globals that the engine/host inject into a Lua VM at
-- runtime. They are NOT part of the C++ sol3 binding surface, so the ApiDocGen tool
-- (which only sees new_usertype / set_function / create_named_table) does not emit them.
-- Kept in its own file so regenerating engine.d.lua / editor.d.lua never clobbers it.
--
-- Game scripts: when you press Play in the editor (LuaGameRunner) or run via
-- LuaGamePlayer, the active scene is exposed as a global before your entry script runs
-- -- see Source/Editor/Project/LuaGameRunner.cpp  SetGlobalScene("scene", ...).

---@type Scene
scene = nil

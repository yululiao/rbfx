// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#pragma once

struct lua_State;

namespace Urho3D
{

/// Preload the LuaSocket modules into a fresh Lua state so require("socket") works for
/// tooling like the LuaPanda debugger without any filesystem searcher. Compiles to a
/// no-op when the engine is built without URHO3D_LUASOCKET (e.g. web, where raw TCP
/// does not exist in the browser sandbox).
void RegisterLuaSocketModules(lua_State* luaState);

}

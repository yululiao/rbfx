//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

// Separate translation unit on purpose: the vendored lua.h has no extern "C" block, so it
// must be wrapped here, and wrapping it in a file that also includes sol3 would let sol3's
// own (unwrapped) include win and leave these symbols C++-mangled at link time.

#include "../Urho3D/Precompiled.h"

#include "LuaScriptContainer.h"

#include "../Urho3D/Core/StringUtils.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

namespace Urho3D
{

namespace
{

/// lua_dump writer that appends into an ea::vector.
int BytecodeWriter(lua_State* /*L*/, const void* block, size_t size, void* userdata)
{
    auto* dest = static_cast<ea::vector<uint8_t>*>(userdata);
    const uint8_t* bytes = static_cast<const uint8_t*>(block);
    dest->insert(dest->end(), bytes, bytes + size);
    return 0;
}

} // namespace

bool LuaScriptCompileToBytecode(
    const void* source, size_t size, const char* chunkName, bool strip, ea::vector<uint8_t>& out, ea::string& error)
{
    out.clear();
    error.clear();

    if (!source && size != 0)
    {
        error = "Lua compilation: null source";
        return false;
    }

    lua_State* L = luaL_newstate();
    if (!L)
    {
        error = "Lua compilation: could not create a Lua state";
        return false;
    }

    const char* name = chunkName && *chunkName ? chunkName : "=[lua-compiler]";

    // Note the explicit zero length fallback: an empty chunk is legal Lua, and passing a
    // null pointer with a non-zero length would read out of bounds.
    const int status = luaL_loadbuffer(L, size != 0 ? static_cast<const char*>(source) : "", size, name);
    if (status != LUA_OK)
    {
        const char* message = lua_tostring(L, -1);
        error = message ? ToString("Lua compilation failed: %s", message) : "Lua compilation failed";
        lua_close(L);
        return false;
    }

    if (lua_dump(L, BytecodeWriter, &out, strip ? 1 : 0) != 0)
    {
        error = "Lua compilation: lua_dump rejected the chunk";
        lua_close(L);
        return false;
    }

    lua_close(L);

    if (out.empty())
    {
        error = "Lua compilation: produced no bytecode";
        return false;
    }

    return true;
}

} // namespace Urho3D

//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#include "../Urho3D/Precompiled.h"

#include "LuaFile.h"

#include "LuaScriptContainer.h"
#include "../Urho3D/Core/Context.h"
#include "../Urho3D/Core/StringUtils.h"
#include "../Urho3D/IO/Log.h"
#include "../Urho3D/IO/Deserializer.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <cstring>

namespace Urho3D
{

LuaFile::LuaFile(Context* context)
    : Resource(context)
{
}

LuaFile::~LuaFile() = default;

void LuaFile::RegisterObject(Context* context)
{
    // The editor keeps two Lua VMs in one Context and Reinitialize() re-runs the setup of a
    // host, so registration has to be a no-op when the type is already reflected.
    if (!context->IsReflected<LuaFile>())
        context->AddFactoryReflection<LuaFile>();
}

bool LuaFile::BeginLoad(Deserializer& source)
{
    data_.clear();
    bytecode_ = false;

    const unsigned size = source.GetSize();
    ea::vector<uint8_t> raw;
    if (size != 0)
    {
        raw.resize(size);
        if (source.Read(raw.data(), size) != size)
        {
            URHO3D_LOGERRORF("Failed to read Lua script %s", GetName().c_str());
            return false;
        }
    }

    // One seam, one decision: packaged bytes go through the container decoder, everything
    // else is taken as-is. That keeps a development tree of plain .lua files and a shipping
    // tree of encrypted .luc files on exactly the same code path from here down.
    ea::vector<uint8_t> payload;
    if (LuaScriptContainerIsPackaged(raw.data(), raw.size()))
    {
        ea::string error;
        if (!LuaScriptContainerUnpack(raw.data(), raw.size(), payload, error))
        {
            URHO3D_LOGERRORF("Failed to load Lua script %s: %s", GetName().c_str(), error.c_str());
            return false;
        }
    }
    else
    {
        payload = ea::move(raw);
    }

    // Strip a UTF-8 BOM; the Lua loader rejects one and Windows editors happily write them.
    if (payload.size() >= 3 && payload[0] == 0xEF && payload[1] == 0xBB && payload[2] == 0xBF)
        payload.erase(payload.begin(), payload.begin() + 3);

    bytecode_ = payload.size() >= 4 && memcmp(payload.data(), LUA_SIGNATURE, 4) == 0;
    data_ = ea::move(payload);

    SetMemoryUse(static_cast<unsigned>(data_.size()));
    ++loadSerial_;
    return true;
}

bool LuaFile::LoadChunk(lua_State* L, ea::string& error) const
{
    error.clear();

    if (!L)
    {
        error = "LuaFile::LoadChunk called with a null Lua state";
        return false;
    }

    // The '@' prefix marks this as a file-originated chunk, which is what makes runtime errors
    // read as "Scripts/main.lua:12: ..." and lets debug.getinfo("S") recover the script folder.
    const ea::string chunkName = ea::string("@") + GetName();

    const int status = luaL_loadbuffer(L,
        data_.empty() ? "" : reinterpret_cast<const char*>(data_.data()),
        data_.size(),
        chunkName.c_str());

    if (status != LUA_OK)
    {
        const char* message = lua_tostring(L, -1);
        error = message ? ToString("%s", message) : "unknown Lua load error";
        lua_pop(L, 1);
        return false;
    }

    return true;
}

} // namespace Urho3D

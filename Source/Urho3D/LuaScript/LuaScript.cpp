//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#include "../Precompiled.h"

#include "../LuaScript/LuaScript.h"

#include "../LuaScript/LuaNodeBindings.h"
#include "../Core/Context.h"
#include "../IO/File.h"
#include "../IO/Log.h"
#include "../Resource/ResourceCache.h"
#include "../Scene/Node.h"

#include <sol/sol.hpp>

namespace Urho3D
{

LuaScript::LuaScript(Context* context)
    : Object(context)
{
}

LuaScript::~LuaScript()
{
}

bool LuaScript::Initialize()
{
    luaState_ = ea::make_unique<sol::state>();
    luaState_->open_libraries(
        sol::lib::base,
        sol::lib::package,
        sol::lib::string,
        sol::lib::table,
        sol::lib::math,
        sol::lib::coroutine,
        sol::lib::debug,
        sol::lib::io,
        sol::lib::os
    );

    RegisterEngineBindings();
    return true;
}

bool LuaScript::ExecuteString(const ea::string& code, const ea::string& chunkName)
{
    if (!luaState_)
    {
        URHO3D_LOGERROR("LuaScript is not initialized.");
        return false;
    }

    try
    {
        const char* name = chunkName.empty() ? "=[string]" : chunkName.c_str();
        sol::load_result loaded = luaState_->load(code.c_str(), name);
        if (!loaded.valid())
        {
            sol::error err = loaded;
            URHO3D_LOGERRORF("Lua load error in %s: %s", name, err.what());
            return false;
        }

        sol::protected_function script = loaded;
        sol::protected_function_result result = script();
        if (!result.valid())
        {
            sol::error err = result;
            URHO3D_LOGERRORF("Lua runtime error in %s: %s", name, err.what());
            return false;
        }
    }
    catch (const std::exception& e)
    {
        URHO3D_LOGERRORF("Lua exception: %s", e.what());
        return false;
    }

    return true;
}

bool LuaScript::ExecuteFile(const ea::string& fileName)
{
    auto* cache = context_->GetSubsystem<ResourceCache>();
    if (!cache)
    {
        URHO3D_LOGERROR("ResourceCache subsystem is required to execute Lua files.");
        return false;
    }

    AbstractFilePtr file = cache->GetFile(fileName);
    if (!file)
    {
        URHO3D_LOGERRORF("Lua script file not found: %s", fileName.c_str());
        return false;
    }

    ea::string code = file->ReadString();
    return ExecuteString(code, fileName);
}

void LuaScript::SetGlobalNode(const ea::string& name, Node* node)
{
    if (luaState_)
        (*luaState_)[name.c_str()] = node;
}

sol::state& LuaScript::GetState()
{
    return *luaState_;
}

lua_State* LuaScript::GetLuaState() const
{
    return luaState_ ? luaState_->lua_state() : nullptr;
}

void LuaScript::RegisterEngineBindings()
{
    RegisterVector3Bindings(*luaState_);
    RegisterNodeBindings(*luaState_);
}

} // namespace Urho3D

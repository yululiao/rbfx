// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#include "../Urho3D/Precompiled.h"

#include "EngineLuaVM.h"

#include "LuaPackageLoader.h"
#include "LuaScriptContainer.h"

#include "../Urho3D/Core/Context.h"
#include "../Urho3D/Core/StringUtils.h"
#include "../Urho3D/Core/Variant.h"
#include "../Urho3D/Engine/EngineEvents.h"
#include "../Urho3D/IO/Log.h"
#include "../Urho3D/Scene/Node.h"
#include "../Urho3D/Scene/Scene.h"

#include <sol/sol.hpp>

namespace Urho3D
{

namespace
{

// Game script output shows up as "Lua: ..." in the log, matching what the game has always
// printed. require() resolves from the mounted resource directories, like every other asset.
LuaVMConfig EngineVMConfig()
{
    LuaVMConfig config;
    config.logPrefix_ = "Lua";
    return config;
}

} // namespace

EngineLuaVM::EngineLuaVM(Context* context)
    : LuaVM(context, EngineVMConfig())
{
    Initialize();
}

void EngineLuaVM::Reinitialize()
{
    if (!luaState_)
        return;

    UnsubscribeFromAllEvents();

    // The searcher closure holds a pointer to the loader and the loader holds the raw
    // lua_State*, so neither may outlive the state in the other direction either.
    packageLoader_.reset();
    luaState_.reset();
    Initialize();
}

bool EngineLuaVM::ExecuteFile(const ea::string& fileName)
{
    if (!packageLoader_)
    {
        URHO3D_LOGERROR("EngineLuaVM is not initialized.");
        return false;
    }

    // A file the game names explicitly is a reload root: a change anywhere below it
    // re-executes this script, which is what makes editing game code while it runs useful.
    return packageLoader_->ExecuteScript(fileName, true);
}

void EngineLuaVM::SetGlobalNode(const ea::string& name, Node* node)
{
    if (luaState_)
        (*luaState_)[name.c_str()] = node;
}

void EngineLuaVM::SetGlobalScene(const ea::string& name, Scene* scene)
{
    if (luaState_)
        (*luaState_)[name.c_str()] = scene;
}

void EngineLuaVM::OnAfterInitialize()
{
    // A key that does not match what was packaged shows up as a load error per file, which points
    // everywhere except at the cause, so name the source once per process. The codec itself stays
    // silent because the offline bundler links it and has no log to write to.
    static bool keySourceReported = false;
    if (!keySourceReported)
    {
        keySourceReported = true;
        switch (LuaScriptContainerGetKeySource())
        {
        case LuaScriptContainerKeySource::RejectedEnvironment:
            URHO3D_LOGWARNING("RBFX_LUA_SCRIPT_KEY is not 64 hex digits, so the compiled key is in use. "
                              "Packaged scripts will fail to load until the two agree.");
            break;
        case LuaScriptContainerKeySource::Environment:
            URHO3D_LOGINFO("Lua script containers use the key from RBFX_LUA_SCRIPT_KEY.");
            break;
        default:
            break;
        }
    }

    // Register as a console command interpreter. The editor console shows
    // every subscriber of E_CONSOLECOMMAND in its interpreter dropdown. Subscribed here
    // rather than in the constructor because Reinitialize() drops every handler and then
    // calls this.
    SubscribeToEvent(E_CONSOLECOMMAND, &EngineLuaVM::HandleConsoleCommand);
}

void EngineLuaVM::HandleConsoleCommand(StringHash eventType, VariantMap& eventData)
{
    using namespace ConsoleCommand;

    if (eventData[P_ID].GetString() != "LuaScript")
        return;

    const ea::string command = eventData[P_COMMAND].GetString();
    if (command.starts_with("dofile "))
        ExecuteFile(command.substr(7));
    else
        ExecuteString(command);
}

} // namespace Urho3D

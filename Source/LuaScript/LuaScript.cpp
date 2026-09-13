//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#include "../Urho3D/Precompiled.h"

#include "LuaScript.h"

#include "LuaBindings.h"
#include "LuaFile.h"
#include "LuaNodeBindings.h"
#include "LuaPackageLoader.h"
#include "LuaScriptContainer.h"
#include "../Urho3D/Core/Context.h"
#include "../Urho3D/Core/Variant.h"
#include "../Urho3D/Engine/EngineEvents.h"
#include "../Urho3D/IO/File.h"
#include "../Urho3D/IO/FileIdentifier.h"
#include "../Urho3D/IO/FileSystem.h"
#include "../Urho3D/IO/Log.h"
#include "../Urho3D/IO/VirtualFileSystem.h"
#include "../Urho3D/Resource/ResourceCache.h"
#include "../Urho3D/Scene/Node.h"
#include "../Urho3D/Scene/Scene.h"

#include <sol/sol.hpp>

namespace Urho3D
{

LuaScript::LuaScript(Context* context)
    : Object(context)
{
    Initialize();
}

LuaScript::~LuaScript()
{
    // Drop event handlers first: they capture sol references which must be
    // released while the Lua state is still alive.
    UnsubscribeFromAllEvents();
}

bool LuaScript::Initialize()
{
    if (luaState_)
        return true;

    // Scripts are resources: that is what gives require() the virtual file system, the
    // container decoding and the file watcher for free.
    LuaFile::RegisterObject(context_);

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

    // Install the VFS searcher while the stack is empty and before any script can run. The one
    // prefix is the empty string: game modules are addressed relative to the mounted resource
    // directories, which is the same naming every other asset in the engine uses.
    packageLoader_ = ea::make_unique<LuaPackageLoader>(context_);
    packageLoader_->Attach(luaState_->lua_state(), { EMPTY_STRING });

    RegisterEngineBindings();

    // Redirect Lua print into the engine log so it is visible in the editor console.
    (*luaState_)["__urho_log_info"] = [](const char* message) { URHO3D_LOGINFO("Lua: {}", message); };
    ExecuteString(
        "function print(...) "
        "local parts = {} "
        "for i = 1, select('#', ...) do parts[#parts + 1] = tostring(select(i, ...)) end "
        "__urho_log_info(table.concat(parts, ' ')) "
        "end",
        "=[print-redirect]");

    // Register as a console command interpreter. The editor console shows
    // every subscriber of E_CONSOLECOMMAND in its interpreter dropdown. Subscribed here rather
    // than in the constructor because Reinitialize() drops every handler and then calls this.
    SubscribeToEvent(E_CONSOLECOMMAND, &LuaScript::HandleConsoleCommand);

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
    if (!packageLoader_)
    {
        URHO3D_LOGERROR("LuaScript is not initialized.");
        return false;
    }

    // A file a host names explicitly is a reload root: a change anywhere below it re-executes
    // this script, which is what makes editing game code while it runs useful.
    return packageLoader_->ExecuteScript(fileName, true);
}

bool LuaScript::ExecuteFileAbsolute(const ea::string& absolutePath)
{
    if (!packageLoader_)
    {
        URHO3D_LOGERROR("LuaScript is not initialized.");
        return false;
    }

    // Deliberately not a second way to run a script: the absolute path is translated into the
    // resource name of a mounted directory, so what executes is still a watched, cacheable
    // resource whose requires are tracked. A path outside every mount has no resource identity,
    // and therefore no reload story, so it is refused instead of silently bypassing the system.
    auto* vfs = context_->GetSubsystem<VirtualFileSystem>();
    const FileIdentifier identifier = vfs ? vfs->GetIdentifierFromAbsoluteName(absolutePath) : FileIdentifier::Empty;
    if (!identifier)
    {
        URHO3D_LOGERRORF(
            "Lua script '%s' is not inside a mounted resource directory, so it cannot be executed as a resource.", absolutePath.c_str());
        return false;
    }

    return ExecuteFile(identifier.ToUri());
}

void LuaScript::Reinitialize()
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

void LuaScript::SetGlobalNode(const ea::string& name, Node* node)
{
    if (luaState_)
        (*luaState_)[name.c_str()] = node;
}

void LuaScript::SetGlobalScene(const ea::string& name, Scene* scene)
{
    if (luaState_)
        (*luaState_)[name.c_str()] = scene;
}

void LuaScript::SubscribeGlobalEvent(const char* eventName, sol::protected_function callback)
{
    if (!luaState_)
    {
        URHO3D_LOGERROR("LuaScript: cannot subscribe, Lua state is not initialized");
        return;
    }
    if (!callback.valid())
    {
        URHO3D_LOGERROR("LuaScript: SubscribeToEvent expects a Lua function as callback");
        return;
    }

    // The lambda copies the sol reference; it is destroyed together with the event
    // handler, which is guaranteed to happen before the Lua state (see destructor).
    SubscribeToEvent(StringHash(eventName),
        [this, callback](Object*, StringHash, VariantMap& eventData) mutable
        {
            InvokeEventCallback(callback, eventData);
        });
}

void LuaScript::SubscribeSenderEvent(Object* sender, const char* eventName, sol::protected_function callback)
{
    if (!luaState_)
    {
        URHO3D_LOGERROR("LuaScript: cannot subscribe, Lua state is not initialized");
        return;
    }
    if (!sender)
    {
        URHO3D_LOGERROR("LuaScript: SubscribeToEvent got null sender");
        return;
    }
    if (!callback.valid())
    {
        URHO3D_LOGERROR("LuaScript: SubscribeToEvent expects a Lua function as callback");
        return;
    }

    // Handler is attached to the sender, so it is automatically removed when
    // the sender is destroyed and Lua callbacks cannot fire on dead objects.
    sender->SubscribeToEvent(StringHash(eventName),
        [this, callback](Object*, StringHash, VariantMap& eventData) mutable
        {
            InvokeEventCallback(callback, eventData);
        });
}

void LuaScript::UnsubscribeEvent(const char* eventName)
{
    UnsubscribeFromEvent(StringHash(eventName));
}

void LuaScript::UnsubscribeSenderEvent(Object* sender, const char* eventName)
{
    UnsubscribeFromEvent(sender, StringHash(eventName));
}

void LuaScript::InvokeEventCallback(sol::protected_function& callback, VariantMap& eventData)
{
    if (!luaState_)
        return;

    sol::protected_function_result result = callback(LuaEventData{&eventData});
    if (!result.valid())
    {
        sol::error err = result;
        URHO3D_LOGERROR("Lua event handler error: {}", err.what());
    }
}

void LuaScript::HandleConsoleCommand(StringHash eventType, VariantMap& eventData)
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
    RegisterMathBindings(*luaState_);
    RegisterCoreBindings(*luaState_, context_);
    RegisterNodeBindings(*luaState_, context_);
    // Resource must be registered before Graphics: Model/Material/Image
    // derive from Resource and sol3 requires base usertypes to exist first.
    RegisterResourceBindings(*luaState_, context_);
    RegisterGraphicsBindings(*luaState_, context_);
    RegisterInputBindings(*luaState_, context_);
    RegisterUIBindings(*luaState_, context_);
    RegisterPhysicsBindings(*luaState_, context_);
    RegisterUrho2DBindings(*luaState_, context_);
    RegisterPhysics2DBindings(*luaState_, context_);
    RegisterAudioBindings(*luaState_, context_);
    RegisterNavigationBindings(*luaState_, context_);
    RegisterNetworkBindings(*luaState_, context_);

    // Event data wrapper: parameters are looked up by name via dynamic indexing,
    // e.g. data.TimeStep, data.Name, data.Node.
    luaState_->new_usertype<LuaEventData>("EventData",
        sol::no_constructor,
        sol::meta_function::index,
        [](LuaEventData& self, const char* name, sol::this_state s) -> sol::object
        {
            if (!self.eventData_)
                return sol::lua_nil;
            const auto iter = self.eventData_->find(StringHash(name));
            if (iter == self.eventData_->end())
                return sol::lua_nil;
            return VariantToLua(sol::state_view(s), iter->second);
        },
        "Contains", [](LuaEventData& self, const char* name) {
            return self.eventData_ && self.eventData_->find(StringHash(name)) != self.eventData_->end();
        });

    // Expose subscription API to Lua. Callbacks receive the EventData wrapper.
    luaState_->set_function("SubscribeToEvent",
        sol::overload(
            [this](const char* eventName, sol::protected_function callback)
            {
                SubscribeGlobalEvent(eventName, std::move(callback));
            },
            [this](Object* sender, const char* eventName, sol::protected_function callback)
            {
                SubscribeSenderEvent(sender, eventName, std::move(callback));
            }));
    luaState_->set_function("UnsubscribeEvent",
        sol::overload(
            [this](const char* eventName) { UnsubscribeEvent(eventName); },
            [this](Object* sender, const char* eventName) { UnsubscribeSenderEvent(sender, eventName); }));
    // tolua-style alias used by the samples (49/50 pause-on-fullscreen-UI).
    luaState_->set_function("UnsubscribeFromEvent",
        sol::overload(
            [this](const char* eventName) { UnsubscribeEvent(eventName); },
            [this](Object* sender, const char* eventName) { UnsubscribeSenderEvent(sender, eventName); }));
}

} // namespace Urho3D

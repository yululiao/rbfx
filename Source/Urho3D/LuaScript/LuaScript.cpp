//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#include "../Precompiled.h"

#include "../LuaScript/LuaScript.h"

#include "../LuaScript/LuaBindings.h"
#include "../LuaScript/LuaNodeBindings.h"
#include "../Core/Context.h"
#include "../Core/Variant.h"
#include "../Engine/EngineEvents.h"
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
    Initialize();

    // Register as a console command interpreter. The editor console shows
    // every subscriber of E_CONSOLECOMMAND in its interpreter dropdown.
    SubscribeToEvent(E_CONSOLECOMMAND, &LuaScript::HandleConsoleCommand);
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

    // Redirect Lua print into the engine log so it is visible in the editor console.
    (*luaState_)["__urho_log_info"] = [](const char* message) { URHO3D_LOGINFO("Lua: {}", message); };
    ExecuteString(
        "function print(...) "
        "local parts = {} "
        "for i = 1, select('#', ...) do parts[#parts + 1] = tostring(select(i, ...)) end "
        "__urho_log_info(table.concat(parts, ' ')) "
        "end",
        "=[print-redirect]");

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

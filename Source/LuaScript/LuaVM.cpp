// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#include "../Urho3D/Precompiled.h"

#include "LuaVM.h"

#include "LuaBindings.h"
#include "LuaFile.h"
#include "LuaNodeBindings.h"
#include "LuaPackageLoader.h"

#include "../Urho3D/Core/Context.h"
#include "../Urho3D/Core/StringUtils.h"
#include "../Urho3D/IO/Log.h"

#include <sol/sol.hpp>

namespace Urho3D
{

LuaVM::LuaVM(Context* context, const LuaVMConfig& config)
    : Object(context)
    , config_(config)
{
}

LuaVM::~LuaVM()
{
    // Drop event handlers while the Lua state is still alive: their lambdas capture sol
    // references which must be released before luaState_ (and its lua_State) is gone.
    UnsubscribeFromAllEvents();
}

bool LuaVM::Initialize()
{
    if (luaState_)
        return true;

    // Scripts are resources: that is what gives require() the virtual file system, the
    // container decoding and the file watcher for free.
    LuaFile::RegisterObject(context_);

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

    // Install the VFS searcher while the stack is empty and before any script can run, with
    // the prefixes from the configuration: the default answers from the mounted resource
    // directories (the naming every other asset uses), a host's plugin scheme sits in front.
    packageLoader_ = ea::make_unique<LuaPackageLoader>(context_);
    packageLoader_->Attach(luaState_->lua_state(), config_.requirePrefixes_);

    RegisterEngineBindings();

    // Redirect Lua print into the engine log under the configured prefix, so script output
    // is visible (and attributable to this VM) in the engine console.
    (*luaState_)["__vm_log_info"] = [prefix = config_.logPrefix_](const char* message)
    {
        URHO3D_LOGINFO("{}: {}", prefix.c_str(), message);
    };
    ExecuteString(
        "function print(...) "
        "local parts = {} "
        "for i = 1, select('#', ...) do parts[#parts + 1] = tostring(select(i, ...)) end "
        "__vm_log_info(table.concat(parts, ' ')) "
        "end",
        "=[print-redirect]");

    OnAfterInitialize();

    return true;
}

bool LuaVM::ExecuteString(const ea::string& code, const ea::string& chunkName)
{
    if (!luaState_)
    {
        URHO3D_LOGERROR("LuaVM is not initialized.");
        return false;
    }

    try
    {
        const char* name = chunkName.empty() ? "=[string]" : chunkName.c_str();
        sol::load_result loaded = luaState_->load(code.c_str(), name);
        if (!loaded.valid())
        {
            sol::error err = loaded;
            URHO3D_LOGERRORF("%s load error in %s: %s", config_.logPrefix_.c_str(), name, err.what());
            return false;
        }

        sol::protected_function script = loaded;
        sol::protected_function_result result = script();
        if (!result.valid())
        {
            sol::error err = result;
            URHO3D_LOGERRORF("%s runtime error in %s: %s", config_.logPrefix_.c_str(), name, err.what());
            return false;
        }
    }
    catch (const std::exception& e)
    {
        URHO3D_LOGERRORF("%s exception: %s", config_.logPrefix_.c_str(), e.what());
        return false;
    }

    return true;
}

sol::state& LuaVM::GetState()
{
    return *luaState_;
}

void LuaVM::SubscribeGlobalEvent(const char* eventName, sol::protected_function callback)
{
    if (!luaState_)
    {
        URHO3D_LOGERROR("LuaVM: cannot subscribe, Lua state is not initialized");
        return;
    }
    if (!callback.valid())
    {
        URHO3D_LOGERROR("LuaVM: subscribe expects a Lua function as callback");
        return;
    }

    // The lambda copies the sol reference; handlers are removed in the destructor before
    // the Lua state is torn down, so callbacks can never fire on a destroyed state.
    SubscribeToEvent(StringHash(eventName),
        [this, callback](Object*, StringHash, VariantMap& eventData) mutable
        {
            InvokeEventCallback(callback, eventData);
        });
}

void LuaVM::SubscribeSenderEvent(Object* sender, const char* eventName, sol::protected_function callback)
{
    if (!luaState_)
    {
        URHO3D_LOGERROR("LuaVM: cannot subscribe, Lua state is not initialized");
        return;
    }
    if (!sender)
    {
        URHO3D_LOGERROR("LuaVM: subscribe got null sender");
        return;
    }
    if (!callback.valid())
    {
        URHO3D_LOGERROR("LuaVM: subscribe expects a Lua function as callback");
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

void LuaVM::UnsubscribeEvent(const char* eventName)
{
    UnsubscribeFromEvent(StringHash(eventName));
}

void LuaVM::UnsubscribeSenderEvent(Object* sender, const char* eventName)
{
    UnsubscribeFromEvent(sender, StringHash(eventName));
}

void LuaVM::InvokeEventCallback(sol::protected_function& callback, VariantMap& eventData)
{
    if (!luaState_)
        return;

    sol::protected_function_result result = callback(LuaEventData{&eventData});
    if (!result.valid())
    {
        sol::error err = result;
        URHO3D_LOGERROR("{} event handler error: {}", config_.logPrefix_.c_str(), err.what());
    }
}

void LuaVM::RegisterEngineBindings()
{
    // Reuse the engine's exported binding modules so scripts get the full engine
    // API (Node/Component/Scene/resources/...) in the same environment in every VM.
    // Order matters: Resource must precede Graphics (Model/Material derive from Resource).
    RegisterMathBindings(*luaState_);
    RegisterCoreBindings(*luaState_, context_);
    RegisterNodeBindings(*luaState_, context_);
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
    // e.g. data.TimeStep, data.Node.
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

    // Expose the subscription API to Lua. Callbacks receive the EventData wrapper. The
    // sender overload lets a script listen to one object only (e.g. one Node's events), and
    // the handler dies with the sender. Same shape in every VM so shared helper scripts
    // behave the same way in the game and in editor plugins.
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

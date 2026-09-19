//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#pragma once

#include "../Urho3D/Container/Ptr.h"
#include "../Urho3D/Core/Object.h"
#include "../Urho3D/Core/Variant.h"

#include "Export.h"

#include <EASTL/shared_ptr.h>
#include <EASTL/string.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/vector.h>

// Lightweight forward declarations for sol types; the heavy <sol/sol.hpp> stays in the .cpp
// so consumers that merely reference the subsystem do not pull the bindings.
#include <sol/forward.hpp>

namespace Urho3D
{

class LuaPackageLoader;

/// Per-instance configuration for a LuaVM: the strings that would otherwise be hardcoded,
/// so several VMs can coexist with distinguishable log output and require() resolution.
struct RBFXLUA_API LuaVMConfig
{
    /// Prefix under which print() output and error messages appear in the engine log.
    ea::string logPrefix_ = "LuaVM";
    /// Search prefixes require() consults, in order. The default answers from the mounted
    /// resource directories; a plugin host puts its scheme in front so its modules win.
    ea::vector<ea::string> requirePrefixes_ = { EMPTY_STRING };
};

/// Lightweight read-only wrapper over event data that allows Lua callbacks
/// to read event parameters by name: data.TimeStep, data.Name, etc.
///
/// The raw eventData_ pointer is only valid during the synchronous dispatch.
/// To stop a script that stashes the wrapper (for example
/// ``SubscribeToEvent("Update", function(d) saved = d end)``) from reading a
/// dead VariantMap later, a shared validity token is flipped false by the
/// bridge right after the callback returns. sol copies this struct into the
/// userdata, and every copy shares the same token, so a stale view reads nil
/// instead of dereferencing freed memory.
class LuaEventData
{
public:
    explicit LuaEventData(const VariantMap* eventData)
        : eventData_(eventData), valid_(ea::make_shared<bool>(true))
    {
    }

    /// Invalidate this view and every copy a script kept. Safe to call twice.
    void Invalidate()
    {
        if (valid_)
            *valid_ = false;
        eventData_ = nullptr;
    }

    /// The live event map during dispatch, or null once invalidated / self null.
    const VariantMap* Get() const { return (valid_ && *valid_) ? eventData_ : nullptr; }

    const VariantMap* eventData_;

private:
    ea::shared_ptr<bool> valid_;
};

/// Base class of every engine-bound Lua virtual machine. It owns a DEDICATED sol state (a
/// separate lua_State) populated with the full set of engine bindings, print() redirected
/// into the engine log under the configured prefix, and an event bridge that lets scripts
/// subscribe to engine events, globally or from a specific sender. It does NOT create any
/// API table of its own: subclasses (and their owners) register the globals they need into
/// the state right after Initialize().
///
/// The flavors in the tree: LuaVMHost, a VM that also hosts a folder of plugin scripts
/// (plugin pipeline plus a callback registry for owner-side draw/click/completion
/// callbacks), and EngineLuaVM, the game-logic VM (console interpreter, reload-root script
/// execution, Play/Stop reinitialization). Subclasses bake in their identity through the
/// configuration and each purpose-defined subclass gets its own Context subsystem slot.
class RBFXLUA_API LuaVM : public Object
{
    URHO3D_OBJECT(LuaVM, Object);

public:
    /// Construct. The configuration is captured once; it cannot change afterwards.
    explicit LuaVM(Context* context, const LuaVMConfig& config = {});
    /// Destruct. Unsubscribes from all events before destroying the Lua state.
    ~LuaVM() override;

    /// Create the Lua state, install require() and register the engine bindings. Idempotent.
    bool Initialize();

    /// Execute Lua code from a string against this state.
    bool ExecuteString(const ea::string& code, const ea::string& chunkName = EMPTY_STRING);

    /// Return whether the Lua state is initialized.
    bool IsInitialized() const { return !!luaState_; }

    /// Return sol state.
    sol::state& GetState();

    /// Subscribe a Lua callback to an engine event broadcast on the Context.
    void SubscribeGlobalEvent(const char* eventName, sol::protected_function callback);
    /// Subscribe a Lua callback to an engine event sent by a specific sender. The handler is
    /// attached to the sender, so it is removed automatically when the sender is destroyed.
    void SubscribeSenderEvent(Object* sender, const char* eventName, sol::protected_function callback);
    /// Unsubscribe Lua callbacks from an event type, any sender.
    void UnsubscribeEvent(const char* eventName);
    /// Unsubscribe Lua callbacks from an event type sent by a specific sender.
    void UnsubscribeSenderEvent(Object* sender, const char* eventName);

protected:
    /// Called at the end of every successful Initialize -- including those inside a rebuild
    /// like EngineLuaVM::Reinitialize -- with the state alive. Lets subclasses (re)attach
    /// what a state reset drops, without hiding the base Initialize().
    virtual void OnAfterInitialize() {}

    /// Invoke a Lua event callback with a read-only EventData wrapper. Usable by subclasses
    /// (the host's one-shot completion callbacks are event-shaped too).
    void InvokeEventCallback(sol::protected_function& callback, VariantMap& eventData);

    /// Captured configuration (log prefix, require prefixes).
    LuaVMConfig config_;
    /// Lua virtual machine state.
    ea::unique_ptr<sol::state> luaState_;
    /// require() through the virtual file system, plus the module graph of this VM.
    /// Destroyed before luaState_ (declaration order), which is the order the searcher requires.
    ea::unique_ptr<LuaPackageLoader> packageLoader_;

private:
    /// Register the engine usertypes/bindings onto the state and the event bridge.
    void RegisterEngineBindings();

    /// Append a callback to the fan-out list for (sender, eventType) and (re)attach the
    /// dispatcher. Shared by the global and sender subscribe entry points.
    ///
    /// Why a fan-out list: rbfx Object::SubscribeToEvent(eventType, handler) *replaces*
    /// (rather than appends) the handler for a given (object, eventType) pair, so a naive
    /// per-callback subscribe would let only the last Lua callback for the same event
    /// survive. Every callback for an (sender, eventType) pair is kept in an opaque
    /// registry (defined in the .cpp, where the sol types are complete) and a single
    /// stateless dispatcher is (re)attached that looks the pair up at fire time and invokes
    /// all of them. `sender == nullptr` denotes a global (context) event.
    void AddSubscriber(Object* sender, StringHash eventType, sol::protected_function callback);

    /// Opaque event fan-out registry (pimpl: keeps sol::protected_function containers out of
    /// this header, which only forward-declares the sol types). Destroyed before luaState_
    /// (declaration order) so the stored callbacks are released while the state is alive.
    struct Subscribers;
    ea::unique_ptr<Subscribers> subscribers_;
};

} // namespace Urho3D

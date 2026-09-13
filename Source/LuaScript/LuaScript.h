//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#pragma once

#include "../Urho3D/Core/Object.h"
#include "../Urho3D/Core/Variant.h"

#include "Export.h"

#include "../Urho3D/Math/Color.h"
#include "../Urho3D/Math/Quaternion.h"
#include "../Urho3D/Math/Vector2.h"
#include "../Urho3D/Math/Vector3.h"

#include <EASTL/unique_ptr.h>

namespace Urho3D
{

class Node;
class Scene;
class LuaPackageLoader;

}

struct lua_State;

// Lightweight forward declarations for sol types (state, protected_function aliases, etc.).
#include <sol/forward.hpp>

namespace Urho3D
{

/// Lightweight read-only wrapper over event data that allows Lua callbacks
/// to read event parameters by name: data.TimeStep, data.Name, etc.
class LuaEventData
{
public:
    explicit LuaEventData(const VariantMap* eventData)
        : eventData_(eventData)
    {
    }

    const VariantMap* eventData_;
};

/// Lua scripting subsystem.
class RBFXLUA_API LuaScript : public Object
{
    URHO3D_OBJECT(LuaScript, Object);

public:
    /// Construct.
    explicit LuaScript(Context* context);
    /// Destruct. Unsubscribes from all events before destroying the Lua state.
    ~LuaScript() override;

    /// Initialize Lua state and register engine bindings. Idempotent.
    bool Initialize();

    /// Execute Lua code from a string.
    bool ExecuteString(const ea::string& code, const ea::string& chunkName = EMPTY_STRING);
    /// Execute Lua code from a resource file. The name is resolved through the virtual file
    /// system, so a packaged .luc is preferred over a .lua of the same base name. Registers the
    /// script as a reload root.
    bool ExecuteFile(const ea::string& fileName);
    /// Same, for a path on disk: it is translated into the resource name of the mounted
    /// directory that contains it, and refused when it belongs to none, because a script with no
    /// resource identity could neither be watched nor reloaded.
    bool ExecuteFileAbsolute(const ea::string& absolutePath);
    /// Destroy and recreate Lua state. Used by the editor to reset game state between play sessions.
    void Reinitialize();

    /// Expose a Node (or a Scene subclass) as a global Lua variable.
    void SetGlobalNode(const ea::string& name, Node* node);
    /// Expose a Scene as a global Lua variable (preserves Scene* type for sol3).
    void SetGlobalScene(const ea::string& name, Scene* scene);
    /// Return whether Lua state is initialized.
    bool IsInitialized() const { return !!luaState_; }

    /// Subscribe Lua callback to an event from any sender.
    void SubscribeGlobalEvent(const char* eventName, sol::protected_function callback);
    /// Subscribe Lua callback to an event from a specific sender.
    void SubscribeSenderEvent(Object* sender, const char* eventName, sol::protected_function callback);
    /// Unsubscribe Lua callbacks from an event type, any sender.
    void UnsubscribeEvent(const char* eventName);
    /// Unsubscribe Lua callbacks from an event type sent by a specific sender.
    void UnsubscribeSenderEvent(Object* sender, const char* eventName);

    /// Return sol state.
    sol::state& GetState();
    /// Return raw Lua state.
    lua_State* GetLuaState() const;

    /// Return the require()/hot-reload bookkeeping installed on this VM, or null before
    /// Initialize(). Hosts use it to add search prefixes or to reset the tracked module graph.
    LuaPackageLoader* GetPackageLoader() const { return packageLoader_.get(); }

private:
    /// Register Urho3D types available to Lua.
    void RegisterEngineBindings();
    /// Handle a command entered in the editor console.
    void HandleConsoleCommand(StringHash eventType, VariantMap& eventData);
    /// Invoke Lua callback with event data wrapper.
    void InvokeEventCallback(sol::protected_function& callback, VariantMap& eventData);

    /// Lua virtual machine state.
    ea::unique_ptr<sol::state> luaState_;
    /// VFS backed require() and the module dependency graph of this VM. Must be destroyed
    /// before luaState_ because the searcher closure refers to it.
    ea::unique_ptr<LuaPackageLoader> packageLoader_;
};

} // namespace Urho3D

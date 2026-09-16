//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#pragma once

#include "LuaVM.h"

namespace Urho3D
{

class Node;
class Scene;

/// The engine's game-logic VM: the Lua subsystem game scripts run in. Beyond the shared VM
/// core it owns the game-specific lifecycle: scripts named by resource execute as reload
/// roots (hot reload while the game runs), the active scene/node are exposed as globals,
/// and it answers as the "LuaScript" interpreter in the editor console. Reinitialize()
/// destroys and recreates the state so the editor can reset all game state between Play
/// sessions. Being its own type gives it a dedicated Context subsystem slot, distinct from
/// every LuaVMHost flavor.
class RBFXLUA_API EngineLuaVM : public LuaVM
{
    URHO3D_OBJECT(EngineLuaVM, LuaVM);

public:
    /// Construct. Initializes immediately: game code expects the subsystem ready on arrival.
    explicit EngineLuaVM(Context* context);

    /// Destroy and recreate the Lua state. Used by the editor to reset game state between play sessions.
    void Reinitialize();

    /// Execute Lua code from a resource file. The name is resolved through the virtual file
    /// system, so a packaged .luc is preferred over a .lua of the same base name. Registers the
    /// script as a reload root.
    bool ExecuteFile(const ea::string& fileName);

    /// Expose a Node (or a Scene subclass) as a global Lua variable.
    void SetGlobalNode(const ea::string& name, Node* node);
    /// Expose a Scene as a global Lua variable (preserves Scene* type for sol3).
    void SetGlobalScene(const ea::string& name, Scene* scene);

protected:
    /// Report the script-container key source once per process and register as the console
    /// command interpreter. Runs at the end of every Initialize, so Reinitialize()
    /// re-subscribes: a state reset drops every handler.
    void OnAfterInitialize() override;

private:
    /// Handle a command entered in the editor console.
    void HandleConsoleCommand(StringHash eventType, VariantMap& eventData);
};

} // namespace Urho3D

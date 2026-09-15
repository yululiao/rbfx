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

#include <EASTL/unique_ptr.h>

// Lightweight forward declarations for sol types; the heavy <sol/sol.hpp> stays in the .cpp
// so consumers that merely reference the subsystem (the editor) do not pull the bindings.
#include <sol/forward.hpp>

namespace Urho3D
{

class LuaPackageLoader;
class MountPoint;

/// Editor-facing Lua scripting subsystem used to author editor plugins in Lua, inspired by
/// Godot's script-based editor extensions.
///
/// It owns a DEDICATED sol state (a separate lua_State) that is populated with the full set
/// of engine bindings plus the VM-plumbing half of the "Editor" table (log, subscribe,
/// exec), so editor plugins see Node/Component/Scene/resources and editor capabilities in
/// one environment. Because the state is created and driven entirely inside RbfxLuaScript,
/// it shares the module's single Lua VM and single sol3 instance and is therefore safe. It
/// is intentionally NOT the game's "LuaScript" state: that one is reinitialized on every
/// play/stop cycle, which would wipe editor plugins.
///
/// The editor-capability half of "Editor" (project access, tabs, menus, windows, selection,
/// build pipeline) and the "imgui" table are registered into this state directly by the
/// editor through GetState(), so editor types never cross into this library.
class RBFXLUA_API EditorLuaScript : public Object
{
    URHO3D_OBJECT(EditorLuaScript, Object);

public:
    /// Construct.
    explicit EditorLuaScript(Context* context);
    /// Destruct. Unsubscribes from all events before destroying the Lua state.
    ~EditorLuaScript() override;

    /// Create the editor Lua state, register engine + editor bindings. Idempotent.
    bool Initialize();

    /// Scan an absolute directory for "*.lua" and execute each as an editor plugin.
    /// The directory is remembered so Editor.reloadPlugins() can re-run it.
    void LoadPlugins(const ea::string& absoluteDir);

    /// Execute Lua code from a string against the editor state.
    bool ExecuteString(const ea::string& code, const ea::string& chunkName = EMPTY_STRING);
    /// Execute Lua code from an absolute file path against the editor state.
    bool ExecuteFileAbsolute(const ea::string& absolutePath);

    /// Return whether the editor Lua state is initialized.
    bool IsInitialized() const { return !!luaState_; }

    /// Return sol state.
    sol::state& GetState();

    /// Subscribe a Lua callback to an engine event broadcast on the Context.
    void SubscribeGlobalEvent(const char* eventName, sol::protected_function callback);
    /// Unsubscribe Lua callbacks from an event type, any sender.
    void UnsubscribeEvent(const char* eventName);

    /// Invoke a UI callback (tab draw / menu click) previously registered by Editor.addTab or
    /// Editor.addMenuItem. Called by the editor from within the ImGui frame. Unknown handles are
    /// ignored so a plugin that was removed across a reload cannot crash the editor.
    void InvokeUICallback(unsigned long long handle);

    /// Invoke a one-shot callback registered by Editor.build with the parameters of the event that
    /// ended the build, then forget the handle. Unlike a tab or a menu item the registration has no
    /// meaning after the one call, and the editor is the only party that knows when that is.
    void InvokeOneShotCallback(unsigned long long handle, VariantMap& eventData);

    /// Store a Lua UI callback and return an opaque handle the editor invokes it through. Used
    /// by the editor-side registrations of Editor.addTab / addMenuItem / addWindow / build.
    unsigned long long RegisterUICallback(sol::protected_function callback);
    /// Drop a registered UI callback. Called by the editor when a registration it handed out
    /// turned out to not be needed after all (e.g. a build refused before it ran).
    void DropUICallback(unsigned long long handle);

    /// Re-run the last plugin directory passed to LoadPlugins (no-op before the first load).
    /// The editor pairs this with resetting its own UI bookkeeping.
    void ReloadPlugins();

private:
    /// Register the engine usertypes/bindings onto the editor state and the event bridge.
    void RegisterEngineBindings();
    /// Register the "Editor" table with its VM-plumbing functions (log, subscribe, exec,
    /// reload). The editor-capability functions are appended by the editor itself.
    void RegisterEditorBindings();
    /// Invoke a Lua event callback with a read-only EventData wrapper.
    void InvokeEventCallback(sol::protected_function& callback, VariantMap& eventData);
    /// Expose the plugin directory to the virtual file system under its own scheme, so that
    /// require() can reach modules in subfolders. Re-mounts only when the directory changed.
    void MountPluginDir(const ea::string& absoluteDir);

    /// Lua virtual machine state dedicated to editor plugins.
    ea::unique_ptr<sol::state> luaState_;
    /// require() through the virtual file system, plus the module graph of the editor VM.
    /// Destroyed before luaState_ (declaration order), which is the order the searcher requires.
    ea::unique_ptr<LuaPackageLoader> packageLoader_;
    /// Last plugin directory passed to LoadPlugins, for reload support.
    ea::string pluginDir_;
    /// Mount point created for pluginDir_, kept alive so project switches can replace it.
    SharedPtr<MountPoint> pluginMount_;
    /// Directory pluginMount_ points at, normalized. Lets reloads skip the re-mount.
    ea::string mountedPluginDir_;
};

} // namespace Urho3D

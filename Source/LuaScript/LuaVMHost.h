//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#pragma once

#include "LuaVM.h"

#include <EASTL/unordered_map.h>

namespace Urho3D
{

class MountPoint;

/// Host flavor of LuaVM: the plugin directory is mounted under this scheme, and it becomes
/// the first require() prefix so plugin modules resolve ahead of ordinary resources.
struct RBFXLUA_API LuaVMHostConfig : LuaVMConfig
{
    /// VFS scheme the plugin directory is mounted under ("scheme://..." require() prefixes).
    ea::string mountScheme_ = "luavm";
};

/// A LuaVM that also hosts a folder of plugin scripts. The plugin pipeline mounts the
/// directory under the configured scheme, executes every top-level "*.lua" as a plugin
/// body, and re-runs them wholesale on ReloadPlugins; the callback registry lets scripts
/// hand functions to the owner, which invokes them later from its own side (the editor:
/// tab draw, menu click, build completion) through opaque handles.
///
/// Nothing in the class knows about any particular use: the editor's flavor is
/// EditorLuaVMHost, which only bakes in the configuration. Hosts register no API tables of
/// their own; the owner registers its globals into the state right after Initialize().
class RBFXLUA_API LuaVMHost : public LuaVM
{
    URHO3D_OBJECT(LuaVMHost, LuaVM);

public:
    /// Construct. The configuration is captured once; it cannot change afterwards.
    explicit LuaVMHost(Context* context, const LuaVMHostConfig& config = {});
    /// Destruct. Clears the callback registry and unmounts the plugin directory.
    ~LuaVMHost() override;

    /// Scan an absolute directory for "*.lua" and execute each as a plugin. The directory is
    /// remembered so ReloadPlugins() can re-run it.
    void LoadPlugins(const ea::string& absoluteDir);

    /// Execute Lua code from an absolute file path against this state. Plugin bodies are
    /// not registered as reload roots -- ReloadPlugins() remains the entry point, because
    /// re-running a plugin body has to come with the callback reset.
    bool ExecuteFileAbsolute(const ea::string& absolutePath);

    /// Store a Lua callback and return an opaque handle it is invoked through. Owners use this
    /// for anything drawn or fired later from their side (the editor: tab draw, menu click,
    /// build completion). Unknown handles are ignored on invoke, so a plugin that was removed
    /// across a reload cannot crash the owner.
    unsigned long long RegisterCallback(sol::protected_function callback);
    /// Invoke a registered callback. Called by the owner at its own discretion (e.g. from
    /// within the ImGui frame).
    void InvokeCallback(unsigned long long handle);
    /// Invoke a one-shot callback with the parameters of the event it was registered for,
    /// then forget the handle. Unlike a persistent callback the registration has no meaning
    /// after the one call, and the owner is the only party that knows when that is.
    void InvokeOneShotCallback(unsigned long long handle, VariantMap& eventData);
    /// Drop a registered callback. Called by the owner when a handle it handed out turned out
    /// to not be needed after all (e.g. a build refused before it ran).
    void DropCallback(unsigned long long handle);

    /// Re-run the last plugin directory passed to LoadPlugins (no-op before the first load).
    /// Owners that keep bookkeeping about plugin-registered items pair this with their own
    /// reset (the editor clears its UI registries).
    void ReloadPlugins();

private:
    /// Expose the plugin directory to the virtual file system under the configured scheme, so
    /// that require() can reach modules in subfolders. Re-mounts only when the directory changed.
    void MountPluginDir(const ea::string& absoluteDir);

    /// Callbacks registered through RegisterCallback, keyed by handle. Per instance on purpose:
    /// a second host must never see (or clear) another host's callbacks. Held behind a pointer
    /// and aliased: instantiating the map needs the full sol headers (its value is a sol type,
    /// and EASTL's unordered_map requires a complete value type), which must stay in the .cpp.
    using CallbackMap = ea::unordered_map<unsigned long long, sol::protected_function>;
    ea::unique_ptr<CallbackMap> callbacks_;

    /// Captured mount scheme (the rest of the configuration lives in the base class).
    ea::string mountScheme_;
    /// Last plugin directory passed to LoadPlugins, for reload support.
    ea::string pluginDir_;
    /// Mount point created for pluginDir_, kept alive so repeated loads can replace it.
    SharedPtr<MountPoint> pluginMount_;
    /// Directory pluginMount_ points at, normalized. Lets reloads skip the re-mount.
    ea::string mountedPluginDir_;
};

} // namespace Urho3D

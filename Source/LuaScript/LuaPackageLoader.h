//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#pragma once

#include "../Urho3D/Core/Object.h"

#include "Export.h"

#include <EASTL/functional.h>
#include <EASTL/string.h>
#include <EASTL/unordered_map.h>
#include <EASTL/unordered_set.h>
#include <EASTL/vector.h>

struct lua_State;

namespace Urho3D
{

class ResourceCache;

/// One Lua VM's require() implementation: a package searcher that resolves modules through
/// the virtual file system, plus the dependency bookkeeping that makes module-level hot
/// reload possible.
///
/// Why a searcher instead of package.path: package.path is handed to the C library fopen, so
/// it only ever sees the real file system. Modules inside a .pak or in APK assets are invisible
/// to it, which is precisely the configuration a shipping build uses. Going through
/// ResourceCache/LuaFile instead means require() inherits the VFS, the container decoding and
/// the file watcher without knowing about any of them.
///
/// Two stacks are tracked, and the order matters:
///   require("a.b") -> searcher runs while the *requiring* module is still on top of the
///   execution stack, so the parent/child edge is recorded there; the child is only pushed
///   when its opener actually starts running.
///
///   A module that is already in package.loaded never reaches a searcher at all, so the global
///   require is wrapped to record the edge before delegating. Without that, every dependency
///   edge except the very first load of a module would be invisible, and a changed file would
///   reload the modules the searcher happened to see while its other dependants kept serving
///   the stale copy.
///
/// Reload semantics (agreed scope): module level. When a script file changes, every module
/// that transitively depends on it is dropped from package.loaded, and the registered roots
/// are re-executed in registration order. Instance state is not migrated. Editor plugins
/// deliberately register no roots - Editor.reloadPlugins() remains the way to re-run them,
/// because re-running a plugin body would duplicate its UI registrations.
class RBFXLUA_API LuaPackageLoader : public Object
{
    URHO3D_OBJECT(LuaPackageLoader, Object);

public:
    explicit LuaPackageLoader(Context* context);
    ~LuaPackageLoader() override;

    /// Insert the searcher into \p L's package.searchers, ahead of the file searcher but after
    /// preload. \p prefixes are prepended, in order, when turning a module name into a resource
    /// name; an empty prefix means "search the normal resource roots".
    void Attach(lua_State* L, const ea::vector<ea::string>& prefixes);

    /// Stop tracking the VM. Leaves the searcher and the wrapped require in place; both refer to
    /// this object through an upvalue, so the VM must not outlive it.
    void Detach();

    /// Turn a name a script or host typed into the resource name that actually exists, or an
    /// empty string when nothing does. An extensionless name and a ".lua" one both resolve to a
    /// packaged ".luc" whenever there is one; an explicit ".luc" is matched literally.
    /// \p ambiguity is set when both spellings exist for one base name, whichever one was chosen.
    ea::string ResolveResourceName(ResourceCache* cache, const ea::string& baseName, bool& ambiguity) const;

    /// Load and run a script resource as a top level chunk. When \p registerRoot is true the
    /// script becomes a reload root: a change anywhere below it re-executes this script.
    bool ExecuteScript(const ea::string& resourceName, bool registerRoot);

    /// Drop every root registered by a previous ExecuteScript call.
    void ClearRoots();

    /// Forget the whole graph and clear every tracked module from package.loaded, so the next
    /// require() re-executes it. Used by hosts that re-run all of their entry scripts, where
    /// keeping the previously required module tables would silently serve stale code.
    void ResetTracking();

    /// True when the searcher is installed on a VM.
    bool IsAttached() const { return luaState_ != nullptr; }

private:
    /// package.searchers entry. Returns a loader function, or a string describing the failure.
    static int Searcher(lua_State* L);
    /// Loader function returned by the searcher; owns the execution stack around the chunk.
    static int Opener(lua_State* L);
    /// Global require() replacement. Records who asked, then delegates: a module that is already
    /// in package.loaded is answered by require() itself and never reaches the searcher, so the
    /// dependency graph would otherwise lose every edge that is not the very first load.
    static int RequireWrapper(lua_State* L);

    int DoSearch(lua_State* L);
    int DoOpen(lua_State* L);
    /// Install RequireWrapper over the VM's global require, once per VM.
    void WrapRequire(lua_State* L);

    /// Queue changed resource names; applied on the next frame so that the ordering of our
    /// handler versus ResourceCache's own reload is irrelevant.
    void HandleFileChanged(StringHash eventType, VariantMap& eventData);
    void HandleBeginFrame(StringHash eventType, VariantMap& eventData);

    /// Apply queued changes. Returns the number of roots re-executed.
    unsigned FlushPendingChanges();

    /// Collect a module and everything that transitively depends on it.
    void CollectDependents(const ea::string& moduleName, ea::unordered_set<ea::string>& result) const;

    /// package.loaded[name] = nil
    void ForgetLoadedModule(const ea::string& moduleName);

    /// Call the global OnReload(changedModules) hook when a script defines one.
    void InvokeReloadHook(const ea::vector<ea::string>& moduleNames);
    /// VM this instance serves. Owned by the host, must outlive this object.
    lua_State* luaState_ = nullptr;
    /// Prefixes tried, in order, when resolving a module name.
    ea::vector<ea::string> prefixes_;
    /// Modules currently executing; the last entry is the "requiring" module in a searcher.
    ea::vector<ea::string> executing_;
    /// Per module bookkeeping, keyed by the module name as it was required.
    struct ModuleInfo
    {
        /// Resource the module body comes from.
        ea::string resourceName;
        /// LuaFile load serial the compiled chunk came from. Comparing it against the current
        /// serial is what tells us ResourceCache has actually re-read the file; it removes any
        /// dependence on the order in which our event handler and ResourceCache's run.
        unsigned serial = 0;
        /// Order in which execution finished; re-runs follow it so dependencies come first.
        unsigned finishOrder = 0;
    };
    ea::unordered_map<ea::string, ModuleInfo> modules_;
    /// Reverse edges: module -> the modules that required it.
    ea::unordered_map<ea::string, ea::unordered_set<ea::string>> dependents_;
    /// Resource -> modules loaded from it, so a file change finds its modules.
    ea::unordered_map<ea::string, ea::unordered_set<ea::string>> modulesByResource_;
    /// Top level scripts, in the order they were executed.
    ea::vector<ea::string> roots_;
    /// Frames spent waiting for ResourceCache on a given resource. Also the queue: an entry
    /// exists exactly while a change is still waiting to be applied.
    ea::unordered_map<ea::string, unsigned> pendingWaits_;
    /// Monotonic counter backing ModuleInfo::finishOrder.
    unsigned finishOrderCounter_ = 0;
    /// Shadowing reports are per module name, and only ever printed once each.
    ea::unordered_set<ea::string> warnedShadowed_;
};

} // namespace Urho3D

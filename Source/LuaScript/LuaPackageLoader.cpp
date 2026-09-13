//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#include "../Urho3D/Precompiled.h"

#include "LuaPackageLoader.h"

#include "LuaFile.h"
#include "../Urho3D/Core/Context.h"
#include "../Urho3D/Core/CoreEvents.h"
#include "../Urho3D/Core/StringUtils.h"
#include "../Urho3D/IO/Log.h"
#include "../Urho3D/Resource/ResourceCache.h"
#include "../Urho3D/Resource/ResourceEvents.h"

// Raw Lua C API on purpose: this translation unit must not include <sol/sol.hpp>, because the
// vendored lua.h has no extern "C" block and sol3's own inclusion of it would clash.
extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>

namespace Urho3D
{

namespace
{

/// Frames a changed resource may sit in the queue before we insist on reloading it ourselves.
/// ResourceCache subscribes to E_FILECHANGED too and the handler order between it and us is
/// arbitrary; the load-serial comparison below settles the question either way, this is only
/// the safety net for the case where the resource is cached but nothing reloaded it.
constexpr unsigned ForceReloadAfterPendingFrames = 2;

/// Packaged extensions first: a shipped .luc is what a development .lua sitting next to it is
/// supposed to lose against.
constexpr size_t kExtensionCount = 2;
constexpr const char* const kExtensions[kExtensionCount] = { ".luc", ".lua" };

size_t ExtensionLength(const char* extension)
{
    size_t length = 0;
    while (extension[length])
        ++length;
    return length;
}

/// True when \p name ends in this exact extension, case insensitively.
bool EndsWithExtension(const ea::string& name, const char* extension)
{
    const size_t length = ExtensionLength(extension);
    if (name.length() < length)
        return false;
    for (size_t i = 0; i < length; ++i)
    {
        const char candidate = name[name.length() - length + i];
        if (ToLower(static_cast<unsigned char>(candidate)) != extension[i])
            return false;
    }
    return true;
}

/// Index in \p kExtensions of the script extension \p name carries, or -1 when it has none.
int LuaExtensionIndex(const ea::string& name)
{
    for (size_t i = 0; i < kExtensionCount; ++i)
    {
        if (EndsWithExtension(name, kExtensions[i]))
            return static_cast<int>(i);
    }
    return -1;
}

bool EndsWithLuaExtension(const ea::string& name)
{
    return LuaExtensionIndex(name) >= 0;
}

/// Case-insensitive comparison key for resource names. The file watcher reports whatever case
/// the operating system has on disk while a script may require a different one, and resource
/// names are hashed case-sensitively everywhere in the engine. Over-matching here only costs a
/// redundant module re-run, under-matching would silently miss a hot reload, so this side is
/// deliberately forgiving.
ea::string MakeWatchKey(const ea::string& resourceName)
{
    ea::string key = resourceName;
    for (char& character : key)
        character = static_cast<char>(ToLower(static_cast<unsigned char>(character)));
    return key;
}

/// Pushes a module name for the duration of one chunk execution. Longjmp based error
/// propagation skips destructors, so the failure path calls Leave() explicitly.
class ExecutionScope
{
public:
    ExecutionScope(ea::vector<ea::string>& stack, const ea::string& moduleName)
        : stack_(stack)
    {
        stack_.push_back(moduleName);
    }

    ~ExecutionScope()
    {
        Leave();
    }

    void Leave()
    {
        if (!stack_.empty())
            stack_.pop_back();
    }

    ExecutionScope(const ExecutionScope&) = delete;
    ExecutionScope& operator=(const ExecutionScope&) = delete;

private:
    ea::vector<ea::string>& stack_;
};

} // namespace

LuaPackageLoader::LuaPackageLoader(Context* context)
    : Object(context)
{
}

LuaPackageLoader::~LuaPackageLoader()
{
    Detach();
}

void LuaPackageLoader::Attach(lua_State* L, const ea::vector<ea::string>& prefixes)
{
    if (!L)
        return;

    prefixes_ = prefixes;
    if (prefixes_.empty())
        prefixes_.push_back(EMPTY_STRING);

    if (luaState_ == L)
        return;

    const int base = lua_gettop(L);

    if (lua_getglobal(L, "package") != LUA_TTABLE)
    {
        lua_settop(L, base);
        URHO3D_LOGERROR("Lua 'package' table is missing, the VFS require searcher was not installed.");
        return;
    }

    // package.searchers in 5.2 and later, package.loaders before that. Both are consulted by
    // name at call time by require(), so supporting either costs nothing.
    if (lua_getfield(L, -1, "searchers") == LUA_TNIL)
    {
        lua_pop(L, 1);
        if (lua_getfield(L, -1, "loaders") == LUA_TNIL)
        {
            lua_settop(L, base);
            URHO3D_LOGERROR("Lua has neither package.searchers nor package.loaders, "
                            "the VFS require searcher was not installed.");
            return;
        }
    }

    const int searchers = lua_gettop(L);
    const int count = static_cast<int>(lua_rawlen(L, searchers));

    // Look for a previous installation. Re-attaching must not push the file searcher further
    // down every time a host calls Initialize(), which is idempotent by contract.
    int existing = 0;
    for (int i = 1; i <= count; ++i)
    {
        if (lua_rawgeti(L, searchers, i) != LUA_TFUNCTION)
        {
            lua_pop(L, 1);
            continue;
        }
        const char* upvalueName = lua_getupvalue(L, -1, 1);
        if (upvalueName && lua_touserdata(L, -1) == this)
            existing = i;
        lua_pop(L, upvalueName ? 2 : 1);
    }

    if (existing == 0)
    {
        // Insert at index 2: after preload, so embedded modules keep winning, and before the
        // file searcher, so the virtual file system decides what "exists" means.
        for (int i = count; i >= 2; --i)
        {
            lua_rawgeti(L, searchers, i);
            lua_rawseti(L, searchers, i + 1);
        }
        lua_pushlightuserdata(L, this);
        lua_pushcclosure(L, &Searcher, 1);
        lua_rawseti(L, searchers, 2);

        // The remaining file searchers reach the C library's fopen, which sees only the real
        // file system: neither a .pak nor APK assets. Leaving package.path populated would let
        // a module resolve from the working directory during development and then disappear in
        // a shipping build, which is the exact failure this searcher exists to prevent. Clearing
        // the templates turns those searchers into no-ops without removing them, so third
        // party code that inspects the table still finds the standard layout.
        lua_pushliteral(L, "");
        lua_setfield(L, -2, "path");
        lua_pushliteral(L, "");
        lua_setfield(L, -2, "cpath");

        SubscribeToEvent(E_FILECHANGED, &LuaPackageLoader::HandleFileChanged);
        SubscribeToEvent(E_BEGINFRAME, &LuaPackageLoader::HandleBeginFrame);
    }

    WrapRequire(L);

    lua_settop(L, base);
    luaState_ = L;
}

void LuaPackageLoader::Detach()
{
    UnsubscribeFromEvent(E_FILECHANGED);
    UnsubscribeFromEvent(E_BEGINFRAME);

    luaState_ = nullptr;
    prefixes_.clear();
    modules_.clear();
    dependents_.clear();
    modulesByResource_.clear();
    roots_.clear();
    pendingWaits_.clear();
    executing_.clear();
    warnedShadowed_.clear();
}

ea::string LuaPackageLoader::ResolveResourceName(ResourceCache* cache, const ea::string& baseName, bool& ambiguity) const
{
    ambiguity = false;
    if (!cache || baseName.empty())
        return EMPTY_STRING;

    // Two spellings of every name: as written, and with dots turned into slashes so that the
    // usual "require('a.b.c')" reaches "a/b/c.lua". The original comes first because hosts pass
    // plain resource paths here too, and those legitimately contain a dot.
    ea::vector<ea::string> forms;
    forms.push_back(baseName);
    ea::string slashed = baseName;
    for (char& character : slashed)
    {
        if (character == '.')
            character = '/';
    }
    if (slashed != baseName)
        forms.push_back(slashed);

    for (const ea::string& form : forms)
    {
        // A name that already carries a scheme is fully qualified: it is rooted somewhere in the
        // virtual file system, so prepending a search prefix would produce nonsense.
        const bool qualified = form.find("://") != ea::string::npos;

        // Everything except a spelled out ".luc" is a request for "the script with this base
        // name", and either answer satisfies it. That is what lets a scene keep naming
        // "Scripts/main.lua" after a build replaced the source with a packaged chunk, which is
        // the reason the offline compiler exists; naming ".luc" explicitly is taken literally,
        // because once the packaged file is asked for there is nothing left to prefer.
        ea::string base = form;
        const int extensionIndex = LuaExtensionIndex(base);
        if (extensionIndex >= 0)
            base.resize(base.length() - ExtensionLength(kExtensions[extensionIndex]));
        const size_t candidateCount = extensionIndex == 0 ? 1 : kExtensionCount;

        for (const ea::string& prefix : prefixes_)
        {
            if (qualified && !prefix.empty())
                continue;

            for (size_t i = 0; i < candidateCount; ++i)
            {
                const ea::string candidate = prefix + base + kExtensions[i];
                if (!cache->Exists(candidate))
                    continue;

                // Report the duplicate rather than the winner: having both spellings on disk is
                // what surprises whoever reads this log later, whichever of them we chose.
                for (size_t other = 0; other < kExtensionCount; ++other)
                {
                    if (other != i && cache->Exists(prefix + base + kExtensions[other]))
                        ambiguity = true;
                }
                return cache->SanitateResourceName(candidate);
            }
        }
    }

    return EMPTY_STRING;
}

bool LuaPackageLoader::ExecuteScript(const ea::string& resourceName, bool registerRoot)
{
    if (!luaState_)
    {
        URHO3D_LOGERROR("LuaPackageLoader is not attached to a Lua state.");
        return false;
    }

    auto* cache = context_->GetSubsystem<ResourceCache>();
    if (!cache)
    {
        URHO3D_LOGERROR("ResourceCache subsystem is required to execute Lua files.");
        return false;
    }

    bool ambiguity = false;
    const ea::string resolved = ResolveResourceName(cache, resourceName, ambiguity);
    if (resolved.empty())
    {
        URHO3D_LOGERRORF("Lua script resource not found: %s", resourceName.c_str());
        return false;
    }

    if (ambiguity && warnedShadowed_.count(MakeWatchKey(resolved)) == 0)
    {
        warnedShadowed_.insert(MakeWatchKey(resolved));
        URHO3D_LOGWARNINGF("Both packaged and plain Lua scripts exist for '%s', using '%s'.", resourceName.c_str(), resolved.c_str());
    }

    LuaFile* file = cache->GetResource<LuaFile>(resolved);
    if (!file)
    {
        URHO3D_LOGERRORF("Failed to load Lua script: %s", resolved.c_str());
        return false;
    }

    ea::string error;
    if (!file->LoadChunk(luaState_, error))
    {
        URHO3D_LOGERRORF("Lua load error in %s: %s", resolved.c_str(), error.c_str());
        return false;
    }

    // The chunk sits on top of the stack; remember where so the bookkeeping below can find it.
    const int chunkIndex = lua_gettop(luaState_);

    if (registerRoot && ea::find(roots_.begin(), roots_.end(), resolved) == roots_.end())
        roots_.push_back(resolved);

    // A top level script is tracked under its resource name, which lets the reload machinery
    // treat roots and required modules with one graph instead of two.
    modules_[resolved].resourceName = resolved;
    modulesByResource_[MakeWatchKey(resolved)].insert(resolved);

    const unsigned serial = file->GetLoadSerial();
    {
        ExecutionScope scope(executing_, resolved);
        const int status = lua_pcall(luaState_, 0, LUA_MULTRET, 0);
        if (status != LUA_OK)
        {
            const char* message = lua_tostring(luaState_, -1);
            URHO3D_LOGERRORF("Lua runtime error in %s: %s", resolved.c_str(), message ? message : "unknown error");
            lua_settop(luaState_, chunkIndex - 1);
            return false;
        }
    }

    // Re-acquire: executing the chunk inserted entries into modules_ and may have rehashed it.
    ModuleInfo& info = modules_[resolved];
    info.resourceName = resolved;
    info.serial = serial;
    info.finishOrder = ++finishOrderCounter_;

    // Return values of a top level script are not a module, drop them.
    lua_settop(luaState_, chunkIndex - 1);
    return true;
}

void LuaPackageLoader::ClearRoots()
{
    roots_.clear();
}

void LuaPackageLoader::ResetTracking()
{
    for (const auto& module : modules_)
        ForgetLoadedModule(module.first);

    modules_.clear();
    dependents_.clear();
    modulesByResource_.clear();
    roots_.clear();
    pendingWaits_.clear();
    executing_.clear();
    finishOrderCounter_ = 0;
    warnedShadowed_.clear();
}

int LuaPackageLoader::Searcher(lua_State* L)
{
    auto* self = static_cast<LuaPackageLoader*>(lua_touserdata(L, lua_upvalueindex(1)));
    if (!self || !self->luaState_)
        return luaL_error(L, "the Lua VFS searcher outlived the state it was installed on");
    return self->DoSearch(L);
}

int LuaPackageLoader::Opener(lua_State* L)
{
    auto* self = static_cast<LuaPackageLoader*>(lua_touserdata(L, lua_upvalueindex(1)));
    if (!self || !self->luaState_)
        return luaL_error(L, "the Lua VFS searcher outlived the state it was installed on");
    return self->DoOpen(L);
}

int LuaPackageLoader::DoSearch(lua_State* L)
{
    const char* nameArgument = lua_tostring(L, 1);
    if (!nameArgument)
        return luaL_error(L, "module name must be a string");
    const ea::string moduleName(nameArgument);

    auto* cache = context_->GetSubsystem<ResourceCache>();
    if (!cache)
    {
        lua_pushliteral(L, "\n\tno ResourceCache available");
        return 1;
    }

    bool ambiguity = false;
    const ea::string resourceName = ResolveResourceName(cache, moduleName, ambiguity);
    if (resourceName.empty())
    {
        // Returning a string means "not mine, and here is why", which require() appends to its
        // error after the remaining searchers have also missed. Erroring here instead would
        // hide a legitimate C-module or embedded-module hit behind our own failure.
        lua_pushfstring(L, "\n\tno Lua script for module '%s' in the virtual file system", moduleName.c_str());
        return 1;
    }

    if (ambiguity)
    {
        const ea::string watchKey = MakeWatchKey(resourceName);
        if (warnedShadowed_.count(watchKey) == 0)
        {
            warnedShadowed_.insert(watchKey);
            URHO3D_LOGWARNINGF("Both packaged and plain Lua scripts exist for module '%s', using '%s'.", moduleName.c_str(), resourceName.c_str());
        }
    }

    // The requiring module is still on top of the execution stack at this point: require() has
    // not called us back yet. That is what makes the parent/child edge recordable here instead
    // of somewhere that cannot know who asked. RequireWrapper records the same edge for the
    // modules require() answers from its cache without ever consulting us.
    modules_[moduleName].resourceName = resourceName;
    modulesByResource_[MakeWatchKey(resourceName)].insert(moduleName);
    if (!executing_.empty())
        dependents_[moduleName].insert(executing_.back());

    lua_pushlightuserdata(L, this);
    lua_pushstring(L, moduleName.c_str());
    lua_pushstring(L, resourceName.c_str());
    lua_pushcclosure(L, &Opener, 3);
    // Second return value is handed to the loader as its extra argument, mirroring how the
    // standard searchers pass the file name down.
    lua_pushstring(L, resourceName.c_str());
    return 2;
}

int LuaPackageLoader::DoOpen(lua_State* L)
{
    const char* moduleNameArgument = lua_tostring(L, lua_upvalueindex(2));
    const char* resourceNameArgument = lua_tostring(L, lua_upvalueindex(3));
    const ea::string moduleName = moduleNameArgument ? moduleNameArgument : "";
    const ea::string resourceName = resourceNameArgument ? resourceNameArgument : "";

    auto* cache = context_->GetSubsystem<ResourceCache>();
    LuaFile* file = cache ? cache->GetResource<LuaFile>(resourceName) : nullptr;
    if (!file)
    {
        return luaL_error(
            L, "error loading module '%s' from resource '%s': the resource could not be read", moduleName.c_str(), resourceName.c_str());
    }

    // Sampled before the chunk runs: a reload landing mid-execution must not be mistaken for
    // the bytes this module was compiled from.
    const unsigned serial = file->GetLoadSerial();

    ea::string error;
    if (!file->LoadChunk(L, error))
    {
        return luaL_error(L,
            "error loading module '%s' from resource '%s':\n\t%s",
            moduleName.c_str(),
            resourceName.c_str(),
            error.c_str());
    }

    const int chunkIndex = lua_gettop(L);

    {
        ExecutionScope scope(executing_, moduleName);
        // Forward require()'s two arguments so the chunk sees exactly what the standard file
        // loader would have handed it.
        lua_pushvalue(L, 1);
        lua_pushvalue(L, 2);
        const int status = lua_pcall(L, 2, LUA_MULTRET, 0);
        if (status != LUA_OK)
        {
            // Leave the tracked stack before the longjmp, then let the module's own message
            // travel unchanged - it already carries the resource name and line number.
            scope.Leave();
            return lua_error(L);
        }
    }

    ModuleInfo& info = modules_[moduleName];
    info.resourceName = resourceName;
    info.serial = serial;
    info.finishOrder = ++finishOrderCounter_;
    modulesByResource_[MakeWatchKey(resourceName)].insert(moduleName);

    return lua_gettop(L) - chunkIndex + 1;
}

int LuaPackageLoader::RequireWrapper(lua_State* L)
{
    auto* self = static_cast<LuaPackageLoader*>(lua_touserdata(L, lua_upvalueindex(1)));
    if (!self || !self->luaState_)
        return luaL_error(L, "the Lua require wrapper outlived the state it was installed on");

    // Record before delegating: after the real require returns, the requesting chunk has moved
    // on and the stack holds the module. Over-recording costs nothing - a name that was never
    // loaded simply has no bookkeeping to invalidate - while a missing edge is a silent stale
    // module, which is the failure this wrapper exists to prevent.
    if (const char* moduleName = lua_tostring(L, 1))
    {
        if (!self->executing_.empty())
            self->dependents_[moduleName].insert(self->executing_.back());
    }

    // Call the original with exactly the arguments we were handed, and hand back everything it
    // produced, so require() keeps its varargs-in, results-out shape.
    lua_pushvalue(L, lua_upvalueindex(2));
    const int arguments = lua_gettop(L) - 1;
    lua_insert(L, 1);
    lua_call(L, arguments, LUA_MULTRET);
    return lua_gettop(L);
}

void LuaPackageLoader::WrapRequire(lua_State* L)
{
    const int top = lua_gettop(L);

    lua_getglobal(L, "require");
    if (!lua_isfunction(L, -1))
    {
        lua_settop(L, top);
        URHO3D_LOGERROR("Lua has no global 'require'. Dependency edges of cached modules cannot be tracked.");
        return;
    }

    // Attaching is idempotent, so nesting a wrapper around our own wrapper is not an option:
    // re-initialising a VM would grow one C frame per require every time. Only the upvalue is
    // popped below, so 'require' stays at top + 1 for the whole function.
    bool alreadyWrapped = false;
    if (const char* upvalueName = lua_getupvalue(L, -1, 1))
    {
        alreadyWrapped = lua_touserdata(L, -1) == this;
        lua_pop(L, 1);
    }
    if (alreadyWrapped)
    {
        lua_settop(L, top);
        return;
    }

    // Upvalues: this pointer, and the function being replaced. The original is captured rather
    // than assumed, so a host that had its own require before we arrived still runs it.
    lua_pushlightuserdata(L, this);
    lua_pushvalue(L, top + 1);
    lua_pushcclosure(L, &RequireWrapper, 2);
    lua_replace(L, top + 1);
    lua_setglobal(L, "require");
}

void LuaPackageLoader::HandleFileChanged(StringHash eventType, VariantMap& eventData)
{
    // The signature is fixed by the event handler typedef; the type itself is not inspected.
    static_cast<void>(eventType);

    if (!luaState_)
        return;

    using namespace FileChanged;
    const ea::string changed = eventData[P_RESOURCENAME].GetString();
    if (changed.empty() || !EndsWithLuaExtension(changed))
        return;

    auto* cache = context_->GetSubsystem<ResourceCache>();
    if (!cache)
        return;

    // Normalize with the same function that produced the tracked names, so a mount that carries
    // a scheme ("editorlua://") matches the event the watcher reports for it.
    const ea::string watchKey = MakeWatchKey(cache->SanitateResourceName(changed));
    if (modulesByResource_.find(watchKey) == modulesByResource_.end())
        return;

    pendingWaits_[watchKey] = 0;
}

void LuaPackageLoader::HandleBeginFrame(StringHash eventType, VariantMap& eventData)
{
    static_cast<void>(eventType);
    static_cast<void>(eventData);

    FlushPendingChanges();
}

unsigned LuaPackageLoader::FlushPendingChanges()
{
    if (pendingWaits_.empty() || !luaState_)
        return 0;

    auto* cache = context_->GetSubsystem<ResourceCache>();
    if (!cache)
    {
        pendingWaits_.clear();
        return 0;
    }

    // Phase one: work out which queued resources have actually produced new bytes. Everything
    // is decided by the load serial, never by which handler ran first.
    ea::vector<ea::string> changedModules;
    for (auto it = pendingWaits_.begin(); it != pendingWaits_.end();)
    {
        const ea::string& watchKey = it->first;
        bool changed = false;
        bool waiting = false;

        const auto modules = modulesByResource_.find(watchKey);
        if (modules == modulesByResource_.end())
        {
            it = pendingWaits_.erase(it);
            continue;
        }

        for (const ea::string& moduleName : modules->second)
        {
            const auto info = modules_.find(moduleName);
            if (info == modules_.end())
                continue;

            // Look it up without loading: if it is not cached any more, the next
            // GetResource() reads it from disk, which is as good as a change.
            LuaFile* file = cache->GetExistingResource<LuaFile>(info->second.resourceName);
            if (!file)
                changed = true;
            else if (file->GetLoadSerial() != info->second.serial)
                changed = true;
            else
                waiting = true;
        }

        if (changed)
        {
            changedModules.insert(changedModules.end(), modules->second.begin(), modules->second.end());
            it = pendingWaits_.erase(it);
            continue;
        }

        if (++it->second >= ForceReloadAfterPendingFrames)
        {
            for (const ea::string& moduleName : modules->second)
            {
                const auto info = modules_.find(moduleName);
                if (info == modules_.end())
                    continue;
                LuaFile* file = cache->GetExistingResource<LuaFile>(info->second.resourceName);
                if (file)
                    cache->ReloadResource(file);
            }
            it = pendingWaits_.erase(it);
            continue;
        }
        ++it;
    }

    if (changedModules.empty())
        return 0;

    // Phase two: everything that transitively depends on a changed module has to go as well.
    ea::unordered_set<ea::string> dirty;
    for (const ea::string& moduleName : changedModules)
        CollectDependents(moduleName, dirty);

    ea::vector<ea::string> dirtyNames;
    dirtyNames.reserve(dirty.size());
    for (const ea::string& moduleName : dirty)
        dirtyNames.push_back(moduleName);
    // Sorted because the reload hook hands the list to scripts, and a nondeterministic order
    // there would be a bug report that cannot be reproduced.
    ea::sort(dirtyNames.begin(), dirtyNames.end());

    InvokeReloadHook(dirtyNames);

    for (const ea::string& moduleName : dirtyNames)
        ForgetLoadedModule(moduleName);

    // Only roots are re-executed. Everything below a root comes back through require() in the
    // order the root asks for it, which is a valid dependency order by construction - re-running
    // the intermediate modules here would need a topological sort of a graph we already walk
    // correctly, and would break modules that no root requires any more.
    unsigned rerunRoots = 0;
    for (const ea::string& root : roots_)
    {
        if (dirty.count(root) == 0)
            continue;
        if (ExecuteScript(root, false))
            ++rerunRoots;
    }

    URHO3D_LOGINFOF(
        "Lua hot reload: %u module(s) invalidated, %u root script(s) re-executed", static_cast<unsigned>(dirtyNames.size()), rerunRoots);
    return rerunRoots;
}

void LuaPackageLoader::CollectDependents(const ea::string& moduleName, ea::unordered_set<ea::string>& result) const
{
    // Inserting first makes this cycle safe, which matters because a pair of modules can
    // require each other through a third one.
    if (result.count(moduleName) != 0)
        return;
    result.insert(moduleName);

    const auto it = dependents_.find(moduleName);
    if (it == dependents_.end())
        return;
    for (const ea::string& parent : it->second)
        CollectDependents(parent, result);
}

void LuaPackageLoader::ForgetLoadedModule(const ea::string& moduleName)
{
    if (!luaState_)
        return;

    lua_getfield(luaState_, LUA_REGISTRYINDEX, LUA_LOADED_TABLE);
    if (lua_istable(luaState_, -1))
    {
        lua_pushnil(luaState_);
        lua_setfield(luaState_, -2, moduleName.c_str());
    }
    lua_pop(luaState_, 1);
}

void LuaPackageLoader::InvokeReloadHook(const ea::vector<ea::string>& moduleNames)
{
    if (!luaState_ || moduleNames.empty())
        return;

    lua_State* L = luaState_;
    const int base = lua_gettop(L);

    lua_getglobal(L, "OnReload");
    if (!lua_isfunction(L, -1))
    {
        lua_settop(L, base);
        return;
    }

    lua_createtable(L, static_cast<int>(moduleNames.size()), 0);
    int index = 1;
    for (const ea::string& moduleName : moduleNames)
    {
        lua_pushstring(L, moduleName.c_str());
        lua_rawseti(L, -2, index++);
    }

    if (lua_pcall(L, 1, 0, 0) != LUA_OK)
    {
        const char* message = lua_tostring(L, -1);
        URHO3D_LOGERRORF("Lua OnReload hook failed: %s", message ? message : "unknown error");
    }
    lua_settop(L, base);
}

} // namespace Urho3D

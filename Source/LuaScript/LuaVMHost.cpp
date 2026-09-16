// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#include "../Urho3D/Precompiled.h"

#include "LuaVMHost.h"

#include "LuaPackageLoader.h"

#include "../Urho3D/Core/Context.h"
#include "../Urho3D/Core/StringUtils.h"
#include "../Urho3D/IO/File.h"
#include "../Urho3D/IO/FileIdentifier.h"
#include "../Urho3D/IO/FileSystem.h"
#include "../Urho3D/IO/Log.h"
#include "../Urho3D/IO/MountPoint.h"
#include "../Urho3D/IO/VirtualFileSystem.h"

#include <EASTL/algorithm.h>

#include <sol/sol.hpp>

namespace Urho3D
{

namespace
{

// Handle source shared by all hosts: handles stay unique across instances, so a handle handed
// to the wrong host finds nothing (and is safely ignored) instead of a foreign callback.
unsigned long long NextCallbackHandle()
{
    static unsigned long long handle = 1;
    return handle++;
}

} // namespace

LuaVMHost::LuaVMHost(Context* context, const LuaVMHostConfig& config)
    : LuaVM(context, config)
    , callbacks_(ea::make_unique<CallbackMap>())
    , mountScheme_(config.mountScheme_)
{
    // Plugins resolve from their own folder first, then from the ordinary resource
    // directories, so a script can require its own modules and engine-provided ones with the
    // same call. The plugin scheme is only useful once LoadPlugins has mounted the folder; a
    // miss simply falls through to the next prefix, which is why prepending here is safe.
    config_.requirePrefixes_.insert(config_.requirePrefixes_.begin(), mountScheme_ + "://");
}

LuaVMHost::~LuaVMHost()
{
    // Release sol references while the Lua state is still alive: the callback map is a
    // derived-class member, so it destroys before the base's luaState_ regardless; the
    // explicit clear is belt and braces against a future member reordering.
    callbacks_->clear();

    // Give back the mount point: the virtual file system holds it with a strong reference, so
    // leaving it behind would keep a previous project's EditorScripts folder reachable under
    // the same scheme after a project switch.
    if (pluginMount_)
    {
        auto* vfs = context_->GetSubsystem<VirtualFileSystem>();
        if (vfs)
            vfs->Unmount(pluginMount_);
        pluginMount_ = nullptr;
    }
}

void LuaVMHost::LoadPlugins(const ea::string& absoluteDir)
{
    // Normalize once here so that the mount, the scan and the resource names derived from the
    // paths all agree; a trailing slash would produce "dir//file.lua" as a resource name.
    ea::string dir = absoluteDir;
    while (!dir.empty() && (dir.back() == '/' || dir.back() == '\\'))
        dir.pop_back();
    pluginDir_ = dir;

    auto* fs = context_->GetSubsystem<FileSystem>();
    if (!fs || dir.empty() || !fs->DirExists(dir))
        return;

    // Reload starts from a clean callback slate on the Lua side; the owner clears its own
    // bookkeeping (the editor: tabs, menus, windows) around this call before scripts re-register.
    callbacks_->clear();

    MountPluginDir(dir);

    // Every plugin body is about to run again, and modules they require must run again with it.
    // Without this the second load would hand plugins the module tables of the first load,
    // because require() answers from package.loaded before any searcher is consulted.
    if (packageLoader_)
        packageLoader_->ResetTracking();

    // Top-level only: everything in this folder is a plugin that must run on its own; anything in
    // a subfolder is a module the plugins opt into via require(), resolved through the mount.
    ea::vector<ea::string> files;
    fs->ScanDir(files, dir, "*.lua", SCAN_FILES);
    ea::sort(files.begin(), files.end());

    for (const ea::string& file : files)
        ExecuteFileAbsolute(dir + "/" + file);
}

void LuaVMHost::MountPluginDir(const ea::string& absoluteDir)
{
    auto* vfs = context_->GetSubsystem<VirtualFileSystem>();
    if (!vfs || absoluteDir.empty())
        return;

    if (pluginMount_ && mountedPluginDir_ == absoluteDir)
        return;

    if (pluginMount_)
    {
        vfs->Unmount(pluginMount_);
        pluginMount_ = nullptr;
        mountedPluginDir_.clear();
    }

    // The folder becomes a resource directory with a scheme of its own, which buys three things
    // over injecting a package.path entry: subfolders resolve without touching the working
    // directory, the plugin sources take part in the same watching/reloading pipeline as every
    // other asset, and packaged .luc siblings are preferred transparently.
    MountPoint* mountPoint = vfs->MountDir(mountScheme_, absoluteDir);
    if (!mountPoint)
    {
        URHO3D_LOGERRORF("Failed to mount the plugin directory '%s' under scheme '%s'",
            absoluteDir.c_str(), mountScheme_.c_str());
        return;
    }

    pluginMount_ = mountPoint;
    mountedPluginDir_ = absoluteDir;
}

bool LuaVMHost::ExecuteFileAbsolute(const ea::string& absolutePath)
{
    if (!packageLoader_)
    {
        URHO3D_LOGERROR("LuaVMHost is not initialized.");
        return false;
    }

    // Translate to a resource name instead of opening the file: plugin bodies then share one
    // execution path with required modules, which is what lets a change be attributed to a
    // module at all.
    auto* vfs = context_->GetSubsystem<VirtualFileSystem>();
    const FileIdentifier identifier = vfs ? vfs->GetIdentifierFromAbsoluteName(absolutePath) : FileIdentifier::Empty;
    if (!identifier)
    {
        URHO3D_LOGERRORF("%s script '%s' is not reachable through the mounted resource directories.",
            config_.logPrefix_.c_str(), absolutePath.c_str());
        return false;
    }

    return packageLoader_->ExecuteScript(identifier.ToUri(), false);
}

unsigned long long LuaVMHost::RegisterCallback(sol::protected_function callback)
{
    const unsigned long long handle = NextCallbackHandle();
    (*callbacks_)[handle] = std::move(callback);
    return handle;
}

void LuaVMHost::DropCallback(unsigned long long handle)
{
    callbacks_->erase(handle);
}

void LuaVMHost::ReloadPlugins()
{
    if (!pluginDir_.empty())
        LoadPlugins(pluginDir_);
}

void LuaVMHost::InvokeCallback(unsigned long long handle)
{
    if (!luaState_)
        return;

    const auto iter = callbacks_->find(handle);
    if (iter == callbacks_->end())
        return; // Callback dropped by a reload; ignore so the owner stays alive.

    sol::protected_function_result result = iter->second();
    if (!result.valid())
    {
        sol::error err = result;
        URHO3D_LOGERROR("{} callback error: {}", config_.logPrefix_.c_str(), err.what());
    }
}

void LuaVMHost::InvokeOneShotCallback(unsigned long long handle, VariantMap& eventData)
{
    if (!luaState_)
        return;

    const auto iter = callbacks_->find(handle);
    if (iter == callbacks_->end())
        return; // Dropped by a reload between the call and the completion it waits for.

    // Taken out before it runs: a callback that starts another wait-worthy action would
    // otherwise be invoked again by that action's completion through the same handle, and one
    // that errors out must not be kept alive by the hope that the next run goes better.
    sol::protected_function callback = ea::move(iter->second);
    callbacks_->erase(iter);
    InvokeEventCallback(callback, eventData);
}

} // namespace Urho3D

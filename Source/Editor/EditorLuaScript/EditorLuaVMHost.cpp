//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#include "EditorLuaVMHost.h"

#ifdef URHO3D_LUA

namespace Urho3D
{

namespace
{

// What makes this host *the editor's*: plugin output is attributed to "EditorLua" in the log
// and the plugin folder mounts under a scheme of its own, distinct from every other host's.
LuaVMHostConfig EditorLuaHostConfig()
{
    LuaVMHostConfig config;
    config.logPrefix_ = "EditorLua";
    config.mountScheme_ = "editorlua";
    return config;
}

} // namespace

EditorLuaVMHost::EditorLuaVMHost(Context* context)
    : LuaVMHost(context, EditorLuaHostConfig())
{
}

} // namespace Urho3D

#endif

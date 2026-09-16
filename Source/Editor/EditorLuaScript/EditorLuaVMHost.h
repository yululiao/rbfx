//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#pragma once

#ifdef URHO3D_LUA

#include <LuaScript/LuaVMHost.h>

namespace Urho3D
{

/// The editor's flavor of LuaVMHost: the generic VM plumbing unchanged, with the editor's
/// identity baked in (plugin output tagged "EditorLua" in the log, the EditorScripts folder
/// mounted under "editorlua://"). Being its own type gives the editor plugin VM a dedicated
/// Context subsystem slot, so further LuaVMHost uses can register their own subclasses without
/// contention. It adds no behavior: the plugin-visible API tables ("imgui", "Editor") are still
/// registered by free functions (RegisterImGuiLuaBindings / RegisterEditorLuaAPI).
class EditorLuaVMHost final : public LuaVMHost
{
    URHO3D_OBJECT(EditorLuaVMHost, LuaVMHost);

public:
    explicit EditorLuaVMHost(Context* context);
};

} // namespace Urho3D

#endif

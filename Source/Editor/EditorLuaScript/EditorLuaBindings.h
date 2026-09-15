//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#pragma once

#include <sol/forward.hpp>

namespace Urho3D
{

class Context;

/// Register the "imgui" global table (a lean, editor-oriented subset of the Dear ImGui
/// immediate-mode API) into the editor Lua state. Called by SetupEditorLua after the state is
/// initialized. Lives in the editor because ImGui drawing only happens inside the editor's render
/// loop, and the path widgets reach the native OS dialog (nfd, desktop editor builds only)
/// directly instead of through the runtime library.
void RegisterImGuiLuaBindings(sol::state& lua);

/// Register the editor-capability half of the "Editor" Lua table: project access, tabs, menus,
/// windows, scene selection and the build pipeline. The lambdas capture the editor Context and
/// query its subsystems directly, so no injection table is needed. The VM-plumbing half
/// (log, subscribe, exec) is registered inside RbfxLuaScript, which owns the state.
void RegisterEditorLuaAPI(Context* context);

}

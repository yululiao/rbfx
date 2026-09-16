//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#include "LuaEditorTab.h"

#ifdef URHO3D_LUA

#include "EditorLuaVMHost.h"

#include <Urho3D/Core/Context.h>

namespace Urho3D
{

// Fixed GUID so a plugin tab keeps a stable ImGui id (title###guid) across sessions; the title
// alone disambiguates multiple plugin tabs.
LuaEditorTab::LuaEditorTab(Context* context, const ea::string& title, unsigned long long handle)
    : EditorTab(context, title, "LuaEditorPlugin", EditorTabFlag::OpenByDefault, EditorTabPlacement::DockRight)
    , handle_(handle)
{
}

void LuaEditorTab::RenderContent()
{
    auto* lua = context_->GetSubsystem<EditorLuaVMHost>();
    if (lua)
        lua->InvokeCallback(handle_);
}

} // namespace Urho3D

#endif

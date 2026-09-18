//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#include "EditorLuaSettingsPage.h"

#ifdef URHO3D_LUA

#include "EditorLuaVMHost.h"

#include <Urho3D/Core/Context.h>

namespace Urho3D
{

LuaSettingsPage::LuaSettingsPage(Context* context, const ea::string& uniqueName, unsigned long long handle)
    : SettingsPage(context)
    , uniqueName_(uniqueName)
    , handle_(handle)
{
}

void LuaSettingsPage::RenderSettings()
{
    // Hand the current ImGui context to the plugin's draw callback. The host is resolved live so a
    // reload (which rebuilds the callback registry under the same subsystem) is picked up; after a
    // reload that dropped this page's function the handle is unknown and InvokeCallback is a no-op,
    // leaving an empty page until the plugin re-registers through Editor.settings.registerPage.
    auto* host = context_->GetSubsystem<EditorLuaVMHost>();
    if (!host || !handle_)
        return;
    host->InvokeCallback(handle_);
}

} // namespace Urho3D

#endif

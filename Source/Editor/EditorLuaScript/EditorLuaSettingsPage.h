//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#pragma once

#ifdef URHO3D_LUA

#include "../Core/SettingsManager.h"

namespace Urho3D
{

class Context;

/// A SettingsManager page whose body is drawn by a Lua plugin callback, registered through
/// Editor.settings.registerPage. The page object is owned by the project's SettingsManager and
/// lives for as long as the project, so it is added to the settings tree exactly once per unique
/// title; only the draw-callback handle is re-pointed when a plugin reloads. It is deliberately
/// non-serializable: a plugin persists its own values through Editor.settings, not the archive.
/// The unique name is "Editor.Lua:<title>", which nests the page under the Editor > Lua group in
/// the settings tree while keeping each plugin page's section (its title) distinct.
class LuaSettingsPage final : public SettingsPage
{
    URHO3D_OBJECT(LuaSettingsPage, SettingsPage);

public:
    LuaSettingsPage(Context* context, const ea::string& uniqueName, unsigned long long handle);

    /// Re-point the draw callback after a plugin reload, so the currently registered function is
    /// the one the (persistent) page renders. The old handle belongs to the cleared callback
    /// registry and is simply dropped.
    void SetHandle(unsigned long long handle) { handle_ = handle; }

    /// SettingsPage interface.
    /// @{
    ea::string GetUniqueName() override { return uniqueName_; }
    bool IsSerializable() override { return false; }
    bool CanResetToDefault() override { return false; }
    void SerializeInBlock(Archive& /*archive*/) override {}
    void RenderSettings() override;
    /// @}

private:
    ea::string uniqueName_;
    unsigned long long handle_;
};

} // namespace Urho3D

#endif

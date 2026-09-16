//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#pragma once

#include "../Project/EditorTab.h"

namespace Urho3D
{

#ifdef URHO3D_LUA

/// Editor tab whose content is drawn by a Lua callback owned by the LuaVMHost
/// subsystem. The tab itself knows nothing about Lua or sol3: it only carries an opaque handle
/// and forwards each render to LuaVMHost::InvokeCallback, keeping all sol3 usage in the
/// single Lua VM owned by that subsystem.
class LuaEditorTab : public EditorTab
{
    URHO3D_OBJECT(LuaEditorTab, EditorTab);

public:
    LuaEditorTab(Context* context, const ea::string& title, unsigned long long handle);

    /// Point the tab at a freshly registered callback (used when a plugin reloads).
    void SetHandle(unsigned long long handle) { handle_ = handle; }
    unsigned long long GetHandle() const { return handle_; }

    /// Implement EditorTab
    /// @{
    void RenderContent() override;
    /// @}

private:
    unsigned long long handle_;
};

#endif

} // namespace Urho3D

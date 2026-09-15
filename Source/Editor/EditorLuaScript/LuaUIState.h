//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#pragma once

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <Urho3D/Container/Ptr.h>

namespace Urho3D
{

class Context;
class LuaEditorTab;
class SceneViewPage;

// Editor-side bookkeeping for the Lua plugin UI, shared between the two parties that touch it:
// EditorLuaBindings.cpp registers items into these registries (from Editor.addTab and friends),
// while EditorLuaIntegration.cpp renders them (menus, floating windows) and resets them on plugin
// reload. Internal to the editor's Lua integration.

namespace Detail
{

/// Menu item registered by a Lua plugin; clicking invokes the callback by handle.
struct LuaMenuItem
{
    ea::string label;
    unsigned long long handle;
};

/// Floating window registered by a Lua plugin. The editor draws it every frame (independent of
/// any dock tab), wrapping the content callback between its own Begin/End. 'visible' is owned by
/// the editor so the title-bar close works; Lua toggles it by title via showWindow/hideWindow.
struct LuaWindow
{
    ea::string title;
    unsigned long long handle;
    bool visible;
    unsigned int flags;
};

ea::vector<LuaMenuItem>& LuaMenuItems();
ea::vector<WeakPtr<LuaEditorTab>>& LuaTabs();
ea::vector<LuaWindow>& LuaWindows();

/// Return the scene-view page currently being edited (its scene + selection), or null.
SceneViewPage* ActiveSceneViewPage(Context* context);

/// Drop accumulated bookkeeping (menu items, windows, pruned tab references) at the start of a
/// plugin (re)load, mirroring the Lua side clearing its own UI-callback registry.
void ResetLuaUI();

}

}

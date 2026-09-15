//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#include "LuaUIState.h"

#include "../Foundation/SceneViewTab.h"
#include "../Project/Project.h"

#include <Urho3D/Core/Context.h>

#include <EASTL/algorithm.h>

namespace Urho3D
{

namespace Detail
{

ea::vector<LuaMenuItem>& LuaMenuItems()
{
    static ea::vector<LuaMenuItem> items;
    return items;
}

// Weak references to the tabs created for the current project, so a plugin reload can update an
// existing tab's callback by title instead of stacking duplicates. Expired entries are pruned on
// each (re)load, which also covers the case where the owning project has been closed.
ea::vector<WeakPtr<LuaEditorTab>>& LuaTabs()
{
    static ea::vector<WeakPtr<LuaEditorTab>> tabs;
    return tabs;
}

ea::vector<LuaWindow>& LuaWindows()
{
    static ea::vector<LuaWindow> windows;
    return windows;
}

SceneViewPage* ActiveSceneViewPage(Context* context)
{
    auto* project = context->GetSubsystem<Project>();
    if (!project)
        return nullptr;
    auto* view = project->FindTab<SceneViewTab>();
    return view ? view->GetActivePage() : nullptr;
}

void ResetLuaUI()
{
    LuaMenuItems().clear();
    LuaWindows().clear();
    auto& tabs = LuaTabs();
    tabs.erase(ea::remove_if(tabs.begin(), tabs.end(),
                   [](const WeakPtr<LuaEditorTab>& weak) { return !weak.Get(); }),
        tabs.end());
}

} // namespace Detail

} // namespace Urho3D

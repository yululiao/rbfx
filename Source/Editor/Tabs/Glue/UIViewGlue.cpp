//
// Copyright (c) 2017-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#include "../../Tabs/Glue/UIViewGlue.h"

namespace Urho3D
{

namespace
{
// Heap-allocated so the OnFocused lambda captures a single pointer, keeping it
// within the small-buffer limit of the signal's fixed_function storage.
struct UIViewGlueState
{
    WeakPtr<HierarchyBrowserTab> hierarchyBrowserTab_;
    WeakPtr<InspectorTab> inspectorTab_;
    WeakPtr<UIViewHierarchy> hierarchy_;
    WeakPtr<UIViewInspector> inspector_;
};
}

void Tabs_UIViewGlue(Context* context, UIViewTab* uiViewTab)
{
    auto project = uiViewTab->GetProject();

    const auto state = ea::make_shared<UIViewGlueState>();
    state->hierarchyBrowserTab_ = project->FindTab<HierarchyBrowserTab>();
    state->inspectorTab_ = project->FindTab<InspectorTab>();
    state->hierarchy_ = uiViewTab->GetHierarchySource();
    state->inspector_ = uiViewTab->GetInspectorSource();

    uiViewTab->OnFocused.Subscribe(uiViewTab, [state](UIViewTab* uiViewTab)
    {
        if (state->hierarchyBrowserTab_ && state->hierarchy_)
            state->hierarchyBrowserTab_->ConnectToSource(state->hierarchy_.Get());
        if (state->inspectorTab_ && state->inspector_)
            state->inspectorTab_->ConnectToSource(state->inspector_.Get());
    });
}

}

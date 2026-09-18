//
// Copyright (c) 2017-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#pragma once

#include "../../Tabs/HierarchyBrowserTab.h"
#include "../../Tabs/InspectorTab.h"
#include "../../Tabs/UIViewTab.h"

namespace Urho3D
{

/// Rebinds the shared Hierarchy and Inspector panels to the UI editor's
/// document whenever the UI tab gains focus.
void Tabs_UIViewGlue(Context* context, UIViewTab* uiViewTab);

}

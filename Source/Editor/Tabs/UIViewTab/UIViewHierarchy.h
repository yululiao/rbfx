//
// Copyright (c) 2017-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#pragma once

#include "../Shared/HierarchyBrowserSource.h"

#include <Urho3D/Core/Object.h>
#include <Urho3D/Container/Ptr.h>

namespace Urho3D
{

struct UiNode;
class UIViewTab;

/// HierarchyBrowserSource: walks the editor model of the tab's document.
///
/// Bound to the shared HierarchyBrowserTab by the focus glue (UIViewGlue) and
/// reconnected whenever the owner tab gains focus. The rows come from the
/// tab's document model; tree state (expanded paths, the row being
/// right-clicked) is kept here as stable child-index paths because node
/// pointers do not survive the whole-tree rebuilds that every editing
/// command performs.
class UIViewHierarchy : public Object, public HierarchyBrowserSource
{
    URHO3D_OBJECT(UIViewHierarchy, Object)

public:
    explicit UIViewHierarchy(UIViewTab* owner);

    /// Implement HierarchyBrowserSource
    /// @{
    EditorTab* GetOwnerTab() override;
    void RenderContent() override;
    void RenderContextMenuItems() override;
    /// The hierarchy issues undoable editing commands through the same
    /// project-wide UndoManager as the owner tab.
    bool IsUndoSupported() override { return true; }
    /// @}

    /// Expand the ancestor chain of the given node path (called on selection).
    void ExpandAncestors(const ea::vector<unsigned>& path);

private:
    void RenderNode(UiNode* node, const ea::vector<unsigned>& path);
    bool IsOpen(UiNode* node, const ea::vector<unsigned>& path) const;
    static bool PathIn(const ea::vector<ea::vector<unsigned>>& set, const ea::vector<unsigned>& path);

    WeakPtr<UIViewTab> owner_;
    // Right-click target, stored as a path: node pointers do not survive the
    // whole-tree rebuilds that every editing command performs.
    ea::vector<unsigned> contextMenuTargetPath_;
    bool contextMenuTargetValid_ = false;
    // Set by RenderNode on right-click, honored at the end of RenderContent:
    // OpenPopup must run where BeginPopup runs (window base ID stack), see
    // the comment there.
    bool openNodeMenuRequested_ = false;
    ea::vector<ea::vector<unsigned>> openedPaths_;
    ea::vector<ea::vector<unsigned>> closedPaths_;
    bool focusPathOnly_ = false;
};

}

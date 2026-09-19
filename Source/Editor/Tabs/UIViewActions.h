//
// Copyright (c) 2017-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#pragma once

#include "UIViewDocumentModel.h"

#include "../Core/UndoManager.h"

#include <EASTL/vector.h>

namespace Urho3D
{

class UIViewDocument;

/// Replace the editable payload (id / class / attributes / inline style /
/// text) of one model node. Mirrors ChangeNodeAttributesAction: old/new
/// snapshots identified by the node's child-index path, merged while the edit
/// continues within the same action group.
class ChangeUiNodeAction : public EditorAction
{
public:
    ChangeUiNodeAction(UIViewDocument* document, const ea::vector<unsigned>& nodePath,
        const UiNodePayload& oldData, const UiNodePayload& newData);

    /// Implement EditorAction.
    /// @{
    bool CanUndoRedo() const override;
    void Redo() const override;
    void Undo() const override;
    bool MergeWith(const EditorAction& other) override;
    /// @}

private:
    void SetPayload(const UiNodePayload& payload) const;

    WeakPtr<UIViewDocument> document_;
    ea::vector<unsigned> nodePath_;
    UiNodePayload oldData_;
    UiNodePayload newData_;
};

/// Insert or remove a whole model-node subtree (AddWidget / Duplicate /
/// Delete). The removed subtree stays alive through the action's SharedPtr;
/// undo re-creates its live DOM elements, redo detaches them again.
class CreateRemoveUiNodeAction : public EditorAction
{
public:
    CreateRemoveUiNodeAction(UIViewDocument* document, const ea::vector<unsigned>& parentPath,
        unsigned indexInParent, const SharedPtr<UiNode>& node, bool removed);

    /// Implement EditorAction.
    /// @{
    bool CanUndoRedo() const override;
    void Redo() const override;
    void Undo() const override;
    /// @}

private:
    void AddNode() const;
    void RemoveNode() const;

    WeakPtr<UIViewDocument> document_;
    ea::vector<unsigned> parentPath_;
    unsigned indexInParent_{};
    SharedPtr<UiNode> node_;
    bool removed_{};
};

}

//
// Copyright (c) 2017-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#include "UIViewActions.h"

#include "UIViewDocument.h"

namespace Urho3D
{

ChangeUiNodeAction::ChangeUiNodeAction(UIViewDocument* document, const ea::vector<unsigned>& nodePath,
        const UiNodePayload& oldData, const UiNodePayload& newData)
    : document_(document)
    , nodePath_(nodePath)
    , oldData_(oldData)
    , newData_(newData)
{
}

bool ChangeUiNodeAction::CanUndoRedo() const
{
    UIViewDocument* document = document_.Get();
    if (!document)
        return false;
    return document->LookupNode(nodePath_) != nullptr;
}

void ChangeUiNodeAction::SetPayload(const UiNodePayload& payload) const
{
    UIViewDocument* document = document_.Get();
    if (!document)
        throw UndoException("UI document is gone");

    if (!document->ApplyNodePayloadInternal(nodePath_, payload))
        throw UndoException("UI node path no longer resolves");
}

void ChangeUiNodeAction::Redo() const
{
    SetPayload(newData_);
}

void ChangeUiNodeAction::Undo() const
{
    SetPayload(oldData_);
}

bool ChangeUiNodeAction::MergeWith(const EditorAction& other)
{
    const auto otherAction = dynamic_cast<const ChangeUiNodeAction*>(&other);
    if (!otherAction)
        return false;

    if (document_ != otherAction->document_ || nodePath_ != otherAction->nodePath_)
        return false;

    newData_ = otherAction->newData_;
    return true;
}

CreateRemoveUiNodeAction::CreateRemoveUiNodeAction(UIViewDocument* document,
        const ea::vector<unsigned>& parentPath, unsigned indexInParent,
        const SharedPtr<UiNode>& node, bool removed)
    : document_(document)
    , parentPath_(parentPath)
    , indexInParent_(indexInParent)
    , node_(node)
    , removed_(removed)
{
}

bool CreateRemoveUiNodeAction::CanUndoRedo() const
{
    UIViewDocument* document = document_.Get();
    if (!document)
        return false;
    return document->LookupNode(parentPath_) != nullptr;
}

void CreateRemoveUiNodeAction::AddNode() const
{
    UIViewDocument* document = document_.Get();
    if (!document)
        throw UndoException("UI document is gone");

    if (!document->InsertNodeInternal(parentPath_, indexInParent_, node_))
        throw UndoException("Cannot re-insert UI node (parent path no longer resolves)");
}

void CreateRemoveUiNodeAction::RemoveNode() const
{
    UIViewDocument* document = document_.Get();
    if (!document)
        throw UndoException("UI document is gone");

    if (!document->RemoveNodeInternal(parentPath_, indexInParent_, node_))
        throw UndoException("Cannot remove UI node (path no longer resolves)");
}

void CreateRemoveUiNodeAction::Redo() const
{
    if (removed_)
        RemoveNode();
    else
        AddNode();
}

void CreateRemoveUiNodeAction::Undo() const
{
    if (removed_)
        AddNode();
    else
        RemoveNode();
}

}

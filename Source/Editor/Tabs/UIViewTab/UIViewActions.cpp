//
// Copyright (c) 2017-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#include "UIViewActions.h"

#include "UIViewDocument.h"

namespace Urho3D
{

UiDocumentSnapshotAction::UiDocumentSnapshotAction(UIViewDocument* document,
        const ea::vector<unsigned>& mergeKeyPath, const ea::string& undoText, const ea::string& redoText)
    : document_(document)
    , mergeKeyPath_(mergeKeyPath)
    , undoText_(undoText)
    , redoText_(redoText)
{
}

bool UiDocumentSnapshotAction::CanUndoRedo() const
{
    return document_.Get() != nullptr;
}

void UiDocumentSnapshotAction::Restore(const ea::string& text) const
{
    UIViewDocument* document = document_.Get();
    if (!document)
        throw UndoException("UI document is gone");

    if (!document->RestoreText(text))
        throw UndoException("UI document failed to reload from snapshot");
}

void UiDocumentSnapshotAction::Redo() const
{
    Restore(redoText_);
}

void UiDocumentSnapshotAction::Undo() const
{
    Restore(undoText_);
}

bool UiDocumentSnapshotAction::MergeWith(const EditorAction& other)
{
    const auto otherAction = dynamic_cast<const UiDocumentSnapshotAction*>(&other);
    if (!otherAction || mergeKeyPath_.empty())
        return false;

    if (document_ != otherAction->document_ || mergeKeyPath_ != otherAction->mergeKeyPath_)
        return false;

    redoText_ = otherAction->redoText_;
    return true;
}

}

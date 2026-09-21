//
// Copyright (c) 2017-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#pragma once

#include "../../Core/UndoManager.h"

#include <EASTL/string.h>
#include <EASTL/vector.h>

namespace Urho3D
{

class UIViewDocument;

/// One undoable UI edit, recorded as whole-document source-text snapshots.
/// The model (source text) is the single source of truth and every command
/// rebuilds the whole tree from it, so undo/redo simply restores the text that
/// was current before/after the edit and lets the document rebuild through its
/// ordinary load path. No node paths or pointers are held across edits, which
/// makes the action immune to structural drift.
///
/// \a mergeKeyPath lets consecutive payload edits of the same node collapse
/// into one undo step (mirrors the old ChangeUiNodeAction merge). Structural
/// commands (add/remove) pass an empty key and never merge.
class UiDocumentSnapshotAction : public EditorAction
{
public:
    UiDocumentSnapshotAction(UIViewDocument* document, const ea::vector<unsigned>& mergeKeyPath,
        const ea::string& undoText, const ea::string& redoText);

    /// Implement EditorAction.
    /// @{
    bool CanUndoRedo() const override;
    void Redo() const override;
    void Undo() const override;
    bool MergeWith(const EditorAction& other) override;
    /// @}

private:
    void Restore(const ea::string& text) const;

    WeakPtr<UIViewDocument> document_;
    ea::vector<unsigned> mergeKeyPath_; ///< Same-node edits merge; empty never merges
    ea::string undoText_;
    ea::string redoText_;
};

}

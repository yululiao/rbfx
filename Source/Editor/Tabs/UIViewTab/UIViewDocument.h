//
// Copyright (c) 2017-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#pragma once

#include "UIViewLayoutMath.h"

#include <Urho3D/Core/Object.h>
#include <Urho3D/Core/Signal.h>
#include <Urho3D/Graphics/Texture2D.h>
#include <Urho3D/Math/Vector2.h>

#include <EASTL/functional.h>
#include <EASTL/utility.h>
#include <EASTL/vector.h>

namespace Rml
{
class Element;
class ElementDocument;
}

namespace Urho3D
{

class RmlUI;
class EditorAction;

/// One editable UI document: the editor model (source of truth) together with
/// its live RmlUi projection and the offscreen preview surface. Extracted from
/// UIViewTab so the tab keeps only ImGui drawing and input routing.
///
/// Editing goes through the undoable commands (AddWidget / DuplicateNode /
/// DeleteNode / MaterializeNode / EditNodePayload / CommitBoxEdit). Each
/// command mutates the in-memory model, re-emits the whole document and then
/// rebuilds the model and the live projection from the emitted text - the
/// exact path a hand-edited file takes when it is (re)opened - so the preview
/// can never drift from what the saved source renders. Undo and redo restore
/// whole-document text snapshots (UiDocumentSnapshotAction). Views therefore
/// hold node pointers only within one generation and must revalidate on
/// OnModelEdited.
class UIViewDocument : public Object
{
    URHO3D_OBJECT(UIViewDocument, Object)

public:
    /// Fired after any model mutation (commands, undo, redo). The whole model
    /// tree has been rebuilt by then: views holding UiNode pointers must
    /// re-resolve them from stable child-index paths. Only the active document
    /// emits this: the UI edits the active document, and undo/redo re-focuses a
    /// document (ResourceActionWrapper) before restoring its text.
    Signal<void()> OnModelEdited;

    explicit UIViewDocument(Context* context);
    ~UIViewDocument() override;

    /// Install a callback that routes editing actions through the owning tab so
    /// the editor can attribute each change to the active resource (per-document
    /// dirty tracking + undo focus, see ResourceEditorTab). When it is unset or
    /// declines, commands fall back to the project UndoManager unchanged.
    void SetUndoPusher(ea::function<bool(SharedPtr<EditorAction>)> pusher) { undoPusher_ = ea::move(pusher); }

    /// Return properties of the document.
    /// @{
    const UiDocumentModel& GetModel() const { return model_; }
    Rml::ElementDocument* GetRmlDocument() const { return document_; }
    RmlUI* GetPreviewUI() const { return previewUI_; }
    Texture2D* GetPreviewTexture() const { return texture_; }
    const IntVector2& GetPreviewSize() const { return previewSize_; }
    /// Resource path the document was opened under (empty until loaded). Used
    /// as the reload URL and as the undo snapshot's document identity.
    const ea::string& GetSourcePath() const { return path_; }
    bool IsDirty() const { return dirty_; }
    void MarkSaved() { dirty_ = false; }
    void MarkDirty() { dirty_ = true; }
    /// @}

    /// Seed the model + projection from raw RML source text loaded under its
    /// real resource path (so relative <link>/<template> hrefs resolve).
    bool LoadFromText(const ea::string& text, const ea::string& path);
    /// Rebuild the model + projection from a snapshot text (undo/redo).
    bool RestoreText(const ea::string& text);
    /// Serialize the model back to complete .rml text.
    ea::string EmitRml() const { return model_.EmitRml(); }

    /// During-drag DOM-only preview write (the model is only touched on
    /// commit; layout re-flows on the next engine update).
    void SetLiveBox(UiNode* node, const UiBox& box);

    /// Offscreen rendering gate: with several documents open at once only the
    /// active one needs its preview texture refreshed every frame.
    void SetPreviewActive(bool active) { previewActive_ = active; }

    /// DOM queries for the views (document-space boxes and hit testing).
    /// @{
    UiNode* HitTest(const Vector2& docPos) const;
    /// Fill \a out with the document-space border boxes the node projects
    /// onto. A regular node yields exactly one box; the nested-doc virtual
    /// node yields one box per chrome element its template minted into the
    /// document (title bar, resize handles) so its outline marks the nested
    /// document's own contribution instead of the whole canvas.
    bool TryGetDomBoxes(const UiNode* node, ea::vector<UiBox>& out) const;
    Vector2 GetInlineStyleBase(const UiNode* node) const;
    /// @}

    /// Undoable editing commands. Each mutates the model, rebuilds the whole
    /// projection from the re-emitted text, marks the document dirty, records
    /// a snapshot action and notifies views (OnModelEdited).
    /// @{
    UiNode* AddWidget(UiNode* parent, const char* tag);
    UiNode* DuplicateNode(UiNode* node);
    bool DeleteNode(UiNode* node);
    bool MaterializeNode(UiNode* node);
    bool EditNodePayload(UiNode* node, const UiNodePayload& newData);
    /// Commit a solved gizmo box (drag release) as one recorded style edit.
    bool CommitBoxEdit(UiNode* node, const UiBox& box);
    /// @}

private:
    /// Renders the offscreen preview at a valid render-phase event.
    void HandleBeginRendering(StringHash eventType, VariantMap& eventData);
    /// Unique data-model name for this document instance.
    ea::string SubstituteDataModelToken(const ea::string& text) const;
    /// (Re)create the preview render-target texture.
    void Rebuild();
    /// Rebuild the RmlUi document and the whole model from \a text. path_
    /// must already be set (LoadFromText does that on open).
    bool ReloadFromText(const ea::string& text);
    /// Emit the model to text and rebuild the projection from it.
    bool EmitAndReload(ea::string& outText);
    /// Shift the authored left/top of the materialized node at \a path so it
    /// renders at \a desiredAbs. True when a correction was written (the
    /// caller re-emits and rebuilds).
    bool CorrectLanding(const ea::vector<unsigned>& path, const Vector2& desiredAbs);
    /// Commit one model edit: emit + whole rebuild (+ optional landing
    /// correction), record the undoable snapshot, mark dirty, notify views.
    /// On reload failure the pre-edit \a undoText is restored best-effort.
    bool CommitAndReload(const ea::string& undoText, const ea::vector<unsigned>& mergeKey,
        const ea::vector<unsigned>* landingPath = nullptr, const Vector2& desiredAbs = Vector2::ZERO);
    bool PushUndoAction(const SharedPtr<EditorAction>& action);

    ea::function<bool(SharedPtr<EditorAction>)> undoPusher_;

    SharedPtr<RmlUI> previewUI_;
    SharedPtr<Texture2D> texture_;
    Rml::ElementDocument* document_ = nullptr;

    UiDocumentModel model_;
    /// Resource path the document was loaded under; reused as the reload URL.
    ea::string path_;
    IntVector2 previewSize_{1024, 768};
    bool dirty_ = false;
    /// Set once per opened document after the no-effective-font warning fired,
    /// so the per-edit reloads do not spam the log.
    bool warnedNoFont_ = false;
    /// Whether HandleBeginRendering draws this document's preview this frame.
    bool previewActive_ = true;
};

}

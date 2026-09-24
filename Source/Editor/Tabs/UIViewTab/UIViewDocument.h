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

/// How much placeholder styling a born widget carries. Inline styles always
/// win over the project stylesheet (RmlUi has no !important), so anything
/// the editor authors is a takeover: the policies are ordered by how much
/// they take over.
enum class UiWidgetStylePolicy
{
    /// No inline look at all (label, text: they render their own content).
    None,
    /// Placeholder background + border. Only for elements that would render
    /// as literally nothing until the project stylesheet defines them:
    /// containers and arbitrary unknown tags. Never for native form controls.
    Panel,
    /// Border-only outline: a 1px edge that marks the element's shape. The
    /// visibility fallback for native form controls - without it they are
    /// fully invisible when the document does not link a stylesheet with
    /// rules for them. A border cannot cover the control's internal chrome
    /// (the select's arrow box, the progress's fill) - unlike a background,
    /// which would sit behind any unstyled child and fake the look.
    Outline,
};

/// Recipe for one palette widget: the tag plus optional default content and
/// the honest-minimum styling policy. Looks live in the project stylesheet;
/// the editor only authors what keeps the element usable before the project
/// has rules for it (a placeholder box for yet-unstyled containers, an
/// outline for otherwise-invisible controls, a born position box so new
/// widgets are visible and draggable at once).
struct UiWidgetSpec
{
    ea::string tag_;
    /// Single default attribute (type for <input>, src for <img>, ...).
    ea::string attrName_;
    ea::string attrValue_;
    /// Default text child content ("Button"); empty adds no text child.
    ea::string childText_;
    /// Repeated default child elements (a <select> needs its <option>s).
    ea::string childElemTag_;
    ea::string childElemText_;
    unsigned childElemCount_ = 0;
    /// Placeholder styling policy (see UiWidgetStylePolicy).
    UiWidgetStylePolicy stylePolicy_ = UiWidgetStylePolicy::None;
    /// Born with an explicit centered position box (vs joining the flow).
    bool materialize_ = true;
    /// Size of the born position box.
    Vector2 size_{160.0f, 48.0f};
};

/// One editable UI document: the editor model (source of truth) together with
/// its live RmlUi projection and the offscreen preview surface. Extracted from
/// UIViewTab so the tab keeps only ImGui drawing and input routing.
///
/// Editing goes through the undoable commands (AddWidget / DuplicateNode /
/// DeleteNode / EditNodePayload / CommitBoxEdit). Each
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
    /// The gizmo box for an absolutely positioned node (position: absolute) in
    /// the left/top frame its authored offsets resolve against, plus that
    /// frame's origin (written to \a base). Prefers authored px; when the node
    /// is positioned but not yet sized/offset it seeds from the rendered box
    /// against the containing block, so a freshly-positioned element is
    /// draggable at once. False when the node is not absolutely positioned.
    bool TryGetDragBox(const UiNode* node, UiBox& out, Vector2& base) const;
    /// Every real element node whose rendered border box lies fully inside the
    /// document-space rectangle [a,b] (corners need not be ordered). Used by
    /// rubber-band (marquee) selection; returns nodes in model pre-order.
    ea::vector<UiNode*> CollectNodesInRect(const Vector2& a, const Vector2& b) const;
    /// Re-fit \a box's layout->window map from the node's live DOM element;
    /// called every frame a gizmo drag is live so the overlay keeps tracking
    /// transform changes made during the gesture.
    void RefreshWindowMap(const UiNode* node, UiBox& box) const;
    /// Map a mouse point (doc px) into the gizmo frame of \a node (left/top
    /// layout space relative to \a base) with the ancestor transform chain
    /// stripped: Project unwinds the full chain exactly as RmlUi's own event
    /// picking does, then the node's own transform is re-applied. Falls back
    /// to the raw point minus \a base when there is no (or a singular)
    /// transform chain.
    Vector2 DocToGizmoFrame(const UiNode* node, const Vector2& base, const Vector2& docPoint) const;
    /// @}

    /// Undoable editing commands. Each mutates the model, rebuilds the whole
    /// projection from the re-emitted text, marks the document dirty, records
    /// a snapshot action and notifies views (OnModelEdited).
    /// @{
    /// Create a widget from a palette recipe under \a parent (root when null
    /// or unsuitable). Returns the new node in the rebuilt tree, or null.
    UiNode* AddWidget(UiNode* parent, const UiWidgetSpec& spec);
    UiNode* DuplicateNode(UiNode* node);
    bool DeleteNode(UiNode* node);
    /// Batch variants for multi-selection: every change is applied to the
    /// model first, then committed as ONE whole-document rebuild so the whole
    /// gesture is a single undo step. Callers must pass only real element
    /// nodes (no root/text/virtual) and a set where no node is a descendant of
    /// another (deleting an ancestor already removes its descendants).
    /// @{
    bool DeleteNodes(const ea::vector<UiNode*>& nodes);
    ea::vector<UiNode*> DuplicateNodes(const ea::vector<UiNode*>& nodes);
    /// @}
    /// Move a node (with its subtree) under \a newParent at \a index. Guards:
    /// no text/nested-doc/root involved, and \a newParent must not live inside
    /// the moved subtree (that would orphan it). Returns the moved node in the
    /// rebuilt tree, or null when the move was rejected.
    UiNode* MoveNode(UiNode* node, UiNode* newParent, unsigned index);
    bool EditNodePayload(UiNode* node, const UiNodePayload& newData);
    /// Commit a solved gizmo box (drag release) as one recorded style edit.
    bool CommitBoxEdit(UiNode* node, const UiBox& box);
    /// Commit solved gizmo boxes for several nodes as ONE recorded style edit
    /// (one undo step for a multi-selection drag). Applies every write to the
    /// model first, then a single rebuild, so all pointers stay live.
    bool CommitBoxEdits(const ea::vector<ea::pair<UiNode*, UiBox>>& edits);
    /// Add one <link> to the document <head> (\a type is "text/rcss" or
    /// "text/template"). <head> is spine territory (untouched by the tree
    /// commands above), so this is a text-level edit: emit, splice the link
    /// line, reload from the spliced text - one undo snapshot like any
    /// command. The new link goes right after the last existing one. False
    /// when the edit does not apply (no <head>, empty or duplicate entry).
    bool AddHeadLink(const ea::string& type, const ea::string& href);
    /// Rewrite the type/href of the head link a #head-link node stands for
    /// (the #nested-doc node stands for the instantiated template link and is
    /// accepted here too). A no-op (no reload, no undo step) when both values
    /// already match. Deleting a link goes through DeleteNode, which routes
    /// both node kinds to the text-level removal.
    bool EditHeadLink(UiNode* node, const ea::string& type, const ea::string& href);
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
    /// Commit a text-level edit (head links): reload from \a redoText, record
    /// one discrete undo snapshot (empty merge key - consecutive link edits
    /// must not collapse), mark dirty, notify views.
    bool CommitTextEdit(const ea::string& undoText, const ea::string& redoText);
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

//
// Copyright (c) 2017-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#pragma once

#include "../ResourceEditorTab.h"
#include "../Shared/HierarchyBrowserSource.h"
#include "../Shared/InspectorSource.h"
#include "UIViewDocument.h"

namespace Rml
{
class Element;
class ElementDocument;
}

namespace Urho3D
{

class HierarchyBrowserTab;
class InspectorTab;
class UIViewTab;
class UIViewHierarchy;
class UIViewInspector;
struct ResourceFileDescriptor;

/// Bootstrapped by EditorApplication.
void Tabs_UIViewTab(Context* context, Project* project);

/// Editor tab that lets the user author an RmlUi document (.rml file).
///
/// Layering (data / logic / view separation):
/// - DATA: UiDocumentModel / UiNode (UIViewDocumentModel) is the source of
///   truth - pure data + (de)serialization.
/// - LOGIC: UIViewLayoutMath holds the RmlUi/ImGui-free interaction math
///   (viewport mapping, transformed hit-testing, gizmo drag solving).
/// - DOCUMENT: UIViewDocument owns the model, its live RmlUi DOM projection,
///   the offscreen preview surface and the undoable editing commands.
/// - VIEW/CONTROLLER: this tab renders ImGui (toolbar, preview, overlay,
///   gizmo), routes pointer input into selection and drags, and forwards
///   editing commands to UIViewDocument. It owns no editing logic itself.
///
/// Deliberately does not use the RmlWorldCanvas Drawable: the editor only needs
/// a 2D preview and using RmlUI + Texture2D directly keeps this tab independent
/// of the Octree / Scene plumbing.
///
/// Derives from ResourceEditorTab so that double-clicking a .rml in the
/// Resource Browser opens it here, and the base owns the surrounding
/// bookkeeping: active-resource tracking, per-document dirty tracking,
/// undo/redo attribution and the project save/close pipeline.
///
/// Multi-instance editors: every open .rml gets its OWN UIViewTab instance
/// (VS Code style - one document per editor tab). The editor framework keys
/// a tab's identity on its title (ImGui window id, ini section, Project tab
/// registry), so each instance takes a unique title ("UI", "UI (2)", ...).
/// Opening is routed through OpenInBestInstance: an already-open document
/// focuses its owning instance, an idle instance takes a new document, and
/// when every instance is busy a fresh one is spawned. The first instance
/// (the one the plugin bootstraps) is the persistent entry point: it stays
/// around with its "open/new document" empty state after its document
/// closes, while secondary instances close together with their document.
class UIViewTab : public ResourceEditorTab
{
    URHO3D_OBJECT(UIViewTab, ResourceEditorTab)

public:
    explicit UIViewTab(Context* context);
    ~UIViewTab() override;
    /// The editable document hosted by this tab.
    UIViewDocument* GetDocument() const { return document_.Get(); }

    /// Selected model node, may be null. Valid only within the current model
    /// generation: every command rebuilds the whole tree from text, and
    /// OnDocumentEdited re-resolves this from the child-index path.
    UiNode* GetSelectedNode() const { return selected_; }
    /// Child-index path of the selection (stable across model rebuilds).
    const ea::vector<unsigned>& GetSelectedPath() const { return selPath_; }
    /// Select from the preview or the hierarchy; keeps selPath_ and the
    /// hierarchy expand state in sync. Passing null clears the selection.
    /// Single-select: it replaces the whole selection with this one node.
    void SetSelectedNode(UiNode* node);

    /// Multi-selection. The source of truth is the ordered set of child-index
    /// paths (see selPaths_); the LAST entry is the primary/anchor that the
    /// Inspector edits and the gizmo drags (== GetSelectedNode()). Raw node
    /// pointers are only valid within the current model generation and are
    /// re-resolved from their paths on every OnModelEdited.
    /// @{
    /// Number of live selected nodes.
    int GetSelectedCount() const { return static_cast<int>(sels_.size()); }
    /// Live selected nodes in selection order (primary last).
    ea::vector<UiNode*> GetSelectedNodes() const;
    /// Top-most selected real elements: drops any node whose ancestor is also
    /// selected, so batch delete / duplicate / move never double-applies to a
    /// subtree (a child rides along with its parent).
    ea::vector<UiNode*> GetTopLevelSelectedNodes() const;
    /// Whether the given child-index path is part of the selection.
    bool IsSelected(const ea::vector<unsigned>& path) const;
    /// Add / remove a node from the selection without disturbing the rest
    /// (ctrl-click). A null node clears everything.
    void ToggleSelectNode(UiNode* node);
    /// Replace the whole selection with \\a nodes; the last becomes primary.
    void SetSelection(const ea::vector<UiNode*>& nodes);
    /// Batch editing entry points shared by the toolbar, the preview context
    /// menu and the hierarchy context menu. They act on the top-level
    /// selection as a single undo step.
    void CopySelection();
    void DeleteSelection();
    /// @}

    /// Structural flow commands (context menus, toolbar, keyboard): wrap the
    /// top-level selection into a flex/plain container, or trade the primary
    /// selection's slot with its nearest element sibling. Both are one undo
    /// step and no-op when their rules reject the selection.
    /// @{
    void WrapSelection(UiWrapMode mode);
    /// Empty string when WrapSelection may run; otherwise the user-facing
    /// reason it is unavailable (drives the disabled menu entries).
    ea::string WrapUnavailableReason() const;
    void MoveSelectionInFlow(int delta);
    bool CanMoveInFlow(int delta) const;
    /// @}

    /// Hierarchy/Inspector data sources hosted by this tab. The Glue binds
    /// the shared HierarchyBrowserTab / InspectorTab to these on focus.
    UIViewHierarchy* GetHierarchySource() const { return hierarchySource_; }
    UIViewInspector* GetInspectorSource() const { return inspectorSource_; }

    /// Route a .rml to the best UIViewTab instance: the one already editing
    /// it (focus), an idle instance, or a freshly spawned one. Idempotent,
    /// so every subscribed instance can funnel an OpenResourceRequest here.
    static void OpenInBestInstance(Project* project, const ea::string& resourceName);

    /// Implement EditorTab.
    void RenderContent() override;
    bool IsUndoSupported() override { return true; }

protected:
    /// Implement ResourceEditorTab.
    /// @{
    ea::string GetResourceTitle() override { return "UI document"; }
    bool CanOpenResource(const ResourceFileDescriptor& desc) override;
    /// Single-document instance (one .rml per editor tab). Multi-document
    /// workflows are served by several UIViewTab instances - see
    /// OpenInBestInstance.
    bool SupportMultipleResources() override { return false; }
    void OnResourceLoaded(const ea::string& resourceName) override;
    void OnResourceUnloaded(const ea::string& resourceName) override;
    void OnActiveResourceChanged(const ea::string& oldResourceName, const ea::string& newResourceName) override;
    void OnResourceSaved(const ea::string& resourceName) override;
    void OnResourceShallowSaved(const ea::string& resourceName) override;
    /// Save guard: the base consults this before writing. Refuses when the
    /// file changed on disk since this instance last loaded or wrote it, and
    /// asks the user whether to overwrite (RenderExternalChangeDialog).
    bool CanSaveResource(const ea::string& resourceName) override;
    /// Route open requests through OpenInBestInstance instead of opening the
    /// resource in every subscribed instance (the base behavior).
    void OnProjectRequest(ProjectRequest* request) override;
    /// @}

    /// Persist open documents. The primary instance serializes the active
    /// document of EVERY UIViewTab instance under its own ini section and
    /// re-creates the secondary instances on restore; secondary instances
    /// persist nothing (their sections would have no reader after restart).
    /// @{
    void WriteIniSettings(ImGuiTextBuffer& output) override;
    void ReadIniSettings(const char* line) override;
    /// @}
    bool CloseShouldRemove() override { return true; }

private:
    void RenderToolbar();
    void RenderPreview();
    /// Modal of the save guard: the file changed on disk while the document
    /// was open and a save wanted to write over it. Overwrite re-runs the
    /// save with a one-shot approval; Cancel keeps the document unsaved.
    void RenderExternalChangeDialog();

    /// Runtime preview: play the edited scene plus this document in the Game
    /// View. The session itself (start/stop, focus, Game View open/close) is
    /// owned by ProjectGlue, reached through Project::OnRequestUiPreview.
    /// @{
    /// Toolbar Run/Stop click: start the preview, or stop the running session
    /// that is previewing this document.
    void ToggleRunInGameView();
    /// Whether the running Game View session is previewing this document.
    bool IsPreviewingInGameView() const;
    /// Empty when the Run button may start a preview; otherwise the reason it
    /// is disabled (shown as its tooltip).
    ea::string RunInGameViewUnavailableReason() const;
    /// @}
    /// Floating inline editor for a pure-text element's #text child: Enter or
    /// blur submits through EditNodePayload, Esc reverts (ImGui built-in).
    void RenderInlineTextEdit(const DocViewport& vp);
    void BeginInlineTextEdit(UiNode* textNode);
    void EndInlineTextEdit(bool commit);
    /// Canvas keyboard shortcuts (Delete / Ctrl+D / Esc / Alt+Up / Alt+Down /
    /// F), inert while any text field wants the keyboard.
    void HandleShortcuts();
    /// Screen-space top-left of the scaled canvas image for the current view
    /// state (centered; viewPan_ nudges it).
    ImVec2 CanvasOrigin(const ImVec2& canvasMin, const ImVec2& avail) const;
    /// Wheel zoom (anchored on the pointer) and middle-drag pan; every other
    /// view consumer reads the DocViewport built from the result.
    void HandleViewInput(const ImVec2& canvasMin, const ImVec2& avail, bool canvasHovered);
    /// Apply a canvas size to the view preference + the open document, then
    /// re-fit so the whole canvas stays visible.
    void ApplyCanvasSize(const IntVector2& size);
    /// Index of the nearest element sibling of \a node in direction \a delta
    /// (-1 up, +1 down), or M_MAX_UNSIGNED when there is none. Shared by the
    /// reorder command and its enablement.
    unsigned FindFlowNeighborIndex(UiNode* node, int delta) const;

    /// Create a fresh .rml from the built-in template via a native "Save As" dialog rooted at
    /// the project Data folder, then open it (a new document must live on disk to be a resource).
    /// Leaves a document file at exactly the path the user picked, or says why it could not.
    void NewDocument();

    /// Raw resource-path <-> text helpers shared by load/save/new.
    /// @{
    ea::string ReadResourceFile(const ea::string& resourceName) const;
    bool WriteResourceFile(const ea::string& resourceName, const ea::string& text);
    /// Write to an absolute disk path, creating missing parent dirs, then confirm the
    /// file really is there. Used where the path itself is the contract (New).
    bool WriteFileAt(const ea::string& absPath, const ea::string& text);
    /// @}

    /// Drop selection/gizmo state so it starts fresh against a (re)loaded doc.
    void ResetViewToDocument();

    /// Rebind the shared Hierarchy/Inspector tabs to this instance. Complements
    /// the focus glue, which only fires after the window actually gains ImGui
    /// focus: on a runtime open the browser's InspectResourceRequest is
    /// processed earlier in the same request queue and hands the Inspector to
    /// the placeholder fallback (.rml has no dedicated inspector), and nothing
    /// would hand it back until the window is focused.
    void ConnectSharedPanels();

    // --- selection helpers ---------------------------------------------------
    ea::vector<unsigned> NodePath(const UiNode* node) const;
    /// Re-point selected_ / selPath_ at the last live selection entry.
    void SyncPrimary();
    /// Revalidate the selection after a model mutation. Only the active
    /// document emits OnModelEdited (see UIViewDocument) - the UI edits the
    /// active document and undo/redo re-focuses before restoring.
    void OnDocumentEdited();

    // --- pointer / overlay (view + controller) ------------------------------
    void HandlePreviewPointer(const DocViewport& vp);
    void DrawOverlay(const DocViewport& vp);
    void DrawGizmo(const DocViewport& vp, const UiBox& box);
    void BeginDrag(const GizmoHandle& handle, UiNode* node, const DocViewport& vp);
    void UpdateDrag(const DocViewport& vp);
    void CommitDrag();
    /// Esc during a gizmo drag: put the DOM-only live preview back to the
    /// press box; nothing was committed, so there is no undo step to roll back.
    void CancelDrag();
    /// Rubber-band (marquee) selection: draw the live band and resolve it into
    /// a selection on release (a sub-threshold band degrades to a click-select).
    void DrawMarquee(const DocViewport& vp);
    void FinishMarquee(const DocViewport& vp);

    /// Structural (flow) drag: move / re-parent a node by dragging it in the
    /// preview. Pressing a flow element arms the candidate (a plain click
    /// still selects); past the threshold the drop is solved every frame into
    /// the drop feedback below and committed as one MoveNode on release.
    /// @{
    bool IsStructDragSource(const UiNode* node) const;
    void CancelStructDrag();
    /// Solve where \a source would land for a pointer at \a mouseDoc and fill
    /// the drop feedback; the in-canvas drag and external payloads share this
    /// and ApplyDrop. \a source may be null (a fresh widget, no cycle rules).
    void EvaluateDrop(UiNode* source, const Vector2& mouseDoc, bool overCanvas);
    /// Commit the evaluated drop as one undoable MoveNode (a no-op when the
    /// node would not actually move).
    void ApplyDrop(UiNode* source);
    /// The canvas as a drop target for external payloads: hierarchy rows
    /// (kUiNodeDragType) and single image files (ResourceDragDropPayload).
    /// Called once per frame from RenderPreview.
    void HandlePreviewDrop(const DocViewport& vp);
    /// Insert a dropped single-image resource as an <img> at the evaluated
    /// slot, sized from the texture.
    void ApplyResourceDrop(const ResourceFileDescriptor& desc);
    /// @}
    /// Drop indicator for the feedback state above (structural drags and
    /// external payloads alike).
    void DrawDropIndicator(const DocViewport& vp);

    /// The document (model + live DOM projection + undo commands) edited by
    /// this instance. One instance edits at most one resource; the base
    /// swaps it via the load/unload/activate callbacks.
    SharedPtr<UIViewDocument> document_;

    // --- external-change guard (overwrite protection) -----------------------
    /// Bytes of the file as this instance last loaded or wrote it: different
    /// bytes on disk mean someone else edited the file while it was open.
    ea::string diskText_;
    /// Resource diskText_ belongs to (empty = no baseline yet).
    ea::string diskTextName_;
    /// Resource waiting for the user's overwrite decision (modal pending).
    ea::string pendingExternalOverwrite_;
    /// One-shot approval from the modal, consumed by the next
    /// CanSaveResource call for that resource.
    ea::string overwriteApproved_;
    /// Whether the modal for pendingExternalOverwrite_ is open in ImGui.
    bool externalDialogOpen_ = false;

    UiNode* selected_ = nullptr;
    ea::vector<unsigned> selPath_;
    ea::vector<unsigned> hoveredPath_;
    /// Multi-selection source of truth: ordered child-index paths, the LAST
    /// being the primary/anchor (mirrored into selected_ / selPath_).
    ea::vector<ea::vector<unsigned>> selPaths_;
    /// Live node pointers parallel to selPaths_, re-resolved on model edits.
    ea::vector<UiNode*> sels_;

    // --- rubber-band (marquee) selection state ------------------------------
    bool marqueeActive_ = false;
    bool marqueeAdditive_ = false;
    Vector2 marqueeStartDoc_;
    Vector2 marqueeCurDoc_;

    // --- structural (flow) drag state ---------------------------------------
    bool structDragActive_ = false; ///< a press armed a drag candidate
    bool structDragging_ = false; ///< past the threshold: the move is live
    UiNode* structDragNode_ = nullptr; ///< the node being moved
    Vector2 structDragStartDoc_; ///< document point of the press (threshold)

    // --- inline text edit state ---------------------------------------------
    bool textEditActive_ = false; ///< the floating #text editor is live
    bool textEditJustOpened_ = false; ///< first frame: focus, skip blur-commit
    UiNode* textEditNode_ = nullptr; ///< the #text node being edited
    /// Child-index path of that node: the stable identity that survives the
    /// whole-tree rebuilds (OnDocumentEdited re-resolves the pointer from it).
    ea::vector<unsigned> textEditPath_;
    char textEditBuf_[1024]{};

    // --- preview view control (zoom / pan / canvas size) --------------------
    float viewZoom_ = 1.0f; ///< screen px per document px (0.1-8.0)
    bool viewFit_ = true; ///< zoom follows the panel until a manual zoom/pan
    Vector2 viewPan_; ///< screen-space nudge of the centered canvas
    bool panningActive_ = false;
    Vector2 panStartMouse_;
    Vector2 panStartOffset_;
    /// Canvas-size preference of the preview. One value shared by every
    /// instance - a tab spawned while the ini is replayed must open on the
    /// same canvas - persisted to the editor ini by the primary instance,
    /// and never written into the .rml.
    static IntVector2 sViewCanvasSize_;
    /// Custom W/H fields of the toolbar combo (view state, re-seeded on open).
    int customCanvasW_ = 1024;
    int customCanvasH_ = 768;

    // --- widget palette filter (Add Widget combo) ---------------------------
    char paletteFilter_[64]{};

    // --- drop feedback (in-canvas drag and external payloads alike) ---------
    /// 0 none, 1 before the hit node, 2 after it, 3 inside it.
    unsigned dropKind_ = 0;
    UiNode* dropParent_ = nullptr; ///< resolved insertion parent
    unsigned dropIndex_ = 0; ///< full child index to insert at
    UiBox dropHitBox_; ///< the hovered node's document-space border box
    bool dropHaveHitBox_ = false;
    bool dropSiblingAxisIsRow_ = false; ///< flow axis of the hit's parent
    bool dropChildAxisIsRow_ = false; ///< flow axis inside the hit
    ea::vector<UiBox> dropChildBoxes_; ///< element children boxes (slot drawing)
    ea::vector<unsigned> dropChildIndices_; ///< their full child indices

    // --- multi-node move drag (extras tracked beside the primary gizmo) -----
    ea::vector<UiNode*> extraDragNodes_;
    ea::vector<UiBox> extraDragStarts_;

    /// First-ever instance (plugin-bootstrapped): the persistent "new/open
    /// document" entry point; stays open when its document closes.
    bool isPrimary_ = true;

    // --- gizmo drag state (data-driven; solved via UIViewLayoutMath) --------
    UiNode* gizmoNode_ = nullptr;
    GizmoHandle gizmoDrag_; ///< op_/mask_ of the handle being dragged
    UiBox gizmoStartBox_; ///< box captured at press
    UiBox gizmoLiveBox_; ///< box solved for the latest drag frame (overlay + tooltip)
    Vector2 gizmoBase_; ///< absolute origin of the left/top frame, captured at press
    Vector2 gizmoPressDoc_; ///< document-space mouse at press
    Vector2 gizmoCurDoc_; ///< document-space mouse of the latest frame
    bool dragging_ = false;

    SharedPtr<UIViewHierarchy> hierarchySource_;
    SharedPtr<UIViewInspector> inspectorSource_;
};

/// HierarchyBrowserSource: walks the editor model of the tab's document.
class UIViewHierarchy : public Object, public HierarchyBrowserSource
{
    URHO3D_OBJECT(UIViewHierarchy, Object)

public:
    explicit UIViewHierarchy(UIViewTab* owner);

    /// Implement HierarchyBrowserSource
    /// @{
    EditorTab* GetOwnerTab() override { return owner_; }
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

/// InspectorSource: edits attributes and inline style of the selected model node.
class UIViewInspector : public Object, public InspectorSource
{
    URHO3D_OBJECT(UIViewInspector, Object)

public:
    explicit UIViewInspector(UIViewTab* owner);

    /// Implement InspectorSource
    /// @{
    EditorTab* GetOwnerTab() override { return owner_; }
    void RenderContent() override;
    /// Attribute and inline-style edits push onto the same project-wide
    /// UndoManager as the owner tab.
    bool IsUndoSupported() override { return true; }
    /// @}

    /// Drop cached per-node edit state (inline-style seed) so the next render
    /// re-reads it from the rebuilt model.
    void InvalidateCaches();

private:
    /// Document-level "add head link" row. The links themselves are
    /// #head-link nodes at the top of the Hierarchy; this is only the spigot
    /// that appends one more <link> to <head>. Rendered above the per-node
    /// editors because it applies with or without a selection.
    void RenderHeadLinks();
    /// Per-link panel for a #head-link node: type/href editing, navigation to
    /// the linked file, removal. The generic attribute/style editors do not
    /// apply - head bytes are edited through the text-level link commands.
    void RenderHeadLink(UiNode* node);
    /// Navigation panel for the nested-doc virtual node (path + reveal/open).
    void RenderNestedDoc(UiNode* node);
    /// The four sections below return true when they committed an edit: any
    /// commit rebuilds the whole model tree, which invalidates every UiNode
    /// pointer - including the caller's. On true, RenderContent stops rendering
    /// for this frame and re-renders from the rebuilt model on the next one
    /// (the commit-then-return pattern the link panels already use).
    bool RenderTextContent(UiNode* node);
    bool RenderAttributes(UiNode* node);
    bool RenderLayout(UiNode* node);
    bool RenderAppearance(UiNode* node);
    bool RenderInlineStyle(UiNode* node);
    /// Read-only sections: they never commit, so the node stays valid.
    void RenderTemplates(UiNode* node);
    void RenderComputed(UiNode* node);

    WeakPtr<UIViewTab> owner_;
    char attributeKeyBuf_[128]{};
    char attributeValueBuf_[1024]{};
    char styleBuf_[2048]{};
    /// Resource path being typed into the head-link field.
    char headLinkHrefBuf_[256]{};
    // Cached inline-style text and the selection path it was seeded from, so
    // the multiline editor is only refreshed when the selection changes.
    ea::vector<unsigned> lastStylePath_;
    bool styleSeedValid_ = false;
    // Cached paragraph-content text: a <p>'s Content field is the multi-line
    // editor, seeded only when the selection changes or the model was rebuilt.
    char contentBuf_[4096]{};
    ea::vector<unsigned> lastContentPath_;
    bool contentSeedValid_ = false;
};

}

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
    void SetSelectedNode(UiNode* node);

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

    /// The document (model + live DOM projection + undo commands) edited by
    /// this instance. One instance edits at most one resource; the base
    /// swaps it via the load/unload/activate callbacks.
    SharedPtr<UIViewDocument> document_;

    UiNode* selected_ = nullptr;
    ea::vector<unsigned> selPath_;
    ea::vector<unsigned> hoveredPath_;

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
    /// The three sections below return true when they committed an edit: any
    /// commit rebuilds the whole model tree, which invalidates every UiNode
    /// pointer - including the caller's. On true, RenderContent stops rendering
    /// for this frame and re-renders from the rebuilt model on the next one
    /// (the commit-then-return pattern the link panels already use).
    bool RenderTextContent(UiNode* node);
    bool RenderAttributes(UiNode* node);
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
};

}

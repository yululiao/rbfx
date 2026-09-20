//
// Copyright (c) 2017-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#pragma once

#include "ResourceEditorTab.h"
#include "Shared/HierarchyBrowserSource.h"
#include "Shared/InspectorSource.h"
#include "UIViewDocument.h"

namespace Rml
{
class Element;
class ElementDocument;
}

namespace Urho3D
{

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
/// bookkeeping: active-resource switching, per-document dirty tracking,
/// undo/redo attribution and the project save/close pipeline. Multi-document is
/// disabled for now (SupportMultipleResources() == false), so opening another
/// .rml gracefully closes the current one (prompting to save if it is dirty).
class UIViewTab : public ResourceEditorTab
{
    URHO3D_OBJECT(UIViewTab, ResourceEditorTab)

public:
    explicit UIViewTab(Context* context);
    ~UIViewTab() override;

    /// The editable document hosted by this tab.
    UIViewDocument* GetDocument() const { return document_.Get(); }

    /// Selected model node, may be null. Node identity is stable across DOM
    /// reloads (only its dom_ projection is refreshed), so this never dangles
    /// between commands within the same document generation.
    UiNode* GetSelectedNode() const { return selected_; }
    /// Child-index path of the selection (stable across DOM reloads).
    const ea::vector<unsigned>& GetSelectedPath() const { return selPath_; }
    /// Select from the preview or the hierarchy; keeps selPath_ and the
    /// hierarchy expand state in sync. Passing null clears the selection.
    void SetSelectedNode(UiNode* node);

    const ea::string& GetResourcePath() const { return resourcePath_; }

    /// Hierarchy/Inspector data sources hosted by this tab. The Glue binds
    /// the shared HierarchyBrowserTab / InspectorTab to these on focus.
    UIViewHierarchy* GetHierarchySource() const { return hierarchySource_; }
    UIViewInspector* GetInspectorSource() const { return inspectorSource_; }

    /// Shared tab used by Hierarchy/Inspector sources to look up the
    /// project-scope instance.
    static UIViewTab* GetActive(Project* project);

    /// Implement EditorTab.
    void RenderContent() override;
    bool IsUndoSupported() override { return true; }

protected:
    /// Implement ResourceEditorTab.
    /// @{
    ea::string GetResourceTitle() override { return "UI document"; }
    bool CanOpenResource(const ResourceFileDescriptor& desc) override;
    bool SupportMultipleResources() override { return false; }
    void OnResourceLoaded(const ea::string& resourceName) override;
    void OnResourceUnloaded(const ea::string& resourceName) override;
    void OnActiveResourceChanged(const ea::string& oldResourceName, const ea::string& newResourceName) override;
    void OnResourceSaved(const ea::string& resourceName) override;
    void OnResourceShallowSaved(const ea::string& resourceName) override;
    /// @}

private:
    void RenderToolbar();
    void RenderPreview();

    /// Create a fresh .rml from the built-in template at the path in the toolbar
    /// field, then open it (a new document must live on disk to be a resource).
    void NewDocument();

    /// Raw resource-path <-> text helpers shared by load/save/new.
    /// @{
    ea::string ReadResourceFile(const ea::string& resourceName) const;
    bool WriteResourceFile(const ea::string& resourceName, const ea::string& text);
    /// @}

    /// Drop selection/gizmo state so it starts fresh against a (re)loaded doc.
    void ResetViewToDocument();

    // --- selection helpers ---------------------------------------------------
    ea::vector<unsigned> NodePath(const UiNode* node) const;
    /// Revalidate the selection after a model mutation (OnModelEdited).
    void OnModelEdited();

    // --- pointer / overlay (view + controller) ------------------------------
    void HandlePreviewPointer(const DocViewport& vp);
    void DrawOverlay(const DocViewport& vp);
    void DrawGizmo(const DocViewport& vp, const UiBox& box);
    void BeginDrag(const GizmoHandle& handle, UiNode* node, const DocViewport& vp);
    void UpdateDrag(const DocViewport& vp);
    void CommitDrag();

    /// The editable document (model + live DOM projection + undo commands).
    SharedPtr<UIViewDocument> document_;

    UiNode* selected_ = nullptr;
    ea::vector<unsigned> selPath_;
    ea::vector<unsigned> hoveredPath_;

    // --- gizmo drag state (data-driven; solved via UIViewLayoutMath) --------
    UiNode* gizmoNode_ = nullptr;
    GizmoHandle gizmoDrag_; ///< op_/mask_ of the handle being dragged
    UiBox gizmoStartBox_; ///< box captured at press
    UiBox gizmoLiveBox_; ///< box solved for the latest drag frame (overlay + tooltip)
    Vector2 gizmoBase_; ///< absolute origin of the left/top frame, captured at press
    Vector2 gizmoPressDoc_; ///< document-space mouse at press
    Vector2 gizmoCurDoc_; ///< document-space mouse of the latest frame
    bool dragging_ = false;

    ea::string resourcePath_;
    char pathInputBuf_[512]{};

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
    /// @}

    /// Expand the ancestor chain of the given node path (called on selection).
    void ExpandAncestors(const ea::vector<unsigned>& path);

private:
    void RenderNode(UiNode* node, const ea::vector<unsigned>& path);
    bool IsOpen(UiNode* node, const ea::vector<unsigned>& path) const;
    static bool PathIn(const ea::vector<ea::vector<unsigned>>& set, const ea::vector<unsigned>& path);

    WeakPtr<UIViewTab> owner_;
    UiNode* contextMenuTarget_ = nullptr;
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
    /// @}

private:
    void RenderAttributes(UiNode* node);
    void RenderInlineStyle(UiNode* node);
    void RenderComputed(UiNode* node);

    WeakPtr<UIViewTab> owner_;
    char attributeKeyBuf_[128]{};
    char attributeValueBuf_[1024]{};
    char styleBuf_[2048]{};
    // Cached inline-style text and the selection path it was seeded from, so
    // the multiline editor is only refreshed when the selection changes.
    ea::vector<unsigned> lastStylePath_;
    bool styleSeedValid_ = false;
};

}

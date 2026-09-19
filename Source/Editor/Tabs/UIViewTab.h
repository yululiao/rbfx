//
// Copyright (c) 2017-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#pragma once

#include "EditorTab.h"
#include "Shared/HierarchyBrowserSource.h"
#include "Shared/InspectorSource.h"
#include "UIViewLayoutMath.h"

#include <Urho3D/Graphics/Texture2D.h>

namespace Rml
{
class Element;
class ElementDocument;
}

namespace Urho3D
{

class RmlUI;
class UIViewTab;
class UIViewHierarchy;
class UIViewInspector;

/// Bootstrapped by EditorApplication.
void Tabs_UIViewTab(Context* context, Project* project);

/// Editor tab that lets the user author an RmlUi document (.rml file).
///
/// Layering (data / view / logic separation):
/// - DATA: UiDocumentModel / UiNode (UIViewDocumentModel) is the source of
///   truth - pure data + (de)serialization.
/// - LOGIC: UIViewLayoutMath holds the RmlUi/ImGui-free interaction math
///   (viewport mapping, transformed hit-testing, gizmo drag solving).
/// - VIEW: the RmlUi DOM is a runtime projection rendered offscreen into a
///   dynamic Texture2D; the ImGui overlay draws hover/selection/gizmo on top.
/// - CONTROLLER: this tab owns editing state and routes events, feeding DOM
///   numbers into the pure logic and writing results back to model / DOM.
///
/// Deliberately does not use the RmlWorldCanvas Drawable: the editor only needs
/// a 2D preview and using RmlUI + Texture2D directly keeps this tab independent
/// of the Octree / Scene plumbing.
class UIViewTab : public EditorTab
{
    URHO3D_OBJECT(UIViewTab, EditorTab)

public:
    explicit UIViewTab(Context* context);
    ~UIViewTab() override;

    /// Load (or reload) the .rml document from a resource path.
    void LoadDocument(const ea::string& path);
    /// Open the given resource path in the editor (loads if not already there).
    void OpenResource(const ea::string& path);
    /// Emit the model and write it back to its source file. Returns true on success.
    bool SaveDocument();
    /// Emit the model and write it to an explicit resource path (Save-As / first
    /// save of a new document). Also rebinds resourcePath_.
    bool SaveDocumentTo(const ea::string& path);
    /// Create an untitled in-memory document from the built-in template.
    void NewDocument();

    /// Selected model node, may be null. Node identity is stable across DOM
    /// reloads (only its dom_ projection is refreshed), so this never dangles.
    UiNode* GetSelectedNode() const { return selected_; }
    /// Select from the preview or the hierarchy; keeps selPath_ and the
    /// hierarchy expand state in sync. Passing null clears the selection.
    void SetSelectedNode(UiNode* node);

    /// The document projected from the model, or null.
    Rml::ElementDocument* GetDocument() const { return document_; }
    /// The resource path of the loaded document (may be empty for untitled).
    const ea::string& GetResourcePath() const { return resourcePath_; }
    /// The offscreen RmlUI instance owning the document. Never null.
    RmlUI* GetPreviewUI() const { return previewUI_; }
    /// Preview texture; ImGui renders this in the tab's viewport.
    Texture2D* GetPreviewTexture() const { return texture_; }
    const UiDocumentModel& GetModel() const { return model_; }

    /// Hierarchy/Inspector data sources hosted by this tab. The Glue binds
    /// the shared HierarchyBrowserTab / InspectorTab to these on focus.
    UIViewHierarchy* GetHierarchySource() const { return hierarchySource_; }
    UIViewInspector* GetInspectorSource() const { return inspectorSource_; }

    /// Shared tab used by Hierarchy/Inspector sources to look up the
    /// project-scope instance.
    static UIViewTab* GetActive(Project* project);

    /// Implement EditorTab.
    void RenderContent() override;

private:
    friend class UIViewHierarchy;
    friend class UIViewInspector;

    void Rebuild();
    void RenderToolbar();
    void RenderPreview();
    // Renders the offscreen preview into the texture at a valid render-phase
    // event (start of the graphics frame); RenderPreview() samples it later.
    void HandleBeginRendering(StringHash eventType, VariantMap& eventData);

    // --- preview lifecycle (data -> DOM projection) -------------------------
    ea::string SubstituteDataModelToken(const ea::string& text) const;
    /// Rebuild the DOM from the emitted model text (structural changes).
    void ReloadPreview();
    /// Seed the model + projection from raw source text.
    bool LoadDocumentFromText(const ea::string& text, const ea::string& path);

    // --- editing operations (mutate model, then reload or sync DOM) ---------
    void MaterializeNode(UiNode* node);
    void AddWidget(const char* tag);
    void DeleteSelected();
    void DuplicateSelected();
    /// Push a node's current inline style (from the model) into the live DOM.
    void SyncStyleToDom(UiNode* node);
    /// (Re)create the live DOM subtree for a (new) model node under parentEl,
    /// linking every UiNode::dom_ as it goes. Used instead of a full re-emit +
    /// reload so template/data-bound documents are never re-instantiated.
    Rml::Element* CreateDomForNode(UiNode& node, Rml::Element* parentEl);
    /// Push a node's id/class/attributes/inline-style back onto its live element.
    void ApplyNodeToDom(UiNode* node);
    /// Apply a single node's model edits to the DOM and refresh layout (used by
    /// the Inspector after an in-place property/style change).
    void CommitNodeEdit(UiNode* node);

    // --- pointer / overlay (view + controller) ------------------------------
    /// Deepest model node whose DOM border box (inverse-transformed) contains
    /// the document-space point; null when outside.
    UiNode* HitTestPreview(const Vector2& docPos) const;
    void HandlePreviewPointer(const DocViewport& vp);
    void DrawOverlay(const DocViewport& vp);
    void DrawGizmo(const DocViewport& vp, const UiBox& box);
    void BeginDrag(const GizmoHandle& handle, UiNode* node, const DocViewport& vp);
    void UpdateDrag(const DocViewport& vp);
    void CommitDrag();

    SharedPtr<RmlUI> previewUI_;
    SharedPtr<Texture2D> texture_;
    Rml::ElementDocument* document_ = nullptr;

    UiDocumentModel model_;

    UiNode* selected_ = nullptr;
    ea::vector<unsigned> selPath_;
    ea::vector<unsigned> hoveredPath_;
    ea::vector<unsigned> NodePath(const UiNode* node) const;

    // --- gizmo drag state (data-driven; solved via UIViewLayoutMath) --------
    UiNode* gizmoNode_ = nullptr;
    GizmoHandle gizmoDrag_; ///< op_/mask_ of the handle being dragged
    UiBox gizmoStartBox_; ///< box captured at press
    UiBox gizmoLiveBox_; ///< box solved for the latest drag frame (overlay + tooltip)
    Vector2 gizmoBase_; ///< absolute origin of the left/top frame, captured at press
    Vector2 gizmoPressDoc_; ///< document-space mouse at press
    Vector2 gizmoCurDoc_; ///< document-space mouse of the latest frame
    bool dragging_ = false;

    IntVector2 previewSize_{1024, 768};
    bool dirty_ = false;
    ea::string resourcePath_;
    char pathInputBuf_[512]{};

    SharedPtr<UIViewHierarchy> hierarchySource_;
    SharedPtr<UIViewInspector> inspectorSource_;
};

/// HierarchyBrowserSource: walks the editor model of UIViewTab's document.
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

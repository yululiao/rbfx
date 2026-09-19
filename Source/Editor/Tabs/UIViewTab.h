//
// Copyright (c) 2017-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#pragma once

#include "EditorTab.h"
#include "Shared/HierarchyBrowserSource.h"
#include "Shared/InspectorSource.h"

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
/// The tab owns a private offscreen RmlUI context that renders the loaded
/// document into a dynamic Texture2D; the preview texture is drawn in the
/// tab's content area. Selection state (currently-highlighted Rml::Element)
/// lives here; the Hierarchy and Inspector panels that plug into the
/// shared HierarchyBrowserTab / InspectorTab read through it.
///
/// Deliberately does not use the RmlWorldCanvas Drawable: the editor only
/// needs a 2D preview and using RmlUI + Texture2D directly keeps this tab
/// independent of the Octree / Scene plumbing. RmlWorldCanvas remains the
/// runtime-facing component for world-space UI in the game.
class UIViewTab : public EditorTab
{
    URHO3D_OBJECT(UIViewTab, EditorTab)

public:
    explicit UIViewTab(Context* context);
    ~UIViewTab() override;

    /// Load (or reload) the .rml document from a resource path.
    void LoadDocument(const ea::string& path);
    /// Open the given resource path in the editor (loads if a document is not already there).
    void OpenResource(const ea::string& path);
    /// Serialize the current DOM back to its source file. Returns true on success.
    bool SaveDocument();

    /// Currently selected Element, may be null or dangling after hot-reload.
    Rml::Element* GetSelected() const { return selected_; }
    void SetSelected(Rml::Element* e) { selected_ = e; }

    /// The document being edited, or null.
    Rml::ElementDocument* GetDocument() const { return document_; }
    /// The resource path of the loaded document (may be empty).
    const ea::string& GetResourcePath() const { return resourcePath_; }
    /// The offscreen RmlUI instance owning the document. Never null.
    RmlUI* GetPreviewUI() const { return previewUI_; }
    /// Preview texture; ImGui renders this in the tab's viewport.
    Texture2D* GetPreviewTexture() const { return texture_; }

    /// Hierarchy/Inspector data sources hosted by this tab. The Glue binds
    /// the shared HierarchyBrowserTab / InspectorTab to these when the tab
    /// gains focus.
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
    // event (start of the graphics frame), then RenderPreview() samples it.
    void HandleBeginRendering(StringHash eventType, VariantMap& eventData);

    SharedPtr<RmlUI> previewUI_;
    SharedPtr<Texture2D> texture_;
    Rml::ElementDocument* document_ = nullptr;
    Rml::Element* selected_ = nullptr;
    ea::string resourcePath_;
    char pathInputBuf_[512]{};
    IntVector2 previewSize_{1024, 768};
    bool dirty_ = false;

    SharedPtr<UIViewHierarchy> hierarchySource_;
    SharedPtr<UIViewInspector> inspectorSource_;
};

/// HierarchyBrowserSource: walks the DOM of UIViewTab's document.
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

private:
    void RenderElement(Rml::Element* element, unsigned depth);

    WeakPtr<UIViewTab> owner_;
    Rml::Element* contextMenuTarget_ = nullptr;
};

/// InspectorSource: shows attributes and inline style of the selected Element.
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
    void RenderAttributes(Rml::Element* element);
    void RenderInlineStyle(Rml::Element* element);

    WeakPtr<UIViewTab> owner_;
    char attributeKeyBuf_[128]{};
    char attributeValueBuf_[1024]{};
    char styleBuf_[2048]{};
    // Cached inline-style text and the element it was seeded from, so the
    // multiline editor is only refreshed when the selection changes.
    Rml::Element* lastStyleElement_ = nullptr;
    ea::string lastStyleText_;
};

}

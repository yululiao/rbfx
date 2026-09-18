//
// Copyright (c) 2017-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#pragma once

#include "Urho3D/Graphics/CustomGeometry.h"

namespace Rml
{
class ElementDocument;
}

namespace Urho3D
{

class RmlUI;
class Texture2D;

/// Rendering mode for a world-space RmlUi canvas. Currently only WORLD is fully
/// implemented; the other two are placeholders that behave as WORLD.
enum RmlCanvasMode
{
    /// The panel lives in world space, its transform is taken from the owning
    /// Node's world transform. Participates in depth test with the rest of the
    /// scene.
    RML_CANVAS_WORLD = 0,
    /// Reserved. Currently behaves as RML_CANVAS_WORLD.
    RML_CANVAS_SCREEN_CAMERA = 1,
    /// Reserved. Currently behaves as RML_CANVAS_WORLD.
    RML_CANVAS_SCREEN_OVERLAY = 2,
};

/// A Drawable that renders an RmlUi document onto a world-space quad.
///
/// This makes RmlUi a first-class citizen of the scene tree: an RmlWorldCanvas
/// attached to a Node participates in octree culling, ray-picking, depth test,
/// parent-child transforms and prefab/scene serialization exactly like any
/// other Drawable. It plays the same role as Unity's World-Space Canvas or
/// Godot's Node3D+SubViewport combination.
///
/// Internally the component owns:
///  - a private RmlUI context rendering into a dynamic Texture2D;
///  - a unit plane geometry (CustomGeometry base) textured with that RenderTarget.
/// Ray hits return the panel UV via the standard Drawable::ProcessRayQuery
/// pipeline, so callers can further translate a hit into a specific Rml::Element
/// by feeding the UV to RmlUi's input or `Element::GetClosestElementBoxHit`.
class URHO3D_API RmlWorldCanvas : public CustomGeometry
{
    URHO3D_OBJECT(RmlWorldCanvas, CustomGeometry);

public:
    explicit RmlWorldCanvas(Context* context);
    ~RmlWorldCanvas() override;
    static void RegisterObject(Context* context);

    /// Set the RmlUi document resource (.rml). Null/empty unloads the document.
    /// @property
    void SetResource(const ResourceRef& resource);
    /// @property
    const ResourceRef& GetResource() const { return resource_; }

    /// Set logical layout size in UI pixels. RmlUi lays out the document
    /// against this size before rasterization; the texture is allocated with
    /// these dimensions.
    /// @property
    void SetLogicalSize(const IntVector2& size);
    /// @property
    IntVector2 GetLogicalSize() const { return logicalSize_; }

    /// Set physical size of the panel in world units (X and Y in local space;
    /// the panel is a flat quad facing +Z of the owning Node).
    /// @property
    void SetPhysicalSize(const Vector2& size);
    /// @property
    Vector2 GetPhysicalSize() const { return physicalSize_; }

    /// @property
    void SetCanvasMode(RmlCanvasMode mode) { mode_ = mode; }
    /// @property
    RmlCanvasMode GetCanvasMode() const { return mode_; }

    /// Color the offscreen UI surface is cleared to before RmlUi renders into it.
    /// @property
    void SetClearColor(const Color& color) { clearColor_ = color; }
    /// @property
    const Color& GetClearColor() const { return clearColor_; }

    /// Return the offscreen RmlUI instance owning the document. Never null
    /// after construction.
    RmlUI* GetRmlUI() const { return offScreenUI_; }
    /// Return the currently loaded RmlUi document, or null if none is open.
    Rml::ElementDocument* GetDocument() const { return document_; }
    /// Return the offscreen texture the UI is rendered into.
    Texture2D* GetTexture() const { return texture_; }

protected:
    /// Implement Component.
    void OnSetEnabled() override;
    /// Implement Component.
    void OnNodeSet(Node* previousNode, Node* currentNode) override;

private:
    /// Drain pending reload / geometry rebuild requests. Called from setters
    /// and lifecycle hooks. Safe to call when not attached or disabled; the
    /// pending flags simply stay set for the next call.
    void ApplyPending();

    /// Rebuild the plane geometry from physicalSize_.
    void BuildQuad();
    /// (Re)allocate the offscreen render-target texture.
    void EnsureTexture();
    /// Load (or reload) the document referenced by resource_.
    void EnsureDocument();
    /// Release the currently-loaded document without touching resource_.
    void ReleaseDocument();
    /// Ensure the internal unlit-alpha material exists and samples texture_.
    void EnsureMaterial();

    /// Attributes
    /// @{
    ResourceRef resource_;
    IntVector2 logicalSize_{512, 512};
    Vector2 physicalSize_{1.0f, 1.0f};
    RmlCanvasMode mode_ = RML_CANVAS_WORLD;
    Color clearColor_ = Color::TRANSPARENT_BLACK;
    /// @}

    /// Offscreen RmlUi instance owned by this Drawable. Renders into texture_.
    SharedPtr<RmlUI> offScreenUI_;
    /// Dynamic texture the offscreen UI is rendered into; sampled by material_.
    SharedPtr<Texture2D> texture_;
    /// Auto-created material used when the caller does not override batches_[0].material_.
    SharedPtr<Material> autoMaterial_;
    /// Currently open document, or null. Owned by offScreenUI_'s context; may be
    /// invalidated on OnDocumentReloaded (RmlUi hot reload).
    Rml::ElementDocument* document_ = nullptr;

    /// Set when the resource or logical size changed and something needs to be
    /// rebuilt on the next Update().
    bool pendingReload_ = true;
    /// Set when physical size changed and the quad geometry needs regeneration.
    bool pendingGeometryRebuild_ = true;
};

}

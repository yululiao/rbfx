//
// Copyright (c) 2017-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#pragma once

#include "UIViewDocumentModel.h"

#include <Urho3D/Math/Vector2.h>

namespace Urho3D
{

// Pure interaction logic for the UI layout editor. Deliberately free of any
// RmlUi / ImGui dependency: it only moves Vector2 / UiTransform data so that
// hit-testing and gizmo solving stay independently testable and are not tangled
// into the view. The controller (UIViewTab) feeds it DOM-derived boxes and turns
// its results back into live SetProperty / model writes.

/// Which gizmo gesture a handle triggers.
enum class GizmoOp
{
    None,
    Move,
    Resize, ///< mask_ selects the edge(s)
    Rotate,
    Scale, ///< uniform
    ScaleX, ///< non-uniform, horizontal only
};

/// Resize edge mask.
enum GizmoMask
{
    GIZMO_LEFT = 1,
    GIZMO_RIGHT = 2,
    GIZMO_TOP = 4,
    GIZMO_BOTTOM = 8,
};

/// Editor box in document (layout) space: the untransformed border rect plus
/// the node's own emitted transform, applied about the box center.
struct UiBox
{
    Vector2 pos_;
    Vector2 size_;
    UiTransform xform_;

    Vector2 Center() const { return pos_ + size_ * 0.5f; }
};

/// Screen<->document mapping for the scaled preview image. Screen coordinates
/// are carried as Vector2 to keep this module ImGui-free; the caller converts.
struct DocViewport
{
    Vector2 origin_; ///< screen position of the preview image top-left
    float scale_ = 1.0f;

    Vector2 ToDoc(const Vector2& screen) const { return (screen - origin_) * (1.0f / scale_); }
    Vector2 ToScreen(const Vector2& doc) const { return origin_ + doc * scale_; }
};

/// Map a layout-space point through the box transform (center-anchored).
Vector2 ForwardMapPoint(const Vector2& p, const UiBox& box);
/// Inverse of ForwardMapPoint: bring a transformed (screen-projected) point
/// back into the box's untransformed layout frame.
Vector2 InverseMapPoint(const Vector2& p, const UiBox& box);

/// One gizmo handle declaration. The entire gizmo is described declaratively by
/// these, so hit-testing and drawing share a single source (data-driven).
struct GizmoHandle
{
    GizmoOp op_ = GizmoOp::None;
    unsigned mask_ = 0; ///< resize edges (GizmoOp::Resize only)
    /// Anchor in normalized box coords. [0,1] lies on the box; values outside
    /// place off-box handles (rotate above, scale to the lower-right).
    float u_ = 0.0f;
    float v_ = 0.0f;
};

/// 8 resize handles (corners + edge midpoints), in topmost-first hit order.
extern const GizmoHandle kResizeHandles[8];
extern const GizmoHandle kMoveHandle; ///< box center
extern const GizmoHandle kRotateHandle; ///< above the top edge
extern const GizmoHandle kScaleHandle; ///< uniform, lower-right outside
extern const GizmoHandle kScaleXHandle; ///< horizontal-only, right edge outside

/// Resolve the topmost handle under a document-space point, or op None.
/// \a grab is the hit radius in layout units (screen grab px / viewport scale).
GizmoHandle PickGizmoHandle(const UiBox& box, const Vector2& docPoint, float grab);

/// Stateless drag solver: given the box at press, the grabbed handle, and the
/// press / current document-space mouse positions, produce the new box.
UiBox SolveDrag(const UiBox& start, const GizmoHandle& handle,
                const Vector2& startMouse, const Vector2& curMouse);

/// Build the editor box straight from a materialized node's inline style
/// (pure data; layout box == explicit px). False when not materialized.
bool TryGetMaterializedBox(const UiNode& node, UiBox& out);
/// Write an editor box back into a node's inline style (left/top/width/height
/// px + transform), the single place geometry is committed to the model.
void WriteBoxToStyle(UiNode& node, const UiBox& box);

}

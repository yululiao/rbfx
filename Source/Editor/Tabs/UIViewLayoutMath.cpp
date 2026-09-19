//
// Copyright (c) 2017-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#include "UIViewLayoutMath.h"

#include <math.h>

namespace Urho3D
{

namespace
{
constexpr float kDeg2Rad = 3.14159265358979323846f / 180.0f;
constexpr float kRad2Deg = 180.0f / 3.14159265358979323846f;
constexpr float kMinSize = 1.0f;

float ClampScale(float v)
{
    if (v < 0.05f)
        return 0.05f;
    if (v > 20.0f)
        return 20.0f;
    return v;
}

Vector2 HandleAnchor(const UiBox& box, const GizmoHandle& h)
{
    return Vector2{box.pos_.x_ + h.u_ * box.size_.x_, box.pos_.y_ + h.v_ * box.size_.y_};
}

float Dist(const Vector2& a, const Vector2& b)
{
    const float dx = a.x_ - b.x_;
    const float dy = a.y_ - b.y_;
    return sqrtf(dx * dx + dy * dy);
}
} // namespace

// Data-driven gizmo definition. Hit-testing and drawing both iterate these, so
// there is a single source of truth for where each handle lives.
const GizmoHandle kResizeHandles[8] = {
    {GizmoOp::Resize, GIZMO_LEFT | GIZMO_TOP, 0.0f, 0.0f}, // corners first (topmost)
    {GizmoOp::Resize, GIZMO_RIGHT | GIZMO_TOP, 1.0f, 0.0f},
    {GizmoOp::Resize, GIZMO_LEFT | GIZMO_BOTTOM, 0.0f, 1.0f},
    {GizmoOp::Resize, GIZMO_RIGHT | GIZMO_BOTTOM, 1.0f, 1.0f},
    {GizmoOp::Resize, GIZMO_TOP, 0.5f, 0.0f},
    {GizmoOp::Resize, GIZMO_BOTTOM, 0.5f, 1.0f},
    {GizmoOp::Resize, GIZMO_LEFT, 0.0f, 0.5f},
    {GizmoOp::Resize, GIZMO_RIGHT, 1.0f, 0.5f},
};
const GizmoHandle kMoveHandle = {GizmoOp::Move, 0, 0.5f, 0.5f};
const GizmoHandle kRotateHandle = {GizmoOp::Rotate, 0, 0.5f, -0.28f};
const GizmoHandle kScaleHandle = {GizmoOp::Scale, 0, 1.18f, 1.18f};
const GizmoHandle kScaleXHandle = {GizmoOp::ScaleX, 0, 1.18f, 0.5f};

Vector2 ForwardMapPoint(const Vector2& p, const UiBox& box)
{
    const Vector2 c = box.Center();
    float dx = p.x_ - c.x_;
    float dy = p.y_ - c.y_;
    dx *= box.xform_.scaleX_;
    dy *= box.xform_.scaleY_;
    const float rad = box.xform_.rotateDeg_ * kDeg2Rad;
    const float cs = cosf(rad);
    const float sn = sinf(rad);
    return Vector2{c.x_ + (dx * cs - dy * sn), c.y_ + (dx * sn + dy * cs)};
}

Vector2 InverseMapPoint(const Vector2& p, const UiBox& box)
{
    const Vector2 c = box.Center();
    const float dx0 = p.x_ - c.x_;
    const float dy0 = p.y_ - c.y_;
    // Inverse rotation, then inverse scale.
    const float rad = -box.xform_.rotateDeg_ * kDeg2Rad;
    const float cs = cosf(rad);
    const float sn = sinf(rad);
    float dx = dx0 * cs - dy0 * sn;
    float dy = dx0 * sn + dy0 * cs;
    if (fabsf(box.xform_.scaleX_) > 1e-6f)
        dx /= box.xform_.scaleX_;
    if (fabsf(box.xform_.scaleY_) > 1e-6f)
        dy /= box.xform_.scaleY_;
    return Vector2{c.x_ + dx, c.y_ + dy};
}

GizmoHandle PickGizmoHandle(const UiBox& box, const Vector2& docPoint, float grab)
{
    const Vector2 local = InverseMapPoint(docPoint, box);

    // Off-box handles have priority (they never overlap the box body).
    const GizmoHandle* priority[] = {
        &kRotateHandle, &kScaleHandle, &kScaleXHandle,
    };
    for (const GizmoHandle* h : priority)
    {
        if (Dist(local, HandleAnchor(box, *h)) <= grab)
            return *h;
    }
    for (const GizmoHandle& h : kResizeHandles)
    {
        if (Dist(local, HandleAnchor(box, h)) <= grab)
            return h;
    }

    // Fall back to move when the point is inside the layout rect.
    if (local.x_ >= box.pos_.x_ && local.x_ <= box.pos_.x_ + box.size_.x_ &&
        local.y_ >= box.pos_.y_ && local.y_ <= box.pos_.y_ + box.size_.y_)
    {
        return kMoveHandle;
    }

    GizmoHandle none;
    return none; // op_ == None
}

UiBox SolveDrag(const UiBox& start, const GizmoHandle& handle,
                const Vector2& startMouse, const Vector2& curMouse)
{
    UiBox result = start;
    const Vector2 delta = curMouse - startMouse;

    switch (handle.op_)
    {
    case GizmoOp::Move:
        result.pos_ = start.pos_ + delta;
        break;

    case GizmoOp::Resize:
    {
        float left = start.pos_.x_;
        float top = start.pos_.y_;
        float right = start.pos_.x_ + start.size_.x_;
        float bottom = start.pos_.y_ + start.size_.y_;
        if (handle.mask_ & GIZMO_LEFT)
            left += delta.x_;
        if (handle.mask_ & GIZMO_RIGHT)
            right += delta.x_;
        if (handle.mask_ & GIZMO_TOP)
            top += delta.y_;
        if (handle.mask_ & GIZMO_BOTTOM)
            bottom += delta.y_;
        if (right - left < kMinSize)
            right = left + kMinSize;
        if (bottom - top < kMinSize)
            bottom = top + kMinSize;
        result.pos_ = Vector2{left, top};
        result.size_ = Vector2{right - left, bottom - top};
        break;
    }

    case GizmoOp::Rotate:
    {
        const Vector2 c = start.Center();
        const float a0 = atan2f(startMouse.y_ - c.y_, startMouse.x_ - c.x_) * kRad2Deg;
        const float a1 = atan2f(curMouse.y_ - c.y_, curMouse.x_ - c.x_) * kRad2Deg;
        result.xform_.rotateDeg_ = start.xform_.rotateDeg_ + (a1 - a0);
        break;
    }

    case GizmoOp::Scale:
    {
        const Vector2 c = start.Center();
        const float base = Dist(startMouse, c);
        if (base > 1e-3f)
        {
            const float ratio = Dist(curMouse, c) / base;
            result.xform_.scaleX_ = ClampScale(start.xform_.scaleX_ * ratio);
            result.xform_.scaleY_ = ClampScale(start.xform_.scaleY_ * ratio);
        }
        break;
    }

    case GizmoOp::ScaleX:
    {
        const Vector2 c = start.Center();
        const float denom = startMouse.x_ - c.x_;
        if (fabsf(denom) > 1e-3f)
        {
            const float ratio = (curMouse.x_ - c.x_) / denom;
            result.xform_.scaleX_ = ClampScale(start.xform_.scaleX_ * ratio);
        }
        break;
    }

    default:
        break;
    }

    return result;
}

bool TryGetMaterializedBox(const UiNode& node, UiBox& out)
{
    if (!node.IsMaterialized())
        return false;
    float l = 0, t = 0, w = 0, h = 0;
    TryParsePx(node.GetStyle("left"), l);
    TryParsePx(node.GetStyle("top"), t);
    TryParsePx(node.GetStyle("width"), w);
    TryParsePx(node.GetStyle("height"), h);
    out.pos_ = Vector2{l, t};
    out.size_ = Vector2{w, h};
    out.xform_ = ParseUiTransform(node.GetStyle("transform"));
    return true;
}

void WriteBoxToStyle(UiNode& node, const UiBox& box)
{
    node.SetStyle("position", "absolute");
    node.SetStyle("box-sizing", "border-box");
    node.SetStyle("left", FormatPx(box.pos_.x_));
    node.SetStyle("top", FormatPx(box.pos_.y_));
    node.SetStyle("width", FormatPx(box.size_.x_));
    node.SetStyle("height", FormatPx(box.size_.y_));
    const ea::string transform = FormatUiTransform(box.xform_);
    if (transform.empty())
        node.RemoveStyle("transform");
    else
        node.SetStyle("transform", transform);
}

}

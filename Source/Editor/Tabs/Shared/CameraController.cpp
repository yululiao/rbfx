//
// Copyright (c) 2022-2022 the rbfx project.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include "../../Tabs/Shared/CameraController.h"

#include <Urho3D/Graphics/Camera.h>
#include <Urho3D/Graphics/Octree.h>
#include <Urho3D/Math/Plane.h>
#include <Urho3D/Scene/Node.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/SystemUI/SystemUI.h>

#include <IconFontCppHeaders/IconsFontAwesome6.h>

namespace Urho3D
{
namespace
{

const auto Hotkey_MoveForward = EditorHotkey{"EditorCamera.MoveForward"}.Hold(SCANCODE_W).Hold(MOUSEB_RIGHT).MaybeShift();
const auto Hotkey_MoveBackward = EditorHotkey{"EditorCamera.MoveBackward"}.Hold(SCANCODE_S).Hold(MOUSEB_RIGHT).MaybeShift();
const auto Hotkey_MoveLeft = EditorHotkey{"EditorCamera.MoveLeft"}.Hold(SCANCODE_A).Hold(MOUSEB_RIGHT).MaybeShift();
const auto Hotkey_MoveRight = EditorHotkey{"EditorCamera.MoveRight"}.Hold(SCANCODE_D).Hold(MOUSEB_RIGHT).MaybeShift();
const auto Hotkey_MoveUp = EditorHotkey{"EditorCamera.MoveUp"}.Hold(SCANCODE_E).Hold(MOUSEB_RIGHT).MaybeShift();
const auto Hotkey_MoveDown = EditorHotkey{"EditorCamera.MoveDown"}.Hold(SCANCODE_Q).Hold(MOUSEB_RIGHT).MaybeShift();

const auto Hotkey_MoveAccelerate = EditorHotkey{"EditorCamera.MoveAccelerate"}.Hold(SCANCODE_LSHIFT).Hold(MOUSEB_RIGHT).MaybeShift();
const auto Hotkey_LookAround = EditorHotkey{"EditorCamera.LookAround"}.Hold(MOUSEB_RIGHT).MaybeShift().MaybeCtrl().MaybeMouse();

// Unity-style navigation: Alt + LMB orbits around the point under cursor.
// Implemented as dedicated mode, independent from RMB-activated orbit.
const auto Hotkey_LmbOrbit = EditorHotkey{"EditorCamera.LmbOrbit"}.Alt().Hold(MOUSEB_LEFT).MaybeShift().MaybeMouse();
// Legacy orbit: Alt + RMB
const auto Hotkey_OrbitAroundAlt = EditorHotkey{"EditorCamera.OrbitAroundAlt"}.Alt().Hold(MOUSEB_RIGHT).MaybeShift().MaybeMouse();
// Unity-style navigation: MMB pans the view
const auto Hotkey_PanView = EditorHotkey{"EditorCamera.PanView"}.Hold(MOUSEB_MIDDLE).MaybeShift().MaybeAlt().MaybeCtrl().MaybeMouse();

}


void CameraController::Settings::SerializeInBlock(Archive& archive)
{
    SerializeOptionalValue(archive, "MouseSensitivity", mouseSensitivity_, Settings{}.mouseSensitivity_);
    SerializeOptionalValue(archive, "MinSpeed", minSpeed_, Settings{}.minSpeed_);
    SerializeOptionalValue(archive, "MaxSpeed", maxSpeed_, Settings{}.maxSpeed_);
    SerializeOptionalValue(archive, "ScrollSpeed", scrollSpeed_, Settings{}.scrollSpeed_);
    SerializeOptionalValue(archive, "PanSpeed", panSpeed_, Settings{}.panSpeed_);
    SerializeOptionalValue(archive, "Acceleration", acceleration_, Settings{}.acceleration_);
    SerializeOptionalValue(archive, "ShiftFactor", shiftFactor_, Settings{}.shiftFactor_);
    SerializeOptionalValue(archive, "FocusDistance", focusDistance_, Settings{}.focusDistance_);
    SerializeOptionalValue(archive, "Orthographic", orthographic_, Settings{}.orthographic_);
    SerializeOptionalValue(archive, "OrthographicSize", orthoSize_, Settings{}.orthoSize_);
}

void CameraController::Settings::RenderSettings()
{
    ui::DragFloat("Mouse Sensitivity", &mouseSensitivity_, 0.01f, 0.0f, 1.0f, "%.2f");
    ui::DragFloat("Min Speed", &minSpeed_, 0.1f, 0.1f, 100.0f, "%.1f");
    ui::DragFloat("Max Speed", &maxSpeed_, 0.1f, 0.1f, 100.0f, "%.1f");
    ui::DragFloat("Scroll Speed", &scrollSpeed_, 0.1f, 0.1f, 100.0f, "%.1f");
    ui::DragFloat("Pan Speed", &panSpeed_, 0.01f, 0.1f, 10.0f, "%.2f");
    ui::DragFloat("Acceleration", &acceleration_, 0.1f, 0.1f, 100.0f, "%.1f");
    ui::DragFloat("Shift Factor", &shiftFactor_, 0.5f, 1.0f, 10.0f, "%.1f");
    ui::DragFloat("Focus Distance", &focusDistance_, 0.1f, 0.1f, 100.0f, "%.1f");
    ui::Checkbox("Orthographic", &orthographic_);
    ui::InputFloat("Orthographic Size", &orthoSize_, 0.1f, 1.0f, "%.1f");
}

CameraController::PageState::PageState()
{
    LookAt(Vector3{0.0f, 5.0f, -10.0f}, Vector3::ZERO);
}

void CameraController::PageState::LookAt(const BoundingBox& box)
{
    auto center = box.Center();
    auto pos = center + box.Size().Length() * Vector3::ONE;
    LookAt(pos, center);
}

void CameraController::PageState::LookAt(const Vector3& position, const Vector3& target)
{
    lastCameraPosition_ = position;
    lastCameraRotation_.FromLookRotation(target - position, Vector3::UP);
    yaw_ = lastCameraRotation_.YawAngle();
    pitch_ = lastCameraRotation_.PitchAngle();
}

void CameraController::PageState::SerializeInBlock(Archive& archive)
{
    SerializeOptionalValue(archive, "Position", lastCameraPosition_);
    SerializeOptionalValue(archive, "Rotation", lastCameraRotation_);

    if (archive.IsInput())
    {
        yaw_ = lastCameraRotation_.YawAngle();
        pitch_ = lastCameraRotation_.PitchAngle();
    }
}

CameraController::CameraController(Context* context, HotkeyManager* hotkeyManager)
    : Object(context)
    , hotkeyManager_(hotkeyManager)
{
    hotkeyManager->BindPassiveHotkey(Hotkey_MoveForward);
    hotkeyManager->BindPassiveHotkey(Hotkey_MoveBackward);
    hotkeyManager->BindPassiveHotkey(Hotkey_MoveLeft);
    hotkeyManager->BindPassiveHotkey(Hotkey_MoveRight);
    hotkeyManager->BindPassiveHotkey(Hotkey_MoveUp);
    hotkeyManager->BindPassiveHotkey(Hotkey_MoveDown);

    hotkeyManager->BindPassiveHotkey(Hotkey_MoveAccelerate);
    hotkeyManager->BindPassiveHotkey(Hotkey_LookAround);
    hotkeyManager->BindPassiveHotkey(Hotkey_LmbOrbit);
    hotkeyManager->BindPassiveHotkey(Hotkey_OrbitAroundAlt);
    hotkeyManager->BindPassiveHotkey(Hotkey_PanView);
}

Vector2 CameraController::GetMouseMove() const
{
    const auto systemUI = GetSubsystem<SystemUI>();
    return systemUI->GetRelativeMouseMove();
}

Vector3 CameraController::GetMoveDirection() const
{
    const ea::pair<const EditorHotkey&, Vector3> keyMapping[]{
        {Hotkey_MoveForward, Vector3::FORWARD},
        {Hotkey_MoveBackward, Vector3::BACK},
        {Hotkey_MoveLeft, Vector3::LEFT},
        {Hotkey_MoveRight, Vector3::RIGHT},
        {Hotkey_MoveUp, Vector3::UP},
        {Hotkey_MoveDown, Vector3::DOWN},
    };

    Vector3 moveDirection;
    for (const auto& [hotkey, direction] : keyMapping)
    {
        if (hotkeyManager_->IsHotkeyActive(hotkey))
            moveDirection += direction;
    }
    return moveDirection.Normalized();
}

bool CameraController::ProcessInput(Camera* camera, PageState& state, const Settings* settings,
    bool allowPlainLmbPan)
{
    if (!settings)
    {
        auto settingsManager = GetSubsystem<Project>()->GetSettingsManager();
        auto settingsPage = dynamic_cast<SettingsPage*>(settingsManager->FindPage("Editor.Scene:Camera"));
        if (settingsPage)
        {
            settings = &settingsPage->GetValues();
        }
    }
    if (!settings)
    {
        return false;
    }
    camera->SetOrthographic(settings->orthographic_);
    if (settings->orthographic_)
    {
        const float aspectRatio = camera->GetAspectRatio();
        camera->SetOrthoSize(settings->orthoSize_);
        camera->SetAspectRatioInternal(aspectRatio);
    }

    const auto systemUI = GetSubsystem<SystemUI>();
    const ImGuiIO& io = ui::GetIO();

    const bool wasActive = isLooking_ || isOrbiting_ || isLmbOrbiting_ || isPanning_;
    const bool wasPanning = isPanning_;
    const bool hotkeyHovered = wasActive || ui::IsItemHovered();

    // Unity-style orbit with Alt + LMB is a dedicated mode, it suppresses other navigation modes
    isLmbOrbiting_ = hotkeyHovered && hotkeyManager_->IsHotkeyActive(Hotkey_LmbOrbit);
    isLooking_ = !isLmbOrbiting_ && hotkeyHovered && hotkeyManager_->IsHotkeyActive(Hotkey_LookAround);
    isOrbiting_ = !isLmbOrbiting_ && hotkeyHovered && hotkeyManager_->IsHotkeyActive(Hotkey_OrbitAroundAlt);

    // Unity-style navigation: MMB or plain LMB drag pans the view.
    // Plain LMB pan starts only after drag threshold is passed (so click still selects)
    // and only if gizmo is not hovered or manipulated by the mouse.
    const bool altDown = ui::IsKeyDown(KEY_LALT) || ui::IsKeyDown(KEY_RALT);
    const bool lmbPanActive = allowPlainLmbPan && ui::IsMouseDown(MOUSEB_LEFT) && !altDown
        && (wasPanning || (!wasActive && ui::IsMouseDragPastThreshold(MOUSEB_LEFT)));
    isPanning_ = !isLmbOrbiting_ && hotkeyHovered
        && (hotkeyManager_->IsHotkeyActive(Hotkey_PanView) || lmbPanActive);

    // Pick orbit pivot (Unity-style: point under cursor) when orbiting starts
    if ((isLmbOrbiting_ || isOrbiting_) && !state.orbitPosition_)
        InitializeOrbitPivot(*settings, camera, state);
    // Pick panning scale distance when panning starts
    if (isPanning_ && !wasPanning)
    {
        if (const auto pivot = QueryOrbitPivot(camera))
            state.orbitDistance_ = pivot->second;
        else
            state.orbitDistance_ = settings->focusDistance_;
    }

    const bool isActive = isLooking_ || isOrbiting_ || isLmbOrbiting_ || isPanning_;
    if (isActive)
    {
        if (!wasActive)
            systemUI->SetRelativeMouseMove(true, true);
    }
    else if (wasActive)
    {
        systemUI->SetRelativeMouseMove(false, true);
    }

    UpdateState(*settings, camera, state);

    // Real cursor is frozen by relative mouse mode while navigating,
    // track virtual cursor position and render Unity-style grab cursor when panning
    if (isActive || wasActive)
    {
        if (!wasActive)
            virtualCursorPos_ = Vector2{io.MousePos.x, io.MousePos.y};
        else
            virtualCursorPos_ += GetMouseMove();

        if (isPanning_)
            RenderGrabCursor();

        // Restore mouse cursor at the position where navigation ends, not where it started
        systemUI->UpdateRelativeMouseRevertPosition(ImVec2{virtualCursorPos_.x_, virtualCursorPos_.y_});
    }

    return isActive;
}

void CameraController::RenderGrabCursor() const
{
    // Clamp virtual cursor to the viewport so it does not fly over other UI
    const ImVec2 itemMin = ui::GetItemRectMin();
    const ImVec2 itemMax = ui::GetItemRectMax();
    const ImVec2 center{Clamp(virtualCursorPos_.x_, itemMin.x, itemMax.x),
        Clamp(virtualCursorPos_.y_, itemMin.y, itemMax.y)};

    ImDrawList* drawList = ui::GetForegroundDrawList();
    const ImFont* font = ui::GetFont();
    const ImVec2 iconSize = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, ICON_FA_HAND);
    const ImVec2 pos{center.x - iconSize.x * 0.5f, center.y - iconSize.y * 0.5f};
    drawList->AddText(font, font->FontSize, ImVec2{pos.x + 1.0f, pos.y + 1.0f}, IM_COL32(0, 0, 0, 200), ICON_FA_HAND);
    drawList->AddText(font, font->FontSize, pos, IM_COL32(255, 255, 255, 255), ICON_FA_HAND);
}


ea::optional<ea::pair<Vector3, float>> CameraController::QueryOrbitPivot(const Camera* camera) const
{
    const ImGuiIO& io = ui::GetIO();
    const ImVec2 itemMin = ui::GetItemRectMin();
    const ImVec2 itemSize = ui::GetItemRectSize();
    if (itemSize.x < 1.0f || itemSize.y < 1.0f)
        return ea::nullopt;

    const Vector2 relPos{(io.MousePos.x - itemMin.x) / itemSize.x,
        (io.MousePos.y - itemMin.y) / itemSize.y};
    return QueryOrbitPivot(camera, relPos);
}

ea::optional<ea::pair<Vector3, float>> CameraController::QueryOrbitPivot(const Camera* camera, const Vector2& relPos) const
{
    Scene* scene = camera->GetNode()->GetScene();
    if (!scene)
        return ea::nullopt;

    if (relPos.x_ < 0.0f || relPos.x_ > 1.0f || relPos.y_ < 0.0f || relPos.y_ > 1.0f)
        return ea::nullopt;

    const Ray cameraRay = camera->GetScreenRay(relPos.x_, relPos.y_);

    ea::vector<RayQueryResult> results;
    RayOctreeQuery query(results, cameraRay, RAY_TRIANGLE, M_LARGE_VALUE, DRAWABLE_GEOMETRY);
    if (auto octree = scene->GetComponent<Octree>())
        octree->Raycast(query);

    for (const RayQueryResult& result : results)
    {
        if (result.drawable_->GetScene() != nullptr)
            return ea::make_pair(result.position_, result.distance_);
    }
    return ea::nullopt;
}

void CameraController::InitializeOrbitPivot(const Settings& cfg, const Camera* camera, PageState& state) const
{
    Node* node = camera->GetNode();

    // Unity-style: orbit around the point in the center of the view.
    // Pivot is located on the view ray, so the camera already looks at it
    // and the view does not jump when orbit starts.
    if (const auto pivot = QueryOrbitPivot(camera, Vector2{0.5f, 0.5f}))
    {
        state.orbitPosition_ = pivot->first;
        state.orbitDistance_ = pivot->second;
        return;
    }

    // Fallback: intersection of the view ray with the ground plane
    const Ray viewRay{node->GetWorldPosition(), node->GetWorldDirection()};
    const Plane groundPlane{Vector3::UP, Vector3::ZERO};
    const float hitDistance = viewRay.HitDistance(groundPlane);
    if (hitDistance > 0.0f && hitDistance < M_LARGE_VALUE)
    {
        state.orbitPosition_ = viewRay.origin_ + viewRay.direction_ * hitDistance;
        state.orbitDistance_ = hitDistance;
        return;
    }

    // Last resort: point at default distance along view direction
    if (state.orbitDistance_ <= 0.0f)
        state.orbitDistance_ = cfg.focusDistance_;
    state.orbitPosition_ = node->GetPosition() + node->GetRotation() * Vector3{0.0f, 0.0f, state.orbitDistance_};
}


void CameraController::UpdateState(const Settings& cfg, const Camera* camera, PageState& state) const
{
    auto time = GetSubsystem<Time>();

    Node* node = camera->GetNode();

    // Restore camera to previous step if moved
    if (state.lastCameraPosition_ != node->GetPosition())
        node->SetPosition(state.lastCameraPosition_);
    if (state.lastCameraRotation_ != node->GetRotation())
        node->SetRotation(state.lastCameraRotation_);

    const bool isAccelerated = hotkeyManager_->IsHotkeyActive(Hotkey_MoveAccelerate);
    if (isLooking_ && !isOrbiting_ && !isPanning_)
    {
        // Apply mouse movement
        const Vector2 mouseMove = GetMouseMove() * cfg.mouseSensitivity_;
        state.yaw_ = Mod(state.yaw_ + mouseMove.x_, 360.0f);
        state.pitch_ = Clamp(state.pitch_ + mouseMove.y_, -89.0f, 89.0f);

        node->SetRotation(Quaternion{state.pitch_, state.yaw_, 0.0f});

        // Apply camera movement
        const float timeStep = GetSubsystem<Time>()->GetTimeStep();
        const Vector3 moveDirection = GetMoveDirection();
        const float multiplier = isAccelerated ? cfg.shiftFactor_ : 1.0f;
        if (moveDirection == Vector3::ZERO)
            state.currentMoveSpeed_ = cfg.minSpeed_;

        node->Translate(moveDirection * state.currentMoveSpeed_ * multiplier * timeStep);

        // Apply acceleration
        state.currentMoveSpeed_ = ea::min(cfg.maxSpeed_, state.currentMoveSpeed_ + cfg.acceleration_ * timeStep);
    }
    else
    {
        state.currentMoveSpeed_ = cfg.minSpeed_;
    }

    if (isLmbOrbiting_)
    {
        // Unity-style orbit around the view focus (see CameraCtrl::rotate reference):
        // camera is rotated around combined axis and always keeps looking at the pivot
        const Vector3& pivot = *state.orbitPosition_;

        // Zoom with mouse wheel while orbiting, Unity-style (step is proportional to distance)
        const float wheel = ui::GetMouseWheel();
        if (Abs(wheel) > 0.05f)
        {
            const Vector3 offset = node->GetPosition() - pivot;
            node->SetPosition(pivot + offset * Pow(0.85f, wheel));
        }

        const Vector2 mouseMove = GetMouseMove() * cfg.mouseSensitivity_;
        if (mouseMove.x_ != 0.0f || mouseMove.y_ != 0.0f)
        {
            // Single rotation around combined axis:
            // world up axis for horizontal drag, camera right axis for vertical drag
            const Vector3 axis = Vector3::UP * mouseMove.x_ + node->GetRight() * mouseMove.y_;
            const float angle = axis.Length();
            Quaternion rotationDelta;
            rotationDelta.FromAngleAxis(angle, axis);

            node->SetPosition(pivot + rotationDelta * (node->GetPosition() - pivot));
            node->SetRotation(rotationDelta * node->GetRotation());
        }

        // Keep look-around state in sync so RMB look does not jump after orbit ends
        const Quaternion rotation = node->GetRotation();
        state.yaw_ = rotation.YawAngle();
        state.pitch_ = rotation.PitchAngle();
        state.orbitDistance_ = (node->GetPosition() - pivot).Length();
        state.lastCameraRotation_ = rotation;
        state.lastCameraPosition_ = node->GetPosition();
    }
    else if (isOrbiting_)
    {
        // Zoom with mouse wheel while orbiting, Unity-style (step is proportional to distance)
        const float wheel = ui::GetMouseWheel();
        if (Abs(wheel) > 0.05f)
            state.orbitDistance_ = Clamp(state.orbitDistance_ * Pow(0.85f, wheel), 0.05f, 1000000.0f);

        const Vector2 mouseMove = GetMouseMove() * cfg.mouseSensitivity_;
        state.yaw_ = Mod(state.yaw_ + mouseMove.x_, 360.0f);
        state.pitch_ = Clamp(state.pitch_ + mouseMove.y_, -89.0f, 89.0f);

        node->SetRotation(Quaternion{state.pitch_, state.yaw_, 0.0f});
        state.lastCameraRotation_ = node->GetRotation();

        node->SetPosition(*state.orbitPosition_ - node->GetRotation() * Vector3{0.0f, 0.0f, state.orbitDistance_});
        state.lastCameraPosition_ = node->GetPosition();
    }

    if (!isLmbOrbiting_ && !isOrbiting_)
    {
        state.orbitPosition_ = ea::nullopt;
    }

    if (isPanning_)
    {
        const Vector2 mouseMove = GetMouseMove();
        if (mouseMove.x_ != 0.0f || mouseMove.y_ != 0.0f)
        {
            const float panDistance = state.orbitDistance_ > 0.0f ? state.orbitDistance_ : cfg.focusDistance_;
            const float viewSize = camera->IsOrthographic()
                ? camera->GetOrthoSize()
                : 2.0f * panDistance * Tan(camera->GetFov() * M_DEGTORAD * 0.5f);
            const float worldPerPixel = viewSize / Max(ui::GetItemRectSize().y, 1.0f) * cfg.panSpeed_;

            const Quaternion rotation = node->GetRotation();
            node->Translate(rotation * Vector3::RIGHT * (-mouseMove.x_ * worldPerPixel)
                + rotation * Vector3::UP * (mouseMove.y_ * worldPerPixel), TS_WORLD);
        }
    }

    if (!isOrbiting_ && !isLmbOrbiting_ && ui::IsItemHovered() && Abs(ui::GetMouseWheel()) > 0.05f)
    {
        // Zoom with mouse wheel, Unity-style (step is proportional to distance to the point under cursor)
        if (const auto pivot = QueryOrbitPivot(camera))
            state.orbitDistance_ = pivot->second;
        else if (state.orbitDistance_ <= 0.0f)
            state.orbitDistance_ = cfg.focusDistance_;

        state.pendingOffset_ += node->GetWorldDirection()
            * state.orbitDistance_ * cfg.scrollSpeed_ * 0.05f * ui::GetMouseWheel();
    }

    if (state.pendingOffset_.Length() > 0.05f)
    {
        const float factor = InverseExponentialDecay(cfg.focusSpeed_ * time->GetTimeStep());
        node->Translate(state.pendingOffset_ * factor, TS_WORLD);
        state.pendingOffset_ *= 1.0f - factor;
    }

    state.lastCameraRotation_ = node->GetRotation();
    state.lastCameraPosition_ = node->GetPosition();
}

} // namespace Urho3D

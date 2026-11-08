//
// Copyright (c) 2022-2024 the rbfx project.
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
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//

#include "TimelineCurveEditor.h"

#include "TimelineCurveUtils.h"
#include "TimelineEditorCommon.h"

#include <Urho3D/Timeline/TimelinePropertyTrack.h>

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>

#include <cfloat>

namespace Urho3D
{

namespace
{

const char* componentNames[] = { "X", "Y", "Z", "W" };

// Distance in pixels under which a key or a tangent handle can be grabbed.
constexpr float GrabRadiusPx = 6.0f;
// Horizontal length of a tangent handle in pixels: tangents are dValue/dTime, so the handle is
// placed with the same mapping as the curve.
constexpr float HandleWidthPx = 50.0f;
// Two key row times closer than this are the same row. The times are stored as floats and a batch
// move of a row can leave a rounding difference behind.
constexpr float RowTimeEps = 0.0001f;

// Tangent modes of the context menu. The names follow Unity's curve editor, the behaviour follows
// the interpolation model of the engine: a scalar tangent per key and per key interpolation.
const char* tangentModeNames[] = { "Auto", "Linear", "Constant", "Free", "Uniform" };
enum TangentMode
{
    /// Smooth: central difference of the neighbours, the same handle on both sides.
    TangentAuto,
    /// Straight segment, the tangents describe that segment so a later switch keeps the shape.
    TangentLinear,
    /// Flat hold to the next key.
    TangentConstant,
    /// The two handles stay independent, nothing is recomputed.
    TangentFree,
    /// The stronger handle is mirrored onto the other side.
    TangentUniform,
    TangentCount,
};

bool SameRowTime(float a, float b)
{
    return Abs(a - b) < RowTimeEps;
}

/// Hue of a curve component in the overlaid display mode, the way Unity tints X, Y, Z and W.
ImU32 ComponentColor(int component)
{
    static const ImU32 palette[] = {
        IM_COL32(235, 90, 90, 255),
        IM_COL32(110, 220, 110, 255),
        IM_COL32(90, 150, 255, 255),
        IM_COL32(230, 230, 230, 255),
    };
    return palette[Clamp(component, 0, 3)];
}

/// Whether any curve of the clip has a key at the given clip-local time. The comparison itself
/// belongs to the shared curve utils, because the sequencer inserts rows through it as well.
bool HasKeyRowAt(const TimelinePropertyClip& clip, float time)
{
    return HasPropertyKeyRowAt(clip, time);
}

/// Slope of the straight segment between two keys of the same curve, used by the tangent modes and
/// by the Continue extrapolation.
float KeySegmentSlope(const ea::vector<TimelinePropertyKey>& keys, unsigned from, unsigned to, int component)
{
    const float dt = keys[to].time_ - keys[from].time_;
    if (SameRowTime(dt, 0.0f))
        return 0.0f;
    return (GetCurveComponentValue(keys[to].value_, component) - GetCurveComponentValue(keys[from].value_, component)) / dt;
}

/// Index of the key of the curve at the given row time, or M_MAX_UNSIGNED.
unsigned FindRowIndex(const TimelinePropertyCurve& curve, float time)
{
    for (unsigned i = 0; i < curve.keys_.size(); ++i)
    {
        if (curve.keys_[i].time_ > time + RowTimeEps)
            break;
        if (curve.keys_[i].time_ >= time - RowTimeEps)
            return i;
    }
    return M_MAX_UNSIGNED;
}

/// Sample a curve outside the range of its keys the way the extrapolation mode of the clip does:
/// Hold keeps the edge value, Loop and PingPong repeat the waveform, Continue extends the tangent
/// of the edge key. The clip range is [0, duration], so times outside it are only reached when the
/// canvas is zoomed or panned past the edge.
float EvaluateExtrapolated(const TimelinePropertyCurve& curve, const TimelinePropertyClip& clip, float time,
    int component)
{
    const float duration = Max(clip.duration_, RowTimeEps);
    switch (clip.extrapolation_)
    {
    case TimelineExtrapolation::Loop:
    {
        float wrapped = fmodf(time, duration);
        if (wrapped < 0.0f)
            wrapped += duration;
        return EvaluateCurveComponent(curve, wrapped, component);
    }

    case TimelineExtrapolation::PingPong:
    {
        const float cycle = duration * 2.0f;
        float wrapped = fmodf(time, cycle);
        if (wrapped < 0.0f)
            wrapped += cycle;
        return EvaluateCurveComponent(curve, wrapped <= duration ? wrapped : cycle - wrapped, component);
    }

    case TimelineExtrapolation::Continue:
    {
        const auto& keys = curve.keys_;
        if (keys.empty())
            return 0.0f;
        if (time < keys.front().time_)
        {
            const float slope = keys.front().interpolation_ == TimelineInterpolation::Bezier
                && Abs(keys.front().outTangent_) > M_EPSILON
                ? keys.front().outTangent_ : KeySegmentSlope(keys, 0, keys.size() > 1 ? 1 : 0, component);
            return GetCurveComponentValue(keys.front().value_, component) + slope * (time - keys.front().time_);
        }
        if (time > keys.back().time_)
        {
            const unsigned last = static_cast<unsigned>(keys.size()) - 1;
            const float slope = keys.back().interpolation_ == TimelineInterpolation::Bezier
                && Abs(keys.back().outTangent_) > M_EPSILON
                ? keys.back().outTangent_ : KeySegmentSlope(keys, last > 0 ? last - 1 : last, last, component);
            return GetCurveComponentValue(keys.back().value_, component) + slope * (time - keys.back().time_);
        }
        return EvaluateCurveComponent(curve, time, component);
    }

    case TimelineExtrapolation::None:
    case TimelineExtrapolation::Hold:
    default:
        return EvaluateCurveComponent(curve, Clamp(time, 0.0f, duration), component);
    }
}

/// Whether the curve is sampled at the given clip-local time at all: only the None mode stops at
/// the edges of the clip, every other mode reaches past them.
bool IsExtrapolatedRange(const TimelinePropertyClip& clip, float time)
{
    if (clip.extrapolation_ == TimelineExtrapolation::None)
        return time >= -RowTimeEps && time <= clip.duration_ + RowTimeEps;
    return true;
}

/// Insert the copy of a key, replacing the key that already sits at the same time. A paste must
/// never reorder or duplicate the keys of a curve.
void InsertOrReplaceKey(TimelinePropertyCurve& curve, const TimelinePropertyKey& key)
{
    unsigned at = 0;
    while (at < curve.keys_.size() && curve.keys_[at].time_ < key.time_ - RowTimeEps)
        ++at;
    if (at < curve.keys_.size() && curve.keys_[at].time_ < key.time_ + RowTimeEps)
        curve.keys_[at] = key;
    else
        curve.keys_.insert(curve.keys_.begin() + static_cast<long>(at), key);
}

}

void TimelineCurveEditor::Open(bool focus)
{
    open_ = true;
    focusPending_ = focus;
}

void TimelineCurveEditor::Close()
{
    open_ = false;
    dragMode_ = DragMode::None;
    timePanning_ = false;
}

void TimelineCurveEditor::Render(TimelinePropertyClip& propertyClip, TimelineEditorHost& host)
{
    if (propertyClip.curves_.empty())
    {
        Close();
        return;
    }

    ui::SetNextWindowSize(ImVec2(640.0f, 400.0f), ImGuiCond_FirstUseEver);
    // Opening the window or retargeting it to another clip fits both axes to the new clip.
    if (focusPending_)
    {
        ui::SetNextWindowFocus();
        focusPending_ = false;
        autoFit_ = true;
        timeAutoFit_ = true;
        componentIndex_ = 0;
        selectedRows_.clear();
        clipboard_.clear();
    }

    bool open = open_;
    if (!ui::Begin("Timeline Curve Editor", &open))
    {
        // Collapsed or fully clipped: keep the window alive but skip the content.
        ui::End();
        open_ = open;
        return;
    }

    // Keep the canvas selection and the anchor the rest of the editor uses in step: undo, a delete
    // in the sequencer or a click in the properties panel all change one of the two.
    ValidateRowSelection(propertyClip);
    const float anchor = host.GetSelectedKeyTime();
    if (anchor >= 0.0f && !IsRowSelected(anchor) && HasKeyRowAt(propertyClip, anchor))
    {
        ClearRowSelection();
        SelectRow(anchor, false);
    }
    else if (!selectedRows_.empty() && !IsRowSelected(anchor))
        host.SetSelectedKeyTime(selectedRows_.front());
    else if (selectedRows_.empty() && anchor >= 0.0f)
        host.SetSelectedKeyTime(-1.0f);

    RenderLegend(propertyClip);
    RenderControls(propertyClip.curves_[selectedCurveIndex_]);
    RenderKeyValues(propertyClip, host);

    BeginCanvas(propertyClip);
    RenderCanvas(propertyClip, host);
    EndCanvas();

    RenderContextMenu(propertyClip, host);
    ProcessShortcuts(propertyClip, host);

    ui::End();
    open_ = open;
}

void TimelineCurveEditor::RenderLegend(TimelinePropertyClip& propertyClip)
{
    // Buttons tinted with the canvas color of the curve pick the curve to edit.
    if (selectedCurveIndex_ >= propertyClip.curves_.size())
        selectedCurveIndex_ = 0u;
    for (unsigned ci = 0; ci < propertyClip.curves_.size(); ++ci)
    {
        const auto& curve = propertyClip.curves_[ci];
        ui::PushID(static_cast<int>(ci));
        ui::PushStyleColor(ImGuiCol_Button,
            selectedCurveIndex_ == ci ? GetCurveCanvasColor(ci) : WithAlpha(GetCurveCanvasColor(ci), 70));
        if (ui::Button(curve.name_.c_str()))
            selectedCurveIndex_ = ci;
        ui::PopStyleColor();
        if (ui::IsItemHovered())
            ui::SetTooltip("%s (%s)", curve.name_.c_str(), Variant::GetTypeName(curve.valueType_).c_str());
        ui::PopID();
        ui::SameLine();
    }
    ui::NewLine();
}

void TimelineCurveEditor::RenderControls(const TimelinePropertyCurve& selectedCurve)
{
    const int numComponents = GetCurveComponentCount(selectedCurve.valueType_);
    if (componentIndex_ >= Max(numComponents, 1))
        componentIndex_ = 0;

    if (numComponents > 1)
    {
        // Unity shows the channels of a vector either one at a time or all over each other; the
        // overlaid view is what makes a rotation track readable at a glance.
        ui::SetNextItemWidth(110.0f);
        if (ui::Combo("Display", &displayMode_, "Single component\0All components\0", 2))
            autoFit_ = true;
        ui::SameLine();
        ui::SetNextItemWidth(90.0f);
        ui::Combo("Component", &componentIndex_, componentNames, numComponents);
        ui::SameLine();
    }
    else
        displayMode_ = DisplaySingle;

    if (ui::Button("Fit V"))
        autoFit_ = true;
    ui::SameLine();
    if (ui::Button("Fit H"))
        timeAutoFit_ = true;
    ui::SameLine();
    ui::Text("%.2f .. %.2f s", timeMin_, timeMax_);
    ui::SameLine();
    ui::Text("%.2f .. %.2f", viewMin_, viewMax_);
    ui::SameLine();
    ui::TextDisabled("selected rows: %u", static_cast<unsigned>(selectedRows_.size()));

    if (ui::IsItemHovered())
        ui::SetTooltip("Drag on empty space selects key rows.\n"
            "Ctrl+click toggles a row, Ctrl+drag moves without snapping.\n"
            "F fits, +/- zooms, Delete removes the selection, Ctrl+C/V copies rows.");
}

void TimelineCurveEditor::RenderKeyValues(TimelinePropertyClip& propertyClip, TimelineEditorHost& host)
{
    if (selectedRows_.empty())
        return;

    // The fields describe a single row. Several selected rows cannot be edited as one set of
    // numbers, so the anchor of the selection is what is shown and what is named above the lines.
    const float anchor = host.GetSelectedKeyTime();
    const float row = anchor >= 0.0f && IsRowSelected(anchor) ? anchor : selectedRows_.front();
    if (selectedRows_.size() > 1)
        ui::TextDisabled("Values at %s (the anchor of the selection)", FormatTime(row).c_str());
    else
        ui::TextDisabled("Values at %s", FormatTime(row).c_str());

    EditKeyRowValues(propertyClip, row, host);
}

void TimelineCurveEditor::BeginCanvas(TimelinePropertyClip& propertyClip)
{
    ImGuiIO& io = ui::GetIO();

    const auto& selectedCurve = propertyClip.curves_[selectedCurveIndex_];
    const int numComponents = GetCurveComponentCount(selectedCurve.valueType_);

    // The canvas fills the rest of the window. The child does not consume the mouse wheel, so the
    // wheel zooms the vertical range instead of scrolling the window.
    ui::BeginChild("##curveCanvas", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    canvasMin_ = ui::GetCursorScreenPos();
    const ImVec2 canvasSize = ui::GetContentRegionAvail();
    canvasMax_ = ImVec2(canvasMin_.x + canvasSize.x, canvasMin_.y + canvasSize.y);
    canvasHovered_ = ui::IsWindowHovered();

    // The canvas keeps its own horizontal range in clip-local time, independent of the sequencer.
    if (timeAutoFit_)
    {
        timeMin_ = 0.0f;
        timeMax_ = Max(propertyClip.duration_, 0.05f);
    }
    pxPerSecond_ = Max((canvasMax_.x - canvasMin_.x) / Max(timeMax_ - timeMin_, 0.0001f), 0.0001f);

    // Auto-fit: the vertical range follows the keys of the selected curve component, or of every
    // component when they are drawn over each other.
    if (autoFit_ && numComponents > 0)
    {
        float lo = FLT_MAX;
        float hi = -FLT_MAX;
        const int firstComponent = displayMode_ == DisplayOverlay ? 0 : componentIndex_;
        const int lastComponent = displayMode_ == DisplayOverlay ? numComponents - 1 : componentIndex_;
        for (const TimelinePropertyKey& key : selectedCurve.keys_)
        {
            for (int c = firstComponent; c <= lastComponent; ++c)
            {
                const float value = GetCurveComponentValue(key.value_, c);
                lo = Min(lo, value);
                hi = Max(hi, value);
            }
        }
        if (lo > hi)
        {
            lo = -1.0f;
            hi = 1.0f;
        }
        const float pad = Max((hi - lo) * 0.1f, 0.001f);
        viewMin_ = lo - pad;
        viewMax_ = hi + pad;
    }

    if (canvasHovered_ && io.MouseWheel != 0.0f)
    {
        const float factor = io.MouseWheel > 0.0f ? 1.25f : 0.8f;
        if (io.KeyCtrl)
        {
            // Ctrl + wheel zooms the horizontal range around the mouse position.
            const float timeUnderMouse = XToTime(io.MousePos.x);
            timeAutoFit_ = false;
            timeMin_ = timeUnderMouse - (timeUnderMouse - timeMin_) / factor;
            timeMax_ = timeUnderMouse + (timeMax_ - timeUnderMouse) / factor;
        }
        else
        {
            const float valueUnderMouse = YToValue(io.MousePos.y);
            autoFit_ = false;
            viewMin_ = valueUnderMouse - (valueUnderMouse - viewMin_) / factor;
            viewMax_ = valueUnderMouse + (viewMax_ - valueUnderMouse) / factor;
        }
    }

    // Middle mouse button pans the horizontal range.
    if (canvasHovered_ && ui::IsMouseClicked(ImGuiMouseButton_Middle))
        timePanning_ = true;
    if (timePanning_)
    {
        if (!io.MouseDown[ImGuiMouseButton_Middle])
            timePanning_ = false;
        else
        {
            const float delta = -io.MouseDelta.x / pxPerSecond_;
            timeAutoFit_ = false;
            timeMin_ += delta;
            timeMax_ += delta;
        }
    }
}

void TimelineCurveEditor::EndCanvas()
{
    ui::EndChild();
}

float TimelineCurveEditor::TimeToX(float localTime) const
{
    const float range = Max(timeMax_ - timeMin_, 0.0001f);
    return canvasMin_.x + (localTime - timeMin_) / range * (canvasMax_.x - canvasMin_.x);
}

float TimelineCurveEditor::XToTime(float x) const
{
    const float range = Max(timeMax_ - timeMin_, 0.0001f);
    return timeMin_ + (x - canvasMin_.x) / (canvasMax_.x - canvasMin_.x) * range;
}

float TimelineCurveEditor::ValueToY(float value) const
{
    const float range = Max(viewMax_ - viewMin_, 0.0001f);
    return canvasMax_.y - (value - viewMin_) / range * (canvasMax_.y - canvasMin_.y);
}

float TimelineCurveEditor::YToValue(float y) const
{
    const float range = Max(viewMax_ - viewMin_, 0.0001f);
    return viewMin_ + (canvasMax_.y - y) / (canvasMax_.y - canvasMin_.y) * range;
}

void TimelineCurveEditor::RenderCanvas(TimelinePropertyClip& propertyClip, TimelineEditorHost& host)
{
    ImGuiIO& io = ui::GetIO();
    ImDrawList* drawList = ui::GetWindowDrawList();
    auto& selectedCurve = propertyClip.curves_[selectedCurveIndex_];
    const int numComponents = GetCurveComponentCount(selectedCurve.valueType_);
    float selectedKeyTime = host.GetSelectedKeyTime();

    // Background and grid. Time is clip-local, so the clip always starts at 0.
    drawList->AddRectFilled(canvasMin_, canvasMax_, IM_COL32(40, 40, 40, 255));

    const float minStepPx = 70.0f;
    static const float timeSteps[] = { 0.1f, 0.2f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 30.0f, 60.0f, 120.0f, 300.0f };
    float timeStep = timeSteps[sizeof(timeSteps) / sizeof(timeSteps[0]) - 1];
    for (float step : timeSteps)
    {
        if (step * pxPerSecond_ >= minStepPx)
        {
            timeStep = step;
            break;
        }
    }
    for (float time = floorf(timeMin_ / timeStep) * timeStep;; time += timeStep)
    {
        const float x = TimeToX(time);
        if (x > canvasMax_.x)
            break;
        if (x < canvasMin_.x)
            continue;
        drawList->AddLine(ImVec2(x, canvasMin_.y), ImVec2(x, canvasMax_.y), ColorTickMinor);
        drawList->AddText(ImVec2(x + 2.0f, canvasMin_.y + 2.0f), ColorTickLabel, FormatTime(time).c_str());
    }

    for (int i = 0; i <= 4; ++i)
    {
        const float value = Lerp(viewMin_, viewMax_, static_cast<float>(i) / 4.0f);
        const float y = ValueToY(value);
        drawList->AddLine(ImVec2(canvasMin_.x, y), ImVec2(canvasMax_.x, y), i == 2 ? ColorTick : ColorTickMinor);
        const ea::string label = Format("{:.2f}", value);
        const float labelY = i == 4 ? y + 2.0f : y - ui::GetTextLineHeight();
        drawList->AddText(ImVec2(canvasMin_.x + 2.0f, labelY), ColorTickLabel, label.c_str());
    }

    // Cursor line when the scrub position is inside the clip.
    const float cursorLocalTime = host.GetCursorTime() - propertyClip.start_;
    if (cursorLocalTime >= 0.0f && cursorLocalTime <= propertyClip.duration_)
    {
        const float x = TimeToX(cursorLocalTime);
        drawList->AddLine(ImVec2(x, canvasMin_.y), ImVec2(x, canvasMax_.y), ColorCursor, 1.5f);
    }

    if (numComponents <= 0)
        return;

    // The selected key row and its Bezier tangent handles. The selection is resolved again after
    // the interactions below, because adding or moving keys can invalidate the cached pointers.
    const float handleDt = HandleWidthPx / pxPerSecond_;
    struct Selection
    {
        TimelinePropertyKey* key_{};
        int index_{ -1 };
        bool hasOut_{};
        bool hasIn_{};
        ImVec2 outPos_{};
        ImVec2 inPos_{};
    };
    const auto resolveSelection = [&](float keyTime)
    {
        Selection selection;
        for (unsigned i = 0; i < selectedCurve.keys_.size(); ++i)
        {
            if (SameRowTime(selectedCurve.keys_[i].time_, keyTime))
            {
                selection.index_ = static_cast<int>(i);
                selection.key_ = &selectedCurve.keys_[i];
                break;
            }
        }
        if (!selection.key_)
            return selection;

        // The out handle reaches right into the next segment, the in handle left into the previous
        // one. Tangents are dValue/dTime, so both use the same screen mapping.
        selection.hasOut_ = selection.key_->interpolation_ == TimelineInterpolation::Bezier
            && selection.index_ + 1 < static_cast<int>(selectedCurve.keys_.size());
        selection.hasIn_ = selection.index_ > 0
            && selectedCurve.keys_[selection.index_ - 1].interpolation_ == TimelineInterpolation::Bezier;
        const float keyValue = GetCurveComponentValue(selection.key_->value_, componentIndex_);
        if (selection.hasOut_)
        {
            selection.outPos_ = ImVec2(TimeToX(selection.key_->time_ + handleDt),
                ValueToY(keyValue + selection.key_->outTangent_ * handleDt));
        }
        if (selection.hasIn_)
        {
            selection.inPos_ = ImVec2(TimeToX(selection.key_->time_ - handleDt),
                ValueToY(keyValue + selection.key_->inTangent_ * -handleDt));
        }
        return selection;
    };
    Selection selection = resolveSelection(selectedKeyTime);

    const auto nearPos = [&](const ImVec2& pos)
    {
        return Abs(io.MousePos.x - pos.x) <= GrabRadiusPx && Abs(io.MousePos.y - pos.y) <= GrabRadiusPx;
    };
    const auto keyScreenPos = [&](const TimelinePropertyKey& key, int component)
    {
        return ImVec2(TimeToX(key.time_), ValueToY(GetCurveComponentValue(key.value_, component)));
    };
    // Which components are drawn and therefore clickable: the active one only, unless the channels
    // of the curve are shown over each other.
    const int firstComponent = displayMode_ == DisplayOverlay ? 0 : componentIndex_;
    const int lastComponent = displayMode_ == DisplayOverlay ? numComponents - 1 : componentIndex_;
    const float mouseLocalTime = XToTime(io.MousePos.x);

    // Right-click: a hit key is selected first, otherwise the menu targets the empty time.
    if (canvasHovered_ && ui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        popupTime_ = Clamp(mouseLocalTime, 0.0f, propertyClip.duration_);
        popupOnKey_ = false;
        for (const TimelinePropertyKey& key : selectedCurve.keys_)
        {
            for (int c = firstComponent; c <= lastComponent; ++c)
            {
                if (nearPos(keyScreenPos(key, c)))
                {
                    // A row that is part of a larger selection keeps the selection, the menu then
                    // works on the whole of it. Otherwise the clicked row becomes the only one.
                    if (!IsRowSelected(key.time_))
                    {
                        ClearRowSelection();
                        SelectRow(key.time_, false);
                    }
                    selectedKeyTime = key.time_;
                    host.SetSelectedKeyTime(selectedKeyTime);
                    popupTime_ = key.time_;
                    popupOnKey_ = true;
                    break;
                }
            }
            if (popupOnKey_)
                break;
        }
        ui::OpenPopup("CurveEditorContextMenu");
    }

    if (canvasHovered_ && ui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        if (ui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            // Double-click adds a whole key row, unless a key already exists at this time.
            const float addTime = Clamp(SnapTime(mouseLocalTime, !io.KeyCtrl), 0.0f, propertyClip.duration_);
            if (!HasKeyRowAt(propertyClip, addTime))
            {
                host.BeginModify();
                AddPropertyKeyAt(propertyClip, addTime);
                ClearRowSelection();
                SelectRow(addTime, false);
                host.SetSelectedKeyTime(addTime);
            }
        }
        else if (selection.hasOut_ && nearPos(selection.outPos_))
        {
            host.BeginModify();
            autoFit_ = false;
            dragMode_ = DragMode::TangentOut;
            dragCurve_ = selectedCurveIndex_;
            dragKeyTime_ = selectedKeyTime;
        }
        else if (selection.hasIn_ && nearPos(selection.inPos_))
        {
            host.BeginModify();
            autoFit_ = false;
            dragMode_ = DragMode::TangentIn;
            dragCurve_ = selectedCurveIndex_;
            dragKeyTime_ = selectedKeyTime;
        }
        else
        {
            // A key starts a drag of the whole selection; empty space starts a rubber band.
            float hitTime = -1.0f;
            for (const TimelinePropertyKey& key : selectedCurve.keys_)
            {
                for (int c = firstComponent; c <= lastComponent; ++c)
                {
                    if (nearPos(keyScreenPos(key, c)))
                    {
                        hitTime = key.time_;
                        break;
                    }
                }
                if (hitTime >= 0.0f)
                    break;
            }
            if (hitTime >= 0.0f)
            {
                SelectRow(hitTime, io.KeyCtrl);
                if (!io.KeyCtrl)
                    host.SetSelectedKeyTime(hitTime);
                host.BeginModify();
                autoFit_ = false;
                dragMode_ = DragMode::KeyMove;
                dragCurve_ = selectedCurveIndex_;
                dragKeyTime_ = hitTime;
                dragOriginTime_ = hitTime;
                dragAppliedOffset_ = 0.0f;
                dragLastValue_ = YToValue(io.MousePos.y);
                // The slot of the grabbed row in the selection: the batch move rewrites the list in
                // place, so the slot keeps identifying the row the cursor is holding.
                dragRowIndex_ = M_MAX_UNSIGNED;
                for (unsigned i = 0; i < selectedRows_.size(); ++i)
                {
                    if (SameRowTime(selectedRows_[i], hitTime))
                    {
                        dragRowIndex_ = i;
                        break;
                    }
                }
            }
            else
            {
                if (!io.KeyCtrl)
                {
                    ClearRowSelection();
                    host.SetSelectedKeyTime(-1.0f);
                }
                dragMode_ = DragMode::SelectRect;
                marqueeMin_ = io.MousePos;
                marqueeMax_ = io.MousePos;
            }
        }
    }

    // Update the active drag. Horizontal movement shifts every selected row by the same offset, the
    // vertical movement changes the values of the dragged curve by the step of the cursor.
    if (dragMode_ != DragMode::None)
    {
        if (!io.MouseDown[ImGuiMouseButton_Left])
        {
            if (dragMode_ == DragMode::SelectRect)
                SelectRectRows(propertyClip, marqueeMin_, marqueeMax_, io.KeyCtrl);
            dragMode_ = DragMode::None;
        }
        else if (dragMode_ == DragMode::SelectRect)
            marqueeMax_ = io.MousePos;
        else if (dragCurve_ < propertyClip.curves_.size())
        {
            auto& dragCurve = propertyClip.curves_[dragCurve_];
            if (dragMode_ == DragMode::KeyMove)
            {
                const float localTime = Clamp(SnapTime(mouseLocalTime, !io.KeyCtrl), 0.0f, propertyClip.duration_);
                const float offset = localTime - dragOriginTime_;
                if (MoveSelectedRows(propertyClip, offset - dragAppliedOffset_))
                {
                    dragAppliedOffset_ = offset;
                    // The row the cursor grabbed is the anchor of the selection.
                    if (dragRowIndex_ < selectedRows_.size())
                    {
                        dragKeyTime_ = selectedRows_[dragRowIndex_];
                        host.SetSelectedKeyTime(dragKeyTime_);
                    }
                }
                const float step = YToValue(io.MousePos.y) - dragLastValue_;
                dragLastValue_ = YToValue(io.MousePos.y);
                if (Abs(step) > RowTimeEps)
                {
                    for (const float rowTime : selectedRows_)
                    {
                        const unsigned index = FindRowIndex(dragCurve, rowTime);
                        if (index == M_MAX_UNSIGNED)
                            continue;
                        Variant value = dragCurve.keys_[index].value_;
                        SetCurveComponentValue(value, componentIndex_, GetCurveComponentValue(value, componentIndex_) + step);
                        dragCurve.keys_[index].value_ = value;
                    }
                }
            }
            else if (dragMode_ == DragMode::TangentOut || dragMode_ == DragMode::TangentIn)
            {
                for (TimelinePropertyKey& key : dragCurve.keys_)
                {
                    if (!SameRowTime(key.time_, dragKeyTime_))
                        continue;
                    const float keyX = TimeToX(key.time_);
                    const float dTime = (io.MousePos.x - keyX) / pxPerSecond_;
                    const float safeDTime = Abs(dTime) < 0.01f ? (dTime < 0.0f ? -0.01f : 0.01f) : dTime;
                    const float keyValue = GetCurveComponentValue(key.value_, componentIndex_);
                    const float tangent = (YToValue(io.MousePos.y) - keyValue) / safeDTime;
                    if (dragMode_ == DragMode::TangentOut)
                        key.outTangent_ = tangent;
                    else
                        key.inTangent_ = tangent;
                    break;
                }
            }
        }
    }

    // The interactions above may have moved the selection; read it back before drawing.
    selectedKeyTime = host.GetSelectedKeyTime();
    selection = resolveSelection(selectedKeyTime);

    // Curves: non-selected ones as thin context lines (component 0), the selected one on top in
    // full color, either with its active component or with every component over each other.
    drawList->PushClipRect(canvasMin_, canvasMax_, true);

    // Edges of the clip: the curve only reaches past them when the extrapolation mode of the clip
    // says so, so the range itself is marked even when the canvas is zoomed onto its middle.
    for (int e = 0; e < 2; ++e)
    {
        const float edge = e == 0 ? 0.0f : propertyClip.duration_;
        const float x = TimeToX(edge);
        if (x >= canvasMin_.x && x <= canvasMax_.x)
            drawList->AddLine(ImVec2(x, canvasMin_.y), ImVec2(x, canvasMax_.y), ColorTick, 1.0f);
    }

    const auto drawCurvePolyline = [&](const TimelinePropertyCurve& curve, int component, ImU32 color, float thickness)
    {
        // Sample the whole canvas width. Outside the key range the extrapolation mode of the clip
        // decides what is drawn, and the None mode simply does not reach there.
        ea::vector<ImVec2> points;
        for (float x = canvasMin_.x; x <= canvasMax_.x; x += 2.0f)
        {
            const float localTime = XToTime(x);
            if (!IsExtrapolatedRange(propertyClip, localTime))
                continue;
            points.push_back(ImVec2(x, ValueToY(EvaluateExtrapolated(curve, propertyClip, localTime, component))));
        }
        drawList->AddPolyline(points.data(), static_cast<int>(points.size()), color, 0, thickness);
    };

    for (unsigned ci = 0; ci < propertyClip.curves_.size(); ++ci)
    {
        if (ci == selectedCurveIndex_)
            continue;
        const auto& curve = propertyClip.curves_[ci];
        if (!curve.keys_.empty() && GetCurveComponentCount(curve.valueType_) > 0)
            drawCurvePolyline(curve, 0, WithAlpha(GetCurveCanvasColor(ci), 130), 1.5f);
    }

    if (displayMode_ == DisplayOverlay)
    {
        for (int c = 0; c < numComponents; ++c)
            drawCurvePolyline(selectedCurve, c, ComponentColor(c), c == componentIndex_ ? 2.5f : 1.5f);
    }
    else
        drawCurvePolyline(selectedCurve, componentIndex_, GetCurveCanvasColor(selectedCurveIndex_), 2.5f);

    // Keys of the selected curve. Every selected row is drawn bright, the anchor of the selection
    // gets the outline so that it can still be told apart from the rest of the group.
    for (int c = firstComponent; c <= lastComponent; ++c)
    {
        const ImU32 curveColor = displayMode_ == DisplayOverlay ? ComponentColor(c)
            : GetCurveCanvasColor(selectedCurveIndex_);
        const float radius = c == componentIndex_ ? 4.5f : 3.5f;
        for (const TimelinePropertyKey& key : selectedCurve.keys_)
        {
            const ImVec2 pos = keyScreenPos(key, c);
            if (pos.x < canvasMin_.x || pos.x > canvasMax_.x)
                continue;
            const bool rowSelected = IsRowSelected(key.time_);
            if (rowSelected)
                drawList->AddCircleFilled(pos, radius + 1.5f, WithAlpha(ColorSelectionGlow, 90));
            drawList->AddCircleFilled(pos, radius, rowSelected ? ColorKeySelected : curveColor);
            drawList->AddCircle(pos, radius, IM_COL32(0, 0, 0, 200), 0, 1.5f);
        }
    }

    // Tangent handles of the selected key.
    if (selection.key_)
    {
        const ImVec2 keyPos = keyScreenPos(*selection.key_, componentIndex_);
        if (selection.hasOut_)
        {
            drawList->AddLine(keyPos, selection.outPos_, IM_COL32(255, 255, 255, 160), 1.5f);
            drawList->AddCircleFilled(selection.outPos_, 3.5f, ColorKeySelected);
        }
        if (selection.hasIn_)
        {
            drawList->AddLine(keyPos, selection.inPos_, IM_COL32(255, 255, 255, 160), 1.5f);
            drawList->AddCircleFilled(selection.inPos_, 3.5f, ColorKeySelected);
        }
    }

    // The rubber band of a selection drag, drawn over everything else of the canvas.
    if (dragMode_ == DragMode::SelectRect)
    {
        const ImVec2 rectMin(Min(marqueeMin_.x, marqueeMax_.x), Min(marqueeMin_.y, marqueeMax_.y));
        const ImVec2 rectMax(Max(marqueeMin_.x, marqueeMax_.x), Max(marqueeMin_.y, marqueeMax_.y));
        drawList->AddRectFilled(rectMin, rectMax, ColorSelectionRect);
        drawList->AddRect(rectMin, rectMax, ColorSelectionBorder, 0.0f, 0, 1.0f);
    }
    drawList->PopClipRect();
}

void TimelineCurveEditor::RenderContextMenu(TimelinePropertyClip& propertyClip, TimelineEditorHost& host)
{
    if (!ui::BeginPopup("CurveEditorContextMenu"))
        return;

    if (popupOnKey_)
    {
        // The menu works on the whole selection: a key row is edited as a unit in this editor, and
        // the row under the cursor is part of the selection whenever the selection is larger.
        ea::vector<float> rows = selectedRows_;
        if (rows.empty())
            rows.push_back(popupTime_);

        if (rows.size() > 1)
        {
            const ea::string label = Format("Delete %d Key Rows", static_cast<unsigned>(rows.size()));
            if (ui::Selectable(label.c_str()))
            {
                host.BeginModify();
                for (const float rowTime : rows)
                    RemovePropertyKeysAt(propertyClip, rowTime);
                ClearRowSelection();
                host.SetSelectedKeyTime(-1.0f);
            }
        }
        else if (ui::Selectable("Delete Key"))
        {
            host.BeginModify();
            RemovePropertyKeysAt(propertyClip, popupTime_);
            ClearRowSelection();
            if (host.GetSelectedKeyTime() >= 0.0f && !selectedRows_.empty())
                host.SetSelectedKeyTime(selectedRows_.front());
            else
                host.SetSelectedKeyTime(-1.0f);
        }

        ui::Separator();
        if (ui::BeginMenu("Interpolation"))
        {
            TimelineInterpolation current = TimelineInterpolation::Ease;
            for (const float rowTime : rows)
            {
                for (const auto& curve : propertyClip.curves_)
                {
                    const unsigned index = FindRowIndex(curve, rowTime);
                    if (index != M_MAX_UNSIGNED)
                    {
                        current = curve.keys_[index].interpolation_;
                        break;
                    }
                }
            }
            constexpr int interpCount = static_cast<int>(sizeof(TIMELINE_INTERPOLATION_NAMES) / sizeof(const char*)) - 1;
            for (int i = 0; i < interpCount; ++i)
            {
                const auto interp = static_cast<TimelineInterpolation>(i);
                if (ui::Selectable(TIMELINE_INTERPOLATION_NAMES[i], current == interp))
                {
                    host.BeginModify();
                    for (const float rowTime : rows)
                    {
                        for (auto& curve : propertyClip.curves_)
                        {
                            const unsigned index = FindRowIndex(curve, rowTime);
                            if (index != M_MAX_UNSIGNED)
                                curve.keys_[index].interpolation_ = interp;
                        }
                    }
                }
            }
            ui::EndMenu();
        }
        if (ui::BeginMenu("Tangent Mode"))
        {
            for (int i = 0; i < TangentCount; ++i)
            {
                if (ui::Selectable(tangentModeNames[i]))
                {
                    host.BeginModify();
                    ApplyTangentMode(propertyClip, i);
                }
            }
            if (ui::IsItemHovered())
                ui::SetTooltip("Auto: smooth, symmetric handles from the neighbours.\n"
                    "Linear: straight segment. Constant: flat hold.\n"
                    "Free: keep both handles independent. Uniform: mirror the stronger one.");
            ui::EndMenu();
        }
    }
    else
    {
        const ea::string label = Format("Add Key at {}", FormatTime(popupTime_).c_str());
        if (ui::Selectable(label.c_str()))
        {
            host.BeginModify();
            AddPropertyKeyAt(propertyClip, popupTime_);
            ClearRowSelection();
            SelectRow(popupTime_, false);
            host.SetSelectedKeyTime(popupTime_);
        }
    }
    ui::EndPopup();
}

void TimelineCurveEditor::ProcessShortcuts(TimelinePropertyClip& propertyClip, TimelineEditorHost& host)
{
    if (!ui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        return;

    ImGuiIO& io = ui::GetIO();

    if (ui::IsKeyPressed(ImGuiKey_F))
        autoFit_ = timeAutoFit_ = true;

    // Plus and minus zoom the vertical range around its centre, the way the Unity canvas does.
    float factor = 0.0f;
    if (ui::IsKeyPressed(ImGuiKey_Equal) || ui::IsKeyPressed(ImGuiKey_KeypadAdd))
        factor = 1.25f;
    else if (ui::IsKeyPressed(ImGuiKey_Minus) || ui::IsKeyPressed(ImGuiKey_KeypadSubtract))
        factor = 0.8f;
    if (factor != 0.0f)
    {
        const float center = (viewMin_ + viewMax_) * 0.5f;
        autoFit_ = false;
        viewMin_ = center - (center - viewMin_) / factor;
        viewMax_ = center + (viewMax_ - center) / factor;
    }

    if (ui::IsKeyPressed(ImGuiKey_Delete) && !selectedRows_.empty())
    {
        host.BeginModify();
        for (const float rowTime : selectedRows_)
            RemovePropertyKeysAt(propertyClip, rowTime);
        ClearRowSelection();
        host.SetSelectedKeyTime(-1.0f);
        return;
    }

    if (!io.KeyCtrl)
        return;

    if (ui::IsKeyPressed(ImGuiKey_C))
    {
        // The whole row is copied: every curve contributes its key, so a paste restores the shape
        // of the clip and not just one channel of it.
        clipboard_.clear();
        for (const float rowTime : selectedRows_)
        {
            CopiedRow row;
            row.time_ = rowTime;
            for (const auto& curve : propertyClip.curves_)
            {
                const unsigned index = FindRowIndex(curve, rowTime);
                if (index == M_MAX_UNSIGNED)
                    continue;
                CopiedKey copied;
                copied.curve_ = curve.name_;
                copied.key_ = curve.keys_[index];
                row.keys_.push_back(copied);
            }
            if (!row.keys_.empty())
                clipboard_.push_back(row);
        }
    }
    else if (ui::IsKeyPressed(ImGuiKey_V) && !clipboard_.empty())
    {
        // The rows keep their spacing and the earliest of them goes under the cursor, which is
        // where Unity drops a pasted group. Over the properties panel the anchor is used instead.
        float earliest = clipboard_.front().time_;
        for (const CopiedRow& row : clipboard_)
            earliest = Min(earliest, row.time_);
        float offset = 0.0f;
        if (canvasHovered_)
            offset = Clamp(XToTime(io.MousePos.x), 0.0f, propertyClip.duration_) - earliest;
        else if (!selectedRows_.empty())
            offset = host.GetSelectedKeyTime() - earliest;

        host.BeginModify();
        ClearRowSelection();
        for (const CopiedRow& row : clipboard_)
        {
            const float time = Clamp(row.time_ + offset, 0.0f, propertyClip.duration_);
            for (const CopiedKey& copied : row.keys_)
            {
                for (auto& curve : propertyClip.curves_)
                {
                    if (curve.name_ != copied.curve_)
                        continue;
                    TimelinePropertyKey key = copied.key_;
                    key.time_ = time;
                    InsertOrReplaceKey(curve, key);
                }
            }
            AddRow(time);
        }
        if (!selectedRows_.empty())
            host.SetSelectedKeyTime(selectedRows_.back());
    }
}

bool TimelineCurveEditor::IsRowSelected(float time) const
{
    for (const float row : selectedRows_)
    {
        if (SameRowTime(row, time))
            return true;
    }
    return false;
}

void TimelineCurveEditor::SelectRow(float time, bool toggle)
{
    if (toggle)
    {
        for (auto it = selectedRows_.begin(); it != selectedRows_.end(); ++it)
        {
            if (SameRowTime(*it, time))
            {
                selectedRows_.erase(it);
                return;
            }
        }
    }
    else
        ClearRowSelection();
    AddRow(time);
}

void TimelineCurveEditor::AddRow(float time)
{
    if (IsRowSelected(time))
        return;
    // The list is kept ascending, which is the order a batch move walks it in.
    unsigned at = 0;
    while (at < selectedRows_.size() && selectedRows_[at] < time)
        ++at;
    selectedRows_.insert(selectedRows_.begin() + static_cast<long>(at), time);
}

void TimelineCurveEditor::ClearRowSelection()
{
    selectedRows_.clear();
}

bool TimelineCurveEditor::MoveSelectedRows(TimelinePropertyClip& propertyClip, float offset)
{
    if (selectedRows_.empty() || Abs(offset) < RowTimeEps)
        return false;

    // The selection is walked from the leading edge of the move, so a row never steps over the
    // neighbour that follows it. The slots are rewritten in place, because the running drag keeps
    // the slot of the row it grabbed to know where that row ended up.
    const unsigned count = static_cast<unsigned>(selectedRows_.size());
    ea::vector<unsigned> order(count);
    for (unsigned i = 0; i < count; ++i)
        order[i] = i;
    ea::sort(order.begin(), order.end(), [this](unsigned a, unsigned b) { return selectedRows_[a] < selectedRows_[b]; });

    bool any = false;
    for (unsigned n = 0; n < count; ++n)
    {
        const unsigned i = order[static_cast<size_t>(offset > 0.0f ? count - 1 - n : n)];
        const float from = selectedRows_[i];
        const float to = Clamp(from + offset, 0.0f, propertyClip.duration_);
        if (Abs(to - from) < RowTimeEps)
            continue;
        // A row cannot be dropped on an unselected row: two key rows of the same time would make
        // the clip ambiguous, so the blocked row simply stays where it is.
        if (!IsRowSelected(to) && HasKeyRowAt(propertyClip, to))
            continue;
        MovePropertyKeysAt(propertyClip, from, to);
        selectedRows_[i] = to;
        any = true;
    }
    return any;
}

void TimelineCurveEditor::ValidateRowSelection(TimelinePropertyClip& propertyClip)
{
    // A row that lost its keys simply drops out of the selection: undo of a delete must never
    // leave the canvas pointing at a time that has no content.
    for (auto it = selectedRows_.begin(); it != selectedRows_.end();)
    {
        if (HasKeyRowAt(propertyClip, *it))
            ++it;
        else
            it = selectedRows_.erase(it);
    }
}

void TimelineCurveEditor::SelectRectRows(TimelinePropertyClip& propertyClip, const ImVec2& min, const ImVec2& max,
    bool add)
{
    const ImVec2 rectMin(Min(min.x, max.x), Min(min.y, max.y));
    const ImVec2 rectMax(Max(min.x, max.x), Max(min.y, max.y));
    // A click without any movement keeps the selection the click itself made.
    if (rectMax.x - rectMin.x < 2.0f && rectMax.y - rectMin.y < 2.0f)
        return;
    if (!add)
        ClearRowSelection();

    if (selectedCurveIndex_ >= propertyClip.curves_.size())
        return;
    const auto& curve = propertyClip.curves_[selectedCurveIndex_];
    const int numComponents = GetCurveComponentCount(curve.valueType_);
    const int first = displayMode_ == DisplayOverlay ? 0 : componentIndex_;
    const int last = displayMode_ == DisplayOverlay ? Max(numComponents - 1, 0) : componentIndex_;
    for (const TimelinePropertyKey& key : curve.keys_)
    {
        for (int c = first; c <= last; ++c)
        {
            const ImVec2 pos(TimeToX(key.time_), ValueToY(GetCurveComponentValue(key.value_, c)));
            if (pos.x >= rectMin.x && pos.x <= rectMax.x && pos.y >= rectMin.y && pos.y <= rectMax.y)
            {
                AddRow(key.time_);
                break;
            }
        }
    }
}

void TimelineCurveEditor::ApplyTangentMode(TimelinePropertyClip& propertyClip, int mode)
{
    // The rows the command applies to: the whole selection, or the row of the menu when the
    // selection is empty (it was cleared after the right-click).
    ea::vector<float> rows = selectedRows_;
    if (rows.empty())
        rows.push_back(popupTime_);

    for (const float rowTime : rows)
    {
        for (auto& curve : propertyClip.curves_)
        {
            const unsigned index = FindRowIndex(curve, rowTime);
            if (index == M_MAX_UNSIGNED)
                continue;
            auto& keys = curve.keys_;
            TimelinePropertyKey& key = keys[index];
            const int components = GetCurveComponentCount(curve.valueType_);
            const int component = Clamp(componentIndex_, 0, Max(components - 1, 0));
            switch (mode)
            {
            case TangentAuto:
            {
                // Central difference over the neighbours, one sided at the ends: symmetric handles
                // give the smooth look Unity's Auto mode has.
                const unsigned lo = index > 0 ? index - 1 : index;
                const unsigned hi = index + 1 < keys.size() ? index + 1 : index;
                key.outTangent_ = key.inTangent_ = KeySegmentSlope(keys, lo, hi, component);
                key.interpolation_ = TimelineInterpolation::Bezier;
                break;
            }

            case TangentLinear:
                // The tangents describe the straight segment, so a later switch to Bezier or Auto
                // starts from a shape that already matches the line.
                key.outTangent_ = KeySegmentSlope(keys, index, index + 1 < keys.size() ? index + 1 : index,
                    component);
                key.inTangent_ = KeySegmentSlope(keys, index > 0 ? index - 1 : index, index, component);
                key.interpolation_ = TimelineInterpolation::Linear;
                break;

            case TangentConstant:
                key.outTangent_ = key.inTangent_ = 0.0f;
                key.interpolation_ = TimelineInterpolation::Bezier;
                break;

            case TangentFree:
                // Both handles stay as they are, only the segment starts using them.
                key.interpolation_ = TimelineInterpolation::Bezier;
                break;

            case TangentUniform:
                if (Abs(key.outTangent_) >= Abs(key.inTangent_))
                    key.inTangent_ = key.outTangent_;
                else
                    key.outTangent_ = key.inTangent_;
                key.interpolation_ = TimelineInterpolation::Bezier;
                break;

            default:
                break;
            }
        }
    }
}

}

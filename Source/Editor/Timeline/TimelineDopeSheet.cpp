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

#include "TimelineDopeSheet.h"

#include "TimelineCurveUtils.h"
#include "TimelineEditorCommon.h"

#include <Urho3D/Timeline/TimelineResource.h>

#include <EASTL/algorithm.h>

namespace Urho3D
{

namespace
{

// Physical size of the sheet. Unlike the sequencer the sheet is a compact overview, so its rows are
// not scaled with the display DPI: the same window shows many more rows this way.
constexpr float BaseRowHeight = 20.0f;
constexpr float BaseLabelWidth = 220.0f;
constexpr float BaseRulerHeight = 22.0f;

// Grab distance of a key square or a marker, in pixels.
constexpr float GrabRadius = 6.0f;
// Horizontal extent of a clip block or a key square.
constexpr float BlockHeight = 12.0f;
constexpr float KeySize = 9.0f;
constexpr float MinPixelsPerSecond = 2.0f;
constexpr float MaxPixelsPerSecond = 800.0f;

// Colors the sheet adds to the shared look of the Timeline editor.
const ImU32 ColorFrameLine = IM_COL32(255, 255, 255, 14);
const ImU32 ColorSecondLine = IM_COL32(255, 255, 255, 40);
const ImU32 ColorLabelBg = IM_COL32(52, 48, 48, 255);
const ImU32 ColorLabelBgAlt = IM_COL32(58, 54, 54, 255);
const ImU32 ColorClipBand = IM_COL32(255, 255, 255, 26);
// Squares of a marker track. The track tint of the caption column is the darker marker color, the
// squares themselves are brighter so that they read as editable content.
const ImU32 ColorMarker = IM_COL32(232, 200, 74, 255);

// Key time comparison tolerance: keys of the curves of a clip are aligned in time, the sheet treats
// anything closer than this as the same row.
constexpr float KeyTimeEps = 0.0001f;

bool SameKeyTime(float a, float b)
{
    return Abs(a - b) < KeyTimeEps;
}

/// Insert the copy of a key into the curve, replacing a key that already sits at that time. Used by
/// the paste and the cross-curve move, neither of which may reorder the existing keys.
void InsertKeyCopy(TimelinePropertyCurve& curve, const TimelinePropertyKey& key)
{
    unsigned at = 0;
    while (at < curve.keys_.size() && curve.keys_[at].time_ < key.time_ - KeyTimeEps)
        ++at;
    if (at < curve.keys_.size() && curve.keys_[at].time_ < key.time_ + KeyTimeEps)
        curve.keys_[at] = key;
    else
        curve.keys_.insert(curve.keys_.begin() + static_cast<long>(at), key);
}

/// Index of the key of the curve at the given clip-local time, or M_MAX_UNSIGNED.
unsigned FindKeyAtTime(const TimelinePropertyCurve& curve, float time)
{
    for (unsigned i = 0; i < curve.keys_.size(); ++i)
    {
        if (curve.keys_[i].time_ > time + KeyTimeEps)
            break;
        if (curve.keys_[i].time_ >= time - KeyTimeEps)
            return i;
    }
    return M_MAX_UNSIGNED;
}

}

void TimelineDopeSheet::Open(bool focus)
{
    open_ = true;
    focusPending_ = focus;
    fitPending_ = focus;
}

void TimelineDopeSheet::Close()
{
    open_ = false;
    dragMode_ = DragMode::None;
}

void TimelineDopeSheet::Render(TimelineEditorHost& host, const ea::vector<TrackRow>& trackRows,
    TimelineResource& resource)
{
    frameLength_ = 1.0f / Max(resource.GetFrameRate(), 1.0f);

    ui::SetNextWindowSize(ImVec2(900.0f, 480.0f), ImGuiCond_FirstUseEver);
    if (focusPending_)
    {
        ui::SetNextWindowFocus();
        focusPending_ = false;
    }

    bool open = open_;
    if (!ui::Begin("Timeline Dope Sheet", &open))
    {
        // Collapsed or fully clipped behind another dock: the content is skipped, the selection and
        // the view range are kept.
        ui::End();
        open_ = open;
        return;
    }

    BuildRows(trackRows);
    ValidateSelection();
    RenderToolbar(host, resource);
    RenderRuler(host);
    RenderGrid(host, resource);
    ProcessShortcuts(host, resource);

    ui::End();
    open_ = open;
    if (!open_)
        dragMode_ = DragMode::None;
}

void TimelineDopeSheet::RenderToolbar(TimelineEditorHost& host, TimelineResource& resource)
{
    if (ui::Button("Fit"))
        fitPending_ = true;
    if (ui::IsItemHovered())
        ui::SetTooltip("Show the whole timeline.");

    ui::SameLine();
    if (ui::Button("Playhead"))
    {
        // Center the visible range on the play head without changing the zoom.
        const float visible = (canvasMax_.x - canvasMin_.x - labelWidth_) / Max(pixelsPerSecond_, 0.01f);
        viewStartTime_ = Max(SnapToFrame(host.GetCursorTime()) - visible * 0.5f, 0.0f);
    }
    if (ui::IsItemHovered())
        ui::SetTooltip("Scroll the view to the play head.");

    ui::SameLine();
    ui::TextDisabled("rows: %u", static_cast<unsigned>(rows_.size()));
    ui::SameLine();
    ui::TextDisabled("selected keys: %u markers: %u clips: %u", static_cast<unsigned>(selectedKeys_.size()),
        static_cast<unsigned>(selectedMarkers_.size()), static_cast<unsigned>(selectedClips_.size()));

    ui::SameLine();
    ui::TextDisabled("click ruler: select key row | drag: marquee | dbl-click/right-click: keys | delete: remove | Ctrl+C/V: keys");

    if (fitPending_)
    {
        // The window size of the first frame is what the fit is measured against, so the fit can
        // only be done once the canvas has been laid out: request it and run on the next frame.
        fitPending_ = false;
        FitToContent(resource);
    }
}

void TimelineDopeSheet::FitToContent(const TimelineResource& resource)
{
    const float duration = Max(resource.GetDuration(), 0.1f);
    const float width = Max(canvasMax_.x - canvasMin_.x - labelWidth_, 100.0f);
    pixelsPerSecond_ = Clamp(width / duration, MinPixelsPerSecond, MaxPixelsPerSecond);
    viewStartTime_ = 0.0f;
    scrollRows_ = 0.0f;
}

void TimelineDopeSheet::BuildRows(const ea::vector<TrackRow>& trackRows)
{
    rows_.clear();
    for (const TrackRow& row : trackRows)
    {
        TimelineTrack* track = row.track_;
        if (!track)
            continue;

        DopeRow dope;
        dope.ref_ = row.ref_;
        dope.track_ = track;
        dope.depth_ = row.depth_;
        dope.color_ = GetTrackTypeColor(track->GetTrackTypeName());
        dope.label_ = track->GetName().empty() ? track->GetTrackTypeName() : track->GetName();

        if (row.isGroup_)
        {
            dope.kind_ = RowKind::Group;
            rows_.push_back(dope);
            continue;
        }

        if (dynamic_cast<TimelineMarkerTrack*>(track))
        {
            dope.kind_ = RowKind::Marker;
            rows_.push_back(dope);
            continue;
        }

        if (auto* propertyTrack = dynamic_cast<TimelinePropertyTrack*>(track))
        {
            // One row per curve of every clip: the keys of one attribute belong together in one
            // line, which is the reason the sheet exists next to the sequencer at all.
            for (unsigned ci = 0; ci < propertyTrack->clips_.size(); ++ci)
            {
                const auto& clip = propertyTrack->clips_[ci];
                for (unsigned ki = 0; ki < clip.curves_.size(); ++ki)
                {
                    DopeRow curveRow = dope;
                    curveRow.kind_ = RowKind::Curve;
                    curveRow.clip_ = ci;
                    curveRow.curve_ = ki;
                    curveRow.depth_ = row.depth_ + 1;
                    curveRow.label_ = Format("{} . {}", dope.label_, clip.curves_[ki].name_);
                    curveRow.color_ = GetCurveCanvasColor(ki);
                    rows_.push_back(curveRow);
                }
            }
            if (propertyTrack->clips_.empty())
            {
                // A property track without clips still gets a line so that it can be seen.
                dope.kind_ = RowKind::Clip;
                rows_.push_back(dope);
            }
            continue;
        }

        dope.kind_ = RowKind::Clip;
        rows_.push_back(dope);
    }
}

void TimelineDopeSheet::RenderRuler(TimelineEditorHost& host)
{
    ui::BeginChild("##dopeRuler", ImVec2(0.0f, rulerHeight_), ImGuiChildFlags_None,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // The ruler and the grid share their horizontal extent, so the frame lines of the grid always
    // line up with the ticks of the ruler.
    canvasMin_.x = ui::GetCursorScreenPos().x;
    canvasMax_.x = canvasMin_.x + ui::GetContentRegionAvail().x;
    // The end of the visible range follows the width of the canvas, which is known before the grid
    // child is laid out: recomputing it here keeps the ruler and the grid on the same numbers.
    viewEndTime_ = viewStartTime_ + Max((canvasMax_.x - canvasMin_.x - labelWidth_) / Max(pixelsPerSecond_, 0.01f),
        0.1f);
    const float top = ui::GetCursorScreenPos().y;
    const float bottom = top + rulerHeight_;
    ImDrawList* drawList = ui::GetWindowDrawList();
    drawList->AddRectFilled(ImVec2(canvasMin_.x, top), ImVec2(canvasMax_.x, bottom), ColorRulerBg);

    const float firstSecond = Floor(viewStartTime_);
    for (float time = firstSecond; time <= viewEndTime_; time += 1.0f)
    {
        const float x = TimeToX(time);
        if (x < canvasMin_.x || x > canvasMax_.x)
            continue;
        drawList->AddLine(ImVec2(x, top), ImVec2(x, bottom), ColorTick, 1.0f);
        drawList->AddText(ImVec2(x + 3.0f, top + (bottom - top - ui::GetFontSize()) * 0.5f), ColorTickLabel,
            Format("%d s", static_cast<int>(time)).c_str());
    }

    // Frame ticks: the density follows the zoom, so a distant view does not turn into solid gray.
    float frameStep = 1.0f;
    while (frameStep * frameLength_ * pixelsPerSecond_ < 10.0f)
        frameStep *= 2.0f;
    for (float time = Floor(viewStartTime_ / frameLength_) * frameLength_; time <= viewEndTime_;
         time += frameLength_ * frameStep)
    {
        const float x = TimeToX(time);
        if (x < canvasMin_.x || x > canvasMax_.x)
            continue;
        drawList->AddLine(ImVec2(x, bottom - 5.0f), ImVec2(x, bottom), ColorTickMinor, 1.0f);
    }

    // The play head of the sequencer is the reference for the paste of copied keys.
    const float cursorX = TimeToX(host.GetCursorTime());
    drawList->AddLine(ImVec2(cursorX, top), ImVec2(cursorX, bottom), ColorCursor, 2.0f);

    // Click on a tick selects every key row of the visible curves at that frame, the way the Unity
    // sheet works. Ctrl adds the frame to the selection instead of replacing it.
    const bool hovered = ui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    if (hovered && ui::IsMouseClicked(ImGuiMouseButton_Left))
        SelectTimeRow(SnapToFrame(XToTime(ui::GetIO().MousePos.x)), ui::GetIO().KeyCtrl);
    if (hovered)
        ui::SetMouseCursor(ImGuiMouseCursor_Hand);

    ui::EndChild();
}

void TimelineDopeSheet::RenderGrid(TimelineEditorHost& host, TimelineResource& resource)
{
    ui::BeginChild("##dopeGrid", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    canvasMin_ = ui::GetCursorScreenPos();
    const ImVec2 size = ui::GetContentRegionAvail();
    canvasMax_ = ImVec2(canvasMin_.x + size.x, canvasMin_.y + size.y);
    viewEndTime_ = viewStartTime_ + Max((canvasMax_.x - canvasMin_.x - labelWidth_) / Max(pixelsPerSecond_, 0.01f),
        0.1f);
    canvasHovered_ = ui::IsWindowHovered();

    if (fitPending_)
        FitToContent(resource);

    ImGuiIO& io = ui::GetIO();
    if (canvasHovered_)
    {
        if (io.MouseWheel != 0.0f)
        {
            if (io.KeyCtrl)
            {
                // Zoom around the mouse position so the content under the cursor stays put.
                const float timeUnderMouse = XToTime(io.MousePos.x);
                const float factor = io.MouseWheel > 0.0f ? 1.25f : 0.8f;
                pixelsPerSecond_ = Clamp(pixelsPerSecond_ * factor, MinPixelsPerSecond, MaxPixelsPerSecond);
                viewStartTime_ = timeUnderMouse - (timeUnderMouse - viewStartTime_) / factor;
            }
            else if (io.KeyShift)
            {
                viewStartTime_ = Max(viewStartTime_ - io.MouseWheel * (canvasMax_.x - canvasMin_.x)
                    / Max(pixelsPerSecond_, 1.0f), 0.0f);
            }
            else
            {
                const float maxScroll = Max(static_cast<float>(rows_.size()) * rowHeight_
                    - (canvasMax_.y - canvasMin_.y), 0.0f) / rowHeight_;
                scrollRows_ = Clamp(scrollRows_ - io.MouseWheel * 3.0f, 0.0f, maxScroll);
            }
        }
        if (ui::IsMouseClicked(ImGuiMouseButton_Middle))
        {
            dragMode_ = DragMode::Pan;
            panGrabX_ = io.MousePos.x;
            panOrigView_ = viewStartTime_;
        }
    }

    ImDrawList* drawList = ui::GetWindowDrawList();
    drawList->PushClipRect(canvasMin_, canvasMax_, true);
    DrawRows(host);
    if (dragMode_ == DragMode::SelectRect)
    {
        // Marquee: filled with a translucent tint and outlined so it stays visible over the rows.
        const ImVec2 min = ImVec2(Min(selectRectMin_.x, selectRectMax_.x), Min(selectRectMin_.y, selectRectMax_.y));
        const ImVec2 max = ImVec2(Max(selectRectMin_.x, selectRectMax_.x), Max(selectRectMin_.y, selectRectMax_.y));
        drawList->AddRectFilled(min, max, ColorSelectionRect);
        drawList->AddRect(min, max, ColorSelectionBorder, 0.0f, 0, 1.0f);
    }
    drawList->PopClipRect();

    // Right-click over the grid, not over the caption column: the menu works on the row it was
    // opened on, so the row and the snapped frame are remembered for as long as it is open.
    if (canvasHovered_ && io.MousePos.x > canvasMin_.x + labelWidth_ && ui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        const int row = static_cast<int>(Floor(YToRow(io.MousePos.y)));
        if (row >= 0 && row < static_cast<int>(rows_.size()))
        {
            popupRow_ = row;
            popupTime_ = SnapToFrame(XToTime(io.MousePos.x));
            ui::OpenPopup("DopeContextMenu");
        }
    }
    RenderContextMenu(host);

    ProcessSelectionDrag(host, resource);
    ui::EndChild();
}

void TimelineDopeSheet::RenderContextMenu(TimelineEditorHost& host)
{
    if (!ui::BeginPopup("DopeContextMenu"))
        return;

    const bool validRow = popupRow_ >= 0 && popupRow_ < static_cast<int>(rows_.size());
    const DopeRow* row = validRow ? &rows_[static_cast<unsigned>(popupRow_)] : nullptr;
    const bool isCurveRow = row && row->kind_ == RowKind::Curve;

    if (isCurveRow)
    {
        // A key row spans every curve of the clip, so the two entries talk about the frame and not
        // about the single curve the caption of the row names.
        TimelinePropertyClip* clip = nullptr;
        auto* propertyTrack = dynamic_cast<TimelinePropertyTrack*>(row->track_);
        if (propertyTrack && row->clip_ < propertyTrack->clips_.size())
            clip = &propertyTrack->clips_[row->clip_];
        const float local = clip ? Clamp(popupTime_ - clip->start_, 0.0f, clip->duration_) : 0.0f;
        const bool rowExists = clip && HasPropertyKeyRowAt(*clip, local);

        // The two verbs match the curve window: "Add Key" and "Delete Key" for one and the same
        // thing seen from two windows.
        ui::BeginDisabled(!rowExists);
        if (ui::Selectable(Format("Delete Key at {}", FormatTime(popupTime_).c_str()).c_str()) && rowExists)
        {
            host.BeginModify();
            RemovePropertyKeysAt(*clip, local);
        }
        if (!rowExists && ui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ui::SetTooltip("No key row at this frame.");
        ui::EndDisabled();

        ui::Separator();

        ui::BeginDisabled(rowExists);
        if (ui::Selectable(Format("Add Key at {}", FormatTime(popupTime_).c_str()).c_str()) && !rowExists)
            AddKeyRowAt(host, popupRow_, popupTime_);
        ui::EndDisabled();
        if (ui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            if (rowExists)
                ui::SetTooltip("A key row already exists at this frame.");
            else
                ui::SetTooltip("Add a key to every curve of the clip at this frame.\n"
                    "The value is copied from the nearest key.");
        }
    }
    else
    {
        ui::TextDisabled("Keys only exist on curve rows.");
    }

    ui::EndPopup();
}

void TimelineDopeSheet::DrawRows(TimelineEditorHost& host)
{
    ImGuiIO& io = ui::GetIO();
    ImDrawList* drawList = ui::GetWindowDrawList();

    // Background, grid lines and captions of the visible slice of rows.
    const int firstRow = Max(static_cast<int>(Floor(scrollRows_)) - 1, 0);
    const int lastRow = static_cast<int>(Ceil(scrollRows_ + (canvasMax_.y - canvasMin_.y) / rowHeight_)) + 1;

    for (float time = Floor(viewStartTime_); time <= viewEndTime_; time += 1.0f)
    {
        const float x = TimeToX(time);
        drawList->AddLine(ImVec2(x, canvasMin_.y), ImVec2(x, canvasMax_.y), ColorSecondLine, 1.0f);
    }
    const float firstFrame = Floor(viewStartTime_ / frameLength_) * frameLength_;
    const float framePx = frameLength_ * pixelsPerSecond_;
    if (framePx >= 3.0f)
    {
        for (float time = firstFrame; time <= viewEndTime_; time += frameLength_)
        {
            const float x = TimeToX(time);
            drawList->AddLine(ImVec2(x, canvasMin_.y), ImVec2(x, canvasMax_.y), ColorFrameLine, 1.0f);
        }
    }

    for (int i = firstRow; i < lastRow && i < static_cast<int>(rows_.size()); ++i)
    {
        const DopeRow& row = rows_[static_cast<unsigned>(i)];
        const float y = RowY(static_cast<float>(i));
        const ImVec2 rowMin(canvasMin_.x, y);
        const ImVec2 rowMax(canvasMax_.x, y + rowHeight_);

        // The caption column is drawn opaque so the content scrolling under it never shows through.
        drawList->AddRectFilled(rowMin, ImVec2(canvasMin_.x + labelWidth_, rowMax.y),
            (i % 2) ? ColorLabelBgAlt : ColorLabelBg);
        const float textX = canvasMin_.x + labelWidth_ * 0.06f + static_cast<float>(row.depth_) * 12.0f;
        drawList->AddRectFilled(ImVec2(textX - 5.0f, y + 3.0f), ImVec2(textX - 2.0f, rowMax.y - 3.0f), row.color_);
        const ea::string label = row.kind_ == RowKind::Group ? Format("/ {} /", row.label_) : row.label_;
        drawList->AddText(ImVec2(textX, y + (rowHeight_ - ui::GetFontSize()) * 0.5f),
            row.kind_ == RowKind::Group ? ColorTickLabel : ColorHeaderText, label.c_str());

        // Separator between the caption column and the grid.
        drawList->AddLine(ImVec2(canvasMin_.x + labelWidth_, rowMin.y), ImVec2(canvasMin_.x + labelWidth_, rowMax.y),
            ColorTick, 1.0f);

        const float centerY = y + rowHeight_ * 0.5f;
        const float offset = (dragMode_ == DragMode::Keys || dragMode_ == DragMode::Markers || dragMode_ == DragMode::Clips)
            ? dragTimeDelta_
            : 0.0f;

        switch (row.kind_)
        {
        case RowKind::Group:
            break;

        case RowKind::Marker:
        {
            auto* markerTrack = dynamic_cast<TimelineMarkerTrack*>(row.track_);
            if (!markerTrack)
                break;
            const auto& markers = markerTrack->GetMarkers();
            for (unsigned mi = 0; mi < markers.size(); ++mi)
            {
                const bool selected = IsMarkerSelected(row.ref_, mi);
                const float x = TimeToX(markers[mi].time_ + (selected ? offset : 0.0f));
                if (x < canvasMin_.x - 8.0f || x > canvasMax_.x + 8.0f)
                    continue;
                const ImVec2 min(x - KeySize * 0.5f, centerY - KeySize * 0.5f);
                const ImVec2 max(x + KeySize * 0.5f, centerY + KeySize * 0.5f);
                drawList->AddRectFilled(min, max, selected ? ColorKeySelected : ColorMarker);
                drawList->AddRect(min, max, ColorClipText, 2.0f, 0, 1.0f);
            }
            break;
        }

        case RowKind::Clip:
        {
            // Every clip of the track is one block; blocks of different tracks of the same type are
            // the same color, the row caption tells them apart.
            const unsigned count = GetClipCount(row.track_);
            for (unsigned ci = 0; ci < count; ++ci)
            {
                const TimelineClip* clip = GetClipAt(row.track_, ci);
                if (!clip)
                    continue;
                const bool selected = IsClipSelected(row.ref_, ci);
                const float x0 = TimeToX(clip->start_ + (selected ? offset : 0.0f));
                const float x1 = TimeToX(clip->GetEndTime() + (selected ? offset : 0.0f));
                if (x1 < canvasMin_.x || x0 > canvasMax_.x)
                    continue;
                const ImVec2 min(Max(x0, canvasMin_.x + labelWidth_), centerY - BlockHeight * 0.5f);
                const ImVec2 max(Min(x1, canvasMax_.x), centerY + BlockHeight * 0.5f);
                drawList->AddRectFilled(min, max, WithAlpha(row.color_, selected ? 230 : 170), 2.0f);
                drawList->AddRect(min, max, selected ? ColorKeySelected : ColorClipText, 2.0f, 0, 1.0f);
            }
            break;
        }

        case RowKind::Curve:
        {
            auto* propertyTrack = dynamic_cast<TimelinePropertyTrack*>(row.track_);
            if (!propertyTrack || row.clip_ >= propertyTrack->clips_.size())
                break;
            const TimelinePropertyClip& clip = propertyTrack->clips_[row.clip_];
            if (row.curve_ >= clip.curves_.size())
                break;
            const TimelinePropertyCurve& curve = clip.curves_[row.curve_];

            // The extent of the owning clip is drawn as a band behind the keys, so it is visible
            // where a row of keys still has a curve to sit on.
            const float bandX0 = TimeToX(clip.start_);
            const float bandX1 = TimeToX(clip.GetEndTime());
            drawList->AddRectFilled(ImVec2(Max(bandX0, canvasMin_.x + labelWidth_), y + 2.0f),
                ImVec2(Min(bandX1, canvasMax_.x), rowMax.y - 2.0f), ColorClipBand);

            for (const TimelinePropertyKey& key : curve.keys_)
            {
                const bool selected = IsKeySelected(row.ref_, row.clip_, key.time_);
                const float time = clip.start_ + key.time_ + (selected ? offset : 0.0f);
                const float x = TimeToX(time);
                if (x < canvasMin_.x || x > canvasMax_.x)
                    continue;
                const ImVec2 center(x, centerY);
                // Keys are squares, not diamonds: the sheet is a grid and squares line up in it.
                const ImVec2 min(center.x - KeySize * 0.5f, center.y - KeySize * 0.5f);
                const ImVec2 max(center.x + KeySize * 0.5f, center.y + KeySize * 0.5f);
                drawList->AddRectFilled(min, max, selected ? ColorKeySelected : WithAlpha(row.color_, 230));
                drawList->AddRect(min, max, WithAlpha(ColorClipText, selected ? 255 : 120), 1.0f, 0, 1.0f);
                if (selected && io.KeyShift)
                {
                    // Value of the selected keys, so a shift of the selection can be judged.
                    drawList->AddText(ImVec2(max.x + 3.0f, center.y - ui::GetFontSize() * 0.5f), ColorTickLabel,
                        Format("%.3f", GetCurveComponentValue(key.value_, 0)).c_str());
                }
            }
            break;
        }
        }
    }

    // Play head over the rows, drawn last so it is never hidden by a block.
    const float cursorX = TimeToX(host.GetCursorTime());
    if (cursorX >= canvasMin_.x && cursorX <= canvasMax_.x)
    {
        drawList->AddLine(ImVec2(cursorX, canvasMin_.y), ImVec2(cursorX, canvasMax_.y), ColorCursor, 2.0f);
    }
}

void TimelineDopeSheet::ProcessSelectionDrag(TimelineEditorHost& host, TimelineResource& resource)
{
    ImGuiIO& io = ui::GetIO();
    const bool inCanvas = io.MousePos.x >= canvasMin_.x && io.MousePos.x <= canvasMax_.x && io.MousePos.y >= canvasMin_.y
        && io.MousePos.y <= canvasMax_.y;

    if (dragMode_ == DragMode::Pan)
    {
        if (!io.MouseDown[ImGuiMouseButton_Middle])
            dragMode_ = DragMode::None;
        else
            viewStartTime_ = Max(panOrigView_ - (io.MousePos.x - panGrabX_) / Max(pixelsPerSecond_, 0.01f), 0.0f);
        return;
    }

    if (!io.MouseDown[ImGuiMouseButton_Left])
    {
        if (dragMode_ == DragMode::SelectRect)
            SelectRect(selectRectMin_, selectRectMax_, io.KeyCtrl);
        else if (dragMode_ != DragMode::None)
            CommitDrag(host, resource);
        dragMode_ = DragMode::None;
        return;
    }

    switch (dragMode_)
    {
    case DragMode::SelectRect:
        selectRectMax_ = io.MousePos;
        break;

    case DragMode::Keys:
    {
        dragTimeDelta_ = SnapToFrame(XToTime(io.MousePos.x)) - dragGrabTime_;
        dragEndRow_ = Clamp(static_cast<int>(Floor(YToRow(io.MousePos.y))), 0, static_cast<int>(rows_.size()) - 1);
        break;
    }

    case DragMode::Markers:
    case DragMode::Clips:
        dragTimeDelta_ = SnapToFrame(XToTime(io.MousePos.x)) - dragGrabTime_;
        break;

    default:
        break;
    }

    if (dragMode_ != DragMode::None)
        return;
    if (!canvasHovered_ || !inCanvas || !ui::IsMouseClicked(ImGuiMouseButton_Left))
        return;

    // A press on the grid either grabs content, or starts a marquee over the empty area.
    DopeKey key;
    DopeMarker marker;
    DopeClip clip;
    dragGrabTime_ = SnapToFrame(XToTime(io.MousePos.x));
    dragStartPos_ = io.MousePos;
    dragEndRow_ = dragStartRow_ = static_cast<int>(Floor(YToRow(io.MousePos.y)));

    if (HitKey(io.MousePos, key))
    {
        if (io.KeyCtrl)
        {
            if (!IsKeySelected(key.ref_, key.clip_, key.time_))
                selectedKeys_.push_back(key);
            else
                EraseKey(key.ref_, key.clip_, key.time_);
            return;
        }
        if (!IsKeySelected(key.ref_, key.clip_, key.time_))
        {
            ClearSelection();
            selectedKeys_.push_back(key);
        }
        dragMode_ = DragMode::Keys;
        dragTimeDelta_ = 0.0f;
        return;
    }
    if (HitMarker(io.MousePos, marker))
    {
        if (io.KeyCtrl && IsMarkerSelected(marker.ref_, marker.index_))
        {
            EraseMarker(marker.ref_, marker.index_);
            return;
        }
        if (!IsMarkerSelected(marker.ref_, marker.index_))
        {
            ClearSelection();
            selectedMarkers_.push_back(marker);
        }
        dragMode_ = DragMode::Markers;
        dragTimeDelta_ = 0.0f;
        return;
    }
    if (HitClip(io.MousePos, clip))
    {
        if (io.KeyCtrl && IsClipSelected(clip.ref_, clip.index_))
        {
            EraseClip(clip.ref_, clip.index_);
            return;
        }
        if (!IsClipSelected(clip.ref_, clip.index_))
        {
            ClearSelection();
            selectedClips_.push_back(clip);
        }
        dragMode_ = DragMode::Clips;
        dragTimeDelta_ = 0.0f;
        return;
    }

    if (!io.KeyCtrl)
        ClearSelection();

    // A double-click on the empty grid of a curve row adds a key row at that frame, the same gesture
    // the curve window uses. Anything else falls through to the marquee.
    if (ui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && AddKeyRowAt(host, dragStartRow_, dragGrabTime_))
        return;

    dragMode_ = DragMode::SelectRect;
    selectRectMin_ = io.MousePos;
    selectRectMax_ = io.MousePos;
}

void TimelineDopeSheet::CommitDrag(TimelineEditorHost& host, TimelineResource& resource)
{
    if (Abs(dragTimeDelta_) < frameLength_ * 0.5f && dragMode_ != DragMode::Keys)
        return;

    if (dragMode_ == DragMode::Keys)
    {
        // A drag that ended on another curve row moved the values across, one that slid sideways
        // moved the key rows in time. Whichever dominates decides what the gesture meant.
        const float dxPixels = Abs(dragTimeDelta_) * pixelsPerSecond_;
        const int rowDelta = dragEndRow_ - dragStartRow_;
        if (rowDelta != 0 && Abs(rowDelta) * rowHeight_ > dxPixels)
        {
            MoveKeysToRow(host, rowDelta);
            return;
        }
        if (dxPixels < 0.5f)
            return;

        host.BeginModify();
        // Keys are addressed by their clip-local time, which the drag did not change, so every
        // selected row can be moved by the same delta without re-reading the content.
        for (const DopeKey& key : selectedKeys_)
        {
            TimelineTrack* track = FindTrack(key.ref_);
            auto* propertyTrack = dynamic_cast<TimelinePropertyTrack*>(track);
            if (!propertyTrack || key.clip_ >= propertyTrack->clips_.size())
                continue;
            TimelinePropertyClip& clip = propertyTrack->clips_[key.clip_];
            const float local = Clamp(key.time_ + dragTimeDelta_, 0.0f, clip.duration_);
            if (SameKeyTime(local, key.time_))
                continue;
            MovePropertyKeysAt(clip, key.time_, local);
        }
        return;
    }

    if (dragMode_ == DragMode::Markers)
    {
        host.BeginModify();
        for (const DopeMarker& item : selectedMarkers_)
        {
            auto* markerTrack = dynamic_cast<TimelineMarkerTrack*>(FindTrack(item.ref_));
            if (!markerTrack || item.index_ >= markerTrack->GetNumMarkers())
                continue;
            auto& markers = markerTrack->GetMarkers();
            markers[item.index_].time_ = Max(SnapToFrame(markers[item.index_].time_ + dragTimeDelta_), 0.0f);
        }
        return;
    }

    if (dragMode_ == DragMode::Clips)
    {
        host.BeginModify();
        for (const DopeClip& item : selectedClips_)
        {
            TimelineTrack* track = FindTrack(item.ref_);
            TimelineClip* clip = track ? GetClipAt(track, item.index_) : nullptr;
            if (!clip)
                continue;
            clip->start_ = Max(SnapToFrame(clip->start_ + dragTimeDelta_), 0.0f);
        }
    }
}

void TimelineDopeSheet::MoveKeysToRow(TimelineEditorHost& host, int rowDelta)
{
    const int target = dragStartRow_ + rowDelta;
    if (target < 0 || target >= static_cast<int>(rows_.size()))
        return;
    const DopeRow& targetRow = rows_[static_cast<unsigned>(target)];
    if (targetRow.kind_ != RowKind::Curve)
        return;
    auto* targetProperty = dynamic_cast<TimelinePropertyTrack*>(targetRow.track_);
    if (!targetProperty || targetRow.clip_ >= targetProperty->clips_.size())
        return;
    TimelinePropertyClip& targetClip = targetProperty->clips_[targetRow.clip_];
    if (targetRow.curve_ >= targetClip.curves_.size())
        return;
    TimelinePropertyCurve& targetCurve = targetClip.curves_[targetRow.curve_];
    const int targetComponents = GetCurveComponentCount(targetCurve.valueType_);
    if (targetComponents == 0)
        return;

    // Keys are copied onto the target curve at their own time, the source keeps them: dropping them
    // would destroy the row the other curves still hold at that time.
    host.BeginModify();
    for (const DopeKey& key : selectedKeys_)
    {
        auto* sourceProperty = dynamic_cast<TimelinePropertyTrack*>(FindTrack(key.ref_));
        if (!sourceProperty || key.clip_ >= sourceProperty->clips_.size())
            continue;
        const TimelinePropertyClip& sourceClip = sourceProperty->clips_[key.clip_];
        for (const TimelinePropertyCurve& curve : sourceClip.curves_)
        {
            const unsigned ki = FindKeyAtTime(curve, key.time_);
            if (ki == M_MAX_UNSIGNED)
                continue;
            TimelinePropertyKey copy = curve.keys_[ki];
            const int sourceComponents = GetCurveComponentCount(curve.valueType_);
            if (sourceComponents != targetComponents)
            {
                // The target animates a type with a different component count: the numbers are
                // carried component by component, the rest of the target value stays at its default.
                // Tangents are per component too, so they only survive an identical layout.
                Variant value(targetCurve.valueType_);
                const int count = Min(sourceComponents, targetComponents);
                for (int c = 0; c < count; ++c)
                    SetCurveComponentValue(value, c, GetCurveComponentValue(copy.value_, c));
                copy.value_ = value;
                copy.outTangent_ = copy.inTangent_ = 0.0f;
            }
            copy.time_ = key.time_;
            InsertKeyCopy(targetCurve, copy);
        }
    }
}

void TimelineDopeSheet::ProcessShortcuts(TimelineEditorHost& host, TimelineResource& resource)
{
    if (!ui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        return;

    if (ui::IsKeyPressed(ImGuiKey_F))
        fitPending_ = true;

    if (ImGui::IsKeyPressed(ImGuiKey_Delete))
    {
        if (!selectedKeys_.empty() || !selectedMarkers_.empty())
            host.BeginModify();
        for (const DopeKey& key : selectedKeys_)
        {
            auto* propertyTrack = dynamic_cast<TimelinePropertyTrack*>(FindTrack(key.ref_));
            if (!propertyTrack || key.clip_ >= propertyTrack->clips_.size())
                continue;
            RemovePropertyKeysAt(propertyTrack->clips_[key.clip_], key.time_);
        }
        // Marker indices shift when one is removed, so the list is erased from the last to the
        // first and only once per track.
        ea::vector<DopeMarker> markers = selectedMarkers_;
        ea::sort(markers.begin(), markers.end(), [](const DopeMarker& a, const DopeMarker& b)
        {
            return a.index_ > b.index_;
        });
        for (const DopeMarker& item : markers)
        {
            auto* markerTrack = dynamic_cast<TimelineMarkerTrack*>(FindTrack(item.ref_));
            if (markerTrack && item.index_ < markerTrack->GetNumMarkers())
                markerTrack->RemoveMarker(item.index_);
        }
        selectedKeys_.clear();
        selectedMarkers_.clear();
        return;
    }

    ImGuiIO& io = ui::GetIO();
    if (!io.KeyCtrl)
        return;

    if (ui::IsKeyPressed(ImGuiKey_C))
    {
        clipboard_.clear();
        for (const DopeKey& key : selectedKeys_)
        {
            auto* propertyTrack = dynamic_cast<TimelinePropertyTrack*>(FindTrack(key.ref_));
            if (!propertyTrack || key.clip_ >= propertyTrack->clips_.size())
                continue;
            const TimelinePropertyClip& clip = propertyTrack->clips_[key.clip_];
            for (const TimelinePropertyCurve& curve : clip.curves_)
            {
                const unsigned ki = FindKeyAtTime(curve, key.time_);
                if (ki == M_MAX_UNSIGNED)
                    continue;
                CopiedKey copied;
                copied.ref_ = key.ref_;
                copied.clip_ = key.clip_;
                copied.curve_ = curve.name_;
                copied.key_ = curve.keys_[ki];
                clipboard_.push_back(copied);
            }
        }
    }
    else if (ui::IsKeyPressed(ImGuiKey_V))
        PasteKeys(host);
}

void TimelineDopeSheet::PasteKeys(TimelineEditorHost& host)
{
    if (clipboard_.empty())
        return;

    host.BeginModify();
    // The earliest copied key of every clip lands on the play head, the rest keep their relative
    // spacing, which is what a copy of a key row means.
    const float cursor = SnapToFrame(host.GetCursorTime());
    for (const CopiedKey& copied : clipboard_)
    {
        auto* propertyTrack = dynamic_cast<TimelinePropertyTrack*>(FindTrack(copied.ref_));
        if (!propertyTrack || copied.clip_ >= propertyTrack->clips_.size())
            continue;
        float earliest = copied.key_.time_;
        for (const CopiedKey& other : clipboard_)
        {
            if (other.ref_ == copied.ref_ && other.clip_ == copied.clip_ && other.key_.time_ < earliest)
                earliest = other.key_.time_;
        }
        TimelinePropertyClip& clip = propertyTrack->clips_[copied.clip_];
        for (TimelinePropertyCurve& curve : clip.curves_)
        {
            if (curve.name_ != copied.curve_)
                continue;
            TimelinePropertyKey key = copied.key_;
            key.time_ = Clamp(key.time_ - earliest + cursor - clip.start_, 0.0f, clip.duration_);
            InsertKeyCopy(curve, key);
        }
    }
}

void TimelineDopeSheet::SelectTimeRow(float time, bool add)
{
    if (!add)
        ClearSelection();
    // Every visible curve row contributes its key at that frame, if it has one.
    for (const DopeRow& row : rows_)
    {
        if (row.kind_ != RowKind::Curve)
            continue;
        auto* propertyTrack = dynamic_cast<TimelinePropertyTrack*>(row.track_);
        if (!propertyTrack || row.clip_ >= propertyTrack->clips_.size())
            continue;
        const TimelinePropertyClip& clip = propertyTrack->clips_[row.clip_];
        const float local = time - clip.start_;
        if (local < -KeyTimeEps || local > clip.duration_ + KeyTimeEps)
            continue;
        for (const TimelinePropertyCurve& curve : clip.curves_)
        {
            if (FindKeyAtTime(curve, local) == M_MAX_UNSIGNED)
                continue;
            if (!IsKeySelected(row.ref_, row.clip_, curve.keys_[FindKeyAtTime(curve, local)].time_))
            {
                selectedKeys_.push_back(
                    DopeKey{row.ref_, row.clip_, curve.keys_[FindKeyAtTime(curve, local)].time_});
            }
            break;
        }
    }
}

void TimelineDopeSheet::SelectRect(const ImVec2& min, const ImVec2& max, bool add)
{
    const ImVec2 rectMin(Min(min.x, max.x), Min(min.y, max.y));
    const ImVec2 rectMax(Max(min.x, max.x), Max(min.y, max.y));
    // A click without any movement keeps the selection the click itself made.
    if (rectMax.x - rectMin.x < 2.0f && rectMax.y - rectMin.y < 2.0f)
        return;
    if (!add)
        ClearSelection();

    const float timeMin = XToTime(rectMin.x);
    const float timeMax = XToTime(rectMax.x);
    const int firstRow = Clamp(static_cast<int>(Floor(YToRow(rectMin.y))), 0, static_cast<int>(rows_.size()) - 1);
    const int lastRow = Clamp(static_cast<int>(Ceil(YToRow(rectMax.y))), 0, static_cast<int>(rows_.size()) - 1);
    for (int i = firstRow; i <= lastRow && i < static_cast<int>(rows_.size()); ++i)
    {
        const DopeRow& row = rows_[static_cast<unsigned>(i)];
        const float y = RowY(static_cast<float>(i));
        if (y + rowHeight_ < rectMin.y || y > rectMax.y)
            continue;

        if (row.kind_ == RowKind::Curve)
        {
            auto* propertyTrack = dynamic_cast<TimelinePropertyTrack*>(row.track_);
            if (!propertyTrack || row.clip_ >= propertyTrack->clips_.size())
                continue;
            const TimelinePropertyClip& clip = propertyTrack->clips_[row.clip_];
            for (const TimelinePropertyCurve& curve : clip.curves_)
            {
                for (const TimelinePropertyKey& key : curve.keys_)
                {
                    const float time = clip.start_ + key.time_;
                    if (time < timeMin || time > timeMax)
                        continue;
                    if (!IsKeySelected(row.ref_, row.clip_, key.time_))
                        selectedKeys_.push_back(DopeKey{row.ref_, row.clip_, key.time_});
                }
            }
        }
        else if (row.kind_ == RowKind::Marker)
        {
            auto* markerTrack = dynamic_cast<TimelineMarkerTrack*>(row.track_);
            if (!markerTrack)
                continue;
            const auto& markers = markerTrack->GetMarkers();
            for (unsigned mi = 0; mi < markers.size(); ++mi)
            {
                if (markers[mi].time_ < timeMin || markers[mi].time_ > timeMax)
                    continue;
                if (!IsMarkerSelected(row.ref_, mi))
                    selectedMarkers_.push_back(DopeMarker{row.ref_, mi});
            }
        }
        else if (row.kind_ == RowKind::Clip)
        {
            const unsigned count = GetClipCount(row.track_);
            for (unsigned ci = 0; ci < count; ++ci)
            {
                const TimelineClip* clip = GetClipAt(row.track_, ci);
                if (!clip || clip->GetEndTime() < timeMin || clip->start_ > timeMax)
                    continue;
                if (!IsClipSelected(row.ref_, ci))
                    selectedClips_.push_back(DopeClip{row.ref_, ci});
            }
        }
    }
}

void TimelineDopeSheet::ValidateSelection()
{
    // A stale address simply drops its selection entry: undo of a clip removal must never leave
    // the sheet pointing at unrelated content.
    auto validTrack = [this](const TrackRef& ref)
    {
        return FindTrack(ref) != nullptr;
    };

    for (auto it = selectedKeys_.begin(); it != selectedKeys_.end();)
    {
        auto* propertyTrack = dynamic_cast<TimelinePropertyTrack*>(validTrack(it->ref_) ? FindTrack(it->ref_) : nullptr);
        bool keep = false;
        if (propertyTrack && it->clip_ < propertyTrack->clips_.size())
        {
            const TimelinePropertyClip& clip = propertyTrack->clips_[it->clip_];
            for (const TimelinePropertyCurve& curve : clip.curves_)
            {
                if (FindKeyAtTime(curve, it->time_) != M_MAX_UNSIGNED)
                {
                    keep = true;
                    break;
                }
            }
        }
        it = keep ? it + 1 : selectedKeys_.erase(it);
    }

    for (auto it = selectedMarkers_.begin(); it != selectedMarkers_.end();)
    {
        auto* markerTrack = dynamic_cast<TimelineMarkerTrack*>(FindTrack(it->ref_));
        const bool keep = markerTrack && it->index_ < markerTrack->GetNumMarkers();
        it = keep ? it + 1 : selectedMarkers_.erase(it);
    }

    for (auto it = selectedClips_.begin(); it != selectedClips_.end();)
    {
        const bool keep = validTrack(it->ref_) && it->index_ < GetClipCount(FindTrack(it->ref_));
        it = keep ? it + 1 : selectedClips_.erase(it);
    }
}

void TimelineDopeSheet::ClearSelection()
{
    selectedKeys_.clear();
    selectedMarkers_.clear();
    selectedClips_.clear();
}

TimelineTrack* TimelineDopeSheet::FindTrack(const TrackRef& ref) const
{
    for (const DopeRow& row : rows_)
    {
        if (row.ref_ == ref)
            return row.track_;
    }
    return nullptr;
}

bool TimelineDopeSheet::IsKeySelected(const TrackRef& ref, unsigned clip, float time) const
{
    for (const DopeKey& key : selectedKeys_)
    {
        if (key.ref_ == ref && key.clip_ == clip && SameKeyTime(key.time_, time))
            return true;
    }
    return false;
}

void TimelineDopeSheet::EraseKey(const TrackRef& ref, unsigned clip, float time)
{
    for (auto it = selectedKeys_.begin(); it != selectedKeys_.end(); ++it)
    {
        if (it->ref_ == ref && it->clip_ == clip && SameKeyTime(it->time_, time))
        {
            selectedKeys_.erase(it);
            return;
        }
    }
}

bool TimelineDopeSheet::IsMarkerSelected(const TrackRef& ref, unsigned index) const
{
    for (const DopeMarker& marker : selectedMarkers_)
    {
        if (marker.ref_ == ref && marker.index_ == index)
            return true;
    }
    return false;
}

void TimelineDopeSheet::EraseMarker(const TrackRef& ref, unsigned index)
{
    for (auto it = selectedMarkers_.begin(); it != selectedMarkers_.end(); ++it)
    {
        if (it->ref_ == ref && it->index_ == index)
        {
            selectedMarkers_.erase(it);
            return;
        }
    }
}

bool TimelineDopeSheet::IsClipSelected(const TrackRef& ref, unsigned index) const
{
    for (const DopeClip& clip : selectedClips_)
    {
        if (clip.ref_ == ref && clip.index_ == index)
            return true;
    }
    return false;
}

void TimelineDopeSheet::EraseClip(const TrackRef& ref, unsigned index)
{
    for (auto it = selectedClips_.begin(); it != selectedClips_.end(); ++it)
    {
        if (it->ref_ == ref && it->index_ == index)
        {
            selectedClips_.erase(it);
            return;
        }
    }
}

bool TimelineDopeSheet::HitKey(const ImVec2& pos, DopeKey& result) const
{
    const int row = static_cast<int>(Floor(YToRow(pos.y)));
    if (row < 0 || row >= static_cast<int>(rows_.size()))
        return false;
    const DopeRow& dopeRow = rows_[static_cast<unsigned>(row)];
    if (dopeRow.kind_ != RowKind::Curve)
        return false;
    auto* propertyTrack = dynamic_cast<TimelinePropertyTrack*>(dopeRow.track_);
    if (!propertyTrack || dopeRow.clip_ >= propertyTrack->clips_.size())
        return false;
    const TimelinePropertyClip& clip = propertyTrack->clips_[dopeRow.clip_];
    if (dopeRow.curve_ >= clip.curves_.size())
        return false;
    const float time = XToTime(pos.x);
    for (const TimelinePropertyKey& key : clip.curves_[dopeRow.curve_].keys_)
    {
        if (Abs(TimeToX(clip.start_ + key.time_) - pos.x) <= GrabRadius)
        {
            result = DopeKey{dopeRow.ref_, dopeRow.clip_, key.time_};
            return true;
        }
    }
    return false;
}

bool TimelineDopeSheet::AddKeyRowAt(TimelineEditorHost& host, int row, float globalTime)
{
    if (row < 0 || row >= static_cast<int>(rows_.size()))
        return false;
    const DopeRow& dopeRow = rows_[static_cast<unsigned>(row)];
    if (dopeRow.kind_ != RowKind::Curve)
        return false;
    auto* propertyTrack = dynamic_cast<TimelinePropertyTrack*>(dopeRow.track_);
    if (!propertyTrack || dopeRow.clip_ >= propertyTrack->clips_.size())
        return false;
    TimelinePropertyClip& clip = propertyTrack->clips_[dopeRow.clip_];
    const float local = Clamp(SnapToFrame(globalTime) - clip.start_, 0.0f, clip.duration_);
    // Two key rows at one frame would leave the segment between them undefined, and the frame the
    // caller offered is only usable when it is not already taken.
    if (HasPropertyKeyRowAt(clip, local))
        return false;

    host.BeginModify();
    AddPropertyKeyAt(clip, local);
    // The new row becomes the selection, so the next drag or delete already works on it.
    ClearSelection();
    selectedKeys_.push_back(DopeKey{ dopeRow.ref_, dopeRow.clip_, local });
    return true;
}

bool TimelineDopeSheet::HitMarker(const ImVec2& pos, DopeMarker& result) const
{
    const int row = static_cast<int>(Floor(YToRow(pos.y)));
    if (row < 0 || row >= static_cast<int>(rows_.size()))
        return false;
    const DopeRow& dopeRow = rows_[static_cast<unsigned>(row)];
    if (dopeRow.kind_ != RowKind::Marker)
        return false;
    auto* markerTrack = dynamic_cast<TimelineMarkerTrack*>(dopeRow.track_);
    if (!markerTrack)
        return false;
    const auto& markers = markerTrack->GetMarkers();
    for (unsigned mi = 0; mi < markers.size(); ++mi)
    {
        if (Abs(TimeToX(markers[mi].time_) - pos.x) <= GrabRadius)
        {
            result = DopeMarker{dopeRow.ref_, mi};
            return true;
        }
    }
    return false;
}

bool TimelineDopeSheet::HitClip(const ImVec2& pos, DopeClip& result) const
{
    const int row = static_cast<int>(Floor(YToRow(pos.y)));
    if (row < 0 || row >= static_cast<int>(rows_.size()))
        return false;
    const DopeRow& dopeRow = rows_[static_cast<unsigned>(row)];
    if (dopeRow.kind_ != RowKind::Clip)
        return false;
    const float time = XToTime(pos.x);
    const unsigned count = GetClipCount(dopeRow.track_);
    for (unsigned ci = 0; ci < count; ++ci)
    {
        const TimelineClip* clip = GetClipAt(dopeRow.track_, ci);
        if (clip && time >= clip->start_ && time <= clip->GetEndTime())
        {
            result = DopeClip{dopeRow.ref_, ci};
            return true;
        }
    }
    return false;
}

float TimelineDopeSheet::TimeToX(float time) const
{
    return canvasMin_.x + labelWidth_ + (time - viewStartTime_) * pixelsPerSecond_;
}

float TimelineDopeSheet::XToTime(float x) const
{
    return viewStartTime_ + (x - canvasMin_.x - labelWidth_) / Max(pixelsPerSecond_, 0.01f);
}

float TimelineDopeSheet::RowY(float index) const
{
    return canvasMin_.y + (index - scrollRows_) * rowHeight_;
}

float TimelineDopeSheet::YToRow(float y) const
{
    return scrollRows_ + (y - canvasMin_.y) / rowHeight_;
}

float TimelineDopeSheet::SnapToFrame(float time) const
{
    return Floor(time / frameLength_ + 0.5f) * frameLength_;
}

}

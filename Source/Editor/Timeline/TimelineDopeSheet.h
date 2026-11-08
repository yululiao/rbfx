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

#pragma once

#include "TimelineEditorHost.h"
#include "TimelineTrackRows.h"

#include <Urho3D/SystemUI/SystemUI.h>
#include <Urho3D/Timeline/TimelinePropertyTrack.h>

#include <EASTL/vector.h>

namespace Urho3D
{

class TimelineResource;

/// Standalone time-over-rows overview of the whole timeline, the way Unity's Dope Sheet works:
/// every track of the hierarchy gets a row, the curves of a property clip get a row of their own,
/// and the content is drawn as blocks and squares on a grid of frames instead of as curves.
///
/// The window owns only its view state and its selection. The edited data stay in the resource and
/// every change is opened as an undo action through the host, which keeps the window independent of
/// the tab that owns it.
class TimelineDopeSheet
{
public:
    /// Show the window. When focus is requested it is raised and fitted to the content.
    void Open(bool focus = true);
    void Close();
    bool IsOpen() const { return open_; }

    /// Draw the window over the visible track rows of the sequencer. Called every frame while
    /// IsOpen(); the rows are rebuilt from the resource each time, so the window never holds on to
    /// a track pointer across frames.
    void Render(TimelineEditorHost& host, const ea::vector<TrackRow>& rows, TimelineResource& resource);

private:
    /// What a row of the sheet draws.
    enum class RowKind
    {
        /// Caption of a group track, no content.
        Group,
        /// One curve of a property clip: keys are the editable squares of the sheet.
        Curve,
        /// Clips of a clip track drawn as blocks.
        Clip,
        /// Markers of a marker track drawn as squares.
        Marker,
    };

    /// One line of the left column of the sheet.
    struct DopeRow
    {
        RowKind kind_{};
        /// Address of the owning track. The pointer below is only valid for the current frame.
        TrackRef ref_;
        TimelineTrack* track_{};
        /// Index of the property clip of a curve row.
        unsigned clip_{M_MAX_UNSIGNED};
        /// Index of the curve inside the property clip of a curve row.
        unsigned curve_{M_MAX_UNSIGNED};
        /// Nesting level, drives the indentation of the caption.
        unsigned depth_{};
        ea::string label_;
        ImU32 color_{};
    };

    /// A key row of the sheet: every curve of the clip has a key at that clip-local time.
    struct DopeKey
    {
        TrackRef ref_;
        unsigned clip_{M_MAX_UNSIGNED};
        float time_{};
    };

    /// A marker, addressed by its track and its index in the marker list.
    struct DopeMarker
    {
        TrackRef ref_;
        unsigned index_{M_MAX_UNSIGNED};
    };

    /// A clip, addressed by its track and its index in the clip list.
    struct DopeClip
    {
        TrackRef ref_;
        unsigned index_{M_MAX_UNSIGNED};
    };

    /// Key held by the internal clipboard: the curve is addressed by name, because pasting into a
    /// clip that was edited in the meantime still has to find the same attribute.
    struct CopiedKey
    {
        TrackRef ref_;
        unsigned clip_{M_MAX_UNSIGNED};
        ea::string curve_;
        TimelinePropertyKey key_;
    };

    /// What the current left mouse drag does. The edits are applied when the button is released,
    /// while the drag only moves the preview offset, so an interrupted drag never rewrites content.
    enum class DragMode
    {
        None,
        /// Rubber band over the grid.
        SelectRect,
        /// Selected keys slid in time or moved to another curve.
        Keys,
        /// Selected markers slid in time.
        Markers,
        /// Selected clips slid in time.
        Clips,
        /// Horizontal pan of the visible time range.
        Pan,
    };

    void RenderToolbar(TimelineEditorHost& host, TimelineResource& resource);
    /// Rebuild the row list from the track rows of the sequencer.
    void BuildRows(const ea::vector<TrackRow>& trackRows);
    /// Ruler band: frame ticks, numbers and the play head.
    void RenderRuler(TimelineEditorHost& host);
    /// The grid itself: captions, content and all mouse interaction of the sheet.
    void RenderGrid(TimelineEditorHost& host, TimelineResource& resource);
    void DrawRows(TimelineEditorHost& host);
    /// Menu of the grid: the entries that add and remove a whole key row at the clicked frame.
    void RenderContextMenu(TimelineEditorHost& host);
    void ProcessSelectionDrag(TimelineEditorHost& host, TimelineResource& resource);
    /// Apply the pending drag of the keys, markers or clips.
    void CommitDrag(TimelineEditorHost& host, TimelineResource& resource);
    /// Copy the selected key rows onto the curve the drag ended on, keeping the source untouched.
    void MoveKeysToRow(TimelineEditorHost& host, int rowDelta);
    /// Keyboard shortcuts of the sheet: fit, delete and the key clipboard.
    void ProcessShortcuts(TimelineEditorHost& host, TimelineResource& resource);
    /// Paste the clipboard key rows so that the earliest of them lands on the play head.
    void PasteKeys(TimelineEditorHost& host);

    /// Track of the row list addressed by the reference, or null when the address went stale.
    TimelineTrack* FindTrack(const TrackRef& ref) const;
    /// Address a key row from the mouse position, null when the grid is empty there.
    bool HitKey(const ImVec2& pos, DopeKey& result) const;
    /// Append a key row to the curve row addressed by index, at the given timeline time. False when
    /// the index is not a curve row, the time falls outside the owning clip or a row sits there
    /// already: the same rule the curve window applies to its double-click and its menu.
    bool AddKeyRowAt(TimelineEditorHost& host, int row, float globalTime);
    bool HitMarker(const ImVec2& pos, DopeMarker& result) const;
    bool HitClip(const ImVec2& pos, DopeClip& result) const;
    bool IsKeySelected(const TrackRef& ref, unsigned clip, float time) const;
    void EraseKey(const TrackRef& ref, unsigned clip, float time);
    bool IsMarkerSelected(const TrackRef& ref, unsigned index) const;
    void EraseMarker(const TrackRef& ref, unsigned index);
    bool IsClipSelected(const TrackRef& ref, unsigned index) const;
    void EraseClip(const TrackRef& ref, unsigned index);
    /// Select every key row of the visible curves at the given timeline time.
    void SelectTimeRow(float time, bool add);
    /// Select the keys inside the rubber band.
    void SelectRect(const ImVec2& min, const ImVec2& max, bool add);
    /// Drop the selection entries that no longer address existing content.
    void ValidateSelection();
    void ClearSelection();
    /// Fit the visible time range to the content of the timeline.
    void FitToContent(const TimelineResource& resource);

    float TimeToX(float time) const;
    float XToTime(float x) const;
    float RowY(float index) const;
    float YToRow(float y) const;
    /// Frame quantized time of a mouse position: the sheet always works on whole frames.
    float SnapToFrame(float time) const;

    // Window state
    bool open_{};
    bool focusPending_{};
    bool fitPending_{};

    // Visible time range in seconds and its mapping to the canvas. The end is recomputed from the
    // canvas width every frame, so a resize never changes what is anchored at the left edge.
    float viewStartTime_{};
    float viewEndTime_{1.0f};
    float pixelsPerSecond_{40.0f};
    float frameLength_{1.0f / 60.0f};

    // Vertical scroll expressed in rows
    float scrollRows_{};

    // Row layout
    float rowHeight_{20.0f};
    float labelWidth_{220.0f};
    float rulerHeight_{22.0f};

    // Active canvas geometry of the current frame
    ImVec2 canvasMin_{};
    ImVec2 canvasMax_{};
    bool canvasHovered_{};

    // Content of the sheet, rebuilt every frame
    ea::vector<DopeRow> rows_;

    // Selection
    ea::vector<DopeKey> selectedKeys_;
    ea::vector<DopeMarker> selectedMarkers_;
    ea::vector<DopeClip> selectedClips_;
    ea::vector<CopiedKey> clipboard_;

    // Interaction
    DragMode dragMode_{DragMode::None};
    ImVec2 dragStartPos_{};
    /// Corners of the marquee while a rubber band selection is being dragged.
    ImVec2 selectRectMin_{};
    ImVec2 selectRectMax_{};
    /// Time offset previewed while the drag is running, applied on release.
    float dragTimeDelta_{};
    /// Row offset of a key drag, used to move keys onto another curve.
    int dragStartRow_{-1};
    int dragEndRow_{-1};
    float dragGrabTime_{};
    float panGrabX_{};
    float panOrigView_{};
    /// Row and snapped time that the open context menu targets.
    int popupRow_{-1};
    float popupTime_{};
};

}

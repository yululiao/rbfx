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

#include <Urho3D/SystemUI/SystemUI.h>
#include <Urho3D/Timeline/TimelinePropertyTrack.h>

#include <EASTL/vector.h>

namespace Urho3D
{

/// Standalone value-over-time window of a property clip. Owns only the view state of the canvas
/// (visible ranges, selected curve/component, active drag); the edited data and the undo actions
/// stay on the host side.
class TimelineCurveEditor
{
public:
    /// Show the window. When focus is requested it is raised and both axes are refitted, which is
    /// also what retargeting the window to another clip needs.
    void Open(bool focus = true);
    void Close();
    bool IsOpen() const { return open_; }

    /// Render the window for the given clip. Called every frame while IsOpen(); closes itself when
    /// the clip loses its curves.
    void Render(TimelinePropertyClip& propertyClip, TimelineEditorHost& host);

private:
    enum class DragMode
    {
        None,
        KeyMove,
        TangentOut,
        TangentIn,
        /// Rubber band over the canvas, selecting whole key rows on release.
        SelectRect,
    };

    /// How the components of a multi component curve are drawn.
    enum DisplayMode
    {
        /// Only the component picked in the combo. The classic view, best when one channel matters.
        DisplaySingle,
        /// Every component over each other in its own hue, the way Unity shows a Vector3.
        DisplayOverlay,
        DisplayCount,
    };

    /// Key held by the internal clipboard. The curve is addressed by name, because a paste into a
    /// clip whose curves were edited in the meantime still has to find the same attribute.
    struct CopiedKey
    {
        ea::string curve_;
        TimelinePropertyKey key_;
    };

    /// Key row held by the internal clipboard: every curve of the clip contributes its key, so a
    /// paste restores the whole row rather than one channel of it.
    struct CopiedRow
    {
        float time_{};
        ea::vector<CopiedKey> keys_;
    };

    void RenderLegend(TimelinePropertyClip& propertyClip);
    void RenderControls(const TimelinePropertyCurve& selectedCurve);
    /// Lay out the canvas, refresh the visible ranges and handle wheel zoom / middle button pan.
    void BeginCanvas(TimelinePropertyClip& propertyClip);
    void EndCanvas();
    void RenderCanvas(TimelinePropertyClip& propertyClip, TimelineEditorHost& host);
    void RenderContextMenu(TimelinePropertyClip& propertyClip, TimelineEditorHost& host);
    /// Keyboard shortcuts of the canvas: fit, zoom, delete and the row clipboard.
    void ProcessShortcuts(TimelinePropertyClip& propertyClip, TimelineEditorHost& host);

    // Key row selection. Rows are identified by their clip-local time, which is how every other
    // part of the editor addresses a key row.
    bool IsRowSelected(float time) const;
    /// Add the row to the selection. Without «toggle» the selection becomes exactly this row, with
    /// it the membership of the row is flipped.
    void SelectRow(float time, bool toggle);
    /// Add the row to the selection without ever removing anything. Used by the rubber band.
    void AddRow(float time);
    void ClearRowSelection();
    /// Shift every selected row by the same offset, skipping the rows that would land on an
    /// unselected one. Returns whether anything moved.
    bool MoveSelectedRows(TimelinePropertyClip& propertyClip, float offset);
    /// Drop the selected times that no longer have a key, and keep the anchor inside the selection.
    void ValidateRowSelection(TimelinePropertyClip& propertyClip);
    /// Select the rows whose keys fall inside the rubber band.
    void SelectRectRows(TimelinePropertyClip& propertyClip, const ImVec2& min, const ImVec2& max, bool add);
    /// Apply one of the tangent modes to the selected rows.
    void ApplyTangentMode(TimelinePropertyClip& propertyClip, int mode);

    // Canvas coordinate mapping, valid between BeginCanvas and EndCanvas.
    float TimeToX(float localTime) const;
    float XToTime(float x) const;
    float ValueToY(float value) const;
    float YToValue(float y) const;

    // Window state
    bool open_{};
    bool focusPending_{};

    // What is displayed
    unsigned selectedCurveIndex_{M_MAX_UNSIGNED};
    int componentIndex_{};
    int displayMode_{DisplaySingle};

    // Selected key rows, clip-local times in ascending order. The host keeps the same information
    // for the properties panel; here it is the anchor of the selection, used by the paste.
    ea::vector<float> selectedRows_;
    ea::vector<CopiedRow> clipboard_;

    // Vertical range of the canvas
    bool autoFit_{true};
    float viewMin_{};
    float viewMax_{1.0f};

    // Horizontal range in clip-local time: the canvas zooms independently of the sequencer,
    // otherwise a long clip would collapse into a few pixels and ramps into vertical lines.
    bool timeAutoFit_{true};
    float timeMin_{};
    float timeMax_{1.0f};
    bool timePanning_{};

    // Active canvas geometry of the current frame
    ImVec2 canvasMin_{};
    ImVec2 canvasMax_{};
    float pxPerSecond_{};
    bool canvasHovered_{};

    // Interaction
    DragMode dragMode_{DragMode::None};
    unsigned dragCurve_{M_MAX_UNSIGNED};
    float dragKeyTime_{};
    /// Time the key row drag started from, so the batch move applies one offset to every row.
    float dragOriginTime_{};
    /// Slot of the grabbed row in the selection list, which stays valid through the batch move.
    unsigned dragRowIndex_{M_MAX_UNSIGNED};
    /// Offset the drag has already applied, so each frame only the remaining step is moved.
    float dragAppliedOffset_{};
    /// Value under the cursor on the previous frame of a vertical drag: the rows are shifted by the
    /// step, which keeps their relative shape without storing their original values.
    float dragLastValue_{};
    /// Corners of the rubber band while it is being dragged.
    ImVec2 marqueeMin_{};
    ImVec2 marqueeMax_{};
    // Right-click context of the canvas: time at the cursor and whether a key was hit.
    float popupTime_{};
    bool popupOnKey_{};
};

}

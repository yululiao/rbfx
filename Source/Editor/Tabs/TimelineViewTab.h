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

#include "ResourceEditorTab.h"

#include <Urho3D/Core/Variant.h>
#include <Urho3D/Timeline/TimelineDefs.h>

#include <EASTL/unordered_map.h>
#include <EASTL/set.h>
#include <EASTL/utility.h>

namespace Urho3D
{

class ModifyResourceAction;
class Node;
class ProjectRequest;
struct TimelineClip;
class TimelineMarkerTrack;
class TimelinePlayer;
class TimelinePropertyTrack;
class TimelineResource;
class TimelineTrack;

void Tabs_TimelineViewTab(Context* context, Project* project);

/// Tab that displays and edits Timeline resources.
/// Shows track headers on the left and sequencer canvas on the right.
/// Clips of all track types are displayed as colored blocks,
/// keyframes of Property tracks and markers as diamonds.
class TimelineViewTab : public ResourceEditorTab
{
    URHO3D_OBJECT(TimelineViewTab, ResourceEditorTab);

public:
    explicit TimelineViewTab(Context* context);
    ~TimelineViewTab() override;

    /// ResourceEditorTab implementation
    /// @{
    ea::string GetResourceTitle() override { return "Timeline"; }
    bool SupportMultipleResources() override { return false; }
    bool CanOpenResource(const ResourceFileDescriptor& desc) override;
    /// @}

protected:
    /// ResourceEditorTab implementation
    /// @{
    void OnResourceLoaded(const ea::string& resourceName) override;
    void OnResourceUnloaded(const ea::string& resourceName) override;
    void OnActiveResourceChanged(const ea::string& oldResourceName, const ea::string& newResourceName) override;
    void OnResourceSaved(const ea::string& resourceName) override;
    void OnResourceShallowSaved(const ea::string& resourceName) override;
    void RenderContent() override;
    /// @}

private:
    // Coordinate helpers
    float TimeToX(float time, float trackX0) const;
    float XToTime(float x, float trackX0) const;

    // Rendering
    void RenderToolbar();
    /// Play/Pause/Stop and Record buttons followed by the recording state.
    void RenderPlaybackControls();
    void RenderSequencer();
    void RenderRuler(float duration, float majorStep, float minorStep, float trackX0, float trackX1);
    /// Frame-number band above the time ruler. Shares the horizontal mapping and scrub behavior.
    void RenderFrameRuler(float duration, float trackX0, float trackX1);
    void RenderBody(float duration, float majorStep, float minorStep);
    /// Background of the body: grid lines, duration shading and header background.
    void RenderBodyGrid(float duration, float majorStep, float minorStep, const ImVec2& canvasPos, float trackX0,
        float trackX1, const ImVec2& visibleMin, const ImVec2& visibleMax);
    void RenderTrackHeader(TimelineTrack* track, unsigned trackIndex, const ImVec2& rowMin, const ImVec2& rowMax,
        float trackX0, bool rowSelected, bool windowHovered);
    void RenderTrackContent(TimelineTrack* track, unsigned trackIndex, const ImVec2& rowMin, const ImVec2& rowMax,
        float trackX0, float trackX1, bool rowSelected);
    /// Drop target over the clip area of the row: a dropped Animation resource becomes a clip.
    void RenderTrackDropTarget(TimelineTrack* track, unsigned trackIndex, const ImVec2& contentMin,
        const ImVec2& contentMax, float trackX0);
    void RenderSelectionPanel();
    void RenderTrackProperties(TimelineTrack* track);
    void RenderClipProperties(TimelineTrack* track);
    void RenderMarkerProperties(TimelineTrack* track);
    void RenderPropertyKeyAndCurves(TimelineTrack* track);

    // Interaction
    /// Drop all selection.
    void ClearSelection();
    /// Select the track, dropping clip/key selection.
    void SelectTrack(unsigned trackIndex);
    /// Return the clip at (track, clip) indices of the displayed timeline, or null when out of range.
    TimelineClip* FindSelectedClip(unsigned trackIndex, unsigned clipIndex);
    /// Copy the multi-selected clips (or the single selected clip) to the clipboard.
    void CopySelectedClips();
    /// Paste the clipboard clips onto the selected track and select the pasted clips.
    void PasteClips(float pasteTime);
    /// Hit-test a single row: updates selection, drag state and context menu requests.
    void ProcessRowHitTesting(TimelineTrack* track, unsigned trackIndex, const ImVec2& rowMin, const ImVec2& rowMax,
        float trackX0);
    /// Hit-test markers of a Marker track row. Returns whether a marker was hit.
    bool ProcessMarkerHitTesting(TimelineMarkerTrack* markerTrack, unsigned trackIndex,
        const ImVec2& rowMin, const ImVec2& rowMax, float trackX0, bool leftClick, bool rightClick);
    /// Hit-test keyframes of a Property track row. Returns whether a key was hit.
    bool ProcessPropertyKeyHitTesting(TimelinePropertyTrack* propertyTrack, unsigned trackIndex,
        const ImVec2& rowMin, const ImVec2& rowMax, float trackX0, bool leftClick);
    /// Hit-test clips of a clip track row. Returns whether a clip was hit.
    bool ProcessClipHitTesting(TimelineTrack* track, unsigned trackIndex, const ImVec2& rowMin, const ImVec2& rowMax,
        float trackX0, bool leftClick, bool rightClick);
    /// Apply the active drag to the timeline.
    void UpdateDrag(float trackX0);
    /// Delete the selected clip, key or marker with the Delete key.
    void ProcessDeleteKey();
    /// Handle clipboard shortcuts (Ctrl+C copy, Ctrl+V paste) over the focused sequencer.
    void ProcessEditKeys();
    /// Render context menus opened from the sequencer.
    void RenderTrackAreaContextMenu();
    void RenderClipContextMenu();
    void RenderTrackHeaderContextMenu();

    // Helpers
    /// InputText that commits the value (and an undo action) only on Enter.
    bool EditString(const char* label, ea::string& value);
    /// Begin undoable modification of the timeline. No-op if modification is already in progress.
    void BeginModify();
    /// Drop selection if indices are out of bounds after undo/redo.
    void ValidateSelection();

    // Node binding
    /// Follow the node selection of the scene: create a timeline for it or open its timeline.
    void OnProjectRequest(ProjectRequest* request);
    /// Open the timeline of the selected node's TimelinePlayer, if any.
    void SyncWithSelectedNode();
    /// Empty state: offer to create a timeline for the selected node.
    void RenderCreateState();
    /// Create a timeline file, bind it to the selected node and start editing it.
    void CreateTimelineForNode();

    // Preview and recording
    /// TimelinePlayer of the context node playing the displayed timeline, if any.
    TimelinePlayer* GetContextPlayer() const;
    /// Start the preview session on demand (Player::Play plus pause for scrubbing).
    TimelinePlayer* EnsurePreviewPlayer();
    /// Advance the preview clock and sample attributes while recording.
    void UpdatePreviewAndRecording();
    /// Stop preview. Player::Stop releases instances and restores animated attributes.
    void StopPreview();
    /// Store attributes changed in the Inspector as keys at the cursor.
    void UpdateRecording(TimelinePlayer* player);

    // View state
    /// Reset zoom, cursor and selection for the currently displayed resource.
    void ResetViewState();
    float viewStartTime_{};        // Time at the left edge of the canvas
    float pixelsPerSecond_{100.0f};
    float cursorTime_{};           // Scrub position
    /// Frame rate of the frame ruler, used to convert seconds to whole frames.
    float fps_{60.0f};

    // Selection. Clip/key indices are relative to the selected track.
    unsigned selectedTrack_{M_MAX_UNSIGNED};
    unsigned selectedClip_{M_MAX_UNSIGNED};
    unsigned selectedKey_{M_MAX_UNSIGNED};     // Marker of the selected Marker track
    float selectedKeyTime_{-1.0f};             // Local time of the selected Property key
    // Set of (track, clip) pairs held by the multi-selection (Ctrl+click toggles, dragging one
    // moves them all). Kept in sync with selectedTrack_/selectedClip_ for the single-selection path.
    ea::set<ea::pair<unsigned, unsigned>> multiSelectedClips_;

    // Height of the bottom properties panel. Dragging the splitter between the sequencer and
    // the panel resizes it, so several property rows can be visible at once.
    float selectionPanelHeight_{160.0f};
    bool draggingPanelSplitter_{false};

    // Clip clipboard: a detached track of the copied type holding the copied clips, so pasting
    // is independent of the source track's lifetime and only same-type tracks accept it.
    SharedPtr<TimelineTrack> clipboardTrack_;
    ea::string clipboardTrackType_;

    // Active drag
    enum class DragKind
    {
        None,
        Cursor,
        ClipMove,
        ClipTrimStart,
        ClipTrimEnd,
        Key,
        Marker,
        Pan,
        TrackReorder,
    };
    DragKind dragKind_{DragKind::None};
    unsigned dragTrack_{M_MAX_UNSIGNED};
    unsigned dragClip_{M_MAX_UNSIGNED};
    unsigned dragKey_{M_MAX_UNSIGNED};
    float dragGrabOffset_{};       // Offset between mouse time and dragged item start
    float dragReorderGrab_{};      // Vertical offset between mouse Y and the dragged track row top
    float dragOrigStart_{};
    float dragOrigDuration_{};
    // Batch clip move: original start of every selected clip at drag start, keyed by (track, clip).
    ea::vector<ea::pair<ea::pair<unsigned, unsigned>, float>> dragBatchOrig_;

    SharedPtr<TimelineResource> timeline_;
    SharedPtr<ModifyResourceAction> pendingAction_;

    // Node selection followed by the tab (weak: the node may be removed)
    WeakPtr<Node> contextNode_;
    bool subscribedToProject_{};
    /// Set when the node selection changed and the view must re-sync on render.
    bool pendingNodeSync_{};
    /// Selection that is applied on mouse release: clicking a node starts both selection and drag&drop.
    WeakPtr<Node> pendingContextNode_;
    bool pendingSyncWaitsForRelease_{};

    // Preview. The editor does not run scene updates, so the tab drives the timeline itself.
    enum class PreviewState
    {
        Off,
        Playing,
        Paused,
    };
    PreviewState previewState_{PreviewState::Off};
    /// Player that owns the preview session. Stop() finalizes it when the preview ends.
    WeakPtr<TimelinePlayer> previewPlayer_;
    /// The cursor was moved this frame (scrub) and must be applied to the scene.
    bool cursorScrubbed_{};
    double previewLastTime_{};

    // Recording: attributes changed in the Inspector become keys at the cursor.
    bool recording_{};
    /// Last sampled value of every recorded attribute, keyed by target and attribute name.
    ea::unordered_map<ea::string, Variant> recordedValues_;

    // Context menu state
    unsigned popupTrack_{M_MAX_UNSIGNED};
    unsigned popupClip_{M_MAX_UNSIGNED};
    float popupTime_{};

    // Input buffers for adding a new property curve
    char newCurveName_[64]{};
    int newCurveType_{};

    // Feedback of the binding slot
    ea::string bindingError_;
    double bindingErrorTime_{};
};

}

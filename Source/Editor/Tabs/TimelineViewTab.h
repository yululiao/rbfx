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

#include "../Timeline/TimelineCurveEditor.h"
#include "../Timeline/TimelineDopeSheet.h"
#include "../Timeline/TimelineEditorCommon.h"
#include "../Timeline/TimelineEditorHost.h"
#include "../Timeline/TimelineTrackRows.h"

#include <Urho3D/Core/Variant.h>
#include <Urho3D/Timeline/TimelineDefs.h>

#include <EASTL/unordered_map.h>
#include <EASTL/set.h>
#include <EASTL/utility.h>

namespace Urho3D
{

class ModifyResourceAction;
class Node;
class Texture2D;
class ProjectRequest;
struct TimelineClip;
struct TimelinePropertyClip;
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
/// Implements the services the floating windows (curve editor, dope sheet) need from the tab.
class TimelineViewTab : public ResourceEditorTab, private TimelineEditorHost
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
    /// Unity-style zoom bar at the bottom-left: drag the middle to pan, drag the ends to zoom.
    void RenderZoomSlider();
    void RenderRuler(float duration, float majorStep, float minorStep, float trackX0, float trackX1);
    /// Frame-number band above the time ruler. Shares the horizontal mapping and scrub behavior.
    void RenderFrameRuler(float duration, float trackX0, float trackX1);
    /// Thin lane above the ruler that owns the loop region: drag it to define the region, drag the
    /// handles to resize it. Kept apart from the ruler so scrubbing and loop editing never fight.
    void RenderLoopLane(float duration, float trackX0, float trackX1);
    /// Context menu of the loop region, opened from the loop lane and from the time ruler.
    void RenderLoopContextMenu();
    /// Collapsible marker list between the sequencer and the properties panel.
    void RenderMarkersPanel();
    void RenderBody(float duration, float majorStep, float minorStep);
    /// Background of the body: grid lines, duration shading and header background.
    void RenderBodyGrid(float duration, float majorStep, float minorStep, const ImVec2& canvasPos, float trackX0,
        float trackX1, const ImVec2& visibleMin, const ImVec2& visibleMax);
    void RenderTrackHeader(TimelineTrack* track, const TrackRow& row, const ImVec2& rowMin, const ImVec2& rowMax,
        float trackX0, bool rowSelected, bool windowHovered);
    void RenderTrackContent(TimelineTrack* track, const TrackRef& trackRef, const ImVec2& rowMin, const ImVec2& rowMax,
        float trackX0, float trackX1, bool rowSelected);
    /// Drop target over the clip area of the row: a dropped Animation resource becomes a clip.
    void RenderTrackDropTarget(TimelineTrack* track, const ImVec2& contentMin, const ImVec2& contentMax,
        float trackX0);
    void RenderSelectionPanel();
    void RenderTrackProperties(TimelineTrack* track);
    void RenderClipProperties(TimelineTrack* track);
    /// Fields of the selected clip that only exist on one track type: assigned resources,
    /// blending and payload. Rendered below the common clip fields, one line per field.
    void RenderClipTypeFields(TimelineTrack* track);
    /// Text field of a clip resource that takes a drop of the given object type from the Project
    /// browser. Writes the dropped resource name to «result» and returns whether it changed.
    bool EditResourceDrop(const char* label, const ea::string& path, StringHash objectType, ea::string& result);
    /// Editable name/type/value table of a signal clip payload.
    void RenderVariantMapRows(VariantMap& data);
    void RenderMarkerProperties(TimelineTrack* track);
    void RenderPropertyKeyAndCurves(TimelineTrack* track);

    // Interaction
    /// Drop all selection.
    void ClearSelection();
    /// Select the track, dropping clip/key selection.
    void SelectTrack(const TrackRef& trackRef);
    /// Return the clip at (track, clip) of the displayed timeline, or null when out of range.
    TimelineClip* FindSelectedClip(const TrackRef& trackRef, unsigned clipIndex);
    /// Property clip currently selected in the sequencer, target of the curve editor window.
    TimelinePropertyClip* FindSelectedPropertyClip();
    /// Copy the multi-selected clips (or the single selected clip) to the clipboard.
    void CopySelectedClips();
    /// Paste the clipboard clips onto the selected track and select the pasted clips.
    void PasteClips(float pasteTime);
    /// Hit-test a single row: updates the collapse state, selection, drag state and context menu
    /// requests. Takes the row instead of the track because the expand/collapse control of a group
    /// is part of the row geometry and must be measured before the header drag begins.
    void ProcessRowHitTesting(const TrackRow& row, const ImVec2& rowMin, const ImVec2& rowMax, float trackX0);
    /// Hit-test markers of a Marker track row. Returns whether a marker was hit.
    bool ProcessMarkerHitTesting(TimelineMarkerTrack* markerTrack, const TrackRef& trackRef,
        const ImVec2& rowMin, const ImVec2& rowMax, float trackX0, bool leftClick, bool rightClick);
    /// Hit-test keyframes of a Property track row. Returns whether a key was hit.
    bool ProcessPropertyKeyHitTesting(TimelinePropertyTrack* propertyTrack, const TrackRef& trackRef,
        const ImVec2& rowMin, const ImVec2& rowMax, float trackX0, bool leftClick);
    /// Hit-test clips of a clip track row. Returns whether a clip was hit.
    bool ProcessClipHitTesting(TimelineTrack* track, const TrackRef& trackRef, const ImVec2& rowMin,
        const ImVec2& rowMax, float trackX0, bool leftClick, bool rightClick);
    /// Apply the active drag to the timeline.
    void UpdateDrag(float trackX0);
    /// Move the play head and keep it inside the view, so keyboard navigation never loses the
    /// cursor off screen. Marks the scene for a preview update like scrubbing does.
    void SetCursorTime(float time);
    /// Display unit of the ruler and the cursor readout.
    TimelineTimeUnit TimeUnit() const { return timeDisplayMode_ == 1 ? TimelineTimeUnit::Frames : TimelineTimeUnit::Seconds; }
    /// Time formatted in the current display unit, using the frame rate of the resource.
    ea::string FormatDisplayTime(float time) const { return FormatTime(time, TimeUnit(), fps_); }
    /// Snap to the grid of the current display unit and to the enabled candidate points. Shadows
    /// the free SnapTime of the shared header inside the tab, which is how every drag in the
    /// sequencer picks up the key and clip edges without passing them explicitly.
    float SnapTime(float time, bool snapEnabled) const;
    /// Sorted, distinct time points of the visible rows. The two flags choose which sets
    /// contribute: snapping passes the toolbar toggles, the bracket keys pass the key set alone.
    void CollectSnapCandidates(ea::vector<float>& candidates, bool clipEdges, bool keyPoints) const;
    /// Time points the active drag owns: its own edges, its own key row, its own marker. They are
    /// removed from the candidates, because an item that snaps to itself cannot reach anything else.
    ea::vector<float> DraggedSnapPoints() const;
    /// Next or previous keyframe or marker of the whole timeline after «time», or «time» itself
    /// when there is none. Used by the bracket keys.
    float StepToNextKey(float time, bool forward) const;
    /// Zoom and pan so that the selected clips fill the canvas, falling back to the loop region
    /// and then to the whole timeline.
    void FitViewToSelection();
    /// Insert a key row into the selected property clip at the play head: the keyboard shortcut of
    /// the same name, and the fastest way to author keys because the cursor is already there.
    void AddKeyRowAtCursor();
    /// Delete the selected clip, key or marker with the Delete key.
    void ProcessDeleteKey();
    /// Handle clipboard shortcuts (Ctrl+C copy, Ctrl+V paste) over the focused sequencer.
    void ProcessEditKeys();
    /// Split the selected clips at the given timeline time, keeping the evaluated content whole.
    void SplitSelectedClips(float splitTime);
    /// Duplicate the selected clip right after itself.
    void DuplicateSelectedClip();
    /// Clips a batch command applies to: the multi-selection, or the single selected clip.
    ea::vector<ClipKey> SelectedClipKeys() const;
    /// Modal-ish window that enters a clip playback speed, opened from the clip context menu.
    void RenderClipSpeedDialog();
    /// Render context menus opened from the sequencer.
    void RenderTrackAreaContextMenu();
    void RenderClipContextMenu();
    void RenderTrackHeaderContextMenu();

    // Helpers
    /// InputText that commits the value (and an undo action) only on Enter.
    bool EditString(const char* label, ea::string& value);
    /// Begin undoable modification of the timeline. No-op if modification is already in progress.
    /// Also the TimelineEditorHost service of the floating windows.
    void BeginModify() override;
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
    /// Store attributes changed in the Inspector or by gizmos as keys at the cursor.
    void UpdateRecording(TimelinePlayer* player);
    /// Property clip covering the cursor with its local time, creating one when the first
    /// change needs a target.
    TimelinePropertyClip* FindOrCreateClipAtCursor(TimelinePropertyTrack& track, float& localTime);
    /// Report to the console which objects a recording take can actually capture.
    void LogRecordingSetup(TimelinePlayer* player);
    /// Add one resolved recording target to the toolbar hint text.
    void AppendRecordingTargetHint(unsigned trackIndex, const ea::string& description);

    /// TimelineEditorHost implementation of the floating editing windows
    float GetCursorTime() const override { return cursorTime_; }
    float GetSelectedKeyTime() const override { return selectedKeyTime_; }
    void SetSelectedKeyTime(float localTime) override { selectedKeyTime_ = localTime; }

    // Row model of the track hierarchy
    /// Rebuild the visible rows of the hierarchy. Called once per frame before layout, so the row
    /// rects used for rendering and for hit-testing always describe the same tracks.
    void RebuildRows();
    /// Index of the row of the track in rows_, or -1 when the track has no visible row.
    int RowIndexOf(const TrackRef& trackRef) const;
    /// Create a track of the given type and insert it at the end of «parent»'s child list, or at
    /// the top level when the parent reference is invalid.
    void AddTrackOfType(const ea::string& typeName, const TrackRef& parent);
    /// Delete a track. A group track is unwrapped: its children move up to its own position.
    void RemoveTrackAt(const TrackRef& trackRef);

    // Inline header controls of the rows (mute / solo / lock)
    /// Whether the track is soloed. Soloing is exclusive: while any track is soloed every track
    /// that is not soloed is silenced in the preview player.
    bool IsTrackSoloed(const TrackRef& trackRef) const;
    /// Flip the solo state of the track and refresh the silence overrides of the preview.
    void ToggleTrackSolo(const TrackRef& trackRef);
    /// Whether the track is protected from selection and from every drag edit.
    bool IsTrackLocked(const TrackRef& trackRef) const;
    /// Flip the edit lock of the track.
    void ToggleTrackLocked(const TrackRef& trackRef);
    /// Whether the row is hoisted to the top of the body while the rest of the hierarchy scrolls.
    bool IsTrackPinned(const TrackRef& trackRef) const;
    /// Flip the pinned state of the track header.
    void ToggleTrackPinned(const TrackRef& trackRef);
    /// Push the solo set onto the preview player as track silence overrides.
    void ApplySoloOverrides();
    /// Drop solo and lock state, called when the displayed timeline changes or the preview ends.
    void ClearOverlayState();

    // View state
    /// Reset zoom, cursor and selection for the currently displayed resource.
    void ResetViewState();
    float viewStartTime_{};        // Time at the left edge of the canvas
    float pixelsPerSecond_{100.0f};
    float cursorTime_{};           // Scrub position
    /// Frame rate of the frame ruler, used to convert seconds to whole frames.
    float fps_{60.0f};

    // Zoom bar interaction state.
    enum class ZoomDragMode
    {
        None,
        Pan,
        LeftEdge,
        RightEdge
    };
    ZoomDragMode zoomDragMode_{ZoomDragMode::None};

    // Selection. Clip/key indices are relative to the selected track.
    TrackRef selectedTrack_;
    unsigned selectedClip_{M_MAX_UNSIGNED};
    unsigned selectedKey_{M_MAX_UNSIGNED};     // Marker of the selected Marker track
    float selectedKeyTime_{-1.0f};             // Local time of the selected Property key
    // Clips held by the multi-selection (Ctrl+click toggles, dragging one moves them all). Kept in
    // sync with selectedTrack_/selectedClip_ for the single-selection path.
    ClipSelection multiSelectedClips_;

    // Visible rows of the track hierarchy, rebuilt every frame by RebuildRows().
    ea::vector<TrackRow> rows_;
    // Groups whose children are hidden. Session only: collapsing is a view preference and the
    // timeline resource has no field for it.
    ea::vector<TrackRef> collapsedTracks_;
    // Soloed tracks. Session only: the mute flag of a track is stored in the resource, its solo is
    // not, so the state lives here and is mirrored onto the preview player as silence overrides.
    ea::vector<TrackRef> soloedTracks_;
    // Tracks protected from selection and drag edits. Session only, like the collapse state.
    ea::vector<TrackRef> lockedTracks_;
    // Tracks whose row is drawn in a sticky band at the top of the body. Session only as well:
    // which rows an editor wants pinned is a view preference, not timeline content.
    ea::vector<TrackRef> pinnedTracks_;

    // Ruler unit, snapping and preview playback. None of this is stored in the resource: it is how
    // the editor displays and drives the same content.
    /// Unit of the ruler labels and the cursor readout, see TimelineTimeUnit.
    int timeDisplayMode_{};
    /// Snap drags to the keyframe and marker times of the timeline. Off by default because those
    /// move with the content and can grab an edit the user did not aim for.
    bool snapToKeys_{};
    /// Snap drags to the clip edges. On by default: the clip boundaries are the grid a sequence is
    /// assembled on.
    bool snapToClips_{true};
    /// Loop the preview inside the region below instead of stopping at the end of the timeline.
    bool loopPreview_{};
    float loopStart_{};
    float loopEnd_{};
    /// Which part of the loop region the drag over the loop lane is editing.
    enum class LoopDragMode
    {
        None,
        Create,
        Region,
        Start,
        End,
    };
    LoopDragMode loopDragMode_{LoopDragMode::None};
    float loopDragGrab_{};
    float loopDragOrigStart_{};
    float loopDragOrigEnd_{};
    /// Expanded state of the marker list between the sequencer and the properties panel.
    bool markersPanelOpen_{true};
    /// Preview playback speed multiplier. Applied to the clock the tab owns, and mirrored onto the
    /// player so that a scene update, when there is one, advances at the same rate.
    float playbackSpeed_{1.0f};

    // Curve editor window of the selected property clip. The canvas state and rendering live in
    // TimelineCurveEditor; the tab only opens, closes and feeds it.
    TimelineCurveEditor curveEditor_;

    // Dope sheet window: the time over rows overview of the whole hierarchy. Like the curve
    // editor it keeps its own view state and works through the host interface only.
    TimelineDopeSheet dopeSheet_;

    // Height of the bottom properties panel. Dragging the splitter between the sequencer and
    // the panel resizes it, so several property rows can be visible at once.
    float selectionPanelHeight_{160.0f};
    bool draggingPanelSplitter_{false};

    // When locked, selecting other nodes in the scene does not retarget the window: the
    // context node and the displayed timeline stay as they are until unlocked.
    bool bindingLocked_{false};
    // Toolbar toggle icons (EditorData Textures/lock.png and lunock.png), loaded lazily.
    SharedPtr<Texture2D> lockIcon_;
    SharedPtr<Texture2D> unlockIcon_;

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
        EaseIn,
        EaseOut,
    };
    DragKind dragKind_{DragKind::None};
    TrackRef dragTrack_;
    unsigned dragClip_{M_MAX_UNSIGNED};
    unsigned dragKey_{M_MAX_UNSIGNED};
    float dragGrabOffset_{};       // Offset between mouse time and dragged item start
    float dragReorderGrab_{};      // Vertical offset between mouse Y and the dragged track row top
    float dragOrigStart_{};
    float dragOrigDuration_{};
    float dragOrigClipIn_{};       // In point of the trimmed clip at drag start, for Alt trimming
    // Batch clip move: original start of every selected clip at drag start, keyed by clip.
    ea::vector<ea::pair<ClipKey, float>> dragBatchOrig_;

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
    /// The cursor was moved by a command rather than by a drag, so the view has to be scrolled to
    /// keep it inside the canvas. Consumed by the sequencer, which is where the geometry is known.
    bool keepCursorVisible_{};
    double previewLastTime_{};

    // Recording: attributes changed in the Inspector or by gizmos become keys at the cursor.
    bool recording_{};
    /// Sampled values of a recorded attribute: the baseline captured when recording started
    /// (used for the automatic key at the clip start) and the last written value.
    struct RecordedChannel
    {
        Variant baseline_;
        Variant last_;
    };
    /// Last sampled value of every recorded attribute, keyed by target and attribute name.
    ea::unordered_map<ea::string, RecordedChannel> recordedValues_;
    /// Number of keys inserted during the current take, shown next to the REC indicator.
    unsigned recordedKeyCount_{};
    /// Human readable list of the objects the current take captures.
    ea::string recordingTargets_;

    // Context menu state
    TrackRef popupTrack_;
    unsigned popupClip_{M_MAX_UNSIGNED};
    float popupTime_{};

    // Clip speed entry of the clip context menu: which clip it edits and whether the block keeps
    // its length while the content changes speed.
    bool speedEditOpen_{};
    TrackRef speedEditTrack_;
    unsigned speedEditClip_{M_MAX_UNSIGNED};
    bool speedEditKeepDuration_{true};
    float speedEditValue_{1.0f};
    /// Raise the dialog once, when it was opened from the context menu.
    bool speedEditFocusPending_{};

    // Input buffers for adding a new property curve
    char newCurveName_[64]{};
    int newCurveType_{};

    // Feedback of the binding slot
    ea::string bindingError_;
    double bindingErrorTime_{};
};

}

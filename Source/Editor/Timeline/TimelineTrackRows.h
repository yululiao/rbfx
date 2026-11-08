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

#include <Urho3D/Core/StringUtils.h>
#include <Urho3D/Math/MathDefs.h>
#include <Urho3D/Timeline/TimelineActivationTrack.h>
#include <Urho3D/Timeline/TimelineAnimationTrack.h>
#include <Urho3D/Timeline/TimelineAudioTrack.h>
#include <Urho3D/Timeline/TimelineControlTrack.h>
#include <Urho3D/Timeline/TimelineMarkerTrack.h>
#include <Urho3D/Timeline/TimelinePropertyTrack.h>
#include <Urho3D/Timeline/TimelineSignalTrack.h>
#include <Urho3D/Timeline/TimelineTrack.h>

#include <EASTL/vector.h>

namespace Urho3D
{

class TimelineResource;
class TimelineTrack;

// Track hierarchy model of the editor. Group tracks may contain other group tracks, so a track
// cannot be addressed by a single index into the resource: it is addressed by the chain of child
// indices leading from the top level track list down to it. These helpers are pure data layer:
// they never touch widget state and never start an undo action.

/// Path based reference of a track. An invalid reference (zero depth) plays the role of the
/// "nothing selected" value the flat index based model used M_MAX_UNSIGNED for.
struct TrackRef
{
    /// Deepest nesting the editor can address. Tracks below this depth are not listed as rows.
    static constexpr unsigned kMaxDepth = 8;

    TrackRef() = default;

    /// Reference of the top level track at the given index.
    static TrackRef AtRoot(unsigned index);

    /// Whether the reference addresses a track (i.e. is not the "none" value).
    bool Valid() const { return depth_ != 0 && depth_ <= kMaxDepth; }
    /// Number of tracks on the way down, one for a top level track.
    unsigned Depth() const { return depth_; }
    /// Index inside the top level track list.
    unsigned RootIndex() const { return depth_ != 0 ? path_[0] : M_MAX_UNSIGNED; }
    /// Index inside the list the track itself belongs to.
    unsigned LeafIndex() const { return depth_ != 0 ? path_[depth_ - 1] : M_MAX_UNSIGNED; }
    /// Index at the given depth of the path.
    unsigned operator[](unsigned level) const { return level < depth_ ? path_[level] : M_MAX_UNSIGNED; }
    /// Reference of the enclosing group track, invalid for a top level track.
    TrackRef Parent() const;
    /// Reference of a child of this track, invalid when the nesting limit is reached.
    TrackRef Child(unsigned index) const;
    /// Reference of the ancestor of this path at the given depth (0 = invalid, Depth() = this).
    TrackRef Prefix(unsigned depth) const;
    /// Whether «this» is the same track as «other» or one of its ancestors. Used to refuse moving
    /// a group into itself or into one of its own children.
    bool IsSelfOrAncestorOf(const TrackRef& other) const;
    /// Three-way comparison, ordered so that a parent precedes its children and siblings keep their
    /// relative order. Used to batch edits per track without disturbing the sequence.
    int Compare(const TrackRef& other) const;
    bool operator==(const TrackRef& other) const { return Compare(other) == 0; }
    bool operator!=(const TrackRef& other) const { return Compare(other) != 0; }
    bool operator<(const TrackRef& other) const { return Compare(other) < 0; }
    /// "1/0/2" style address, shown in tooltips and log messages.
    ea::string ToString() const;

    /// Index chain from the top level down to the track. Only the first depth_ entries matter.
    unsigned path_[kMaxDepth]{};
    /// Number of significant entries of path_.
    unsigned depth_{};
};

/// One visible row of the sequencer: a track of the hierarchy together with its display context.
struct TrackRow
{
    /// Address of the track.
    TrackRef ref_;
    /// Track the row shows. Null when the address became stale (undo/redo of a track removal).
    TimelineTrack* track_{};
    /// Nesting level of the row, zero for a top level track. Drives the header indentation.
    unsigned depth_{};
    /// Whether the track is a group track, which owns the expand/collapse control.
    bool isGroup_{};
    /// Whether the group is collapsed, i.e. its children are hidden.
    bool collapsed_{};
};

/// A clip addressed by its owning track and its index within that track.
struct ClipKey
{
    TrackRef track_;
    unsigned clip_{};

    bool operator==(const ClipKey& other) const { return track_ == other.track_ && clip_ == other.clip_; }
    bool operator!=(const ClipKey& other) const { return !(*this == other); }
};

/// Multi-selection of clips. Held as a vector instead of a set because the items are compared by
/// value and their count is small; insertion order is preserved so batch operations stay stable.
struct ClipSelection
{
    bool Contains(const TrackRef& track, unsigned clip) const;
    /// Append the clip to the selection. Returns whether it was not selected yet.
    bool Insert(const TrackRef& track, unsigned clip);
    /// Remove the clip from the selection. Returns whether it was selected.
    bool Erase(const TrackRef& track, unsigned clip);
    void Clear() { keys_.clear(); }
    unsigned Size() const { return static_cast<unsigned>(keys_.size()); }
    bool Empty() const { return keys_.empty(); }
    const ea::vector<ClipKey>& Keys() const { return keys_; }

    /// Selected clips in insertion order.
    ea::vector<ClipKey> keys_;
};

/// Return the track addressed by the reference, or null when the reference is stale.
TimelineTrack* TrackAt(const TimelineResource* resource, const TrackRef& ref);

/// Address the track by searching the hierarchy depth-first. Returns false when the track does not
/// belong to the resource.
bool FindTrackRef(const TimelineResource* resource, const TimelineTrack* track, TrackRef& result);

/// Depth-first, top-to-bottom list of the visible rows. Children of a collapsed group are left out.
/// The result is what the sequencer lays out and hit-tests, so every hit lands on a visible row.
void BuildTrackRows(const TimelineResource* resource, const ea::vector<TrackRef>& collapsed,
    ea::vector<TrackRow>& result);

/// BuildTrackRows with an empty collapse set: every track of the hierarchy, groups included. Used
/// by the preview and the recorder, which must see the nested tracks as well.
void FlattenTracks(const TimelineResource* resource, ea::vector<TrackRow>& result);

/// Whether the reference is an element of the list. The lists are short, so a linear search beats
/// any ordering the editor would have to maintain for a lookup.
bool ContainsRef(const ea::vector<TrackRef>& refs, const TrackRef& ref);

/// Add the reference to the list, or remove it when it is already there. Returns the new state.
bool ToggleRef(ea::vector<TrackRef>& refs, const TrackRef& ref);

bool IsCollapsed(const ea::vector<TrackRef>& collapsed, const TrackRef& ref);
/// Flip the collapse state of a group. Returns the new state.
bool ToggleCollapsed(ea::vector<TrackRef>& collapsed, const TrackRef& ref);

/// Drop the references of the list that no longer address a track, so indices of a rebuilt track
/// list are never reinterpreted as the old, unrelated tracks. Used by the session only view state
/// of the editor: collapsed groups, soloed and locked tracks.
void PruneStaleRefs(ea::vector<TrackRef>& refs, const TimelineResource* resource);

/// PruneStaleRefs of a collapse set, named the way the view state of the sequencer calls it.
void PruneCollapsed(ea::vector<TrackRef>& collapsed, const TimelineResource* resource);

/// Remove the track from whichever list holds it, groups included. Returns whether it was found.
bool RemoveTrackDeep(TimelineResource* resource, TimelineTrack* track);

/// Insert a group track's children into the group's own list at the group's position and drop the
/// group. Deleting a group must never delete its content. Returns false when the track is not a
/// group or does not belong to the resource.
bool UnwrapGroup(TimelineResource* resource, TimelineTrack* groupTrack);

/// Place the track into «parent» (an invalid parent reference means the top level list) at the given
/// index, reparenting it when it lives elsewhere. Works for tracks that are not in the resource
/// yet, which is how newly created tracks enter a group. Returns false when the destination is a
/// group that does not exist, or when the track would become its own child.
bool AddTrackTo(TimelineResource* resource, TimelineTrack* track, const TrackRef& parent, unsigned index,
    TrackRef& result);

/// Move an already owned track next to the anchor track of the same list. The anchor's own position
/// is recomputed after the move, so dragging downwards needs no index correction.
bool MoveTrackBeside(TimelineResource* resource, TimelineTrack* track, const TrackRef& anchor, bool after,
    TrackRef& result);

/// Invoke the callback on the clip vector of the track. Returns whether the track type has clips:
/// group and marker tracks have none. Every clip track stores its clips in a vector of a
/// TimelineClip-derived struct, so the type specific access is centralized here for the sequencer,
/// the curve editor and the dope sheet alike.
template <class Func>
bool DispatchClipVector(const TimelineTrack* track, const Func& func)
{
    if (const auto* t = dynamic_cast<const TimelineAnimationTrack*>(track)) { func(t->clips_); return true; }
    if (const auto* t = dynamic_cast<const TimelineControlTrack*>(track)) { func(t->clips_); return true; }
    if (const auto* t = dynamic_cast<const TimelineActivationTrack*>(track)) { func(t->clips_); return true; }
    if (const auto* t = dynamic_cast<const TimelineAudioTrack*>(track)) { func(t->clips_); return true; }
    if (const auto* t = dynamic_cast<const TimelineSignalTrack*>(track)) { func(t->clips_); return true; }
    if (const auto* t = dynamic_cast<const TimelinePropertyTrack*>(track)) { func(t->clips_); return true; }
    return false;
}

/// DispatchClipVector of a mutable track.
template <class Func>
bool DispatchClipVector(TimelineTrack* track, const Func& func)
{
    if (auto* t = dynamic_cast<TimelineAnimationTrack*>(track)) { func(t->clips_); return true; }
    if (auto* t = dynamic_cast<TimelineControlTrack*>(track)) { func(t->clips_); return true; }
    if (auto* t = dynamic_cast<TimelineActivationTrack*>(track)) { func(t->clips_); return true; }
    if (auto* t = dynamic_cast<TimelineAudioTrack*>(track)) { func(t->clips_); return true; }
    if (auto* t = dynamic_cast<TimelineSignalTrack*>(track)) { func(t->clips_); return true; }
    if (auto* t = dynamic_cast<TimelinePropertyTrack*>(track)) { func(t->clips_); return true; }
    return false;
}

/// Number of clips of the track, zero for a track type without clips.
unsigned GetClipCount(const TimelineTrack* track);

/// Base data of the clip at the index, null when the track has no such clip.
TimelineClip* GetClipAt(TimelineTrack* track, unsigned index);
const TimelineClip* GetClipAt(const TimelineTrack* track, unsigned index);

}

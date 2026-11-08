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

#include "TimelineTrackRows.h"

#include <Urho3D/Core/StringUtils.h>
#include <Urho3D/Timeline/TimelineResource.h>
#include <Urho3D/Timeline/TimelineTrack.h>

namespace Urho3D
{

namespace
{

using TrackList = ea::vector<SharedPtr<TimelineTrack>>;

const TrackList* ChildList(const TimelineTrack* track)
{
    const auto* group = dynamic_cast<const TimelineGroupTrack*>(track);
    return group ? &group->GetTracks() : nullptr;
}

/// Follow the path of the reference inside the given list, descending into group tracks.
TimelineTrack* ResolveInList(const TrackList& tracks, const TrackRef& ref, unsigned level)
{
    if (level >= ref.Depth() || ref[level] >= tracks.size())
        return nullptr;
    TimelineTrack* track = tracks[ref[level]].Get();
    if (level + 1 == ref.Depth())
        return track;
    const TrackList* children = ChildList(track);
    return children ? ResolveInList(*children, ref, level + 1) : nullptr;
}

/// Depth-first search of the track, accumulating the address of the enclosing group.
bool FindInList(const TrackList& tracks, const TimelineTrack* target, const TrackRef& prefix, TrackRef& result)
{
    for (unsigned i = 0; i < tracks.size(); ++i)
    {
        const TimelineTrack* track = tracks[i].Get();
        if (track == target)
        {
            result = prefix.Child(i);
            return result.Valid();
        }
        if (const TrackList* children = ChildList(track))
        {
            const TrackRef childPrefix = prefix.Child(i);
            if (childPrefix.Valid() && FindInList(*children, target, childPrefix, result))
                return true;
        }
    }
    return false;
}

void AppendRows(const TrackList& tracks, const TrackRef& prefix, const ea::vector<TrackRef>& collapsed,
    ea::vector<TrackRow>& result)
{
    for (unsigned i = 0; i < tracks.size(); ++i)
    {
        TrackRow row;
        row.ref_ = prefix.Child(i);
        // Beyond the addressable nesting the remaining tracks have no row: stop listing them.
        if (!row.ref_.Valid())
            return;
        row.track_ = tracks[i].Get();
        row.depth_ = row.ref_.Depth() - 1;
        const TrackList* children = ChildList(row.track_);
        row.isGroup_ = children != nullptr;
        row.collapsed_ = row.isGroup_ && IsCollapsed(collapsed, row.ref_);
        result.push_back(row);
        if (children && !row.collapsed_)
            AppendRows(*children, row.ref_, collapsed, result);
    }
}

/// Insert into the list owned by «parentGroup», or into the top level list when it is null. The
/// index is clamped, so appending is expressed by passing any value past the end.
bool InsertInList(TimelineResource* resource, TimelineTrack* track, TimelineTrack* parentGroup, unsigned index,
    TrackRef& result)
{
    if (!resource || !track)
        return false;

    if (parentGroup)
    {
        auto* group = dynamic_cast<TimelineGroupTrack*>(parentGroup);
        if (!group)
            return false;
        group->AddTrack(track, Min(index, group->GetNumTracks()));
    }
    else
    {
        resource->AddTrack(track, Min(index, resource->GetNumTracks()));
    }
    return FindTrackRef(resource, track, result);
}

bool RemoveFromGroup(TimelineGroupTrack* group, TimelineTrack* track)
{
    if (group->RemoveTrack(track))
        return true;
    for (const SharedPtr<TimelineTrack>& child : group->GetTracks())
    {
        if (auto* nested = dynamic_cast<TimelineGroupTrack*>(child.Get()))
        {
            if (RemoveFromGroup(nested, track))
                return true;
        }
    }
    return false;
}

} // namespace

TrackRef TrackRef::AtRoot(unsigned index)
{
    TrackRef ref;
    ref.path_[0] = index;
    ref.depth_ = 1;
    return ref;
}

TrackRef TrackRef::Parent() const
{
    return Prefix(depth_ != 0 ? depth_ - 1 : 0);
}

TrackRef TrackRef::Child(unsigned index) const
{
    TrackRef ref;
    if (depth_ + 1 > kMaxDepth)
        return ref;
    for (unsigned i = 0; i < depth_; ++i)
        ref.path_[i] = path_[i];
    ref.path_[depth_] = index;
    ref.depth_ = depth_ + 1;
    return ref;
}

TrackRef TrackRef::Prefix(unsigned depth) const
{
    TrackRef ref;
    if (depth > depth_ || depth > kMaxDepth)
        return ref;
    for (unsigned i = 0; i < depth; ++i)
        ref.path_[i] = path_[i];
    ref.depth_ = depth;
    return ref;
}

bool TrackRef::IsSelfOrAncestorOf(const TrackRef& other) const
{
    if (!Valid() || !other.Valid() || depth_ > other.depth_)
        return false;
    for (unsigned i = 0; i < depth_; ++i)
    {
        if (path_[i] != other.path_[i])
            return false;
    }
    return true;
}

int TrackRef::Compare(const TrackRef& other) const
{
    const unsigned common = Min(depth_, other.depth_);
    for (unsigned i = 0; i < common; ++i)
    {
        if (path_[i] != other.path_[i])
            return path_[i] < other.path_[i] ? -1 : 1;
    }
    if (depth_ == other.depth_)
        return 0;
    // A group precedes its own children, mirroring the order of the rows.
    return depth_ < other.depth_ ? -1 : 1;
}

ea::string TrackRef::ToString() const
{
    if (!Valid())
        return "(none)";
    ea::string result;
    for (unsigned i = 0; i < depth_; ++i)
    {
        if (i != 0)
            result += "/";
        result += Format("{}", path_[i]);
    }
    return result;
}

bool ClipSelection::Contains(const TrackRef& track, unsigned clip) const
{
    for (const ClipKey& key : keys_)
    {
        if (key.track_ == track && key.clip_ == clip)
            return true;
    }
    return false;
}

bool ClipSelection::Insert(const TrackRef& track, unsigned clip)
{
    if (Contains(track, clip))
        return false;
    keys_.push_back(ClipKey{track, clip});
    return true;
}

bool ClipSelection::Erase(const TrackRef& track, unsigned clip)
{
    for (unsigned i = 0; i < keys_.size(); ++i)
    {
        if (keys_[i].track_ == track && keys_[i].clip_ == clip)
        {
            keys_.erase(keys_.begin() + static_cast<long>(i));
            return true;
        }
    }
    return false;
}

TimelineTrack* TrackAt(const TimelineResource* resource, const TrackRef& ref)
{
    if (!resource || !ref.Valid())
        return nullptr;
    return ResolveInList(resource->GetTracks(), ref, 0);
}

bool FindTrackRef(const TimelineResource* resource, const TimelineTrack* track, TrackRef& result)
{
    if (!resource || !track)
        return false;
    return FindInList(resource->GetTracks(), track, TrackRef{}, result);
}

void BuildTrackRows(const TimelineResource* resource, const ea::vector<TrackRef>& collapsed,
    ea::vector<TrackRow>& result)
{
    result.clear();
    if (!resource)
        return;
    AppendRows(resource->GetTracks(), TrackRef{}, collapsed, result);
}

void FlattenTracks(const TimelineResource* resource, ea::vector<TrackRow>& result)
{
    const ea::vector<TrackRef> noCollapse;
    BuildTrackRows(resource, noCollapse, result);
}

bool ContainsRef(const ea::vector<TrackRef>& refs, const TrackRef& ref)
{
    for (const TrackRef& candidate : refs)
    {
        if (candidate == ref)
            return true;
    }
    return false;
}

bool ToggleRef(ea::vector<TrackRef>& refs, const TrackRef& ref)
{
    for (unsigned i = 0; i < refs.size(); ++i)
    {
        if (refs[i] == ref)
        {
            refs.erase(refs.begin() + static_cast<long>(i));
            return false;
        }
    }
    refs.push_back(ref);
    return true;
}

bool IsCollapsed(const ea::vector<TrackRef>& collapsed, const TrackRef& ref)
{
    return ContainsRef(collapsed, ref);
}

bool ToggleCollapsed(ea::vector<TrackRef>& collapsed, const TrackRef& ref)
{
    return ToggleRef(collapsed, ref);
}

void PruneStaleRefs(ea::vector<TrackRef>& refs, const TimelineResource* resource)
{
    for (auto iter = refs.begin(); iter != refs.end();)
    {
        if (TrackAt(resource, *iter))
            ++iter;
        else
            iter = refs.erase(iter);
    }
}

void PruneCollapsed(ea::vector<TrackRef>& collapsed, const TimelineResource* resource)
{
    PruneStaleRefs(collapsed, resource);
}

bool RemoveTrackDeep(TimelineResource* resource, TimelineTrack* track)
{
    if (!resource || !track)
        return false;
    if (resource->RemoveTrack(track))
        return true;
    for (const SharedPtr<TimelineTrack>& child : resource->GetTracks())
    {
        if (auto* group = dynamic_cast<TimelineGroupTrack*>(child.Get()))
        {
            if (RemoveFromGroup(group, track))
                return true;
        }
    }
    return false;
}

bool UnwrapGroup(TimelineResource* resource, TimelineTrack* groupTrack)
{
    if (!resource || !groupTrack)
        return false;

    auto* group = dynamic_cast<TimelineGroupTrack*>(groupTrack);
    if (!group)
        return false;

    TrackRef ref;
    if (!FindTrackRef(resource, groupTrack, ref))
        return false;

    // The children are lifted into the list the group itself lives in, at the position of the
    // group. Both the parent group and the insertion index are resolved again after the group is
    // taken out, because removing it shifts everything below it.
    TimelineTrack* parentTrack = TrackAt(resource, ref.Parent());
    const unsigned insertIndex = ref.LeafIndex();
    ea::vector<SharedPtr<TimelineTrack>> children = group->GetTracks();
    group->RemoveAllTracks();
    RemoveTrackDeep(resource, groupTrack);

    unsigned index = insertIndex;
    if (const TrackList* list = ChildList(parentTrack))
        index = Min(insertIndex, static_cast<unsigned>(list->size()));
    else if (parentTrack)
        return false;
    else
        index = Min(insertIndex, resource->GetNumTracks());

    for (const SharedPtr<TimelineTrack>& child : children)
    {
        if (!InsertInList(resource, child.Get(), parentTrack, index, ref))
            return false;
        ++index;
    }
    return true;
}

bool AddTrackTo(TimelineResource* resource, TimelineTrack* track, const TrackRef& parent, unsigned index,
    TrackRef& result)
{
    if (!resource || !track)
        return false;

    TimelineTrack* parentTrack = parent.Valid() ? TrackAt(resource, parent) : nullptr;
    if (parent.Valid() && !parentTrack)
        return false;
    if (parentTrack == track)
        return false;

    // Refuse to nest a group inside itself or inside one of its own children: that would make the
    // hierarchy cyclic and the track list unreachable.
    TrackRef ref;
    if (FindTrackRef(resource, track, ref) && ref.IsSelfOrAncestorOf(parent))
        return false;

    // Hold the track across the detach: an owned track would otherwise be destroyed the moment it
    // is taken out of its list.
    const SharedPtr<TimelineTrack> keep(track);
    RemoveTrackDeep(resource, track);
    return InsertInList(resource, track, parentTrack, index, result);
}

bool MoveTrackBeside(TimelineResource* resource, TimelineTrack* track, const TrackRef& anchor, bool after,
    TrackRef& result)
{
    if (!resource || !track || !anchor.Valid())
        return false;

    TimelineTrack* anchorTrack = TrackAt(resource, anchor);
    if (!anchorTrack || anchorTrack == track)
        return false;

    TrackRef ref;
    if (!FindTrackRef(resource, track, ref))
        return false;
    if (ref.IsSelfOrAncestorOf(anchor))
        return false;

    // The parent list is identified by its owning track, not by the address of the anchor: taking
    // the moved track out of an enclosing list shifts the address while the list stays the same.
    TimelineTrack* parentTrack = TrackAt(resource, anchor.Parent());
    const SharedPtr<TimelineTrack> keep(track);
    RemoveTrackDeep(resource, track);

    // Re-read the anchor position after the detach, so dragging downwards needs no off-by-one fix.
    TrackRef freshAnchor;
    if (!FindTrackRef(resource, anchorTrack, freshAnchor) || TrackAt(resource, freshAnchor.Parent()) != parentTrack)
        return false;

    const unsigned index = freshAnchor.LeafIndex() + (after ? 1u : 0u);
    return InsertInList(resource, track, parentTrack, index, result);
}

unsigned GetClipCount(const TimelineTrack* track)
{
    unsigned count = 0;
    DispatchClipVector(track, [&count](const auto& clips) { count = static_cast<unsigned>(clips.size()); });
    return count;
}

TimelineClip* GetClipAt(TimelineTrack* track, unsigned index)
{
    TimelineClip* clip = nullptr;
    DispatchClipVector(track, [index, &clip](auto& clips)
    {
        if (index < clips.size())
            clip = &clips[index];
    });
    return clip;
}

const TimelineClip* GetClipAt(const TimelineTrack* track, unsigned index)
{
    return GetClipAt(const_cast<TimelineTrack*>(track), index);
}

}

// Copyright (c) 2022-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#include <Urho3D/Precompiled.h>

#include <Urho3D/IO/ArchiveSerialization.h>
#include <Urho3D/Timeline/TimelineResource.h>

namespace Urho3D
{

namespace
{

const BinaryMagic TIMELINE_BINARY_MAGIC{{'\0', 'T', 'L', 'N'}};

}

TimelineResource::TimelineResource(Context* context) :
    SimpleResource(context)
{
}

TimelineResource::~TimelineResource() = default;

void TimelineResource::RegisterObject(Context* context)
{
    context->AddFactoryReflection<TimelineResource>(Category_Timeline);
}

void TimelineResource::SerializeInBlock(Archive& archive)
{
    SerializeOptionalValue(archive, "durationMode", durationMode_, TimelineDurationMode::Content,
        [](Archive& archive, const char* name, auto& value) //
        { SerializeEnum(archive, name, value, TIMELINE_DURATION_MODE_NAMES); });
    SerializeOptionalValue(archive, "duration", fixedDuration_, 5.0f);
    SerializeVectorAsObjects(archive, "tracks", tracks_, "track");
}

float TimelineResource::GetDuration() const
{
    if (durationMode_ == TimelineDurationMode::Fixed)
        return fixedDuration_;
    return GetContentDuration();
}

float TimelineResource::GetContentDuration() const
{
    float duration = 0.0f;
    for (const SharedPtr<TimelineTrack>& track : tracks_)
        duration = Max(duration, track->GetContentDuration());
    return duration;
}

void TimelineResource::AddTrack(TimelineTrack* track, unsigned index)
{
    if (!track)
        return;

    if (index >= tracks_.size())
        tracks_.push_back(SharedPtr<TimelineTrack>(track));
    else
        tracks_.insert(tracks_.begin() + static_cast<ea::vector<SharedPtr<TimelineTrack>>::difference_type>(index),
            SharedPtr<TimelineTrack>(track));

    ++tracksRevision_;
}

bool TimelineResource::RemoveTrack(TimelineTrack* track)
{
    const auto iter = ea::find(tracks_.begin(), tracks_.end(), SharedPtr<TimelineTrack>(track));
    if (iter == tracks_.end())
        return false;

    tracks_.erase(iter);
    ++tracksRevision_;
    return true;
}

void TimelineResource::RemoveAllTracks()
{
    if (tracks_.empty())
        return;

    tracks_.clear();
    ++tracksRevision_;
}

TimelineTrack* TimelineResource::GetTrack(unsigned index) const
{
    return index < tracks_.size() ? tracks_[index].Get() : nullptr;
}

unsigned TimelineResource::GetTrackIndex(TimelineTrack* track) const
{
    for (unsigned i = 0; i < tracks_.size(); ++i)
    {
        if (tracks_[i].Get() == track)
            return i;
    }
    return M_MAX_UNSIGNED;
}

BinaryMagic TimelineResource::GetBinaryMagic() const
{
    return TIMELINE_BINARY_MAGIC;
}

}

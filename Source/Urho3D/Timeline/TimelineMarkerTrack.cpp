// Copyright (c) 2022-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#include <Urho3D/Precompiled.h>

#include <Urho3D/Core/ObjectCategory.h>
#include <Urho3D/IO/ArchiveSerialization.h>
#include <Urho3D/Timeline/TimelineMarkerTrack.h>
#include <Urho3D/Timeline/TimelinePlayer.h>

namespace Urho3D
{

void TimelineMarker::SerializeInBlock(Archive& archive)
{
    SerializeOptionalValue(archive, "time", time_, 0.0f);
    SerializeOptionalValue(archive, "name", name_);
}

TimelineMarkerTrack::TimelineMarkerTrack(Context* context) :
    TimelineTrack(context)
{
}

TimelineMarkerTrack::~TimelineMarkerTrack() = default;

void TimelineMarkerTrack::RegisterObject(Context* context)
{
    context->AddFactoryReflection<TimelineMarkerTrack>(Category_Timeline);
}

void TimelineMarkerTrack::SerializeInBlock(Archive& archive)
{
    TimelineTrack::SerializeInBlock(archive);
    SerializeVectorAsObjects(archive, "markers", markers_, "marker");
}

float TimelineMarkerTrack::GetContentDuration() const
{
    float duration = 0.0f;
    for (const TimelineMarker& marker : markers_)
        duration = Max(duration, marker.time_);
    return duration;
}

void TimelineMarkerTrack::AddMarker(const TimelineMarker& marker)
{
    markers_.push_back(marker);
    ea::sort(markers_.begin(), markers_.end(),
        [](const TimelineMarker& lhs, const TimelineMarker& rhs) { return lhs.time_ < rhs.time_; });
}

void TimelineMarkerTrack::InsertMarker(unsigned index, const TimelineMarker& marker)
{
    if (index >= markers_.size())
        markers_.push_back(marker);
    else
        markers_.insert(markers_.begin() + static_cast<ea::vector<TimelineMarker>::difference_type>(index), marker);
}

bool TimelineMarkerTrack::RemoveMarker(const TimelineMarker& marker)
{
    const auto iter = ea::find_if(markers_.begin(), markers_.end(),
        [&](const TimelineMarker& other) { return other.name_ == marker.name_ && other.time_ == marker.time_; });
    if (iter == markers_.end())
        return false;

    markers_.erase(iter);
    return true;
}

void TimelineMarkerTrack::RemoveMarker(unsigned index)
{
    if (index < markers_.size())
        markers_.erase(markers_.begin() + static_cast<ea::vector<TimelineMarker>::difference_type>(index));
}

void TimelineMarkerTrack::Evaluate(TimelineTrackContext& context)
{
    if (IsInactive(context))
        return;

    for (const TimelineMarker& marker : markers_)
    {
        // Markers fire when playback crosses their time, in both directions.
        bool fire = false;
        if (context.firstEvaluation_)
            fire = marker.time_ <= context.time_ && marker.time_ > context.prevTime_;
        else if (context.time_ > context.prevTime_)
            fire = marker.time_ > context.prevTime_ && marker.time_ <= context.time_;
        else if (context.time_ < context.prevTime_)
            fire = marker.time_ >= context.time_ && marker.time_ < context.prevTime_;

        if (fire)
            context.player_.EmitMarker(marker.name_, marker.time_);
    }
}

}

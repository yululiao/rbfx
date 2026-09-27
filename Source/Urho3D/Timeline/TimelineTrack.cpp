// Copyright (c) 2022-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#include <Urho3D/Precompiled.h>

#include <Urho3D/Core/ObjectCategory.h>
#include <Urho3D/IO/ArchiveSerialization.h>
#include <Urho3D/Timeline/TimelineTrack.h>

namespace Urho3D
{

TimelineTrack::TimelineTrack(Context* context) :
    Object(context)
{
}

TimelineTrack::~TimelineTrack() = default;

void TimelineTrack::RegisterObject(Context* context)
{
    context->AddFactoryReflection<TimelineTrack>(Category_Timeline);
}

void TimelineTrack::SerializeInBlock(Archive& archive)
{
    SerializeOptionalValue(archive, "name", name_);
    SerializeOptionalValue(archive, "binding", bindingPath_);
    SerializeOptionalValue(archive, "weight", weight_, 1.0f);
    SerializeOptionalValue(archive, "muted", muted_, false);
}

ea::unique_ptr<TimelineTrackRuntime> TimelineTrack::CreateRuntime() const
{
    return ea::make_unique<TimelineTrackRuntime>();
}

TimelineGroupTrack::TimelineGroupTrack(Context* context) :
    TimelineTrack(context)
{
}

TimelineGroupTrack::~TimelineGroupTrack() = default;

void TimelineGroupTrack::RegisterObject(Context* context)
{
    context->AddFactoryReflection<TimelineGroupTrack>(Category_Timeline);
}

void TimelineGroupTrack::SerializeInBlock(Archive& archive)
{
    TimelineTrack::SerializeInBlock(archive);
    SerializeVectorAsObjects(archive, "tracks", tracks_, "track");
}

float TimelineGroupTrack::GetContentDuration() const
{
    float duration = 0.0f;
    for (const SharedPtr<TimelineTrack>& track : tracks_)
        duration = Max(duration, track->GetContentDuration());
    return duration;
}

ea::unique_ptr<TimelineTrackRuntime> TimelineGroupTrack::CreateRuntime() const
{
    auto runtime = ea::make_unique<Runtime>();
    runtime->children_.resize(tracks_.size());
    for (unsigned i = 0; i < tracks_.size(); ++i)
        runtime->children_[i] = tracks_[i]->CreateRuntime();
    return runtime;
}

void TimelineGroupTrack::OnTimelineStarted(TimelinePlayer& player, TimelineTrackRuntime& runtime)
{
    Runtime& groupRuntime = static_cast<Runtime&>(runtime);
    for (unsigned i = 0; i < tracks_.size() && i < groupRuntime.children_.size(); ++i)
        tracks_[i]->OnTimelineStarted(player, *groupRuntime.children_[i]);
}

void TimelineGroupTrack::OnTimelineStopped(TimelinePlayer& player, TimelineTrackRuntime& runtime, bool finished)
{
    Runtime& groupRuntime = static_cast<Runtime&>(runtime);
    for (unsigned i = 0; i < tracks_.size() && i < groupRuntime.children_.size(); ++i)
        tracks_[i]->OnTimelineStopped(player, *groupRuntime.children_[i], finished);
}

void TimelineGroupTrack::OnTimelinePaused(TimelinePlayer& player, TimelineTrackRuntime& runtime, bool paused)
{
    Runtime& groupRuntime = static_cast<Runtime&>(runtime);
    for (unsigned i = 0; i < tracks_.size() && i < groupRuntime.children_.size(); ++i)
        tracks_[i]->OnTimelinePaused(player, *groupRuntime.children_[i], paused);
}

void TimelineGroupTrack::Evaluate(TimelineTrackContext& context)
{
    Runtime& runtime = static_cast<Runtime&>(context.runtime_);

    // Restore child runtime states if tracks were added or removed after the runtime was created.
    if (runtime.children_.size() != tracks_.size())
    {
        runtime.children_.resize(tracks_.size());
        for (unsigned i = 0; i < tracks_.size(); ++i)
        {
            if (!runtime.children_[i])
                runtime.children_[i] = tracks_[i]->CreateRuntime();
        }
    }

    const float childWeightScale = IsInactive(context) ? 0.0f : context.weightScale_ * weight_;

    for (unsigned i = 0; i < tracks_.size(); ++i)
    {
        TimelineTrackContext childContext{context.player_, *runtime.children_[i], context.time_, context.prevTime_,
            context.deltaTime_, childWeightScale, context.playing_, context.firstEvaluation_};
        tracks_[i]->Evaluate(childContext);
    }
}

void TimelineGroupTrack::AddTrack(TimelineTrack* track, unsigned index)
{
    if (!track)
        return;

    if (index >= tracks_.size())
        tracks_.push_back(SharedPtr<TimelineTrack>(track));
    else
        tracks_.insert(tracks_.begin() + static_cast<ea::vector<SharedPtr<TimelineTrack>>::difference_type>(index),
            SharedPtr<TimelineTrack>(track));
}

bool TimelineGroupTrack::RemoveTrack(TimelineTrack* track)
{
    const auto iter = ea::find(tracks_.begin(), tracks_.end(), SharedPtr<TimelineTrack>(track));
    if (iter == tracks_.end())
        return false;

    tracks_.erase(iter);
    return true;
}

TimelineTrack* TimelineGroupTrack::GetTrack(unsigned index) const
{
    return index < tracks_.size() ? tracks_[index].Get() : nullptr;
}

}

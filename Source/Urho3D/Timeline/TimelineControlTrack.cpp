// Copyright (c) 2022-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#include <Urho3D/Precompiled.h>

#include <Urho3D/Core/ObjectCategory.h>
#include <Urho3D/IO/ArchiveSerialization.h>
#include <Urho3D/Resource/Resource.h>
#include <Urho3D/Scene/Node.h>
#include <Urho3D/Timeline/TimelineControlTrack.h>
#include <Urho3D/Timeline/TimelinePlayer.h>

namespace Urho3D
{

void TimelineControlClip::SerializeInBlock(Archive& archive)
{
    TimelineClip::SerializeInBlock(archive);

    if (!archive.IsInput())
    {
        if (prefab_)
            prefabRef_ = GetResourceRef(prefab_, PrefabResource::GetTypeStatic());
        if (timeline_)
            timelineRef_ = GetResourceRef(timeline_, TimelineResource::GetTypeStatic());
    }

    SerializeResource(archive, "prefab", prefab_, prefabRef_);
    SerializeResource(archive, "timeline", timeline_, timelineRef_);
    SerializeOptionalValue(archive, "postPlayback", postPlayback_, TimelinePostPlayback::Destroy,
        [](Archive& archive, const char* name, auto& value) //
        { SerializeEnum(archive, name, value, TIMELINE_POST_PLAYBACK_NAMES); });
}

TimelineControlTrack::TimelineControlTrack(Context* context) :
    TimelineTrack(context)
{
}

TimelineControlTrack::~TimelineControlTrack() = default;

void TimelineControlTrack::RegisterObject(Context* context)
{
    context->AddFactoryReflection<TimelineControlTrack>(Category_Timeline);
}

void TimelineControlTrack::SerializeInBlock(Archive& archive)
{
    TimelineTrack::SerializeInBlock(archive);
    SerializeVectorAsObjects(archive, "clips", clips_, "clip");
}

float TimelineControlTrack::GetContentDuration() const
{
    float duration = 0.0f;
    for (const TimelineControlClip& clip : clips_)
        duration = Max(duration, clip.GetEndTime());
    return duration;
}

ea::unique_ptr<TimelineTrackRuntime> TimelineControlTrack::CreateRuntime() const
{
    return ea::make_unique<Runtime>();
}

void TimelineControlTrack::OnTimelineStopped(TimelinePlayer& player, TimelineTrackRuntime& runtime, bool finished)
{
    Runtime& trackRuntime = static_cast<Runtime&>(runtime);
    for (unsigned i = 0; i < clips_.size() && i < trackRuntime.clips_.size(); ++i)
    {
        Runtime::ClipState& state = trackRuntime.clips_[i];
        if (state.started_)
            StopClip(clips_[i], state);
    }
}

void TimelineControlTrack::OnTimelinePaused(TimelinePlayer& player, TimelineTrackRuntime& runtime, bool paused)
{
    Runtime& trackRuntime = static_cast<Runtime&>(runtime);
    for (Runtime::ClipState& state : trackRuntime.clips_)
    {
        if (TimelinePlayer* nested = state.player_)
            nested->SetPaused(paused);
    }
}

void TimelineControlTrack::Evaluate(TimelineTrackContext& context)
{
    Runtime& runtime = static_cast<Runtime&>(context.runtime_);
    runtime.clips_.resize(clips_.size());

    const bool inactive = IsInactive(context);

    for (unsigned i = 0; i < clips_.size(); ++i)
    {
        const TimelineControlClip& clip = clips_[i];
        Runtime::ClipState& state = runtime.clips_[i];

        float localTime = 0.0f;
        const bool active = !inactive && clip.EvaluateLocalTime(context.time_, localTime);

        if (active && !state.started_)
            StartClip(context, clip, state);
        else if (!active && state.started_)
            StopClip(clip, state);
        else if (active && state.started_)
        {
            // Keep nested playback in sync with the parent timeline.
            if (TimelinePlayer* nested = state.player_)
                nested->SetPlaybackSpeed(context.player_.GetPlaybackSpeed());
        }
    }
}

void TimelineControlTrack::StartClip(
    TimelineTrackContext& context, const TimelineControlClip& clip, Runtime::ClipState& state)
{
    TimelinePlayer& player = context.player_;
    Node* host = nullptr;
    bool spawned = false;

    Node* boundNode = GetBoundNode(player.ResolveTrackBinding(*this, context.runtime_));

    if (clip.prefab_)
    {
        Node* parent = boundNode ? boundNode : player.GetNode();
        if (parent)
        {
            host = clip.prefab_->InstantiateReference(parent);
            spawned = true;
        }
    }
    else
    {
        host = boundNode ? boundNode : player.GetNode();
    }

    state.node_ = host;
    state.spawned_ = spawned;
    state.started_ = true;

    if (host && clip.timeline_)
    {
        TimelinePlayer* nested = host->GetComponent<TimelinePlayer>();
        if (!nested)
            nested = host->CreateComponent<TimelinePlayer>();

        if (nested)
        {
            nested->SetTimeline(clip.timeline_);
            nested->SetPlaybackSpeed(player.GetPlaybackSpeed());
            nested->Play();
        }
        state.player_ = nested;
    }
}

void TimelineControlTrack::StopClip(const TimelineControlClip& clip, Runtime::ClipState& state)
{
    if (TimelinePlayer* nested = state.player_)
        nested->Stop();

    Node* node = state.node_;
    const bool spawned = state.spawned_;
    state = {};

    if (!node)
        return;

    switch (clip.postPlayback_)
    {
    case TimelinePostPlayback::Disable:
        node->SetEnabled(false);
        break;

    case TimelinePostPlayback::Keep:
        break;

    case TimelinePostPlayback::Destroy:
    case TimelinePostPlayback::Revert:
    default:
        if (spawned)
            node->Remove();
        break;
    }
}

}

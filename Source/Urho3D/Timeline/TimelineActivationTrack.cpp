// Copyright (c) 2022-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#include <Urho3D/Precompiled.h>

#include <Urho3D/Core/ObjectCategory.h>
#include <Urho3D/IO/ArchiveSerialization.h>
#include <Urho3D/Scene/Node.h>
#include <Urho3D/Timeline/TimelineActivationTrack.h>
#include <Urho3D/Timeline/TimelinePlayer.h>

namespace Urho3D
{

void TimelineActivationClip::SerializeInBlock(Archive& archive)
{
    TimelineClip::SerializeInBlock(archive);
    SerializeOptionalValue(archive, "active", active_, true);
}

TimelineActivationTrack::TimelineActivationTrack(Context* context) :
    TimelineTrack(context)
{
}

TimelineActivationTrack::~TimelineActivationTrack() = default;

void TimelineActivationTrack::RegisterObject(Context* context)
{
    context->AddFactoryReflection<TimelineActivationTrack>(Category_Timeline);
}

void TimelineActivationTrack::SerializeInBlock(Archive& archive)
{
    TimelineTrack::SerializeInBlock(archive);
    SerializeOptionalValue(archive, "postPlayback", postPlaybackState_, TimelinePostPlaybackState::Revert,
        [](Archive& archive, const char* name, auto& value) //
        { SerializeEnum(archive, name, value, TIMELINE_POST_PLAYBACK_STATE_NAMES); });
    SerializeVectorAsObjects(archive, "clips", clips_, "clip");
}

float TimelineActivationTrack::GetContentDuration() const
{
    float duration = 0.0f;
    for (const TimelineActivationClip& clip : clips_)
        duration = Max(duration, clip.GetEndTime());
    return duration;
}

ea::unique_ptr<TimelineTrackRuntime> TimelineActivationTrack::CreateRuntime() const
{
    return ea::make_unique<Runtime>();
}

void TimelineActivationTrack::OnTimelineStopped(TimelinePlayer& player, TimelineTrackRuntime& runtime, bool finished)
{
    Runtime& trackRuntime = static_cast<Runtime&>(runtime);
    if (!trackRuntime.applied_)
        return;

    if (Node* node = GetBoundNode(trackRuntime.boundObject_))
    {
        switch (postPlaybackState_)
        {
        case TimelinePostPlaybackState::Revert:
            node->SetEnabled(trackRuntime.savedEnabled_);
            break;
        case TimelinePostPlaybackState::Active:
            node->SetEnabled(true);
            break;
        case TimelinePostPlaybackState::Inactive:
            node->SetEnabled(false);
            break;
        case TimelinePostPlaybackState::LeaveAsIs:
        default:
            break;
        }
    }

    trackRuntime.applied_ = false;
    trackRuntime.stateSaved_ = false;
}

void TimelineActivationTrack::Evaluate(TimelineTrackContext& context)
{
    Runtime& runtime = static_cast<Runtime&>(context.runtime_);

    // Muted and zero-weight tracks do not drive the bound node.
    if (IsInactive(context))
        return;

    Node* node = GetBoundNode(context.player_.ResolveTrackBinding(*this, context.runtime_));
    if (!node)
        return;

    if (!runtime.stateSaved_)
    {
        runtime.savedEnabled_ = node->IsEnabled();
        runtime.stateSaved_ = true;
    }

    // The state of the last active clip wins. Without active clips the node is deactivated.
    bool driven = false;
    for (const TimelineActivationClip& clip : clips_)
    {
        float localTime = 0.0f;
        if (clip.EvaluateLocalTime(context.time_, localTime))
            driven = clip.active_;
    }

    if (!runtime.applied_ || runtime.currentActive_ != driven)
    {
        runtime.applied_ = true;
        runtime.currentActive_ = driven;
        node->SetEnabled(driven);
    }
}

}

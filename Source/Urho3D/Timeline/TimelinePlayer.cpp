// Copyright (c) 2022-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#include <Urho3D/Precompiled.h>

#include <Urho3D/Core/ObjectCategory.h>
#include <Urho3D/Core/Timer.h>
#include <Urho3D/Graphics/AnimationController.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Node.h>
#include <Urho3D/Timeline/TimelineAnimationTrack.h>
#include <Urho3D/Timeline/TimelineEvents.h>
#include <Urho3D/Timeline/TimelinePlayer.h>
#include <Urho3D/Timeline/TimelineResource.h>

#include <cmath>

namespace Urho3D
{

namespace
{

/// Epsilon of the previous time of the first evaluation. Ensures that signals and markers
/// at the exact playback start time are fired.
const float FIRST_EVALUATION_EPSILON = 0.001f;

/// Fix cached animation instance indices of the track runtime after removing an instance at given index.
void FixAnimationRuntimes(
    TimelineTrack* track, TimelineTrackRuntime* runtime, const AnimationController* controller, unsigned removedIndex)
{
    if (!track || !runtime)
        return;

    if (auto* groupTrack = dynamic_cast<TimelineGroupTrack*>(track))
    {
        auto* groupRuntime = static_cast<TimelineGroupTrack::Runtime*>(runtime);
        for (unsigned i = 0; i < groupTrack->GetNumTracks() && i < groupRuntime->children_.size(); ++i)
            FixAnimationRuntimes(groupTrack->GetTrack(i), groupRuntime->children_[i].get(), controller, removedIndex);
        return;
    }

    if (auto* animationTrack = dynamic_cast<TimelineAnimationTrack*>(track))
    {
        auto* animationRuntime = static_cast<TimelineAnimationTrack::Runtime*>(runtime);
        if (animationRuntime->controller_.Get() != controller)
            return;

        for (TimelineAnimationTrack::Runtime::ClipState& state : animationRuntime->clips_)
        {
            if (!state.active_)
                continue;

            if (state.controllerIndex_ == removedIndex)
                state = {};
            else if (state.controllerIndex_ > removedIndex)
                --state.controllerIndex_;
        }
    }
}

}

Node* GetBoundNode(Object* boundObject)
{
    if (!boundObject)
        return nullptr;

    if (auto* node = dynamic_cast<Node*>(boundObject))
        return node;

    if (auto* component = dynamic_cast<Component*>(boundObject))
        return component->GetNode();

    return nullptr;
}

TimelinePlayer::TimelinePlayer(Context* context) :
    LogicComponent(context)
{
    SetUpdateEventMask(USE_UPDATE);
}

TimelinePlayer::~TimelinePlayer() = default;

void TimelinePlayer::RegisterObject(Context* context)
{
    context->AddFactoryReflection<TimelinePlayer>(Category_Timeline);

    URHO3D_ATTRIBUTE("Timeline", ResourceRef, timelineRef_, ResourceRef(), AM_DEFAULT);
    URHO3D_ATTRIBUTE("Play On Awake", bool, playOnAwake_, true, AM_DEFAULT);
    URHO3D_ENUM_ATTRIBUTE("Wrap Mode", wrapMode_, TIMELINE_WRAP_MODE_NAMES, TimelineWrapMode::None, AM_DEFAULT);
    URHO3D_ENUM_ATTRIBUTE("Time Update Mode", timeUpdateMode_, TIMELINE_TIME_UPDATE_MODE_NAMES,
        TimelineTimeUpdateMode::GameTime, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Playback Speed", float, playbackSpeed_, 1.0f, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Initial Time", float, initialTime_, 0.0f, AM_DEFAULT);
}

void TimelinePlayer::ApplyAttributes()
{
    const ea::string& resourceName = timelineRef_.name_;
    if (resourceName.empty())
    {
        if (timeline_)
            SetTimeline(nullptr);
        return;
    }

    if (timeline_ && timeline_->GetName() == resourceName)
        return;

    SharedPtr<TimelineResource> resource(GetSubsystem<ResourceCache>()->GetResource<TimelineResource>(resourceName));
    if (resource)
        SetTimeline(resource);
    // On load failure keep the reference so that the component can be saved back.
}

void TimelinePlayer::SetTimeline(TimelineResource* timeline)
{
    if (timeline_ == timeline)
        return;

    if (playing_)
        StopPlayback(false);

    timeline_ = timeline;
    timelineRef_ = GetResourceRef(timeline_, TimelineResource::GetTypeStatic());
    tracksRevision_ = M_MAX_UNSIGNED;
    time_ = Clamp(time_, 0.0f, GetDuration());
}

float TimelinePlayer::GetDuration() const
{
    return timeline_ ? timeline_->GetDuration() : 0.0f;
}

void TimelinePlayer::Play()
{
    UpdateRuntimeState();

    if (playing_)
    {
        // Already playing: resume if paused.
        SetPaused(false);
        return;
    }

    if (!timeline_)
        return;

    StartPlayback();
}

void TimelinePlayer::Stop()
{
    UpdateRuntimeState();
    StopPlayback(false);
    time_ = 0.0f;
}

void TimelinePlayer::SetPaused(bool paused)
{
    if (!playing_ || paused_ == paused)
        return;

    paused_ = paused;
    NotifyTracksPaused(paused_ || !IsEnabled());

    SendTimelineEvent(E_TIMELINE_PAUSED, {{TimelinePaused::P_PAUSE, paused_}});
}

void TimelinePlayer::Evaluate()
{
    UpdateRuntimeState();
    EvaluateTracks(time_, false);
}

void TimelinePlayer::SetTime(float time)
{
    UpdateRuntimeState();

    const float newTime = Clamp(time, 0.0f, GetDuration());
    if (newTime == time_)
        return;

    const float prevTime = time_;
    time_ = newTime;

    // The timeline is evaluated only if it is playing: scrubbing a stopped player only moves the time value.
    if (playing_)
        EvaluateTracks(prevTime, false);
}

void TimelinePlayer::SetTrackBinding(TimelineTrack* track, Object* object)
{
    if (!track)
        return;

    if (object)
        trackBindings_[track] = WeakPtr<Object>(object);
    else
        trackBindings_.erase(track);

    // Invalidate cached bindings.
    for (TrackRuntime& trackRuntime : trackRuntimes_)
    {
        if (trackRuntime.runtime_)
            trackRuntime.runtime_->bindingDirty_ = true;
    }
}

Object* TimelinePlayer::GetTrackBinding(const TimelineTrack* track) const
{
    const auto iter = trackBindings_.find(track);
    return iter != trackBindings_.end() ? iter->second.Get() : nullptr;
}

Object* TimelinePlayer::ResolveTrackBinding(const TimelineTrack& track, TimelineTrackRuntime& runtime)
{
    if (!runtime.bindingDirty_ && runtime.boundObject_)
        return runtime.boundObject_;

    Object* resolved = nullptr;

    // Explicit binding override wins over the path.
    const auto iter = trackBindings_.find(&track);
    if (iter != trackBindings_.end())
        resolved = iter->second.Get();

    if (!resolved)
        resolved = ResolveBindingPath(track.GetBindingPath());

    runtime.boundObject_ = resolved;
    runtime.bindingDirty_ = false;
    return resolved;
}

Object* TimelinePlayer::ResolveBindingPath(const ea::string& path) const
{
    Node* node = GetNode();
    if (!node)
        return nullptr;

    if (path.empty())
        return node;

    // Resolve path segments relative to the player node. The last segment may reference a component.
    unsigned begin = 0;
    const unsigned length = path.length();
    while (begin < length)
    {
        unsigned end = begin;
        while (end < length && path[end] != '/')
            ++end;

        const ea::string segment = path.substr(begin, end - begin);
        const bool isLast = end >= length;

        if (!segment.empty())
        {
            Node* child = node->GetChild(segment, false);
            if (!child)
                child = node->GetChild(segment, true);

            if (child)
                node = child;
            else if (isLast)
            {
                // Component on the current node, e.g. "Weapon/AnimationController".
                Component* component = node->GetComponent(StringHash(segment));
                return component;
            }
            else
                return nullptr;
        }

        begin = end + 1;
    }

    return node;
}

void TimelinePlayer::RemoveAnimationInstance(AnimationController& controller, unsigned index)
{
    if (index >= controller.GetNumAnimations())
        return;

    controller.RemoveAnimation(index);

    // Removal shifts instance indices: fix cached indices of all animation tracks.
    for (TrackRuntime& trackRuntime : trackRuntimes_)
        FixAnimationRuntimes(trackRuntime.track_, trackRuntime.runtime_.get(), &controller, index);
}

void TimelinePlayer::EmitSignal(const ea::string& signal, const VariantMap& data, float time, bool retroactive)
{
    if (retroactive)
        retroactiveSignals_.push_back(TimelineFiredSignal{signal, data, time});

    SendTimelineEvent(E_TIMELINE_SIGNAL,
        {{TimelineSignal::P_SIGNAL, signal}, {TimelineSignal::P_TIME, time}, {TimelineSignal::P_DATA, data}});
}

void TimelinePlayer::EmitMarker(const ea::string& marker, float time)
{
    SendTimelineEvent(E_TIMELINE_MARKER, {{TimelineMarkerEvent::P_MARKER, marker}, {TimelineMarkerEvent::P_TIME, time}});
}

void TimelinePlayer::OnSetEnabled()
{
    LogicComponent::OnSetEnabled();

    if (!playing_)
        return;

    // Freeze tracks while the component is disabled and restore the state when it is enabled again.
    NotifyTracksPaused(!IsEnabled() || paused_);
}

void TimelinePlayer::Start()
{
    // Node is assigned: bindings must be resolved from the new hierarchy again.
    for (TrackRuntime& trackRuntime : trackRuntimes_)
    {
        if (trackRuntime.runtime_)
            trackRuntime.runtime_->bindingDirty_ = true;
    }
}

void TimelinePlayer::DelayedStart()
{
    if (playOnAwake_)
        Play();
}

void TimelinePlayer::Update(float timeStep)
{
    sceneTimeStep_ = timeStep;

    UpdateRuntimeState();

    if (!playing_ || paused_)
        return;

    if (timeUpdateMode_ == TimelineTimeUpdateMode::Manual)
        return;

    float deltaTime = timeStep;
    if (timeUpdateMode_ == TimelineTimeUpdateMode::UnscaledGameTime)
        deltaTime = GetSubsystem<Time>()->GetTimeStep();

    deltaTime *= playbackSpeed_;

    const float prevTime = time_;
    const float duration = GetDuration();

    if (deltaTime == 0.0f)
    {
        // No time movement: keep tracks up to date without advancing the clock.
        EvaluateTracks(prevTime, false);
        return;
    }

    if (duration <= M_EPSILON)
    {
        time_ = 0.0f;
        EvaluateTracks(prevTime, false);
        return;
    }

    const float newTime = time_ + deltaTime;

    if (deltaTime > 0.0f && newTime >= duration)
    {
        switch (wrapMode_)
        {
        case TimelineWrapMode::None:
            time_ = duration;
            EvaluateTracks(prevTime, false);
            StopPlayback(true);
            return;

        case TimelineWrapMode::Hold:
            time_ = duration;
            EvaluateTracks(prevTime, false);
            return;

        case TimelineWrapMode::Loop:
        default:
        {
            // Play to the end, then wrap and evaluate the remaining time.
            time_ = duration;
            EvaluateTracks(prevTime, false);

            time_ = fmodf(newTime, duration);
            SendTimelineEvent(E_TIMELINE_WRAPPED, {{TimelineWrapped::P_TIME, time_}});
            EvaluateTracks(-FIRST_EVALUATION_EPSILON, false);
            return;
        }
        }
    }

    if (deltaTime < 0.0f && newTime < 0.0f)
    {
        switch (wrapMode_)
        {
        case TimelineWrapMode::None:
            time_ = 0.0f;
            EvaluateTracks(prevTime, false);
            StopPlayback(true);
            return;

        case TimelineWrapMode::Hold:
            time_ = 0.0f;
            EvaluateTracks(prevTime, false);
            return;

        case TimelineWrapMode::Loop:
        default:
        {
            // Play to the beginning, then wrap and evaluate the remaining time.
            time_ = 0.0f;
            EvaluateTracks(prevTime, false);

            time_ = duration + fmodf(newTime, duration);
            SendTimelineEvent(E_TIMELINE_WRAPPED, {{TimelineWrapped::P_TIME, time_}});
            EvaluateTracks(duration + FIRST_EVALUATION_EPSILON, false);
            return;
        }
        }
    }

    time_ = newTime;
    EvaluateTracks(prevTime, false);
}

void TimelinePlayer::UpdateRuntimeState()
{
    if (!timeline_)
    {
        if (!trackRuntimes_.empty() || tracksRevision_ != 0)
            RebuildRuntimeState();
        return;
    }

    if (timeline_->GetTracksRevision() != tracksRevision_ || trackRuntimes_.size() != timeline_->GetNumTracks())
    {
        RebuildRuntimeState();
        return;
    }

    // Detect replaced tracks with an unchanged track count.
    for (unsigned i = 0; i < trackRuntimes_.size(); ++i)
    {
        if (trackRuntimes_[i].track_.Get() != timeline_->GetTrack(i))
        {
            RebuildRuntimeState();
            return;
        }
    }
}

void TimelinePlayer::RebuildRuntimeState()
{
    const bool wasPlaying = playing_;

    if (wasPlaying)
    {
        for (TrackRuntime& trackRuntime : trackRuntimes_)
        {
            if (trackRuntime.track_ && trackRuntime.runtime_)
                trackRuntime.track_->OnTimelineStopped(*this, *trackRuntime.runtime_, false);
        }
    }

    trackRuntimes_.clear();
    tracksRevision_ = timeline_ ? timeline_->GetTracksRevision() : 0;

    if (timeline_)
    {
        const unsigned numTracks = timeline_->GetNumTracks();
        trackRuntimes_.resize(numTracks);
        for (unsigned i = 0; i < numTracks; ++i)
        {
            TimelineTrack* track = timeline_->GetTrack(i);
            trackRuntimes_[i].track_ = track;
            trackRuntimes_[i].runtime_ = track ? track->CreateRuntime() : nullptr;
        }
    }

    if (wasPlaying)
    {
        for (TrackRuntime& trackRuntime : trackRuntimes_)
        {
            if (trackRuntime.track_ && trackRuntime.runtime_)
                trackRuntime.track_->OnTimelineStarted(*this, *trackRuntime.runtime_);
        }
    }
}

void TimelinePlayer::EvaluateTracks(float prevTime, bool firstEvaluation)
{
    const bool effectivelyPlaying = playing_ && !paused_;

    for (TrackRuntime& trackRuntime : trackRuntimes_)
    {
        if (!trackRuntime.track_ || !trackRuntime.runtime_)
            continue;

        TimelineTrackContext context{*this, *trackRuntime.runtime_, time_, prevTime, time_ - prevTime, 1.0f,
            effectivelyPlaying, firstEvaluation};
        trackRuntime.track_->Evaluate(context);
    }
}

void TimelinePlayer::StartPlayback()
{
    time_ = Clamp(initialTime_, 0.0f, GetDuration());
    retroactiveSignals_.clear();

    // Recreate runtime states so that each playback session starts fresh.
    RebuildRuntimeState();

    playing_ = true;
    paused_ = false;

    for (TrackRuntime& trackRuntime : trackRuntimes_)
    {
        if (trackRuntime.track_ && trackRuntime.runtime_)
            trackRuntime.track_->OnTimelineStarted(*this, *trackRuntime.runtime_);
    }

    SendTimelineEvent(E_TIMELINE_PLAYED, {});

    // Evaluate at the start time: signals at this time are fired, retroactive signals are emitted.
    EvaluateTracks(time_ - FIRST_EVALUATION_EPSILON, true);
}

void TimelinePlayer::StopPlayback(bool finished)
{
    if (!playing_)
        return;

    playing_ = false;
    paused_ = false;

    for (TrackRuntime& trackRuntime : trackRuntimes_)
    {
        if (trackRuntime.track_ && trackRuntime.runtime_)
            trackRuntime.track_->OnTimelineStopped(*this, *trackRuntime.runtime_, finished);
    }

    SendTimelineEvent(E_TIMELINE_STOPPED, {{TimelineStopped::P_FINISHED, finished}});
}

void TimelinePlayer::NotifyTracksPaused(bool paused)
{
    for (TrackRuntime& trackRuntime : trackRuntimes_)
    {
        if (trackRuntime.track_ && trackRuntime.runtime_)
            trackRuntime.track_->OnTimelinePaused(*this, *trackRuntime.runtime_, paused);
    }
}

void TimelinePlayer::SendTimelineEvent(StringHash eventType, VariantMap parameters)
{
    Node* node = GetNode();
    if (!node)
        return;

    VariantMap& eventData = GetEventDataMap();
    eventData[TimelinePlayed::P_NODE] = node;
    eventData[TimelinePlayed::P_TIMELINE] = timeline_.Get();
    for (const auto& parameter : parameters)
        eventData[parameter.first] = parameter.second;

    node->SendEvent(eventType, eventData);
}

}

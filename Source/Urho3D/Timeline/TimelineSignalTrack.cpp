// Copyright (c) 2022-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#include <Urho3D/Precompiled.h>

#include <Urho3D/Core/ObjectCategory.h>
#include <Urho3D/IO/ArchiveSerialization.h>
#include <Urho3D/IO/ArchiveSerializationVariant.h>
#include <Urho3D/Timeline/TimelinePlayer.h>
#include <Urho3D/Timeline/TimelineSignalTrack.h>

namespace Urho3D
{

void TimelineSignalClip::SerializeInBlock(Archive& archive)
{
    TimelineClip::SerializeInBlock(archive);

    SerializeOptionalValue(archive, "signal", signal_);
    SerializeOptionalValue(archive, "data", data_);
    SerializeOptionalValue(archive, "emitOnce", emitOnce_, false);
    SerializeOptionalValue(archive, "retroactive", retroactive_, false);
}

TimelineSignalTrack::TimelineSignalTrack(Context* context) :
    TimelineTrack(context)
{
}

TimelineSignalTrack::~TimelineSignalTrack() = default;

void TimelineSignalTrack::RegisterObject(Context* context)
{
    context->AddFactoryReflection<TimelineSignalTrack>(Category_Timeline);
}

void TimelineSignalTrack::SerializeInBlock(Archive& archive)
{
    TimelineTrack::SerializeInBlock(archive);
    SerializeVectorAsObjects(archive, "clips", clips_, "clip");
}

float TimelineSignalTrack::GetContentDuration() const
{
    float duration = 0.0f;
    for (const TimelineSignalClip& clip : clips_)
        duration = Max(duration, clip.GetEndTime());
    return duration;
}

ea::unique_ptr<TimelineTrackRuntime> TimelineSignalTrack::CreateRuntime() const
{
    return ea::make_unique<Runtime>();
}

void TimelineSignalTrack::Evaluate(TimelineTrackContext& context)
{
    Runtime& runtime = static_cast<Runtime&>(context.runtime_);
    runtime.clips_.resize(clips_.size());

    if (IsInactive(context))
        return;

    for (unsigned i = 0; i < clips_.size(); ++i)
    {
        const TimelineSignalClip& clip = clips_[i];
        Runtime::ClipState& state = runtime.clips_[i];

        if (clip.signal_.empty())
            continue;

        // Signals fire when playback crosses the clip start time, in both directions.
        const float signalTime = clip.start_;

        bool fire = false;
        if (context.firstEvaluation_)
        {
            // Fire signals at the playback start time, and retroactively fire signals located before it.
            fire = signalTime <= context.time_ && (signalTime > context.prevTime_ || clip.retroactive_);
        }
        else if (context.time_ > context.prevTime_)
            fire = signalTime > context.prevTime_ && signalTime <= context.time_;
        else if (context.time_ < context.prevTime_)
            fire = signalTime >= context.time_ && signalTime < context.prevTime_;

        if (!fire)
            continue;

        if (clip.emitOnce_)
        {
            if (state.emitted_)
                continue;
            state.emitted_ = true;
        }

        context.player_.EmitSignal(clip.signal_, clip.data_, signalTime, clip.retroactive_);
    }
}

}

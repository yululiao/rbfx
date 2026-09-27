// Copyright (c) 2022-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#include <Urho3D/Precompiled.h>

#include <Urho3D/Audio/SoundSource.h>
#include <Urho3D/Core/ObjectCategory.h>
#include <Urho3D/IO/ArchiveSerialization.h>
#include <Urho3D/Resource/Resource.h>
#include <Urho3D/Scene/Node.h>
#include <Urho3D/Timeline/TimelineAudioTrack.h>
#include <Urho3D/Timeline/TimelinePlayer.h>

namespace Urho3D
{

namespace
{

/// Maximum allowed deviation of the playing sound position from the expected position, in seconds.
const float AUDIO_SYNC_TOLERANCE = 0.1f;

/// Return effective playback rate of the clip.
float GetPlaybackRate(const TimelineAudioClip& clip)
{
    return Max(clip.timeScale_ * clip.pitch_, 0.01f);
}

/// Return absolute playback frequency of the clip.
float GetPlaybackFrequency(const TimelineAudioClip& clip)
{
    return Clamp(clip.sound_->GetFrequency() * GetPlaybackRate(clip), 1.0f, 535232.0f);
}

/// Return expected position within the sound for given timeline time.
float GetExpectedSourceTime(const TimelineAudioClip& clip, float timelineTime)
{
    float localTime = 0.0f;
    clip.EvaluateLocalTime(timelineTime, localTime);
    return clip.clipIn_ + localTime * GetPlaybackRate(clip);
}

}

void TimelineAudioClip::SerializeInBlock(Archive& archive)
{
    TimelineClip::SerializeInBlock(archive);

    if (!archive.IsInput() && sound_)
        soundRef_ = GetResourceRef(sound_, Sound::GetTypeStatic());

    SerializeResource(archive, "sound", sound_, soundRef_);
    SerializeOptionalValue(archive, "volume", volume_, 1.0f);
    SerializeOptionalValue(archive, "pitch", pitch_, 1.0f);
}

TimelineAudioTrack::TimelineAudioTrack(Context* context) :
    TimelineTrack(context)
{
}

TimelineAudioTrack::~TimelineAudioTrack() = default;

void TimelineAudioTrack::RegisterObject(Context* context)
{
    context->AddFactoryReflection<TimelineAudioTrack>(Category_Timeline);
}

void TimelineAudioTrack::SerializeInBlock(Archive& archive)
{
    TimelineTrack::SerializeInBlock(archive);
    SerializeVectorAsObjects(archive, "clips", clips_, "clip");
}

float TimelineAudioTrack::GetContentDuration() const
{
    float duration = 0.0f;
    for (const TimelineAudioClip& clip : clips_)
        duration = Max(duration, clip.GetEndTime());
    return duration;
}

ea::unique_ptr<TimelineTrackRuntime> TimelineAudioTrack::CreateRuntime() const
{
    return ea::make_unique<Runtime>();
}

void TimelineAudioTrack::OnTimelineStopped(TimelinePlayer& player, TimelineTrackRuntime& runtime, bool finished)
{
    StopPlayback(static_cast<Runtime&>(runtime));
}

void TimelineAudioTrack::OnTimelinePaused(TimelinePlayer& player, TimelineTrackRuntime& runtime, bool paused)
{
    if (paused)
        StopPlayback(static_cast<Runtime&>(runtime));
}

SoundSource* TimelineAudioTrack::ResolveSource(TimelinePlayer& player, Runtime& runtime) const
{
    SoundSource* source = runtime.source_;
    if (source && !runtime.bindingDirty_)
        return source;

    Object* bound = player.ResolveTrackBinding(*this, runtime);
    if (auto* asSource = dynamic_cast<SoundSource*>(bound))
        source = asSource;
    else if (auto* asNode = dynamic_cast<Node*>(bound))
        source = asNode->GetComponent<SoundSource>();
    else
        source = nullptr;

    if (source != runtime.source_)
    {
        // Stop playback on the previous source before switching to another one.
        StopPlayback(runtime);
        runtime.source_ = source;
    }
    return source;
}

void TimelineAudioTrack::StopPlayback(Runtime& runtime)
{
    if (runtime.currentClip_ != M_MAX_UNSIGNED && runtime.source_)
        runtime.source_->Stop();
    runtime.currentClip_ = M_MAX_UNSIGNED;
}

void TimelineAudioTrack::StartPlayback(
    const TimelineTrackContext& context, const TimelineAudioClip& clip, SoundSource& source)
{
    const float gain = Max(clip.volume_ * clip.EvaluateWeight(context.time_) * weight_ * context.weightScale_, 0.0f);

    source.Play(clip.sound_, GetPlaybackFrequency(clip), gain);
    source.Seek(GetExpectedSourceTime(clip, context.time_));
}

void TimelineAudioTrack::UpdatePlayback(
    const TimelineTrackContext& context, const TimelineAudioClip& clip, SoundSource& source)
{
    const float gain = Max(clip.volume_ * clip.EvaluateWeight(context.time_) * weight_ * context.weightScale_, 0.0f);
    const float frequency = GetPlaybackFrequency(clip);
    const float sourceTime = GetExpectedSourceTime(clip, context.time_);

    if (source.GetSound() != clip.sound_)
    {
        // The sound was replaced externally: restart playback from the expected position.
        source.Play(clip.sound_, frequency, gain);
        source.Seek(sourceTime);
        return;
    }

    source.SetGain(gain);
    source.SetFrequency(frequency);

    const float soundLength = clip.sound_->GetLength();
    if (!source.IsPlaying())
    {
        // The sound has finished before the clip: restart only if the clip expects playback within the sound.
        // Clip extrapolation Loop or PingPong naturally triggers this when the source time wraps around.
        if (sourceTime < soundLength - M_EPSILON)
        {
            source.Play(clip.sound_, frequency, gain);
            source.Seek(sourceTime);
        }
        return;
    }

    // Correct drift to keep audio in sync with the timeline clock.
    if (Abs(source.GetTimePosition() - sourceTime) > AUDIO_SYNC_TOLERANCE)
        source.Seek(sourceTime);
}

void TimelineAudioTrack::Evaluate(TimelineTrackContext& context)
{
    Runtime& runtime = static_cast<Runtime&>(context.runtime_);

    SoundSource* source = IsInactive(context) ? nullptr : ResolveSource(context.player_, runtime);

    // Determine which clip should own the sound source. Overlapping clips of a single track are not supported,
    // the last active clip wins.
    unsigned newOwner = M_MAX_UNSIGNED;
    if (source && context.playing_)
    {
        for (unsigned i = 0; i < clips_.size(); ++i)
        {
            const TimelineAudioClip& clip = clips_[i];
            float localTime = 0.0f;
            if (!clip.sound_ || !clip.EvaluateLocalTime(context.time_, localTime))
                continue;
            if (clip.EvaluateWeight(context.time_) * weight_ * context.weightScale_ <= M_EPSILON)
                continue;
            newOwner = i;
        }
    }

    if (newOwner == runtime.currentClip_)
    {
        if (newOwner != M_MAX_UNSIGNED && source)
            UpdatePlayback(context, clips_[newOwner], *source);
        return;
    }

    // The owner changed: stop the previous playback and start the new one.
    StopPlayback(runtime);
    if (newOwner != M_MAX_UNSIGNED && source)
    {
        StartPlayback(context, clips_[newOwner], *source);
        runtime.currentClip_ = newOwner;
    }
}

}

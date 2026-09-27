// Copyright (c) 2022-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#pragma once

#include <Urho3D/Audio/Sound.h>
#include <Urho3D/Timeline/TimelineClip.h>
#include <Urho3D/Timeline/TimelineTrack.h>

namespace Urho3D
{

class SoundSource;

/// Clip of the audio track. Plays a Sound asset on the SoundSource of the bound object.
struct TimelineAudioClip : public TimelineClip
{
    /// Serialize clip fields.
    void SerializeInBlock(Archive& archive);

    /// Sound resource.
    SharedPtr<Sound> sound_;
    /// Resource reference used for serialization.
    ResourceRef soundRef_;
    /// Playback volume.
    float volume_{1.0f};
    /// Playback pitch multiplier applied on top of clip time scale.
    float pitch_{1.0f};
};

/// Audio track. Drives a SoundSource of the bound object. Clips of a single track must not overlap.
/// Bound object can be a SoundSource or a Node with SoundSource component.
class URHO3D_API TimelineAudioTrack : public TimelineTrack
{
    URHO3D_OBJECT(TimelineAudioTrack, TimelineTrack);

public:
    explicit TimelineAudioTrack(Context* context);
    ~TimelineAudioTrack() override;

    static void RegisterObject(Context* context);

    void SerializeInBlock(Archive& archive) override;
    float GetContentDuration() const override;
    ea::string GetTrackTypeName() const override { return "Audio"; }

    ea::unique_ptr<TimelineTrackRuntime> CreateRuntime() const override;
    void OnTimelineStopped(TimelinePlayer& player, TimelineTrackRuntime& runtime, bool finished) override;
    void OnTimelinePaused(TimelinePlayer& player, TimelineTrackRuntime& runtime, bool paused) override;
    void Evaluate(TimelineTrackContext& context) override;

    /// Audio clips.
    ea::vector<TimelineAudioClip> clips_;

    /// Runtime state of the audio track.
    struct Runtime : public TimelineTrackRuntime
    {
        /// Resolved sound source.
        WeakPtr<SoundSource> source_;
        /// Index of the clip that currently owns the sound source, or M_MAX_UNSIGNED.
        unsigned currentClip_{M_MAX_UNSIGNED};
    };

private:
    /// Resolve the bound sound source.
    SoundSource* ResolveSource(TimelinePlayer& player, Runtime& runtime) const;
    /// Stop playback owned by the track.
    static void StopPlayback(Runtime& runtime);
    /// Start playback of the clip.
    void StartPlayback(const TimelineTrackContext& context, const TimelineAudioClip& clip, SoundSource& source);
    /// Keep playing sound in sync with the timeline clock.
    void UpdatePlayback(const TimelineTrackContext& context, const TimelineAudioClip& clip, SoundSource& source);
};

}

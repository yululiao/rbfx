// Copyright (c) 2022-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#pragma once

#include <Urho3D/Timeline/TimelineClip.h>
#include <Urho3D/Timeline/TimelineTrack.h>

namespace Urho3D
{

/// Clip of the activation track. Sets enabled state of the bound node while the clip is active.
struct TimelineActivationClip : public TimelineClip
{
    /// Serialize clip fields.
    void SerializeInBlock(Archive& archive);

    /// Whether the bound node is activated while the clip is active.
    bool active_{true};
};

/// Activation track. Controls enabled state of the bound node.
/// While no clip is active, the bound node is deactivated.
class URHO3D_API TimelineActivationTrack : public TimelineTrack
{
    URHO3D_OBJECT(TimelineActivationTrack, TimelineTrack);

public:
    explicit TimelineActivationTrack(Context* context);
    ~TimelineActivationTrack() override;

    static void RegisterObject(Context* context);

    void SerializeInBlock(Archive& archive) override;
    float GetContentDuration() const override;
    ea::string GetTrackTypeName() const override { return "Activation"; }

    ea::unique_ptr<TimelineTrackRuntime> CreateRuntime() const override;
    void OnTimelineStopped(TimelinePlayer& player, TimelineTrackRuntime& runtime, bool finished) override;
    void Evaluate(TimelineTrackContext& context) override;

    /// Set state of the bound node applied when the timeline stops.
    void SetPostPlaybackState(TimelinePostPlaybackState state) { postPlaybackState_ = state; }
    TimelinePostPlaybackState GetPostPlaybackState() const { return postPlaybackState_; }

    /// Activation clips.
    ea::vector<TimelineActivationClip> clips_;

    /// Runtime state of the activation track.
    struct Runtime : public TimelineTrackRuntime
    {
        /// Whether the initial state of the bound node was saved.
        bool stateSaved_{};
        /// Enabled state of the bound node before the timeline started driving it.
        bool savedEnabled_{};
        /// Whether the track currently drives the bound node.
        bool applied_{};
        /// Current driven state of the bound node.
        bool currentActive_{};
    };

private:
    /// State of the bound node applied when the timeline stops.
    TimelinePostPlaybackState postPlaybackState_{TimelinePostPlaybackState::Revert};
};

}

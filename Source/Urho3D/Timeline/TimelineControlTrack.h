// Copyright (c) 2022-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#pragma once

#include <Urho3D/Scene/PrefabResource.h>
#include <Urho3D/Timeline/TimelineClip.h>
#include <Urho3D/Timeline/TimelineResource.h>
#include <Urho3D/Timeline/TimelineTrack.h>

namespace Urho3D
{

class Node;
class TimelinePlayer;

/// Clip of the control track. Spawns a prefab and/or plays a nested timeline while the clip is active.
struct TimelineControlClip : public TimelineClip
{
    /// Serialize clip fields.
    void SerializeInBlock(Archive& archive);

    /// Prefab to instantiate for the duration of the clip. Optional.
    SharedPtr<PrefabResource> prefab_;
    /// Resource reference of the prefab used for serialization.
    ResourceRef prefabRef_;
    /// Nested timeline to play on the spawned or bound object. Optional.
    SharedPtr<TimelineResource> timeline_;
    /// Resource reference of the nested timeline used for serialization.
    ResourceRef timelineRef_;
    /// Action applied to the controlled object when the clip ends.
    TimelinePostPlayback postPlayback_{TimelinePostPlayback::Destroy};
};

/// Control track. Spawns prefabs and controls nested timelines.
/// Bound object is used as parent of the spawned prefab, or as host of the nested timeline.
class URHO3D_API TimelineControlTrack : public TimelineTrack
{
    URHO3D_OBJECT(TimelineControlTrack, TimelineTrack);

public:
    explicit TimelineControlTrack(Context* context);
    ~TimelineControlTrack() override;

    static void RegisterObject(Context* context);

    void SerializeInBlock(Archive& archive) override;
    float GetContentDuration() const override;
    ea::string GetTrackTypeName() const override { return "Control"; }

    ea::unique_ptr<TimelineTrackRuntime> CreateRuntime() const override;
    void OnTimelineStopped(TimelinePlayer& player, TimelineTrackRuntime& runtime, bool finished) override;
    void OnTimelinePaused(TimelinePlayer& player, TimelineTrackRuntime& runtime, bool paused) override;
    void Evaluate(TimelineTrackContext& context) override;

    /// Control clips.
    ea::vector<TimelineControlClip> clips_;

    /// Runtime state of the control track.
    struct Runtime : public TimelineTrackRuntime
    {
        /// Per-clip state.
        struct ClipState
        {
            /// Spawned node, or host node of the nested timeline.
            WeakPtr<Node> node_;
            /// Nested timeline player.
            WeakPtr<TimelinePlayer> player_;
            /// Whether the node was spawned by this clip.
            bool spawned_{};
            /// Whether the clip is currently started.
            bool started_{};
        };

        /// Per-clip states.
        ea::vector<ClipState> clips_;
    };

private:
    /// Start the clip: spawn prefab and start nested timeline.
    void StartClip(TimelineTrackContext& context, const TimelineControlClip& clip, Runtime::ClipState& state);
    /// Stop the clip: stop nested timeline and apply post-playback action.
    void StopClip(const TimelineControlClip& clip, Runtime::ClipState& state);
};

}

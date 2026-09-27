// Copyright (c) 2022-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#pragma once

#include <Urho3D/Graphics/Animation.h>
#include <Urho3D/Timeline/TimelineClip.h>
#include <Urho3D/Timeline/TimelineTrack.h>

namespace Urho3D
{

class AnimationController;

/// Clip of the animation track. Plays an Animation asset on the bound AnimationController.
struct TimelineAnimationClip : public TimelineClip
{
    /// Serialize clip fields.
    void SerializeInBlock(Archive& archive);

    /// Animation resource.
    SharedPtr<Animation> animation_;
    /// Resource reference used for serialization.
    ResourceRef animationRef_;
    /// Whether the animation is played with additive blending.
    bool additive_{false};
    /// Animation layer.
    unsigned layer_{0};
    /// Optional start bone.
    ea::string startBone_;
};

/// Animation track. Drives an AnimationController of the bound object.
/// Bound object can be an AnimationController or a Node with AnimationController component.
class URHO3D_API TimelineAnimationTrack : public TimelineTrack
{
    URHO3D_OBJECT(TimelineAnimationTrack, TimelineTrack);

public:
    explicit TimelineAnimationTrack(Context* context);
    ~TimelineAnimationTrack() override;

    static void RegisterObject(Context* context);

    void SerializeInBlock(Archive& archive) override;
    float GetContentDuration() const override;
    ea::string GetTrackTypeName() const override { return "Animation"; }

    ea::unique_ptr<TimelineTrackRuntime> CreateRuntime() const override;
    void OnTimelineStopped(TimelinePlayer& player, TimelineTrackRuntime& runtime, bool finished) override;
    void OnTimelinePaused(TimelinePlayer& player, TimelineTrackRuntime& runtime, bool paused) override;
    void Evaluate(TimelineTrackContext& context) override;

    /// Animation clips.
    ea::vector<TimelineAnimationClip> clips_;

    /// Runtime state of the animation track.
    struct Runtime : public TimelineTrackRuntime
    {
        /// Per-clip state.
        struct ClipState
        {
            /// Index of the animation instance in the bound controller.
            unsigned controllerIndex_{M_MAX_UNSIGNED};
            /// Instance index of the animation instance, used to re-find the instance if indices shift.
            unsigned instanceIndex_{M_MAX_UNSIGNED};
            /// Whether the clip currently owns an animation instance.
            bool active_{};
        };

        /// Per-clip states.
        ea::vector<ClipState> clips_;
        /// Resolved animation controller.
        WeakPtr<AnimationController> controller_;
    };

private:
    /// Resolve the bound animation controller.
    AnimationController* ResolveController(TimelineTrackContext& context) const;
    /// Remove animation instances owned by this track from given controller.
    void ReleaseInstances(TimelinePlayer& player, Runtime& runtime);
};

}

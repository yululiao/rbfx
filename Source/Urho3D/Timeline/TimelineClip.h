// Copyright (c) 2022-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#pragma once

#include <Urho3D/Timeline/TimelineDefs.h>

namespace Urho3D
{

class Archive;

/// Base data of a timeline clip: time range within the timeline, trimming, playback speed and easing.
struct TimelineClip
{
    TimelineClip() = default;
    TimelineClip(float start, float duration) :
        start_(start),
        duration_(duration)
    {
    }

    /// Serialize common clip fields.
    void SerializeInBlock(Archive& archive);

    /// Return end time of the clip within the timeline.
    float GetEndTime() const { return start_ + duration_; }

    /// Evaluate activity of the clip at given timeline time.
    /// Return true if the clip is active. If active, localTime is set within [0, duration]
    /// for most extrapolation modes.
    bool EvaluateLocalTime(float timelineTime, float& localTime) const;

    /// Evaluate effective clip weight at given timeline time. Result is within [0, weight_].
    /// Outside of the clip time range returns full weight for Hold and Continue modes and zero otherwise.
    float EvaluateWeight(float timelineTime) const;

    /// Return source time of the clip for given local time, respecting trimming and speed.
    float GetSourceTime(float localTime) const { return clipIn_ + localTime * timeScale_; }

    /// Clip name. Optional, used for debugging and events.
    ea::string name_;
    /// Start time within the timeline.
    float start_{};
    /// Duration within the timeline.
    float duration_{1.0f};
    /// Offset into the source content (animation or audio).
    float clipIn_{};
    /// Playback speed of the clip.
    float timeScale_{1.0f};
    /// Duration of the ease-in ramp at the clip start.
    float easeIn_{};
    /// Duration of the ease-out ramp at the clip end.
    float easeOut_{};
    /// Easing function of ease-in and ease-out ramps.
    TimelineEase ease_{TimelineEase::Linear};
    /// Behavior when timeline time is outside of the clip time range.
    TimelineExtrapolation extrapolation_{TimelineExtrapolation::None};
    /// Clip weight.
    float weight_{1.0f};
};

}

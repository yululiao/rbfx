// Copyright (c) 2022-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#pragma once

#include <Urho3D/Math/MathDefs.h>

#include <cmath>

namespace Urho3D
{

/// How timeline playback behaves when reaching the end of the timeline.
enum class TimelineWrapMode
{
    /// Stop playback upon reaching the end.
    None = 0,
    /// Clamp time at the timeline end and keep evaluating.
    Hold,
    /// Loop playback.
    Loop,
};

/// How the timeline player advances its clock.
enum class TimelineTimeUpdateMode
{
    /// Time is scaled by Scene time scale.
    GameTime = 0,
    /// Time ignores Scene time scale.
    UnscaledGameTime,
    /// Time is only set manually via SetTime() / Evaluate().
    Manual,
};

/// How a clip behaves when timeline time is outside the clip time range.
enum class TimelineExtrapolation
{
    /// Clip is inactive outside its time range.
    None = 0,
    /// Clip holds its first or last frame outside its time range.
    Hold,
    /// Clip loops within its time range.
    Loop,
    /// Clip ping-pongs within its time range.
    PingPong,
    /// Clip keeps playing outside its time range.
    Continue,
};

/// Easing function applied to clip ease in/out ramps.
enum class TimelineEase
{
    /// Linear ramp.
    Linear = 0,
    /// Quadratic ease in.
    InQuad,
    /// Quadratic ease out.
    OutQuad,
    /// Quadratic ease in-out.
    InOutQuad,
    /// Cubic ease in.
    InCubic,
    /// Cubic ease out.
    OutCubic,
    /// Cubic ease in-out.
    InOutCubic,
    /// Sine ease in.
    InSine,
    /// Sine ease out.
    OutSine,
    /// Sine ease in-out.
    InOutSine,
};

/// What happens to objects controlled by a Control Track clip when the clip ends.
enum class TimelinePostPlayback
{
    /// Destroy the spawned object.
    Destroy = 0,
    /// Disable the spawned object.
    Disable,
    /// Destroy the spawned object and revert modifications made by the clip.
    Revert,
    /// Leave the spawned object as is.
    Keep,
};

/// What happens to nodes activated by an Activation Track when the timeline stops.
enum class TimelinePostPlaybackState
{
    /// Revert nodes to their state before the timeline started.
    Revert = 0,
    /// Keep nodes activated.
    Active,
    /// Keep nodes deactivated.
    Inactive,
    /// Leave nodes in their current state.
    LeaveAsIs,
};

/// How timeline duration is determined.
enum class TimelineDurationMode
{
    /// Timeline duration is fixed and set on the resource.
    Fixed = 0,
    /// Timeline duration is computed from tracks and markers.
    Content,
};

/// @name Serialization constants.
/// @{
inline const char* TIMELINE_WRAP_MODE_NAMES[] = {"None", "Hold", "Loop", nullptr};
inline const char* TIMELINE_TIME_UPDATE_MODE_NAMES[] = {"GameTime", "UnscaledGameTime", "Manual", nullptr};
inline const char* TIMELINE_EXTRAPOLATION_NAMES[] = {"None", "Hold", "Loop", "PingPong", "Continue", nullptr};
inline const char* TIMELINE_EASE_NAMES[] = {"Linear", "InQuad", "OutQuad", "InOutQuad", "InCubic", "OutCubic",
    "InOutCubic", "InSine", "OutSine", "InOutSine", nullptr};
inline const char* TIMELINE_POST_PLAYBACK_NAMES[] = {"Destroy", "Disable", "Revert", "Keep", nullptr};
inline const char* TIMELINE_POST_PLAYBACK_STATE_NAMES[] = {"Revert", "Active", "Inactive", "LeaveAsIs", nullptr};
inline const char* TIMELINE_DURATION_MODE_NAMES[] = {"Fixed", "Content", nullptr};
/// @}

/// Evaluate easing function. Input and output are in [0, 1] range.
inline float EvaluateTimelineEase(TimelineEase ease, float time)
{
    time = Clamp(time, 0.0f, 1.0f);
    switch (ease)
    {
    case TimelineEase::Linear:
        return time;
    case TimelineEase::InQuad:
        return time * time;
    case TimelineEase::OutQuad:
        return time * (2.0f - time);
    case TimelineEase::InOutQuad:
        return time < 0.5f ? 2.0f * time * time : 1.0f - 2.0f * (1.0f - time) * (1.0f - time);
    case TimelineEase::InCubic:
        return time * time * time;
    case TimelineEase::OutCubic:
    {
        const float inverted = 1.0f - time;
        return 1.0f - inverted * inverted * inverted;
    }
    case TimelineEase::InOutCubic:
    {
        if (time < 0.5f)
            return 4.0f * time * time * time;
        const float inverted = 1.0f - time;
        return 1.0f - 4.0f * inverted * inverted * inverted;
    }
    case TimelineEase::InSine:
        return 1.0f - cosf(time * M_PI * 0.5f);
    case TimelineEase::OutSine:
        return sinf(time * M_PI * 0.5f);
    case TimelineEase::InOutSine:
        return 0.5f * (1.0f - cosf(time * M_PI));
    default:
        return time;
    }
}

}

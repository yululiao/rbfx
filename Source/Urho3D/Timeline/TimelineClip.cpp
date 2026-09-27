// Copyright (c) 2022-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#include <Urho3D/Precompiled.h>

#include <Urho3D/IO/ArchiveSerialization.h>
#include <Urho3D/Timeline/TimelineClip.h>

namespace Urho3D
{

void TimelineClip::SerializeInBlock(Archive& archive)
{
    SerializeOptionalValue(archive, "name", name_);
    SerializeValue(archive, "start", start_);
    SerializeValue(archive, "duration", duration_);
    SerializeOptionalValue(archive, "clipIn", clipIn_, 0.0f);
    SerializeOptionalValue(archive, "timeScale", timeScale_, 1.0f);
    SerializeOptionalValue(archive, "easeIn", easeIn_, 0.0f);
    SerializeOptionalValue(archive, "easeOut", easeOut_, 0.0f);
    SerializeOptionalValue(archive, "ease", ease_, TimelineEase::Linear,
        [](Archive& archive, const char* name, auto& value) //
        { SerializeEnum(archive, name, value, TIMELINE_EASE_NAMES); });
    SerializeOptionalValue(archive, "extrapolation", extrapolation_, TimelineExtrapolation::None,
        [](Archive& archive, const char* name, auto& value) //
        { SerializeEnum(archive, name, value, TIMELINE_EXTRAPOLATION_NAMES); });
    SerializeOptionalValue(archive, "weight", weight_, 1.0f);
}

bool TimelineClip::EvaluateLocalTime(float timelineTime, float& localTime) const
{
    const float end = GetEndTime();

    if (timelineTime >= start_ && timelineTime <= end)
    {
        localTime = timelineTime - start_;
        return true;
    }

    switch (extrapolation_)
    {
    case TimelineExtrapolation::None:
        return false;

    case TimelineExtrapolation::Hold:
        localTime = timelineTime < start_ ? 0.0f : duration_;
        return true;

    case TimelineExtrapolation::Loop:
    {
        if (duration_ <= M_EPSILON)
        {
            localTime = 0.0f;
            return true;
        }
        const float wrapped = fmodf(timelineTime - start_, duration_);
        localTime = wrapped < 0.0f ? wrapped + duration_ : wrapped;
        return true;
    }

    case TimelineExtrapolation::PingPong:
    {
        if (duration_ <= M_EPSILON)
        {
            localTime = 0.0f;
            return true;
        }
        const float cycle = duration_ * 2.0f;
        const float wrapped = fmodf(timelineTime - start_, cycle);
        const float positive = wrapped < 0.0f ? wrapped + cycle : wrapped;
        localTime = positive <= duration_ ? positive : cycle - positive;
        return true;
    }

    case TimelineExtrapolation::Continue:
        localTime = timelineTime - start_;
        return true;

    default:
        return false;
    }
}

float TimelineClip::EvaluateWeight(float timelineTime) const
{
    if (weight_ <= M_EPSILON)
        return 0.0f;

    // Looping clips are blended with constant weight to avoid weight dips at loop boundaries.
    if (extrapolation_ == TimelineExtrapolation::Loop || extrapolation_ == TimelineExtrapolation::PingPong)
        return weight_;

    // Clamp local time so that Hold and Continue modes keep the weight of the nearest edge.
    const float local = Clamp(timelineTime - start_, 0.0f, duration_);
    float result = weight_;

    if (easeIn_ > M_EPSILON)
        result = Min(result, weight_ * EvaluateTimelineEase(ease_, local / easeIn_));
    if (easeOut_ > M_EPSILON)
        result = Min(result, weight_ * EvaluateTimelineEase(ease_, (duration_ - local) / easeOut_));

    return Clamp(result, 0.0f, weight_);
}

}

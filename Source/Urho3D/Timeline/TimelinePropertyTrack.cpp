// Copyright (c) 2022-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#include <Urho3D/Precompiled.h>

#include <Urho3D/Core/ObjectCategory.h>
#include <Urho3D/IO/ArchiveSerialization.h>
#include <Urho3D/Scene/Serializable.h>
#include <Urho3D/Scene/Node.h>
#include <Urho3D/Timeline/TimelinePlayer.h>
#include <Urho3D/Timeline/TimelinePropertyTrack.h>

namespace Urho3D
{

namespace
{

/// Interpolate between two keyframe values of the same type.
Variant LerpVariantValues(const Variant& lhs, const Variant& rhs, float t)
{
    switch (lhs.GetType())
    {
    case VAR_FLOAT:
        return Lerp(lhs.GetFloat(), rhs.GetFloat(), t);
    case VAR_DOUBLE:
        return Lerp(lhs.GetDouble(), rhs.GetDouble(), t);
    case VAR_VECTOR2:
        return lhs.GetVector2().Lerp(rhs.GetVector2(), t);
    case VAR_VECTOR3:
        return lhs.GetVector3().Lerp(rhs.GetVector3(), t);
    case VAR_VECTOR4:
        return lhs.GetVector4().Lerp(rhs.GetVector4(), t);
    case VAR_QUATERNION:
        return lhs.GetQuaternion().Slerp(rhs.GetQuaternion(), t);
    case VAR_COLOR:
        return lhs.GetColor().Lerp(rhs.GetColor(), t);
    default:
        // Non-interpolatable types step to the next keyframe.
        return t < 1.0f ? lhs : rhs;
    }
}

/// Sample a cubic Hermite segment. p0/p1 are the endpoint values, m0/m1 the tangents
/// (dValue/dTime), span the segment length in time, t the normalized position in [0, 1].
inline float HermiteScalar(float p0, float p1, float m0, float m1, float t, float span)
{
    const float t2 = t * t;
    const float t3 = t2 * t;
    const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
    const float h10 = t3 - 2.0f * t2 + t;
    const float h01 = -2.0f * t3 + 3.0f * t2;
    const float h11 = t3 - t2;
    return h00 * p0 + h10 * span * m0 + h01 * p1 + h11 * span * m1;
}

/// Hermite for a scalar key. Non-scalar types have no single well-defined dValue/dTime tangent
/// (per-channel tangent curves are a future extension), so they fall back to linear.
Variant HermiteVariantValue(const Variant& lhs, const Variant& rhs, float m0, float m1, float t, float span)
{
    switch (lhs.GetType())
    {
    case VAR_FLOAT:
        return HermiteScalar(lhs.GetFloat(), rhs.GetFloat(), m0, m1, t, span);
    case VAR_DOUBLE:
        return static_cast<double>(
            HermiteScalar(static_cast<float>(lhs.GetDouble()), static_cast<float>(rhs.GetDouble()), m0, m1, t, span));
    default:
        return LerpVariantValues(lhs, rhs, t);
    }
}

/// Sample the curve at given source time. Keyframes are expected to be sorted by time.
/// Return false if the curve has no keyframes.
bool EvaluatePropertyCurve(const TimelinePropertyCurve& curve, float time, Variant& result)
{
    const ea::vector<TimelinePropertyKey>& keys = curve.keys_;
    if (keys.empty())
        return false;

    if (time <= keys.front().time_)
    {
        result = keys.front().value_;
        return true;
    }

    for (unsigned i = 1; i < keys.size(); ++i)
    {
        const TimelinePropertyKey& from = keys[i - 1];
        if (time < keys[i].time_)
        {
            const TimelinePropertyKey& to = keys[i];
            const float span = to.time_ - from.time_;
            const float rawT = (time - from.time_) / span;
            switch (from.interpolation_)
            {
            case TimelineInterpolation::Constant:
                // Hold the left key value across the segment.
                result = from.value_;
                break;
            case TimelineInterpolation::Linear:
                result = LerpVariantValues(from.value_, to.value_, rawT);
                break;
            case TimelineInterpolation::Bezier:
                result = HermiteVariantValue(from.value_, to.value_, from.outTangent_, to.inTangent_, rawT, span);
                break;
            case TimelineInterpolation::Ease:
            default:
                result = LerpVariantValues(from.value_, to.value_, EvaluateTimelineEase(from.ease_, rawT));
                break;
            }
            return true;
        }
    }

    result = keys.back().value_;
    return true;
}

/// Find attribute of the target by exact name. Return null if not found.
const AttributeInfo* FindAttribute(const Serializable& target, const ea::string& name)
{
    const auto* attributes = target.GetAttributes();
    if (!attributes)
        return nullptr;

    for (const AttributeInfo& info : *attributes)
    {
        if (info.name_ == name)
            return &info;
    }
    return nullptr;
}

}

void TimelinePropertyKey::SerializeInBlock(Archive& archive, VariantType variantType)
{
    SerializeValue(archive, "time", time_);
    if (variantType != VAR_NONE)
        SerializeVariantAsType(archive, "value", value_, variantType);
    SerializeOptionalValue(archive, "ease", ease_, TimelineEase::Linear,
        [](Archive& archive, const char* name, auto& value) //
        { SerializeEnum(archive, name, value, TIMELINE_EASE_NAMES); });
    // Interpolation + tangents are optional so files authored before this change (which only had
    // "ease") decode to the legacy Ease behaviour with flat tangents.
    SerializeOptionalValue(archive, "interp", interpolation_, TimelineInterpolation::Ease,
        [](Archive& archive, const char* name, auto& value) //
        { SerializeEnum(archive, name, value, TIMELINE_INTERPOLATION_NAMES); });
    SerializeOptionalValue(archive, "outTangent", outTangent_, 0.0f);
    SerializeOptionalValue(archive, "inTangent", inTangent_, 0.0f);
}

void TimelinePropertyCurve::SerializeInBlock(Archive& archive)
{
    SerializeValue(archive, "attribute", name_);
    SerializeValue(archive, "type", valueType_);

    // Value of each key is serialized as the type of the curve.
    // Every key must be wrapped into its own block: the surrounding 'keys' block is an Array
    // whose expected element count equals the number of keys, and a key consists of several
    // fields which would otherwise overflow that budget and trip the archive assertions.
    SerializeVectorAsObjects(archive, "keys", keys_, "key",
        [variantType = valueType_](Archive& archive, const char* name, TimelinePropertyKey& key) //
        {
            ArchiveBlock block = archive.OpenUnorderedBlock(name);
            key.SerializeInBlock(archive, variantType);
        });
}

void TimelinePropertyClip::SerializeInBlock(Archive& archive)
{
    TimelineClip::SerializeInBlock(archive);
    SerializeVectorAsObjects(archive, "curves", curves_, "curve");
}

TimelinePropertyTrack::TimelinePropertyTrack(Context* context) :
    TimelineTrack(context)
{
}

TimelinePropertyTrack::~TimelinePropertyTrack() = default;

void TimelinePropertyTrack::RegisterObject(Context* context)
{
    context->AddFactoryReflection<TimelinePropertyTrack>(Category_Timeline);
}

void TimelinePropertyTrack::SerializeInBlock(Archive& archive)
{
    TimelineTrack::SerializeInBlock(archive);
    SerializeOptionalValue(archive, "postPlayback", postPlaybackState_, TimelinePostPlaybackState::Revert,
        [](Archive& archive, const char* name, auto& value) //
        { SerializeEnum(archive, name, value, TIMELINE_POST_PLAYBACK_STATE_NAMES); });
    SerializeVectorAsObjects(archive, "clips", clips_, "clip");
}

float TimelinePropertyTrack::GetContentDuration() const
{
    float duration = 0.0f;
    for (const TimelinePropertyClip& clip : clips_)
        duration = Max(duration, clip.GetEndTime());
    return duration;
}

ea::unique_ptr<TimelineTrackRuntime> TimelinePropertyTrack::CreateRuntime() const
{
    return ea::make_unique<Runtime>();
}

void TimelinePropertyTrack::OnTimelineStopped(TimelinePlayer& player, TimelineTrackRuntime& runtime, bool finished)
{
    Runtime& trackRuntime = static_cast<Runtime&>(runtime);
    if (!trackRuntime.driving_)
        return;

    // Serializable rather than Animatable: Node derives from Serializable directly, so
    // binding a property track to a node must animate its transform attributes as well.
    if (auto* target = dynamic_cast<Serializable*>(trackRuntime.boundObject_.Get()))
    {
        if (postPlaybackState_ == TimelinePostPlaybackState::Revert)
        {
            // Restore the values saved before the track first wrote them.
            for (const auto& [name, savedValue] : trackRuntime.savedValues_)
            {
                if (const AttributeInfo* attributeInfo = FindAttribute(*target, name))
                {
                    target->OnSetAttribute(*attributeInfo, savedValue);
                    target->ApplyAttributes();
                }
            }
        }
        // For other post playback states the last animated values are kept as is.
    }

    trackRuntime.driving_ = false;
    trackRuntime.attributeCache_.clear();
    trackRuntime.savedValues_.clear();
    trackRuntime.lastValues_.clear();
}

void TimelinePropertyTrack::Evaluate(TimelineTrackContext& context)
{
    Runtime& runtime = static_cast<Runtime&>(context.runtime_);

    // Muted and zero-weight tracks do not write values. Current values are kept as is.
    if (IsInactive(context))
        return;

    const bool bindingDirty = context.runtime_.bindingDirty_;
    Object* bound = context.player_.ResolveTrackBinding(*this, context.runtime_);
    if (bindingDirty)
        runtime.attributeCache_.clear();

    auto* target = dynamic_cast<Serializable*>(bound);
    if (!target)
        return;

    // The last active clip wins.
    const TimelinePropertyClip* activeClip = nullptr;
    float localTime = 0.0f;
    for (const TimelinePropertyClip& clip : clips_)
    {
        if (!clip.curves_.empty() && clip.EvaluateLocalTime(context.time_, localTime))
            activeClip = &clip;
    }

    if (!activeClip)
        return;

    const float sourceTime = activeClip->GetSourceTime(localTime);

    for (const TimelinePropertyCurve& curve : activeClip->curves_)
    {
        // Resolve and cache the attribute. Null in the cache marks an invalid attribute.
        const AttributeInfo* attributeInfo = nullptr;
        const auto cacheIter = runtime.attributeCache_.find(curve.name_);
        if (cacheIter != runtime.attributeCache_.end())
        {
            attributeInfo = cacheIter->second;
        }
        else
        {
            attributeInfo = curve.valueType_ != VAR_NONE ? FindAttribute(*target, curve.name_) : nullptr;
            if (attributeInfo && attributeInfo->type_ != curve.valueType_)
                attributeInfo = nullptr;

            if (!attributeInfo)
            {
                URHO3D_LOGWARNING(
                    "Timeline property track: attribute '{}' with type '{}' is not found on '{}'",
                    curve.name_, Variant::GetTypeName(curve.valueType_), target->GetTypeName());
            }
            runtime.attributeCache_.emplace(curve.name_, attributeInfo);
        }

        if (!attributeInfo)
            continue;

        Variant value;
        if (!EvaluatePropertyCurve(curve, sourceTime, value))
            continue;

        // Skip redundant writes to avoid propagating dirty state every frame.
        const auto lastIter = runtime.lastValues_.find(curve.name_);
        if (lastIter != runtime.lastValues_.end() && lastIter->second == value)
            continue;

        // Save the original value before the first write, for the post playback revert.
        if (runtime.savedValues_.find(curve.name_) == runtime.savedValues_.end())
        {
            Variant original;
            target->OnGetAttribute(*attributeInfo, original);
            runtime.savedValues_.emplace(curve.name_, original);
        }

        target->OnSetAttribute(*attributeInfo, value);
        target->ApplyAttributes();
        runtime.lastValues_[curve.name_] = value;
        runtime.driving_ = true;
    }
}

}

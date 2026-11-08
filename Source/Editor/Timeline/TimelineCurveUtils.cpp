//
// Copyright (c) 2022-2024 the rbfx project.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//

#include "TimelineCurveUtils.h"

#include <Urho3D/Math/Color.h>
#include <Urho3D/Math/Quaternion.h>
#include <Urho3D/Math/Vector2.h>
#include <Urho3D/Math/Vector3.h>
#include <Urho3D/Math/Vector4.h>
#include <Urho3D/Timeline/TimelineDefs.h>

#include <EASTL/algorithm.h>
#include <EASTL/set.h>
#include <EASTL/sort.h>

#include <cfloat>

namespace Urho3D
{

void AddPropertyCurve(TimelinePropertyClip& clip, const ea::string& name, VariantType type)
{
    if (name.empty())
        return;

    TimelinePropertyCurve curve;
    curve.name_ = name;
    curve.valueType_ = type;
    // A single key at the clip start with the default value of the type
    TimelinePropertyKey key;
    key.time_ = 0.0f;
    key.value_ = Variant(type);
    curve.keys_.push_back(key);
    clip.curves_.push_back(curve);
}

ea::vector<float> GetPropertyKeyTimes(const TimelinePropertyClip& clip)
{
    ea::set<float> times;
    for (const auto& curve : clip.curves_)
    {
        for (const auto& key : curve.keys_)
            times.insert(key.time_);
    }
    return ea::vector<float>(times.begin(), times.end());
}

// Distance in seconds below which two key times count as the same key row of a clip. The curve
// editors of the tab, the sheet and the curve window all group keys by time, and this is the
// window inside which a snapped click is the row that is already there.
constexpr float KeyRowEps = 0.0001f;

bool HasPropertyKeyRowAt(const TimelinePropertyClip& clip, float localTime)
{
    for (const auto& curve : clip.curves_)
    {
        for (const auto& key : curve.keys_)
        {
            if (Abs(key.time_ - localTime) < KeyRowEps)
                return true;
        }
    }
    return false;
}

void AddPropertyKeyAt(TimelinePropertyClip& clip, float localTime)
{
    // Two keys at one time leave the segment between them undefined, so an existing row is never
    // inserted a second time.
    if (HasPropertyKeyRowAt(clip, localTime))
        return;

    for (auto& curve : clip.curves_)
    {
        // Copy the value from the nearest key, or use the default value of the curve
        const TimelinePropertyKey* nearest = nullptr;
        float bestDist = FLT_MAX;
        for (const auto& key : curve.keys_)
        {
            const float dist = Abs(key.time_ - localTime);
            if (dist < bestDist)
            {
                bestDist = dist;
                nearest = &key;
            }
        }

        TimelinePropertyKey newKey;
        newKey.time_ = localTime;
        newKey.value_ = nearest ? nearest->value_ : Variant(curve.valueType_);

        unsigned insertAt = curve.keys_.size();
        for (unsigned i = 0; i < curve.keys_.size(); ++i)
        {
            if (curve.keys_[i].time_ > localTime)
            {
                insertAt = i;
                break;
            }
        }
        curve.keys_.insert(curve.keys_.begin() + static_cast<long>(insertAt), newKey);
    }
}

void MovePropertyKeysAt(TimelinePropertyClip& clip, float fromLocal, float toLocal)
{
    for (auto& curve : clip.curves_)
    {
        bool moved = false;
        for (auto& key : curve.keys_)
        {
            if (key.time_ == fromLocal)
            {
                key.time_ = toLocal;
                moved = true;
            }
        }
        if (moved)
            ea::stable_sort(curve.keys_.begin(), curve.keys_.end(),
                [](const TimelinePropertyKey& lhs, const TimelinePropertyKey& rhs) { return lhs.time_ < rhs.time_; });
    }
}

void RemovePropertyKeysAt(TimelinePropertyClip& clip, float localTime)
{
    for (auto& curve : clip.curves_)
    {
        curve.keys_.erase(ea::remove_if(curve.keys_.begin(), curve.keys_.end(),
            [localTime](const TimelinePropertyKey& key) { return key.time_ == localTime; }), curve.keys_.end());
    }
}

int GetCurveComponentCount(VariantType type)
{
    switch (type)
    {
    case VAR_FLOAT:
    case VAR_DOUBLE:
    case VAR_INT:
    case VAR_BOOL:
        return 1;
    case VAR_VECTOR2:
    case VAR_INTVECTOR2:
        return 2;
    case VAR_VECTOR3:
    case VAR_INTVECTOR3:
        return 3;
    case VAR_VECTOR4:
    case VAR_COLOR:
    case VAR_QUATERNION:
        return 4;
    default:
        return 0;
    }
}

float GetCurveComponentValue(const Variant& value, int component)
{
    switch (value.GetType())
    {
    case VAR_FLOAT: return value.GetFloat();
    case VAR_DOUBLE: return static_cast<float>(value.GetDouble());
    case VAR_INT: return static_cast<float>(value.GetInt());
    case VAR_BOOL: return value.GetBool() ? 1.0f : 0.0f;
    case VAR_VECTOR2: return value.GetVector2().Data()[component];
    case VAR_VECTOR3: return value.GetVector3().Data()[component];
    case VAR_VECTOR4: return value.GetVector4().Data()[component];
    case VAR_INTVECTOR2: return static_cast<float>(value.GetIntVector2().Data()[component]);
    case VAR_INTVECTOR3: return static_cast<float>(value.GetIntVector3().Data()[component]);
    case VAR_COLOR: return value.GetColor().Data()[component];
    case VAR_QUATERNION: return value.GetQuaternion().Data()[component];
    default: return 0.0f;
    }
}

void SetCurveComponentValue(Variant& value, int component, float newValue)
{
    switch (value.GetType())
    {
    case VAR_FLOAT: value = newValue; break;
    case VAR_DOUBLE: value = static_cast<double>(newValue); break;
    case VAR_INT: value = RoundToInt(newValue); break;
    case VAR_BOOL: value = newValue >= 0.5f; break;
    case VAR_VECTOR2:
    {
        Vector2 v = value.GetVector2();
        v.MutableData()[component] = newValue;
        value = v;
        break;
    }
    case VAR_VECTOR3:
    {
        Vector3 v = value.GetVector3();
        v.MutableData()[component] = newValue;
        value = v;
        break;
    }
    case VAR_VECTOR4:
    {
        Vector4 v = value.GetVector4();
        switch (component)
        {
        case 0: v.x_ = newValue; break;
        case 1: v.y_ = newValue; break;
        case 2: v.z_ = newValue; break;
        default: v.w_ = newValue; break;
        }
        value = v;
        break;
    }
    case VAR_INTVECTOR2:
    {
        IntVector2 v = value.GetIntVector2();
        v.MutableData()[component] = RoundToInt(newValue);
        value = v;
        break;
    }
    case VAR_INTVECTOR3:
    {
        IntVector3 v = value.GetIntVector3();
        v.MutableData()[component] = RoundToInt(newValue);
        value = v;
        break;
    }
    case VAR_COLOR:
    {
        Color v = value.GetColor();
        switch (component)
        {
        case 0: v.r_ = newValue; break;
        case 1: v.g_ = newValue; break;
        case 2: v.b_ = newValue; break;
        default: v.a_ = newValue; break;
        }
        value = v;
        break;
    }
    case VAR_QUATERNION:
    {
        Quaternion v = value.GetQuaternion();
        switch (component)
        {
        case 0: v.w_ = newValue; break;
        case 1: v.x_ = newValue; break;
        case 2: v.y_ = newValue; break;
        default: v.z_ = newValue; break;
        }
        value = v;
        break;
    }
    default: break;
    }
}

float HermiteScalarCurve(float p0, float p1, float m0, float m1, float t, float span)
{
    const float t2 = t * t;
    const float t3 = t2 * t;
    return (2.0f * t3 - 3.0f * t2 + 1.0f) * p0
        + (t3 - 2.0f * t2 + t) * span * m0
        + (-2.0f * t3 + 3.0f * t2) * p1
        + (t3 - t2) * span * m1;
}

float EvaluateCurveComponent(const TimelinePropertyCurve& curve, float time, int component)
{
    const auto& keys = curve.keys_;
    if (keys.empty())
        return 0.0f;
    if (time <= keys.front().time_)
        return GetCurveComponentValue(keys.front().value_, component);

    for (unsigned i = 1; i < keys.size(); ++i)
    {
        const TimelinePropertyKey& from = keys[i - 1];
        if (time < keys[i].time_)
        {
            const TimelinePropertyKey& to = keys[i];
            const float span = to.time_ - from.time_;
            const float t = (time - from.time_) / span;
            const float p0 = GetCurveComponentValue(from.value_, component);
            const float p1 = GetCurveComponentValue(to.value_, component);
            switch (from.interpolation_)
            {
            case TimelineInterpolation::Constant:
                return p0;
            case TimelineInterpolation::Linear:
                return Lerp(p0, p1, t);
            case TimelineInterpolation::Bezier:
                return HermiteScalarCurve(p0, p1, from.outTangent_, to.inTangent_, t, span);
            case TimelineInterpolation::Ease:
            default:
                return Lerp(p0, p1, EvaluateTimelineEase(from.ease_, t));
            }
        }
    }
    return GetCurveComponentValue(keys.back().value_, component);
}

ImU32 GetCurveCanvasColor(unsigned index)
{
    static const ImU32 palette[] = {
        IM_COL32(255, 200, 60, 255),
        IM_COL32(80, 200, 255, 255),
        IM_COL32(140, 255, 120, 255),
        IM_COL32(255, 120, 200, 255),
        IM_COL32(200, 140, 255, 255),
        IM_COL32(255, 140, 80, 255),
    };
    return palette[index % (sizeof(palette) / sizeof(palette[0]))];
}

}

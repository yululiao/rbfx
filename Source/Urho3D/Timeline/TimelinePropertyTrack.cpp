// Copyright (c) 2022-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#include <Urho3D/Precompiled.h>

#include <Urho3D/Core/ObjectCategory.h>
#include <Urho3D/Core/StringUtils.h>
#include <Urho3D/Graphics/Material.h>
#include <Urho3D/Graphics/Texture.h>
#include <Urho3D/IO/ArchiveSerialization.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Component.h>
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

/// Prefix marking a curve that drives a shader parameter of a bound Material.
constexpr const char* kShaderParameterPrefix = "ShaderParameters/";
/// Prefix marking a curve that switches a texture unit of a bound Material.
constexpr const char* kTexturePrefix = "Textures/";

/// True if the string starts with the given null-terminated prefix.
bool HasPrefix(const ea::string& str, const char* prefix)
{
    const size_t length = strlen(prefix);
    return str.size() >= length && str.compare(0, length, prefix) == 0;
}

/// Write a curve targeted at a Material. Shader parameter and texture curves are addressed by
/// the prefixed channel name ("ShaderParameters/Name", "Textures/Name"); anything else is not
/// a material curve, because Material exposes no reflected attributes. The curve name is kept
/// as the state key so component-qualified channels stay distinct during revert.
void ApplyMaterialCurve(Material& material, const ea::string& curveName, const ea::string& channelName,
    const TimelinePropertyCurve& curve, float sourceTime, TimelinePropertyTrack::Runtime& runtime)
{
    Variant value;
    if (!EvaluatePropertyCurve(curve, sourceTime, value))
        return;

    // Skip redundant writes to avoid reuploading shader constants every frame.
    const auto lastIter = runtime.lastValues_.find(curveName);
    if (lastIter != runtime.lastValues_.end() && lastIter->second == value)
        return;

    if (HasPrefix(channelName, kShaderParameterPrefix))
    {
        const ea::string parameterName = channelName.substr(strlen(kShaderParameterPrefix));
        // Save the original value before the first write, for the post playback revert.
        if (runtime.savedValues_.find(curveName) == runtime.savedValues_.end())
            runtime.savedValues_.emplace(curveName, material.GetShaderParameter(parameterName));
        if (value.IsEmpty())
            material.RemoveShaderParameter(parameterName);
        else
            material.SetShaderParameter(parameterName, value);
    }
    else if (HasPrefix(channelName, kTexturePrefix))
    {
        const ea::string textureName = channelName.substr(strlen(kTexturePrefix));
        if (runtime.savedValues_.find(curveName) == runtime.savedValues_.end())
        {
            Texture* original = material.GetTexture(StringHash(textureName));
            runtime.savedValues_.emplace(curveName, original
                ? Variant(ResourceRef(original->GetType(), GetResourceName(original)))
                : Variant::EMPTY);
        }
        auto* cache = material.GetSubsystem<ResourceCache>();
        Texture* texture = value.IsEmpty() ? nullptr : cache->GetResource<Texture>(value.GetResourceRef().name_);
        material.SetTexture(textureName, texture);
    }
    else
        return;

    runtime.lastValues_[curveName] = value;
    runtime.driving_ = true;
}

/// Resolve a component-qualified material curve name
/// ("ComponentType/AttrName[/Index]/ShaderParameters/Param"). Returns the material and fills
/// materialCurveName with the trailing "ShaderParameters/..." or "Textures/..." part.
Material* ResolveComponentMaterial(Object& bound, const ea::string& curveName, ea::string& materialCurveName)
{
    Node* node = nullptr;
    if (auto* nodeBound = dynamic_cast<Node*>(&bound))
        node = nodeBound;
    else if (auto* componentBound = dynamic_cast<Component*>(&bound))
        node = componentBound->GetNode();
    if (!node)
        return nullptr;

    // First segment: component type name.
    const auto firstSlash = curveName.find('/');
    if (firstSlash == ea::string::npos)
        return nullptr;
    const ea::string componentType = curveName.substr(0, firstSlash);

    // Second segment: resource attribute name.
    const ea::string rest = curveName.substr(firstSlash + 1);
    const auto secondSlash = rest.find('/');
    if (secondSlash == ea::string::npos)
        return nullptr;
    const ea::string attrName = rest.substr(0, secondSlash);
    const ea::string tail = rest.substr(secondSlash + 1);

    Component* component = node->GetComponent(StringHash(componentType));
    if (!component)
        return nullptr;

    const auto* attributes = component->GetAttributes();
    if (!attributes)
        return nullptr;

    for (const AttributeInfo& info : *attributes)
    {
        if (info.name_ != attrName)
            continue;

        Variant value;
        component->OnGetAttribute(info, value);
        auto* cache = component->GetSubsystem<ResourceCache>();

        if (info.type_ == VAR_RESOURCEREF)
        {
            // The tail is the material channel name: "ShaderParameters/..." or "Textures/...".
            if (value.IsEmpty())
                return nullptr;
            const ResourceRef& ref = value.GetResourceRef();
            if (ref.type_ != Material::GetTypeStatic())
                return nullptr;
            materialCurveName = tail;
            return cache->GetResource<Material>(ref.name_);
        }
        if (info.type_ == VAR_RESOURCEREFLIST)
        {
            // The tail is "Index/ShaderParameters/...".
            const auto thirdSlash = tail.find('/');
            if (thirdSlash == ea::string::npos)
                return nullptr;
            const ea::string indexStr = tail.substr(0, thirdSlash);
            const ResourceRefList& list = value.GetResourceRefList();
            if (list.type_ != Material::GetTypeStatic())
                return nullptr;
            const unsigned index = ToUInt(indexStr, M_MAX_UNSIGNED);
            if (index >= list.names_.size())
                return nullptr;
            materialCurveName = tail.substr(thirdSlash + 1);
            return cache->GetResource<Material>(list.names_[index]);
        }
        return nullptr;
    }
    return nullptr;
}

/// Resolve a component-qualified attribute path, e.g. "Light/Brightness" or, when several
/// components of the same type exist, "StaticModel/1/IsOccludee". Returns the addressed
/// component and writes the bare attribute name to attrName. Returns null when the curve
/// name does not start with a component type of the bound node.
Component* ResolveComponentAttribute(Object& bound, const ea::string& curveName, ea::string& attrName)
{
    Node* node = nullptr;
    if (auto* nodeBound = dynamic_cast<Node*>(&bound))
        node = nodeBound;
    else if (auto* componentBound = dynamic_cast<Component*>(&bound))
        node = componentBound->GetNode();
    if (!node)
        return nullptr;

    const auto firstSlash = curveName.find('/');
    if (firstSlash == ea::string::npos)
        return nullptr;
    const ea::string componentType = curveName.substr(0, firstSlash);
    const ea::string rest = curveName.substr(firstSlash + 1);

    // An all-digit middle segment is an index disambiguating same-type components.
    unsigned index = 0;
    const auto secondSlash = rest.find('/');
    if (secondSlash != ea::string::npos)
    {
        const ea::string segment = rest.substr(0, secondSlash);
        bool numeric = !segment.empty();
        for (char c : segment)
            numeric = numeric && c >= '0' && c <= '9';
        if (numeric)
        {
            index = ToUInt(segment);
            attrName = rest.substr(secondSlash + 1);
        }
        else
            attrName = rest;
    }
    else
        attrName = rest;

    const StringHash typeHash(componentType);
    unsigned count = 0;
    for (const auto& component : node->GetComponents())
    {
        if (component->GetType() != typeHash)
            continue;
        if (count == index)
            return component.Get();
        ++count;
    }
    return nullptr;
}

/// Restore a material curve value saved before the first write. Shader parameters that did not
/// exist before are removed; texture units are reset to the original texture or cleared.
void RestoreMaterialCurve(Material& material, const ea::string& name, const Variant& savedValue)
{
    if (HasPrefix(name, kShaderParameterPrefix))
    {
        const ea::string parameterName = name.substr(strlen(kShaderParameterPrefix));
        if (savedValue.IsEmpty())
            material.RemoveShaderParameter(parameterName);
        else
            material.SetShaderParameter(parameterName, savedValue);
    }
    else if (HasPrefix(name, kTexturePrefix))
    {
        const ea::string textureName = name.substr(strlen(kTexturePrefix));
        auto* cache = material.GetSubsystem<ResourceCache>();
        Texture* texture = savedValue.IsEmpty() ? nullptr
            : cache->GetResource<Texture>(savedValue.GetResourceRef().name_);
        material.SetTexture(textureName, texture);
    }
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
                // Directly bound materials store unprefixed channel names.
                if (auto* material = dynamic_cast<Material*>(target))
                {
                    if (HasPrefix(name, kShaderParameterPrefix) || HasPrefix(name, kTexturePrefix))
                        RestoreMaterialCurve(*material, name, savedValue);
                    continue;
                }

                // Component-qualified names resolve back to a material of a component.
                ea::string materialCurveName;
                if (Material* material = ResolveComponentMaterial(*target, name, materialCurveName))
                {
                    RestoreMaterialCurve(*material, materialCurveName, savedValue);
                    continue;
                }

                // Component-qualified names restore attributes of a component.
                ea::string attrName;
                if (Component* component = ResolveComponentAttribute(*target, name, attrName))
                {
                    if (const AttributeInfo* attributeInfo = FindAttribute(*component, attrName))
                    {
                        component->OnSetAttribute(*attributeInfo, savedValue);
                        component->ApplyAttributes();
                    }
                    continue;
                }

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
        // Materials are driven through their shader parameters and texture units, addressed by
        // prefixed curve names, because they expose no reflected attributes.
        if (auto* material = dynamic_cast<Material*>(target))
        {
            ApplyMaterialCurve(*material, curve.name_, curve.name_, curve, sourceTime, runtime);
            continue;
        }

        // Component-qualified curves address a material of a component on the bound node:
        // "ComponentType/AttrName[/Index]/ShaderParameters/Param".
        if (ea::string materialCurveName; Material* material = ResolveComponentMaterial(*target, curve.name_, materialCurveName))
        {
            ApplyMaterialCurve(*material, curve.name_, materialCurveName, curve, sourceTime, runtime);
            continue;
        }

        // Component-qualified names address attributes of a component of the bound node,
        // e.g. "Light/Brightness" or "StaticModel/1/IsOccludee".
        Serializable* writeTarget = target;
        ea::string attrName = curve.name_;
        if (Component* component = ResolveComponentAttribute(*target, curve.name_, attrName))
            writeTarget = component;

        // Resolve and cache the attribute. Null in the cache marks an invalid attribute.
        const AttributeInfo* attributeInfo = nullptr;
        const auto cacheIter = runtime.attributeCache_.find(curve.name_);
        if (cacheIter != runtime.attributeCache_.end())
        {
            attributeInfo = cacheIter->second;
        }
        else
        {
            attributeInfo = curve.valueType_ != VAR_NONE ? FindAttribute(*writeTarget, attrName) : nullptr;
            if (attributeInfo && attributeInfo->type_ != curve.valueType_)
                attributeInfo = nullptr;

            if (!attributeInfo)
            {
                URHO3D_LOGWARNING(
                    "Timeline property track: attribute '{}' with type '{}' is not found on '{}'",
                    curve.name_, Variant::GetTypeName(curve.valueType_), writeTarget->GetTypeName());
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
            writeTarget->OnGetAttribute(*attributeInfo, original);
            runtime.savedValues_.emplace(curve.name_, original);
        }

        writeTarget->OnSetAttribute(*attributeInfo, value);
        writeTarget->ApplyAttributes();
        runtime.lastValues_[curve.name_] = value;
        runtime.driving_ = true;
    }
}

}

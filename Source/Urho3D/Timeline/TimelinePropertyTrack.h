// Copyright (c) 2022-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#pragma once

#include <Urho3D/Core/Variant.h>
#include <Urho3D/Timeline/TimelineClip.h>
#include <Urho3D/Timeline/TimelineTrack.h>

#include <EASTL/unordered_map.h>

namespace Urho3D
{

class Animatable;

/// Keyframe of an inline property curve.
struct TimelinePropertyKey
{
    /// Serialize key fields. Value is serialized as the type of the owning curve.
    void SerializeInBlock(Archive& archive, VariantType variantType);

    /// Time within the source content of the clip.
    float time_{};
    /// Value of the attribute at the keyframe.
    Variant value_;
    /// Easing of the segment from this keyframe to the next one.
    TimelineEase ease_{TimelineEase::Linear};
};

/// Inline curve animating a single attribute of the track's bound object.
/// Property curves are first-class content of the timeline and are not shared with other systems.
struct TimelinePropertyCurve
{
    /// Serialize curve fields.
    void SerializeInBlock(Archive& archive);

    /// Name of the attribute on the bound object.
    ea::string name_;
    /// Type of animated values. Must match the type of the attribute.
    VariantType valueType_{VAR_NONE};
    /// Keyframes sorted by time.
    ea::vector<TimelinePropertyKey> keys_;
};

/// Clip of the property track. Animates attributes of the bound object while the clip is active.
/// When no clip is active, previously animated attributes keep their last values.
struct TimelinePropertyClip : public TimelineClip
{
    /// Serialize clip fields.
    void SerializeInBlock(Archive& archive);

    /// Curves of the clip. Attribute names should be unique within the clip.
    ea::vector<TimelinePropertyCurve> curves_;
};

/// Property track. Animates arbitrary reflected attributes of the bound object.
/// Bound object can be any Animatable in the scene: a Node or a Component,
/// referenced by the binding path of the track, e.g. "Weapon/StaticModel".
/// Only one track should drive the same attribute of the same object at a time.
class URHO3D_API TimelinePropertyTrack : public TimelineTrack
{
    URHO3D_OBJECT(TimelinePropertyTrack, TimelineTrack);

public:
    explicit TimelinePropertyTrack(Context* context);
    ~TimelinePropertyTrack() override;

    static void RegisterObject(Context* context);

    void SerializeInBlock(Archive& archive) override;
    float GetContentDuration() const override;
    ea::string GetTrackTypeName() const override { return "Property"; }

    ea::unique_ptr<TimelineTrackRuntime> CreateRuntime() const override;
    void OnTimelineStopped(TimelinePlayer& player, TimelineTrackRuntime& runtime, bool finished) override;
    void Evaluate(TimelineTrackContext& context) override;

    /// Set state of the animated attributes applied when the timeline stops.
    void SetPostPlaybackState(TimelinePostPlaybackState state) { postPlaybackState_ = state; }
    TimelinePostPlaybackState GetPostPlaybackState() const { return postPlaybackState_; }

    /// Property clips.
    ea::vector<TimelinePropertyClip> clips_;

    /// Runtime state of the property track.
    struct Runtime : public TimelineTrackRuntime
    {
        /// Resolved attributes of the bound object, cached by attribute name.
        /// Null value means the attribute is invalid on the bound object.
        ea::unordered_map<ea::string, const AttributeInfo*> attributeCache_;
        /// Original values of animated attributes, saved before the track first writes them.
        ea::unordered_map<ea::string, Variant> savedValues_;
        /// Last values written by the track, used to skip redundant attribute writes.
        ea::unordered_map<ea::string, Variant> lastValues_;
        /// Whether the track currently drives attributes of the bound object.
        bool driving_{};
    };

private:
    /// State of the animated attributes applied when the timeline stops.
    TimelinePostPlaybackState postPlaybackState_{TimelinePostPlaybackState::Revert};
};

}

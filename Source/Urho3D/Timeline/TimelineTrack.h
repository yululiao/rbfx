// Copyright (c) 2022-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#pragma once

#include <Urho3D/Core/Object.h>
#include <Urho3D/Timeline/TimelineDefs.h>

#include <EASTL/unique_ptr.h>

namespace Urho3D
{

class Archive;
class TimelinePlayer;

/// Per-player runtime state of a timeline track. Tracks themselves are immutable during playback.
struct TimelineTrackRuntime
{
    virtual ~TimelineTrackRuntime() = default;

    /// Object bound to the track: resolved from runtime binding override or binding path.
    WeakPtr<Object> boundObject_;
    /// Whether the binding should be resolved again.
    bool bindingDirty_{true};
};

/// Timeline track evaluation context passed by the player.
struct TimelineTrackContext
{
    /// Player that evaluates the track.
    TimelinePlayer& player_;
    /// Per-player runtime state of the track.
    TimelineTrackRuntime& runtime_;
    /// Current timeline time.
    float time_{};
    /// Timeline time of the previous evaluation. On first evaluation after playback start it is slightly less than time_.
    float prevTime_{};
    /// Signed time advance of this step.
    float deltaTime_{};
    /// Accumulated track weight multiplier, including weights of parent group tracks.
    float weightScale_{1.0f};
    /// Whether the timeline is currently playing.
    bool playing_{};
    /// Whether this is the first evaluation after playback start.
    bool firstEvaluation_{};
};

/// Base class of timeline tracks. Track is a piece of timeline content which drives bound objects.
/// Tracks are stateless: all runtime state is stored in TimelineTrackRuntime owned by the player.
class URHO3D_API TimelineTrack : public Object
{
    URHO3D_OBJECT(TimelineTrack, Object);

public:
    explicit TimelineTrack(Context* context);
    ~TimelineTrack() override;

    static void RegisterObject(Context* context);

    void SerializeInBlock(Archive& archive) override;

    /// @name Properties
    /// @{
    void SetName(const ea::string& name) { name_ = name; }
    const ea::string& GetName() const { return name_; }
    void SetMuted(bool muted) { muted_ = muted; }
    bool IsMuted() const { return muted_; }
    void SetWeight(float weight) { weight_ = weight; }
    float GetWeight() const { return weight_; }
    /// Set path of the node to bind the track to. Empty path binds to the player node.
    void SetBindingPath(const ea::string& bindingPath) { bindingPath_ = bindingPath; }
    const ea::string& GetBindingPath() const { return bindingPath_; }
    /// @}

    /// Return end time of the last clip of the track.
    virtual float GetContentDuration() const { return 0.0f; }
    /// Return human-readable name of the track type.
    virtual ea::string GetTrackTypeName() const { return "Track"; }

    /// Create per-player runtime state of the track.
    virtual ea::unique_ptr<TimelineTrackRuntime> CreateRuntime() const;
    /// Called when playback starts. Runtime state is reset before this call.
    virtual void OnTimelineStarted(TimelinePlayer& player, TimelineTrackRuntime& runtime) {}
    /// Called when playback stops. If finished is true, the timeline reached its end.
    virtual void OnTimelineStopped(TimelinePlayer& player, TimelineTrackRuntime& runtime, bool finished) {}
    /// Called when playback is paused or resumed, and when the player component is disabled or enabled.
    virtual void OnTimelinePaused(TimelinePlayer& player, TimelineTrackRuntime& runtime, bool paused) {}
    /// Evaluate the track. Called every frame while playing and on manual time changes.
    virtual void Evaluate(TimelineTrackContext& context) {}

protected:
    /// Return whether the track should be considered inactive: muted or has zero effective weight.
    bool IsInactive(const TimelineTrackContext& context) const
    {
        return muted_ || weight_ <= M_EPSILON || context.weightScale_ <= M_EPSILON;
    }

    /// Node binding path.
    ea::string bindingPath_;
    /// Track name.
    ea::string name_;
    /// Track weight.
    float weight_{1.0f};
    /// Whether the track is muted.
    bool muted_{};
};

/// Group track. Contains other tracks and scales their evaluation with its own weight.
class URHO3D_API TimelineGroupTrack : public TimelineTrack
{
    URHO3D_OBJECT(TimelineGroupTrack, TimelineTrack);

public:
    explicit TimelineGroupTrack(Context* context);
    ~TimelineGroupTrack() override;

    static void RegisterObject(Context* context);

    void SerializeInBlock(Archive& archive) override;
    float GetContentDuration() const override;
    ea::string GetTrackTypeName() const override { return "Group"; }

    ea::unique_ptr<TimelineTrackRuntime> CreateRuntime() const override;
    void OnTimelineStarted(TimelinePlayer& player, TimelineTrackRuntime& runtime) override;
    void OnTimelineStopped(TimelinePlayer& player, TimelineTrackRuntime& runtime, bool finished) override;
    void OnTimelinePaused(TimelinePlayer& player, TimelineTrackRuntime& runtime, bool paused) override;
    void Evaluate(TimelineTrackContext& context) override;

    /// @name Child tracks
    /// @{
    void AddTrack(TimelineTrack* track, unsigned index = M_MAX_UNSIGNED);
    bool RemoveTrack(TimelineTrack* track);
    void RemoveAllTracks() { tracks_.clear(); }
    unsigned GetNumTracks() const { return tracks_.size(); }
    TimelineTrack* GetTrack(unsigned index) const;
    const ea::vector<SharedPtr<TimelineTrack>>& GetTracks() const { return tracks_; }
    /// @}

    /// Runtime state of the group track.
    struct Runtime : public TimelineTrackRuntime
    {
        /// Per-child runtime states, parallel to child tracks.
        ea::vector<ea::unique_ptr<TimelineTrackRuntime>> children_;
    };

private:
    /// Child tracks.
    ea::vector<SharedPtr<TimelineTrack>> tracks_;
};

}

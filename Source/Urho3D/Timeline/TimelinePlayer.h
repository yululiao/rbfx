// Copyright (c) 2022-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#pragma once

#include <Urho3D/Core/Variant.h>
#include <Urho3D/Scene/LogicComponent.h>
#include <Urho3D/Timeline/TimelineDefs.h>
#include <Urho3D/Timeline/TimelineTrack.h>

#include <EASTL/unique_ptr.h>
#include <EASTL/unordered_map.h>

namespace Urho3D
{

class AnimationController;
class TimelineResource;

/// Return node of the bound object: the object itself if it is a node, or the node owning the component.
URHO3D_API Node* GetBoundNode(Object* boundObject);

/// Signal fired during timeline playback. Recorded for retroactive delivery to receivers.
struct TimelineFiredSignal
{
    /// Signal name.
    ea::string signal_;
    /// Signal payload.
    VariantMap data_;
    /// Signal time within the timeline.
    float time_{};
};

/// Timeline player component. Drives timeline tracks: evaluates them every frame, manages the playback clock
/// and resolves track bindings.
/// The player updates at E_SCENEUPDATE, before AnimationController consumes its input at E_SCENEPOSTUPDATE.
class URHO3D_API TimelinePlayer : public LogicComponent
{
    URHO3D_OBJECT(TimelinePlayer, LogicComponent);

public:
    explicit TimelinePlayer(Context* context);
    ~TimelinePlayer() override;

    static void RegisterObject(Context* context);

    /// @name Timeline
    /// @{
    /// Set timeline resource. Stops playback if another timeline was playing.
    void SetTimeline(TimelineResource* timeline);
    TimelineResource* GetTimeline() const { return timeline_; }
    /// Return duration of the timeline.
    float GetDuration() const;
    /// @}

    /// @name Playback
    /// @{
    /// Start playback. When resuming from pause, continues from the current time.
    /// When starting from stopped state, playback starts at Initial Time.
    void Play();
    /// Stop playback and reset time to 0. Applies post playback actions of the tracks.
    void Stop();
    /// Pause or resume playback.
    void SetPaused(bool paused);
    /// Return whether playback is paused.
    bool IsPaused() const { return paused_; }
    /// Return whether the timeline is playing.
    bool IsPlaying() const { return playing_; }
    /// Re-evaluate tracks at current time.
    void Evaluate();
    /// Set playback time. The timeline is evaluated if it is playing (including paused).
    void SetTime(float time);
    /// Return playback time.
    float GetTime() const { return time_; }
    /// Set playback speed multiplier.
    void SetPlaybackSpeed(float speed) { playbackSpeed_ = speed; }
    /// Return playback speed multiplier.
    float GetPlaybackSpeed() const { return playbackSpeed_; }
    /// @}

    /// @name Track binding
    /// @{
    /// Override binding of the track with an explicit object. Pass null to restore path based binding.
    void SetTrackBinding(TimelineTrack* track, Object* object);
    /// Return binding override of the track, or null.
    Object* GetTrackBinding(const TimelineTrack* track) const;
    /// Resolve and cache binding of the track. The track is bound to the override object,
    /// to the object referenced by the binding path, or to the player node.
    Object* ResolveTrackBinding(const TimelineTrack& track, TimelineTrackRuntime& runtime);
    /// @}

    /// @name Runtime API for tracks
    /// @{
    /// Return time step of the scene. Animation tracks use it to pre-compensate controller time advance.
    float GetSceneTimeStep() const { return sceneTimeStep_; }
    /// Remove animation instance from the controller and fix cached indices of all animation tracks.
    void RemoveAnimationInstance(AnimationController& controller, unsigned index);
    /// Fire a signal. Retroactive signals are recorded for delivery to receivers enabled later.
    void EmitSignal(const ea::string& signal, const VariantMap& data, float time, bool retroactive);
    /// Fire a marker.
    void EmitMarker(const ea::string& marker, float time);
    /// Return signals fired during current playback which should be retroactively delivered.
    const ea::vector<TimelineFiredSignal>& GetRetroactiveSignals() const { return retroactiveSignals_; }
    /// @}

protected:
    /// Handle enabled/disabled state change.
    void OnSetEnabled() override;
    /// Invalidate track bindings when the node is assigned.
    void Start() override;
    /// Start playback if Play On Awake is enabled.
    void DelayedStart() override;
    /// Advance the playback clock and evaluate tracks.
    void Update(float timeStep) override;
    /// Load the timeline resource when the resource reference changes.
    void ApplyAttributes() override;

private:
    /// Per-track runtime state.
    struct TrackRuntime
    {
        /// Track that owns the runtime state.
        WeakPtr<TimelineTrack> track_;
        /// Runtime state of the track.
        ea::unique_ptr<TimelineTrackRuntime> runtime_;
    };

    /// Rebuild runtime states if the timeline content changed.
    void UpdateRuntimeState();
    /// Recreate runtime states of all tracks.
    void RebuildRuntimeState();
    /// Evaluate all tracks with given previous time.
    void EvaluateTracks(float prevTime, bool firstEvaluation);
    /// Start playback from the initial time.
    void StartPlayback();
    /// Stop playback and notify tracks.
    void StopPlayback(bool finished);
    /// Notify tracks about pause state change.
    void NotifyTracksPaused(bool paused);
    /// Resolve binding path relative to the player node.
    Object* ResolveBindingPath(const ea::string& path) const;
    /// Send an event with the common timeline parameters. Sender is the player node.
    void SendTimelineEvent(StringHash eventType, VariantMap parameters);

    /// Timeline resource.
    SharedPtr<TimelineResource> timeline_;
    /// Resource reference of the timeline used for serialization.
    ResourceRef timelineRef_;
    /// Per-track runtime states, parallel to the timeline tracks.
    ea::vector<TrackRuntime> trackRuntimes_;
    /// Tracks revision the runtime states were built for.
    unsigned tracksRevision_{M_MAX_UNSIGNED};

    /// Explicit track bindings. Not serialized.
    ea::unordered_map<const TimelineTrack*, WeakPtr<Object>> trackBindings_;

    /// Signals fired during current playback which should be retroactively delivered.
    ea::vector<TimelineFiredSignal> retroactiveSignals_;

    /// @name Serialized properties
    /// @{
    /// Playback starts automatically when the component is created.
    bool playOnAwake_{true};
    /// Behavior when playback reaches the end of the timeline.
    TimelineWrapMode wrapMode_{TimelineWrapMode::None};
    /// How the playback clock is advanced.
    TimelineTimeUpdateMode timeUpdateMode_{TimelineTimeUpdateMode::GameTime};
    /// Playback speed multiplier.
    float playbackSpeed_{1.0f};
    /// Playback time used when playback starts.
    float initialTime_{};
    /// @}

    /// Current playback time.
    float time_{};
    /// Whether playback is running.
    bool playing_{};
    /// Whether playback is paused.
    bool paused_{};
    /// Time step received from the scene update. Used by animation tracks for pre-compensation.
    float sceneTimeStep_{};
};

}

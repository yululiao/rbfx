// Copyright (c) 2022-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#pragma once

#include <Urho3D/Core/Variant.h>
#include <Urho3D/Timeline/TimelineClip.h>
#include <Urho3D/Timeline/TimelineTrack.h>

namespace Urho3D
{

/// Clip of the signal track. Fires a named signal with an optional payload when playback crosses the clip start time.
struct TimelineSignalClip : public TimelineClip
{
    /// Serialize clip fields.
    void SerializeInBlock(Archive& archive);

    /// Signal name.
    ea::string signal_;
    /// Optional signal payload.
    VariantMap data_;
    /// Whether the signal is emitted only once per playback, even if crossed multiple times.
    bool emitOnce_{};
    /// Whether the signal is retroactively emitted when playback starts after the signal time.
    /// Retroactive signals are also delivered to receivers enabled in the middle of playback.
    bool retroactive_{};
};

/// Signal track. Fires signals at given times. Signals are received by SignalReceiver components
/// and are delivered as E_TIMELINE_SIGNAL events.
class URHO3D_API TimelineSignalTrack : public TimelineTrack
{
    URHO3D_OBJECT(TimelineSignalTrack, TimelineTrack);

public:
    explicit TimelineSignalTrack(Context* context);
    ~TimelineSignalTrack() override;

    static void RegisterObject(Context* context);

    void SerializeInBlock(Archive& archive) override;
    float GetContentDuration() const override;
    ea::string GetTrackTypeName() const override { return "Signal"; }

    ea::unique_ptr<TimelineTrackRuntime> CreateRuntime() const override;
    void Evaluate(TimelineTrackContext& context) override;

    /// Signal clips.
    ea::vector<TimelineSignalClip> clips_;

    /// Runtime state of the signal track.
    struct Runtime : public TimelineTrackRuntime
    {
        /// Per-clip state.
        struct ClipState
        {
            /// Whether the signal was already emitted during the current playback.
            bool emitted_{};
        };

        /// Per-clip states.
        ea::vector<ClipState> clips_;
    };
};

}

// Copyright (c) 2022-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#pragma once

#include <Urho3D/Timeline/TimelineTrack.h>

namespace Urho3D
{

/// Marker of the marker track. Marker is a named time point which fires an event when crossed by playback.
struct TimelineMarker
{
    TimelineMarker() = default;
    TimelineMarker(const ea::string& name, float time) :
        name_(name),
        time_(time)
    {
    }

    /// Serialize marker fields.
    void SerializeInBlock(Archive& archive);

    /// Marker name.
    ea::string name_;
    /// Marker time within the timeline.
    float time_{};
};

/// Marker track. Fires E_TIMELINE_MARKER events when playback crosses marker times in either direction.
class URHO3D_API TimelineMarkerTrack : public TimelineTrack
{
    URHO3D_OBJECT(TimelineMarkerTrack, TimelineTrack);

public:
    explicit TimelineMarkerTrack(Context* context);
    ~TimelineMarkerTrack() override;

    static void RegisterObject(Context* context);

    void SerializeInBlock(Archive& archive) override;
    float GetContentDuration() const override;
    ea::string GetTrackTypeName() const override { return "Marker"; }

    void Evaluate(TimelineTrackContext& context) override;

    /// @name Markers
    /// @{
    void AddMarker(const TimelineMarker& marker);
    void InsertMarker(unsigned index, const TimelineMarker& marker);
    bool RemoveMarker(const TimelineMarker& marker);
    void RemoveMarker(unsigned index);
    void RemoveAllMarkers() { markers_.clear(); }
    unsigned GetNumMarkers() const { return markers_.size(); }
    const TimelineMarker& GetMarker(unsigned index) const { return markers_[index]; }
    ea::vector<TimelineMarker>& GetMarkers() { return markers_; }
    const ea::vector<TimelineMarker>& GetMarkers() const { return markers_; }
    /// @}

    /// Markers.
    ea::vector<TimelineMarker> markers_;
};

}

// Copyright (c) 2022-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#pragma once

#include <Urho3D/Resource/Resource.h>
#include <Urho3D/Timeline/TimelineDefs.h>
#include <Urho3D/Timeline/TimelineTrack.h>

namespace Urho3D
{

/// Timeline resource. Contains a list of tracks which are played by TimelinePlayer.
class URHO3D_API TimelineResource : public SimpleResource
{
    URHO3D_OBJECT(TimelineResource, SimpleResource);

public:
    explicit TimelineResource(Context* context);
    ~TimelineResource() override;

    static void RegisterObject(Context* context);

    void SerializeInBlock(Archive& archive) override;

    /// @name Properties
    /// @{
    void SetDurationMode(TimelineDurationMode mode) { durationMode_ = mode; }
    TimelineDurationMode GetDurationMode() const { return durationMode_; }
    void SetFixedDuration(float duration) { fixedDuration_ = Max(duration, 0.0f); }
    float GetFixedDuration() const { return fixedDuration_; }
    /// Return effective duration of the timeline.
    float GetDuration() const;
    /// Return duration of the timeline content, i.e. the end time of the last clip of any track.
    float GetContentDuration() const;
    /// @}

    /// @name Tracks
    /// @{
    void AddTrack(TimelineTrack* track, unsigned index = M_MAX_UNSIGNED);
    bool RemoveTrack(TimelineTrack* track);
    void RemoveAllTracks();
    unsigned GetNumTracks() const { return tracks_.size(); }
    TimelineTrack* GetTrack(unsigned index) const;
    unsigned GetTrackIndex(TimelineTrack* track) const;
    ea::vector<SharedPtr<TimelineTrack>>& GetTracks() { return tracks_; }
    const ea::vector<SharedPtr<TimelineTrack>>& GetTracks() const { return tracks_; }
    /// @}

    /// Return revision of the track list. Changed when tracks are added, removed or reordered.
    unsigned GetTracksRevision() const { return tracksRevision_; }

private:
    /// Return binary magic of the resource.
    BinaryMagic GetBinaryMagic() const override;
    /// Return root block name of the resource.
    const char* GetRootBlockName() const override { return "Timeline"; }

    /// Track list revision.
    unsigned tracksRevision_{};
    /// Timeline duration mode.
    TimelineDurationMode durationMode_{TimelineDurationMode::Content};
    /// Timeline duration for Fixed duration mode.
    float fixedDuration_{5.0f};
    /// Tracks.
    ea::vector<SharedPtr<TimelineTrack>> tracks_;
};

}

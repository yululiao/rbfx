// Copyright (c) 2022-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#include <Urho3D/Precompiled.h>

#include <Urho3D/Timeline/Timeline.h>

#include <Urho3D/Timeline/SignalReceiver.h>
#include <Urho3D/Timeline/TimelineActivationTrack.h>
#include <Urho3D/Timeline/TimelineAnimationTrack.h>
#include <Urho3D/Timeline/TimelineAudioTrack.h>
#include <Urho3D/Timeline/TimelineControlTrack.h>
#include <Urho3D/Timeline/TimelineMarkerTrack.h>
#include <Urho3D/Timeline/TimelinePlayer.h>
#include <Urho3D/Timeline/TimelineResource.h>
#include <Urho3D/Timeline/TimelineSignalTrack.h>
#include <Urho3D/Timeline/TimelineTrack.h>

namespace Urho3D
{

void RegisterTimelineLibrary(Context* context)
{
    TimelineTrack::RegisterObject(context);
    TimelineGroupTrack::RegisterObject(context);

    TimelineAnimationTrack::RegisterObject(context);
    TimelineActivationTrack::RegisterObject(context);
    TimelineControlTrack::RegisterObject(context);
    TimelineAudioTrack::RegisterObject(context);
    TimelineSignalTrack::RegisterObject(context);
    TimelineMarkerTrack::RegisterObject(context);

    TimelineResource::RegisterObject(context);

    TimelinePlayer::RegisterObject(context);
    SignalReceiver::RegisterObject(context);
}

} // namespace Urho3D

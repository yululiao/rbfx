// Copyright (c) 2022-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#pragma once

#include <Urho3D/Core/Object.h>

namespace Urho3D
{

/// Sent when timeline playback starts. Sender is the timeline player node.
URHO3D_EVENT(E_TIMELINE_PLAYED, TimelinePlayed)
{
    URHO3D_PARAM(P_NODE, Node);                     // Node pointer of the timeline player
    URHO3D_PARAM(P_TIMELINE, Timeline);             // TimelineResource pointer
}

/// Sent when timeline playback stops. Sender is the timeline player node.
URHO3D_EVENT(E_TIMELINE_STOPPED, TimelineStopped)
{
    URHO3D_PARAM(P_NODE, Node);                     // Node pointer of the timeline player
    URHO3D_PARAM(P_TIMELINE, Timeline);             // TimelineResource pointer
    URHO3D_PARAM(P_FINISHED, Finished);             // bool, true when stopped by reaching the timeline end
}

/// Sent when timeline playback is paused or resumed. Sender is the timeline player node.
URHO3D_EVENT(E_TIMELINE_PAUSED, TimelinePaused)
{
    URHO3D_PARAM(P_NODE, Node);                     // Node pointer of the timeline player
    URHO3D_PARAM(P_TIMELINE, Timeline);             // TimelineResource pointer
    URHO3D_PARAM(P_PAUSE, Pause);                   // bool
}

/// Sent when looping timeline playback wraps around. Sender is the timeline player node.
URHO3D_EVENT(E_TIMELINE_WRAPPED, TimelineWrapped)
{
    URHO3D_PARAM(P_NODE, Node);                     // Node pointer of the timeline player
    URHO3D_PARAM(P_TIMELINE, Timeline);             // TimelineResource pointer
    URHO3D_PARAM(P_TIME, Time);                     // float time after the wrap
}

/// Sent when a signal is fired by a Signal Track. Sender is the timeline player node.
URHO3D_EVENT(E_TIMELINE_SIGNAL, TimelineSignal)
{
    URHO3D_PARAM(P_NODE, Node);                     // Node pointer of the timeline player
    URHO3D_PARAM(P_TIMELINE, Timeline);             // TimelineResource pointer
    URHO3D_PARAM(P_SIGNAL, Signal);                 // ea::string signal name
    URHO3D_PARAM(P_TIME, Time);                     // float
    URHO3D_PARAM(P_DATA, Data);                     // VariantMap
}

/// Sent when a marker is passed on the Marker Track. Sender is the timeline player node.
URHO3D_EVENT(E_TIMELINE_MARKER, TimelineMarkerEvent)
{
    URHO3D_PARAM(P_NODE, Node);                     // Node pointer of the timeline player
    URHO3D_PARAM(P_TIMELINE, Timeline);             // TimelineResource pointer
    URHO3D_PARAM(P_MARKER, Marker);                 // ea::string marker name
    URHO3D_PARAM(P_TIME, Time);                     // float
}

/// Sent by SignalReceiver when a subscribed signal is delivered to it. Sender is the receiver node.
URHO3D_EVENT(E_TIMELINE_SIGNALRECEIVED, TimelineSignalReceived)
{
    URHO3D_PARAM(P_NODE, Node);                     // Node pointer of the SignalReceiver
    URHO3D_PARAM(P_SIGNAL, Signal);                 // ea::string signal name
    URHO3D_PARAM(P_DATA, Data);                     // VariantMap
}

}

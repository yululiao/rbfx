// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#pragma once

#include "Urho3D/Core/Object.h"

namespace Urho3D
{

/// The host resumed the game (vendor onShow). The frame loop resumes; the audio session is
/// resumed by the JavaScript runtime at the same time.
URHO3D_EVENT(E_MINIGAMESHOW, MinigameShow)
{
}

/// The host sent the game to the background (vendor onHide). The vendor runtime stops
/// driving the frame loop; the audio session is suspended by the JavaScript runtime.
URHO3D_EVENT(E_MINIGAMEHIDE, MinigameHide)
{
}

/// The host reported memory pressure. P_LEVEL mirrors the vendor scale.
URHO3D_EVENT(E_MINIGAMEMEMORYWARNING, MinigameMemoryWarning)
{
    URHO3D_PARAM(P_LEVEL, Level); // int
}

/// The rendering context of the game canvas was lost.
URHO3D_EVENT(E_MINIGAMECONTEXTLOST, MinigameContextLost)
{
}

/// A background prefetch requested through MinigamePlatform::PrefetchFiles finished.
URHO3D_EVENT(E_MINIGAMEPREFETCHED, MinigamePrefetched)
{
    URHO3D_PARAM(P_READY, Ready);   // int, files that are locally available now
    URHO3D_PARAM(P_FAILED, Failed); // int, files that could not be made available
}

/// Result of a MinigameSDK request started earlier (login, share, rewarded ad, payment,
/// privacy authorization). P_REQUESTID matches the value the request call returned.
URHO3D_EVENT(E_MINIGAMESDKRESULT, MinigameSdkResult)
{
    URHO3D_PARAM(P_REQUESTID, RequestId); // int
    URHO3D_PARAM(P_KIND, Kind);           // int, MinigameSDKRequestKind
    URHO3D_PARAM(P_SUCCESS, Success);     // bool
    URHO3D_PARAM(P_PAYLOAD, Payload);     // String, JSON or vendor text, may be empty
}

} // namespace Urho3D

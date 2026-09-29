// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#pragma once

#include "Urho3D/Container/Str.h"
#include "Urho3D/Core/Object.h"

#include <EASTL/unordered_map.h>

namespace Urho3D
{

/// The host capabilities the game can ask for. Values are part of the JavaScript bridge
/// contract: minigame_sdk.js maps them onto the vendor APIs.
enum class MinigameSDKRequestKind
{
    /// Ask the host to sign the player in.
    Login = 0,
    /// Share to the host's social feed.
    Share = 1,
    /// Show a rewarded video ad.
    RewardedAd = 2,
    /// Start a payment flow for an order.
    Payment = 3,
    /// Ask the player to accept the privacy policy.
    PrivacyAuthorization = 4,
};

/// Bridge to the host SDK capabilities (login, share, rewarded ads, payment, privacy).
///
/// Every request is asynchronous and non-blocking: the vendor APIs answer through
/// callbacks and the game keeps running in the meantime, so a request returns its id
/// immediately and the answer arrives later as a MinigameSdkResult event carrying the
/// same id, the request kind, a success flag and a payload string (vendor JSON or a
/// failure reason).
///
/// Answers are delivered on the frame boundary. The JavaScript shim may invoke the
/// completion hook from any turn, including synchronously inside the request call;
/// completions are therefore queued and dispatched from E_BEGINFRAME, which keeps event
/// handlers safe to start new requests.
///
/// Outside a host (desktop, editor, tests) requests complete immediately with a failure
/// payload, so game code can exercise its result handling without a vendor runtime.
class URHO3D_API MinigameSDK : public Object
{
    URHO3D_OBJECT(MinigameSDK, Object)

public:
    /// Construct.
    explicit MinigameSDK(Context* context);
    /// Destruct.
    ~MinigameSDK() override;

    /// Install the native completion hook the JavaScript SDK shim calls back into and
    /// detect whether the shim is attached. Call once after registering the subsystem.
    bool InstallNativeHooks();

    /// The JavaScript SDK shim reporting a finished request (it echoes the id the request
    /// call returned). Normally invoked through the installed hook; public so a custom
    /// host can drive it directly. Completions for unknown ids are dropped with a warning.
    void HandleNativeCompletion(int requestId, bool success, const ea::string& payload);

    /// Ask the host to sign the player in. The payload of the answer carries the vendor's
    /// login result.
    int Login();
    /// Share to the host's social feed. The answer payload carries the vendor result.
    int Share(const ea::string& title, const ea::string& imageUrl, const ea::string& query = EMPTY_STRING);
    /// Show a rewarded video ad for the given ad unit. Reward granting is driven by the
    /// answer payload, never assumed from the request.
    int ShowRewardedAd(const ea::string& adUnitId);
    /// Start the payment flow. orderJson is forwarded to the vendor verbatim.
    int Pay(const ea::string& orderJson);
    /// Ask the player to accept the privacy policy; required by the vendors before any
    /// personal data is touched.
    int RequestPrivacyAuthorization();

    /// Return whether the JavaScript SDK shim is attached.
    /// @property
    bool IsAvailable() const { return sdkAvailable_; }

private:
    /// One finished request, queued for the frame boundary.
    struct CompletedRequest
    {
        int requestId_{};
        MinigameSDKRequestKind kind_{};
        bool success_{};
        ea::string payload_;
    };

    /// Assign an id, hand the request to the JavaScript shim and return the id.
    int Invoke(MinigameSDKRequestKind kind, const ea::string& payloadJson);
    /// Queue a completion produced without the shim (host unavailable).
    void CompleteRequest(int requestId, MinigameSDKRequestKind kind, bool success, const ea::string& payload);
    /// Dispatch queued completions as MinigameSdkResult events.
    void DeliverCompletedRequests();

    /// Next request id. Ids are per-session and never reused.
    int nextRequestId_{1};
    /// Kinds of the requests handed to the shim, keyed by request id. The shim echoes
    /// only the id, the kind is resolved here.
    ea::unordered_map<int, MinigameSDKRequestKind> pending_;
    /// Whether the JavaScript SDK shim accepted the completion hook.
    bool sdkAvailable_{};
    /// Completed requests waiting for the frame boundary.
    ea::vector<CompletedRequest> completed_;
};

} // namespace Urho3D

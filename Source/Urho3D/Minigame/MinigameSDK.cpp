// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#include "Urho3D/Minigame/MinigameSDK.h"

#include "Urho3D/Core/CoreEvents.h"
#include "Urho3D/IO/Log.h"
#include "Urho3D/Minigame/MinigameEvents.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

#if defined(__EMSCRIPTEN__)

// JavaScript library functions used by the EM_JS blocks below.
EM_JS_DEPS(rbfx_minigame_sdk_deps, "$UTF8ToString,$stringToUTF8,$lengthBytesUTF8");

// Installed into globalThis.rbfx.native: the JavaScript SDK shim
// (minigame_sdk.js) calls this when a vendor request finishes. The payload string is
// copied onto the wasm heap for the duration of the call; the C side makes its own copy.
// The "_" prefix on the callee is the emscripten binding name of the C symbol below,
// not part of the C name.
EM_JS(int, rbfx_install_sdk_hooks, (), {
    const g = globalThis;
    if (!g.rbfx) {
        if (typeof console !== "undefined")
            console.warn("[rbfx] no JavaScript bootstrap found, SDK hooks are not installed");
        return 0;
    }
    const natives = g.rbfx.native = g.rbfx.native || {};
    natives.sdk_complete = function (requestId, success, payload) {
        const str = (payload === null || payload === undefined) ? "" : String(payload);
        const lengthBytes = lengthBytesUTF8(str) + 1;
        const stringOnWasmHeap = _malloc(lengthBytes);
        stringToUTF8(str, stringOnWasmHeap, lengthBytes);
        _rbfx_minigame_on_sdk_complete(requestId | 0, success ? 1 : 0, stringOnWasmHeap);
        _free(stringOnWasmHeap);
    };
    return 1;
});

// Forward a request to the JavaScript SDK shim. Returns 0 when the shim is missing; the
// caller then reports the failure itself on the regular result path.
EM_JS(int, rbfx_minigame_sdk_invoke, (int kind, int requestId, const char* payload), {
    const sdk = globalThis.rbfx && globalThis.rbfx.sdk;
    if (!sdk || typeof sdk.invoke !== "function")
        return 0;
    try {
        sdk.invoke(kind | 0, requestId | 0, UTF8ToString(payload));
    } catch (error) {
        console.error("[rbfx] sdk invoke failed", error);
        return 0;
    }
    return 1;
});

#endif // defined(__EMSCRIPTEN__)

namespace Urho3D
{

namespace
{

/// The subsystem instance the native completion hook forwards into.
MinigameSDK* sdk = nullptr;

/// Minimal JSON string escaping for the request payloads built below.
ea::string EscapeJson(const ea::string& value)
{
    static const char* hexDigits = "0123456789abcdef";

    ea::string result;
    result.reserve(value.length() + 2);
    result += '"';
    for (char ch : value)
    {
        switch (ch)
        {
        case '"':
            result += "\\\"";
            break;
        case '\\':
            result += "\\\\";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
        {
            const unsigned char code = static_cast<unsigned char>(ch);
            if (code < 0x20)
            {
                result += "\\u00";
                result += hexDigits[(code >> 4) & 0xF];
                result += hexDigits[code & 0xF];
            }
            else
                result += ch;
            break;
        }
        }
    }
    result += '"';
    return result;
}

}

// The native completion hook. extern "C" keeps the name exact (the JavaScript side calls
// the "_"-prefixed binding name); the keepalive attribute keeps the function in the wasm
// export table even though only JavaScript references it.
extern "C"
{

#if defined(__EMSCRIPTEN__)
#define RBFX_MINIGAME_SDK_KEEPALIVE EMSCRIPTEN_KEEPALIVE
#else
#define RBFX_MINIGAME_SDK_KEEPALIVE
#endif

void RBFX_MINIGAME_SDK_KEEPALIVE rbfx_minigame_on_sdk_complete(int requestId, int success, const char* payload)
{
    if (!sdk)
        return;

    const ea::string payloadCopy = payload ? ea::string{payload} : EMPTY_STRING;
    sdk->HandleNativeCompletion(requestId, success != 0, payloadCopy);
}

#undef RBFX_MINIGAME_SDK_KEEPALIVE

} // extern "C"

MinigameSDK::MinigameSDK(Context* context)
    : Object(context)
{
    SubscribeToEvent(E_BEGINFRAME, [this](StringHash, VariantMap&) { DeliverCompletedRequests(); });
}

MinigameSDK::~MinigameSDK()
{
    if (sdk == this)
        sdk = nullptr;
}

bool MinigameSDK::InstallNativeHooks()
{
    sdk = this;

#if defined(__EMSCRIPTEN__)
    sdkAvailable_ = rbfx_install_sdk_hooks() != 0;
#else
    sdkAvailable_ = false;
#endif

    if (!sdkAvailable_)
        URHO3D_LOGINFO("MinigameSDK: no host SDK detected, requests complete with a failure result");

    return sdkAvailable_;
}

void MinigameSDK::HandleNativeCompletion(int requestId, bool success, const ea::string& payload)
{
    // The shim echoes only the id; resolve the kind from the pending table. This runs on
    // the engine thread - the JavaScript shim either answers in its own turn or, when the
    // vendor fires its callback synchronously, re-enters wasm from inside the invoke call
    // (which is safe: the pending entry is written before the shim is called).
    const auto it = pending_.find(requestId);
    if (it == pending_.end())
    {
        URHO3D_LOGWARNING("MinigameSDK: completion for unknown request {}", requestId);
        return;
    }

    const MinigameSDKRequestKind kind = it->second;
    pending_.erase(it);
    CompleteRequest(requestId, kind, success, payload);
}

int MinigameSDK::Login()
{
    return Invoke(MinigameSDKRequestKind::Login, EMPTY_STRING);
}

int MinigameSDK::Share(const ea::string& title, const ea::string& imageUrl, const ea::string& query)
{
    ea::string payload;
    payload += "{\"title\":";
    payload += EscapeJson(title);
    payload += ",\"imageUrl\":";
    payload += EscapeJson(imageUrl);
    payload += ",\"query\":";
    payload += EscapeJson(query);
    payload += '}';
    return Invoke(MinigameSDKRequestKind::Share, payload);
}

int MinigameSDK::ShowRewardedAd(const ea::string& adUnitId)
{
    ea::string payload;
    payload += "{\"adUnitId\":";
    payload += EscapeJson(adUnitId);
    payload += '}';
    return Invoke(MinigameSDKRequestKind::RewardedAd, payload);
}

int MinigameSDK::Pay(const ea::string& orderJson)
{
    // Forwarded verbatim: the order structure is defined by the vendor and the game is
    // the only side that knows it.
    return Invoke(MinigameSDKRequestKind::Payment, orderJson);
}

int MinigameSDK::RequestPrivacyAuthorization()
{
    return Invoke(MinigameSDKRequestKind::PrivacyAuthorization, EMPTY_STRING);
}

int MinigameSDK::Invoke(MinigameSDKRequestKind kind, const ea::string& payloadJson)
{
    const int requestId = nextRequestId_++;

#if defined(__EMSCRIPTEN__)
    // Record the kind before calling out: a vendor callback may complete the request
    // synchronously while the shim call is still on the stack.
    pending_[requestId] = kind;
    if (rbfx_minigame_sdk_invoke(static_cast<int>(kind), requestId, payloadJson.c_str()) != 0)
        return requestId;
    pending_.erase(requestId);
#endif

    // No shim attached (desktop, tests, or a module hosted without the JavaScript
    // bootstrap): report the failure through the regular result path so callers do not
    // need to care which world they run in.
    CompleteRequest(requestId, kind, false, "the host SDK is not available");
    DeliverCompletedRequests();
    return requestId;
}

void MinigameSDK::CompleteRequest(int requestId, MinigameSDKRequestKind kind, bool success,
    const ea::string& payload)
{
    CompletedRequest& completed = completed_.emplace_back();
    completed.requestId_ = requestId;
    completed.kind_ = kind;
    completed.success_ = success;
    completed.payload_ = payload;
}

void MinigameSDK::DeliverCompletedRequests()
{
    if (completed_.empty())
        return;

    // Swap the batch out first: an event handler may start another request, and those
    // completions must land in the queue for the next boundary.
    ea::vector<CompletedRequest> batch;
    batch.swap(completed_);

    for (const CompletedRequest& completed : batch)
    {
        using namespace MinigameSdkResult;

        VariantMap& eventData = GetEventDataMap();
        eventData[P_REQUESTID] = completed.requestId_;
        eventData[P_KIND] = static_cast<int>(completed.kind_);
        eventData[P_SUCCESS] = completed.success_;
        eventData[P_PAYLOAD] = completed.payload_;
        SendEvent(E_MINIGAMESDKRESULT, eventData);
    }
}

} // namespace Urho3D

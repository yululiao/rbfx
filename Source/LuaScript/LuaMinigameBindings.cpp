//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#include "../Urho3D/Precompiled.h"

#include "LuaBindings.h"
#include "LuaBindHelpers.h"
#include "LuaBindMacros.h"

#include "../Urho3D/Container/Str.h"
#include "../Urho3D/Minigame/MinigamePlatform.h"
#include "../Urho3D/Minigame/MinigameSDK.h"

#include <sol/sol.hpp>

namespace sol
{

template <> struct is_automagical<Urho3D::MinigameSDK> : std::false_type {};
template <> struct is_automagical<Urho3D::MinigamePlatform> : std::false_type {};

} // namespace sol

namespace Urho3D
{

void RegisterMinigameBindings(sol::state& lua, Context* context)
{
    // MinigameSDK: host SDK capabilities (login, share, rewarded ads, payment,
    // privacy authorization). Requests are asynchronous: each call returns a
    // request id immediately and the answer arrives later as the
    // "MinigameSdkResult" event (data.RequestId / data.Kind / data.Success /
    // data.Payload). Outside a host every request fails right away, so result
    // handling runs unconditionally.
    {
        using LUA_THIS = MinigameSDK;
        LUA_CLASS(MinigameSDK, sol::no_constructor
            LUA_BASES(Object)
            LUA_MEMBER_FUNC(Login)
            // The vendor query string is optional for a plain share.
            LUA_MEMBER_FUNC_RAW(Share, [](MinigameSDK* sdk, const ea::string& title,
                const ea::string& imageUrl, sol::optional<ea::string> query) -> int {
                return sdk ? sdk->Share(title, imageUrl, query.value_or(EMPTY_STRING)) : 0;
            })
            LUA_MEMBER_FUNC(ShowRewardedAd)
            LUA_MEMBER_FUNC(Pay)
            LUA_MEMBER_FUNC(RequestPrivacyAuthorization)
            LUA_MEMBER_FUNC_RET(IsAvailable, bool)
        );
    }
    RegisterLuaObjectWrapper<MinigameSDK>();

    // MinigamePlatform: the host lifecycle surface a game may drive directly.
    // The runtimes forbid terminating the process, so ExitGame is the only
    // supported quit; prefetch completion arrives as the "MinigamePrefetched"
    // event.
    {
        using LUA_THIS = MinigamePlatform;
        LUA_CLASS(MinigamePlatform, sol::no_constructor
            LUA_BASES(Object)
            LUA_MEMBER_FUNC_RET(GetUserDataPath, ea::string)
            // Exit code is optional (defaults to 0).
            LUA_MEMBER_FUNC_RAW(ExitGame, [](MinigamePlatform* platform, sol::optional<int> code) {
                if (platform)
                    platform->ExitGame(code.value_or(0));
            })
            // Package-relative file names as a Lua string array; the call is a
            // hint and returns immediately.
            LUA_MEMBER_FUNC_RAW(PrefetchFiles, [](MinigamePlatform* platform, sol::table files) {
                if (!platform)
                    return;
                StringVector names;
                for (const auto& entry : files)
                    names.push_back(entry.second.as<ea::string>());
                platform->PrefetchFiles(names);
            })
            LUA_MEMBER_FUNC_RET(IsHostAvailable, bool)
            LUA_MEMBER_FUNC_RET(IsForeground, bool)
        );
    }
    RegisterLuaObjectWrapper<MinigamePlatform>();

    // Request kinds for matching data.Kind in the "MinigameSdkResult" event.
    LUA_ENUM_TABLE(MINIGAME_REQUEST_KIND,
        "LOGIN", static_cast<int>(MinigameSDKRequestKind::Login),
        "SHARE", static_cast<int>(MinigameSDKRequestKind::Share),
        "REWARDED_AD", static_cast<int>(MinigameSDKRequestKind::RewardedAd),
        "PAYMENT", static_cast<int>(MinigameSDKRequestKind::Payment),
        "PRIVACY_AUTHORIZATION", static_cast<int>(MinigameSDKRequestKind::PrivacyAuthorization));
}

} // namespace Urho3D

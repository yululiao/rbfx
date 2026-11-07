//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#include "../Urho3D/Precompiled.h"

#include "LuaBindings.h"
#include "LuaBindHelpers.h"
#include "LuaBindMacros.h"

#include "../Urho3D/Core/Context.h"
#include "../Urho3D/IO/Log.h"

// URHO3D_TIMELINE is defined to 1 only when the Timeline subsystem is part of this engine build;
// everything below - the headers, the sol specializations and the usertype registrations - follows
// it, and RegisterTimelineBindings degrades to a stub so LuaVM can keep calling every entry point
// unconditionally (same shape as the RmlUI bindings).
#if URHO3D_TIMELINE
#include "../Urho3D/Timeline/SignalReceiver.h"
#include "../Urho3D/Timeline/TimelineActivationTrack.h"
#include "../Urho3D/Timeline/TimelineAnimationTrack.h"
#include "../Urho3D/Timeline/TimelineAudioTrack.h"
#include "../Urho3D/Timeline/TimelineControlTrack.h"
#include "../Urho3D/Timeline/TimelineMarkerTrack.h"
#include "../Urho3D/Timeline/TimelinePlayer.h"
#include "../Urho3D/Timeline/TimelinePropertyTrack.h"
#include "../Urho3D/Timeline/TimelineResource.h"
#include "../Urho3D/Timeline/TimelineSignalTrack.h"
#include "../Urho3D/Timeline/TimelineTrack.h"
#endif

#include <sol/sol.hpp>

namespace sol
{

#if URHO3D_TIMELINE
// RefCounted engine objects without value semantics: register behavior explicitly.
template <> struct is_automagical<Urho3D::TimelinePlayer> : std::false_type {};
template <> struct is_automagical<Urho3D::TimelineResource> : std::false_type {};
template <> struct is_automagical<Urho3D::SignalReceiver> : std::false_type {};
template <> struct is_automagical<Urho3D::TimelineTrack> : std::false_type {};
template <> struct is_automagical<Urho3D::TimelineGroupTrack> : std::false_type {};
template <> struct is_automagical<Urho3D::TimelineAnimationTrack> : std::false_type {};
template <> struct is_automagical<Urho3D::TimelineActivationTrack> : std::false_type {};
template <> struct is_automagical<Urho3D::TimelinePropertyTrack> : std::false_type {};
template <> struct is_automagical<Urho3D::TimelineControlTrack> : std::false_type {};
template <> struct is_automagical<Urho3D::TimelineAudioTrack> : std::false_type {};
template <> struct is_automagical<Urho3D::TimelineSignalTrack> : std::false_type {};
template <> struct is_automagical<Urho3D::TimelineMarkerTrack> : std::false_type {};
#endif

} // namespace sol

namespace Urho3D
{

void RegisterTimelineBindings(sol::state& lua, Context* context)
{
#if URHO3D_TIMELINE
    // Track types are registered as their own usertypes (and casters) even though the Lua surface
    // is the shared Track header, so TimelineResource::GetTrack hands back a typed handle that is
    // accepted by TimelinePlayer::SetTrackBinding(TimelineTrack*). Without a caster the wrapper
    // would fall back to the generic ObjectRef and the upcast to TimelineTrack* would be rejected.
    // The base TimelineTrack is registered first; sol3 requires declared bases to predate derived
    // usertypes. Per-clip authoring is intentionally NOT exposed here - tracks and their clips are
    // authored in the Timeline editor; this module only drives and inspects them at runtime.

    // TimelineTrack: shared header of every track (name / mute / weight / node binding).
    {
        using LUA_THIS = TimelineTrack;
        LUA_CLASS(TimelineTrack, sol::no_constructor
            LUA_BASES(Object)
            LUA_MEMBER_FUNC_RET(GetName, std::string)
            LUA_MEMBER_FUNC(SetName)
            LUA_MEMBER_FUNC_RET(IsMuted, bool)
            LUA_MEMBER_FUNC(SetMuted)
            LUA_MEMBER_FUNC_RET(GetWeight, float)
            LUA_MEMBER_FUNC(SetWeight)
            LUA_MEMBER_FUNC_RET(GetBindingPath, std::string)
            LUA_MEMBER_FUNC(SetBindingPath)
            LUA_MEMBER_FUNC_RET(GetContentDuration, float)
            LUA_MEMBER_FUNC_RET(GetTrackTypeName, std::string)
        );
    }
    RegisterLuaObjectWrapper<TimelineTrack>();

    // TimelineGroupTrack: contains child tracks (registered after its base TimelineTrack).
    {
        using LUA_THIS = TimelineGroupTrack;
        LUA_CLASS(TimelineGroupTrack, sol::no_constructor
            LUA_BASES(TimelineTrack, Object)
            LUA_MEMBER_FUNC_RET(GetNumTracks, unsigned)
            // GetTrack takes an index, so it is a raw lambda rather than the zero-arg OBJ form.
            , "GetTrack", [](TimelineGroupTrack* self, unsigned index, sol::this_state s) -> sol::object {
                return self ? WrapLuaObjectAs<TimelineTrack>(sol::state_view(s), self->GetTrack(index)) : sol::lua_nil;
            }
        );
    }
    RegisterLuaObjectWrapper<TimelineGroupTrack>();

    // Concrete track leaves. They add no extra Lua surface beyond TimelineTrack for runtime use;
    // registering them (plus casters) is what makes GetTrack resolve to a typed handle.
    {
        using LUA_THIS = TimelineAnimationTrack;
        LUA_CLASS(TimelineAnimationTrack, sol::no_constructor LUA_BASES(TimelineTrack, Object));
    }
    RegisterLuaObjectWrapper<TimelineAnimationTrack>();
    {
        using LUA_THIS = TimelineActivationTrack;
        LUA_CLASS(TimelineActivationTrack, sol::no_constructor LUA_BASES(TimelineTrack, Object));
    }
    RegisterLuaObjectWrapper<TimelineActivationTrack>();
    {
        using LUA_THIS = TimelinePropertyTrack;
        LUA_CLASS(TimelinePropertyTrack, sol::no_constructor LUA_BASES(TimelineTrack, Object));
    }
    RegisterLuaObjectWrapper<TimelinePropertyTrack>();
    {
        using LUA_THIS = TimelineControlTrack;
        LUA_CLASS(TimelineControlTrack, sol::no_constructor LUA_BASES(TimelineTrack, Object));
    }
    RegisterLuaObjectWrapper<TimelineControlTrack>();
    {
        using LUA_THIS = TimelineAudioTrack;
        LUA_CLASS(TimelineAudioTrack, sol::no_constructor LUA_BASES(TimelineTrack, Object));
    }
    RegisterLuaObjectWrapper<TimelineAudioTrack>();
    {
        using LUA_THIS = TimelineSignalTrack;
        LUA_CLASS(TimelineSignalTrack, sol::no_constructor LUA_BASES(TimelineTrack, Object));
    }
    RegisterLuaObjectWrapper<TimelineSignalTrack>();
    {
        using LUA_THIS = TimelineMarkerTrack;
        LUA_CLASS(TimelineMarkerTrack, sol::no_constructor LUA_BASES(TimelineTrack, Object));
    }
    RegisterLuaObjectWrapper<TimelineMarkerTrack>();

    // TimelineResource: the authored timeline (asset). Loaded via GetResource("TimelineResource", ...)
    // or the TimelinePlayer "Timeline" attribute; the caster makes the typed usertype come back.
    // Its base Resource is registered in the Resource module, which runs before this one.
    {
        using LUA_THIS = TimelineResource;
        LUA_CLASS(TimelineResource, sol::no_constructor
            LUA_BASES(Resource, Object)
            LUA_MEMBER_FUNC_RET(GetDuration, float)
            LUA_MEMBER_FUNC_RET(GetContentDuration, float)
            LUA_MEMBER_FUNC_RET(GetFixedDuration, float)
            LUA_MEMBER_FUNC(SetFixedDuration)
            LUA_MEMBER_FUNC_RET(GetNumTracks, unsigned)
            // GetTrack takes an index, so it is a raw lambda rather than the zero-arg OBJ form.
            , "GetTrack", [](TimelineResource* self, unsigned index, sol::this_state s) -> sol::object {
                return self ? WrapLuaObjectAs<TimelineTrack>(sol::state_view(s), self->GetTrack(index)) : sol::lua_nil;
            }
        );
    }
    RegisterLuaObjectWrapper<TimelineResource>();

    // TimelinePlayer (LogicComponent): the runtime driver. Play / Stop / scrub / speed plus the
    // imperative track-binding override. The serialized header properties (Wrap Mode, Time Update
    // Mode, Play On Awake, Playback Speed, Initial Time, Timeline) stay reachable through the
    // inherited Serializable Get/SetAttribute channel, e.g. player:SetAttribute("Wrap Mode", 2).
    {
        using LUA_THIS = TimelinePlayer;
        LUA_CLASS(TimelinePlayer, sol::no_constructor
            LUA_BASES(Component, Serializable, Object)
            LUA_MEMBER_FUNC(Play)
            LUA_MEMBER_FUNC(Stop)
            LUA_MEMBER_FUNC(SetPaused)
            LUA_MEMBER_FUNC_RET(IsPaused, bool)
            LUA_MEMBER_FUNC_RET(IsPlaying, bool)
            LUA_MEMBER_FUNC(Evaluate)
            LUA_MEMBER_FUNC(SetTime)
            LUA_MEMBER_FUNC_RET(GetTime, float)
            LUA_MEMBER_FUNC(SetPlaybackSpeed)
            LUA_MEMBER_FUNC_RET(GetPlaybackSpeed, float)
            LUA_MEMBER_FUNC_RET(GetDuration, float)
            LUA_MEMBER_FUNC_OBJ(GetTimeline, TimelineResource, GetTimeline)
            LUA_MEMBER_FUNC(SetTimeline)
            // Second parameter is optional so passing nil restores path-based binding.
            , "SetTrackBinding", [](TimelinePlayer* self, TimelineTrack* track, sol::optional<Object*> object) {
                if (self)
                    self->SetTrackBinding(track, object.value_or(nullptr));
            }
            , "GetTrackBinding", [](TimelinePlayer* self, TimelineTrack* track, sol::this_state s) -> sol::object {
                return self ? WrapLuaObject(sol::state_view(s), self->GetTrackBinding(track)) : sol::lua_nil;
            }
        );
    }
    RegisterLuaObjectWrapper<TimelinePlayer>();

    // SignalReceiver (Component): subscribes to timeline signals by name. Handlers run through the
    // E_TIMELINE_SIGNAL event, which is already reachable from Lua via SubscribeToEvent; the C++
    // Subscribe(callback) path is intentionally not exposed (a Lua callback would need marshalling
    // the event channel already provides).
    {
        using LUA_THIS = SignalReceiver;
        LUA_CLASS(SignalReceiver, sol::no_constructor
            LUA_BASES(Component, Serializable, Object)
            LUA_MEMBER_FUNC(AddSignal)
            LUA_MEMBER_FUNC_RET(RemoveSignal, bool)
            LUA_MEMBER_FUNC(RemoveAllSignals)
            LUA_MEMBER_FUNC_RET(GetNumSignals, unsigned)
            LUA_MEMBER_FUNC_RET(IsSubscribed, bool)
        );
    }
    RegisterLuaObjectWrapper<SignalReceiver>();

    // Convenience enum tables mirroring TimelineDefs, so scripts can name the integer payloads used
    // with SetAttribute("Wrap Mode", TimelineWrapMode.Loop) etc.
    LUA_ENUM_TABLE(TimelineWrapMode, "None", 0, "Hold", 1, "Loop", 2);
    LUA_ENUM_TABLE(TimelineTimeUpdateMode, "GameTime", 0, "UnscaledGameTime", 1, "Manual", 2);

    // The Timeline types are registered at engine startup by RegisterTimelineLibrary when the
    // module is enabled, so this module only adds usertypes/casters and never touches the Context.
    (void)context;
#else
    // Compiled out of this engine build; the entry point stays so the VM can keep calling it.
    (void)lua;
    (void)context;
#endif
}

} // namespace Urho3D

//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#include "../Urho3D/Precompiled.h"

#include "LuaBindings.h"
#include "LuaBindHelpers.h"
#include "LuaBindMacros.h"

#include "../Urho3D/Audio/Audio.h"
#include "../Urho3D/Audio/AudioDefs.h"
#include "../Urho3D/Audio/BufferedSoundStream.h"
#include "../Urho3D/Audio/Microphone.h"
#include "../Urho3D/Audio/Sound.h"
#include "../Urho3D/Audio/SoundListener.h"
#include "../Urho3D/Audio/SoundSource.h"
#include "../Urho3D/Audio/SoundSource3D.h"
#include "../Urho3D/Audio/SoundStream.h"
#include "../Urho3D/Core/Context.h"
#include "../Urho3D/Scene/Node.h"

#include <sol/sol.hpp>

namespace sol
{

template <> struct is_automagical<Urho3D::Audio> : std::false_type {};
template <> struct is_automagical<Urho3D::Sound> : std::false_type {};
template <> struct is_automagical<Urho3D::SoundSource> : std::false_type {};
template <> struct is_automagical<Urho3D::SoundSource3D> : std::false_type {};
template <> struct is_automagical<Urho3D::SoundListener> : std::false_type {};
template <> struct is_automagical<Urho3D::SoundStream> : std::false_type {};
template <> struct is_automagical<Urho3D::BufferedSoundStream> : std::false_type {};
template <> struct is_automagical<Urho3D::Microphone> : std::false_type {};

} // namespace sol

namespace Urho3D
{

void RegisterAudioBindings(sol::state& lua, Context* context)
{
    // Audio subsystem: master gain per sound type, microphone access.
    {
        using LUA_THIS = Audio;
        LUA_CLASS(Audio, sol::no_constructor
            LUA_BASES(Object)
            LUA_MEMBER_FUNC_RAW(SetMasterGain, [](Audio* audio, const char* type, float gain) {
                if (audio)
                    audio->SetMasterGain(type, gain);
            })
            LUA_MEMBER_FUNC_RAW(GetMasterGain, [](Audio* audio, const char* type) -> float {
                return audio ? audio->GetMasterGain(type) : 0.0f;
            })
            LUA_MEMBER_FUNC(ResumeAll)
            LUA_MEMBER_FUNC(Stop)
            // Enumerate available microphone pretty-names as a Lua array.
            LUA_MEMBER_FUNC_RAW(EnumerateMicrophones, [](Audio* audio, sol::this_state s) -> sol::object {
                if (!audio)
                    return sol::lua_nil;
                const StringVector names = audio->EnumerateMicrophones();
                sol::state_view lua(s);
                sol::table result = lua.create_table();
                for (unsigned i = 0; i < names.size(); ++i)
                    result[i + 1] = names[i];
                return result;
            })
            LUA_MEMBER_FUNC_RAW(CreateMicrophone, [](Audio* audio, const char* name, bool forSpeechRecog,
                unsigned wantedFreq, sol::optional<unsigned> silenceLevelLimit) -> SharedPtr<Microphone> {
                if (!audio)
                    return nullptr;
                return audio->CreateMicrophone(name, forSpeechRecog, wantedFreq, silenceLevelLimit.value_or(0));
            })
        );
    }
    RegisterLuaObjectWrapper<Audio>();

    // Sound resource: pass to SoundSource:Play.
    {
        using LUA_THIS = Sound;
        LUA_CLASS(Sound, sol::no_constructor
            LUA_BASES(Resource, Object)
            LUA_MEMBER_FUNC(SetLooped)
        );
    }
    RegisterLuaObjectWrapper<Sound>();

    // SoundSource: playback control. The single-argument Play overload is
    // enough for samples; full control is available through the extra args.
    {
        using LUA_THIS = SoundSource;
        LUA_CLASS(SoundSource, sol::no_constructor
            LUA_BASES(Component, Serializable, Object)
            LUA_MEMBER_FUNC_OVERLOAD(Play,
                LUA_CAST(Play, void, Sound*),
                LUA_CAST(Play, void, Sound*, float),
                LUA_CAST(Play, void, Sound*, float, float),
                LUA_CAST(Play, void, Sound*, float, float, float),
                LUA_CAST(Play, void, SoundStream*))
            LUA_MEMBER_FUNC_RAW(Stop, static_cast<void (SoundSource::*)()>(&SoundSource::Stop))
            LUA_MEMBER_FUNC_RAW(SetSoundType, [](SoundSource* source, const char* type) {
                if (source)
                    source->SetSoundType(type);
            })
            LUA_MEMBER_FUNC(SetFrequency)
            LUA_MEMBER_FUNC(SetGain)
            LUA_MEMBER_FUNC(SetAttenuation)
            LUA_MEMBER_FUNC(SetPanning)
            LUA_MEMBER_FUNC(SetReach)
            LUA_MEMBER_FUNC(SetLowFrequency)
            LUA_MEMBER_FUNC_ENUM(SetAutoRemoveMode, AutoRemoveMode)
            // Keep synthesizing audio even when the scene is paused
            // (29_SoundSynthesis).
            LUA_MEMBER_FUNC(SetIgnoreSceneTimeScale)
            LUA_MEMBER_FUNC(IsPlaying)
        );
    }
    RegisterLuaObjectWrapper<SoundSource>();

    // SoundSource3D: positional audio.
    {
        using LUA_THIS = SoundSource3D;
        LUA_CLASS(SoundSource3D, sol::no_constructor
            LUA_BASES(SoundSource, Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetNearDistance)
            LUA_MEMBER_FUNC(SetFarDistance)
        );
    }
    RegisterLuaObjectWrapper<SoundSource3D>();

    // SoundListener: marks the node ears are attached to.
    {
        using LUA_THIS = SoundListener;
        LUA_CLASS(SoundListener, sol::no_constructor
            LUA_BASES(Component, Serializable, Object)
        );
    }
    RegisterLuaObjectWrapper<SoundListener>();

    // SoundStream: abstract producer of audio data (SoundStream.h).
    {
        using LUA_THIS = SoundStream;
        LUA_CLASS(SoundStream, sol::no_constructor
            LUA_MEMBER_FUNC(SetFormat)
            LUA_MEMBER_FUNC(SetStopAtEnd)
            LUA_MEMBER_FUNC(GetFrequency)
            LUA_MEMBER_FUNC(GetSampleSize)
        );
    }

    // BufferedSoundStream: manually buffered stream used for microphone
    // capture playback (14_SoundEffects) and runtime synthesis
    // (29_SoundSynthesis).
    {
        using LUA_THIS = BufferedSoundStream;
        LUA_CLASS(BufferedSoundStream,
            sol::call_constructor, sol::factories([]() {
                return SharedPtr<BufferedSoundStream>(new BufferedSoundStream());
            }),
            // SoundStream is a RefCounted-only hierarchy (not URHO3D_OBJECT), so it
            // stays on plain sol::bases: LuaBases' audit machinery is Object-only.
            sol::base_classes, sol::bases<SoundStream>()
            // Raw sample bytes packed by Lua (string.pack or manual assembly).
            LUA_MEMBER_FUNC_RAW(AddData, [](BufferedSoundStream* stream, const std::string& data) {
                if (stream && !data.empty())
                    stream->AddData(const_cast<char*>(data.data()), static_cast<unsigned>(data.size()));
            })
            LUA_MEMBER_FUNC(Clear)
            LUA_MEMBER_FUNC(GetBufferNumBytes)
            LUA_MEMBER_FUNC(GetBufferLength)
        );
    }

    // Microphone: OS capture device, links into a BufferedSoundStream.
    {
        using LUA_THIS = Microphone;
        LUA_CLASS(Microphone, sol::no_constructor
            LUA_BASES(Object)
            LUA_MEMBER_FUNC(GetFrequency)
            LUA_MEMBER_FUNC_RAW(Link, [](Microphone* microphone, BufferedSoundStream* stream) {
                if (microphone && stream)
                    microphone->Link(SharedPtr<BufferedSoundStream>(stream));
            })
        );
    }

    // Sound type names for Audio:SetMasterGain / SoundSource:SetSoundType
    // (AudioDefs.h).
    LUA_ENUM_TABLE(SOUND, "MASTER", SOUND_MASTER, "EFFECT", SOUND_EFFECT,
        "AMBIENT", SOUND_AMBIENT, "MUSIC", SOUND_MUSIC);

    // Auto-remove modes for SoundSource:SetAutoRemoveMode (Component.h).
    LUA_ENUM_TABLE(REMOVE, "DISABLED", REMOVE_DISABLED, "COMPONENT", REMOVE_COMPONENT, "NODE", REMOVE_NODE);
}

} // namespace Urho3D

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
        using RBFX_THIS = Audio;
        RBFX_USERTYPE(Audio, sol::no_constructor
            RBFX_BASES(Object)
            RBFX_RAW(SetMasterGain, [](Audio* audio, const char* type, float gain) {
                if (audio)
                    audio->SetMasterGain(type, gain);
            })
            RBFX_RAW(GetMasterGain, [](Audio* audio, const char* type) -> float {
                return audio ? audio->GetMasterGain(type) : 0.0f;
            })
            RBFX_M(ResumeAll)
            RBFX_M(Stop)
            // Enumerate available microphone pretty-names as a Lua array.
            RBFX_RAW(EnumerateMicrophones, [](Audio* audio, sol::this_state s) -> sol::object {
                if (!audio)
                    return sol::lua_nil;
                const StringVector names = audio->EnumerateMicrophones();
                sol::state_view lua(s);
                sol::table result = lua.create_table();
                for (unsigned i = 0; i < names.size(); ++i)
                    result[i + 1] = names[i];
                return result;
            })
            RBFX_RAW(CreateMicrophone, [](Audio* audio, const char* name, bool forSpeechRecog,
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
        using RBFX_THIS = Sound;
        RBFX_USERTYPE(Sound, sol::no_constructor
            RBFX_BASES(Resource, Object)
            RBFX_M(SetLooped)
        );
    }
    RegisterLuaObjectWrapper<Sound>();

    // SoundSource: playback control. The single-argument Play overload is
    // enough for samples; full control is available through the extra args.
    {
        using RBFX_THIS = SoundSource;
        RBFX_USERTYPE(SoundSource, sol::no_constructor
            RBFX_BASES(Component, Serializable, Object)
            RBFX_OVERLOAD(Play,
                RBFX_CAST(Play, void, Sound*),
                RBFX_CAST(Play, void, Sound*, float),
                RBFX_CAST(Play, void, Sound*, float, float),
                RBFX_CAST(Play, void, Sound*, float, float, float),
                RBFX_CAST(Play, void, SoundStream*))
            RBFX_RAW(Stop, static_cast<void (SoundSource::*)()>(&SoundSource::Stop))
            RBFX_RAW(SetSoundType, [](SoundSource* source, const char* type) {
                if (source)
                    source->SetSoundType(type);
            })
            RBFX_M(SetFrequency)
            RBFX_M(SetGain)
            RBFX_M(SetAttenuation)
            RBFX_M(SetPanning)
            RBFX_M(SetReach)
            RBFX_M(SetLowFrequency)
            RBFX_M_ENUM(SetAutoRemoveMode, AutoRemoveMode)
            // Keep synthesizing audio even when the scene is paused
            // (29_SoundSynthesis).
            RBFX_M(SetIgnoreSceneTimeScale)
            RBFX_M(IsPlaying)
        );
    }
    RegisterLuaObjectWrapper<SoundSource>();

    // SoundSource3D: positional audio.
    {
        using RBFX_THIS = SoundSource3D;
        RBFX_USERTYPE(SoundSource3D, sol::no_constructor
            RBFX_BASES(SoundSource, Component, Serializable, Object)
            RBFX_M(SetNearDistance)
            RBFX_M(SetFarDistance)
        );
    }
    RegisterLuaObjectWrapper<SoundSource3D>();

    // SoundListener: marks the node ears are attached to.
    {
        using RBFX_THIS = SoundListener;
        RBFX_USERTYPE(SoundListener, sol::no_constructor
            RBFX_BASES(Component, Serializable, Object)
        );
    }
    RegisterLuaObjectWrapper<SoundListener>();

    // SoundStream: abstract producer of audio data (SoundStream.h).
    {
        using RBFX_THIS = SoundStream;
        RBFX_USERTYPE(SoundStream, sol::no_constructor
            RBFX_M(SetFormat)
            RBFX_M(SetStopAtEnd)
            RBFX_M(GetFrequency)
            RBFX_M(GetSampleSize)
        );
    }

    // BufferedSoundStream: manually buffered stream used for microphone
    // capture playback (14_SoundEffects) and runtime synthesis
    // (29_SoundSynthesis).
    {
        using RBFX_THIS = BufferedSoundStream;
        RBFX_USERTYPE(BufferedSoundStream,
            sol::call_constructor, sol::factories([]() {
                return SharedPtr<BufferedSoundStream>(new BufferedSoundStream());
            }),
            // SoundStream is a RefCounted-only hierarchy (not URHO3D_OBJECT), so it
            // stays on plain sol::bases: LuaBases' audit machinery is Object-only.
            sol::base_classes, sol::bases<SoundStream>()
            // Raw sample bytes packed by Lua (string.pack or manual assembly).
            RBFX_RAW(AddData, [](BufferedSoundStream* stream, const std::string& data) {
                if (stream && !data.empty())
                    stream->AddData(const_cast<char*>(data.data()), static_cast<unsigned>(data.size()));
            })
            RBFX_M(Clear)
            RBFX_M(GetBufferNumBytes)
            RBFX_M(GetBufferLength)
        );
    }

    // Microphone: OS capture device, links into a BufferedSoundStream.
    {
        using RBFX_THIS = Microphone;
        RBFX_USERTYPE(Microphone, sol::no_constructor
            RBFX_BASES(Object)
            RBFX_M(GetFrequency)
            RBFX_RAW(Link, [](Microphone* microphone, BufferedSoundStream* stream) {
                if (microphone && stream)
                    microphone->Link(SharedPtr<BufferedSoundStream>(stream));
            })
        );
    }

    // Sound type names for Audio:SetMasterGain / SoundSource:SetSoundType
    // (AudioDefs.h).
    RBFX_ENUM_TABLE(SOUND, "MASTER", SOUND_MASTER, "EFFECT", SOUND_EFFECT,
        "AMBIENT", SOUND_AMBIENT, "MUSIC", SOUND_MUSIC);

    // Auto-remove modes for SoundSource:SetAutoRemoveMode (Component.h).
    RBFX_ENUM_TABLE(REMOVE, "DISABLED", REMOVE_DISABLED, "COMPONENT", REMOVE_COMPONENT, "NODE", REMOVE_NODE);
}

} // namespace Urho3D

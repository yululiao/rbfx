//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#include "../Urho3D/Precompiled.h"

#include "LuaBindings.h"

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
    lua.new_usertype<Audio>("Audio",
        sol::no_constructor,
        sol::base_classes, sol::bases<Object>(),
        "SetMasterGain", [](Audio* audio, const char* type, float gain) {
            if (audio)
                audio->SetMasterGain(type, gain);
        },
        "GetMasterGain", [](Audio* audio, const char* type) -> float {
            return audio ? audio->GetMasterGain(type) : 0.0f;
        },
        "ResumeAll", &Audio::ResumeAll,
        "Stop", &Audio::Stop,
        // Enumerate available microphone pretty-names as a Lua array.
        "EnumerateMicrophones", [](Audio* audio, sol::this_state s) -> sol::object {
            if (!audio)
                return sol::lua_nil;
            const StringVector names = audio->EnumerateMicrophones();
            sol::state_view lua(s);
            sol::table result = lua.create_table();
            for (unsigned i = 0; i < names.size(); ++i)
                result[i + 1] = names[i];
            return result;
        },
        "CreateMicrophone", [](Audio* audio, const char* name, bool forSpeechRecog,
            unsigned wantedFreq, sol::optional<unsigned> silenceLevelLimit) -> SharedPtr<Microphone> {
            if (!audio)
                return nullptr;
            return audio->CreateMicrophone(name, forSpeechRecog, wantedFreq, silenceLevelLimit.value_or(0));
        }
    );
    RegisterLuaObjectWrapper<Audio>();

    // Sound resource: pass to SoundSource:Play.
    lua.new_usertype<Sound>("Sound",
        sol::no_constructor,
        sol::base_classes, sol::bases<Resource, Object>(),
        "SetLooped", &Sound::SetLooped
    );
    RegisterLuaObjectWrapper<Sound>();

    // SoundSource: playback control. The single-argument Play overload is
    // enough for samples; full control is available through the extra args.
    lua.new_usertype<SoundSource>("SoundSource",
        sol::no_constructor,
        sol::base_classes, sol::bases<Component, Serializable, Object>(),
        "Play", sol::overload(
            static_cast<void (SoundSource::*)(Sound*)>(&SoundSource::Play),
            static_cast<void (SoundSource::*)(Sound*, float)>(&SoundSource::Play),
            static_cast<void (SoundSource::*)(Sound*, float, float)>(&SoundSource::Play),
            static_cast<void (SoundSource::*)(Sound*, float, float, float)>(&SoundSource::Play),
            static_cast<void (SoundSource::*)(SoundStream*)>(&SoundSource::Play)),
        "Stop", static_cast<void (SoundSource::*)()>(&SoundSource::Stop),
        "SetSoundType", [](SoundSource* source, const char* type) {
            if (source)
                source->SetSoundType(type);
        },
        "SetFrequency", &SoundSource::SetFrequency,
        "SetGain", &SoundSource::SetGain,
        "SetAttenuation", &SoundSource::SetAttenuation,
        "SetPanning", &SoundSource::SetPanning,
        "SetReach", &SoundSource::SetReach,
        "SetLowFrequency", &SoundSource::SetLowFrequency,
        "SetAutoRemoveMode", [](SoundSource* source, int mode) {
            if (source)
                source->SetAutoRemoveMode(static_cast<AutoRemoveMode>(mode));
        },
        // Keep synthesizing audio even when the scene is paused
        // (29_SoundSynthesis).
        "SetIgnoreSceneTimeScale", &SoundSource::SetIgnoreSceneTimeScale,
        "IsPlaying", &SoundSource::IsPlaying
    );
    RegisterLuaObjectWrapper<SoundSource>();

    // SoundSource3D: positional audio.
    lua.new_usertype<SoundSource3D>("SoundSource3D",
        sol::no_constructor,
        sol::base_classes, sol::bases<SoundSource, Component, Serializable, Object>(),
        "SetNearDistance", &SoundSource3D::SetNearDistance,
        "SetFarDistance", &SoundSource3D::SetFarDistance
    );
    RegisterLuaObjectWrapper<SoundSource3D>();

    // SoundListener: marks the node ears are attached to.
    lua.new_usertype<SoundListener>("SoundListener",
        sol::no_constructor,
        sol::base_classes, sol::bases<Component, Serializable, Object>()
    );
    RegisterLuaObjectWrapper<SoundListener>();

    // SoundStream: abstract producer of audio data (SoundStream.h).
    lua.new_usertype<SoundStream>("SoundStream",
        sol::no_constructor,
        "SetFormat", &SoundStream::SetFormat,
        "SetStopAtEnd", &SoundStream::SetStopAtEnd,
        "GetFrequency", &SoundStream::GetFrequency,
        "GetSampleSize", &SoundStream::GetSampleSize
    );

    // BufferedSoundStream: manually buffered stream used for microphone
    // capture playback (14_SoundEffects) and runtime synthesis
    // (29_SoundSynthesis).
    lua.new_usertype<BufferedSoundStream>("BufferedSoundStream",
        sol::call_constructor, sol::factories([]() {
            return SharedPtr<BufferedSoundStream>(new BufferedSoundStream());
        }),
        sol::base_classes, sol::bases<SoundStream>(),
        // Raw sample bytes packed by Lua (string.pack or manual assembly).
        "AddData", [](BufferedSoundStream* stream, const std::string& data) {
            if (stream && !data.empty())
                stream->AddData(const_cast<char*>(data.data()), static_cast<unsigned>(data.size()));
        },
        "Clear", &BufferedSoundStream::Clear,
        "GetBufferNumBytes", &BufferedSoundStream::GetBufferNumBytes,
        "GetBufferLength", &BufferedSoundStream::GetBufferLength
    );

    // Microphone: OS capture device, links into a BufferedSoundStream.
    lua.new_usertype<Microphone>("Microphone",
        sol::no_constructor,
        sol::base_classes, sol::bases<Object>(),
        "GetFrequency", &Microphone::GetFrequency,
        "Link", [](Microphone* microphone, BufferedSoundStream* stream) {
            if (microphone && stream)
                microphone->Link(SharedPtr<BufferedSoundStream>(stream));
        }
    );

    // Sound type names for Audio:SetMasterGain / SoundSource:SetSoundType
    // (AudioDefs.h).
    sol::table sound = lua.create_named_table("SOUND");
    sound["MASTER"] = SOUND_MASTER;
    sound["EFFECT"] = SOUND_EFFECT;
    sound["AMBIENT"] = SOUND_AMBIENT;
    sound["MUSIC"] = SOUND_MUSIC;

    // Auto-remove modes for SoundSource:SetAutoRemoveMode (Component.h).
    sol::table remove = lua.create_named_table("REMOVE");
    remove["DISABLED"] = REMOVE_DISABLED;
    remove["COMPONENT"] = REMOVE_COMPONENT;
    remove["NODE"] = REMOVE_NODE;
}

} // namespace Urho3D

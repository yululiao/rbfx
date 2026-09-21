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
#include "../Urho3D/IO/FileSystem.h"
#include "../Urho3D/IO/Log.h"
#include "../Urho3D/Resource/Resource.h"
#include "../Urho3D/Resource/ResourceCache.h"
#include "../Urho3D/Resource/Localization.h"
#include "../Urho3D/Resource/XMLFile.h"
#include "../Urho3D/Scene/PrefabResource.h"

#include <sol/sol.hpp>

namespace sol
{

template <> struct is_automagical<Urho3D::Localization> : std::false_type {};

template <> struct is_automagical<Urho3D::ResourceCache> : std::false_type {};
template <> struct is_automagical<Urho3D::Resource> : std::false_type {};
template <> struct is_automagical<Urho3D::XMLFile> : std::false_type {};
template <> struct is_automagical<Urho3D::FileSystem> : std::false_type {};

} // namespace sol

namespace Urho3D
{

void RegisterResourceBindings(sol::state& lua, Context* context)
{
    // Base resource class. Concrete resources are exposed through their
    // dedicated modules; everything else flows through LuaObjectRef with
    // attribute reflection.
    {
        using LUA_THIS = Resource;
        LUA_CLASS(Resource, sol::no_constructor
            LUA_BASES(Object)
            LUA_MEMBER_PROP_RAW(name, sol::readonly_property([](Resource* resource) -> std::string {
                return resource ? resource->GetName().c_str() : "";
            }))
            LUA_MEMBER_FUNC_RET(GetName, std::string)
            LUA_MEMBER_FUNC(GetMemoryUse)
            LUA_MEMBER_FUNC(SetName)
        );
    }

    {
        using LUA_THIS = ResourceCache;
        LUA_CLASS(ResourceCache, sol::no_constructor
            LUA_BASES(Object)
            // Typed resource getters: the type is resolved by the same name-based
            // factory used by Node::CreateComponent, so every registered resource
            // type works without a dedicated binding.
            LUA_MEMBER_FUNC_RAW(GetResource, [context](ResourceCache* cache, const char* typeName, const char* name, sol::this_state s) -> sol::object {
                if (!cache)
                    return sol::lua_nil;
                const StringHash type{typeName};
                if (!context->GetTypeName(type).empty())
                {
                    Resource* resource = cache->GetResource(type, name);
                    return WrapLuaObject(sol::state_view(s), resource);
                }
                URHO3D_LOGERROR("Resource type '{}' is not registered", typeName);
                return sol::lua_nil;
            })
            LUA_MEMBER_FUNC(Exists)
            LUA_MEMBER_FUNC_RAW(GetResourceFileName, [](ResourceCache* cache, const char* name) -> std::string {
                return cache ? cache->GetResourceFileName(name).c_str() : "";
            })
            LUA_MEMBER_FUNC_OVERLOAD(ReleaseResource,
                LUA_CAST(ReleaseResource, void, const ea::string&, bool))
        );
    }
    RegisterLuaObjectWrapper<ResourceCache>();

    // XMLFile: load arbitrary XML resources for manual parsing needs.
    {
        using LUA_THIS = XMLFile;
        LUA_CLASS(XMLFile, sol::no_constructor
            LUA_BASES(Resource, Object)
        );
    }
    RegisterLuaObjectWrapper<XMLFile>();

    // PrefabResource: instanced scene fragment, consumed by PrefabReference
    // (LuaNodeBindings). Lives HERE, not next to its consumer: the LuaBases
    // audit requires Resource to be registered first, and the Node module
    // runs before this one.
    {
        using LUA_THIS = PrefabResource;
        LUA_CLASS(PrefabResource, sol::no_constructor
            LUA_BASES(Resource, Object)
        );
    }
    RegisterLuaObjectWrapper<PrefabResource>();

    // FileSystem: path helpers.
    {
        using LUA_THIS = FileSystem;
        LUA_CLASS(FileSystem, sol::no_constructor
            LUA_BASES(Object)
            LUA_MEMBER_FUNC(FileExists)
            LUA_MEMBER_FUNC(DirExists)
            LUA_MEMBER_FUNC_RET(GetProgramDir, std::string)
            LUA_MEMBER_FUNC_RET(GetUserDocumentsDir, std::string)
        );
    }
    RegisterLuaObjectWrapper<FileSystem>();

    // Localization: string translation subsystem (40_Localization).
    {
        using LUA_THIS = Localization;
        LUA_CLASS(Localization, sol::no_constructor
            LUA_BASES(Object)
            LUA_MEMBER_FUNC_OVERLOAD(SetLanguage,
                LUA_CAST(SetLanguage, void, int),
                [](Localization* l10n, const char* language) {
                    if (l10n)
                        l10n->SetLanguage(language);
                })
            LUA_MEMBER_FUNC_OVERLOAD(GetLanguageIndex,
                LUA_CAST_C(GetLanguageIndex, int),
                [](Localization* l10n, const char* language) -> int {
                    return l10n ? l10n->GetLanguageIndex(language) : -1;
                })
            LUA_MEMBER_FUNC_OVERLOAD(GetLanguage,
                LUA_CAST(GetLanguage, const ea::string&),
                [](Localization* l10n, int index) -> ea::string {
                    return l10n ? l10n->GetLanguage(index) : ea::string{};
                })
            LUA_MEMBER_FUNC(GetNumLanguages)
            LUA_MEMBER_FUNC_RAW(LoadJSONFile, [](Localization* l10n, const char* name, sol::optional<const char*> language) {
                if (l10n)
                    l10n->LoadJSONFile(name, language ? *language : "");
            })
            LUA_MEMBER_FUNC_RAW(GetString, [](Localization* l10n, const char* id, sol::optional<int> index) -> std::string {
                return l10n ? std::string(l10n->Get(id, index.value_or(-1)).c_str()) : std::string{};
            })
            LUA_MEMBER_FUNC(Reset)
        );
    }
    RegisterLuaObjectWrapper<Localization>();

    // Global helper mirroring cache->GetResource<T>(name) with type name.
    LUA_GLOBAL_FUNC(GetResource, [context](sol::this_state s, const char* typeName, const char* name) -> sol::object {
        auto* cache = context->GetSubsystem<ResourceCache>();
        if (!cache)
            return sol::lua_nil;
        Resource* resource = cache->GetResource(StringHash(typeName), name);
        return WrapLuaObject(sol::state_view(s), resource);
    });
}

} // namespace Urho3D

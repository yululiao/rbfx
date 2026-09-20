//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#include "../Urho3D/Precompiled.h"

#include "LuaBindings.h"
#include "LuaBindHelpers.h"

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
    lua.new_usertype<Resource>("Resource",
        sol::no_constructor,
        sol::base_classes, LuaBases<Resource, Object>::bases(lua),
        "name", sol::readonly_property([](Resource* resource) -> std::string {
            return resource ? resource->GetName().c_str() : "";
        }),
        "GetName", [](Resource* resource) -> std::string { return resource ? resource->GetName().c_str() : ""; },
        "GetMemoryUse", &Resource::GetMemoryUse,
        "SetName", &Resource::SetName
    );

    lua.new_usertype<ResourceCache>("ResourceCache",
        sol::no_constructor,
        sol::base_classes, LuaBases<ResourceCache, Object>::bases(lua),
        // Typed resource getters: the type is resolved by the same name-based
        // factory used by Node::CreateComponent, so every registered resource
        // type works without a dedicated binding.
        "GetResource", [context](ResourceCache* cache, const char* typeName, const char* name, sol::this_state s) -> sol::object {
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
        },
        "Exists", &ResourceCache::Exists,
        "GetResourceFileName", [](ResourceCache* cache, const char* name) -> std::string {
            return cache ? cache->GetResourceFileName(name).c_str() : "";
        },
        "ReleaseResource", sol::overload(
            static_cast<void (ResourceCache::*)(const ea::string&, bool)>(&ResourceCache::ReleaseResource))
    );
    RegisterLuaObjectWrapper<ResourceCache>();

    // XMLFile: load arbitrary XML resources for manual parsing needs.
    lua.new_usertype<XMLFile>("XMLFile",
        sol::no_constructor,
        sol::base_classes, LuaBases<XMLFile, Resource, Object>::bases(lua)
    );
    RegisterLuaObjectWrapper<XMLFile>();

    // PrefabResource: instanced scene fragment, consumed by PrefabReference
    // (LuaNodeBindings). Lives HERE, not next to its consumer: the LuaBases
    // audit requires Resource to be registered first, and the Node module
    // runs before this one.
    lua.new_usertype<PrefabResource>("PrefabResource",
        sol::no_constructor,
        sol::base_classes, LuaBases<PrefabResource, Resource, Object>::bases(lua)
    );
    RegisterLuaObjectWrapper<PrefabResource>();

    // FileSystem: path helpers.
    lua.new_usertype<FileSystem>("FileSystem",
        sol::no_constructor,
        sol::base_classes, LuaBases<FileSystem, Object>::bases(lua),
        "FileExists", &FileSystem::FileExists,
        "DirExists", &FileSystem::DirExists,
        "GetProgramDir", [](FileSystem* fs) -> std::string { return fs ? fs->GetProgramDir().c_str() : ""; },
        "GetUserDocumentsDir", [](FileSystem* fs) -> std::string { return fs ? fs->GetUserDocumentsDir().c_str() : ""; }
    );
    RegisterLuaObjectWrapper<FileSystem>();

    // Localization: string translation subsystem (40_Localization).
    lua.new_usertype<Localization>("Localization",
        sol::no_constructor,
        sol::base_classes, LuaBases<Localization, Object>::bases(lua),
        "SetLanguage", sol::overload(
            static_cast<void (Localization::*)(int)>(&Localization::SetLanguage),
            [](Localization* l10n, const char* language) {
                if (l10n)
                    l10n->SetLanguage(language);
            }),
        "GetLanguageIndex", sol::overload(
            static_cast<int (Localization::*)() const>(&Localization::GetLanguageIndex),
            [](Localization* l10n, const char* language) -> int {
                return l10n ? l10n->GetLanguageIndex(language) : -1;
            }),
        "GetLanguage", sol::overload(
            static_cast<const ea::string& (Localization::*)()>(&Localization::GetLanguage),
            [](Localization* l10n, int index) -> ea::string {
                return l10n ? l10n->GetLanguage(index) : ea::string{};
            }),
        "GetNumLanguages", &Localization::GetNumLanguages,
        "LoadJSONFile", [](Localization* l10n, const char* name, sol::optional<const char*> language) {
            if (l10n)
                l10n->LoadJSONFile(name, language ? *language : "");
        },
        "GetString", [](Localization* l10n, const char* id, sol::optional<int> index) -> std::string {
            return l10n ? std::string(l10n->Get(id, index.value_or(-1)).c_str()) : std::string{};
        },
        "Reset", &Localization::Reset
    );
    RegisterLuaObjectWrapper<Localization>();

    // Global helper mirroring cache->GetResource<T>(name) with type name.
    lua.set_function("GetResource", [context](sol::this_state s, const char* typeName, const char* name) -> sol::object {
        auto* cache = context->GetSubsystem<ResourceCache>();
        if (!cache)
            return sol::lua_nil;
        Resource* resource = cache->GetResource(StringHash(typeName), name);
        return WrapLuaObject(sol::state_view(s), resource);
    });
}

} // namespace Urho3D

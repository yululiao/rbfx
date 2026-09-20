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
        using RBFX_THIS = Resource;
        RBFX_USERTYPE(Resource, sol::no_constructor
            RBFX_BASES(Object)
            RBFX_RAW(name, sol::readonly_property([](Resource* resource) -> std::string {
                return resource ? resource->GetName().c_str() : "";
            }))
            RBFX_M_RET(GetName, std::string)
            RBFX_M(GetMemoryUse)
            RBFX_M(SetName)
        );
    }

    {
        using RBFX_THIS = ResourceCache;
        RBFX_USERTYPE(ResourceCache, sol::no_constructor
            RBFX_BASES(Object)
            // Typed resource getters: the type is resolved by the same name-based
            // factory used by Node::CreateComponent, so every registered resource
            // type works without a dedicated binding.
            RBFX_RAW(GetResource, [context](ResourceCache* cache, const char* typeName, const char* name, sol::this_state s) -> sol::object {
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
            RBFX_M(Exists)
            RBFX_RAW(GetResourceFileName, [](ResourceCache* cache, const char* name) -> std::string {
                return cache ? cache->GetResourceFileName(name).c_str() : "";
            })
            RBFX_OVERLOAD(ReleaseResource,
                RBFX_CAST(ReleaseResource, void, const ea::string&, bool))
        );
    }
    RegisterLuaObjectWrapper<ResourceCache>();

    // XMLFile: load arbitrary XML resources for manual parsing needs.
    {
        using RBFX_THIS = XMLFile;
        RBFX_USERTYPE(XMLFile, sol::no_constructor
            RBFX_BASES(Resource, Object)
        );
    }
    RegisterLuaObjectWrapper<XMLFile>();

    // PrefabResource: instanced scene fragment, consumed by PrefabReference
    // (LuaNodeBindings). Lives HERE, not next to its consumer: the LuaBases
    // audit requires Resource to be registered first, and the Node module
    // runs before this one.
    {
        using RBFX_THIS = PrefabResource;
        RBFX_USERTYPE(PrefabResource, sol::no_constructor
            RBFX_BASES(Resource, Object)
        );
    }
    RegisterLuaObjectWrapper<PrefabResource>();

    // FileSystem: path helpers.
    {
        using RBFX_THIS = FileSystem;
        RBFX_USERTYPE(FileSystem, sol::no_constructor
            RBFX_BASES(Object)
            RBFX_M(FileExists)
            RBFX_M(DirExists)
            RBFX_M_RET(GetProgramDir, std::string)
            RBFX_M_RET(GetUserDocumentsDir, std::string)
        );
    }
    RegisterLuaObjectWrapper<FileSystem>();

    // Localization: string translation subsystem (40_Localization).
    {
        using RBFX_THIS = Localization;
        RBFX_USERTYPE(Localization, sol::no_constructor
            RBFX_BASES(Object)
            RBFX_OVERLOAD(SetLanguage,
                RBFX_CAST(SetLanguage, void, int),
                [](Localization* l10n, const char* language) {
                    if (l10n)
                        l10n->SetLanguage(language);
                })
            RBFX_OVERLOAD(GetLanguageIndex,
                RBFX_CAST_C(GetLanguageIndex, int),
                [](Localization* l10n, const char* language) -> int {
                    return l10n ? l10n->GetLanguageIndex(language) : -1;
                })
            RBFX_OVERLOAD(GetLanguage,
                RBFX_CAST(GetLanguage, const ea::string&),
                [](Localization* l10n, int index) -> ea::string {
                    return l10n ? l10n->GetLanguage(index) : ea::string{};
                })
            RBFX_M(GetNumLanguages)
            RBFX_RAW(LoadJSONFile, [](Localization* l10n, const char* name, sol::optional<const char*> language) {
                if (l10n)
                    l10n->LoadJSONFile(name, language ? *language : "");
            })
            RBFX_RAW(GetString, [](Localization* l10n, const char* id, sol::optional<int> index) -> std::string {
                return l10n ? std::string(l10n->Get(id, index.value_or(-1)).c_str()) : std::string{};
            })
            RBFX_M(Reset)
        );
    }
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

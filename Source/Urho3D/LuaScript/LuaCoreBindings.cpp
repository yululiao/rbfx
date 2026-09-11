//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#include "../Precompiled.h"

#include "../LuaScript/LuaBindings.h"

#include "../Core/Context.h"
#include "../Core/StringUtils.h"
#include "../Core/Timer.h"
#include "../Engine/Engine.h"
#include "../IO/Log.h"
#include "../Scene/Serializable.h"

#include <sol/sol.hpp>

#include <string>

namespace sol
{

// LuaObjectRef is a plain wrapper without comparison operators.
template <>
struct is_automagical<Urho3D::LuaObjectRef> : std::false_type {};

} // namespace sol

namespace Urho3D
{

namespace
{

ea::unordered_map<StringHash, LuaObjectCaster>& GetCasterRegistry()
{
    static ea::unordered_map<StringHash, LuaObjectCaster> registry;
    return registry;
}

/// Lowercase a name and drop spaces so Lua keys map to engine attribute names
/// ("FarClip" and "Far Clip" both resolve to the "Far Clip" attribute).
ea::string NormalizeAttributeName(const ea::string& name)
{
    ea::string normalized;
    normalized.reserve(name.size());
    for (char ch : name)
    {
        if (ch != ' ')
            normalized.push_back(static_cast<char>(ToLower(ch)));
    }
    return normalized;
}

/// Match a Lua attribute key against engine attribute names.
const AttributeInfo* FindAttribute(const Serializable* serializable, const char* key)
{
    const auto* attributes = serializable->GetAttributes();
    if (!attributes)
        return nullptr;

    const ea::string normalized = NormalizeAttributeName(key);
    for (const AttributeInfo& attr : *attributes)
    {
        if (NormalizeAttributeName(attr.name_) == normalized)
            return &attr;
    }
    return nullptr;
}

} // namespace

void RegisterLuaObjectCaster(StringHash type, LuaObjectCaster caster)
{
    GetCasterRegistry()[type] = caster;
}

sol::object WrapLuaObject(sol::state_view lua, Object* object)
{
    if (!object)
        return sol::lua_nil;

    auto& registry = GetCasterRegistry();
    const auto iter = registry.find(object->GetType());
    if (iter != registry.end())
        return iter->second(lua, object);

    return sol::make_object(lua, LuaObjectRef{object});
}

Variant LuaToVariant(sol::state_view lua, const sol::object& value)
{
    if (!value.valid() || value == sol::lua_nil)
        return Variant::EMPTY;

    const sol::type type = value.get_type();
    switch (type)
    {
    case sol::type::boolean:
        return Variant(value.as<bool>());
    case sol::type::string:
        return Variant(value.as<const char*>());
    case sol::type::number:
    {
        // Prefer integral representation when the number is whole.
        const double number = value.as<double>();
        if (number == std::floor(number) && std::abs(number) < 2147483648.0)
            return Variant(static_cast<int>(number));
        return Variant(static_cast<double>(number));
    }
    case sol::type::table:
    {
        // ResourceRef shorthand for attribute animations, e.g.
        // { type = "Texture2D", name = "Urho2D/GoldIcon/1.png" }
        // (30_LightAnimation sprite texture keyframes).
        const sol::table table = value.as<sol::table>();
        const sol::object typeName = table["type"];
        const sol::object resourceName = table["name"];
        if (typeName.valid() && typeName.is<const char*>() && resourceName.valid()
            && resourceName.is<const char*>())
            return Variant(ResourceRef(typeName.as<const char*>(), resourceName.as<const char*>()));
        return Variant::EMPTY;
    }
    case sol::type::userdata:
        if (value.is<Vector3>())
            return Variant(value.as<Vector3>());
        if (value.is<Vector2>())
            return Variant(value.as<Vector2>());
        if (value.is<Vector4>())
            return Variant(value.as<Vector4>());
        if (value.is<Quaternion>())
            return Variant(value.as<Quaternion>());
        if (value.is<Color>())
            return Variant(value.as<Color>());
        if (value.is<IntVector2>())
            return Variant(value.as<IntVector2>());
        if (value.is<LuaObjectRef>())
            return Variant(value.as<LuaObjectRef>().Get());
        if (value.is<Object*>())
            return Variant(value.as<Object*>());
        return Variant::EMPTY;
    default:
        return Variant::EMPTY;
    }
}

// Read-only view over a nested VariantMap stored inside a Variant, e.g.
// event payloads like the NetworkHostDiscovered Beacon (53_LANDiscovery).
// Keys are StringHashes resolved by name on lookup, mirroring EventData.
struct LuaVariantMapView
{
    const VariantMap* map_;
};

sol::object VariantToLua(sol::state_view lua, const Variant& value)
{
    switch (value.GetType())
    {
    case VAR_BOOL: return sol::make_object(lua, value.GetBool());
    case VAR_INT: return sol::make_object(lua, value.GetInt());
    case VAR_INT64: return sol::make_object(lua, value.GetInt64());
    case VAR_FLOAT: return sol::make_object(lua, value.GetFloat());
    case VAR_DOUBLE: return sol::make_object(lua, value.GetDouble());
    case VAR_STRING: return sol::make_object(lua, value.GetString().c_str());
    case VAR_VECTOR2: return sol::make_object(lua, value.GetVector2());
    case VAR_VECTOR3: return sol::make_object(lua, value.GetVector3());
    case VAR_VECTOR4: return sol::make_object(lua, value.GetVector4());
    case VAR_QUATERNION: return sol::make_object(lua, value.GetQuaternion());
    case VAR_COLOR: return sol::make_object(lua, value.GetColor());
    case VAR_INTVECTOR2: return sol::make_object(lua, value.GetIntVector2());
    case VAR_VARIANTMAP:
    {
        // Nested variant maps (e.g. the NetworkHostDiscovered Beacon in
        // 53_LANDiscovery) surface as a read-only view keyed by parameter
        // name. VariantMap keys are StringHashes and thus not reversible,
        // so a plain Lua table with string keys cannot be produced; the
        // view resolves names on lookup, exactly like EventData itself.
        return sol::make_object(lua, LuaVariantMapView{&value.GetVariantMap()});
    }
    case VAR_BUFFER:
    {
        // Byte buffers (e.g. NetworkMessage Data) cross the boundary as
        // binary-safe Lua strings.
        const ea::vector<unsigned char>& buffer = value.GetBuffer();
        return sol::make_object(lua, std::string(buffer.begin(), buffer.end()));
    }
    case VAR_PTR:
        // Pointer parameters resolve through the usertype registry so that
        // event data exposes bound objects (nodes, components, connections).
        if (Object* object = dynamic_cast<Object*>(value.GetPtr()))
            return WrapLuaObject(lua, object);
        return sol::lua_nil;
    default: return sol::lua_nil;
    }
}

sol::table VariantVectorToLuaTable(sol::state_view lua, const VariantVector& values)
{
    sol::table table = lua.create_table(static_cast<unsigned>(values.size()), 0);
    for (unsigned i = 0; i < values.size(); ++i)
        table[i + 1] = VariantToLua(lua, values[i]);
    return table;
}

void RegisterCoreBindings(sol::state& lua, Context* context)
{
    // Nested VariantMap view used by VariantToLua for VAR_VARIANTMAP values.
    lua.new_usertype<LuaVariantMapView>("VariantMapView",
        sol::no_constructor,
        sol::meta_function::index,
        [](LuaVariantMapView& self, const char* name, sol::this_state s) -> sol::object {
            if (!self.map_)
                return sol::lua_nil;
            const auto iter = self.map_->find(StringHash(name));
            if (iter == self.map_->end())
                return sol::lua_nil;
            return VariantToLua(sol::state_view(s), iter->second);
        },
        "Contains", [](LuaVariantMapView& self, const char* name) {
            return self.map_ && self.map_->find(StringHash(name)) != self.map_->end();
        }
    );

    // Base class of every engine object with an identity. Explicit attribute
    // reflection methods give Lua access to all Serializable attributes even
    // for types without dedicated usertype bindings.
    lua.new_usertype<Object>("Object",
        sol::no_constructor,
        sol::meta_function::equal_to, [](Object* a, Object* b) { return a == b; },
        "GetTypeName", [](Object* object) -> std::string { return object->GetTypeName().c_str(); },
        "GetCategory", [](Object* object) -> std::string { return object->GetCategory().c_str(); },
        "GetContext", [](Object* object) -> Context* { return object->GetContext(); },
        "GetType", [](Object* object) { return object->GetType().Value(); }
    );

    // Serializable adds the attribute reflection channel.
    lua.new_usertype<Serializable>("Serializable",
        sol::no_constructor,
        sol::base_classes, sol::bases<Object>(),
        "GetAttribute", [&lua](Serializable* self, const char* name) -> sol::object {
            return self ? VariantToLua(lua, self->GetAttribute(name)) : sol::lua_nil;
        },
        "SetAttribute", [&lua](Serializable* self, const char* name, sol::object value) {
            if (self)
                self->SetAttribute(name, LuaToVariant(lua, value));
        },
        "GetAttributes", [&lua](Serializable* self) -> sol::object {
            if (!self)
                return sol::lua_nil;
            const auto* attributes = self->GetAttributes();
            if (!attributes)
                return sol::lua_nil;
            sol::table result = lua.create_table(static_cast<unsigned>(attributes->size()), 0);
            for (unsigned i = 0; i < attributes->size(); ++i)
            {
                sol::table info = lua.create_table();
                info["name"] = (*attributes)[i].name_.c_str();
                info["type"] = (*attributes)[i].type_;
                result[i + 1] = info;
            }
            return result;
        },
        "LoadXML", [](Serializable* self, const char* resourceName) {
            return self && self->LoadXML(resourceName);
        }
    );

    // Generic weak object reference with dynamic attribute access:
    //   ref.FarClip = 100.0  or  ref["Far Clip"] = 100.0
    lua.new_usertype<LuaObjectRef>("ObjectRef",
        sol::no_constructor,
        "Get", &LuaObjectRef::Get,
        "GetTypeName", [](LuaObjectRef& self) -> std::string {
            return self.Get() ? self.Get()->GetTypeName().c_str() : "";
        },
        "CastTo", [context, &lua](LuaObjectRef& self, const char* typeName) -> sol::object {
            Object* object = self.Get();
            if (!object || !object->IsInstanceOf(StringHash(typeName)))
                return sol::lua_nil;
            return WrapLuaObject(lua, object);
        },
        "GetAttribute", [&lua](LuaObjectRef& self, const char* name) -> sol::object {
            auto* serializable = dynamic_cast<Serializable*>(self.Get());
            return serializable ? VariantToLua(lua, serializable->GetAttribute(name)) : sol::lua_nil;
        },
        "SetAttribute", [&lua](LuaObjectRef& self, const char* name, sol::object value) {
            if (auto* serializable = dynamic_cast<Serializable*>(self.Get()))
                serializable->SetAttribute(name, LuaToVariant(lua, value));
        },
        "GetAttributes", [&lua](LuaObjectRef& self) -> sol::object {
            auto* serializable = dynamic_cast<Serializable*>(self.Get());
            if (!serializable)
                return sol::lua_nil;
            const auto* attributes = serializable->GetAttributes();
            if (!attributes)
                return sol::lua_nil;
            sol::table result = lua.create_table(static_cast<unsigned>(attributes->size()), 0);
            for (unsigned i = 0; i < attributes->size(); ++i)
            {
                sol::table info = lua.create_table();
                info["name"] = (*attributes)[i].name_.c_str();
                info["type"] = (*attributes)[i].type_;
                result[i + 1] = info;
            }
            return result;
        },
        sol::meta_function::to_string, [](LuaObjectRef& self) -> std::string {
            return self.Get() ? "ObjectRef: " + std::string(self.Get()->GetTypeName().c_str())
                              : "ObjectRef: <destroyed>";
        },
        sol::meta_function::index, [&lua](LuaObjectRef& self, const char* key) -> sol::object {
            auto* serializable = dynamic_cast<Serializable*>(self.Get());
            if (!serializable)
                return sol::lua_nil;
            const AttributeInfo* attr = FindAttribute(serializable, key);
            return attr ? VariantToLua(lua, serializable->GetAttribute(attr->name_)) : sol::lua_nil;
        },
        sol::meta_function::new_index, [&lua](LuaObjectRef& self, const char* key, sol::object value) {
            auto* serializable = dynamic_cast<Serializable*>(self.Get());
            if (!serializable)
                return;
            if (const AttributeInfo* attr = FindAttribute(serializable, key))
                serializable->SetAttribute(attr->name_, LuaToVariant(lua, value));
            else
                URHO3D_LOGERROR("ObjectRef: attribute '{}' not found on {}", key, serializable->GetTypeName());
        }
    );

    // Engine: run control and engine-wide queries.
    lua.new_usertype<Engine>("Engine",
        sol::no_constructor,
        sol::base_classes, sol::bases<Object>(),
        "Exit", &Engine::Exit,
        "DumpResources", &Engine::DumpResources,
        "IsHeadless", &Engine::IsHeadless
    );
    RegisterLuaObjectWrapper<Engine>();

    // Time: frame delta and total time queries.
    lua.new_usertype<Time>("Time",
        sol::no_constructor,
        sol::base_classes, sol::bases<Object>(),
        "GetTimeStep", &Time::GetTimeStep,
        "GetElapsedTime", &Time::GetElapsedTime,
        "GetFramesPerSecond", &Time::GetFramesPerSecond,
        "GetFrameNumber", &Time::GetFrameNumber,
        // Static helpers mirrored through lambdas (50_Sample2D seed,
        // 53_LANDiscovery expiry timestamps).
        "GetSystemTime", [](sol::this_state, Time*) { return Time::GetSystemTime(); },
        "GetTimeSinceEpoch", [](sol::this_state, Time*) { return Time::GetTimeSinceEpoch(); },
        "timeStep", sol::readonly_property(&Time::GetTimeStep)
    );
    RegisterLuaObjectWrapper<Time>();

    // Global subsystem accessor, the Lua counterpart of GetSubsystem<T>().
    lua.set_function("GetSubsystem", [context](sol::this_state s, const char* name) -> sol::object {
        if (!context)
            return sol::lua_nil;
        return WrapLuaObject(sol::state_view(s), context->GetSubsystem(StringHash(name)));
    });
}

} // namespace Urho3D

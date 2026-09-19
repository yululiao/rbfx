//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#include "../Urho3D/Precompiled.h"

#include "LuaBindings.h"

#include "../Urho3D/Core/Context.h"
#include "../Urho3D/Core/StringUtils.h"
#include "../Urho3D/Core/Timer.h"
#include "../Urho3D/Engine/Engine.h"
#include "../Urho3D/IO/Log.h"
#include "../Urho3D/Scene/Serializable.h"

#include <sol/sol.hpp>

#include <cstdint>
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

// Registry key holding the per-state object identity cache (a weak-valued table).
constexpr const char* const sObjectCacheKey = "rbfx.luaObjectCache";

/// Get (or lazily build) the per-state object identity cache. It is a table keyed
/// by an engine Object's address, mapping to the Lua wrapper already handed out for
/// that object, so re-wrapping the same live object returns the SAME Lua value
/// (stable `==`/rawequal and table-key identity, and no repeated userdata alloc).
/// The table is weak-VALUED (__mode="v"): an entry disappears as soon as Lua drops
/// the last reference to the wrapper, so the cache never keeps an object alive and
/// cannot leak. Living in the registry scopes it to one lua_State (no shared mutable
/// state across VMs/threads).
sol::table GetObjectCache(sol::state_view lua)
{
    lua_State* L = lua.lua_state();

    lua_pushstring(L, sObjectCacheKey);
    lua_gettable(L, LUA_REGISTRYINDEX);
    if (lua_istable(L, -1))
    {
        sol::table cache(sol::stack_reference(L, -1));
        lua_pop(L, 1);
        return cache;
    }
    lua_pop(L, 1); // drop the nil lookup

    // local cache = setmetatable({}, { __mode = "v" })
    lua_newtable(L);                       // cache
    lua_newtable(L);                       // mt
    lua_pushliteral(L, "__mode");
    lua_pushliteral(L, "v");
    lua_settable(L, -3);                   // mt.__mode = "v"
    lua_setmetatable(L, -2);               // setmetatable(cache, mt); pops mt

    // registry[sObjectCacheKey] = cache
    lua_pushstring(L, sObjectCacheKey);
    lua_pushvalue(L, -2);                  // push a copy of cache
    lua_settable(L, LUA_REGISTRYINDEX);    // pops key + value

    sol::table cache(sol::stack_reference(L, -1));
    lua_pop(L, 1);                         // pop cache
    return cache;
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

    // Fast path: exact name-hash match. One hash computation for the key, then
    // cheap integer compares -- no per-attribute allocation. This covers the
    // common case (the Lua key already equals the attribute name).
    const StringHash hash(key);
    for (const AttributeInfo& attr : *attributes)
    {
        if (attr.nameHash_ == hash)
            return &attr;
    }

    // Slow path: case/space-insensitive normalization, only reached on a hash
    // miss (e.g. key "FarClip" vs attribute name "Far Clip").
    const ea::string normalized = NormalizeAttributeName(key);
    for (const AttributeInfo& attr : *attributes)
    {
        if (NormalizeAttributeName(attr.name_) == normalized)
            return &attr;
    }
    return nullptr;
}

/// Report an attribute operation attempted through a generic ObjectRef whose
/// engine object has already been destroyed. The reflection channel otherwise
/// collapses to a silent nil read / dropped write, which hides "operate on a
/// removed object" bugs -- so surface it explicitly. Non-fatal by design (this
/// project does not force SOL_ALL_SAFETIES, so a thrown C++ exception could
/// escape into the engine and crash). Liveness probes (Get/CastTo/GetTypeName/
/// to_string) stay silent on purpose: nil/"" is their documented "is it gone?"
/// answer, not an error.
void ReportDeadObjectRef(const char* op)
{
    URHO3D_LOGERROR("Lua ObjectRef: '{}' used on a destroyed engine object; the operation is a no-op -- "
                    "re-fetch the object or guard with ref:Get() ~= nil", op);
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

    sol::table cache = GetObjectCache(lua);
    const lua_Integer key = static_cast<lua_Integer>(reinterpret_cast<std::uintptr_t>(object));

    sol::object cached = cache.get<sol::object>(key);
    if (cached.get_type() == sol::type::userdata)
    {
        // Registered-type wrappers hold a strong SharedPtr, so while the wrapper is
        // alive its object cannot be destroyed and the address cannot be recycled --
        // a hit is guaranteed to be this same live object. The generic LuaObjectRef
        // holds only a WeakPtr, so re-validate it still points at this object before
        // reusing it (a stale weak ref over a recycled address must not be returned).
        if (!cached.is<LuaObjectRef>() || cached.as<LuaObjectRef>().Get() == object)
            return cached;
    }

    auto& registry = GetCasterRegistry();
    const auto iter = registry.find(object->GetType());
    sol::object wrapper = (iter != registry.end())
        ? iter->second(lua, object)
        : sol::make_object(lua, LuaObjectRef{object});

    cache[key] = wrapper;
    return wrapper;
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
        // Prefer integral representation when the number is whole. Keep the
        // 32-bit range in VAR_INT, but land larger whole numbers on VAR_INT64
        // rather than silently degrading them to VAR_DOUBLE (which loses
        // precision past 2^53 and mis-types integer-only attributes).
        const double number = value.as<double>();
        if (number == std::floor(number))
        {
            if (number >= -2147483648.0 && number < 2147483648.0)
                return Variant(static_cast<int>(number));
            return Variant(static_cast<long long>(number));
        }
        return Variant(number);
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
        // A table that is not the { type, name } ResourceRef shorthand cannot be
        // mapped to a Variant. Report it instead of silently yielding EMPTY --
        // callers such as SetAttribute/SetVar would otherwise clear the value
        // with no clue why the write "did nothing".
        URHO3D_LOGERROR("LuaToVariant: cannot convert Lua table to Variant (expected a { type, name } ResourceRef shorthand); value dropped");
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
        if (value.is<IntVector3>())
            return Variant(value.as<IntVector3>());
        if (value.is<Rect>())
            return Variant(value.as<Rect>());
        if (value.is<LuaObjectRef>())
            return Variant(value.as<LuaObjectRef>().Get());
        if (value.is<Object*>())
            return Variant(value.as<Object*>());
        URHO3D_LOGERROR("LuaToVariant: unsupported userdata passed where a Variant was expected; value dropped");
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
    case VAR_INTVECTOR3: return sol::make_object(lua, value.GetIntVector3());
    case VAR_RECT: return sol::make_object(lua, value.GetRect());
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
        // Identity by underlying pointer so == agrees with every other Object wrapper.
        // (This vendored sol3 has no meta_function::hash, so userdata-as-table-key
        // identity is still unreliable -- see the gen-lua-binding skill contract.)
        // The wrapped handle is a WeakPtr, so a dead object collapses to null (two dead
        // refs compare equal) rather than dangling.
        sol::meta_function::equal_to, [](LuaObjectRef& a, LuaObjectRef& b) { return a.Get() == b.Get(); },
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
            Object* object = self.Get();
            if (!object)
            {
                ReportDeadObjectRef("GetAttribute");
                return sol::lua_nil;
            }
            auto* serializable = dynamic_cast<Serializable*>(object);
            return serializable ? VariantToLua(lua, serializable->GetAttribute(name)) : sol::lua_nil;
        },
        "SetAttribute", [&lua](LuaObjectRef& self, const char* name, sol::object value) {
            Object* object = self.Get();
            if (!object)
            {
                ReportDeadObjectRef("SetAttribute");
                return;
            }
            if (auto* serializable = dynamic_cast<Serializable*>(object))
                serializable->SetAttribute(name, LuaToVariant(lua, value));
        },
        "GetAttributes", [&lua](LuaObjectRef& self) -> sol::object {
            Object* object = self.Get();
            if (!object)
            {
                ReportDeadObjectRef("GetAttributes");
                return sol::lua_nil;
            }
            auto* serializable = dynamic_cast<Serializable*>(object);
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
            Object* object = self.Get();
            if (!object)
            {
                ReportDeadObjectRef("index");
                return sol::lua_nil;
            }
            auto* serializable = dynamic_cast<Serializable*>(object);
            if (!serializable)
                return sol::lua_nil;
            const AttributeInfo* attr = FindAttribute(serializable, key);
            return attr ? VariantToLua(lua, serializable->GetAttribute(attr->name_)) : sol::lua_nil;
        },
        sol::meta_function::new_index, [&lua](LuaObjectRef& self, const char* key, sol::object value) {
            Object* object = self.Get();
            if (!object)
            {
                ReportDeadObjectRef("new_index");
                return;
            }
            auto* serializable = dynamic_cast<Serializable*>(object);
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

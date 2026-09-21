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

// Registry key holding the per-state base-chain audit table: type hash ->
// { [1] = registration sequence number, [2..] = declared base type hashes }.
// Written by LuaBases<T, Bases...>::bases() (LuaBindHelpers.h) at registration
// time; checked by VerifyLuaBaseChains() once every binding module has run.
// Plain data (no weak semantics): it is tiny and dies with the state.
constexpr const char* const sBaseChainKey = "rbfx.luaBaseChainAudit";

// Registry key holding the per-state registration counter for the audit table
// (the final audit distinguishes "base never registered" from "base registered
// only after the derived type" via these sequence numbers).
constexpr const char* const sBaseChainSeqKey = "rbfx.luaBaseChainSeq";

// Registry key holding the per-state ancestry table: type hash -> array of
// the type's URHO3D_OBJECT ancestor hashes (minus the type itself). The
// startup audit demands that every ancestor that is itself a bound usertype
// also appears in the type's declared base chain.
constexpr const char* const sBaseHierarchyKey = "rbfx.luaBaseHierarchy";

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

/// Monotonic per-state registration counter for the base-chain audit.
lua_Integer NextBaseChainSequence(sol::state_view lua)
{
    lua_State* L = lua.lua_state();

    lua_pushstring(L, sBaseChainSeqKey);
    lua_gettable(L, LUA_REGISTRYINDEX);
    const lua_Integer next = (lua_isinteger(L, -1) ? lua_tointeger(L, -1) : 0) + 1;
    lua_pop(L, 1);

    lua_pushstring(L, sBaseChainSeqKey);
    lua_pushinteger(L, next);
    lua_settable(L, LUA_REGISTRYINDEX);
    return next;
}

/// Get (or lazily build) a plain table stored in the Lua registry under `key`.
sol::table GetRegistryTable(sol::state_view lua, const char* key)
{
    lua_State* L = lua.lua_state();

    lua_pushstring(L, key);
    lua_gettable(L, LUA_REGISTRYINDEX);
    if (lua_istable(L, -1))
    {
        sol::table table(sol::stack_reference(L, -1));
        lua_pop(L, 1);
        return table;
    }
    lua_pop(L, 1); // drop the nil lookup

    lua_newtable(L);
    lua_pushstring(L, key);
    lua_pushvalue(L, -2);
    lua_settable(L, LUA_REGISTRYINDEX);

    sol::table table(sol::stack_reference(L, -1));
    lua_pop(L, 1);
    return table;
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

sol::table GetLuaBaseChainTable(sol::state_view lua)
{
    return GetRegistryTable(lua, sBaseChainKey);
}

void RecordLuaBaseChain(sol::state_view lua, StringHash type,
    const ea::vector<StringHash>& bases, const ea::vector<StringHash>& hierarchy)
{
    sol::table audit = GetLuaBaseChainTable(lua);
    const lua_Integer key = static_cast<lua_Integer>(type.Value());

    if (const sol::optional<sol::table> existing = audit.get<sol::optional<sol::table>>(key))
    {
        // The same usertype registered twice in one state is tolerated only
        // while the declared chain is identical (a diverging second
        // registration would leave sol3's wiring and the audit out of sync).
        bool identical = existing->size() == bases.size() + 1;
        for (unsigned i = 0; identical && i < bases.size(); ++i)
        {
            const sol::optional<lua_Integer> recorded = existing->get<sol::optional<lua_Integer>>(i + 2);
            if (!recorded || static_cast<unsigned>(*recorded) != bases[i].Value())
                identical = false;
        }
        if (!identical)
            URHO3D_LOGERROR("Lua bindings: usertype '{}' registered twice with different base "
                            "chains; keep exactly one new_usertype registration per type",
                            type.ToDebugString().c_str());
        return;
    }

    sol::table chain = lua.create_table(static_cast<unsigned>(bases.size()) + 1, 0);
    chain[1] = NextBaseChainSequence(lua);
    for (unsigned i = 0; i < bases.size(); ++i)
        chain[i + 2] = static_cast<lua_Integer>(bases[i].Value());
    audit[key] = chain;

    // Full URHO3D_OBJECT ancestry (minus the type itself) for the startup
    // audit: every ancestor that ends up bound must also be declared.
    sol::table ancestors = lua.create_table(static_cast<unsigned>(hierarchy.size()), 0);
    for (unsigned i = 0; i < hierarchy.size(); ++i)
        ancestors[i + 1] = static_cast<lua_Integer>(hierarchy[i].Value());
    GetRegistryTable(lua, sBaseHierarchyKey)[key] = ancestors;
}

void VerifyLuaBaseChains(sol::state_view lua)
{
    sol::table audit = GetLuaBaseChainTable(lua);

    unsigned types = 0;
    unsigned violations = 0;
    for (auto& kv : audit)
    {
        ++types;
        const auto typeHash = static_cast<unsigned>(kv.first.as<lua_Integer>());
        const sol::table chain = kv.second.as<sol::table>();
        const lua_Integer seq = chain.get<lua_Integer>(1);
        const unsigned numBases = chain.size() - 1;
        for (unsigned i = 0; i < numBases; ++i)
        {
            const auto baseHash = static_cast<unsigned>(chain.get<lua_Integer>(i + 2));
            const sol::optional<sol::table> baseChain =
                audit.get<sol::optional<sol::table>>(static_cast<lua_Integer>(baseHash));
            if (!baseChain)
            {
                ++violations;
                URHO3D_LOGERROR("Lua bindings: usertype {} declares base {} that is never registered "
                                "in this Lua state -- the sol3 upcast to it is dead; bind the base "
                                "before the derived type or drop it from the chain",
                                StringHash{typeHash}.ToDebugString().c_str(),
                                StringHash{baseHash}.ToDebugString().c_str());
                continue;
            }
            if (baseChain->get<lua_Integer>(1) >= seq)
            {
                ++violations;
                URHO3D_LOGERROR("Lua bindings: usertype {} was registered before its base {} -- the "
                                "sol3 upcast to that base is dead; register base usertypes first",
                                StringHash{typeHash}.ToDebugString().c_str(),
                                StringHash{baseHash}.ToDebugString().c_str());
            }
        }
    }

    // Every BOUND ancestor of a registered type must appear in its declared
    // chain: sol3 exposes base members only through declared bases, so a
    // bound-but-undeclared ancestor's Lua members would be unreachable from
    // the derived type. Ancestors that are not bound themselves carry no Lua
    // members and may legitimately be skipped.
    sol::table hierarchies = GetRegistryTable(lua, sBaseHierarchyKey);
    for (auto& kv : hierarchies)
    {
        const auto typeHash = static_cast<unsigned>(kv.first.as<lua_Integer>());
        const sol::table ancestors = kv.second.as<sol::table>();
        const sol::optional<sol::table> declaredChain =
            audit.get<sol::optional<sol::table>>(static_cast<lua_Integer>(typeHash));
        if (!declaredChain)
            continue; // unreachable: the declared chain is always recorded first

        ea::unordered_set<unsigned> declared;
        for (unsigned i = 2; i <= declaredChain->size(); ++i)
            declared.insert(static_cast<unsigned>(declaredChain->get<lua_Integer>(i)));

        const unsigned numAncestors = ancestors.size();
        for (unsigned i = 1; i <= numAncestors; ++i)
        {
            const auto ancestorHash = static_cast<unsigned>(ancestors.get<lua_Integer>(i));
            if (declared.find(ancestorHash) != declared.end())
                continue;
            const sol::optional<sol::table> boundAncestor =
                audit.get<sol::optional<sol::table>>(static_cast<lua_Integer>(ancestorHash));
            if (!boundAncestor)
                continue;
            ++violations;
            URHO3D_LOGERROR("Lua bindings: bound ancestor {} of usertype {} is missing from its "
                            "declared base chain -- the ancestor's Lua members are unreachable "
                            "from {}; add the ancestor to LuaBases",
                            StringHash{ancestorHash}.ToDebugString().c_str(),
                            StringHash{typeHash}.ToDebugString().c_str(),
                            StringHash{typeHash}.ToDebugString().c_str());
        }
    }

    if (violations == 0)
        URHO3D_LOGINFO("Lua bindings: inheritance chains verified for {} usertypes", types);
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
    // Hand-written opener on purpose: the Lua name ("VariantMapView") differs
    // from the C++ class (LuaVariantMapView), so LUA_CLASS's #NAME would
    // stringify the wrong name (see LuaBindMacros.h contract).
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
    {
        using LUA_THIS = Object;
        LUA_CLASS(Object,
            sol::no_constructor,
            // Root of every chain: an empty base list (identical to sol3's default),
            // but routed through LuaBases so the audit table records the root and
            // derived types can verify their bases against it.
            sol::base_classes, LuaBases<Object>::bases(lua),
            sol::meta_function::equal_to, [](Object* a, Object* b) { return a == b; },
            "GetTypeName", [](Object* object) -> std::string { return object->GetTypeName().c_str(); },
            "GetCategory", [](Object* object) -> std::string { return object->GetCategory().c_str(); },
            "GetContext", [](Object* object) -> Context* { return object->GetContext(); },
            "GetType", [](Object* object) { return object->GetType().Value(); }
        );
    }

    // Serializable adds the attribute reflection channel.
    {
        using LUA_THIS = Serializable;
        LUA_CLASS(Serializable,
            sol::no_constructor,
            sol::base_classes, LuaBases<Serializable, Object>::bases(lua),
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
    }

    // Generic weak object reference with dynamic attribute access:
    //   ref.FarClip = 100.0  or  ref["Far Clip"] = 100.0
    // Hand-written opener on purpose: exposed as "ObjectRef", not the C++
    // class name (LuaObjectRef) -- same #NAME constraint as VariantMapView.
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
    {
        using LUA_THIS = Engine;
        LUA_CLASS(Engine,
            sol::no_constructor,
            sol::base_classes, LuaBases<Engine, Object>::bases(lua),
            "Exit", &Engine::Exit,
            "DumpResources", &Engine::DumpResources,
            "IsHeadless", &Engine::IsHeadless
        );
    }
    RegisterLuaObjectWrapper<Engine>();

    // Time: frame delta and total time queries.
    {
        using LUA_THIS = Time;
        LUA_CLASS(Time,
            sol::no_constructor
            LUA_BASES(Object)
            LUA_MEMBER_FUNC(GetTimeStep)
            LUA_MEMBER_FUNC(GetElapsedTime)
            LUA_MEMBER_FUNC(GetFramesPerSecond)
            LUA_MEMBER_FUNC(GetFrameNumber)
            // Static helpers mirrored through lambdas (50_Sample2D seed,
            // 53_LANDiscovery expiry timestamps).
            LUA_MEMBER_FUNC_RAW(GetSystemTime, [](sol::this_state, Time*) { return Time::GetSystemTime(); })
            LUA_MEMBER_FUNC_RAW(GetTimeSinceEpoch, [](sol::this_state, Time*) { return Time::GetTimeSinceEpoch(); })
        );
    }
    RegisterLuaObjectWrapper<Time>();

    // Global subsystem accessor, the Lua counterpart of GetSubsystem<T>().
    LUA_GLOBAL_FUNC(GetSubsystem, [context](sol::this_state s, const char* name) -> sol::object {
        if (!context)
            return sol::lua_nil;
        return WrapLuaObject(sol::state_view(s), context->GetSubsystem(StringHash(name)));
    });

    // Global log sinks. Lua scripts have no other channel into the engine log
    // file (print goes to a stdout that windowed apps do not have), so expose
    // the log levels as plain string functions. The sample-framework memory
    // probe (Source/LuaSamples/Framework.lua) reports through LogInfo/LogError.
    LUA_GLOBAL_FUNC(LogInfo, [](const char* message) { URHO3D_LOGINFO("{}", message); });
    LUA_GLOBAL_FUNC(LogWarning, [](const char* message) { URHO3D_LOGWARNING("{}", message); });
    LUA_GLOBAL_FUNC(LogError, [](const char* message) { URHO3D_LOGERROR("{}", message); });
}

} // namespace Urho3D

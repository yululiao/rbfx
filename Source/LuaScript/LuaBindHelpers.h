//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#pragma once

#include "LuaBindings.h"

#include "../Urho3D/IO/Log.h"

#include <sol/sol.hpp>

#include <type_traits>

namespace Urho3D
{

// Declarative binding helpers shared by the hand-written binding translation
// units. They exist to collapse the single most common piece of boilerplate in
// the surface -- the "null-check self then forward" lambda -- into an
// expression that still keeps the engine member pointer visible (so a changed
// engine signature is a compile error, which is the whole point of the
// hand-written binding surface).
//
// IMPORTANT (doc-generation coupling): the LuaLS stub generator
// (Source/Tools/ApiDocGen/generate_api_docs.py) reads the API surface straight
// out of the registration source text. Two invariants therefore hold for every
// helper introduced here:
//   1. The string key must stay a literal argument of new_usertype/set_function
//      (helpers only ever wrap the *value* that follows a key, never the key).
//   2. The generator recovers a `---@return` / `---@type` annotation by scanning
//      the bound value for an explicit `-> Type` trailing return. Because a
//      member pointer such as &T::M carries no trailing return, wrapping a
//      *getter* in NullChecked would silently drop its return annotation and
//      make the committed *.d.lua drift. NullChecked is therefore intentionally
//      limited to void / side-effect members, whose existing guard lambdas have
//      no trailing return to lose, so migrating them is byte-for-byte
//      doc-generation neutral. Getters keep their explicit `-> Type` lambda form.
//
// Source/Tools/LuaBindingCI/check_api_docs.ps1 is the mechanical guard for both
// invariants; run it after touching any binding registration.

/// Wrap a void (or ignored-return) engine member so the bound Lua method
/// null-checks its `self` object before dispatch, mirroring the previous
/// `[](T* self, Args... a) { if (self) self->M(a...); }` lambdas exactly.
/// Non-const overload.
template <class C, class... Args>
auto NullChecked(void (C::*member)(Args...))
{
    return [member](C* self, Args... args) {
        if (self)
            (self->*member)(args...);
    };
}

/// Const-member overload of NullChecked.
template <class C, class... Args>
auto NullChecked(void (C::*member)(Args...) const)
{
    return [member](C* self, Args... args) {
        if (self)
            (self->*member)(args...);
    };
}

/// Wrap a value-returning engine member, returning a default-constructed value
/// when `self` is null. Like the void overload this is doc-generation neutral
/// only when the method previously had no explicit `-> Type` guard lambda; it
/// is provided for methods that were previously guarded via `if (self)` around
/// a value and whose return type the docs do not annotate. Callers must keep
/// getters that the docs annotate in their literal lambda form.
template <class C, class R, class... Args>
auto NullCheckedValue(R (C::*member)(Args...), R fallback)
{
    return [member, fallback](C* self, Args... args) -> R {
        return self ? (self->*member)(args...) : fallback;
    };
}

/// Wrap an engine object through the identity cache, naming the Lua-facing
/// type for the doc generator. Routing an accessor through the cache means
/// its lambda returns sol::object (the cached wrapper), which has no
/// nameable trailing return; the stub generator instead reads the annotation
/// off WrapLuaObjectAs<X>(...) inside the body, so a getter's ---@type / ---@return
/// hint survives migrating from a direct-push `-> X*` / `-> SharedPtr<X>`
/// return to the cached path (rawequal and table-key identity come for free).
template <typename T>
sol::object WrapLuaObjectAs(sol::state_view lua, Object* object)
{
    return WrapLuaObject(lua, object);
}

// ---------------------------------------------------------------------------
// Inheritance chain (binding rule 11, machine-checked)
// ---------------------------------------------------------------------------
//
// sol3 upcasts a usertype value ONLY through the bases its registration
// declares, and only when the base's own usertype was registered first. A
// dropped link or a swapped registration order does not fail the build or
// print anything: the upcast path just silently dies (e.g. an event sender
// or GetSubsystem result no longer reaches the expected base). LuaBases
// replaces sol::bases in every new_usertype call and converts both halves of
// the invariant into machine checks:
//
//   * compile time -- every declared base must really be a base of the type
//     (a fabricated entry would compile fine with sol::bases and silently
//     no-op the upcast), and the chain must reach Object (Object*-typed
//     paths such as event senders need it);
//   * registration time -- bases(lua) records the chain in the per-state
//     audit table and reports a base that is not registered yet (a loud
//     URHO3D_LOGERROR instead of a dead cast path);
//   * end of startup -- VerifyLuaBaseChains() (wired in LuaVM.cpp) walks the
//     finished graph: every declared base must exist and must have been
//     registered before the derived type, and every BOUND ancestor of a type
//     (one with its own registration) must appear in its declared chain --
//     sol3 exposes base members only through declared bases, so a bound-but-
//     undeclared ancestor's Lua members would be unreachable. Ancestors that
//     are not bound themselves carry no Lua members and may be skipped.
//
// Doc-generation coupling (invariant 3): call it as
//     sol::base_classes, LuaBases<T, Bases...>::bases(lua)
// The first template argument repeats the registered type; the stub generator
// reads the whole template list back out of the call text and filters the
// self-entry, so the emitted bases are exactly the declared ones.

/// Compile-time resolve a type's URHO3D_OBJECT TypeId. Every class created
/// through URHO3D_OBJECT(typeName, base) carries a static constexpr TypeId;
/// the root Object is NOT created through the macro and has none, so it
/// resolves to an empty hash -- matching nothing, exactly like its empty
/// TypeHierarchy.
template <typename T, typename = void>
struct LuaTypeIdOf
{
    static constexpr StringHash value{};
};

template <typename T>
struct LuaTypeIdOf<T, std::void_t<decltype(T::TypeId)>>
{
    static constexpr StringHash value = T::TypeId;
};

/// Compile-time resolve a type's registered name for diagnostics.
/// URHO3D_OBJECT types expose GetTypeNameStatic(); the root Object does not.
template <typename T, typename = void>
struct LuaTypeNameOf
{
    static ea::string Get() { return "Object"; }
};

template <typename T>
struct LuaTypeNameOf<T, std::void_t<decltype(&T::GetTypeNameStatic)>>
{
    static ea::string Get() { return T::GetTypeNameStatic(); }
};

/// Compile-time detect a URHO3D_OBJECT type: it carries the static constexpr
/// TypeHierarchy that the runtime audit records.
template <typename T, typename = void>
struct LuaHasTypeHierarchy : std::false_type
{
};

template <typename T>
struct LuaHasTypeHierarchy<T, std::void_t<decltype(T::TypeHierarchy)>> : std::true_type
{
};

/// Drop-in replacement for sol::bases<...>() in sol::base_classes arguments.
/// Returns the same base list sol::bases would, so the registration's runtime
/// behavior is unchanged; passing the registering state lets the call audit
/// registration order (see the block comment above).
template <typename T, typename... Bases>
class LuaBases
{
    static_assert(LuaHasTypeHierarchy<T>::value,
        "LuaBases: T is not a URHO3D_OBJECT type. The audited-chain machinery "
        "only applies to Object types; use plain sol::bases for "
        "RefCounted-only hierarchies (e.g. SoundStream).");
    static_assert((std::is_base_of_v<Bases, T> && ...),
        "LuaBases: a declared base is not actually a base of this type. The "
        "sol3 upcast to it would be a silent no-op; fix the base list.");
    static_assert(sizeof...(Bases) == 0 || (std::is_same_v<Bases, Object> || ...),
        "LuaBases: the chain must reach Object -- Object*-typed paths (event "
        "senders, GetSubsystem casts) need the full chain.");

public:
    static sol::base_list<Bases...> bases(sol::state_view lua)
    {
        sol::table audit = GetLuaBaseChainTable(lua);
        const ea::string typeName = LuaTypeNameOf<T>::Get();
        (CheckBaseRegistered<Bases>(audit, typeName), ...);

        // Record the type's full URHO3D_OBJECT ancestry (minus itself) next
        // to the declared chain: the startup audit uses it to demand that
        // every ancestor which ends up BOUND is also declared, since sol3
        // only exposes base members through declared bases.
        const auto& hierarchy = T::TypeHierarchy;
        ea::vector<StringHash> ancestors;
        for (size_t i = 1; i < hierarchy.size(); ++i)
            ancestors.push_back(hierarchy[i]);

        RecordLuaBaseChain(lua, LuaTypeIdOf<T>::value, {LuaTypeIdOf<Bases>::value...}, ancestors);
        return {};
    }

private:
    /// Report a declared base that is not a registered usertype in this state
    /// (yet). Bases must register before the types derived from them, across
    /// modules and within a module alike.
    template <typename Base>
    static void CheckBaseRegistered(sol::table& audit, const ea::string& derivedName)
    {
        const lua_Integer key = static_cast<lua_Integer>(LuaTypeIdOf<Base>::value.Value());
        if (audit.get<sol::optional<sol::object>>(key))
            return;
        URHO3D_LOGERROR("Lua bindings: base '{}' of '{}' is not a registered usertype in this Lua "
                        "state yet -- sol3 upcasts from '{}' to it stay dead until the base's own "
                        "new_usertype call runs. Register every base before the types derived from "
                        "it (module order in LuaVM::RegisterEngineBindings and the order inside "
                        "each binding TU both matter).",
                        LuaTypeNameOf<Base>::Get().c_str(), derivedName.c_str(), derivedName.c_str());
    }
};

} // namespace Urho3D

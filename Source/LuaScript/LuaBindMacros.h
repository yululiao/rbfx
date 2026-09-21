//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#pragma once

#include "LuaBindHelpers.h"

// ===========================================================================
// One-line binding macros -- sugar over the sol3 call-site
// ===========================================================================
//
// These macros collapse the recurring registration patterns into one line
// per member WITHOUT bypassing any machine contract:
//
//   * LUA_BASES goes through LuaBases<...>::bases(lua), so the three-layer
//     inheritance audit (compile-time static_asserts, registration-time
//     base-already-registered check, startup VerifyLuaBaseChains) runs
//     exactly as with the hand-written form.
//   * LUA_MEMBER_PROP_OBJ_R / LUA_MEMBER_FUNC_OBJ route through
//     WrapLuaObjectAs<T>, so the per-state identity cache (rawequal /
//     table-key semantics) holds.
//
// ---------------------------------------------------------------------------
// Naming: two axes, after ejoy2dx's tolua.h (lua_class / lua_member_func /
// lua_member_prop_f / lua_global_func / lua_const)
// ---------------------------------------------------------------------------
// The prefix is LUA_ (verified collision-free against the Lua C API's own
// LUA_* defines). The rest of the name states WHAT is bound, then HOW:
//
//     LUA_<subject>_<shape>
//
//   subject:  CLASS | BASES | MEMBER_FUNC | MEMBER_PROP | MEMBER_CONST |
//             GLOBAL_FUNC
//   shape:    (none) = the plain direct form
//             _RAW   = explicit value, escape hatch (semantics carried by
//                      the value's head token, see the bucket contracts)
//             _RET   = doc generator re-synthesizes ---@return from RET
//             _ENUM  = int -> enum adapter lambda
//             _OBJ   = return value routed through the identity cache
//             _F/_FR = property from a getter/setter pair / getter only
//             _OVERLOAD = sol::overload set
//
// ---------------------------------------------------------------------------
// Call-site conventions: LEADING COMMA + LUA_THIS
// ---------------------------------------------------------------------------
// Every list macro below expands to ` , "key", value` -- it carries its own
// separator. No macro names the usertype class: each registration opens a
// block that binds it once via a scoped alias, and the macros read it:
//
//     {                                           // block: LUA_THIS scope
//         using LUA_THIS = RigidBody;
//         LUA_CLASS(RigidBody, sol::no_constructor
//             LUA_BASES(Component, Serializable, Object)
//             LUA_MEMBER_FUNC(SetMass)
//             LUA_MEMBER_FUNC_ENUM(SetCollisionEventMode, CollisionEventMode)
//             LUA_MEMBER_PROP_F(mass, float, GetMass, SetMass)
//             LUA_MEMBER_FUNC_OVERLOAD(ApplyForce,
//                 LUA_CAST(ApplyForce, void, const Vector3&),
//                 LUA_CAST(ApplyForce, void, const Vector3&, const Vector3&))
//             LUA_MEMBER_FUNC_RAW(DrawDebugGeometry, [](RigidBody* body) { ... })
//         );
//     }
//
// The block is REQUIRED: a using-alias cannot be redeclared in the same
// scope, so each usertype registration owns one. Everything inside a
// registration (the new_usertype call, RegisterLuaObjectWrapper, adjacent
// table setup) belongs to the same block.
//
// Because macros bring their own comma, the LAST member needs no special
// casing and a forgotten separator is structurally impossible. Hand-written
// entries that the _RAW forms cannot swallow -- values containing TOP-LEVEL
// commas, e.g. a static_cast whose member-pointer type has several
// parameters (macro arguments split on those commas) -- are written
// literally with a leading comma, as the "ApplyForce" entry above.
//
// ---------------------------------------------------------------------------
// Per-macro contract
// ---------------------------------------------------------------------------
// LUA_CLASS(NAME, ...)
//     The registration opener: expands to lua.new_usertype<NAME>("NAME",
//     ...) -- NAME is stringized, so the type name is written once. The
//     block's `using LUA_THIS = NAME;` must agree (the block opener repeats
//     the name by necessity). Sole constraint: NAME must be the exact Lua
//     type name -- a registration whose Lua name differs from the C++ class
//     (LuaObjectRef exposed as "ObjectRef") stays literal, since #NAME
//     would stringify the wrong name. Constructor forms pass through fine:
//     the preprocessor splits sol::constructors<A(), B(float, float)> on
//     its template-argument commas, but __VA_ARGS__ re-joins the pieces
//     verbatim (the doc generator's splitter/rejoiner mirrors that), so the
//     expansion is text-identical. Only REARRANGING macros (LUA_CAST:
//     arguments injected at several positions) demand comma-free tokens.
//
// LUA_BASES(base, ...)
//     Expands to the audited base chain for LUA_THIS. At least one base
//     must be listed; the chain must reach Object (LuaBases static_asserts
//     enforce the rest). Refers to the registration function's `sol::state&
//     lua` parameter by name -- every RegisterXxxBindings uses that name.
//
// LUA_MEMBER_FUNC(MEMBER)
//     Plain member function pointer under its own name. MEMBER must be
//     directly nameable (no overloads, no casts needed) and the Lua key
//     equals the C++ member name -- anything else is an escape hatch.
//
// LUA_MEMBER_FUNC_RET(MEMBER, RET)
//     LUA_MEMBER_FUNC for a getter whose stub carries ---@return. The C++
//     expansion is the same bare member pointer; RET is consumed ONLY by
//     the doc generator, which re-synthesizes the `-> RET` trailing return
//     the stub scanner reads. Retires the pure-forwarding arrow lambda
//     pattern (`return self ? self->M() : Default;` -- no-arg calls only).
//     Members with parameters keep their lambda when it adapts anything:
//     default arguments (Pitch(angle) hides TransformSpace),
//     optional-unwrapping, or type shims -- the lambda IS the adaptation.
//
// LUA_MEMBER_FUNC_ENUM(MEMBER, ENUM)
//     `void MEMBER(ENUM)` adapter: accepts the enum value as a Lua number
//     and static_casts it before dispatch, null-checking self. Only for
//     single-signature members taking exactly one enum value.
//
// LUA_MEMBER_FUNC_OBJ(NAME, RET, GETTER)
//     Method form whose return value routes through the identity cache:
//     `WrapLuaObjectAs<RET>(state, LUA_THIS::GETTER())`. RET must be an
//     Object-derived usertype (often NOT LUA_THIS -- Component::GetNode
//     returns Node) and GETTER must return a raw pointer to it.
//
// LUA_MEMBER_PROP_OBJ_R(NAME, RET, GETTER)
//     Readonly-property twin of LUA_MEMBER_FUNC_OBJ (the `obj.prop` form).
//
// LUA_MEMBER_PROP_F(KEY, TYPE, GETTER, SETTER)
//     ejoy lua_member_prop_f: ONE line registers the read-write property KEY
//     plus its GETTER and SETTER methods (three entries). The C++ expansion
//     binds bare member pointers -- `if (self)` guards are unreachable
//     defensive code for registered types (contract rule 12a), and member
//     pointers keep the engine signature compile-checked. TYPE is consumed
//     ONLY by the doc generator: the macro hides the `-> Type` trailing
//     return the stub scanner reads, so the generator re-synthesizes it from
//     TYPE. Use it where the hand-written form used arrow lambdas; clusters
//     whose stubs carry NO annotations (member-pointer originals) stay
//     literal so parity does not drift.
//
// LUA_MEMBER_PROP_FR(KEY, TYPE, GETTER)
//     ejoy lua_member_prop_fr: readonly property only. No method entries --
//     readonly clusters in this codebase register no paired getter methods.
//     TYPE feeds the doc generator exactly as in LUA_MEMBER_PROP_F.
//
// LUA_MEMBER_FUNC_RAW(KEY, VALUE)
//     Escape hatch for METHOD-shaped pairs: KEY is an identifier stringized
//     like every other macro in this family; VALUE is a lambda, a member
//     function pointer (possibly inherited: `&Component::SetEnabled`), or a
//     static_cast expression. VALUE may contain commas only INSIDE
//     parentheses -- the preprocessor's argument splitter is purely textual
//     and does NOT respect braces (a comma at brace level, e.g. `int x = 0,
//     y = 0;` inside a raw lambda body, splits the macro; write the whole
//     entry literally with a leading comma instead). The doc generator
//     rejects a VALUE that starts with sol::property / sol::readonly_property
//     / sol::var -- those belong to the prop/const buckets below, and the
//     misfiling fails the parity gate.
//
// LUA_MEMBER_PROP_RAW(KEY, VALUE)
//     The PROPERTY-shaped escape hatch: VALUE is a sol::property /
//     sol::readonly_property wrapper (adapting lambdas: `rotation2D`'s
//     Quaternion<->float pair) or a bare data-member pointer
//     (`&Vector2::x_`, the trailing underscore marks rbfx data members).
//     Same comma rules as LUA_MEMBER_FUNC_RAW.
//
// LUA_MEMBER_CONST(KEY, VALUE)
//     ejoy lua_const: class-level constant (Vector2.ZERO, Quaternion.
//     IDENTITY, ...). The macro wraps VALUE in sol::var() itself, so the
//     call site passes the bare constant. VALUE must be comma-free at the
//     top level (constants are flat).
//
// LUA_GLOBAL_FUNC(NAME, ...)
//     ejoy lua_global_func: a global function on the state, expanding to
//     `lua.set_function(#NAME, __VA_ARGS__)`. Only usable where the
//     registration function's `lua` parameter is in scope (every binding
//     TU); VM-bootstrap registrations that capture `this` off a different
//     state handle (LuaVM.cpp's SubscribeToEvent family) stay hand-written
//     -- they carry a guard comment.
//
// LUA_ENUM_TABLE(NAME, "KEY", VALUE, ...)
//     Standalone statement (outside usertype calls): creates the named
//     global enum table with the given string-keyed pairs in one line.
//     NAME must be a valid identifier (it is stringized).
//
// LUA_META(SELECTOR, VALUE)
//     The sol::meta_function twin of the RAW forms: expands to
//     `, sol::meta_function::SELECTOR, VALUE` (equal_to, to_string,
//     multiplication, ...). Selector keys cannot be stringized, so they
//     get a dedicated macro instead of a second key flavor.
//
// LUA_CAST(MEMBER, RET, ...) / LUA_CAST_C(... const)
//     static_cast sugar for overload disambiguation against LUA_THIS:
//     LUA_CAST(SetOrthoSize, void, float) is `static_cast<void
//     (LUA_THIS::*)(float)>(&LUA_THIS::SetOrthoSize)`. RET and the
//     parameter types are token runs; none of them may contain a top-level
//     comma (a templated parameter type such as `ea::pair<int, int>` splits
//     the macro -- use a literal static_cast there). _C selects a const
//     member function.
//
// ---------------------------------------------------------------------------
// Doc-generator contract
// ---------------------------------------------------------------------------
// Source/Tools/ApiDocGen/generate_api_docs.py recognizes this macro family
// at token level and emits exactly the same stub entries as the equivalent
// hand-written form. It also ENFORCES the bucket taxonomy: LUA_MEMBER_PROP_RAW
// must carry a property-shaped value head, LUA_MEMBER_FUNC_RAW must not, and
// a misfiled call makes check_api_docs.ps1 fail loudly. The parity guard
// must stay green after any change to a call site or to this header.
// ===========================================================================

#define LUA_CLASS(NAME, ...) \
    lua.new_usertype<NAME>(#NAME, __VA_ARGS__)

#define LUA_BASES(...) \
    , sol::base_classes, LuaBases<LUA_THIS, __VA_ARGS__>::bases(lua)

#define LUA_MEMBER_FUNC(MEMBER) \
    , #MEMBER, &LUA_THIS::MEMBER

#define LUA_MEMBER_FUNC_RET(MEMBER, RET) \
    , #MEMBER, &LUA_THIS::MEMBER

#define LUA_MEMBER_FUNC_ENUM(MEMBER, ENUM) \
    , #MEMBER, [](LUA_THIS* self, int value) { \
        if (self) \
            self->MEMBER(static_cast<ENUM>(value)); \
    }

#define LUA_MEMBER_FUNC_OBJ(NAME, RET, GETTER) \
    , #NAME, [](LUA_THIS* self, sol::this_state s) -> sol::object { \
        return self \
            ? WrapLuaObjectAs<RET>(sol::state_view(s), self->GETTER()) \
            : sol::lua_nil; \
    }

#define LUA_MEMBER_PROP_OBJ_R(NAME, RET, GETTER) \
    , #NAME, sol::readonly_property( \
        [](LUA_THIS* self, sol::this_state s) -> sol::object { \
            return self \
                ? WrapLuaObjectAs<RET>(sol::state_view(s), self->GETTER()) \
                : sol::lua_nil; \
        })

#define LUA_MEMBER_FUNC_OVERLOAD(MEMBER, ...) \
    , #MEMBER, sol::overload(__VA_ARGS__)

#define LUA_MEMBER_FUNC_RAW(KEY, VALUE) \
    , #KEY, VALUE

#define LUA_MEMBER_PROP_F(KEY, TYPE, GETTER, SETTER) \
    , #KEY, sol::property(&LUA_THIS::GETTER, &LUA_THIS::SETTER) \
    , #GETTER, &LUA_THIS::GETTER \
    , #SETTER, &LUA_THIS::SETTER

#define LUA_MEMBER_PROP_FR(KEY, TYPE, GETTER) \
    , #KEY, sol::readonly_property(&LUA_THIS::GETTER)

#define LUA_MEMBER_PROP_RAW(KEY, VALUE) \
    , #KEY, VALUE

#define LUA_MEMBER_CONST(KEY, VALUE) \
    , #KEY, sol::var(VALUE)

#define LUA_GLOBAL_FUNC(NAME, ...) \
    lua.set_function(#NAME, __VA_ARGS__)

#define LUA_ENUM_TABLE(NAME, ...) \
    lua.create_named_table(#NAME, __VA_ARGS__)

#define LUA_META(SELECTOR, VALUE) \
    , sol::meta_function::SELECTOR, VALUE

#define LUA_CAST(MEMBER, RET, ...) \
    static_cast<RET (LUA_THIS::*)(__VA_ARGS__)>(&LUA_THIS::MEMBER)

#define LUA_CAST_C(MEMBER, RET, ...) \
    static_cast<RET (LUA_THIS::*)(__VA_ARGS__) const>(&LUA_THIS::MEMBER)

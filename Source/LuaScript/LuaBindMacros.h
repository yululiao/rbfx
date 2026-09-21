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
//   * RBFX_BASES goes through LuaBases<...>::bases(lua), so the three-layer
//     inheritance audit (compile-time static_asserts, registration-time
//     base-already-registered check, startup VerifyLuaBaseChains) runs
//     exactly as with the hand-written form.
//   * RBFX_OBJ_R / RBFX_OBJ_M route through WrapLuaObjectAs<T>, so the
//     per-state identity cache (rawequal / table-key semantics) holds.
//
// ---------------------------------------------------------------------------
// Call-site conventions: LEADING COMMA + RBFX_THIS
// ---------------------------------------------------------------------------
// Every list macro below expands to ` , "key", value` -- it carries its own
// separator. No macro names the usertype class: each registration opens a
// block that binds it once via a scoped alias, and the macros read it:
//
//     {                                           // block: RBFX_THIS scope
//         using RBFX_THIS = RigidBody;
//         RBFX_USERTYPE(RigidBody, sol::no_constructor
//             RBFX_BASES(Component, Serializable, Object)
//             RBFX_M(SetMass)
//             RBFX_M_ENUM(SetCollisionEventMode, CollisionEventMode)
//             RBFX_PROP(mass, float, GetMass, SetMass)
//             RBFX_OVERLOAD(ApplyForce,
//                 RBFX_CAST(ApplyForce, void, const Vector3&),
//                 RBFX_CAST(ApplyForce, void, const Vector3&, const Vector3&))
//             RBFX_RAW(DrawDebugGeometry, [](RigidBody* body) { ... })
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
// entries that RBFX_RAW cannot swallow -- values containing TOP-LEVEL
// commas, e.g. a static_cast whose member-pointer type has several
// parameters (macro arguments split on those commas) -- are written
// literally with a leading comma, as the "ApplyForce" entry above.
//
// ---------------------------------------------------------------------------
// Per-macro contract
// ---------------------------------------------------------------------------
// RBFX_USERTYPE(NAME, ...)
//     The registration opener: expands to lua.new_usertype<NAME>("NAME",
//     ...) -- NAME is stringized, so the type name is written once. The
//     block's `using RBFX_THIS = NAME;` must agree (the block opener repeats
//     the name by necessity). Sole constraint: NAME must be the exact Lua
//     type name -- a registration whose Lua name differs from the C++ class
//     (LuaObjectRef exposed as "ObjectRef") stays literal, since #NAME
//     would stringify the wrong name. Constructor forms pass through fine:
//     the preprocessor splits sol::constructors<A(), B(float, float)> on
//     its template-argument commas, but __VA_ARGS__ re-joins the pieces
//     verbatim (the doc generator's splitter/rejoiner mirrors that), so the
//     expansion is text-identical. Only REARRANGING macros (RBFX_CAST:
//     arguments injected at several positions) demand comma-free tokens.
//
// RBFX_BASES(base, ...)
//     Expands to the audited base chain for RBFX_THIS. At least one base
//     must be listed; the chain must reach Object (LuaBases static_asserts
//     enforce the rest). Refers to the registration function's `sol::state&
//     lua` parameter by name -- every RegisterXxxBindings uses that name.
//
// RBFX_M(MEMBER)
//     Plain member function/variable pointer under its own name. MEMBER
//     must be directly nameable (no overloads, no casts needed) and the
//     Lua key equals the C++ member name -- anything else is an escape
//     hatch.
//
// RBFX_M_RET(MEMBER, RET)
//     RBFX_M for a getter whose stub carries ---@return. The C++ expansion
//     is the same bare member pointer; RET is consumed ONLY by the doc
//     generator, which re-synthesizes the `-> RET` trailing return the stub
//     scanner reads. Retires the pure-forwarding arrow lambda pattern
//     (`return self ? self->M() : Default;` -- no-arg calls only). Members
//     with parameters keep their lambda when it adapts anything: default
//     arguments (Pitch(angle) hides TransformSpace), optional-unwrapping,
//     or type shims -- the lambda IS the adaptation, not boilerplate.
//
// RBFX_M_ENUM(MEMBER, ENUM)
//     `void MEMBER(ENUM)` adapter: accepts the enum value as a Lua number
//     and static_casts it before dispatch, null-checking self. Only for
//     single-signature members taking exactly one enum value.
//
// RBFX_OBJ_R(NAME, RET, GETTER)
//     Cached read-only property backed by a null-argument object getter:
//     `WrapLuaObjectAs<RET>(state, RBFX_THIS::GETTER())`. RET must be an
//     Object-derived usertype (often NOT RBFX_THIS -- Component::GetNode
//     returns Node) and GETTER must return a raw pointer to it.
//
// RBFX_OBJ_M(NAME, RET, GETTER)
//     Method form of RBFX_OBJ_R (no readonly_property wrapper), for
//     `GetNode`-style accessors.
//
// RBFX_PROP(KEY, TYPE, GETTER, SETTER)
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
// RBFX_PROP_R(KEY, TYPE, GETTER)
//     ejoy lua_member_prop_fr: readonly property only. No method entries --
//     readonly clusters in this codebase register no paired getter methods.
//     TYPE feeds the doc generator exactly as in RBFX_PROP.
//
// RBFX_OVERLOAD(MEMBER, ...)
//     sol::overload entry under the leading-comma convention. MEMBER is an
//     identifier stringized like RBFX_M's member argument; the overloads are
//     passed through verbatim (RBFX_CAST forms, lambdas, or a mix).
//     sol::meta_function selectors cannot be stringized -- bind those with
//     RBFX_META(multiplication, sol::overload(...)).
//
// RBFX_CAST(MEMBER, RET, ...) / RBFX_CAST_C(... const)
//     static_cast sugar for overload disambiguation against RBFX_THIS:
//     RBFX_CAST(SetOrthoSize, void, float) is `static_cast<void
//     (RBFX_THIS::*)(float)>(&RBFX_THIS::SetOrthoSize)`. RET and the
//     parameter types are token runs; none of them may contain a top-level
//     comma (a templated parameter type such as `ea::pair<int, int>` splits
//     the macro -- use a literal static_cast there). _C selects a const
//     member function.
//
// RBFX_ENUM_TABLE(NAME, "KEY", VALUE, ...)
//     Standalone statement (outside usertype calls): creates the named
//     global enum table with the given string-keyed pairs in one line.
//     NAME must be a valid identifier (it is stringized).
//
// RBFX_RAW(KEY, VALUE)
//     Escape hatch for pairs that fit on one macro argument list: KEY is an
//     identifier stringized like every other macro in this family; VALUE
//     may contain commas only INSIDE parentheses. The preprocessor's
//     argument splitter is purely textual and does NOT respect braces: a
//     comma at brace level (e.g. `int x = 0, y = 0;` inside a raw lambda
//     body) splits the macro. If VALUE has such a comma, write the whole
//     entry literally with a leading comma instead.
//
// RBFX_META(SELECTOR, VALUE)
//     The sol::meta_function twin of RBFX_RAW: expands to
//     `, sol::meta_function::SELECTOR, VALUE` (equal_to, to_string,
//     multiplication, ...). Selector keys cannot be stringized, so they
//     get a dedicated macro instead of a second key flavor on RBFX_RAW.
//
// ---------------------------------------------------------------------------
// Doc-generator contract
// ---------------------------------------------------------------------------
// Source/Tools/ApiDocGen/generate_api_docs.py recognizes this macro family
// at token level and emits exactly the same stub entries as the equivalent
// hand-written form. The parity guard (check_api_docs.ps1) must stay green
// after any change to a call site or to this header.
// ===========================================================================

#define RBFX_USERTYPE(NAME, ...) \
    lua.new_usertype<NAME>(#NAME, __VA_ARGS__)

#define RBFX_BASES(...) \
    , sol::base_classes, LuaBases<RBFX_THIS, __VA_ARGS__>::bases(lua)

#define RBFX_M(MEMBER) \
    , #MEMBER, &RBFX_THIS::MEMBER

#define RBFX_M_RET(MEMBER, RET) \
    , #MEMBER, &RBFX_THIS::MEMBER

#define RBFX_M_ENUM(MEMBER, ENUM) \
    , #MEMBER, [](RBFX_THIS* self, int value) { \
        if (self) \
            self->MEMBER(static_cast<ENUM>(value)); \
    }

#define RBFX_OBJ_R(NAME, RET, GETTER) \
    , #NAME, sol::readonly_property( \
        [](RBFX_THIS* self, sol::this_state s) -> sol::object { \
            return self \
                ? WrapLuaObjectAs<RET>(sol::state_view(s), self->GETTER()) \
                : sol::lua_nil; \
        })

#define RBFX_OBJ_M(NAME, RET, GETTER) \
    , #NAME, [](RBFX_THIS* self, sol::this_state s) -> sol::object { \
        return self \
            ? WrapLuaObjectAs<RET>(sol::state_view(s), self->GETTER()) \
            : sol::lua_nil; \
    }

#define RBFX_ENUM_TABLE(NAME, ...) \
    lua.create_named_table(#NAME, __VA_ARGS__)

#define RBFX_RAW(KEY, VALUE) \
    , #KEY, VALUE

#define RBFX_META(SELECTOR, VALUE) \
    , sol::meta_function::SELECTOR, VALUE

#define RBFX_PROP(KEY, TYPE, GETTER, SETTER) \
    , #KEY, sol::property(&RBFX_THIS::GETTER, &RBFX_THIS::SETTER) \
    , #GETTER, &RBFX_THIS::GETTER \
    , #SETTER, &RBFX_THIS::SETTER

#define RBFX_PROP_R(KEY, TYPE, GETTER) \
    , #KEY, sol::readonly_property(&RBFX_THIS::GETTER)

#define RBFX_OVERLOAD(MEMBER, ...) \
    , #MEMBER, sol::overload(__VA_ARGS__)

#define RBFX_CAST(MEMBER, RET, ...) \
    static_cast<RET (RBFX_THIS::*)(__VA_ARGS__)>(&RBFX_THIS::MEMBER)

#define RBFX_CAST_C(MEMBER, RET, ...) \
    static_cast<RET (RBFX_THIS::*)(__VA_ARGS__) const>(&RBFX_THIS::MEMBER)

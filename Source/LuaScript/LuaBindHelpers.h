//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#pragma once

#include "LuaBindings.h"

#include <sol/sol.hpp>

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

} // namespace Urho3D

//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#pragma once

#include "../Container/Ptr.h"

#include <sol/unique_usertype_traits.hpp>

namespace sol
{

class state;

// Teach sol3 to treat Urho3D::SharedPtr<T> as a smart pointer wrapper so that
// functions returning SharedPtr<Node> behave like raw Node* in Lua.
template <typename T, typename RefCountedType>
struct unique_usertype_traits<Urho3D::SharedPtr<T, RefCountedType>>
{
    using pointer = T*;
    using element_type = T;
    using actual_type = Urho3D::SharedPtr<T, RefCountedType>;

    template <typename X>
    using rebind_actual_type = Urho3D::SharedPtr<X, RefCountedType>;

    static bool is_null(lua_State*, const Urho3D::SharedPtr<T, RefCountedType>& p) noexcept
    {
        return p.Get() == nullptr;
    }

    static pointer get(lua_State*, const Urho3D::SharedPtr<T, RefCountedType>& p) noexcept
    {
        return p.Get();
    }
};

} // namespace sol

namespace Urho3D
{

/// Register Vector2 math type to Lua.
void RegisterVector2Bindings(sol::state& lua);
/// Register Vector3 math type to Lua.
void RegisterVector3Bindings(sol::state& lua);
/// Register Quaternion math type to Lua.
void RegisterQuaternionBindings(sol::state& lua);
/// Register Color math type to Lua.
void RegisterColorBindings(sol::state& lua);
/// Register Node, Scene and component types to Lua.
void RegisterNodeBindings(sol::state& lua);

} // namespace Urho3D

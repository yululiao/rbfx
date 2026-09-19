//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#pragma once

#include "../Urho3D/Core/Object.h"
#include "../Urho3D/Core/Variant.h"

#include "Export.h"

#include <sol/sol.hpp>

namespace sol
{
namespace stack
{

// Engine strings are ea::string (eastl). Teach sol3 to marshal them as plain
// Lua strings so engine APIs taking or returning ea::string bind directly,
// without per-call conversion lambdas.
template <> struct unqualified_pusher<ea::string>
{
    static int push(lua_State* lua, const ea::string& value)
    {
        lua_pushlstring(lua, value.data(), value.size());
        return 1;
    }
};

template <> struct unqualified_getter<ea::string>
{
    static ea::string get(lua_State* lua, int index, record& tracking)
    {
        tracking.use(1);
        std::size_t length = 0;
        const char* str = lua_tolstring(lua, index, &length);
        return str ? ea::string(str, length) : ea::string{};
    }
};

template <>
struct unqualified_checker<ea::string, type::string>
{
    template <typename Handler>
    static bool check(lua_State* lua, int index, Handler&& handler, record& tracking)
    {
        tracking.use(1);
        if (lua_type(lua, index) == LUA_TSTRING)
            return true;
        handler(lua, index, type::string, type_of(lua, index), "");
        return false;
    }
};

} // namespace stack

// Teach sol3 to treat Urho3D::SharedPtr<T> as a smart pointer wrapper so that
// functions returning SharedPtr<T> behave like raw T* in Lua.
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

/// Generic weak reference to an engine Object. Returned to Lua when the concrete
/// type has no explicit sol3 usertype registration. All Serializable attributes
/// remain accessible through the reflection channel, so unbound types are still
/// usable from scripts (e.g. the family of 2D constraints).
class LuaObjectRef
{
public:
    explicit LuaObjectRef(Object* object)
        : object_(object)
    {
    }

    /// Return the referenced object or null when it has been destroyed.
    Object* Get() const { return object_.Get(); }

private:
    WeakPtr<Object> object_;
};

/// Function that casts an Object* into a registered usertype Lua value.
using LuaObjectCaster = sol::object (*)(sol::state_view, Object*);

/// Register a caster for a concrete Object type. Called by binding modules
/// while registering their usertypes.
RBFXLUA_API void RegisterLuaObjectCaster(StringHash type, LuaObjectCaster caster);

/// Wrap an engine object for Lua: returns the registered usertype when the
/// exact runtime type has one, otherwise a generic LuaObjectRef. Never null:
/// null objects become Lua nil.
RBFXLUA_API sol::object WrapLuaObject(sol::state_view lua, Object* object);

/// Convert a Lua value into a Variant (type deduction: bool / number /
/// string / Vector2 / Vector3 / Vector4 / Quaternion / Color / Object*).
RBFXLUA_API Variant LuaToVariant(sol::state_view lua, const sol::object& value);

/// Convert a Variant into a Lua value. VAR_PTR values resolve through the
/// usertype caster registry (falling back to a generic LuaObjectRef).
RBFXLUA_API sol::object VariantToLua(sol::state_view lua, const Variant& value);

/// Convert a VariantVector into a Lua table.
RBFXLUA_API sol::table VariantVectorToLuaTable(sol::state_view lua, const VariantVector& values);

// Binding module entry points. Each module lives in its own translation unit
// to keep sol3 template instantiation within MSVC object file section limits.
// Modules that expose subsystem access receive the owning Context.

RBFXLUA_API void RegisterMathBindings(sol::state& lua);
RBFXLUA_API void RegisterCoreBindings(sol::state& lua, Context* context);
RBFXLUA_API void RegisterGraphicsBindings(sol::state& lua, Context* context);
RBFXLUA_API void RegisterResourceBindings(sol::state& lua, Context* context);
RBFXLUA_API void RegisterInputBindings(sol::state& lua, Context* context);
RBFXLUA_API void RegisterUIBindings(sol::state& lua, Context* context);
RBFXLUA_API void RegisterPhysicsBindings(sol::state& lua, Context* context);
RBFXLUA_API void RegisterUrho2DBindings(sol::state& lua, Context* context);
RBFXLUA_API void RegisterPhysics2DBindings(sol::state& lua, Context* context);
RBFXLUA_API void RegisterAudioBindings(sol::state& lua, Context* context);
RBFXLUA_API void RegisterNavigationBindings(sol::state& lua, Context* context);
RBFXLUA_API void RegisterNetworkBindings(sol::state& lua, Context* context);
RBFXLUA_API void RegisterRmlUIBindings(sol::state& lua, Context* context);

/// Helper for binding modules: register the caster of a concrete type.
/// Casters return a SharedPtr so the Lua reference keeps the object alive
/// (RefCounted intrusive counting) and is released when Lua drops it.
template <typename T>
void RegisterLuaObjectWrapper()
{
    RegisterLuaObjectCaster(T::GetTypeStatic(), [](sol::state_view lua, Object* object) -> sol::object
    {
        return sol::make_object(lua, SharedPtr<T>(static_cast<T*>(object)));
    });
}

} // namespace Urho3D

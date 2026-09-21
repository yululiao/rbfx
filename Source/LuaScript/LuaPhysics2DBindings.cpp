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
#include "../Urho3D/Physics2D/CollisionChain2D.h"
#include "../Urho3D/Physics2D/CollisionEdge2D.h"
#include "../Urho3D/Physics2D/CollisionPolygon2D.h"
#include "../Urho3D/Physics2D/Constraint2D.h"
#include "../Urho3D/Physics2D/ConstraintDistance2D.h"
#include "../Urho3D/Physics2D/ConstraintFriction2D.h"
#include "../Urho3D/Physics2D/ConstraintGear2D.h"
#include "../Urho3D/Physics2D/ConstraintMotor2D.h"
#include "../Urho3D/Physics2D/ConstraintMouse2D.h"
#include "../Urho3D/Physics2D/ConstraintPrismatic2D.h"
#include "../Urho3D/Physics2D/ConstraintPulley2D.h"
#include "../Urho3D/Physics2D/ConstraintRevolute2D.h"
#include "../Urho3D/Physics2D/ConstraintRope2D.h"
#include "../Urho3D/Physics2D/ConstraintWeld2D.h"
#include "../Urho3D/Physics2D/ConstraintWheel2D.h"
#include "../Urho3D/Physics2D/PhysicsWorld2D.h"
#include "../Urho3D/Physics2D/RigidBody2D.h"
#include "../Urho3D/Scene/Node.h"

#include <sol/sol.hpp>

namespace sol
{

template <> struct is_automagical<Urho3D::Constraint2D> : std::false_type {};
template <> struct is_automagical<Urho3D::ConstraintDistance2D> : std::false_type {};
template <> struct is_automagical<Urho3D::ConstraintFriction2D> : std::false_type {};
template <> struct is_automagical<Urho3D::ConstraintGear2D> : std::false_type {};
template <> struct is_automagical<Urho3D::ConstraintMotor2D> : std::false_type {};
template <> struct is_automagical<Urho3D::ConstraintMouse2D> : std::false_type {};
template <> struct is_automagical<Urho3D::ConstraintPrismatic2D> : std::false_type {};
template <> struct is_automagical<Urho3D::ConstraintPulley2D> : std::false_type {};
template <> struct is_automagical<Urho3D::ConstraintRevolute2D> : std::false_type {};
template <> struct is_automagical<Urho3D::ConstraintRope2D> : std::false_type {};
template <> struct is_automagical<Urho3D::ConstraintWeld2D> : std::false_type {};
template <> struct is_automagical<Urho3D::ConstraintWheel2D> : std::false_type {};
template <> struct is_automagical<Urho3D::CollisionEdge2D> : std::false_type {};
template <> struct is_automagical<Urho3D::CollisionPolygon2D> : std::false_type {};
template <> struct is_automagical<Urho3D::CollisionChain2D> : std::false_type {};

} // namespace sol

namespace Urho3D
{

namespace
{

/// Convert a Lua array of Vector2 into the vector expected by polygon/chain shapes.
ea::vector<Vector2> LuaTableToVector2Array(const sol::table& vertices)
{
    ea::vector<Vector2> result;
    if (!vertices.valid())
        return result;
    result.reserve(vertices.size());
    for (int i = 1; i <= static_cast<int>(vertices.size()); ++i)
    {
        const sol::object point = vertices[i];
        if (point.is<Vector2>())
            result.push_back(point.as<Vector2>());
    }
    return result;
}

} // namespace

void RegisterPhysics2DBindings(sol::state& lua, Context* context)
{
    // Constraint2D: shared base of all 2D physics joints. The owner body is
    // the body the constraint component is attached to (32_Physics2DConstraints).
    {
        using LUA_THIS = Constraint2D;
        LUA_CLASS(Constraint2D, sol::no_constructor
            LUA_BASES(Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetOtherBody)
            LUA_MEMBER_FUNC(SetCollideConnected)
            LUA_MEMBER_FUNC(GetOwnerBody)
            LUA_MEMBER_FUNC(GetOtherBody)
        );
    }
    RegisterLuaObjectWrapper<Constraint2D>();

    {
        using LUA_THIS = ConstraintDistance2D;
        LUA_CLASS(ConstraintDistance2D, sol::no_constructor
            LUA_BASES(Constraint2D, Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetOwnerBodyAnchor)
            LUA_MEMBER_FUNC(SetOtherBodyAnchor)
            LUA_MEMBER_FUNC(SetFrequencyHz)
            LUA_MEMBER_FUNC(SetDampingRatio)
            LUA_MEMBER_FUNC(SetLength)
        );
    }
    RegisterLuaObjectWrapper<ConstraintDistance2D>();

    {
        using LUA_THIS = ConstraintFriction2D;
        LUA_CLASS(ConstraintFriction2D, sol::no_constructor
            LUA_BASES(Constraint2D, Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetAnchor)
            LUA_MEMBER_FUNC(SetMaxForce)
            LUA_MEMBER_FUNC(SetMaxTorque)
        );
    }
    RegisterLuaObjectWrapper<ConstraintFriction2D>();

    {
        using LUA_THIS = ConstraintGear2D;
        LUA_CLASS(ConstraintGear2D, sol::no_constructor
            LUA_BASES(Constraint2D, Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetOwnerConstraint)
            LUA_MEMBER_FUNC(SetOtherConstraint)
            LUA_MEMBER_FUNC(SetRatio)
        );
    }
    RegisterLuaObjectWrapper<ConstraintGear2D>();

    {
        using LUA_THIS = ConstraintMotor2D;
        LUA_CLASS(ConstraintMotor2D, sol::no_constructor
            LUA_BASES(Constraint2D, Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetLinearOffset)
            LUA_MEMBER_FUNC(SetAngularOffset)
            LUA_MEMBER_FUNC(SetMaxForce)
            LUA_MEMBER_FUNC(SetMaxTorque)
            LUA_MEMBER_FUNC(SetCorrectionFactor)
        );
    }
    RegisterLuaObjectWrapper<ConstraintMotor2D>();

    {
        using LUA_THIS = ConstraintMouse2D;
        LUA_CLASS(ConstraintMouse2D, sol::no_constructor
            LUA_BASES(Constraint2D, Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetTarget)
            LUA_MEMBER_FUNC(SetMaxForce)
            LUA_MEMBER_FUNC(SetFrequencyHz)
            LUA_MEMBER_FUNC(SetDampingRatio)
        );
    }
    RegisterLuaObjectWrapper<ConstraintMouse2D>();

    {
        using LUA_THIS = ConstraintPrismatic2D;
        LUA_CLASS(ConstraintPrismatic2D, sol::no_constructor
            LUA_BASES(Constraint2D, Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetAnchor)
            LUA_MEMBER_FUNC(SetAxis)
            LUA_MEMBER_FUNC(SetEnableLimit)
            LUA_MEMBER_FUNC(SetLowerTranslation)
            LUA_MEMBER_FUNC(SetUpperTranslation)
            LUA_MEMBER_FUNC(SetEnableMotor)
            LUA_MEMBER_FUNC(SetMaxMotorForce)
            LUA_MEMBER_FUNC(SetMotorSpeed)
        );
    }
    RegisterLuaObjectWrapper<ConstraintPrismatic2D>();

    {
        using LUA_THIS = ConstraintPulley2D;
        LUA_CLASS(ConstraintPulley2D, sol::no_constructor
            LUA_BASES(Constraint2D, Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetOwnerBodyGroundAnchor)
            LUA_MEMBER_FUNC(SetOtherBodyGroundAnchor)
            LUA_MEMBER_FUNC(SetOwnerBodyAnchor)
            LUA_MEMBER_FUNC(SetOtherBodyAnchor)
            LUA_MEMBER_FUNC(SetRatio)
        );
    }
    RegisterLuaObjectWrapper<ConstraintPulley2D>();

    {
        using LUA_THIS = ConstraintRevolute2D;
        LUA_CLASS(ConstraintRevolute2D, sol::no_constructor
            LUA_BASES(Constraint2D, Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetAnchor)
            LUA_MEMBER_FUNC(SetEnableLimit)
            LUA_MEMBER_FUNC(SetLowerAngle)
            LUA_MEMBER_FUNC(SetUpperAngle)
            LUA_MEMBER_FUNC(SetEnableMotor)
            LUA_MEMBER_FUNC(SetMotorSpeed)
            LUA_MEMBER_FUNC(SetMaxMotorTorque)
        );
    }
    RegisterLuaObjectWrapper<ConstraintRevolute2D>();

    {
        using LUA_THIS = ConstraintRope2D;
        LUA_CLASS(ConstraintRope2D, sol::no_constructor
            LUA_BASES(Constraint2D, Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetOwnerBodyAnchor)
            LUA_MEMBER_FUNC(SetOtherBodyAnchor)
            LUA_MEMBER_FUNC(SetMaxLength)
        );
    }
    RegisterLuaObjectWrapper<ConstraintRope2D>();

    {
        using LUA_THIS = ConstraintWeld2D;
        LUA_CLASS(ConstraintWeld2D, sol::no_constructor
            LUA_BASES(Constraint2D, Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetAnchor)
            LUA_MEMBER_FUNC(SetFrequencyHz)
            LUA_MEMBER_FUNC(SetDampingRatio)
        );
    }
    RegisterLuaObjectWrapper<ConstraintWeld2D>();

    {
        using LUA_THIS = ConstraintWheel2D;
        LUA_CLASS(ConstraintWheel2D, sol::no_constructor
            LUA_BASES(Constraint2D, Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetAnchor)
            LUA_MEMBER_FUNC(SetAxis)
            LUA_MEMBER_FUNC(SetEnableMotor)
            LUA_MEMBER_FUNC(SetMaxMotorTorque)
            LUA_MEMBER_FUNC(SetMotorSpeed)
            LUA_MEMBER_FUNC(SetFrequencyHz)
            LUA_MEMBER_FUNC(SetDampingRatio)
        );
    }
    RegisterLuaObjectWrapper<ConstraintWheel2D>();

    // Remaining collision shapes taking vertex lists from Lua tables.
    {
        using LUA_THIS = CollisionEdge2D;
        LUA_CLASS(CollisionEdge2D, sol::no_constructor
            LUA_BASES(CollisionShape2D, Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetVertices)
        );
    }
    RegisterLuaObjectWrapper<CollisionEdge2D>();

    {
        using LUA_THIS = CollisionPolygon2D;
        LUA_CLASS(CollisionPolygon2D, sol::no_constructor
            LUA_BASES(CollisionShape2D, Component, Serializable, Object)
            // Incremental vertex setup used by the tile-map object shapes (49/50).
            LUA_MEMBER_FUNC(SetVertexCount)
            LUA_MEMBER_FUNC(SetVertex)
            LUA_MEMBER_FUNC_RAW(SetVertices, [](CollisionPolygon2D* shape, const sol::table& vertices) {
                if (shape)
                    shape->SetVertices(LuaTableToVector2Array(vertices));
            })
        );
    }
    RegisterLuaObjectWrapper<CollisionPolygon2D>();

    {
        using LUA_THIS = CollisionChain2D;
        LUA_CLASS(CollisionChain2D, sol::no_constructor
            LUA_BASES(CollisionShape2D, Component, Serializable, Object)
            // Incremental vertex setup used by the tile-map object shapes (49/50).
            LUA_MEMBER_FUNC(SetVertexCount)
            LUA_MEMBER_FUNC(SetVertex)
            LUA_MEMBER_FUNC_RAW(SetVertices, [](CollisionChain2D* shape, const sol::table& vertices) {
                if (shape)
                    shape->SetVertices(LuaTableToVector2Array(vertices));
            })
        );
    }
    RegisterLuaObjectWrapper<CollisionChain2D>();
}

} // namespace Urho3D

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
        using RBFX_THIS = Constraint2D;
        RBFX_USERTYPE(Constraint2D, sol::no_constructor
            RBFX_BASES(Component, Serializable, Object)
            RBFX_M(SetOtherBody)
            RBFX_M(SetCollideConnected)
            RBFX_M(GetOwnerBody)
            RBFX_M(GetOtherBody)
        );
    }
    RegisterLuaObjectWrapper<Constraint2D>();

    {
        using RBFX_THIS = ConstraintDistance2D;
        RBFX_USERTYPE(ConstraintDistance2D, sol::no_constructor
            RBFX_BASES(Constraint2D, Component, Serializable, Object)
            RBFX_M(SetOwnerBodyAnchor)
            RBFX_M(SetOtherBodyAnchor)
            RBFX_M(SetFrequencyHz)
            RBFX_M(SetDampingRatio)
            RBFX_M(SetLength)
        );
    }
    RegisterLuaObjectWrapper<ConstraintDistance2D>();

    {
        using RBFX_THIS = ConstraintFriction2D;
        RBFX_USERTYPE(ConstraintFriction2D, sol::no_constructor
            RBFX_BASES(Constraint2D, Component, Serializable, Object)
            RBFX_M(SetAnchor)
            RBFX_M(SetMaxForce)
            RBFX_M(SetMaxTorque)
        );
    }
    RegisterLuaObjectWrapper<ConstraintFriction2D>();

    {
        using RBFX_THIS = ConstraintGear2D;
        RBFX_USERTYPE(ConstraintGear2D, sol::no_constructor
            RBFX_BASES(Constraint2D, Component, Serializable, Object)
            RBFX_M(SetOwnerConstraint)
            RBFX_M(SetOtherConstraint)
            RBFX_M(SetRatio)
        );
    }
    RegisterLuaObjectWrapper<ConstraintGear2D>();

    {
        using RBFX_THIS = ConstraintMotor2D;
        RBFX_USERTYPE(ConstraintMotor2D, sol::no_constructor
            RBFX_BASES(Constraint2D, Component, Serializable, Object)
            RBFX_M(SetLinearOffset)
            RBFX_M(SetAngularOffset)
            RBFX_M(SetMaxForce)
            RBFX_M(SetMaxTorque)
            RBFX_M(SetCorrectionFactor)
        );
    }
    RegisterLuaObjectWrapper<ConstraintMotor2D>();

    {
        using RBFX_THIS = ConstraintMouse2D;
        RBFX_USERTYPE(ConstraintMouse2D, sol::no_constructor
            RBFX_BASES(Constraint2D, Component, Serializable, Object)
            RBFX_M(SetTarget)
            RBFX_M(SetMaxForce)
            RBFX_M(SetFrequencyHz)
            RBFX_M(SetDampingRatio)
        );
    }
    RegisterLuaObjectWrapper<ConstraintMouse2D>();

    {
        using RBFX_THIS = ConstraintPrismatic2D;
        RBFX_USERTYPE(ConstraintPrismatic2D, sol::no_constructor
            RBFX_BASES(Constraint2D, Component, Serializable, Object)
            RBFX_M(SetAnchor)
            RBFX_M(SetAxis)
            RBFX_M(SetEnableLimit)
            RBFX_M(SetLowerTranslation)
            RBFX_M(SetUpperTranslation)
            RBFX_M(SetEnableMotor)
            RBFX_M(SetMaxMotorForce)
            RBFX_M(SetMotorSpeed)
        );
    }
    RegisterLuaObjectWrapper<ConstraintPrismatic2D>();

    {
        using RBFX_THIS = ConstraintPulley2D;
        RBFX_USERTYPE(ConstraintPulley2D, sol::no_constructor
            RBFX_BASES(Constraint2D, Component, Serializable, Object)
            RBFX_M(SetOwnerBodyGroundAnchor)
            RBFX_M(SetOtherBodyGroundAnchor)
            RBFX_M(SetOwnerBodyAnchor)
            RBFX_M(SetOtherBodyAnchor)
            RBFX_M(SetRatio)
        );
    }
    RegisterLuaObjectWrapper<ConstraintPulley2D>();

    {
        using RBFX_THIS = ConstraintRevolute2D;
        RBFX_USERTYPE(ConstraintRevolute2D, sol::no_constructor
            RBFX_BASES(Constraint2D, Component, Serializable, Object)
            RBFX_M(SetAnchor)
            RBFX_M(SetEnableLimit)
            RBFX_M(SetLowerAngle)
            RBFX_M(SetUpperAngle)
            RBFX_M(SetEnableMotor)
            RBFX_M(SetMotorSpeed)
            RBFX_M(SetMaxMotorTorque)
        );
    }
    RegisterLuaObjectWrapper<ConstraintRevolute2D>();

    {
        using RBFX_THIS = ConstraintRope2D;
        RBFX_USERTYPE(ConstraintRope2D, sol::no_constructor
            RBFX_BASES(Constraint2D, Component, Serializable, Object)
            RBFX_M(SetOwnerBodyAnchor)
            RBFX_M(SetOtherBodyAnchor)
            RBFX_M(SetMaxLength)
        );
    }
    RegisterLuaObjectWrapper<ConstraintRope2D>();

    {
        using RBFX_THIS = ConstraintWeld2D;
        RBFX_USERTYPE(ConstraintWeld2D, sol::no_constructor
            RBFX_BASES(Constraint2D, Component, Serializable, Object)
            RBFX_M(SetAnchor)
            RBFX_M(SetFrequencyHz)
            RBFX_M(SetDampingRatio)
        );
    }
    RegisterLuaObjectWrapper<ConstraintWeld2D>();

    {
        using RBFX_THIS = ConstraintWheel2D;
        RBFX_USERTYPE(ConstraintWheel2D, sol::no_constructor
            RBFX_BASES(Constraint2D, Component, Serializable, Object)
            RBFX_M(SetAnchor)
            RBFX_M(SetAxis)
            RBFX_M(SetEnableMotor)
            RBFX_M(SetMaxMotorTorque)
            RBFX_M(SetMotorSpeed)
            RBFX_M(SetFrequencyHz)
            RBFX_M(SetDampingRatio)
        );
    }
    RegisterLuaObjectWrapper<ConstraintWheel2D>();

    // Remaining collision shapes taking vertex lists from Lua tables.
    {
        using RBFX_THIS = CollisionEdge2D;
        RBFX_USERTYPE(CollisionEdge2D, sol::no_constructor
            RBFX_BASES(CollisionShape2D, Component, Serializable, Object)
            RBFX_M(SetVertices)
        );
    }
    RegisterLuaObjectWrapper<CollisionEdge2D>();

    {
        using RBFX_THIS = CollisionPolygon2D;
        RBFX_USERTYPE(CollisionPolygon2D, sol::no_constructor
            RBFX_BASES(CollisionShape2D, Component, Serializable, Object)
            // Incremental vertex setup used by the tile-map object shapes (49/50).
            RBFX_M(SetVertexCount)
            RBFX_M(SetVertex)
            RBFX_RAW(SetVertices, [](CollisionPolygon2D* shape, const sol::table& vertices) {
                if (shape)
                    shape->SetVertices(LuaTableToVector2Array(vertices));
            })
        );
    }
    RegisterLuaObjectWrapper<CollisionPolygon2D>();

    {
        using RBFX_THIS = CollisionChain2D;
        RBFX_USERTYPE(CollisionChain2D, sol::no_constructor
            RBFX_BASES(CollisionShape2D, Component, Serializable, Object)
            // Incremental vertex setup used by the tile-map object shapes (49/50).
            RBFX_M(SetVertexCount)
            RBFX_M(SetVertex)
            RBFX_RAW(SetVertices, [](CollisionChain2D* shape, const sol::table& vertices) {
                if (shape)
                    shape->SetVertices(LuaTableToVector2Array(vertices));
            })
        );
    }
    RegisterLuaObjectWrapper<CollisionChain2D>();
}

} // namespace Urho3D

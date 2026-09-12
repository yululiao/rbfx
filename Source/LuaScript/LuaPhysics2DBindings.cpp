//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#include "../Urho3D/Precompiled.h"

#include "LuaBindings.h"

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
    lua.new_usertype<Constraint2D>("Constraint2D",
        sol::no_constructor,
        sol::base_classes, sol::bases<Component, Serializable, Object>(),
        "SetOtherBody", &Constraint2D::SetOtherBody,
        "SetCollideConnected", &Constraint2D::SetCollideConnected,
        "GetOwnerBody", &Constraint2D::GetOwnerBody,
        "GetOtherBody", &Constraint2D::GetOtherBody
    );
    RegisterLuaObjectWrapper<Constraint2D>();

    lua.new_usertype<ConstraintDistance2D>("ConstraintDistance2D",
        sol::no_constructor,
        sol::base_classes, sol::bases<Constraint2D, Component, Serializable, Object>(),
        "SetOwnerBodyAnchor", &ConstraintDistance2D::SetOwnerBodyAnchor,
        "SetOtherBodyAnchor", &ConstraintDistance2D::SetOtherBodyAnchor,
        "SetFrequencyHz", &ConstraintDistance2D::SetFrequencyHz,
        "SetDampingRatio", &ConstraintDistance2D::SetDampingRatio,
        "SetLength", &ConstraintDistance2D::SetLength
    );
    RegisterLuaObjectWrapper<ConstraintDistance2D>();

    lua.new_usertype<ConstraintFriction2D>("ConstraintFriction2D",
        sol::no_constructor,
        sol::base_classes, sol::bases<Constraint2D, Component, Serializable, Object>(),
        "SetAnchor", &ConstraintFriction2D::SetAnchor,
        "SetMaxForce", &ConstraintFriction2D::SetMaxForce,
        "SetMaxTorque", &ConstraintFriction2D::SetMaxTorque
    );
    RegisterLuaObjectWrapper<ConstraintFriction2D>();

    lua.new_usertype<ConstraintGear2D>("ConstraintGear2D",
        sol::no_constructor,
        sol::base_classes, sol::bases<Constraint2D, Component, Serializable, Object>(),
        "SetOwnerConstraint", &ConstraintGear2D::SetOwnerConstraint,
        "SetOtherConstraint", &ConstraintGear2D::SetOtherConstraint,
        "SetRatio", &ConstraintGear2D::SetRatio
    );
    RegisterLuaObjectWrapper<ConstraintGear2D>();

    lua.new_usertype<ConstraintMotor2D>("ConstraintMotor2D",
        sol::no_constructor,
        sol::base_classes, sol::bases<Constraint2D, Component, Serializable, Object>(),
        "SetLinearOffset", &ConstraintMotor2D::SetLinearOffset,
        "SetAngularOffset", &ConstraintMotor2D::SetAngularOffset,
        "SetMaxForce", &ConstraintMotor2D::SetMaxForce,
        "SetMaxTorque", &ConstraintMotor2D::SetMaxTorque,
        "SetCorrectionFactor", &ConstraintMotor2D::SetCorrectionFactor
    );
    RegisterLuaObjectWrapper<ConstraintMotor2D>();

    lua.new_usertype<ConstraintMouse2D>("ConstraintMouse2D",
        sol::no_constructor,
        sol::base_classes, sol::bases<Constraint2D, Component, Serializable, Object>(),
        "SetTarget", &ConstraintMouse2D::SetTarget,
        "SetMaxForce", &ConstraintMouse2D::SetMaxForce,
        "SetFrequencyHz", &ConstraintMouse2D::SetFrequencyHz,
        "SetDampingRatio", &ConstraintMouse2D::SetDampingRatio
    );
    RegisterLuaObjectWrapper<ConstraintMouse2D>();

    lua.new_usertype<ConstraintPrismatic2D>("ConstraintPrismatic2D",
        sol::no_constructor,
        sol::base_classes, sol::bases<Constraint2D, Component, Serializable, Object>(),
        "SetAnchor", &ConstraintPrismatic2D::SetAnchor,
        "SetAxis", &ConstraintPrismatic2D::SetAxis,
        "SetEnableLimit", &ConstraintPrismatic2D::SetEnableLimit,
        "SetLowerTranslation", &ConstraintPrismatic2D::SetLowerTranslation,
        "SetUpperTranslation", &ConstraintPrismatic2D::SetUpperTranslation,
        "SetEnableMotor", &ConstraintPrismatic2D::SetEnableMotor,
        "SetMaxMotorForce", &ConstraintPrismatic2D::SetMaxMotorForce,
        "SetMotorSpeed", &ConstraintPrismatic2D::SetMotorSpeed
    );
    RegisterLuaObjectWrapper<ConstraintPrismatic2D>();

    lua.new_usertype<ConstraintPulley2D>("ConstraintPulley2D",
        sol::no_constructor,
        sol::base_classes, sol::bases<Constraint2D, Component, Serializable, Object>(),
        "SetOwnerBodyGroundAnchor", &ConstraintPulley2D::SetOwnerBodyGroundAnchor,
        "SetOtherBodyGroundAnchor", &ConstraintPulley2D::SetOtherBodyGroundAnchor,
        "SetOwnerBodyAnchor", &ConstraintPulley2D::SetOwnerBodyAnchor,
        "SetOtherBodyAnchor", &ConstraintPulley2D::SetOtherBodyAnchor,
        "SetRatio", &ConstraintPulley2D::SetRatio
    );
    RegisterLuaObjectWrapper<ConstraintPulley2D>();

    lua.new_usertype<ConstraintRevolute2D>("ConstraintRevolute2D",
        sol::no_constructor,
        sol::base_classes, sol::bases<Constraint2D, Component, Serializable, Object>(),
        "SetAnchor", &ConstraintRevolute2D::SetAnchor,
        "SetEnableLimit", &ConstraintRevolute2D::SetEnableLimit,
        "SetLowerAngle", &ConstraintRevolute2D::SetLowerAngle,
        "SetUpperAngle", &ConstraintRevolute2D::SetUpperAngle,
        "SetEnableMotor", &ConstraintRevolute2D::SetEnableMotor,
        "SetMotorSpeed", &ConstraintRevolute2D::SetMotorSpeed,
        "SetMaxMotorTorque", &ConstraintRevolute2D::SetMaxMotorTorque
    );
    RegisterLuaObjectWrapper<ConstraintRevolute2D>();

    lua.new_usertype<ConstraintRope2D>("ConstraintRope2D",
        sol::no_constructor,
        sol::base_classes, sol::bases<Constraint2D, Component, Serializable, Object>(),
        "SetOwnerBodyAnchor", &ConstraintRope2D::SetOwnerBodyAnchor,
        "SetOtherBodyAnchor", &ConstraintRope2D::SetOtherBodyAnchor,
        "SetMaxLength", &ConstraintRope2D::SetMaxLength
    );
    RegisterLuaObjectWrapper<ConstraintRope2D>();

    lua.new_usertype<ConstraintWeld2D>("ConstraintWeld2D",
        sol::no_constructor,
        sol::base_classes, sol::bases<Constraint2D, Component, Serializable, Object>(),
        "SetAnchor", &ConstraintWeld2D::SetAnchor,
        "SetFrequencyHz", &ConstraintWeld2D::SetFrequencyHz,
        "SetDampingRatio", &ConstraintWeld2D::SetDampingRatio
    );
    RegisterLuaObjectWrapper<ConstraintWeld2D>();

    lua.new_usertype<ConstraintWheel2D>("ConstraintWheel2D",
        sol::no_constructor,
        sol::base_classes, sol::bases<Constraint2D, Component, Serializable, Object>(),
        "SetAnchor", &ConstraintWheel2D::SetAnchor,
        "SetAxis", &ConstraintWheel2D::SetAxis,
        "SetEnableMotor", &ConstraintWheel2D::SetEnableMotor,
        "SetMaxMotorTorque", &ConstraintWheel2D::SetMaxMotorTorque,
        "SetMotorSpeed", &ConstraintWheel2D::SetMotorSpeed,
        "SetFrequencyHz", &ConstraintWheel2D::SetFrequencyHz,
        "SetDampingRatio", &ConstraintWheel2D::SetDampingRatio
    );
    RegisterLuaObjectWrapper<ConstraintWheel2D>();

    // Remaining collision shapes taking vertex lists from Lua tables.
    lua.new_usertype<CollisionEdge2D>("CollisionEdge2D",
        sol::no_constructor,
        sol::base_classes, sol::bases<CollisionShape2D, Component, Serializable, Object>(),
        "SetVertices", &CollisionEdge2D::SetVertices
    );
    RegisterLuaObjectWrapper<CollisionEdge2D>();

    lua.new_usertype<CollisionPolygon2D>("CollisionPolygon2D",
        sol::no_constructor,
        sol::base_classes, sol::bases<CollisionShape2D, Component, Serializable, Object>(),
        // Incremental vertex setup used by the tile-map object shapes (49/50).
        "SetVertexCount", &CollisionPolygon2D::SetVertexCount,
        "SetVertex", &CollisionPolygon2D::SetVertex,
        "SetVertices", [](CollisionPolygon2D* shape, const sol::table& vertices) {
            if (shape)
                shape->SetVertices(LuaTableToVector2Array(vertices));
        }
    );
    RegisterLuaObjectWrapper<CollisionPolygon2D>();

    lua.new_usertype<CollisionChain2D>("CollisionChain2D",
        sol::no_constructor,
        sol::base_classes, sol::bases<CollisionShape2D, Component, Serializable, Object>(),
        // Incremental vertex setup used by the tile-map object shapes (49/50).
        "SetVertexCount", &CollisionChain2D::SetVertexCount,
        "SetVertex", &CollisionChain2D::SetVertex,
        "SetVertices", [](CollisionChain2D* shape, const sol::table& vertices) {
            if (shape)
                shape->SetVertices(LuaTableToVector2Array(vertices));
        }
    );
    RegisterLuaObjectWrapper<CollisionChain2D>();
}

} // namespace Urho3D

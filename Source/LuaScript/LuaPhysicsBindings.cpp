//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#include "../Urho3D/Precompiled.h"

#include "LuaBindings.h"
#include "LuaBindHelpers.h"

#include "../Urho3D/Core/Context.h"
#include "../Urho3D/Math/Ray.h"
#include "../Urho3D/Physics/CollisionShape.h"
#include "../Urho3D/Physics/Constraint.h"
#include "../Urho3D/Physics/PhysicsWorld.h"
#include "../Urho3D/Physics/RaycastVehicle.h"
#include "../Urho3D/Physics/RaycastVehicleWheel.h"
#include "../Urho3D/Physics/RigidBody.h"
#include "../Urho3D/Scene/Node.h"

#include <sol/sol.hpp>

namespace sol
{

template <> struct is_automagical<Urho3D::PhysicsWorld> : std::false_type {};
template <> struct is_automagical<Urho3D::RigidBody> : std::false_type {};
template <> struct is_automagical<Urho3D::CollisionShape> : std::false_type {};
template <> struct is_automagical<Urho3D::Constraint> : std::false_type {};
template <> struct is_automagical<Urho3D::RaycastVehicle> : std::false_type {};
template <> struct is_automagical<Urho3D::RaycastVehicleWheel> : std::false_type {};

} // namespace sol

namespace Urho3D
{

void RegisterPhysicsBindings(sol::state& lua, Context* context)
{
    // PhysicsWorld: simulation setup and ray queries.
    lua.new_usertype<PhysicsWorld>("PhysicsWorld",
        sol::no_constructor,
        sol::base_classes, LuaBases<PhysicsWorld, Component, Serializable, Object>::bases(lua),
        "SetGravity", &PhysicsWorld::SetGravity,
        "GetGravity", &PhysicsWorld::GetGravity,
        "SetFps", &PhysicsWorld::SetFps,
        "SetMaxSubSteps", &PhysicsWorld::SetMaxSubSteps,
        "SetNumIterations", &PhysicsWorld::SetNumIterations,
        "SetInterpolation", &PhysicsWorld::SetInterpolation,
        "SetInternalEdge", &PhysicsWorld::SetInternalEdge,
        "SetSplitImpulse", &PhysicsWorld::SetSplitImpulse,
        "DrawDebugGeometry", [](PhysicsWorld* world, bool depthTest) {
            if (world)
                world->DrawDebugGeometry(depthTest);
        },
        // Ray query returning a table {position, normal, distance, body} or nil.
        "RaycastSingle", [](PhysicsWorld* world, const Ray& ray, float maxDistance,
            sol::optional<unsigned> collisionMask, sol::this_state s) -> sol::object {
            if (!world)
                return sol::lua_nil;
            PhysicsRaycastResult result;
            world->RaycastSingle(result, ray, maxDistance, collisionMask.value_or(M_MAX_UNSIGNED));
            if (!result.body_)
                return sol::lua_nil;
            sol::state_view lua(s);
            sol::table hit = lua.create_table();
            hit["position"] = result.position_;
            hit["normal"] = result.normal_;
            hit["distance"] = result.distance_;
            hit["body"] = result.body_;
            return hit;
        }
    );
    RegisterLuaObjectWrapper<PhysicsWorld>();

    // RigidBody: mass properties, velocities, forces.
    lua.new_usertype<RigidBody>("RigidBody",
        sol::no_constructor,
        sol::base_classes, LuaBases<RigidBody, Component, Serializable, Object>::bases(lua),
        "SetMass", &RigidBody::SetMass,
        // Debug visualization of the collision shape (46_RaycastVehicle).
        "DrawDebugGeometry", &RigidBody::DrawDebugGeometry,
        "GetMass", &RigidBody::GetMass,
        "SetLinearVelocity", &RigidBody::SetLinearVelocity,
        "GetLinearVelocity", &RigidBody::GetLinearVelocity,
        // World-space orientation, for applying vehicle torques (19_VehicleDemo).
        "GetRotation", &RigidBody::GetRotation,
        "SetAngularVelocity", &RigidBody::SetAngularVelocity,
        "GetAngularVelocity", &RigidBody::GetAngularVelocity,
        "SetLinearDamping", &RigidBody::SetLinearDamping,
        "SetAngularDamping", &RigidBody::SetAngularDamping,
        // Lock rotational axes so physics does not turn the body on its own
        // (18_CharacterDemo).
        "SetAngularFactor", &RigidBody::SetAngularFactor,
        "SetFriction", &RigidBody::SetFriction,
        "SetRollingFriction", &RigidBody::SetRollingFriction,
        "SetRestitution", &RigidBody::SetRestitution,
        "SetUseGravity", &RigidBody::SetUseGravity,
        "SetKinematic", &RigidBody::SetKinematic,
        "SetTrigger", &RigidBody::SetTrigger,
        "SetCcdRadius", &RigidBody::SetCcdRadius,
        "SetCcdMotionThreshold", &RigidBody::SetCcdMotionThreshold,
        "SetLinearRestThreshold", &RigidBody::SetLinearRestThreshold,
        "SetAngularRestThreshold", &RigidBody::SetAngularRestThreshold,
        "SetContactProcessingThreshold", &RigidBody::SetContactProcessingThreshold,
        "SetCollisionLayer", &RigidBody::SetCollisionLayer,
        "SetCollisionMask", &RigidBody::SetCollisionMask,
        "SetCollisionLayerAndMask", &RigidBody::SetCollisionLayerAndMask,
        "SetCollisionEventMode", [](RigidBody* body, int mode) {
            if (body)
                body->SetCollisionEventMode(static_cast<CollisionEventMode>(mode));
        },
        "ApplyForce", sol::overload(
            static_cast<void (RigidBody::*)(const Vector3&)>(&RigidBody::ApplyForce),
            static_cast<void (RigidBody::*)(const Vector3&, const Vector3&)>(&RigidBody::ApplyForce)),
        "ApplyTorque", static_cast<void (RigidBody::*)(const Vector3&)>(&RigidBody::ApplyTorque),
        "ApplyImpulse", sol::overload(
            static_cast<void (RigidBody::*)(const Vector3&)>(&RigidBody::ApplyImpulse),
            static_cast<void (RigidBody::*)(const Vector3&, const Vector3&)>(&RigidBody::ApplyImpulse)),
        "ApplyTorqueImpulse", &RigidBody::ApplyTorqueImpulse,
        "ResetForces", &RigidBody::ResetForces,
        "Activate", &RigidBody::Activate
    );
    RegisterLuaObjectWrapper<RigidBody>();

    // CollisionShape: primitive and mesh shapes. Defaults are expanded in the
    // lambdas so Lua calls stay short: shape:SetBox(Vector3(2,2,2)).
    lua.new_usertype<CollisionShape>("CollisionShape",
        sol::no_constructor,
        sol::base_classes, LuaBases<CollisionShape, Component, Serializable, Object>::bases(lua),
        "SetBox", [](CollisionShape* shape, const Vector3& size, sol::optional<Vector3> position,
            sol::optional<Quaternion> rotation) {
            if (shape)
                shape->SetBox(size, position.value_or(Vector3::ZERO), rotation.value_or(Quaternion::IDENTITY));
        },
        "SetSphere", [](CollisionShape* shape, float diameter, sol::optional<Vector3> position,
            sol::optional<Quaternion> rotation) {
            if (shape)
                shape->SetSphere(diameter, position.value_or(Vector3::ZERO), rotation.value_or(Quaternion::IDENTITY));
        },
        "SetCylinder", [](CollisionShape* shape, float diameter, float height, sol::optional<Vector3> position,
            sol::optional<Quaternion> rotation) {
            if (shape)
                shape->SetCylinder(diameter, height, position.value_or(Vector3::ZERO), rotation.value_or(Quaternion::IDENTITY));
        },
        "SetCapsule", [](CollisionShape* shape, float diameter, float height, sol::optional<Vector3> position,
            sol::optional<Quaternion> rotation) {
            if (shape)
                shape->SetCapsule(diameter, height, position.value_or(Vector3::ZERO), rotation.value_or(Quaternion::IDENTITY));
        },
        // Collision from the sibling Terrain component's heightmap
        // (19_VehicleDemo).
        "SetTerrain", [](CollisionShape* shape) {
            if (shape)
                shape->SetTerrain();
        },
        "SetCone", [](CollisionShape* shape, float diameter, float height, sol::optional<Vector3> position,
            sol::optional<Quaternion> rotation) {
            if (shape)
                shape->SetCone(diameter, height, position.value_or(Vector3::ZERO), rotation.value_or(Quaternion::IDENTITY));
        },
        "SetTriangleMesh", [](CollisionShape* shape, Model* model, sol::optional<unsigned> lodLevel) {
            if (shape)
                shape->SetTriangleMesh(model, lodLevel.value_or(0));
        },
        "SetConvexHull", [](CollisionShape* shape, Model* model, sol::optional<unsigned> lodLevel) {
            if (shape)
                shape->SetConvexHull(model, lodLevel.value_or(0));
        },
        "SetTerrain", [](CollisionShape* shape, sol::optional<unsigned> lodLevel) {
            if (shape)
                shape->SetTerrain(lodLevel.value_or(0));
        }
    );
    RegisterLuaObjectWrapper<CollisionShape>();

    // Constraint base: joint pivots and limits. Concrete joints
    // (ConstraintHinge, ConstraintSlider, ...) are created through
    // Node:CreateComponent and configure themselves via attributes.
    lua.new_usertype<Constraint>("Constraint",
        sol::no_constructor,
        sol::base_classes, LuaBases<Constraint, Component, Serializable, Object>::bases(lua),
        "SetOtherBody", &Constraint::SetOtherBody,
        "SetPosition", &Constraint::SetPosition,
        "SetRotation", &Constraint::SetRotation,
        "SetOtherPosition", &Constraint::SetOtherPosition,
        "SetOtherRotation", &Constraint::SetOtherRotation,
        "SetWorldPosition", &Constraint::SetWorldPosition,
        "SetConstraintType", [](Constraint* constraint, int type) {
            if (constraint)
                constraint->SetConstraintType(static_cast<ConstraintType>(type));
        },
        "SetAxis", &Constraint::SetAxis,
        "SetOtherAxis", &Constraint::SetOtherAxis,
        "SetHighLimit", &Constraint::SetHighLimit,
        "SetLowLimit", &Constraint::SetLowLimit,
        "SetDisableCollision", &Constraint::SetDisableCollision
    );
    RegisterLuaObjectWrapper<Constraint>();

    // RaycastVehicle: arcade vehicle simulation on a RigidBody
    // (46_RaycastVehicle). Wheels are RaycastVehicleWheel components on
    // child nodes and self-register with the vehicle.
    lua.new_usertype<RaycastVehicle>("RaycastVehicle",
        sol::no_constructor,
        sol::base_classes, LuaBases<RaycastVehicle, Component, Serializable, Object>::bases(lua),
        "Init", &RaycastVehicle::Init,
        "UpdateInput", &RaycastVehicle::UpdateInput,
        "ResetWheels", &RaycastVehicle::ResetWheels,
        "ResetSuspension", &RaycastVehicle::ResetSuspension,
        "GetNumWheels", &RaycastVehicle::GetNumWheels,
        "GetWheel", &RaycastVehicle::GetWheel,
        "AddWheel", &RaycastVehicle::AddWheel,
        "RemoveWheel", &RaycastVehicle::RemoveWheel,
        "GetMaxSideSlipSpeed", &RaycastVehicle::GetMaxSideSlipSpeed,
        "SetMaxSideSlipSpeed", &RaycastVehicle::SetMaxSideSlipSpeed,
        "SetInAirRPM", &RaycastVehicle::SetInAirRPM,
        "SetEngineForce", &RaycastVehicle::SetEngineForce,
        "GetEngineForce", &RaycastVehicle::GetEngineForce,
        "SetBrakingForce", &RaycastVehicle::SetBrakingForce,
        "GetBrakingForce", &RaycastVehicle::GetBrakingForce
    );
    RegisterLuaObjectWrapper<RaycastVehicle>();

    // RaycastVehicleWheel: per-wheel suspension geometry and handling
    // parameters (46_RaycastVehicle Vehicle component).
    lua.new_usertype<RaycastVehicleWheel>("RaycastVehicleWheel",
        sol::no_constructor,
        sol::base_classes, LuaBases<RaycastVehicleWheel, Component, Serializable, Object>::bases(lua),
        "SetConnectionPoint", &RaycastVehicleWheel::SetConnectionPoint,
        "GetConnectionPoint", &RaycastVehicleWheel::GetConnectionPoint,
        "SetDirection", &RaycastVehicleWheel::SetDirection,
        "GetDirection", &RaycastVehicleWheel::GetDirection,
        "SetAxle", &RaycastVehicleWheel::SetAxle,
        "GetAxle", &RaycastVehicleWheel::GetAxle,
        "SetRotation", &RaycastVehicleWheel::SetRotation,
        "GetRotation", &RaycastVehicleWheel::GetRotation,
        "SetOffset", &RaycastVehicleWheel::SetOffset,
        "SetRadius", &RaycastVehicleWheel::SetRadius,
        "GetRadius", &RaycastVehicleWheel::GetRadius,
        "SetSuspensionRestLength", &RaycastVehicleWheel::SetSuspensionRestLength,
        "GetSuspensionRestLength", &RaycastVehicleWheel::GetSuspensionRestLength,
        "SetSuspensionStiffness", &RaycastVehicleWheel::SetSuspensionStiffness,
        "SetDampingRelaxation", &RaycastVehicleWheel::SetDampingRelaxation,
        "SetDampingCompression", &RaycastVehicleWheel::SetDampingCompression,
        "SetFrictionSlip", &RaycastVehicleWheel::SetFrictionSlip,
        "SetRollInfluence", &RaycastVehicleWheel::SetRollInfluence,
        "SetSteeringFactor", &RaycastVehicleWheel::SetSteeringFactor,
        "SetEngineFactor", &RaycastVehicleWheel::SetEngineFactor,
        "SetBrakeFactor", &RaycastVehicleWheel::SetBrakeFactor,
        "SetSteeringValue", &RaycastVehicleWheel::SetSteeringValue,
        "SetBrakeValue", &RaycastVehicleWheel::SetBrakeValue,
        "SetEngineForce", &RaycastVehicleWheel::SetEngineForce,
        "IsInContact", &RaycastVehicleWheel::IsInContact,
        "GetSkidInfoCumulative", &RaycastVehicleWheel::GetSkidInfoCumulative,
        "GetBrakeValue", &RaycastVehicleWheel::GetBrakeValue,
        "GetContactPosition", &RaycastVehicleWheel::GetContactPosition,
        // Debug visualization of the wheel (46_RaycastVehicle).
        "DrawDebugGeometry", &RaycastVehicleWheel::DrawDebugGeometry
    );
    RegisterLuaObjectWrapper<RaycastVehicleWheel>();

    // Collision event signaling modes for RigidBody:SetCollisionEventMode.
    sol::table cem = lua.create_named_table("CEM");
    cem["NEVER"] = COLLISION_NEVER;
    cem["ACTIVE"] = COLLISION_ACTIVE;
    cem["ALWAYS"] = COLLISION_ALWAYS;

    // Constraint types for Constraint:SetConstraintType.
    sol::table ct = lua.create_named_table("CT");
    ct["POINT"] = CONSTRAINT_POINT;
    ct["HINGE"] = CONSTRAINT_HINGE;
    ct["SLIDER"] = CONSTRAINT_SLIDER;
    ct["CONETWIST"] = CONSTRAINT_CONETWIST;
}

} // namespace Urho3D

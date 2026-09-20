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
    {
        using RBFX_THIS = PhysicsWorld;
        RBFX_USERTYPE(PhysicsWorld, sol::no_constructor
            RBFX_BASES(Component, Serializable, Object)
            RBFX_M(SetGravity)
            RBFX_M(GetGravity)
            RBFX_M(SetFps)
            RBFX_M(SetMaxSubSteps)
            RBFX_M(SetNumIterations)
            RBFX_M(SetInterpolation)
            RBFX_M(SetInternalEdge)
            RBFX_M(SetSplitImpulse)
            RBFX_RAW(DrawDebugGeometry, [](PhysicsWorld* world, bool depthTest) {
                if (world)
                    world->DrawDebugGeometry(depthTest);
            })
            // Ray query returning a table {position, normal, distance, body} or nil.
            RBFX_RAW(RaycastSingle, [](PhysicsWorld* world, const Ray& ray, float maxDistance,
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
            })
        );
    }
    RegisterLuaObjectWrapper<PhysicsWorld>();

    // RigidBody: mass properties, velocities, forces.
    {
        using RBFX_THIS = RigidBody;
        RBFX_USERTYPE(RigidBody, sol::no_constructor
            RBFX_BASES(Component, Serializable, Object)
            RBFX_M(SetMass)
            // Debug visualization of the collision shape (46_RaycastVehicle).
            RBFX_M(DrawDebugGeometry)
            RBFX_M(GetMass)
            RBFX_M(SetLinearVelocity)
            RBFX_M(GetLinearVelocity)
            // World-space orientation, for applying vehicle torques (19_VehicleDemo).
            RBFX_M(GetRotation)
            RBFX_M(SetAngularVelocity)
            RBFX_M(GetAngularVelocity)
            RBFX_M(SetLinearDamping)
            RBFX_M(SetAngularDamping)
            // Lock rotational axes so physics does not turn the body on its own
            // (18_CharacterDemo).
            RBFX_M(SetAngularFactor)
            RBFX_M(SetFriction)
            RBFX_M(SetRollingFriction)
            RBFX_M(SetRestitution)
            RBFX_M(SetUseGravity)
            RBFX_M(SetKinematic)
            RBFX_M(SetTrigger)
            RBFX_M(SetCcdRadius)
            RBFX_M(SetCcdMotionThreshold)
            RBFX_M(SetLinearRestThreshold)
            RBFX_M(SetAngularRestThreshold)
            RBFX_M(SetContactProcessingThreshold)
            RBFX_M(SetCollisionLayer)
            RBFX_M(SetCollisionMask)
            RBFX_M(SetCollisionLayerAndMask)
            RBFX_M_ENUM(SetCollisionEventMode, CollisionEventMode)
            RBFX_OVERLOAD(ApplyForce,
                RBFX_CAST(ApplyForce, void, const Vector3&),
                RBFX_CAST(ApplyForce, void, const Vector3&, const Vector3&))
            RBFX_RAW(ApplyTorque,
                static_cast<void (RigidBody::*)(const Vector3&)>(&RigidBody::ApplyTorque))
            RBFX_OVERLOAD(ApplyImpulse,
                RBFX_CAST(ApplyImpulse, void, const Vector3&),
                RBFX_CAST(ApplyImpulse, void, const Vector3&, const Vector3&))
            RBFX_M(ApplyTorqueImpulse)
            RBFX_M(ResetForces)
            RBFX_M(Activate)
        );
    }
    RegisterLuaObjectWrapper<RigidBody>();

    // CollisionShape: primitive and mesh shapes. Defaults are expanded in the
    // lambdas so Lua calls stay short: shape:SetBox(Vector3(2,2,2)).
    {
        using RBFX_THIS = CollisionShape;
        RBFX_USERTYPE(CollisionShape, sol::no_constructor
            RBFX_BASES(Component, Serializable, Object)
            RBFX_RAW(SetBox, [](CollisionShape* shape, const Vector3& size, sol::optional<Vector3> position,
                sol::optional<Quaternion> rotation) {
                if (shape)
                    shape->SetBox(size, position.value_or(Vector3::ZERO), rotation.value_or(Quaternion::IDENTITY));
            })
            RBFX_RAW(SetSphere, [](CollisionShape* shape, float diameter, sol::optional<Vector3> position,
                sol::optional<Quaternion> rotation) {
                if (shape)
                    shape->SetSphere(diameter, position.value_or(Vector3::ZERO), rotation.value_or(Quaternion::IDENTITY));
            })
            RBFX_RAW(SetCylinder, [](CollisionShape* shape, float diameter, float height, sol::optional<Vector3> position,
                sol::optional<Quaternion> rotation) {
                if (shape)
                    shape->SetCylinder(diameter, height, position.value_or(Vector3::ZERO), rotation.value_or(Quaternion::IDENTITY));
            })
            RBFX_RAW(SetCapsule, [](CollisionShape* shape, float diameter, float height, sol::optional<Vector3> position,
                sol::optional<Quaternion> rotation) {
                if (shape)
                    shape->SetCapsule(diameter, height, position.value_or(Vector3::ZERO), rotation.value_or(Quaternion::IDENTITY));
            })
            // Collision from the sibling Terrain component's heightmap
            // (19_VehicleDemo).
            RBFX_RAW(SetTerrain, [](CollisionShape* shape) {
                if (shape)
                    shape->SetTerrain();
            })
            RBFX_RAW(SetCone, [](CollisionShape* shape, float diameter, float height, sol::optional<Vector3> position,
                sol::optional<Quaternion> rotation) {
                if (shape)
                    shape->SetCone(diameter, height, position.value_or(Vector3::ZERO), rotation.value_or(Quaternion::IDENTITY));
            })
            RBFX_RAW(SetTriangleMesh, [](CollisionShape* shape, Model* model, sol::optional<unsigned> lodLevel) {
                if (shape)
                    shape->SetTriangleMesh(model, lodLevel.value_or(0));
            })
            RBFX_RAW(SetConvexHull, [](CollisionShape* shape, Model* model, sol::optional<unsigned> lodLevel) {
                if (shape)
                    shape->SetConvexHull(model, lodLevel.value_or(0));
            })
            RBFX_RAW(SetTerrain, [](CollisionShape* shape, sol::optional<unsigned> lodLevel) {
                if (shape)
                    shape->SetTerrain(lodLevel.value_or(0));
            })
        );
    }
    RegisterLuaObjectWrapper<CollisionShape>();

    // Constraint base: joint pivots and limits. Concrete joints
    // (ConstraintHinge, ConstraintSlider, ...) are created through
    // Node:CreateComponent and configure themselves via attributes.
    {
        using RBFX_THIS = Constraint;
        RBFX_USERTYPE(Constraint, sol::no_constructor
            RBFX_BASES(Component, Serializable, Object)
            RBFX_M(SetOtherBody)
            RBFX_M(SetPosition)
            RBFX_M(SetRotation)
            RBFX_M(SetOtherPosition)
            RBFX_M(SetOtherRotation)
            RBFX_M(SetWorldPosition)
            RBFX_M_ENUM(SetConstraintType, ConstraintType)
            RBFX_M(SetAxis)
            RBFX_M(SetOtherAxis)
            RBFX_M(SetHighLimit)
            RBFX_M(SetLowLimit)
            RBFX_M(SetDisableCollision)
        );
    }
    RegisterLuaObjectWrapper<Constraint>();

    // RaycastVehicle: arcade vehicle simulation on a RigidBody
    // (46_RaycastVehicle). Wheels are RaycastVehicleWheel components on
    // child nodes and self-register with the vehicle.
    {
        using RBFX_THIS = RaycastVehicle;
        RBFX_USERTYPE(RaycastVehicle, sol::no_constructor
            RBFX_BASES(Component, Serializable, Object)
            RBFX_M(Init)
            RBFX_M(UpdateInput)
            RBFX_M(ResetWheels)
            RBFX_M(ResetSuspension)
            RBFX_M(GetNumWheels)
            RBFX_M(GetWheel)
            RBFX_M(AddWheel)
            RBFX_M(RemoveWheel)
            RBFX_M(GetMaxSideSlipSpeed)
            RBFX_M(SetMaxSideSlipSpeed)
            RBFX_M(SetInAirRPM)
            RBFX_M(SetEngineForce)
            RBFX_M(GetEngineForce)
            RBFX_M(SetBrakingForce)
            RBFX_M(GetBrakingForce)
        );
    }
    RegisterLuaObjectWrapper<RaycastVehicle>();

    // RaycastVehicleWheel: per-wheel suspension geometry and handling
    // parameters (46_RaycastVehicle Vehicle component).
    {
        using RBFX_THIS = RaycastVehicleWheel;
        RBFX_USERTYPE(RaycastVehicleWheel, sol::no_constructor
            RBFX_BASES(Component, Serializable, Object)
            RBFX_M(SetConnectionPoint)
            RBFX_M(GetConnectionPoint)
            RBFX_M(SetDirection)
            RBFX_M(GetDirection)
            RBFX_M(SetAxle)
            RBFX_M(GetAxle)
            RBFX_M(SetRotation)
            RBFX_M(GetRotation)
            RBFX_M(SetOffset)
            RBFX_M(SetRadius)
            RBFX_M(GetRadius)
            RBFX_M(SetSuspensionRestLength)
            RBFX_M(GetSuspensionRestLength)
            RBFX_M(SetSuspensionStiffness)
            RBFX_M(SetDampingRelaxation)
            RBFX_M(SetDampingCompression)
            RBFX_M(SetFrictionSlip)
            RBFX_M(SetRollInfluence)
            RBFX_M(SetSteeringFactor)
            RBFX_M(SetEngineFactor)
            RBFX_M(SetBrakeFactor)
            RBFX_M(SetSteeringValue)
            RBFX_M(SetBrakeValue)
            RBFX_M(SetEngineForce)
            RBFX_M(IsInContact)
            RBFX_M(GetSkidInfoCumulative)
            RBFX_M(GetBrakeValue)
            RBFX_M(GetContactPosition)
            // Debug visualization of the wheel (46_RaycastVehicle).
            RBFX_M(DrawDebugGeometry)
        );
    }
    RegisterLuaObjectWrapper<RaycastVehicleWheel>();

    // Collision event signaling modes for RigidBody:SetCollisionEventMode.
    RBFX_ENUM_TABLE(CEM, "NEVER", COLLISION_NEVER, "ACTIVE", COLLISION_ACTIVE, "ALWAYS", COLLISION_ALWAYS);

    // Constraint types for Constraint:SetConstraintType.
    RBFX_ENUM_TABLE(CT, "POINT", CONSTRAINT_POINT, "HINGE", CONSTRAINT_HINGE,
        "SLIDER", CONSTRAINT_SLIDER, "CONETWIST", CONSTRAINT_CONETWIST);
}

} // namespace Urho3D

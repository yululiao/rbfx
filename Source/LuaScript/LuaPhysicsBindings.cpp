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
        using LUA_THIS = PhysicsWorld;
        LUA_CLASS(PhysicsWorld, sol::no_constructor
            LUA_BASES(Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetGravity)
            LUA_MEMBER_FUNC(GetGravity)
            LUA_MEMBER_FUNC(SetFps)
            LUA_MEMBER_FUNC(SetMaxSubSteps)
            LUA_MEMBER_FUNC(SetNumIterations)
            LUA_MEMBER_FUNC(SetInterpolation)
            LUA_MEMBER_FUNC(SetInternalEdge)
            LUA_MEMBER_FUNC(SetSplitImpulse)
            LUA_MEMBER_FUNC_RAW(DrawDebugGeometry, [](PhysicsWorld* world, bool depthTest) {
                if (world)
                    world->DrawDebugGeometry(depthTest);
            })
            // Ray query returning a table {position, normal, distance, body} or nil.
            LUA_MEMBER_FUNC_RAW(RaycastSingle, [](PhysicsWorld* world, const Ray& ray, float maxDistance,
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
        using LUA_THIS = RigidBody;
        LUA_CLASS(RigidBody, sol::no_constructor
            LUA_BASES(Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetMass)
            // Debug visualization of the collision shape (46_RaycastVehicle).
            LUA_MEMBER_FUNC(DrawDebugGeometry)
            LUA_MEMBER_FUNC(GetMass)
            LUA_MEMBER_FUNC(SetLinearVelocity)
            LUA_MEMBER_FUNC(GetLinearVelocity)
            // World-space orientation, for applying vehicle torques (19_VehicleDemo).
            LUA_MEMBER_FUNC(GetRotation)
            LUA_MEMBER_FUNC(SetAngularVelocity)
            LUA_MEMBER_FUNC(GetAngularVelocity)
            LUA_MEMBER_FUNC(SetLinearDamping)
            LUA_MEMBER_FUNC(SetAngularDamping)
            // Lock rotational axes so physics does not turn the body on its own
            // (18_CharacterDemo).
            LUA_MEMBER_FUNC(SetAngularFactor)
            LUA_MEMBER_FUNC(SetFriction)
            LUA_MEMBER_FUNC(SetRollingFriction)
            LUA_MEMBER_FUNC(SetRestitution)
            LUA_MEMBER_FUNC(SetUseGravity)
            LUA_MEMBER_FUNC(SetKinematic)
            LUA_MEMBER_FUNC(SetTrigger)
            LUA_MEMBER_FUNC(SetCcdRadius)
            LUA_MEMBER_FUNC(SetCcdMotionThreshold)
            LUA_MEMBER_FUNC(SetLinearRestThreshold)
            LUA_MEMBER_FUNC(SetAngularRestThreshold)
            LUA_MEMBER_FUNC(SetContactProcessingThreshold)
            LUA_MEMBER_FUNC(SetCollisionLayer)
            LUA_MEMBER_FUNC(SetCollisionMask)
            LUA_MEMBER_FUNC(SetCollisionLayerAndMask)
            LUA_MEMBER_FUNC_ENUM(SetCollisionEventMode, CollisionEventMode)
            LUA_MEMBER_FUNC_OVERLOAD(ApplyForce,
                LUA_CAST(ApplyForce, void, const Vector3&),
                LUA_CAST(ApplyForce, void, const Vector3&, const Vector3&))
            LUA_MEMBER_FUNC_RAW(ApplyTorque,
                static_cast<void (RigidBody::*)(const Vector3&)>(&RigidBody::ApplyTorque))
            LUA_MEMBER_FUNC_OVERLOAD(ApplyImpulse,
                LUA_CAST(ApplyImpulse, void, const Vector3&),
                LUA_CAST(ApplyImpulse, void, const Vector3&, const Vector3&))
            LUA_MEMBER_FUNC(ApplyTorqueImpulse)
            LUA_MEMBER_FUNC(ResetForces)
            LUA_MEMBER_FUNC(Activate)
        );
    }
    RegisterLuaObjectWrapper<RigidBody>();

    // CollisionShape: primitive and mesh shapes. Defaults are expanded in the
    // lambdas so Lua calls stay short: shape:SetBox(Vector3(2,2,2)).
    {
        using LUA_THIS = CollisionShape;
        LUA_CLASS(CollisionShape, sol::no_constructor
            LUA_BASES(Component, Serializable, Object)
            LUA_MEMBER_FUNC_RAW(SetBox, [](CollisionShape* shape, const Vector3& size, sol::optional<Vector3> position,
                sol::optional<Quaternion> rotation) {
                if (shape)
                    shape->SetBox(size, position.value_or(Vector3::ZERO), rotation.value_or(Quaternion::IDENTITY));
            })
            LUA_MEMBER_FUNC_RAW(SetSphere, [](CollisionShape* shape, float diameter, sol::optional<Vector3> position,
                sol::optional<Quaternion> rotation) {
                if (shape)
                    shape->SetSphere(diameter, position.value_or(Vector3::ZERO), rotation.value_or(Quaternion::IDENTITY));
            })
            LUA_MEMBER_FUNC_RAW(SetCylinder, [](CollisionShape* shape, float diameter, float height, sol::optional<Vector3> position,
                sol::optional<Quaternion> rotation) {
                if (shape)
                    shape->SetCylinder(diameter, height, position.value_or(Vector3::ZERO), rotation.value_or(Quaternion::IDENTITY));
            })
            LUA_MEMBER_FUNC_RAW(SetCapsule, [](CollisionShape* shape, float diameter, float height, sol::optional<Vector3> position,
                sol::optional<Quaternion> rotation) {
                if (shape)
                    shape->SetCapsule(diameter, height, position.value_or(Vector3::ZERO), rotation.value_or(Quaternion::IDENTITY));
            })
            // Collision from the sibling Terrain component's heightmap
            // (19_VehicleDemo).
            LUA_MEMBER_FUNC_RAW(SetTerrain, [](CollisionShape* shape) {
                if (shape)
                    shape->SetTerrain();
            })
            LUA_MEMBER_FUNC_RAW(SetCone, [](CollisionShape* shape, float diameter, float height, sol::optional<Vector3> position,
                sol::optional<Quaternion> rotation) {
                if (shape)
                    shape->SetCone(diameter, height, position.value_or(Vector3::ZERO), rotation.value_or(Quaternion::IDENTITY));
            })
            LUA_MEMBER_FUNC_RAW(SetTriangleMesh, [](CollisionShape* shape, Model* model, sol::optional<unsigned> lodLevel) {
                if (shape)
                    shape->SetTriangleMesh(model, lodLevel.value_or(0));
            })
            LUA_MEMBER_FUNC_RAW(SetConvexHull, [](CollisionShape* shape, Model* model, sol::optional<unsigned> lodLevel) {
                if (shape)
                    shape->SetConvexHull(model, lodLevel.value_or(0));
            })
            LUA_MEMBER_FUNC_RAW(SetTerrain, [](CollisionShape* shape, sol::optional<unsigned> lodLevel) {
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
        using LUA_THIS = Constraint;
        LUA_CLASS(Constraint, sol::no_constructor
            LUA_BASES(Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetOtherBody)
            LUA_MEMBER_FUNC(SetPosition)
            LUA_MEMBER_FUNC(SetRotation)
            LUA_MEMBER_FUNC(SetOtherPosition)
            LUA_MEMBER_FUNC(SetOtherRotation)
            LUA_MEMBER_FUNC(SetWorldPosition)
            LUA_MEMBER_FUNC_ENUM(SetConstraintType, ConstraintType)
            LUA_MEMBER_FUNC(SetAxis)
            LUA_MEMBER_FUNC(SetOtherAxis)
            LUA_MEMBER_FUNC(SetHighLimit)
            LUA_MEMBER_FUNC(SetLowLimit)
            LUA_MEMBER_FUNC(SetDisableCollision)
        );
    }
    RegisterLuaObjectWrapper<Constraint>();

    // RaycastVehicle: arcade vehicle simulation on a RigidBody
    // (46_RaycastVehicle). Wheels are RaycastVehicleWheel components on
    // child nodes and self-register with the vehicle.
    {
        using LUA_THIS = RaycastVehicle;
        LUA_CLASS(RaycastVehicle, sol::no_constructor
            LUA_BASES(Component, Serializable, Object)
            LUA_MEMBER_FUNC(Init)
            LUA_MEMBER_FUNC(UpdateInput)
            LUA_MEMBER_FUNC(ResetWheels)
            LUA_MEMBER_FUNC(ResetSuspension)
            LUA_MEMBER_FUNC(GetNumWheels)
            LUA_MEMBER_FUNC(GetWheel)
            LUA_MEMBER_FUNC(AddWheel)
            LUA_MEMBER_FUNC(RemoveWheel)
            LUA_MEMBER_FUNC(GetMaxSideSlipSpeed)
            LUA_MEMBER_FUNC(SetMaxSideSlipSpeed)
            LUA_MEMBER_FUNC(SetInAirRPM)
            LUA_MEMBER_FUNC(SetEngineForce)
            LUA_MEMBER_FUNC(GetEngineForce)
            LUA_MEMBER_FUNC(SetBrakingForce)
            LUA_MEMBER_FUNC(GetBrakingForce)
        );
    }
    RegisterLuaObjectWrapper<RaycastVehicle>();

    // RaycastVehicleWheel: per-wheel suspension geometry and handling
    // parameters (46_RaycastVehicle Vehicle component).
    {
        using LUA_THIS = RaycastVehicleWheel;
        LUA_CLASS(RaycastVehicleWheel, sol::no_constructor
            LUA_BASES(Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetConnectionPoint)
            LUA_MEMBER_FUNC(GetConnectionPoint)
            LUA_MEMBER_FUNC(SetDirection)
            LUA_MEMBER_FUNC(GetDirection)
            LUA_MEMBER_FUNC(SetAxle)
            LUA_MEMBER_FUNC(GetAxle)
            LUA_MEMBER_FUNC(SetRotation)
            LUA_MEMBER_FUNC(GetRotation)
            LUA_MEMBER_FUNC(SetOffset)
            LUA_MEMBER_FUNC(SetRadius)
            LUA_MEMBER_FUNC(GetRadius)
            LUA_MEMBER_FUNC(SetSuspensionRestLength)
            LUA_MEMBER_FUNC(GetSuspensionRestLength)
            LUA_MEMBER_FUNC(SetSuspensionStiffness)
            LUA_MEMBER_FUNC(SetDampingRelaxation)
            LUA_MEMBER_FUNC(SetDampingCompression)
            LUA_MEMBER_FUNC(SetFrictionSlip)
            LUA_MEMBER_FUNC(SetRollInfluence)
            LUA_MEMBER_FUNC(SetSteeringFactor)
            LUA_MEMBER_FUNC(SetEngineFactor)
            LUA_MEMBER_FUNC(SetBrakeFactor)
            LUA_MEMBER_FUNC(SetSteeringValue)
            LUA_MEMBER_FUNC(SetBrakeValue)
            LUA_MEMBER_FUNC(SetEngineForce)
            LUA_MEMBER_FUNC(IsInContact)
            LUA_MEMBER_FUNC(GetSkidInfoCumulative)
            LUA_MEMBER_FUNC(GetBrakeValue)
            LUA_MEMBER_FUNC(GetContactPosition)
            // Debug visualization of the wheel (46_RaycastVehicle).
            LUA_MEMBER_FUNC(DrawDebugGeometry)
        );
    }
    RegisterLuaObjectWrapper<RaycastVehicleWheel>();

    // Collision event signaling modes for RigidBody:SetCollisionEventMode.
    LUA_ENUM_TABLE(CEM, "NEVER", COLLISION_NEVER, "ACTIVE", COLLISION_ACTIVE, "ALWAYS", COLLISION_ALWAYS);

    // Constraint types for Constraint:SetConstraintType.
    LUA_ENUM_TABLE(CT, "POINT", CONSTRAINT_POINT, "HINGE", CONSTRAINT_HINGE,
        "SLIDER", CONSTRAINT_SLIDER, "CONETWIST", CONSTRAINT_CONETWIST);
}

} // namespace Urho3D

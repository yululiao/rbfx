//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#include "../Urho3D/Precompiled.h"

#include "LuaBindings.h"

#include "../Urho3D/Core/Context.h"
#include "../Urho3D/Physics2D/CollisionBox2D.h"
#include "../Urho3D/Physics2D/CollisionCircle2D.h"
#include "../Urho3D/Physics2D/CollisionShape2D.h"
#include "../Urho3D/Physics2D/PhysicsWorld2D.h"
#include "../Urho3D/Physics2D/RigidBody2D.h"
#include "../Urho3D/Scene/Node.h"
#include "../Urho3D/Urho2D/AnimatedSprite2D.h"
#include "../Urho3D/Urho2D/AnimationSet2D.h"
#include "../Urho3D/Urho2D/Drawable2D.h"
#include "../Urho3D/Urho2D/ParticleEffect2D.h"
#include "../Urho3D/Urho2D/ParticleEmitter2D.h"
#include "../Urho3D/Urho2D/Sprite2D.h"
#include "../Urho3D/Urho2D/StaticSprite2D.h"
#include "../Urho3D/Urho2D/StretchableSprite2D.h"
#include "../Urho3D/Urho2D/TileMap2D.h"
#include "../Urho3D/Urho2D/TileMapDefs2D.h"
#include "../Urho3D/Urho2D/TileMapLayer2D.h"
#include "../Urho3D/Urho2D/TmxFile2D.h"

#include <sol/sol.hpp>

namespace sol
{

template <> struct is_automagical<Urho3D::Drawable2D> : std::false_type {};
template <> struct is_automagical<Urho3D::Sprite2D> : std::false_type {};
template <> struct is_automagical<Urho3D::AnimationSet2D> : std::false_type {};
template <> struct is_automagical<Urho3D::StaticSprite2D> : std::false_type {};
template <> struct is_automagical<Urho3D::AnimatedSprite2D> : std::false_type {};
template <> struct is_automagical<Urho3D::TileMap2D> : std::false_type {};
template <> struct is_automagical<Urho3D::TileMapLayer2D> : std::false_type {};
template <> struct is_automagical<Urho3D::TileMapObject2D> : std::false_type {};
template <> struct is_automagical<Urho3D::Tile2D> : std::false_type {};
template <> struct is_automagical<Urho3D::ParticleEffect2D> : std::false_type {};
template <> struct is_automagical<Urho3D::ParticleEmitter2D> : std::false_type {};
template <> struct is_automagical<Urho3D::StretchableSprite2D> : std::false_type {};
template <> struct is_automagical<Urho3D::TmxFile2D> : std::false_type {};
template <> struct is_automagical<Urho3D::TileMapInfo2D> : std::false_type {};
template <> struct is_automagical<Urho3D::RigidBody2D> : std::false_type {};
template <> struct is_automagical<Urho3D::CollisionShape2D> : std::false_type {};
template <> struct is_automagical<Urho3D::CollisionBox2D> : std::false_type {};
template <> struct is_automagical<Urho3D::CollisionCircle2D> : std::false_type {};
template <> struct is_automagical<Urho3D::PhysicsWorld2D> : std::false_type {};

} // namespace sol

namespace Urho3D
{

void RegisterUrho2DBindings(sol::state& lua, Context* context)
{
    // Drawable2D: shared 2D layer/order controls.
    lua.new_usertype<Drawable2D>("Drawable2D",
        sol::no_constructor,
        sol::base_classes, sol::bases<Drawable, Component, Serializable, Object>(),
        "SetLayer", &Drawable2D::SetLayer,
        "SetOrderInLayer", &Drawable2D::SetOrderInLayer
    );
    RegisterLuaObjectWrapper<Drawable2D>();

    // Sprite resources.
    lua.new_usertype<Sprite2D>("Sprite2D",
        sol::no_constructor,
        sol::base_classes, sol::bases<Resource, Object>()
    );
    RegisterLuaObjectWrapper<Sprite2D>();

    lua.new_usertype<AnimationSet2D>("AnimationSet2D",
        sol::no_constructor,
        sol::base_classes, sol::bases<Resource, Object>(),
        "GetNumAnimations", &AnimationSet2D::GetNumAnimations,
        "GetAnimation", &AnimationSet2D::GetAnimation,
        "GetSprite",
            static_cast<Sprite2D* (AnimationSet2D::*)(int, int) const>(&AnimationSet2D::GetSpriterFileSprite)
    );
    RegisterLuaObjectWrapper<AnimationSet2D>();

    // StaticSprite2D: the workhorse of the 2D samples.
    lua.new_usertype<StaticSprite2D>("StaticSprite2D",
        sol::no_constructor,
        sol::base_classes, sol::bases<Drawable2D, Drawable, Component, Serializable, Object>(),
        "SetSprite", &StaticSprite2D::SetSprite,
        "GetSprite", &StaticSprite2D::GetSprite,
        "SetColor", &StaticSprite2D::SetColor,
        "SetBlendMode", [](StaticSprite2D* sprite, int mode) {
            if (sprite)
                sprite->SetBlendMode(static_cast<BlendMode>(mode));
        },
        "SetFlip", [](StaticSprite2D* sprite, bool flipX, bool flipY, sol::optional<bool> swapXY) {
            if (sprite)
                sprite->SetFlip(flipX, flipY, swapXY.value_or(false));
        },
        "SetFlipX", &StaticSprite2D::SetFlipX,
        "SetFlipY", &StaticSprite2D::SetFlipY,
        "SetUseHotSpot", &StaticSprite2D::SetUseHotSpot,
        "SetHotSpot", &StaticSprite2D::SetHotSpot
    );
    RegisterLuaObjectWrapper<StaticSprite2D>();

    // AnimatedSprite2D: Spriter-driven animation playback.
    lua.new_usertype<AnimatedSprite2D>("AnimatedSprite2D",
        sol::no_constructor,
        // Full chain: sol3 base lookup is single-level, SetLayer lives on
        // Drawable2D (49/50 Sample2D).
        sol::base_classes, sol::bases<StaticSprite2D, Drawable2D, Drawable, Component, Serializable, Object>(),
        "SetAnimationSet", &AnimatedSprite2D::SetAnimationSet,
        "GetAnimationSet", &AnimatedSprite2D::GetAnimationSet,
        "SetAnimation", [](AnimatedSprite2D* sprite, const char* name, sol::optional<bool> looped) {
            if (sprite)
                sprite->SetAnimation(name, looped.value_or(true) ? LM_FORCE_LOOPED : LM_FORCE_CLAMPED);
        },
        // Current animation name; compare or feed back into SetAnimation
        // (33_SpriterAnimation index switching).
        "GetAnimation", [](AnimatedSprite2D* sprite) {
            return sprite ? sprite->GetAnimation().c_str() : "";
        },
        "SetSpeed", &AnimatedSprite2D::SetSpeed,
        "SetFlipX", &AnimatedSprite2D::SetFlipX,
        "SetFlipY", &AnimatedSprite2D::SetFlipY,
        "GetFlipX", &AnimatedSprite2D::GetFlipX,
        "GetFlipY", &AnimatedSprite2D::GetFlipY
    );
    RegisterLuaObjectWrapper<AnimatedSprite2D>();

    // StretchableSprite2D: 9-slice sprite (51_Urho2DStretchableSprite).
    lua.new_usertype<StretchableSprite2D>("StretchableSprite2D",
        sol::no_constructor,
        sol::base_classes, sol::bases<StaticSprite2D, Drawable2D, Drawable, Component, Serializable, Object>(),
        "SetBorder", &StretchableSprite2D::SetBorder
    );
    RegisterLuaObjectWrapper<StretchableSprite2D>();

    // ParticleEmitter2D: sprite particles from a ParticleEffect2D resource
    // (25_Urho2DParticle).
    lua.new_usertype<ParticleEffect2D>("ParticleEffect2D",
        sol::no_constructor,
        sol::base_classes, sol::bases<Resource, Object>()
    );
    RegisterLuaObjectWrapper<ParticleEffect2D>();

    lua.new_usertype<ParticleEmitter2D>("ParticleEmitter2D",
        sol::no_constructor,
        sol::base_classes, sol::bases<Drawable2D, Drawable, Component, Serializable, Object>(),
        "SetEffect", &ParticleEmitter2D::SetEffect,
        "GetEffect", &ParticleEmitter2D::GetEffect,
        "SetSprite", &ParticleEmitter2D::SetSprite,
        "SetEmitting", &ParticleEmitter2D::SetEmitting,
        "IsEmitting", &ParticleEmitter2D::IsEmitting
    );
    RegisterLuaObjectWrapper<ParticleEmitter2D>();

    // Tile maps (39_TileMap, 41_Dungeon).
    lua.new_usertype<TmxFile2D>("TmxFile2D",
        sol::no_constructor,
        sol::base_classes, sol::bases<Resource, Object>()
    );
    RegisterLuaObjectWrapper<TmxFile2D>();

    // Tile map header info: orientation, dimensions and tile sizes
    // (36_Urho2DTileMap, 49/50 Sample2D logic).
    lua.new_usertype<TileMapInfo2D>("TileMapInfo2D",
        sol::no_constructor,
        "GetMapWidth", &TileMapInfo2D::GetMapWidth,
        "GetMapHeight", &TileMapInfo2D::GetMapHeight,
        "orientation", sol::readonly_property([](TileMapInfo2D* info) {
            return info ? static_cast<int>(info->orientation_) : 0;
        }),
        "width", sol::readonly_property(&TileMapInfo2D::width_),
        "height", sol::readonly_property(&TileMapInfo2D::height_),
        "tileWidth", sol::readonly_property(&TileMapInfo2D::tileWidth_),
        "tileHeight", sol::readonly_property(&TileMapInfo2D::tileHeight_)
    );

    lua.new_usertype<TileMap2D>("TileMap2D",
        sol::no_constructor,
        // rbfx simplified the tile map classes to derive from Component
        // directly (upstream Urho3D used Drawable2D).
        sol::base_classes, sol::bases<Component, Serializable, Object>(),
        "SetTmxFile", &TileMap2D::SetTmxFile,
        "GetInfo", [](TileMap2D* tileMap) -> TileMapInfo2D* {
            return tileMap ? const_cast<TileMapInfo2D*>(&tileMap->GetInfo()) : nullptr;
        },
        "GetNumLayers", &TileMap2D::GetNumLayers,
        "GetLayer", &TileMap2D::GetLayer,
        "TileIndexToPosition", &TileMap2D::TileIndexToPosition,
        "PositionToTileIndex", [](TileMap2D* tileMap, const Vector2& position) {
            int x = 0, y = 0;
            const bool ok = tileMap && tileMap->PositionToTileIndex(x, y, position);
            return std::make_tuple(x, y, ok);
        },
        // Debug overlay of the tile grid (36_Urho2DTileMap).
        "DrawDebugGeometry", static_cast<void (TileMap2D::*)(DebugRenderer*, bool)>(&TileMap2D::DrawDebugGeometry)
    );
    RegisterLuaObjectWrapper<TileMap2D>();

    // TileMap layer: tiles, objects or image (36_Urho2DTileMap, 49/50).
    lua.new_usertype<TileMapLayer2D>("TileMapLayer2D",
        sol::no_constructor,
        sol::base_classes, sol::bases<Component, Serializable, Object>(),
        "GetTileMap", &TileMapLayer2D::GetTileMap,
        "SetVisible", &TileMapLayer2D::SetVisible,
        "IsVisible", &TileMapLayer2D::IsVisible,
        "HasProperty", &TileMapLayer2D::HasProperty,
        "GetProperty", [](TileMapLayer2D* layer, const char* name) -> const char* {
            static thread_local ea::string value;
            value = layer ? layer->GetProperty(name) : "";
            return value.c_str();
        },
        "GetLayerType", &TileMapLayer2D::GetLayerType,
        "GetWidth", &TileMapLayer2D::GetWidth,
        "GetHeight", &TileMapLayer2D::GetHeight,
        "GetTileNode", &TileMapLayer2D::GetTileNode,
        "GetTile", &TileMapLayer2D::GetTile,
        "GetNumObjects", &TileMapLayer2D::GetNumObjects,
        "GetObject", &TileMapLayer2D::GetObject,
        "GetObjectNode", &TileMapLayer2D::GetObjectNode,
        "GetImageNode", &TileMapLayer2D::GetImageNode
    );
    RegisterLuaObjectWrapper<TileMapLayer2D>();

    // Single object inside an object group layer.
    lua.new_usertype<TileMapObject2D>("TileMapObject2D",
        sol::no_constructor,
        "GetObjectType", &TileMapObject2D::GetObjectType,
        "GetName", &TileMapObject2D::GetName,
        "GetType", &TileMapObject2D::GetType,
        "GetPosition", &TileMapObject2D::GetPosition,
        "GetSize", &TileMapObject2D::GetSize,
        "GetNumPoints", &TileMapObject2D::GetNumPoints,
        "GetPoint", &TileMapObject2D::GetPoint,
        "GetTileGid", &TileMapObject2D::GetTileGid,
        "GetTileSprite", &TileMapObject2D::GetTileSprite,
        "GetProperty", [](TileMapObject2D* object, const char* name) -> const char* {
            static thread_local ea::string value;
            value = object ? object->GetProperty(name) : "";
            return value.c_str();
        },
        "HasProperty", [](TileMapObject2D* object, const char* name) -> bool {
            return object && object->HasProperty(name);
        }
    );
    // TileMapObject2D is plain RefCounted, not an Object: no caster.

    // Single tile inside a tile layer.
    lua.new_usertype<Tile2D>("Tile2D",
        sol::no_constructor,
        "GetGid", &Tile2D::GetGid,
        "GetFlipX", &Tile2D::GetFlipX,
        "GetFlipY", &Tile2D::GetFlipY,
        "GetSprite", &Tile2D::GetSprite,
        "GetProperty", [](Tile2D* tile, const char* name) -> const char* {
            static thread_local ea::string value;
            value = tile ? tile->GetProperty(name) : "";
            return value.c_str();
        }
    );
    // Tile2D is plain RefCounted, not an Object: no caster.

    // --- Physics2D ---

    lua.new_usertype<RigidBody2D>("RigidBody2D",
        sol::no_constructor,
        sol::base_classes, sol::bases<Component, Serializable, Object>(),
        "SetBodyType", [](RigidBody2D* body, int type) {
            if (body)
                body->SetBodyType(static_cast<BodyType2D>(type));
        },
        "SetMass", &RigidBody2D::SetMass,
        "SetUseFixtureMass", &RigidBody2D::SetUseFixtureMass,
        "SetLinearDamping", &RigidBody2D::SetLinearDamping,
        "SetAngularDamping", &RigidBody2D::SetAngularDamping,
        "SetAllowSleep", &RigidBody2D::SetAllowSleep,
        "SetFixedRotation", &RigidBody2D::SetFixedRotation,
        "SetBullet", &RigidBody2D::SetBullet,
        "SetGravityScale", &RigidBody2D::SetGravityScale,
        "SetAwake", &RigidBody2D::SetAwake,
        "SetLinearVelocity", &RigidBody2D::SetLinearVelocity,
        "GetLinearVelocity", &RigidBody2D::GetLinearVelocity,
        "SetAngularVelocity", &RigidBody2D::SetAngularVelocity,
        "ApplyForce", &RigidBody2D::ApplyForce,
        "ApplyForceToCenter", [](RigidBody2D* body, const Vector2& force, sol::optional<bool> wake) {
            if (body)
                body->ApplyForceToCenter(force, wake.value_or(true));
        },
        "ApplyTorque", &RigidBody2D::ApplyTorque,
        "ApplyAngularImpulse", &RigidBody2D::ApplyAngularImpulse,
        "ApplyLinearImpulse", [](RigidBody2D* body, const Vector2& impulse, const Vector2& point, sol::optional<bool> wake) {
            if (body)
                body->ApplyLinearImpulse(impulse, point, wake.value_or(true));
        },
        "ApplyLinearImpulseToCenter", [](RigidBody2D* body, const Vector2& impulse, sol::optional<bool> wake) {
            if (body)
                body->ApplyLinearImpulseToCenter(impulse, wake.value_or(true));
        },
        "SetInertia", &RigidBody2D::SetInertia,
        "SetMassCenter", &RigidBody2D::SetMassCenter,
        "GetMass", &RigidBody2D::GetMass,
        "GetMassCenter", &RigidBody2D::GetMassCenter,
        "GetBodyType", [](RigidBody2D* body) {
            return body ? static_cast<int>(body->GetBodyType()) : 0;
        }
    );
    RegisterLuaObjectWrapper<RigidBody2D>();

    lua.new_usertype<CollisionShape2D>("CollisionShape2D",
        sol::no_constructor,
        sol::base_classes, sol::bases<Component, Serializable, Object>(),
        "SetTrigger", &CollisionShape2D::SetTrigger,
        "SetCategoryBits", &CollisionShape2D::SetCategoryBits,
        "SetMaskBits", &CollisionShape2D::SetMaskBits,
        "SetGroupIndex", &CollisionShape2D::SetGroupIndex,
        "SetDensity", &CollisionShape2D::SetDensity,
        "SetFriction", &CollisionShape2D::SetFriction,
        "SetRestitution", &CollisionShape2D::SetRestitution
    );
    RegisterLuaObjectWrapper<CollisionShape2D>();

    lua.new_usertype<CollisionBox2D>("CollisionBox2D",
        sol::no_constructor,
        sol::base_classes, sol::bases<CollisionShape2D, Component, Serializable, Object>(),
        "SetSize", sol::overload(
            static_cast<void (CollisionBox2D::*)(const Vector2&)>(&CollisionBox2D::SetSize),
            static_cast<void (CollisionBox2D::*)(float, float)>(&CollisionBox2D::SetSize)),
        "SetCenter", sol::overload(
            static_cast<void (CollisionBox2D::*)(const Vector2&)>(&CollisionBox2D::SetCenter),
            static_cast<void (CollisionBox2D::*)(float, float)>(&CollisionBox2D::SetCenter)),
        "SetAngle", &CollisionBox2D::SetAngle
    );
    RegisterLuaObjectWrapper<CollisionBox2D>();

    lua.new_usertype<CollisionCircle2D>("CollisionCircle2D",
        sol::no_constructor,
        sol::base_classes, sol::bases<CollisionShape2D, Component, Serializable, Object>(),
        "SetRadius", &CollisionCircle2D::SetRadius,
        "SetCenter", sol::overload(
            static_cast<void (CollisionCircle2D::*)(const Vector2&)>(&CollisionCircle2D::SetCenter),
            static_cast<void (CollisionCircle2D::*)(float, float)>(&CollisionCircle2D::SetCenter))
    );
    RegisterLuaObjectWrapper<CollisionCircle2D>();

    lua.new_usertype<PhysicsWorld2D>("PhysicsWorld2D",
        sol::no_constructor,
        sol::base_classes, sol::bases<Component, Serializable, Object>(),
        "SetGravity", &PhysicsWorld2D::SetGravity,
        "GetGravity", &PhysicsWorld2D::GetGravity,
        "SetDrawShape", &PhysicsWorld2D::SetDrawShape,
        "SetDrawJoint", &PhysicsWorld2D::SetDrawJoint,
        "SetDrawAabb", &PhysicsWorld2D::SetDrawAabb,
        "SetDrawPair", &PhysicsWorld2D::SetDrawPair,
        "SetDrawCenterOfMass", &PhysicsWorld2D::SetDrawCenterOfMass,
        "SetAllowSleeping", &PhysicsWorld2D::SetAllowSleeping,
        "SetSubStepping", &PhysicsWorld2D::SetSubStepping,
        "SetAutoClearForces", &PhysicsWorld2D::SetAutoClearForces,
        "SetVelocityIterations", &PhysicsWorld2D::SetVelocityIterations,
        "SetPositionIterations", &PhysicsWorld2D::SetPositionIterations,
        "DrawDebugGeometry", [](PhysicsWorld2D* world) {
            if (world)
                world->DrawDebugGeometry();
        },
        // Screen-space and world-space rigid body picking (32_Physics2DConstraints).
        "GetRigidBody", sol::overload(
            [](PhysicsWorld2D* world, int screenX, int screenY, sol::optional<unsigned> collisionMask) {
                return world ? world->GetRigidBody(screenX, screenY, collisionMask.value_or(M_MAX_UNSIGNED)) : nullptr;
            },
            [](PhysicsWorld2D* world, const Vector2& point, sol::optional<unsigned> collisionMask) {
                return world ? world->GetRigidBody(point, collisionMask.value_or(M_MAX_UNSIGNED)) : nullptr;
            }),
        // AABB query returning all bodies in a Rect (50_Urho2DPlatformer
        // character ground checks).
        "GetRigidBodies", [](PhysicsWorld2D* world, const Rect& aabb, sol::optional<unsigned> collisionMask,
            sol::this_state s) -> sol::table {
            sol::state_view lua(s);
            sol::table result = lua.create_table();
            if (world)
            {
                ea::vector<RigidBody2D*> bodies;
                world->GetRigidBodies(bodies, aabb, collisionMask.value_or(M_MAX_UNSIGNED));
                unsigned index = 1;
                for (RigidBody2D* body : bodies)
                    result[index++] = body;
            }
            return result;
        }
    );
    RegisterLuaObjectWrapper<PhysicsWorld2D>();

    // Box2D body type constants.
    sol::table bt = lua.create_named_table("BT2D");
    bt["STATIC"] = BT_STATIC;
    bt["DYNAMIC"] = BT_DYNAMIC;
    bt["KINEMATIC"] = BT_KINEMATIC;

    // Tile map orientation constants (TileMapDefs2D.h).
    sol::table orient = lua.create_named_table("ORIENT2D");
    orient["ORTHOGONAL"] = O_ORTHOGONAL;
    orient["ISOMETRIC"] = O_ISOMETRIC;
    orient["STAGGERED"] = O_STAGGERED;
    orient["HEXAGONAL"] = O_HEXAGONAL;

    // Tile map layer type constants (TileMapLayer2D:GetLayerType).
    sol::table lt = lua.create_named_table("LT2D");
    lt["TILE_LAYER"] = LT_TILE_LAYER;
    lt["OBJECT_GROUP"] = LT_OBJECT_GROUP;
    lt["IMAGE_LAYER"] = LT_IMAGE_LAYER;

    // Tile map object type constants (Sample2D collision shape factory).
    sol::table ot = lua.create_named_table("OT2D");
    ot["RECTANGLE"] = OT_RECTANGLE;
    ot["ELLIPSE"] = OT_ELLIPSE;
    ot["POLYGON"] = OT_POLYGON;
    ot["POLYLINE"] = OT_POLYLINE;
    ot["TILE"] = OT_TILE;
}

} // namespace Urho3D

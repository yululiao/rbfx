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
    {
        using RBFX_THIS = Drawable2D;
        RBFX_USERTYPE(Drawable2D, sol::no_constructor
            RBFX_BASES(Drawable, Component, Serializable, Object)
            RBFX_M(SetLayer)
            RBFX_M(SetOrderInLayer)
        );
    }
    RegisterLuaObjectWrapper<Drawable2D>();

    // Sprite resources.
    {
        using RBFX_THIS = Sprite2D;
        RBFX_USERTYPE(Sprite2D, sol::no_constructor
            RBFX_BASES(Resource, Object)
        );
    }
    RegisterLuaObjectWrapper<Sprite2D>();

    {
        using RBFX_THIS = AnimationSet2D;
        RBFX_USERTYPE(AnimationSet2D, sol::no_constructor
            RBFX_BASES(Resource, Object)
            RBFX_M(GetNumAnimations)
            RBFX_M(GetAnimation)
            RBFX_RAW(GetSprite,
                static_cast<Sprite2D* (AnimationSet2D::*)(int, int) const>(&AnimationSet2D::GetSpriterFileSprite))
        );
    }
    RegisterLuaObjectWrapper<AnimationSet2D>();

    // StaticSprite2D: the workhorse of the 2D samples.
    {
        using RBFX_THIS = StaticSprite2D;
        RBFX_USERTYPE(StaticSprite2D, sol::no_constructor
            RBFX_BASES(Drawable2D, Drawable, Component, Serializable, Object)
            RBFX_M(SetSprite)
            RBFX_M(GetSprite)
            RBFX_M(SetColor)
            RBFX_RAW(SetBlendMode, [](StaticSprite2D* sprite, int mode) {
                if (sprite)
                    sprite->SetBlendMode(static_cast<BlendMode>(mode));
            })
            RBFX_RAW(SetFlip, [](StaticSprite2D* sprite, bool flipX, bool flipY, sol::optional<bool> swapXY) {
                if (sprite)
                    sprite->SetFlip(flipX, flipY, swapXY.value_or(false));
            })
            RBFX_M(SetFlipX)
            RBFX_M(SetFlipY)
            RBFX_M(SetUseHotSpot)
            RBFX_M(SetHotSpot)
        );
    }
    RegisterLuaObjectWrapper<StaticSprite2D>();

    // AnimatedSprite2D: Spriter-driven animation playback.
    {
        using RBFX_THIS = AnimatedSprite2D;
        RBFX_USERTYPE(AnimatedSprite2D, sol::no_constructor
            // Full chain: sol3 base lookup is single-level, SetLayer lives on
            // Drawable2D (49/50 Sample2D).
            RBFX_BASES(StaticSprite2D, Drawable2D, Drawable, Component, Serializable, Object)
            RBFX_M(SetAnimationSet)
            RBFX_M(GetAnimationSet)
            RBFX_RAW(SetAnimation, [](AnimatedSprite2D* sprite, const char* name, sol::optional<bool> looped) {
                if (sprite)
                    sprite->SetAnimation(name, looped.value_or(true) ? LM_FORCE_LOOPED : LM_FORCE_CLAMPED);
            })
            // Current animation name; compare or feed back into SetAnimation
            // (33_SpriterAnimation index switching).
            RBFX_RAW(GetAnimation, [](AnimatedSprite2D* sprite) {
                return sprite ? sprite->GetAnimation().c_str() : "";
            })
            RBFX_M(SetSpeed)
            RBFX_M(SetFlipX)
            RBFX_M(SetFlipY)
            RBFX_M(GetFlipX)
            RBFX_M(GetFlipY)
        );
    }
    RegisterLuaObjectWrapper<AnimatedSprite2D>();

    // StretchableSprite2D: 9-slice sprite (51_Urho2DStretchableSprite).
    {
        using RBFX_THIS = StretchableSprite2D;
        RBFX_USERTYPE(StretchableSprite2D, sol::no_constructor
            RBFX_BASES(StaticSprite2D, Drawable2D, Drawable, Component, Serializable, Object)
            RBFX_M(SetBorder)
        );
    }
    RegisterLuaObjectWrapper<StretchableSprite2D>();

    // ParticleEmitter2D: sprite particles from a ParticleEffect2D resource
    // (25_Urho2DParticle).
    {
        using RBFX_THIS = ParticleEffect2D;
        RBFX_USERTYPE(ParticleEffect2D, sol::no_constructor
            RBFX_BASES(Resource, Object)
        );
    }
    RegisterLuaObjectWrapper<ParticleEffect2D>();

    {
        using RBFX_THIS = ParticleEmitter2D;
        RBFX_USERTYPE(ParticleEmitter2D, sol::no_constructor
            RBFX_BASES(Drawable2D, Drawable, Component, Serializable, Object)
            RBFX_M(SetEffect)
            RBFX_M(GetEffect)
            RBFX_M(SetSprite)
            RBFX_M(SetEmitting)
            RBFX_M(IsEmitting)
        );
    }
    RegisterLuaObjectWrapper<ParticleEmitter2D>();

    // Tile maps (39_TileMap, 41_Dungeon).
    {
        using RBFX_THIS = TmxFile2D;
        RBFX_USERTYPE(TmxFile2D, sol::no_constructor
            RBFX_BASES(Resource, Object)
        );
    }
    RegisterLuaObjectWrapper<TmxFile2D>();

    // Tile map header info: orientation, dimensions and tile sizes
    // (36_Urho2DTileMap, 49/50 Sample2D logic).
    {
        using RBFX_THIS = TileMapInfo2D;
        RBFX_USERTYPE(TileMapInfo2D, sol::no_constructor
            RBFX_M(GetMapWidth)
            RBFX_M(GetMapHeight)
            RBFX_RAW(orientation, sol::readonly_property([](TileMapInfo2D* info) {
                return info ? static_cast<int>(info->orientation_) : 0;
            }))
            RBFX_PROP_R(width, int, width_)
            RBFX_PROP_R(height, int, height_)
            RBFX_PROP_R(tileWidth, float, tileWidth_)
            RBFX_PROP_R(tileHeight, float, tileHeight_)
        );
    }

    {
        using RBFX_THIS = TileMap2D;
        RBFX_USERTYPE(TileMap2D, sol::no_constructor
            // rbfx simplified the tile map classes to derive from Component
            // directly (upstream Urho3D used Drawable2D).
            RBFX_BASES(Component, Serializable, Object)
            RBFX_M(SetTmxFile)
            RBFX_RAW(GetInfo, [](TileMap2D* tileMap) -> TileMapInfo2D* {
                return tileMap ? const_cast<TileMapInfo2D*>(&tileMap->GetInfo()) : nullptr;
            })
            RBFX_M(GetNumLayers)
            RBFX_M(GetLayer)
            RBFX_M(TileIndexToPosition)
            // Escape hatch (literal entry, leading comma): the lambda body declares
            // two locals in one statement, and a comma at brace level would split
            // the macro arguments -- the preprocessor only respects parentheses.
            , "PositionToTileIndex", [](TileMap2D* tileMap, const Vector2& position) {
                int x = 0, y = 0;
                const bool ok = tileMap && tileMap->PositionToTileIndex(x, y, position);
                return std::make_tuple(x, y, ok);
            }
            // Debug overlay of the tile grid (36_Urho2DTileMap).
            RBFX_RAW(DrawDebugGeometry, static_cast<void (TileMap2D::*)(DebugRenderer*, bool)>(&TileMap2D::DrawDebugGeometry))
        );
    }
    RegisterLuaObjectWrapper<TileMap2D>();

    // TileMap layer: tiles, objects or image (36_Urho2DTileMap, 49/50).
    {
        using RBFX_THIS = TileMapLayer2D;
        RBFX_USERTYPE(TileMapLayer2D, sol::no_constructor
            RBFX_BASES(Component, Serializable, Object)
            RBFX_M(GetTileMap)
            RBFX_M(SetVisible)
            RBFX_M(IsVisible)
            RBFX_M(HasProperty)
            RBFX_RAW(GetProperty, [](TileMapLayer2D* layer, const char* name) -> const char* {
                static thread_local ea::string value;
                value = layer ? layer->GetProperty(name) : "";
                return value.c_str();
            })
            RBFX_M(GetLayerType)
            RBFX_M(GetWidth)
            RBFX_M(GetHeight)
            RBFX_M(GetTileNode)
            RBFX_M(GetTile)
            RBFX_M(GetNumObjects)
            RBFX_M(GetObject)
            RBFX_M(GetObjectNode)
            RBFX_M(GetImageNode)
        );
    }
    RegisterLuaObjectWrapper<TileMapLayer2D>();

    // Single object inside an object group layer.
    {
        using RBFX_THIS = TileMapObject2D;
        RBFX_USERTYPE(TileMapObject2D, sol::no_constructor
            RBFX_M(GetObjectType)
            RBFX_M(GetName)
            RBFX_M(GetType)
            RBFX_M(GetPosition)
            RBFX_M(GetSize)
            RBFX_M(GetNumPoints)
            RBFX_M(GetPoint)
            RBFX_M(GetTileGid)
            RBFX_M(GetTileSprite)
            RBFX_RAW(GetProperty, [](TileMapObject2D* object, const char* name) -> const char* {
                static thread_local ea::string value;
                value = object ? object->GetProperty(name) : "";
                return value.c_str();
            })
            RBFX_RAW(HasProperty, [](TileMapObject2D* object, const char* name) -> bool {
                return object && object->HasProperty(name);
            })
        );
    }
    // TileMapObject2D is plain RefCounted, not an Object: no caster.

    // Single tile inside a tile layer.
    {
        using RBFX_THIS = Tile2D;
        RBFX_USERTYPE(Tile2D, sol::no_constructor
            RBFX_M(GetGid)
            RBFX_M(GetFlipX)
            RBFX_M(GetFlipY)
            RBFX_M(GetSprite)
            RBFX_RAW(GetProperty, [](Tile2D* tile, const char* name) -> const char* {
                static thread_local ea::string value;
                value = tile ? tile->GetProperty(name) : "";
                return value.c_str();
            })
        );
    }
    // Tile2D is plain RefCounted, not an Object: no caster.

    // --- Physics2D ---

    {
        using RBFX_THIS = RigidBody2D;
        RBFX_USERTYPE(RigidBody2D, sol::no_constructor
            RBFX_BASES(Component, Serializable, Object)
            RBFX_RAW(SetBodyType, [](RigidBody2D* body, int type) {
                if (body)
                    body->SetBodyType(static_cast<BodyType2D>(type));
            })
            RBFX_M(SetMass)
            RBFX_M(SetUseFixtureMass)
            RBFX_M(SetLinearDamping)
            RBFX_M(SetAngularDamping)
            RBFX_M(SetAllowSleep)
            RBFX_M(SetFixedRotation)
            RBFX_M(SetBullet)
            RBFX_M(SetGravityScale)
            RBFX_M(SetAwake)
            RBFX_M(SetLinearVelocity)
            RBFX_M(GetLinearVelocity)
            RBFX_M(SetAngularVelocity)
            RBFX_M(ApplyForce)
            RBFX_RAW(ApplyForceToCenter, [](RigidBody2D* body, const Vector2& force, sol::optional<bool> wake) {
                if (body)
                    body->ApplyForceToCenter(force, wake.value_or(true));
            })
            RBFX_M(ApplyTorque)
            RBFX_M(ApplyAngularImpulse)
            RBFX_RAW(ApplyLinearImpulse, [](RigidBody2D* body, const Vector2& impulse, const Vector2& point, sol::optional<bool> wake) {
                if (body)
                    body->ApplyLinearImpulse(impulse, point, wake.value_or(true));
            })
            RBFX_RAW(ApplyLinearImpulseToCenter, [](RigidBody2D* body, const Vector2& impulse, sol::optional<bool> wake) {
                if (body)
                    body->ApplyLinearImpulseToCenter(impulse, wake.value_or(true));
            })
            RBFX_M(SetInertia)
            RBFX_M(SetMassCenter)
            RBFX_M(GetMass)
            RBFX_M(GetMassCenter)
            RBFX_RAW(GetBodyType, [](RigidBody2D* body) {
                return body ? static_cast<int>(body->GetBodyType()) : 0;
            })
        );
    }
    RegisterLuaObjectWrapper<RigidBody2D>();

    {
        using RBFX_THIS = CollisionShape2D;
        RBFX_USERTYPE(CollisionShape2D, sol::no_constructor
            RBFX_BASES(Component, Serializable, Object)
            RBFX_M(SetTrigger)
            RBFX_M(SetCategoryBits)
            RBFX_M(SetMaskBits)
            RBFX_M(SetGroupIndex)
            RBFX_M(SetDensity)
            RBFX_M(SetFriction)
            RBFX_M(SetRestitution)
        );
    }
    RegisterLuaObjectWrapper<CollisionShape2D>();

    {
        using RBFX_THIS = CollisionBox2D;
        RBFX_USERTYPE(CollisionBox2D, sol::no_constructor
            RBFX_BASES(CollisionShape2D, Component, Serializable, Object)
            RBFX_OVERLOAD(SetSize,
                RBFX_CAST(SetSize, void, const Vector2&),
                RBFX_CAST(SetSize, void, float, float))
            RBFX_OVERLOAD(SetCenter,
                RBFX_CAST(SetCenter, void, const Vector2&),
                RBFX_CAST(SetCenter, void, float, float))
            RBFX_M(SetAngle)
        );
    }
    RegisterLuaObjectWrapper<CollisionBox2D>();

    {
        using RBFX_THIS = CollisionCircle2D;
        RBFX_USERTYPE(CollisionCircle2D, sol::no_constructor
            RBFX_BASES(CollisionShape2D, Component, Serializable, Object)
            RBFX_M(SetRadius)
            RBFX_OVERLOAD(SetCenter,
                RBFX_CAST(SetCenter, void, const Vector2&),
                RBFX_CAST(SetCenter, void, float, float))
        );
    }
    RegisterLuaObjectWrapper<CollisionCircle2D>();

    {
        using RBFX_THIS = PhysicsWorld2D;
        RBFX_USERTYPE(PhysicsWorld2D, sol::no_constructor
            RBFX_BASES(Component, Serializable, Object)
            RBFX_M(SetGravity)
            RBFX_M(GetGravity)
            RBFX_M(SetDrawShape)
            RBFX_M(SetDrawJoint)
            RBFX_M(SetDrawAabb)
            RBFX_M(SetDrawPair)
            RBFX_M(SetDrawCenterOfMass)
            RBFX_M(SetAllowSleeping)
            RBFX_M(SetSubStepping)
            RBFX_M(SetAutoClearForces)
            RBFX_M(SetVelocityIterations)
            RBFX_M(SetPositionIterations)
            RBFX_RAW(DrawDebugGeometry, [](PhysicsWorld2D* world) {
                if (world)
                    world->DrawDebugGeometry();
            })
            // Screen-space and world-space rigid body picking (32_Physics2DConstraints).
            RBFX_OVERLOAD(GetRigidBody,
                [](PhysicsWorld2D* world, int screenX, int screenY, sol::optional<unsigned> collisionMask) {
                    return world ? world->GetRigidBody(screenX, screenY, collisionMask.value_or(M_MAX_UNSIGNED)) : nullptr;
                },
                [](PhysicsWorld2D* world, const Vector2& point, sol::optional<unsigned> collisionMask) {
                    return world ? world->GetRigidBody(point, collisionMask.value_or(M_MAX_UNSIGNED)) : nullptr;
                })
            // AABB query returning all bodies in a Rect (50_Urho2DPlatformer
            // character ground checks).
            RBFX_RAW(GetRigidBodies, [](PhysicsWorld2D* world, const Rect& aabb, sol::optional<unsigned> collisionMask,
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
            })
        );
    }
    RegisterLuaObjectWrapper<PhysicsWorld2D>();

    // Box2D body type constants.
    RBFX_ENUM_TABLE(BT2D, "STATIC", BT_STATIC, "DYNAMIC", BT_DYNAMIC, "KINEMATIC", BT_KINEMATIC);

    // Tile map orientation constants (TileMapDefs2D.h).
    RBFX_ENUM_TABLE(ORIENT2D, "ORTHOGONAL", O_ORTHOGONAL, "ISOMETRIC", O_ISOMETRIC,
        "STAGGERED", O_STAGGERED, "HEXAGONAL", O_HEXAGONAL);

    // Tile map layer type constants (TileMapLayer2D:GetLayerType).
    RBFX_ENUM_TABLE(LT2D, "TILE_LAYER", LT_TILE_LAYER, "OBJECT_GROUP", LT_OBJECT_GROUP, "IMAGE_LAYER", LT_IMAGE_LAYER);

    // Tile map object type constants (Sample2D collision shape factory).
    RBFX_ENUM_TABLE(OT2D, "RECTANGLE", OT_RECTANGLE, "ELLIPSE", OT_ELLIPSE, "POLYGON", OT_POLYGON,
        "POLYLINE", OT_POLYLINE, "TILE", OT_TILE);
}

} // namespace Urho3D

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
        using LUA_THIS = Drawable2D;
        LUA_CLASS(Drawable2D, sol::no_constructor
            LUA_BASES(Drawable, Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetLayer)
            LUA_MEMBER_FUNC(SetOrderInLayer)
        );
    }
    RegisterLuaObjectWrapper<Drawable2D>();

    // Sprite resources.
    {
        using LUA_THIS = Sprite2D;
        LUA_CLASS(Sprite2D, sol::no_constructor
            LUA_BASES(Resource, Object)
        );
    }
    RegisterLuaObjectWrapper<Sprite2D>();

    {
        using LUA_THIS = AnimationSet2D;
        LUA_CLASS(AnimationSet2D, sol::no_constructor
            LUA_BASES(Resource, Object)
            LUA_MEMBER_FUNC(GetNumAnimations)
            LUA_MEMBER_FUNC(GetAnimation)
            LUA_MEMBER_FUNC_RAW(GetSprite,
                static_cast<Sprite2D* (AnimationSet2D::*)(int, int) const>(&AnimationSet2D::GetSpriterFileSprite))
        );
    }
    RegisterLuaObjectWrapper<AnimationSet2D>();

    // StaticSprite2D: the workhorse of the 2D samples.
    {
        using LUA_THIS = StaticSprite2D;
        LUA_CLASS(StaticSprite2D, sol::no_constructor
            LUA_BASES(Drawable2D, Drawable, Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetSprite)
            LUA_MEMBER_FUNC(GetSprite)
            LUA_MEMBER_FUNC(SetColor)
            LUA_MEMBER_FUNC_RAW(SetBlendMode, [](StaticSprite2D* sprite, int mode) {
                if (sprite)
                    sprite->SetBlendMode(static_cast<BlendMode>(mode));
            })
            LUA_MEMBER_FUNC_RAW(SetFlip, [](StaticSprite2D* sprite, bool flipX, bool flipY, sol::optional<bool> swapXY) {
                if (sprite)
                    sprite->SetFlip(flipX, flipY, swapXY.value_or(false));
            })
            LUA_MEMBER_FUNC(SetFlipX)
            LUA_MEMBER_FUNC(SetFlipY)
            LUA_MEMBER_FUNC(SetUseHotSpot)
            LUA_MEMBER_FUNC(SetHotSpot)
        );
    }
    RegisterLuaObjectWrapper<StaticSprite2D>();

    // AnimatedSprite2D: Spriter-driven animation playback.
    {
        using LUA_THIS = AnimatedSprite2D;
        LUA_CLASS(AnimatedSprite2D, sol::no_constructor
            // Full chain: sol3 base lookup is single-level, SetLayer lives on
            // Drawable2D (49/50 Sample2D).
            LUA_BASES(StaticSprite2D, Drawable2D, Drawable, Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetAnimationSet)
            LUA_MEMBER_FUNC(GetAnimationSet)
            LUA_MEMBER_FUNC_RAW(SetAnimation, [](AnimatedSprite2D* sprite, const char* name, sol::optional<bool> looped) {
                if (sprite)
                    sprite->SetAnimation(name, looped.value_or(true) ? LM_FORCE_LOOPED : LM_FORCE_CLAMPED);
            })
            // Current animation name; compare or feed back into SetAnimation
            // (33_SpriterAnimation index switching).
            LUA_MEMBER_FUNC_RAW(GetAnimation, [](AnimatedSprite2D* sprite) {
                return sprite ? sprite->GetAnimation().c_str() : "";
            })
            LUA_MEMBER_FUNC(SetSpeed)
            LUA_MEMBER_FUNC(SetFlipX)
            LUA_MEMBER_FUNC(SetFlipY)
            LUA_MEMBER_FUNC(GetFlipX)
            LUA_MEMBER_FUNC(GetFlipY)
        );
    }
    RegisterLuaObjectWrapper<AnimatedSprite2D>();

    // StretchableSprite2D: 9-slice sprite (51_Urho2DStretchableSprite).
    {
        using LUA_THIS = StretchableSprite2D;
        LUA_CLASS(StretchableSprite2D, sol::no_constructor
            LUA_BASES(StaticSprite2D, Drawable2D, Drawable, Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetBorder)
        );
    }
    RegisterLuaObjectWrapper<StretchableSprite2D>();

    // ParticleEmitter2D: sprite particles from a ParticleEffect2D resource
    // (25_Urho2DParticle).
    {
        using LUA_THIS = ParticleEffect2D;
        LUA_CLASS(ParticleEffect2D, sol::no_constructor
            LUA_BASES(Resource, Object)
        );
    }
    RegisterLuaObjectWrapper<ParticleEffect2D>();

    {
        using LUA_THIS = ParticleEmitter2D;
        LUA_CLASS(ParticleEmitter2D, sol::no_constructor
            LUA_BASES(Drawable2D, Drawable, Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetEffect)
            LUA_MEMBER_FUNC(GetEffect)
            LUA_MEMBER_FUNC(SetSprite)
            LUA_MEMBER_FUNC(SetEmitting)
            LUA_MEMBER_FUNC(IsEmitting)
        );
    }
    RegisterLuaObjectWrapper<ParticleEmitter2D>();

    // Tile maps (39_TileMap, 41_Dungeon).
    {
        using LUA_THIS = TmxFile2D;
        LUA_CLASS(TmxFile2D, sol::no_constructor
            LUA_BASES(Resource, Object)
        );
    }
    RegisterLuaObjectWrapper<TmxFile2D>();

    // Tile map header info: orientation, dimensions and tile sizes
    // (36_Urho2DTileMap, 49/50 Sample2D logic).
    {
        using LUA_THIS = TileMapInfo2D;
        LUA_CLASS(TileMapInfo2D, sol::no_constructor
            LUA_MEMBER_FUNC(GetMapWidth)
            LUA_MEMBER_FUNC(GetMapHeight)
            LUA_MEMBER_PROP_RAW(orientation, sol::readonly_property([](TileMapInfo2D* info) {
                return info ? static_cast<int>(info->orientation_) : 0;
            }))
            LUA_MEMBER_PROP_FR(width, int, width_)
            LUA_MEMBER_PROP_FR(height, int, height_)
            LUA_MEMBER_PROP_FR(tileWidth, float, tileWidth_)
            LUA_MEMBER_PROP_FR(tileHeight, float, tileHeight_)
        );
    }

    {
        using LUA_THIS = TileMap2D;
        LUA_CLASS(TileMap2D, sol::no_constructor
            // rbfx simplified the tile map classes to derive from Component
            // directly (upstream Urho3D used Drawable2D).
            LUA_BASES(Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetTmxFile)
            LUA_MEMBER_FUNC_RAW(GetInfo, [](TileMap2D* tileMap) -> TileMapInfo2D* {
                return tileMap ? const_cast<TileMapInfo2D*>(&tileMap->GetInfo()) : nullptr;
            })
            LUA_MEMBER_FUNC(GetNumLayers)
            LUA_MEMBER_FUNC(GetLayer)
            LUA_MEMBER_FUNC(TileIndexToPosition)
            // Escape hatch (literal entry, leading comma): the lambda body declares
            // two locals in one statement, and a comma at brace level would split
            // the macro arguments -- the preprocessor only respects parentheses.
            , "PositionToTileIndex", [](TileMap2D* tileMap, const Vector2& position) {
                int x = 0, y = 0;
                const bool ok = tileMap && tileMap->PositionToTileIndex(x, y, position);
                return std::make_tuple(x, y, ok);
            }
            // Debug overlay of the tile grid (36_Urho2DTileMap).
            LUA_MEMBER_FUNC_RAW(DrawDebugGeometry, static_cast<void (TileMap2D::*)(DebugRenderer*, bool)>(&TileMap2D::DrawDebugGeometry))
        );
    }
    RegisterLuaObjectWrapper<TileMap2D>();

    // TileMap layer: tiles, objects or image (36_Urho2DTileMap, 49/50).
    {
        using LUA_THIS = TileMapLayer2D;
        LUA_CLASS(TileMapLayer2D, sol::no_constructor
            LUA_BASES(Component, Serializable, Object)
            LUA_MEMBER_FUNC(GetTileMap)
            LUA_MEMBER_FUNC(SetVisible)
            LUA_MEMBER_FUNC(IsVisible)
            LUA_MEMBER_FUNC(HasProperty)
            LUA_MEMBER_FUNC_RAW(GetProperty, [](TileMapLayer2D* layer, const char* name) -> const char* {
                static thread_local ea::string value;
                value = layer ? layer->GetProperty(name) : "";
                return value.c_str();
            })
            LUA_MEMBER_FUNC(GetLayerType)
            LUA_MEMBER_FUNC(GetWidth)
            LUA_MEMBER_FUNC(GetHeight)
            LUA_MEMBER_FUNC(GetTileNode)
            LUA_MEMBER_FUNC(GetTile)
            LUA_MEMBER_FUNC(GetNumObjects)
            LUA_MEMBER_FUNC(GetObject)
            LUA_MEMBER_FUNC(GetObjectNode)
            LUA_MEMBER_FUNC(GetImageNode)
        );
    }
    RegisterLuaObjectWrapper<TileMapLayer2D>();

    // Single object inside an object group layer.
    {
        using LUA_THIS = TileMapObject2D;
        LUA_CLASS(TileMapObject2D, sol::no_constructor
            LUA_MEMBER_FUNC(GetObjectType)
            LUA_MEMBER_FUNC(GetName)
            LUA_MEMBER_FUNC(GetType)
            LUA_MEMBER_FUNC(GetPosition)
            LUA_MEMBER_FUNC(GetSize)
            LUA_MEMBER_FUNC(GetNumPoints)
            LUA_MEMBER_FUNC(GetPoint)
            LUA_MEMBER_FUNC(GetTileGid)
            LUA_MEMBER_FUNC(GetTileSprite)
            LUA_MEMBER_FUNC_RAW(GetProperty, [](TileMapObject2D* object, const char* name) -> const char* {
                static thread_local ea::string value;
                value = object ? object->GetProperty(name) : "";
                return value.c_str();
            })
            LUA_MEMBER_FUNC_RAW(HasProperty, [](TileMapObject2D* object, const char* name) -> bool {
                return object && object->HasProperty(name);
            })
        );
    }
    // TileMapObject2D is plain RefCounted, not an Object: no caster.

    // Single tile inside a tile layer.
    {
        using LUA_THIS = Tile2D;
        LUA_CLASS(Tile2D, sol::no_constructor
            LUA_MEMBER_FUNC(GetGid)
            LUA_MEMBER_FUNC(GetFlipX)
            LUA_MEMBER_FUNC(GetFlipY)
            LUA_MEMBER_FUNC(GetSprite)
            LUA_MEMBER_FUNC_RAW(GetProperty, [](Tile2D* tile, const char* name) -> const char* {
                static thread_local ea::string value;
                value = tile ? tile->GetProperty(name) : "";
                return value.c_str();
            })
        );
    }
    // Tile2D is plain RefCounted, not an Object: no caster.

    // --- Physics2D ---

    {
        using LUA_THIS = RigidBody2D;
        LUA_CLASS(RigidBody2D, sol::no_constructor
            LUA_BASES(Component, Serializable, Object)
            LUA_MEMBER_FUNC_RAW(SetBodyType, [](RigidBody2D* body, int type) {
                if (body)
                    body->SetBodyType(static_cast<BodyType2D>(type));
            })
            LUA_MEMBER_FUNC(SetMass)
            LUA_MEMBER_FUNC(SetUseFixtureMass)
            LUA_MEMBER_FUNC(SetLinearDamping)
            LUA_MEMBER_FUNC(SetAngularDamping)
            LUA_MEMBER_FUNC(SetAllowSleep)
            LUA_MEMBER_FUNC(SetFixedRotation)
            LUA_MEMBER_FUNC(SetBullet)
            LUA_MEMBER_FUNC(SetGravityScale)
            LUA_MEMBER_FUNC(SetAwake)
            LUA_MEMBER_FUNC(SetLinearVelocity)
            LUA_MEMBER_FUNC(GetLinearVelocity)
            LUA_MEMBER_FUNC(SetAngularVelocity)
            LUA_MEMBER_FUNC(ApplyForce)
            LUA_MEMBER_FUNC_RAW(ApplyForceToCenter, [](RigidBody2D* body, const Vector2& force, sol::optional<bool> wake) {
                if (body)
                    body->ApplyForceToCenter(force, wake.value_or(true));
            })
            LUA_MEMBER_FUNC(ApplyTorque)
            LUA_MEMBER_FUNC(ApplyAngularImpulse)
            LUA_MEMBER_FUNC_RAW(ApplyLinearImpulse, [](RigidBody2D* body, const Vector2& impulse, const Vector2& point, sol::optional<bool> wake) {
                if (body)
                    body->ApplyLinearImpulse(impulse, point, wake.value_or(true));
            })
            LUA_MEMBER_FUNC_RAW(ApplyLinearImpulseToCenter, [](RigidBody2D* body, const Vector2& impulse, sol::optional<bool> wake) {
                if (body)
                    body->ApplyLinearImpulseToCenter(impulse, wake.value_or(true));
            })
            LUA_MEMBER_FUNC(SetInertia)
            LUA_MEMBER_FUNC(SetMassCenter)
            LUA_MEMBER_FUNC(GetMass)
            LUA_MEMBER_FUNC(GetMassCenter)
            LUA_MEMBER_FUNC_RAW(GetBodyType, [](RigidBody2D* body) {
                return body ? static_cast<int>(body->GetBodyType()) : 0;
            })
        );
    }
    RegisterLuaObjectWrapper<RigidBody2D>();

    {
        using LUA_THIS = CollisionShape2D;
        LUA_CLASS(CollisionShape2D, sol::no_constructor
            LUA_BASES(Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetTrigger)
            LUA_MEMBER_FUNC(SetCategoryBits)
            LUA_MEMBER_FUNC(SetMaskBits)
            LUA_MEMBER_FUNC(SetGroupIndex)
            LUA_MEMBER_FUNC(SetDensity)
            LUA_MEMBER_FUNC(SetFriction)
            LUA_MEMBER_FUNC(SetRestitution)
        );
    }
    RegisterLuaObjectWrapper<CollisionShape2D>();

    {
        using LUA_THIS = CollisionBox2D;
        LUA_CLASS(CollisionBox2D, sol::no_constructor
            LUA_BASES(CollisionShape2D, Component, Serializable, Object)
            LUA_MEMBER_FUNC_OVERLOAD(SetSize,
                LUA_CAST(SetSize, void, const Vector2&),
                LUA_CAST(SetSize, void, float, float))
            LUA_MEMBER_FUNC_OVERLOAD(SetCenter,
                LUA_CAST(SetCenter, void, const Vector2&),
                LUA_CAST(SetCenter, void, float, float))
            LUA_MEMBER_FUNC(SetAngle)
        );
    }
    RegisterLuaObjectWrapper<CollisionBox2D>();

    {
        using LUA_THIS = CollisionCircle2D;
        LUA_CLASS(CollisionCircle2D, sol::no_constructor
            LUA_BASES(CollisionShape2D, Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetRadius)
            LUA_MEMBER_FUNC_OVERLOAD(SetCenter,
                LUA_CAST(SetCenter, void, const Vector2&),
                LUA_CAST(SetCenter, void, float, float))
        );
    }
    RegisterLuaObjectWrapper<CollisionCircle2D>();

    {
        using LUA_THIS = PhysicsWorld2D;
        LUA_CLASS(PhysicsWorld2D, sol::no_constructor
            LUA_BASES(Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetGravity)
            LUA_MEMBER_FUNC(GetGravity)
            LUA_MEMBER_FUNC(SetDrawShape)
            LUA_MEMBER_FUNC(SetDrawJoint)
            LUA_MEMBER_FUNC(SetDrawAabb)
            LUA_MEMBER_FUNC(SetDrawPair)
            LUA_MEMBER_FUNC(SetDrawCenterOfMass)
            LUA_MEMBER_FUNC(SetAllowSleeping)
            LUA_MEMBER_FUNC(SetSubStepping)
            LUA_MEMBER_FUNC(SetAutoClearForces)
            LUA_MEMBER_FUNC(SetVelocityIterations)
            LUA_MEMBER_FUNC(SetPositionIterations)
            LUA_MEMBER_FUNC_RAW(DrawDebugGeometry, [](PhysicsWorld2D* world) {
                if (world)
                    world->DrawDebugGeometry();
            })
            // Screen-space and world-space rigid body picking (32_Physics2DConstraints).
            LUA_MEMBER_FUNC_OVERLOAD(GetRigidBody,
                [](PhysicsWorld2D* world, int screenX, int screenY, sol::optional<unsigned> collisionMask) {
                    return world ? world->GetRigidBody(screenX, screenY, collisionMask.value_or(M_MAX_UNSIGNED)) : nullptr;
                },
                [](PhysicsWorld2D* world, const Vector2& point, sol::optional<unsigned> collisionMask) {
                    return world ? world->GetRigidBody(point, collisionMask.value_or(M_MAX_UNSIGNED)) : nullptr;
                })
            // AABB query returning all bodies in a Rect (50_Urho2DPlatformer
            // character ground checks).
            LUA_MEMBER_FUNC_RAW(GetRigidBodies, [](PhysicsWorld2D* world, const Rect& aabb, sol::optional<unsigned> collisionMask,
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
    LUA_ENUM_TABLE(BT2D, "STATIC", BT_STATIC, "DYNAMIC", BT_DYNAMIC, "KINEMATIC", BT_KINEMATIC);

    // Tile map orientation constants (TileMapDefs2D.h).
    LUA_ENUM_TABLE(ORIENT2D, "ORTHOGONAL", O_ORTHOGONAL, "ISOMETRIC", O_ISOMETRIC,
        "STAGGERED", O_STAGGERED, "HEXAGONAL", O_HEXAGONAL);

    // Tile map layer type constants (TileMapLayer2D:GetLayerType).
    LUA_ENUM_TABLE(LT2D, "TILE_LAYER", LT_TILE_LAYER, "OBJECT_GROUP", LT_OBJECT_GROUP, "IMAGE_LAYER", LT_IMAGE_LAYER);

    // Tile map object type constants (Sample2D collision shape factory).
    LUA_ENUM_TABLE(OT2D, "RECTANGLE", OT_RECTANGLE, "ELLIPSE", OT_ELLIPSE, "POLYGON", OT_POLYGON,
        "POLYLINE", OT_POLYLINE, "TILE", OT_TILE);
}

} // namespace Urho3D

//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#include "../Urho3D/Precompiled.h"

#include "LuaNodeBindings.h"

#include "LuaBindings.h"
#include "LuaBindHelpers.h"
#include "LuaBindMacros.h"

#include "../Urho3D/Core/Context.h"
#include "../Urho3D/IO/Log.h"
#include "../Urho3D/IO/VectorBuffer.h"
#include "../Urho3D/Resource/ResourceCache.h"
#include "../Urho3D/Resource/XMLFile.h"
#include "../Urho3D/Scene/Component.h"
#include "../Urho3D/Scene/Node.h"
#include "../Urho3D/Scene/PrefabReference.h"
#include "../Urho3D/Scene/Scene.h"
#include "../Urho3D/Scene/ValueAnimation.h"

#include <sol/sol.hpp>

namespace sol
{

// RefCounted scene objects without value semantics: register behavior explicitly.
template <> struct is_automagical<Urho3D::Node> : std::false_type {};
template <> struct is_automagical<Urho3D::Component> : std::false_type {};
template <> struct is_automagical<Urho3D::Scene> : std::false_type {};
template <> struct is_automagical<Urho3D::PrefabReference> : std::false_type {};

} // namespace sol

namespace Urho3D
{

namespace
{

// Identity comparison for RefCounted scene objects.
inline bool NodeEquals(const Node& a, const Node& b) { return &a == &b; }

template <typename T>
sol::table WrapObjectVector(sol::state_view lua, const ea::vector<T*>& objects)
{
    sol::table result = lua.create_table(static_cast<unsigned>(objects.size()), 0);
    for (unsigned i = 0; i < objects.size(); ++i)
        result[i + 1] = WrapLuaObject(lua, objects[i]);
    return result;
}

} // namespace

void RegisterNodeBindings(sol::state& lua, Context* context)
{
    {
        using RBFX_THIS = Node;
        RBFX_USERTYPE(Node, sol::no_constructor
            // Full chain to Object: sol3 type casts (e.g. event senders passed
            // as Object*) only match the directly declared bases.
            RBFX_BASES(Serializable, Object)
            RBFX_META(equal_to, [](Node* a, Node* b) { return a == b; })
            RBFX_META(to_string, [](Node* node) -> std::string {
                return "Node: " + std::string(node->GetName().c_str());
            })

            // Identification
            RBFX_PROP(name, std::string, GetName, SetName)
            RBFX_PROP_R(id, unsigned, GetID)

            // Local transform
            RBFX_PROP(position, Vector3, GetPosition, SetPosition)
            RBFX_PROP(rotation, Quaternion, GetRotation, SetRotation)
            // Transform-adapted property (RollAngle / single-float Quaternion ctor).
            RBFX_RAW(rotation2D, sol::property(
                [](Node* node) -> float { return node ? node->GetRotation().RollAngle() : 0.0f; },
                [](Node* node, float value) { if (node) node->SetRotation(Quaternion(value)); }))
            // SetScale is overloaded engine-side, so the setter method is a separate
            // RBFX_OVERLOAD and the property stays a plain forwarding one.
            RBFX_RAW(scale, sol::property(
                [](Node* node) -> Vector3 { return node ? node->GetScale() : Vector3::ONE; },
                [](Node* node, const Vector3& value) { if (node) node->SetScale(value); }))
            RBFX_M_RET(GetPosition2D, Vector2)
            RBFX_M_RET(GetWorldPosition2D, Vector2)
            RBFX_M_RET(GetScale, Vector3)
            RBFX_OVERLOAD(SetScale,
                RBFX_CAST(SetScale, void, const Vector3&),
                RBFX_CAST(SetScale, void, float))
            RBFX_M(SetDirection)
            RBFX_OVERLOAD(SetTransform,
                RBFX_CAST(SetTransform, void, const Vector3&, const Quaternion&),
                RBFX_CAST(SetTransform, void, const Vector3&, const Quaternion&, const Vector3&))
            RBFX_RAW(SetTransform2D, [](Node* node, const Vector2& position, float rotation) { if (node) node->SetTransform2D(position, rotation); })
            // 2D-transform helpers (49/50_Urho2D, 51_StretchableSprite).
            RBFX_RAW(SetPosition2D, [](Node* node, const Vector2& position) { if (node) node->SetPosition2D(position); })
            RBFX_RAW(Translate2D, [](Node* node, const Vector2& delta, sol::optional<int> space) {
                if (node) node->Translate2D(delta, static_cast<TransformSpace>(space.value_or(TS_LOCAL))); })
            RBFX_RAW(Scale2D, [](Node* node, const Vector2& scale) { if (node) node->Scale2D(scale); })
            RBFX_OVERLOAD(SetScale2D,
                RBFX_CAST(SetScale2D, void, const Vector2&),
                RBFX_CAST(SetScale2D, void, float, float))

            // World transform. worldPosition/worldRotation stay literal: their
            // getter methods are bare member pointers (no ---@return in the stubs),
            // which RBFX_PROP's synthesized arrow would drift.
            RBFX_RAW(worldPosition, sol::property(
                [](Node* node) -> Vector3 { return node ? node->GetWorldPosition() : Vector3::ZERO; },
                [](Node* node, const Vector3& value) { if (node) node->SetWorldPosition(value); }))
            RBFX_RAW(worldRotation, sol::property(
                [](Node* node) -> Quaternion { return node ? node->GetWorldRotation() : Quaternion::IDENTITY; },
                [](Node* node, const Quaternion& value) { if (node) node->SetWorldRotation(value); }))
            RBFX_PROP_R(worldScale, Vector3, GetWorldScale)
            RBFX_PROP_R(worldDirection, Vector3, GetWorldDirection)
            RBFX_PROP_R(direction, Vector3, GetDirection)
            RBFX_M(SetWorldPosition)
            RBFX_M(SetWorldRotation)
            RBFX_RAW(LocalToWorld, [](Node* node, const Vector3& position) -> Vector3 {
                return node ? node->LocalToWorld(position) : Vector3::ZERO;
            })
            RBFX_RAW(WorldToLocal, [](Node* node, const Vector3& position) -> Vector3 {
                return node ? node->WorldToLocal(position) : Vector3::ZERO;
            })

            // Transform helpers. Translate/Rotate keep their lambdas: the engine
            // side is a single default-argument function, so there is no exact
            // overload to static_cast and the int space must stay a manual cast.
            RBFX_OVERLOAD(Translate,
                [](Node* node, const Vector3& delta) { if (node) node->Translate(delta); },
                [](Node* node, const Vector3& delta, int space) {
                    if (node) node->Translate(delta, static_cast<TransformSpace>(space)); })
            RBFX_OVERLOAD(Rotate,
                [](Node* node, const Quaternion& delta) { if (node) node->Rotate(delta); },
                [](Node* node, const Quaternion& delta, int space) { if (node) node->Rotate(delta, static_cast<TransformSpace>(space)); })
            RBFX_RAW(RotateAround, [](Node* node, const Vector3& point, const Quaternion& delta) { if (node) node->RotateAround(point, delta); })
            RBFX_RAW(Pitch, [](Node* node, float angle) { if (node) node->Pitch(angle); })
            RBFX_RAW(Yaw, [](Node* node, float angle) { if (node) node->Yaw(angle); })
            RBFX_RAW(Roll, [](Node* node, float angle) { if (node) node->Roll(angle); })
            RBFX_RAW(LookAt, [](Node* node, const Vector3& target, const Vector3& up, int space) {
                if (node) node->LookAt(target, up, static_cast<TransformSpace>(space)); })

            // Hierarchy
            RBFX_OBJ_R(parent, Node, GetParent)
            RBFX_M(SetParent)
            RBFX_OBJ_R(scene, Scene, GetScene)
            RBFX_RAW(numChildren, sol::readonly_property([](Node* node) -> unsigned { return node ? node->GetNumChildren() : 0; }))
            RBFX_RAW(numComponents, sol::readonly_property([](Node* node) -> unsigned { return node ? node->GetNumComponents() : 0; }))
            RBFX_OVERLOAD(CreateChild,
                [](Node* node, const char* name, sol::this_state s) -> sol::object {
                    return node ? WrapLuaObjectAs<Node>(sol::state_view(s), node->CreateChild(name)) : sol::lua_nil;
                },
                [](Node* node, sol::this_state s) -> sol::object {
                    return node ? WrapLuaObjectAs<Node>(sol::state_view(s), node->CreateChild()) : sol::lua_nil;
                })
            RBFX_RAW(AddChild, [](Node* node, Node* child) { if (node && child) node->AddChild(child); })
            RBFX_RAW(RemoveChild, [](Node* node, Node* child) { if (node) node->RemoveChild(child); })
            RBFX_M(RemoveAllChildren)
            RBFX_RAW(Remove, [](Node* node) { if (node) node->Remove(); })
            RBFX_RAW(Clone, [](Node* node, sol::this_state s) -> sol::object {
                return node ? WrapLuaObjectAs<Node>(sol::state_view(s), node->Clone()) : sol::lua_nil;
            })
            // Instantiate a PrefabResource under this node (17_SceneReplication).
            RBFX_RAW(InstantiatePrefab, [](Node* node, PrefabResource* prefab, const Vector3& position,
                const Quaternion& rotation, sol::this_state s) -> sol::object {
                return (node && prefab)
                    ? WrapLuaObjectAs<Node>(sol::state_view(s), node->InstantiatePrefab(prefab, position, rotation))
                    : sol::lua_nil;
            })
            RBFX_OVERLOAD(GetChild,
                [](Node* node, const char* name, sol::this_state s) -> sol::object {
                    return node ? WrapLuaObjectAs<Node>(sol::state_view(s), node->GetChild(StringHash(name))) : sol::lua_nil;
                },
                [](Node* node, const char* name, bool recursive, sol::this_state s) -> sol::object {
                    return node ? WrapLuaObjectAs<Node>(sol::state_view(s), node->GetChild(StringHash(name), recursive)) : sol::lua_nil;
                })
            RBFX_RAW(GetChildren, [](Node* node, sol::optional<bool> recursive, sol::this_state s) -> sol::object {
                if (!node)
                    return sol::lua_nil;
                return WrapObjectVector<Node>(sol::state_view(s), node->GetChildren(recursive.value_or(false)));
            })
            // Callback-style enumeration: visits children without allocating an
            // intermediate Lua table (the per-frame churn GetChildren incurs).
            // callback(child); return false from the callback to stop early.
            // Non-recursive iterates the engine's internal vector by reference (zero copy).
            RBFX_RAW(ForEachChild, [](Node* node, sol::protected_function callback, sol::optional<bool> recursive, sol::this_state s) {
                if (!node || !callback.valid())
                    return;
                sol::state_view lua(s);
                if (!recursive.value_or(false))
                {
                    for (const SharedPtr<Node>& child : node->GetChildren())
                    {
                        sol::protected_function_result r = callback(WrapLuaObject(lua, child.Get()));
                        if (!r.valid())
                        {
                            sol::error err = r;
                            URHO3D_LOGERROR("Node:ForEachChild callback failed: {}", err.what());
                            return;
                        }
                        if (r.get_type() == sol::type::boolean && !r.get<bool>())
                            return; // early break
                    }
                }
                else
                {
                    ea::vector<Node*> children;
                    node->GetChildren(children, true);
                    for (Node* child : children)
                    {
                        sol::protected_function_result r = callback(WrapLuaObject(lua, child));
                        if (!r.valid())
                        {
                            sol::error err = r;
                            URHO3D_LOGERROR("Node:ForEachChild callback failed: {}", err.what());
                            return;
                        }
                        if (r.get_type() == sol::type::boolean && !r.get<bool>())
                            return;
                    }
                }
            })
            // Same idea for components: callback(component); return false to stop.
            RBFX_RAW(ForEachComponent, [](Node* node, sol::protected_function callback, sol::this_state s) {
                if (!node || !callback.valid())
                    return;
                sol::state_view lua(s);
                for (const SharedPtr<Component>& component : node->GetComponents())
                {
                    sol::protected_function_result r = callback(WrapLuaObject(lua, component.Get()));
                    if (!r.valid())
                    {
                        sol::error err = r;
                        URHO3D_LOGERROR("Node:ForEachComponent callback failed: {}", err.what());
                        return;
                    }
                    if (r.get_type() == sol::type::boolean && !r.get<bool>())
                        return;
                }
            })

            // Components: string-typed factory channel. Any component type
            // registered in the Context is creatable from Lua.
            RBFX_RAW(CreateComponent, [context](Node* node, const char* typeName, sol::this_state s) -> sol::object {
                if (!node)
                    return sol::lua_nil;
                Component* component = node->CreateComponent(StringHash(typeName));
                if (!component)
                {
                    URHO3D_LOGERROR("Component type '{}' is not registered", typeName);
                    return sol::lua_nil;
                }
                return WrapLuaObject(sol::state_view(s), component);
            })
            RBFX_RAW(GetOrCreateComponent, [context](Node* node, const char* typeName, sol::this_state s) -> sol::object {
                if (!node)
                    return sol::lua_nil;
                Component* component = node->GetOrCreateComponent(StringHash(typeName));
                if (!component)
                {
                    URHO3D_LOGERROR("Component type '{}' is not registered", typeName);
                    return sol::lua_nil;
                }
                return WrapLuaObject(sol::state_view(s), component);
            })
            RBFX_RAW(GetComponent, [context](Node* node, const char* typeName, sol::this_state s) -> sol::object {
                if (!node)
                    return sol::lua_nil;
                return WrapLuaObject(sol::state_view(s), node->GetComponent(StringHash(typeName)));
            })
            RBFX_RAW(RemoveComponent, [context](Node* node, const char* typeName) {
                if (node)
                    node->RemoveComponent(StringHash(typeName));
            })
            RBFX_RAW(HasComponent, [context](Node* node, const char* typeName) -> bool {
                return node && node->HasComponent(StringHash(typeName));
            })
            RBFX_RAW(GetComponents, [context](Node* node, const char* typeName, sol::this_state s) -> sol::object {
                if (!node)
                    return sol::lua_nil;
                ea::vector<Component*> components;
                node->GetComponents(components, StringHash(typeName));
                return WrapObjectVector<Component>(sol::state_view(s), components);
            })
            RBFX_RAW(GetChildrenWithComponent, [context](Node* node, const char* typeName, sol::optional<bool> recursive, sol::this_state s) -> sol::object {
                if (!node)
                    return sol::lua_nil;
                ea::vector<Node*> children;
                node->GetChildrenWithComponent(children, StringHash(typeName), recursive.value_or(false));
                return WrapObjectVector<Node>(sol::state_view(s), children);
            })

            // State
            RBFX_RAW(enabled, sol::property(
                [](Node* node) -> bool { return node && node->IsEnabled(); },
                [](Node* node, bool enabled) { if (node) node->SetEnabled(enabled); }))
            RBFX_RAW(SetEnabled, [](Node* node, bool enabled) { if (node) node->SetEnabled(enabled); })

            // User variables
            RBFX_RAW(SetVar, [](Node* node, const char* key, sol::object value, sol::this_state s) {
                if (node) node->SetVar(key, LuaToVariant(sol::state_view(s), value));
            })
            RBFX_RAW(GetVar, [](Node* node, const char* key, sol::this_state s) -> sol::object {
                return node ? VariantToLua(sol::state_view(s), node->GetVar(key)) : sol::lua_nil;
            })

            // Tags (used by several samples to group nodes)
            RBFX_M(AddTag)
            RBFX_M(HasTag)
            RBFX_RAW(GetNumChildren, [](Node* node, sol::optional<bool> recursive) {
                return node ? node->GetNumChildren(recursive.value_or(false)) : 0u;
            })
            RBFX_RAW(GetChildrenWithTag, [](Node* node, const char* tag, sol::this_state s) -> sol::table {
                sol::state_view lua(s);
                sol::table result = lua.create_table();
                if (node)
                {
                    unsigned index = 1;
                    for (Node* child : node->GetChildrenWithTag(tag))
                        result[index++] = WrapLuaObject(lua, child);
                }
                return result;
            })

            // World-space transform queries
            RBFX_M(GetWorldPosition)
            RBFX_M(GetWorldRotation)
            RBFX_M(GetWorldDirection)
            RBFX_M(GetWorldUp)
            RBFX_M(GetWorldRight)
            RBFX_M(SetTemporary)
        );
    }

    {
        using RBFX_THIS = Scene;
        RBFX_USERTYPE(Scene, sol::no_constructor
            RBFX_BASES(Node, Serializable, Object)

            // Instantiate a prefab XML resource under the scene.
            RBFX_RAW(InstantiateXML, [context](Scene* scene, const char* resourceName, const Vector3& position, const Quaternion& rotation, sol::this_state s) -> sol::object {
                if (!scene)
                    return sol::lua_nil;
                auto* cache = context->GetSubsystem<ResourceCache>();
                XMLFile* xml = cache->GetResource<XMLFile>(resourceName);
                if (!xml)
                    return sol::lua_nil;
                return WrapLuaObject(sol::state_view(s), scene->InstantiateXML(xml->GetRoot(), position, rotation));
            })
            RBFX_RAW(LoadXML, [context](Scene* scene, const char* resourceName) -> bool {
                if (!scene)
                    return false;
                auto* cache = context->GetSubsystem<ResourceCache>();
                XMLFile* xml = cache->GetResource<XMLFile>(resourceName);
                return xml && scene->LoadXML(xml->GetRoot());
            })
            RBFX_RAW(GetChildByIndex, [](Scene* scene, unsigned index, sol::this_state s) -> sol::object {
                return scene ? WrapLuaObjectAs<Node>(sol::state_view(s), scene->GetChild(index)) : sol::lua_nil;
            })
            // In-memory save/load used by the 2D samples' reload feature
            // (49_Urho2DIsometricDemo).
            RBFX_RAW(Save, [](Scene* scene, VectorBuffer* dest) -> bool {
                return scene && dest ? scene->Save(*dest) : false;
            })
            RBFX_RAW(Load, [](Scene* scene, VectorBuffer* source) -> bool {
                return scene && source ? scene->Load(*source) : false;
            })
            RBFX_M(SetUpdateEnabled)
            RBFX_M(IsUpdateEnabled)
            RBFX_M(SetTimeScale)
            RBFX_M(GetTimeScale)
            RBFX_RAW(GetWorldOrigin, [](Scene* scene) { return scene ? scene->GetWorldOrigin() : IntVector3::ZERO; })
            // Remove all replicated content and reset the scene
            // (20_HugeObjectCount rebuild).
            RBFX_M(Clear)
        );
    }

    {
        using RBFX_THIS = Component;
        RBFX_USERTYPE(Component, sol::no_constructor
            RBFX_BASES(Serializable, Object)
            RBFX_META(equal_to, [](Component* a, Component* b) { return a == b; })
            RBFX_META(to_string, [](Component* component) -> std::string {
                return "Component: " + std::string(component->GetTypeName().c_str());
            })
            RBFX_OBJ_R(node, Node, GetNode)
            RBFX_OBJ_M(GetNode, Node, GetNode)
            RBFX_OBJ_R(scene, Scene, GetScene)
            RBFX_RAW(id, sol::readonly_property(&Component::GetID))
            RBFX_RAW(enabled, sol::property(
                &Component::IsEnabled,
                [](Component* component, bool enabled) { if (component) component->SetEnabled(enabled); }))
            RBFX_RAW(SetEnabled, [](Component* component, bool enabled) { if (component) component->SetEnabled(enabled); })
            RBFX_M(IsEnabledEffective)
            // Remove from the node and destroy (49/50 orc body removal).
            RBFX_RAW(Remove, [](Component* component) { if (component) component->Remove(); })
        );
    }

    {
        using RBFX_THIS = PrefabReference;
        RBFX_USERTYPE(PrefabReference, sol::no_constructor
            RBFX_BASES(Component, Serializable, Object)
            RBFX_RAW(SetPrefab, [context](PrefabReference* prefab, const char* resourceName) {
                if (!prefab)
                    return;
                auto* cache = context->GetSubsystem<ResourceCache>();
                PrefabResource* resource = cache->GetResource<PrefabResource>(resourceName);
                prefab->SetPrefab(resource);
            })
            // Inline the prefab into the scene, discarding the reference
            // (18_CharacterDemo's sliding door).
            RBFX_RAW(InlineAggressive, [](PrefabReference* prefab) {
                if (prefab)
                    prefab->InlineAggressive();
            })
        );
    }
    RegisterLuaObjectWrapper<PrefabReference>();

    {
        using RBFX_THIS = ValueAnimation;
        lua.new_usertype<ValueAnimation>("ValueAnimation",
            sol::call_constructor, sol::factories([context]() {
                return SharedPtr<ValueAnimation>(new ValueAnimation(context));
            })
            RBFX_RAW(SetKeyFrame, [](ValueAnimation* animation, float time, sol::object value, sol::this_state s) {
                if (animation)
                    animation->SetKeyFrame(time, LuaToVariant(sol::state_view(s), value));
            })
            RBFX_RAW(SetEventFrame, [](ValueAnimation* animation, float time, const char* eventType) {
                if (animation)
                    animation->SetEventFrame(time, StringHash(eventType));
            })
        );
    }
    RegisterLuaObjectWrapper<ValueAnimation>();

    // Casters so GetSubsystem / event data / GetComponent return full usertypes.
    RegisterLuaObjectWrapper<Node>();
    RegisterLuaObjectWrapper<Scene>();

    // Global factory for the scene root. Ownership is shared with Lua.
    lua.set_function("CreateScene", [context](sol::this_state s) -> sol::object {
        return sol::make_object(sol::state_view(s), SharedPtr<Scene>(new Scene(context)));
    });

    // Transform space constants (Node::Translate/Rotate/Pitch/...).
    RBFX_ENUM_TABLE(TS, "LOCAL", TS_LOCAL, "PARENT", TS_PARENT, "WORLD", TS_WORLD);
}

} // namespace Urho3D

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
        using LUA_THIS = Node;
        LUA_CLASS(Node, sol::no_constructor
            // Full chain to Object: sol3 type casts (e.g. event senders passed
            // as Object*) only match the directly declared bases.
            LUA_BASES(Serializable, Object)
            LUA_META(equal_to, [](Node* a, Node* b) { return a == b; })
            LUA_META(to_string, [](Node* node) -> std::string {
                return "Node: " + std::string(node->GetName().c_str());
            })

            // Identification
            LUA_MEMBER_FUNC_RET(GetName, std::string)
            LUA_MEMBER_FUNC(SetName)
            LUA_MEMBER_FUNC_RET(GetID, unsigned)

            // Local transform
            LUA_MEMBER_FUNC_RET(GetPosition, Vector3)
            LUA_MEMBER_FUNC(SetPosition)
            LUA_MEMBER_FUNC_RET(GetRotation, Quaternion)
            LUA_MEMBER_FUNC(SetRotation)
            LUA_MEMBER_FUNC_RET(GetRotation2D, float)
            LUA_MEMBER_FUNC(SetRotation2D)
            LUA_MEMBER_FUNC_RET(GetPosition2D, Vector2)
            LUA_MEMBER_FUNC_RET(GetWorldPosition2D, Vector2)
            LUA_MEMBER_FUNC_RET(GetScale, Vector3)
            LUA_MEMBER_FUNC_OVERLOAD(SetScale,
                LUA_CAST(SetScale, void, const Vector3&),
                LUA_CAST(SetScale, void, float))
            LUA_MEMBER_FUNC(SetDirection)
            LUA_MEMBER_FUNC_OVERLOAD(SetTransform,
                LUA_CAST(SetTransform, void, const Vector3&, const Quaternion&),
                LUA_CAST(SetTransform, void, const Vector3&, const Quaternion&, const Vector3&))
            LUA_MEMBER_FUNC_RAW(SetTransform2D, [](Node* node, const Vector2& position, float rotation) { if (node) node->SetTransform2D(position, rotation); })
            // 2D-transform helpers (49/50_Urho2D, 51_StretchableSprite).
            LUA_MEMBER_FUNC_RAW(SetPosition2D, [](Node* node, const Vector2& position) { if (node) node->SetPosition2D(position); })
            LUA_MEMBER_FUNC_RAW(Translate2D, [](Node* node, const Vector2& delta, sol::optional<int> space) {
                if (node) node->Translate2D(delta, static_cast<TransformSpace>(space.value_or(TS_LOCAL))); })
            LUA_MEMBER_FUNC_RAW(Scale2D, [](Node* node, const Vector2& scale) { if (node) node->Scale2D(scale); })
            LUA_MEMBER_FUNC_OVERLOAD(SetScale2D,
                LUA_CAST(SetScale2D, void, const Vector2&),
                LUA_CAST(SetScale2D, void, float, float))

            // World transform
            LUA_MEMBER_FUNC_RET(GetWorldPosition, Vector3)
            LUA_MEMBER_FUNC(SetWorldPosition)
            LUA_MEMBER_FUNC_RET(GetWorldRotation, Quaternion)
            LUA_MEMBER_FUNC(SetWorldRotation)
            LUA_MEMBER_FUNC_RET(GetWorldScale, Vector3)
            LUA_MEMBER_FUNC_RET(GetWorldDirection, Vector3)
            LUA_MEMBER_FUNC_RET(GetDirection, Vector3)
            LUA_MEMBER_FUNC_RAW(LocalToWorld, [](Node* node, const Vector3& position) -> Vector3 {
                return node ? node->LocalToWorld(position) : Vector3::ZERO;
            })
            LUA_MEMBER_FUNC_RAW(WorldToLocal, [](Node* node, const Vector3& position) -> Vector3 {
                return node ? node->WorldToLocal(position) : Vector3::ZERO;
            })

            // Transform helpers. Translate/Rotate keep their lambdas: the engine
            // side is a single default-argument function, so there is no exact
            // overload to static_cast and the int space must stay a manual cast.
            LUA_MEMBER_FUNC_OVERLOAD(Translate,
                [](Node* node, const Vector3& delta) { if (node) node->Translate(delta); },
                [](Node* node, const Vector3& delta, int space) {
                    if (node) node->Translate(delta, static_cast<TransformSpace>(space)); })
            LUA_MEMBER_FUNC_OVERLOAD(Rotate,
                [](Node* node, const Quaternion& delta) { if (node) node->Rotate(delta); },
                [](Node* node, const Quaternion& delta, int space) { if (node) node->Rotate(delta, static_cast<TransformSpace>(space)); })
            LUA_MEMBER_FUNC_RAW(RotateAround, [](Node* node, const Vector3& point, const Quaternion& delta) { if (node) node->RotateAround(point, delta); })
            LUA_MEMBER_FUNC_RAW(Pitch, [](Node* node, float angle) { if (node) node->Pitch(angle); })
            LUA_MEMBER_FUNC_RAW(Yaw, [](Node* node, float angle) { if (node) node->Yaw(angle); })
            LUA_MEMBER_FUNC_RAW(Roll, [](Node* node, float angle) { if (node) node->Roll(angle); })
            LUA_MEMBER_FUNC_RAW(LookAt, [](Node* node, const Vector3& target, const Vector3& up, int space) {
                if (node) node->LookAt(target, up, static_cast<TransformSpace>(space)); })

            // Hierarchy
            LUA_MEMBER_FUNC_OBJ(GetParent, Node, GetParent)
            LUA_MEMBER_FUNC(SetParent)
            LUA_MEMBER_FUNC_OBJ(GetScene, Scene, GetScene)
            LUA_MEMBER_FUNC_RET(GetNumChildren, unsigned)
            LUA_MEMBER_FUNC_RET(GetNumComponents, unsigned)
            LUA_MEMBER_FUNC_OVERLOAD(CreateChild,
                [](Node* node, const char* name, sol::this_state s) -> sol::object {
                    return node ? WrapLuaObjectAs<Node>(sol::state_view(s), node->CreateChild(name)) : sol::lua_nil;
                },
                [](Node* node, sol::this_state s) -> sol::object {
                    return node ? WrapLuaObjectAs<Node>(sol::state_view(s), node->CreateChild()) : sol::lua_nil;
                })
            LUA_MEMBER_FUNC_RAW(AddChild, [](Node* node, Node* child) { if (node && child) node->AddChild(child); })
            LUA_MEMBER_FUNC_RAW(RemoveChild, [](Node* node, Node* child) { if (node) node->RemoveChild(child); })
            LUA_MEMBER_FUNC(RemoveAllChildren)
            LUA_MEMBER_FUNC_RAW(Remove, [](Node* node) { if (node) node->Remove(); })
            LUA_MEMBER_FUNC_RAW(Clone, [](Node* node, sol::this_state s) -> sol::object {
                return node ? WrapLuaObjectAs<Node>(sol::state_view(s), node->Clone()) : sol::lua_nil;
            })
            // Instantiate a PrefabResource under this node (17_SceneReplication).
            LUA_MEMBER_FUNC_RAW(InstantiatePrefab, [](Node* node, PrefabResource* prefab, const Vector3& position,
                const Quaternion& rotation, sol::this_state s) -> sol::object {
                return (node && prefab)
                    ? WrapLuaObjectAs<Node>(sol::state_view(s), node->InstantiatePrefab(prefab, position, rotation))
                    : sol::lua_nil;
            })
            LUA_MEMBER_FUNC_OVERLOAD(GetChild,
                [](Node* node, const char* name, sol::this_state s) -> sol::object {
                    return node ? WrapLuaObjectAs<Node>(sol::state_view(s), node->GetChild(StringHash(name))) : sol::lua_nil;
                },
                [](Node* node, const char* name, bool recursive, sol::this_state s) -> sol::object {
                    return node ? WrapLuaObjectAs<Node>(sol::state_view(s), node->GetChild(StringHash(name), recursive)) : sol::lua_nil;
                })
            LUA_MEMBER_FUNC_RAW(GetChildren, [](Node* node, sol::optional<bool> recursive, sol::this_state s) -> sol::object {
                if (!node)
                    return sol::lua_nil;
                return WrapObjectVector<Node>(sol::state_view(s), node->GetChildren(recursive.value_or(false)));
            })
            // Callback-style enumeration: visits children without allocating an
            // intermediate Lua table (the per-frame churn GetChildren incurs).
            // callback(child); return false from the callback to stop early.
            // Non-recursive iterates the engine's internal vector by reference (zero copy).
            LUA_MEMBER_FUNC_RAW(ForEachChild, [](Node* node, sol::protected_function callback, sol::optional<bool> recursive, sol::this_state s) {
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
            LUA_MEMBER_FUNC_RAW(ForEachComponent, [](Node* node, sol::protected_function callback, sol::this_state s) {
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
            LUA_MEMBER_FUNC_RAW(CreateComponent, [context](Node* node, const char* typeName, sol::this_state s) -> sol::object {
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
            LUA_MEMBER_FUNC_RAW(GetOrCreateComponent, [context](Node* node, const char* typeName, sol::this_state s) -> sol::object {
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
            LUA_MEMBER_FUNC_RAW(GetComponent, [context](Node* node, const char* typeName, sol::this_state s) -> sol::object {
                if (!node)
                    return sol::lua_nil;
                return WrapLuaObject(sol::state_view(s), node->GetComponent(StringHash(typeName)));
            })
            LUA_MEMBER_FUNC_RAW(RemoveComponent, [context](Node* node, const char* typeName) {
                if (node)
                    node->RemoveComponent(StringHash(typeName));
            })
            LUA_MEMBER_FUNC_RAW(HasComponent, [context](Node* node, const char* typeName) -> bool {
                return node && node->HasComponent(StringHash(typeName));
            })
            LUA_MEMBER_FUNC_RAW(GetComponents, [context](Node* node, const char* typeName, sol::this_state s) -> sol::object {
                if (!node)
                    return sol::lua_nil;
                ea::vector<Component*> components;
                node->GetComponents(components, StringHash(typeName));
                return WrapObjectVector<Component>(sol::state_view(s), components);
            })
            LUA_MEMBER_FUNC_RAW(GetChildrenWithComponent, [context](Node* node, const char* typeName, sol::optional<bool> recursive, sol::this_state s) -> sol::object {
                if (!node)
                    return sol::lua_nil;
                ea::vector<Node*> children;
                node->GetChildrenWithComponent(children, StringHash(typeName), recursive.value_or(false));
                return WrapObjectVector<Node>(sol::state_view(s), children);
            })

            // State
            LUA_MEMBER_FUNC_RET(IsEnabled, bool)
            LUA_MEMBER_FUNC_RAW(SetEnabled, [](Node* node, bool enabled) { if (node) node->SetEnabled(enabled); })

            // User variables
            LUA_MEMBER_FUNC_RAW(SetVar, [](Node* node, const char* key, sol::object value, sol::this_state s) {
                if (node) node->SetVar(key, LuaToVariant(sol::state_view(s), value));
            })
            LUA_MEMBER_FUNC_RAW(GetVar, [](Node* node, const char* key, sol::this_state s) -> sol::object {
                return node ? VariantToLua(sol::state_view(s), node->GetVar(key)) : sol::lua_nil;
            })

            // Tags (used by several samples to group nodes)
            LUA_MEMBER_FUNC(AddTag)
            LUA_MEMBER_FUNC(HasTag)
            LUA_MEMBER_FUNC_RAW(GetNumChildren, [](Node* node, sol::optional<bool> recursive) {
                return node ? node->GetNumChildren(recursive.value_or(false)) : 0u;
            })
            LUA_MEMBER_FUNC_RAW(GetChildrenWithTag, [](Node* node, const char* tag, sol::this_state s) -> sol::table {
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
            LUA_MEMBER_FUNC(GetWorldPosition)
            LUA_MEMBER_FUNC(GetWorldRotation)
            LUA_MEMBER_FUNC(GetWorldDirection)
            LUA_MEMBER_FUNC(GetWorldUp)
            LUA_MEMBER_FUNC(GetWorldRight)
            LUA_MEMBER_FUNC(SetTemporary)
        );
    }

    {
        using LUA_THIS = Scene;
        LUA_CLASS(Scene, sol::no_constructor
            LUA_BASES(Node, Serializable, Object)

            // Instantiate a prefab XML resource under the scene.
            LUA_MEMBER_FUNC_RAW(InstantiateXML, [context](Scene* scene, const char* resourceName, const Vector3& position, const Quaternion& rotation, sol::this_state s) -> sol::object {
                if (!scene)
                    return sol::lua_nil;
                auto* cache = context->GetSubsystem<ResourceCache>();
                XMLFile* xml = cache->GetResource<XMLFile>(resourceName);
                if (!xml)
                    return sol::lua_nil;
                return WrapLuaObject(sol::state_view(s), scene->InstantiateXML(xml->GetRoot(), position, rotation));
            })
            LUA_MEMBER_FUNC_RAW(LoadXML, [context](Scene* scene, const char* resourceName) -> bool {
                if (!scene)
                    return false;
                auto* cache = context->GetSubsystem<ResourceCache>();
                XMLFile* xml = cache->GetResource<XMLFile>(resourceName);
                return xml && scene->LoadXML(xml->GetRoot());
            })
            LUA_MEMBER_FUNC_RAW(GetChildByIndex, [](Scene* scene, unsigned index, sol::this_state s) -> sol::object {
                return scene ? WrapLuaObjectAs<Node>(sol::state_view(s), scene->GetChild(index)) : sol::lua_nil;
            })
            // In-memory save/load used by the 2D samples' reload feature
            // (49_Urho2DIsometricDemo).
            LUA_MEMBER_FUNC_RAW(Save, [](Scene* scene, VectorBuffer* dest) -> bool {
                return scene && dest ? scene->Save(*dest) : false;
            })
            LUA_MEMBER_FUNC_RAW(Load, [](Scene* scene, VectorBuffer* source) -> bool {
                return scene && source ? scene->Load(*source) : false;
            })
            LUA_MEMBER_FUNC(SetUpdateEnabled)
            LUA_MEMBER_FUNC(IsUpdateEnabled)
            LUA_MEMBER_FUNC(SetTimeScale)
            LUA_MEMBER_FUNC(GetTimeScale)
            LUA_MEMBER_FUNC_RAW(GetWorldOrigin, [](Scene* scene) { return scene ? scene->GetWorldOrigin() : IntVector3::ZERO; })
            // Remove all replicated content and reset the scene
            // (20_HugeObjectCount rebuild).
            LUA_MEMBER_FUNC(Clear)
        );
    }

    {
        using LUA_THIS = Component;
        LUA_CLASS(Component, sol::no_constructor
            LUA_BASES(Serializable, Object)
            LUA_META(equal_to, [](Component* a, Component* b) { return a == b; })
            LUA_META(to_string, [](Component* component) -> std::string {
                return "Component: " + std::string(component->GetTypeName().c_str());
            })
            LUA_MEMBER_FUNC_OBJ(GetNode, Node, GetNode)
            LUA_MEMBER_FUNC_OBJ(GetScene, Scene, GetScene)
            LUA_MEMBER_FUNC_RET(GetID, unsigned)
            LUA_MEMBER_FUNC_RET(IsEnabled, bool)
            LUA_MEMBER_FUNC_RAW(SetEnabled, [](Component* component, bool enabled) { if (component) component->SetEnabled(enabled); })
            LUA_MEMBER_FUNC(IsEnabledEffective)
            // Remove from the node and destroy (49/50 orc body removal).
            LUA_MEMBER_FUNC_RAW(Remove, [](Component* component) { if (component) component->Remove(); })
        );
    }

    {
        using LUA_THIS = PrefabReference;
        LUA_CLASS(PrefabReference, sol::no_constructor
            LUA_BASES(Component, Serializable, Object)
            LUA_MEMBER_FUNC_RAW(SetPrefab, [context](PrefabReference* prefab, const char* resourceName) {
                if (!prefab)
                    return;
                auto* cache = context->GetSubsystem<ResourceCache>();
                PrefabResource* resource = cache->GetResource<PrefabResource>(resourceName);
                prefab->SetPrefab(resource);
            })
            // Inline the prefab into the scene, discarding the reference
            // (18_CharacterDemo's sliding door).
            LUA_MEMBER_FUNC_RAW(InlineAggressive, [](PrefabReference* prefab) {
                if (prefab)
                    prefab->InlineAggressive();
            })
        );
    }
    RegisterLuaObjectWrapper<PrefabReference>();

    {
        using LUA_THIS = ValueAnimation;
        LUA_CLASS(ValueAnimation,
            sol::call_constructor, sol::factories([context]() {
                return SharedPtr<ValueAnimation>(new ValueAnimation(context));
            })
            LUA_MEMBER_FUNC_RAW(SetKeyFrame, [](ValueAnimation* animation, float time, sol::object value, sol::this_state s) {
                if (animation)
                    animation->SetKeyFrame(time, LuaToVariant(sol::state_view(s), value));
            })
            LUA_MEMBER_FUNC_RAW(SetEventFrame, [](ValueAnimation* animation, float time, const char* eventType) {
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
    LUA_GLOBAL_FUNC(CreateScene, [context](sol::this_state s) -> sol::object {
        return sol::make_object(sol::state_view(s), SharedPtr<Scene>(new Scene(context)));
    });

    // Transform space constants (Node::Translate/Rotate/Pitch/...).
    LUA_ENUM_TABLE(TS, "LOCAL", TS_LOCAL, "PARENT", TS_PARENT, "WORLD", TS_WORLD);
}

} // namespace Urho3D

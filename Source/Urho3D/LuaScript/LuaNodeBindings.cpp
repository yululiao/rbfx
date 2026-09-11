//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#include "../Precompiled.h"

#include "../LuaScript/LuaNodeBindings.h"

#include "../LuaScript/LuaBindings.h"

#include "../Core/Context.h"
#include "../IO/Log.h"
#include "../IO/VectorBuffer.h"
#include "../Resource/ResourceCache.h"
#include "../Resource/XMLFile.h"
#include "../Scene/Component.h"
#include "../Scene/Node.h"
#include "../Scene/PrefabReference.h"
#include "../Scene/Scene.h"
#include "../Scene/ValueAnimation.h"

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
    lua.new_usertype<Node>("Node",
        sol::no_constructor,
        // Full chain to Object: sol3 type casts (e.g. event senders passed
        // as Object*) only match the directly declared bases.
        sol::base_classes, sol::bases<Serializable, Object>(),
        sol::meta_function::equal_to, [](Node* a, Node* b) { return a == b; },
        sol::meta_function::to_string, [](Node* node) -> std::string {
            return "Node: " + std::string(node->GetName().c_str());
        },

        // Identification
        "name", sol::property(
            [](Node* node) -> std::string { return node ? node->GetName().c_str() : ""; },
            [](Node* node, const char* name) { if (node) node->SetName(name); }),
        "id", sol::readonly_property([](Node* node) -> unsigned { return node ? node->GetID() : 0; }),
        "SetName", [](Node* node, const char* name) { if (node) node->SetName(name); },
        "GetName", [](Node* node) -> std::string { return node ? node->GetName().c_str() : ""; },

        // Local transform
        "position", sol::property(
            [](Node* node) -> Vector3 { return node ? node->GetPosition() : Vector3::ZERO; },
            [](Node* node, const Vector3& value) { if (node) node->SetPosition(value); }),
        "rotation", sol::property(
            [](Node* node) -> Quaternion { return node ? node->GetRotation() : Quaternion::IDENTITY; },
            [](Node* node, const Quaternion& value) { if (node) node->SetRotation(value); }),
        "rotation2D", sol::property(
            [](Node* node) -> float { return node ? node->GetRotation().RollAngle() : 0.0f; },
            [](Node* node, float value) { if (node) node->SetRotation(Quaternion(value)); }),
        "scale", sol::property(
            [](Node* node) -> Vector3 { return node ? node->GetScale() : Vector3::ONE; },
            [](Node* node, const Vector3& value) { if (node) node->SetScale(value); }),
        "SetPosition", [](Node* node, const Vector3& v) { if (node) node->SetPosition(v); },
        "GetPosition", [](Node* node) -> Vector3 { return node ? node->GetPosition() : Vector3::ZERO; },
        "GetPosition2D", [](Node* node) -> Vector2 { return node ? node->GetPosition2D() : Vector2::ZERO; },
        "GetWorldPosition2D", [](Node* node) -> Vector2 { return node ? node->GetWorldPosition2D() : Vector2::ZERO; },
        "SetRotation", [](Node* node, const Quaternion& q) { if (node) node->SetRotation(q); },
        "GetRotation", [](Node* node) -> Quaternion { return node ? node->GetRotation() : Quaternion::IDENTITY; },
        "GetScale", [](Node* node) -> Vector3 { return node ? node->GetScale() : Vector3::ONE; },
        "SetScale", sol::overload(
            [](Node* node, const Vector3& v) { if (node) node->SetScale(v); },
            [](Node* node, float v) { if (node) node->SetScale(v); }),
        "SetDirection", [](Node* node, const Vector3& d) { if (node) node->SetDirection(d); },
        "SetTransform", sol::overload(
            [](Node* node, const Vector3& position, const Quaternion& rotation) { if (node) node->SetTransform(position, rotation); },
            [](Node* node, const Vector3& position, const Quaternion& rotation, const Vector3& scale) { if (node) node->SetTransform(position, rotation, scale); }),
        "SetTransform2D", [](Node* node, const Vector2& position, float rotation) { if (node) node->SetTransform2D(position, rotation); },
        // 2D-transform helpers (49/50_Urho2D, 51_StretchableSprite).
        "SetPosition2D", [](Node* node, const Vector2& position) { if (node) node->SetPosition2D(position); },
        "Translate2D", [](Node* node, const Vector2& delta, sol::optional<int> space) {
            if (node) node->Translate2D(delta, static_cast<TransformSpace>(space.value_or(TS_LOCAL))); },
        "Scale2D", [](Node* node, const Vector2& scale) { if (node) node->Scale2D(scale); },
        "SetScale2D", sol::overload(
            [](Node* node, const Vector2& scale) { if (node) node->SetScale2D(scale); },
            [](Node* node, float x, float y) { if (node) node->SetScale2D(x, y); }),

        // World transform
        "worldPosition", sol::property(
            [](Node* node) -> Vector3 { return node ? node->GetWorldPosition() : Vector3::ZERO; },
            [](Node* node, const Vector3& value) { if (node) node->SetWorldPosition(value); }),
        "worldRotation", sol::property(
            [](Node* node) -> Quaternion { return node ? node->GetWorldRotation() : Quaternion::IDENTITY; },
            [](Node* node, const Quaternion& value) { if (node) node->SetWorldRotation(value); }),
        "worldScale", sol::readonly_property([](Node* node) -> Vector3 {
            return node ? node->GetWorldScale() : Vector3::ONE;
        }),
        "worldDirection", sol::readonly_property([](Node* node) -> Vector3 {
            return node ? node->GetWorldDirection() : Vector3::FORWARD;
        }),
        "direction", sol::readonly_property([](Node* node) -> Vector3 {
            return node ? node->GetDirection() : Vector3::FORWARD;
        }),
        "SetWorldPosition", [](Node* node, const Vector3& v) { if (node) node->SetWorldPosition(v); },
        "SetWorldRotation", [](Node* node, const Quaternion& q) { if (node) node->SetWorldRotation(q); },
        "LocalToWorld", [](Node* node, const Vector3& position) -> Vector3 {
            return node ? node->LocalToWorld(position) : Vector3::ZERO;
        },
        "WorldToLocal", [](Node* node, const Vector3& position) -> Vector3 {
            return node ? node->WorldToLocal(position) : Vector3::ZERO;
        },

        // Transform helpers
        "Translate", sol::overload(
            [](Node* node, const Vector3& delta) { if (node) node->Translate(delta); },
            [](Node* node, const Vector3& delta, int space) {
                if (node) node->Translate(delta, static_cast<TransformSpace>(space)); }),
        "Rotate", sol::overload(
            [](Node* node, const Quaternion& delta) { if (node) node->Rotate(delta); },
            [](Node* node, const Quaternion& delta, int space) { if (node) node->Rotate(delta, static_cast<TransformSpace>(space)); }),
        "RotateAround", [](Node* node, const Vector3& point, const Quaternion& delta) { if (node) node->RotateAround(point, delta); },
        "Pitch", [](Node* node, float angle) { if (node) node->Pitch(angle); },
        "Yaw", [](Node* node, float angle) { if (node) node->Yaw(angle); },
        "Roll", [](Node* node, float angle) { if (node) node->Roll(angle); },
        "LookAt", [](Node* node, const Vector3& target, const Vector3& up, int space) {
            if (node) node->LookAt(target, up, static_cast<TransformSpace>(space)); },

        // Hierarchy
        "parent", sol::readonly_property([](Node* node) -> Node* { return node ? node->GetParent() : nullptr; }),
        "SetParent", [](Node* node, Node* parent) {
            if (node)
                node->SetParent(parent);
        },
        "scene", sol::readonly_property([](Node* node) -> Scene* { return node ? node->GetScene() : nullptr; }),
        "numChildren", sol::readonly_property([](Node* node) -> unsigned { return node ? node->GetNumChildren() : 0; }),
        "numComponents", sol::readonly_property([](Node* node) -> unsigned { return node ? node->GetNumComponents() : 0; }),
        "CreateChild", sol::overload(
            [](Node* node, const char* name) -> SharedPtr<Node> { return node ? SharedPtr<Node>(node->CreateChild(name)) : nullptr; },
            [](Node* node) -> SharedPtr<Node> { return node ? SharedPtr<Node>(node->CreateChild()) : nullptr; }),
        "AddChild", [](Node* node, Node* child) { if (node && child) node->AddChild(child); },
        "RemoveChild", [](Node* node, Node* child) { if (node) node->RemoveChild(child); },
        "RemoveAllChildren", [](Node* node) { if (node) node->RemoveAllChildren(); },
        "Remove", [](Node* node) { if (node) node->Remove(); },
        "Clone", [](Node* node) -> SharedPtr<Node> { return node ? SharedPtr<Node>(node->Clone()) : nullptr; },
        // Instantiate a PrefabResource under this node (17_SceneReplication).
        "InstantiatePrefab", [](Node* node, PrefabResource* prefab, const Vector3& position,
            const Quaternion& rotation) -> Node* {
            return (node && prefab) ? node->InstantiatePrefab(prefab, position, rotation) : nullptr;
        },
        "GetChild", sol::overload(
            [](Node* node, const char* name) -> Node* { return node ? node->GetChild(StringHash(name)) : nullptr; },
            [](Node* node, const char* name, bool recursive) -> Node* { return node ? node->GetChild(StringHash(name), recursive) : nullptr; }),
        "GetChildren", [](Node* node, sol::optional<bool> recursive, sol::this_state s) -> sol::object {
            if (!node)
                return sol::lua_nil;
            return WrapObjectVector<Node>(sol::state_view(s), node->GetChildren(recursive.value_or(false)));
        },

        // Components: string-typed factory channel. Any component type
        // registered in the Context is creatable from Lua.
        "CreateComponent", [context](Node* node, const char* typeName, sol::this_state s) -> sol::object {
            if (!node)
                return sol::lua_nil;
            Component* component = node->CreateComponent(StringHash(typeName));
            if (!component)
            {
                URHO3D_LOGERROR("Component type '{}' is not registered", typeName);
                return sol::lua_nil;
            }
            return WrapLuaObject(sol::state_view(s), component);
        },
        "GetOrCreateComponent", [context](Node* node, const char* typeName, sol::this_state s) -> sol::object {
            if (!node)
                return sol::lua_nil;
            Component* component = node->GetOrCreateComponent(StringHash(typeName));
            if (!component)
            {
                URHO3D_LOGERROR("Component type '{}' is not registered", typeName);
                return sol::lua_nil;
            }
            return WrapLuaObject(sol::state_view(s), component);
        },
        "GetComponent", [context](Node* node, const char* typeName, sol::this_state s) -> sol::object {
            if (!node)
                return sol::lua_nil;
            return WrapLuaObject(sol::state_view(s), node->GetComponent(StringHash(typeName)));
        },
        "RemoveComponent", [context](Node* node, const char* typeName) {
            if (node)
                node->RemoveComponent(StringHash(typeName));
        },
        "HasComponent", [context](Node* node, const char* typeName) -> bool {
            return node && node->HasComponent(StringHash(typeName));
        },
        "GetComponents", [context](Node* node, const char* typeName, sol::this_state s) -> sol::object {
            if (!node)
                return sol::lua_nil;
            ea::vector<Component*> components;
            node->GetComponents(components, StringHash(typeName));
            return WrapObjectVector<Component>(sol::state_view(s), components);
        },
        "GetChildrenWithComponent", [context](Node* node, const char* typeName, sol::optional<bool> recursive, sol::this_state s) -> sol::object {
            if (!node)
                return sol::lua_nil;
            ea::vector<Node*> children;
            node->GetChildrenWithComponent(children, StringHash(typeName), recursive.value_or(false));
            return WrapObjectVector<Node>(sol::state_view(s), children);
        },

        // State
        "enabled", sol::property(
            [](Node* node) -> bool { return node && node->IsEnabled(); },
            [](Node* node, bool enabled) { if (node) node->SetEnabled(enabled); }),
        "SetEnabled", [](Node* node, bool enabled) { if (node) node->SetEnabled(enabled); },

        // User variables
        "SetVar", [](Node* node, const char* key, sol::object value, sol::this_state s) {
            if (node) node->SetVar(key, LuaToVariant(sol::state_view(s), value));
        },
        "GetVar", [](Node* node, const char* key, sol::this_state s) -> sol::object {
            return node ? VariantToLua(sol::state_view(s), node->GetVar(key)) : sol::lua_nil;
        },

        // Tags (used by several samples to group nodes)
        "AddTag", &Node::AddTag,
        "HasTag", &Node::HasTag,
        "GetNumChildren", [](Node* node, sol::optional<bool> recursive) {
            return node ? node->GetNumChildren(recursive.value_or(false)) : 0u;
        },
        "GetChildrenWithTag", [](Node* node, const char* tag, sol::this_state s) -> sol::table {
            sol::state_view lua(s);
            sol::table result = lua.create_table();
            if (node)
            {
                unsigned index = 1;
                for (Node* child : node->GetChildrenWithTag(tag))
                    result[index++] = child;
            }
            return result;
        },

        // World-space transform queries
        "GetWorldPosition", &Node::GetWorldPosition,
        "GetWorldRotation", &Node::GetWorldRotation,
        "GetWorldDirection", &Node::GetWorldDirection,
        "GetWorldUp", &Node::GetWorldUp,
        "GetWorldRight", &Node::GetWorldRight,
        "SetTemporary", &Node::SetTemporary
    );

    lua.new_usertype<Scene>("Scene",
        sol::no_constructor,
        sol::base_classes, sol::bases<Node, Serializable, Object>(),

        // Instantiate a prefab XML resource under the scene.
        "InstantiateXML", [context](Scene* scene, const char* resourceName, const Vector3& position, const Quaternion& rotation, sol::this_state s) -> sol::object {
            if (!scene)
                return sol::lua_nil;
            auto* cache = context->GetSubsystem<ResourceCache>();
            XMLFile* xml = cache->GetResource<XMLFile>(resourceName);
            if (!xml)
                return sol::lua_nil;
            return WrapLuaObject(sol::state_view(s), scene->InstantiateXML(xml->GetRoot(), position, rotation));
        },
        "LoadXML", [context](Scene* scene, const char* resourceName) -> bool {
            if (!scene)
                return false;
            auto* cache = context->GetSubsystem<ResourceCache>();
            XMLFile* xml = cache->GetResource<XMLFile>(resourceName);
            return xml && scene->LoadXML(xml->GetRoot());
        },
        "GetChildByIndex", [](Scene* scene, unsigned index) -> Node* {
            return scene ? scene->GetChild(index) : nullptr;
        },
        // In-memory save/load used by the 2D samples' reload feature
        // (49_Urho2DIsometricDemo).
        "Save", [](Scene* scene, VectorBuffer* dest) -> bool {
            return scene && dest ? scene->Save(*dest) : false;
        },
        "Load", [](Scene* scene, VectorBuffer* source) -> bool {
            return scene && source ? scene->Load(*source) : false;
        },
        "SetUpdateEnabled", &Scene::SetUpdateEnabled,
        "IsUpdateEnabled", &Scene::IsUpdateEnabled,
        "SetTimeScale", &Scene::SetTimeScale,
        "GetTimeScale", &Scene::GetTimeScale,
        "GetWorldOrigin", [](Scene* scene) { return scene ? scene->GetWorldOrigin() : IntVector3::ZERO; },
        // Remove all replicated content and reset the scene
        // (20_HugeObjectCount rebuild).
        "Clear", &Scene::Clear
    );

    lua.new_usertype<Component>("Component",
        sol::no_constructor,
        sol::base_classes, sol::bases<Serializable, Object>(),
        sol::meta_function::equal_to, [](Component* a, Component* b) { return a == b; },
        sol::meta_function::to_string, [](Component* component) -> std::string {
            return "Component: " + std::string(component->GetTypeName().c_str());
        },
        "node", sol::readonly_property(&Component::GetNode),
        "GetNode", &Component::GetNode,
        "scene", sol::readonly_property(&Component::GetScene),
        "id", sol::readonly_property(&Component::GetID),
        "enabled", sol::property(
            &Component::IsEnabled,
            [](Component* component, bool enabled) { if (component) component->SetEnabled(enabled); }),
        "SetEnabled", [](Component* component, bool enabled) { if (component) component->SetEnabled(enabled); },
        "IsEnabledEffective", &Component::IsEnabledEffective,
        // Remove from the node and destroy (49/50 orc body removal).
        "Remove", [](Component* component) { if (component) component->Remove(); }
    );

    lua.new_usertype<PrefabReference>("PrefabReference",
        sol::no_constructor,
        sol::base_classes, sol::bases<Component, Serializable, Object>(),
        "SetPrefab", [context](PrefabReference* prefab, const char* resourceName) {
            if (!prefab)
                return;
            auto* cache = context->GetSubsystem<ResourceCache>();
            PrefabResource* resource = cache->GetResource<PrefabResource>(resourceName);
            prefab->SetPrefab(resource);
        },
        // Inline the prefab into the scene, discarding the reference
        // (18_CharacterDemo's sliding door).
        "InlineAggressive", [](PrefabReference* prefab) {
            if (prefab)
                prefab->InlineAggressive();
        }
    );
    RegisterLuaObjectWrapper<PrefabReference>();

    // PrefabResource: instanced scene fragment, consumed by PrefabReference.
    lua.new_usertype<PrefabResource>("PrefabResource",
        sol::no_constructor,
        sol::base_classes, sol::bases<Resource, Object>()
    );
    RegisterLuaObjectWrapper<PrefabResource>();
    lua.new_usertype<ValueAnimation>("ValueAnimation",
        sol::call_constructor, sol::factories([context]() {
            return SharedPtr<ValueAnimation>(new ValueAnimation(context));
        }),
        "SetKeyFrame", [](ValueAnimation* animation, float time, sol::object value, sol::this_state s) {
            if (animation)
                animation->SetKeyFrame(time, LuaToVariant(sol::state_view(s), value));
        },
        "SetEventFrame", [](ValueAnimation* animation, float time, const char* eventType) {
            if (animation)
                animation->SetEventFrame(time, StringHash(eventType));
        }
    );
    RegisterLuaObjectWrapper<ValueAnimation>();

    // Casters so GetSubsystem / event data / GetComponent return full usertypes.
    RegisterLuaObjectWrapper<Node>();
    RegisterLuaObjectWrapper<Scene>();

    // Global factory for the scene root. Ownership is shared with Lua.
    lua.set_function("CreateScene", [context](sol::this_state s) -> sol::object {
        return sol::make_object(sol::state_view(s), SharedPtr<Scene>(new Scene(context)));
    });

    // Transform space constants (Node::Translate/Rotate/Pitch/...).
    sol::table transformSpace = lua.create_named_table("TS");
    transformSpace["LOCAL"] = TS_LOCAL;
    transformSpace["PARENT"] = TS_PARENT;
    transformSpace["WORLD"] = TS_WORLD;
}

} // namespace Urho3D

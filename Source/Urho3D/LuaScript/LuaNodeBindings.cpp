//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#include "../Precompiled.h"

#include "../LuaScript/LuaNodeBindings.h"

#include "../Core/Context.h"
#include "../Graphics/Material.h"
#include "../Graphics/Model.h"
#include "../Graphics/StaticModel.h"
#include "../Math/Quaternion.h"
#include "../Math/Vector3.h"
#include "../Scene/Node.h"
#include "../Scene/Scene.h"

#include <sol/sol.hpp>

namespace sol
{

// Disable sol3's automagical registrations for Node. Node is a RefCounted object
// without comparison operators; we register all desired Lua behavior explicitly.
template <>
struct is_automagical<Urho3D::Node> : std::false_type {};

} // namespace sol

namespace Urho3D
{

// Provide comparison operators for Node so sol3 can auto-generate Lua __eq/__lt/__le.
// Node is a RefCounted object without value semantics, so identity (address) comparison is used.
inline bool operator==(const Node& a, const Node& b) { return &a == &b; }
inline bool operator!=(const Node& a, const Node& b) { return &a != &b; }
inline bool operator<(const Node& a, const Node& b) { return &a < &b; }
inline bool operator<=(const Node& a, const Node& b) { return &a <= &b; }
inline bool operator>(const Node& a, const Node& b) { return &a > &b; }
inline bool operator>=(const Node& a, const Node& b) { return &a >= &b; }

namespace
{

// Create a child node and return it as SharedPtr so Lua holds a safe reference.
SharedPtr<Node> NodeCreateChild(Node* parent, const char* name)
{
    if (!parent)
        return nullptr;
    return SharedPtr<Node>(parent->CreateChild(name));
}

} // namespace

void RegisterVector3Bindings(sol::state& lua)
{
    lua.new_usertype<Vector3>("Vector3",
        sol::call_constructor, sol::constructors<Vector3(), Vector3(float, float, float)>(),
        "x", &Vector3::x_,
        "y", &Vector3::y_,
        "z", &Vector3::z_,
        "Length", &Vector3::Length,
        "Normalized", &Vector3::Normalized,
        "Dot", &Vector3::DotProduct,
        "Cross", &Vector3::CrossProduct,
        "ZERO", sol::var(Vector3::ZERO),
        sol::meta_function::addition, [](const Vector3& a, const Vector3& b) { return a + b; },
        sol::meta_function::subtraction, [](const Vector3& a, const Vector3& b) { return a - b; },
        sol::meta_function::multiplication, [](const Vector3& a, float s) { return a * s; },
        sol::meta_function::division, [](const Vector3& a, float s) { return a / s; }
    );
}

void RegisterNodeBindings(sol::state& lua)
{
    // Node is the core scene graph object. It is owned by rbfx via RefCounted;
    // Lua receives a SharedPtr wrapper so the reference remains valid.
    lua.new_usertype<Node>("Node",
        sol::no_constructor,

        // Provide identity comparison explicitly. Node is a RefCounted object
        // without operator==, so sol3 cannot auto-generate __eq.
        sol::meta_function::equal_to, [](Node* a, Node* b) { return a == b; },

        // Identification
        "name", sol::property(
            [](Node* node) -> const char* { return node ? node->GetName().c_str() : ""; },
            [](Node* node, const char* name) { if (node) node->SetName(name); }
        ),
        "id", sol::readonly_property([](Node* node) -> unsigned { return node ? node->GetID() : 0; }),

        // Transform - use lambdas to avoid any overload resolution ambiguities
        "position", sol::property(
            [](Node* node) -> Vector3 { return node ? node->GetPosition() : Vector3::ZERO; },
            [](Node* node, const Vector3& value) { if (node) node->SetPosition(value); }
        ),
        "setPositionXYZ", [](Node* node, float x, float y, float z) {
            if (node) node->SetPosition(Vector3(x, y, z));
        },
        "rotation", sol::property(
            [](Node* node) -> Quaternion { return node ? node->GetRotation() : Quaternion::IDENTITY; },
            [](Node* node, const Quaternion& value) { if (node) node->SetRotation(value); }
        ),
        "scale", sol::property(
            [](Node* node) -> Vector3 { return node ? node->GetScale() : Vector3::ONE; },
            [](Node* node, const Vector3& value) { if (node) node->SetScale(value); }
        ),
        "worldPosition", sol::readonly_property([](Node* node) -> Vector3 {
            return node ? node->GetWorldPosition() : Vector3::ZERO;
        }),
        "worldRotation", sol::readonly_property([](Node* node) -> Quaternion {
            return node ? node->GetWorldRotation() : Quaternion::IDENTITY;
        }),
        "worldScale", sol::readonly_property([](Node* node) -> Vector3 {
            return node ? node->GetWorldScale() : Vector3::ONE;
        }),

        // Hierarchy
        "parent", sol::readonly_property([](Node* node) -> Node* { return node ? node->GetParent() : nullptr; }),
        "scene", sol::readonly_property([](Node* node) -> Scene* { return node ? node->GetScene() : nullptr; }),
        "numChildren", sol::readonly_property([](Node* node) -> unsigned {
            return node ? node->GetNumChildren() : 0;
        }),
        "CreateChild", &NodeCreateChild,
        "Remove", [](Node* node) { if (node) node->Remove(); },
        "RemoveChild", [](Node* node, Node* child) { if (node) node->RemoveChild(child); },
        "RemoveAllChildren", [](Node* node) { if (node) node->RemoveAllChildren(); },

        // Components
        "CreateStaticModel", [](Node* node) -> StaticModel* {
            return node ? node->GetOrCreateComponent<StaticModel>() : nullptr;
        },
        "GetStaticModel", [](Node* node) -> StaticModel* {
            return node ? node->GetComponent<StaticModel>() : nullptr;
        }
    );

    // Scene is just a Node with extra capabilities; expose it as a Node subclass.
    lua.new_usertype<Scene>("Scene",
        sol::no_constructor,
        sol::base_classes, sol::bases<Node>(),
        "CreateChild", &NodeCreateChild
    );

    // StaticModel component bindings.
    lua.new_usertype<StaticModel>("StaticModel",
        sol::no_constructor,
        "SetModel", &StaticModel::SetModel,
        "GetModel", &StaticModel::GetModel,
        "SetMaterial", [](StaticModel* sm, Material* material) { if (sm) sm->SetMaterial(material); },
        "GetMaterial", [](StaticModel* sm) -> Material* { return sm ? sm->GetMaterial() : nullptr; }
    );
}

} // namespace Urho3D

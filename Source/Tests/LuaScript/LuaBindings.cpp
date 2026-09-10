//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#ifdef URHO3D_LUA

#include "../CommonUtils.h"

#include <Urho3D/Graphics/StaticModel.h>
#include <Urho3D/LuaScript/LuaScript.h>
#include <Urho3D/Math/Vector3.h>
#include <Urho3D/Scene/Node.h>
#include <Urho3D/Scene/Scene.h>

TEST_CASE("Lua binding creates and manipulates Node")
{
    auto context = Tests::GetOrCreateContext(Tests::CreateCompleteContext);
    auto scene = MakeShared<Scene>(context);

    auto* luaScript = context->GetSubsystem<LuaScript>();
    REQUIRE(luaScript);

    // Expose the scene to Lua. The SharedPtr is kept alive by C++ for the
    // duration of the test, so a raw pointer wrapper is sufficient here.
    luaScript->SetGlobalNode("scene", scene.Get());

    const char* code = R"(
        -- Create a node from Lua
        local node = scene:CreateChild("LuaNode")
        node.name = "RenamedFromLua"
        node:setPositionXYZ(1.0, 2.0, 3.0)

        -- Create a child node and use Vector3
        local child = node:CreateChild("LuaChild")
        child.position = Vector3(10, 20, 30)

        -- Add a component
        local sm = node:CreateStaticModel()
        assert(sm ~= nil, "StaticModel component should be created")

        -- Access read-only hierarchy info
        assert(node.numChildren == 1)
        assert(node.parent == scene)
    )";

    CHECK(luaScript->ExecuteString(code, "LuaNodeTest"));

    // Verify that Lua changes are reflected on the C++ side
    auto* node = scene->GetChild("RenamedFromLua", true);
    REQUIRE(node);
    CHECK(node->GetPosition() == Vector3(1.0f, 2.0f, 3.0f));

    auto* child = scene->GetChild("LuaChild", true);
    REQUIRE(child);
    CHECK(child->GetPosition() == Vector3(10.0f, 20.0f, 30.0f));

    auto* sm = node->GetComponent<StaticModel>();
    REQUIRE(sm);
}

TEST_CASE("Lua binding Vector3 math")
{
    auto context = Tests::GetOrCreateContext(Tests::CreateCompleteContext);
    auto* luaScript = context->GetSubsystem<LuaScript>();
    REQUIRE(luaScript);

    const char* code = R"(
        local a = Vector3(1, 2, 3)
        local b = Vector3(4, 5, 6)
        local c = a + b
        assert(c.x == 5 and c.y == 7 and c.z == 9)
        assert(a:Length() > 0)
    )";

    CHECK(luaScript->ExecuteString(code, "LuaVector3Test"));
}

#endif // URHO3D_LUA

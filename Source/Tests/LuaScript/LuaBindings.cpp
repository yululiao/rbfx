//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#ifdef URHO3D_LUA

#include "../CommonUtils.h"

#include <Urho3D/Graphics/StaticModel.h>
#include <Urho3D/Graphics/FrameSprite.h>
#include <Urho3D/Graphics/Texture2D.h>
#include <Urho3D/Graphics/Octree.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/IO/FileSystem.h>
extern "C"
{
#include <Lua/lua.h>
#include <Lua/lauxlib.h>
#include <Lua/lualib.h>
}
#include <LuaScript/EngineLuaVM.h>
#include <Urho3D/Math/Vector3.h>
#include <Urho3D/Scene/Node.h>
#include <Urho3D/Scene/Scene.h>

namespace
{

// The shared test context does not bring the game-logic VM up on its own, so the first Lua
// test that runs creates it. Later tests find it already registered.
Urho3D::EngineLuaVM* GetOrCreateEngineLua(Urho3D::Context* context)
{
    if (auto* existing = context->GetSubsystem<Urho3D::EngineLuaVM>())
        return existing;
    const auto engineLua = Urho3D::MakeShared<Urho3D::EngineLuaVM>(context);
    context->RegisterSubsystem(engineLua);
    return engineLua.Get();
}

}

TEST_CASE("Lua binding creates and manipulates Node")
{
    auto context = Tests::GetOrCreateContext(Tests::CreateCompleteContext);
    auto scene = MakeShared<Scene>(context);

    auto* luaScript = GetOrCreateEngineLua(context.Get());
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
    auto* luaScript = GetOrCreateEngineLua(context.Get());
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

TEST_CASE("FrameSprite Lua interfaces create and switch native playback")
{
    auto context = Tests::GetOrCreateContext(Tests::CreateCompleteContext);
    auto scene = MakeShared<Scene>(context);
    scene->CreateComponent<Octree>();
    auto texture = MakeShared<Texture2D>(context);
    texture->SetName("FrameSpriteTest/Lua.png");
    context->GetSubsystem<ResourceCache>()->AddManualResource(texture);
    auto luaScript = GetOrCreateEngineLua(context);
    luaScript->SetGlobalNode("frameSpriteScene", scene);
    REQUIRE(luaScript->ExecuteString(R"(
        local texture = GetResource("Texture2D", "FrameSpriteTest/Lua.png")
        local node = frameSpriteScene:CreateChild("Sprite")
        local sprite = node:CreateComponent("FrameSprite")
        sprite:SetMaterial(Material())
        sprite:SetSize(Vector2(2, 2))
        sprite:SetOffset(Vector3(0, 1, 0))
        local animations = FrameSpriteAnimator()
        assert(animations:AddGridAnimation("run", texture, 4, 1, 0, 4, 4, true))
        local ok, result = pcall(function()
            return animations:AddImageAnimation("single", {"FrameSpriteTest/Lua.png"}, 4, false)
        end)
        assert(ok, "AddImageAnimation failed: " .. tostring(result))
        assert(result, "AddImageAnimation returned false")
        local animator = FrameSpriteAnimator()
        animator:CopyAnimations(animations)
        animator:SetSprite(sprite)
        assert(animator:PlayAni("run"))
        assert(sprite:IsPlaying())
        assert(not animator:PlayAni("missing"))
        sprite:Pause()
        assert(not sprite:IsPlaying())
        sprite:Resume()
        assert(sprite:IsPlaying())
        assert(frameSpriteScene:GetComponent("FrameSpriteBatch") ~= nil)
        animator:SetSprite(nil)
        assert(not animator:PlayAni("run"))
    )", "FrameSpriteBindings"));
    auto sprite = scene->GetChild("Sprite")->GetComponent<FrameSprite>();
    REQUIRE(sprite);
    scene->Update(0.25f);
    CHECK(sprite->GetFrame() == 1);
    CHECK(scene->GetComponent<FrameSpriteBatch>()->GetNumSprites() == 1);
    REQUIRE(luaScript->ExecuteString(R"(
        local batch = frameSpriteScene:GetComponent("FrameSpriteBatch")
        assert(batch:GetNumSprites() == 1)
        assert(not batch:GetStatisticsEnabled())
        assert(batch:GetUpdateCount() == 0)
        assert(batch:GetTotalUpdateMs() == 0 and batch:GetFrameChanges() == 0)
        batch:SetStatisticsEnabled(true)
        assert(batch:GetStatisticsEnabled())
        batch:Update(0)
        assert(batch:GetUpdateCount() == 1)
        batch:SetStatisticsEnabled(false)
        batch:Update(0)
        assert(batch:GetUpdateCount() == 0 and batch:GetMaxUpdateMs() == 0)
        batch:SetAutoUpdate(false)
        assert(not batch:GetAutoUpdate())
    )", "FrameSpriteManualBatch"));
    scene->Update(0.25f);
    CHECK(sprite->GetFrame() == 1); // No automatic double advance.
    REQUIRE(luaScript->ExecuteString(R"(
        local batch = frameSpriteScene:GetComponent("FrameSpriteBatch")
        local node = frameSpriteScene:GetChild("Sprite", false)
        node:SetPosition(Vector3(3, 0, 0))
        batch:Update(0) -- Hit-stop still follows position without advancing frames.
    )", "FrameSpriteHitStop"));
    CHECK(sprite->GetFrame() == 1);
    scene->GetComponent<FrameSpriteBatch>()->Update(0.25f);
    CHECK(sprite->GetFrame() == 2);
}

TEST_CASE("FrameSprite migration preserves FireGame gameplay hot paths")
{
    // A separate Lua state keeps the mock Vector3/Config out of the engine VM.
    const ea::string testPath = GetPath(__FILE__) + "../../../script/test_firegame_hotpaths.lua";
    lua_State* lua = luaL_newstate();
    REQUIRE(lua);
    luaL_openlibs(lua);
    // Resolve module paths from the source tree rather than the test runner's cwd.
    const ea::string modulePath = GetPath(__FILE__) + "../../../rbfxProjs/FireGame/Data/Scripts/?.lua";
    lua_getglobal(lua, "package");
    lua_pushstring(lua, modulePath.c_str());
    lua_setfield(lua, -2, "path");
    lua_pop(lua, 1);
    const int status = luaL_dofile(lua, testPath.c_str());
    const ea::string error = status == LUA_OK ? "" : lua_tostring(lua, -1);
    lua_close(lua);
    INFO(error.c_str());
    CHECK(status == LUA_OK);
}

TEST_CASE("FishingGuy player restores idle and faces fishing direction")
{
    const ea::string root = GetPath(__FILE__) + "../../../";
    lua_State* lua = luaL_newstate();
    REQUIRE(lua);
    luaL_openlibs(lua);
    lua_pushstring(lua, (root + "rbfxProjs/FishingGuy/Data/").c_str());
    lua_setglobal(lua, "FISHINGGUY_DATA_ROOT");
    const int status = luaL_dofile(lua, (root + "script/test_fishingguy_player.lua").c_str());
    const ea::string error = status == LUA_OK ? "" : lua_tostring(lua, -1);
    lua_close(lua);
    INFO(error.c_str());
    CHECK(status == LUA_OK);
}

#endif // URHO3D_LUA

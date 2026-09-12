// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

// Standalone host for Lua-driven games, mirroring LuaSamplesRunner but for real
// project scenes instead of self-contained samples.
//
// Boot flow (the game's whole logic is driven from Lua):
//   1. Register the LuaScript subsystem and the serializable LuaGameScript type
//      (the latter ships in the shared RbfxLuaScript module so both the editor
//      and this player can deserialize scenes that reference it).
//   2. Read the startup scene path from Data/Game.json:
//          { "startupScene": "Scenes/Default.scene" }
//   3. Load that scene (which fires component OnNodeAdded and, being update
//      enabled by default, ticks and renders through a viewport).
//   4. Execute the entry script referenced by the scene's LuaGameScript
//      components; the script then wires up the rest of the game.
//
// The pure C++ Player application is intentionally left untouched.

#include <Urho3D/Core/Context.h>
#include <Urho3D/Engine/Application.h>
#include <Urho3D/Engine/Engine.h>
#include <Urho3D/Engine/EngineDefs.h>
#include <Urho3D/Graphics/Camera.h>
#include <Urho3D/Graphics/Renderer.h>
#include <Urho3D/Graphics/Viewport.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Resource/JSONFile.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>

#include <LuaScript/LuaGameScript.h>
#include <LuaScript/LuaScript.h>

using namespace Urho3D;

namespace
{

/// Startup scene used when Game.json is missing or does not specify one.
const ea::string defaultStartupScene = "Scenes/Default.scene";

} // namespace

class LuaGamePlayer : public Application
{
    URHO3D_OBJECT(LuaGamePlayer, Application);

public:
    explicit LuaGamePlayer(Context* context)
        : Application(context)
    {
    }

    void Setup() override
    {
        engineParameters_[EP_WINDOW_TITLE] = "LuaGamePlayer";
        engineParameters_[EP_APPLICATION_NAME] = "LuaGamePlayer";
        engineParameters_[EP_LOG_NAME] = "player.log";
        engineParameters_[EP_RESOURCE_PATHS] = "CoreData;Data";
        engineParameters_[EP_HEADLESS] = false;
        engineParameters_[EP_SOUND] = true;
    }

    void Start() override
    {
        // 1) Bring up the Lua runtime and make the game entry component known to
        //    the reflection system so scenes referencing it can be deserialized.
        const auto luaScript = MakeShared<LuaScript>(context_);
        context_->RegisterSubsystem(luaScript);
        luaScript->Initialize();
        LuaGameScript::RegisterObject(context_);

        // 2) Resolve the startup scene from Data/Game.json (fall back to default).
        auto* cache = GetSubsystem<ResourceCache>();
        ea::string startupScene = ReadStartupScene(cache);

        // 3) Load the scene. A freshly loaded Scene is update-enabled by default
        //    and subscribes to E_UPDATE, so it ticks on its own once started.
        scene_ = cache->GetResource<Scene>(startupScene);
        if (!scene_)
        {
            URHO3D_LOGERROR("LuaGamePlayer: failed to load startup scene '{}'", startupScene);
            GetSubsystem<Engine>()->Exit();
            return;
        }

        SetupViewport();

        // 4) Expose the active scene and run the entry script(s) the scene asks for.
        luaScript->SetGlobalScene("scene", scene_);
        ea::vector<LuaGameScript*> entryScripts;
        scene_->GetComponents<LuaGameScript>(entryScripts);
        if (entryScripts.empty())
            URHO3D_LOGWARNING("LuaGamePlayer: startup scene '{}' has no LuaGameScript component", startupScene);

        for (LuaGameScript* entry : entryScripts)
        {
            const ea::string& scriptPath = entry->GetScriptPath();
            if (scriptPath.empty())
                continue;
            if (!luaScript->ExecuteFile(scriptPath))
                URHO3D_LOGERROR("LuaGamePlayer: failed to execute entry script '{}'", scriptPath);
            else
                URHO3D_LOGINFO("LuaGamePlayer: executed entry script '{}'", scriptPath);
        }
    }

    void Stop() override
    {
        if (auto* luaScript = context_->GetSubsystem<LuaScript>())
            luaScript->SetGlobalScene("scene", nullptr);
        scene_ = nullptr;
    }

private:
    /// Read the "startupScene" key from Data/Game.json, or return the default.
    ea::string ReadStartupScene(ResourceCache* cache) const
    {
        if (auto* settings = cache->GetResource<JSONFile>("Game.json"))
        {
            const JSONValue& root = settings->GetRoot();
            if (root.Contains("startupScene"))
            {
                const ea::string scene = root.Get("startupScene").GetString();
                if (!scene.empty())
                    return scene;
            }
            URHO3D_LOGWARNING("LuaGamePlayer: Game.json has no 'startupScene', using default");
        }
        else
        {
            URHO3D_LOGWARNING("LuaGamePlayer: Game.json not found, using default startup scene");
        }
        return defaultStartupScene;
    }

    /// Point the main renderer viewport at the first camera found in the scene.
    void SetupViewport()
    {
        auto* renderer = GetSubsystem<Renderer>();
        if (!renderer)
            return;

        Camera* camera = scene_->GetComponent<Camera>();
        if (!camera)
        {
            URHO3D_LOGWARNING("LuaGamePlayer: startup scene has no Camera, nothing will be rendered");
            return;
        }

        auto viewport = MakeShared<Viewport>(context_);
        viewport->SetScene(scene_);
        viewport->SetCamera(camera);
        renderer->SetViewport(0, viewport);
    }

    SharedPtr<Scene> scene_;
};

URHO3D_DEFINE_APPLICATION_MAIN(LuaGamePlayer);

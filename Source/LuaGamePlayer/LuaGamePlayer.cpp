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
#include <Urho3D/Graphics/Graphics.h>
#include <Urho3D/Input/Input.h>
#include <Urho3D/Input/InputEvents.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Resource/JSONFile.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/IO/VirtualFileSystem.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/Scene/SceneResource.h>

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
        auto* fs = context_->GetSubsystem<FileSystem>();

        engineParameters_[EP_WINDOW_TITLE] = "LuaGamePlayer";
        engineParameters_[EP_APPLICATION_NAME] = "LuaGamePlayer";
        engineParameters_[EP_LOG_NAME] = "player.log";
        // Narrower on purpose than the engine default ("CoreData;Cache;Data"): a shipped game has
        // no asset cache next to it. Each name is looked for under every prefix path below.
        engineParameters_[EP_RESOURCE_PATHS] = "CoreData;Data";
        // A packaged game needs nothing here: the executable already sits next to its Data/ and
        // the engine always mounts the program directory. For running straight out of the build
        // tree, use the same discovery the editor does, which finds the CoreData of the engine
        // checkout by walking up from the working directory. A ResourceRoot.ini beside the
        // executable, or an explicit --pp/--pr, is still honoured and takes precedence.
        if (const ea::string prefixPath = fs->FindResourcePrefixPath(); !prefixPath.empty())
            engineParameters_[EP_RESOURCE_PREFIX_PATHS] = prefixPath;
        engineParameters_[EP_HEADLESS] = false;
        engineParameters_[EP_SOUND] = true;
        // The engine default for this parameter is true, which creates a window without a title
        // bar - and a window with no close button is a window the player cannot leave. A plain
        // bordered window is the right default for a standalone game. An explicit --borderless or
        // a config file still wins, because overriding only replaces a value that was not set.
        engineParameters_[EP_BORDERLESS] = false;
    }

    void Start() override
    {
        // Ask the VFS to watch the mounted directories before anything is loaded. New mount
        // points inherit this flag, so a hot reload of Lua scripts works here exactly like it
        // does in the editor, where EditorApplication turns watching on for the whole session.
        if (auto* vfs = context_->GetSubsystem<VirtualFileSystem>())
            vfs->SetWatching(true);

        // The mouse starts hidden and confined to the window on purpose: Input defaults to
        // invisible + MM_ABSOLUTE, and that combination grabs the cursor (SDL_SetWindowGrab) so a
        // game can draw its own cursor sprite. This host has no cursor sprite, so ask for the
        // ordinary desktop pointer instead. Order matters - showing the cursor releases the grab,
        // and only then does MM_FREE keep the pointer free to travel outside the window.
        if (auto* input = context_->GetSubsystem<Input>())
        {
            input->SetMouseVisible(true);
            input->SetMouseMode(MM_FREE);
        }

        // Escape quits. Kept here rather than in the game script for two reasons: the same
        // main.lua also runs inside the editor's Play session, where quitting on Escape would
        // take the editor down with it; and a shipped game may well want Escape for a pause
        // menu, in which case this is the one subscription to remove or gate, not the script.
        auto engine = GetContext()->GetSubsystem<Engine>();
        SubscribeToEvent(E_KEYDOWN, [engine](StringHash /*eventType*/, VariantMap& eventData) {
            using namespace KeyDown;
            if (eventData[P_KEY].GetInt() == KEY_ESCAPE)
                engine->Exit();
        });

        if (auto* graphics = context_->GetSubsystem<Graphics>())
        {
            URHO3D_LOGINFO("LuaGamePlayer: window '{}' {}x{} borderless={}, cursor visible={}",
                graphics->GetWindowTitle(), graphics->GetWidth(), graphics->GetHeight(),
                graphics->GetBorderless(), GetSubsystem<Input>() ? GetSubsystem<Input>()->IsMouseVisible() : false);
        }

        // 1) Bring up the Lua runtime and make the game entry component known to
        //    the reflection system so scenes referencing it can be deserialized.
        const auto luaScript = MakeShared<LuaScript>(context_);
        context_->RegisterSubsystem(luaScript);
        luaScript->Initialize();
        LuaGameScript::RegisterObject(context_);

        // 2) Resolve the startup scene from Data/Game.json (fall back to default).
        auto* cache = GetSubsystem<ResourceCache>();
        ea::string startupScene = ReadStartupScene(cache);

        // 3) Load the scene. Scene is not a Resource in rbfx - it is wrapped by SceneResource,
        //    which owns the live Scene (that is also what the editor uses to open .scene files).
        //    A freshly created Scene is update-enabled by default and subscribes to E_UPDATE,
        //    so it ticks on its own once started.
        sceneResource_ = cache->GetResource<SceneResource>(startupScene);
        scene_ = sceneResource_ ? sceneResource_->GetScene() : nullptr;
        if (!scene_)
        {
            URHO3D_LOGERROR("LuaGamePlayer: failed to load startup scene '{}'", startupScene);
            GetSubsystem<Engine>()->Exit();
            return;
        }

        SetupViewport();

        // 4) Expose the active scene and run the entry script(s) the scene asks for.
        luaScript->SetGlobalScene("scene", scene_);
        // The LuaGameScript component is looked up on the scene root node only, mirroring
        // LuaGameRunner::Start() so Play in the editor and this host agree.
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
        // Drop the scene first - it is owned by the resource wrapper.
        scene_ = nullptr;
        sceneResource_ = nullptr;
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

        // Camera is usually on a child node, so search the whole hierarchy.
        Camera* camera = scene_->FindComponent<Camera>();
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

    /// Wrapper that owns the running scene; kept alive so a cache release cannot pull it away.
    SharedPtr<SceneResource> sceneResource_;
    SharedPtr<Scene> scene_;
};

URHO3D_DEFINE_APPLICATION_MAIN(LuaGamePlayer);

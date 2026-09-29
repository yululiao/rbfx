// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

// Standalone host for minigame platforms (Douyin/WeChat). It mirrors LuaGamePlayer's boot
// flow but runs inside a vendor runtime instead of a browser:
//
//   * the canvas, frame loop and audio session are owned by the runtime; the JavaScript
//     bootstrap (game.js) instantiates this module (createRbfxModule) and serves the game
//     files to the engine over the synchronous file bridge mounted during engine init;
//   * there is no DOM: no HTML shell, no IndexedDB, no guaranteed SharedArrayBuffer. The
//     engine accordingly builds without pthreads and without IDBFS (see URHO3D_MINIGAME in
//     CMake/Modules/UrhoOptions.cmake).
//
// Boot flow (identical to LuaGamePlayer, the game's whole logic is driven from Lua):
//   1. Register the game-logic EngineLuaVM subsystem and the serializable LuaGameScript type.
//   2. Read the startup scene path from Data/Game.json:
//          { "startupScene": "Scenes/Default.scene" }
//   3. Load that scene (which fires component OnNodeAdded and, being update enabled by
//      default, ticks and renders through a viewport).
//   4. Execute the entry script referenced by the scene's LuaGameScript components.
//
// The pure C++ Player application and the browser LuaGamePlayer are intentionally left
// untouched. Platform work that is specific to the runtime plugs in through the JavaScript
// bootstrap and the dedicated Minigame subsystems - lifecycle events, the host SDK and the
// file bridge mounted during engine init - so this host stays a plain Application.

#include <Urho3D/Core/Context.h>
#include <Urho3D/Core/CoreEvents.h>
#include <Urho3D/Engine/Application.h>
#include <Urho3D/Engine/Engine.h>
#include <Urho3D/Engine/EngineDefs.h>
#include <Urho3D/Engine/EngineEvents.h>
#include <Urho3D/Graphics/Camera.h>
#include <Urho3D/Graphics/Renderer.h>
#include <Urho3D/Graphics/Viewport.h>
#include <Urho3D/Graphics/Graphics.h>
#include <Urho3D/Input/Input.h>
#include <Urho3D/Input/InputEvents.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/IO/MountedRemoteFiles.h>
#include <Urho3D/IO/MountedUserData.h>
#include <Urho3D/Resource/JSONFile.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/IO/VirtualFileSystem.h>
#include <Urho3D/Minigame/MinigamePlatform.h>
#include <Urho3D/Minigame/MinigameSDK.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/Scene/SceneResource.h>

#include <LuaScript/LuaGameScript.h>
#include <LuaScript/EngineLuaVM.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

using namespace Urho3D;

namespace
{

/// Startup scene used when Game.json is missing or does not specify one.
const ea::string defaultStartupScene = "Scenes/Default.scene";

/// Report bootstrap progress to the JavaScript loading manager (game.js/loading.js). The
/// manager may be absent - a test harness can instantiate the module without the minigame
/// bootstrap - so the call is a no-op then.
void ReportBootstrapInitStatus(float rate, bool finished)
{
#ifdef __EMSCRIPTEN__
    EM_ASM({
        const loading = globalThis.rbfx && globalThis.rbfx.loading;
        if (loading)
            loading.setEngineInitStatus($1 === 1, $0);
    }, rate, finished ? 1 : 0);
#else
    (void)rate;
    (void)finished;
#endif
}

} // namespace

class MinigamePlayer : public Application
{
    URHO3D_OBJECT(MinigamePlayer, Application);

public:
    explicit MinigamePlayer(Context* context)
        : Application(context)
    {
    }

    void Setup() override
    {
        auto* fs = context_->GetSubsystem<FileSystem>();

        // The mounts have to be in place while the engine initializes - renderer defaults,
        // ImGui's shader, the engine config are all read before Application::Start() runs -
        // and the engine resets the VFS inside Engine::Initialize. The event handler is the
        // one point where both hold: standard mounting is done and subsystems have not
        // started yet.
        SubscribeToEvent(E_VIRTUALFILESYSTEMINITIALIZED,
            URHO3D_HANDLER(MinigamePlayer, HandleVirtualFileSystemInitialized));

        engineParameters_[EP_WINDOW_TITLE] = "MinigamePlayer";
        engineParameters_[EP_APPLICATION_NAME] = "MinigamePlayer";
        // No engine-managed log file: the minigame platforms have no writable engine file
        // system, and the runtime already forwards console output to the host's log viewer.
        // The bootstrap mirrors selected output into user storage on its own.
        engineParameters_[EP_LOG_NAME] = EMPTY_STRING;
        // Narrower on purpose than the engine default ("CoreData;Cache;Data"): a shipped game
        // has no asset cache next to it. Each name becomes a namespace inside the package
        // (see HandleVirtualFileSystemInitialized); every entry is also looked up under the
        // prefix paths when running out of a build tree.
        engineParameters_[EP_RESOURCE_PATHS] = "CoreData;Data";
        // A packaged game needs nothing here; running out of a build tree still benefits from
        // the same discovery the editor uses. A ResourceRoot.ini beside the module, or an
        // explicit --pp/--pr, is honoured and takes precedence.
        if (const ea::string prefixPath = fs->FindResourcePrefixPath(); !prefixPath.empty())
            engineParameters_[EP_RESOURCE_PREFIX_PATHS] = prefixPath;
        engineParameters_[EP_HEADLESS] = false;
        engineParameters_[EP_SOUND] = true;
        // The runtime never shows window decorations; the value is kept for parity with the
        // other hosts so a shared config file behaves identically everywhere.
        engineParameters_[EP_BORDERLESS] = false;
    }

    void Start() override
    {
        // The mounts were attached by HandleVirtualFileSystemInitialized during
        // Engine::Initialize; nothing is left to do here before the first load.
        // Bring up the platform layer: host lifecycle events, the user data directory, the
        // exit request and the background prefetch request. Outside a host the subsystem is
        // inert, so everything below is platform-agnostic.
        const auto platform = MakeShared<MinigamePlatform>(context_);
        context_->RegisterSubsystem(platform);
        platform->InstallNativeHooks();

        // Bring up the SDK bridge: login, share, rewarded ads, payment and privacy all
        // complete through MinigameSdkResult events. When no host shim is attached the
        // requests complete immediately with a failure, keeping one code path in game code.
        const auto sdk = MakeShared<MinigameSDK>(context_);
        context_->RegisterSubsystem(sdk);
        sdk->InstallNativeHooks();

        // The mouse starts hidden and confined on purpose in Input's default state, which
        // grabs the pointer; ask for the ordinary pointer instead. On the minigame runtime
        // the mouse is emulated from touch by the vendor layer, so the same calls describe
        // the desired end state.
        if (auto* input = context_->GetSubsystem<Input>())
        {
            input->SetMouseVisible(true);
            input->SetMouseMode(MM_FREE);
        }

        // Escape quits on platforms that have a keyboard (the runtime also runs on desktop
        // shells). Keep this here rather than in the game script: the same main.lua also runs
        // inside the editor's Play session, where quitting on Escape would take the editor
        // down with it.
        auto engine = GetContext()->GetSubsystem<Engine>();
        SubscribeToEvent(E_KEYDOWN, [engine](StringHash /*eventType*/, VariantMap& eventData) {
            using namespace KeyDown;
            if (eventData[P_KEY].GetInt() == KEY_ESCAPE)
                engine->Exit();
        });

        if (auto* graphics = context_->GetSubsystem<Graphics>())
        {
            URHO3D_LOGINFO("MinigamePlayer: canvas {}x{} borderless={}, cursor visible={}",
                graphics->GetWidth(), graphics->GetHeight(),
                graphics->GetBorderless(), GetSubsystem<Input>() ? GetSubsystem<Input>()->IsMouseVisible() : false);
        }

        // Move the loading screen's bar while the game boots; the manager may not exist when
        // the module runs outside the minigame package, in which case this is a no-op.
        ReportBootstrapInitStatus(0.25f, false);

        // 1) Bring up the Lua runtime and make the game entry component known to
        //    the reflection system so scenes referencing it can be deserialized.
        const auto luaScript = MakeShared<EngineLuaVM>(context_);
        context_->RegisterSubsystem(luaScript);
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
            URHO3D_LOGERROR("MinigamePlayer: failed to load startup scene '{}'", startupScene);
            // A failed boot must still release the loading screen; there will be no frame.
            ReportBootstrapInitStatus(1.0f, true);
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
            URHO3D_LOGWARNING("MinigamePlayer: startup scene '{}' has no LuaGameScript component", startupScene);

        for (LuaGameScript* entry : entryScripts)
        {
            const ea::string& scriptPath = entry->GetScriptPath();
            if (scriptPath.empty())
                continue;
            if (!luaScript->ExecuteFile(scriptPath))
                URHO3D_LOGERROR("MinigamePlayer: failed to execute entry script '{}'", scriptPath);
            else
                URHO3D_LOGINFO("MinigamePlayer: executed entry script '{}'", scriptPath);
        }

        ReportBootstrapInitStatus(0.75f, false);

        // The loading screen keeps painting on top of the canvas until the first real frame;
        // that is the earliest safe moment to hand the canvas over for good.
        SubscribeToEvent(E_ENDFRAME, [this] {
            ReportBootstrapInitStatus(1.0f, true);
            UnsubscribeFromEvent(E_ENDFRAME);
        });
    }

    void Stop() override
    {
        if (auto* luaScript = context_->GetSubsystem<EngineLuaVM>())
            luaScript->SetGlobalScene("scene", nullptr);
        // Drop the scene first - it is owned by the resource wrapper.
        scene_ = nullptr;
        sceneResource_ = nullptr;
    }

private:
    /// Attach the package mounts once the engine's own mounting is done but nothing has been
    /// read yet.
    void HandleVirtualFileSystemInitialized(StringHash eventType, VariantMap& eventData)
    {
        auto* vfs = context_->GetSubsystem<VirtualFileSystem>();
        if (!vfs)
            return;

        // Every resource path becomes a namespace inside the package ("CoreData/...",
        // "Data/..."), the same way the engine maps real folders; later mounts take priority,
        // exactly like MountExistingDirectoriesOrPackages does. The JavaScript file layer
        // answers reads synchronously - readFileSync is in fact the only package access the
        // minigame runtimes provide.
        const StringVector resourcePaths = engineParameters_[EP_RESOURCE_PATHS].GetString().split(';');
        for (const ea::string& path : resourcePaths)
        {
            if (!path.empty())
                vfs->Mount(MakeShared<MountedRemoteFiles>(context_, path));
        }

        // Writable storage (save games, settings) lives under the "user" scheme.
        vfs->Mount(MakeShared<MountedUserData>(context_));

        // Ask the VFS to watch the mounted directories before anything is loaded. The web family
        // has no file watcher, so this is a no-op there; it keeps the host identical to
        // LuaGamePlayer and future-proofs editor-hosted runs.
        vfs->SetWatching(true);
    }

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
            URHO3D_LOGWARNING("MinigamePlayer: Game.json has no 'startupScene', using default");
        }
        else
        {
            URHO3D_LOGWARNING("MinigamePlayer: Game.json not found, using default startup scene");
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
            URHO3D_LOGWARNING("MinigamePlayer: startup scene has no Camera, nothing will be rendered");
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

URHO3D_DEFINE_APPLICATION_MAIN(MinigamePlayer);

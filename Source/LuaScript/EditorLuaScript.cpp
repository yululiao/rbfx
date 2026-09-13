//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#include "../Urho3D/Precompiled.h"

#include "EditorLuaScript.h"

#include "EditorLuaHooks.h"
#include "LuaBindings.h"
#include "LuaFile.h"
#include "LuaNodeBindings.h"
#include "LuaPackageLoader.h"
#include "LuaScript.h"

#include "../Urho3D/Core/Context.h"
#include "../Urho3D/Core/StringUtils.h"
#include "../Urho3D/IO/File.h"
#include "../Urho3D/IO/FileIdentifier.h"
#include "../Urho3D/IO/FileSystem.h"
#include "../Urho3D/IO/Log.h"
#include "../Urho3D/IO/MountPoint.h"
#include "../Urho3D/IO/VirtualFileSystem.h"
#include "../Urho3D/Scene/Node.h"
#include "../Urho3D/Scene/Component.h"
#include "../Urho3D/Scene/Scene.h"

#include <EASTL/algorithm.h>
#include <EASTL/unordered_map.h>
#include <sol/sol.hpp>

#include <string>

namespace Urho3D
{

namespace
{

// Process-wide editor hook table. Installed by the editor at startup; the "Editor" API
// forwards to it. Kept here (not in the header) so the exported accessors have one home.
EditorLuaHooks& Hooks()
{
    static EditorLuaHooks hooks;
    return hooks;
}

// UI callbacks (tab draw / menu click) registered by plugins, keyed by an opaque handle the
// editor echoes back through InvokeUICallback. Kept in the DLL next to the sol state so the
// editor never touches sol types. A single EditorLuaScript instance exists per editor session.
ea::unordered_map<unsigned long long, sol::protected_function>& UICallbacks()
{
    static ea::unordered_map<unsigned long long, sol::protected_function> callbacks;
    return callbacks;
}

unsigned long long NextUICallbackHandle()
{
    static unsigned long long handle = 1;
    return handle++;
}

} // namespace

void SetEditorLuaHooks(const EditorLuaHooks& hooks)
{
    Hooks() = hooks;
}

EditorLuaHooks& GetEditorLuaHooks()
{
    return Hooks();
}

EditorLuaScript::EditorLuaScript(Context* context)
    : Object(context)
{
}

EditorLuaScript::~EditorLuaScript()
{
    // Release sol references while the Lua state is still alive: the UI-callback registry is a
    // function-local static whose protected_function values would otherwise be destroyed at
    // process exit, after luaState_ (and its lua_State) is already gone.
    UICallbacks().clear();
    // Drop event handlers too: their lambdas capture sol references for the same reason.
    UnsubscribeFromAllEvents();

    // Give back the mount point: the virtual file system holds it with a strong reference, so
    // leaving it behind would keep a previous project's EditorScripts folder reachable under
    // the same scheme after a project switch.
    if (pluginMount_)
    {
        auto* vfs = context_->GetSubsystem<VirtualFileSystem>();
        if (vfs)
            vfs->Unmount(pluginMount_);
        pluginMount_ = nullptr;
    }
}

bool EditorLuaScript::Initialize()
{
    if (luaState_)
        return true;

    LuaFile::RegisterObject(context_);

    luaState_ = ea::make_unique<sol::state>();
    luaState_->open_libraries(
        sol::lib::base,
        sol::lib::package,
        sol::lib::string,
        sol::lib::table,
        sol::lib::math,
        sol::lib::coroutine,
        sol::lib::debug,
        sol::lib::io,
        sol::lib::os
    );

    // Editor plugins resolve from their own folder first, then from the ordinary resource
    // directories, so a project can require its own modules and engine-provided ones with the
    // same call. The plugin scheme is only useful once LoadPlugins has mounted the folder; a
    // miss simply falls through to the next prefix, which is why attaching here is safe.
    packageLoader_ = ea::make_unique<LuaPackageLoader>(context_);
    packageLoader_->Attach(luaState_->lua_state(), { "editorlua://", EMPTY_STRING });

    RegisterEngineBindings();
    RegisterEditorBindings();
    // Dear ImGui is only meaningful inside the editor's render loop, so it is exposed
    // exclusively to this editor state (never to the game LuaScript state).
    RegisterImGuiBindings(*luaState_);

    // Redirect Lua print into the engine log so plugin output is visible in the editor console.
    (*luaState_)["__editor_log_info"] = [](const char* message) { URHO3D_LOGINFO("EditorLua: {}", message); };
    ExecuteString(
        "function print(...) "
        "local parts = {} "
        "for i = 1, select('#', ...) do parts[#parts + 1] = tostring(select(i, ...)) end "
        "__editor_log_info(table.concat(parts, ' ')) "
        "end",
        "=[editor-print-redirect]");

    return true;
}

void EditorLuaScript::LoadPlugins(const ea::string& absoluteDir)
{
    // Normalize once here so that the mount, the scan and the resource names derived from the
    // paths all agree; a trailing slash would produce "dir//file.lua" as a resource name.
    ea::string dir = absoluteDir;
    while (!dir.empty() && (dir.back() == '/' || dir.back() == '\\'))
        dir.pop_back();
    pluginDir_ = dir;

    auto* fs = context_->GetSubsystem<FileSystem>();
    if (!fs || dir.empty() || !fs->DirExists(dir))
        return;

    // Reload starts from a clean UI slate; plugins below re-register their tabs and menu items.
    UICallbacks().clear();
    if (GetEditorLuaHooks().resetUI)
        GetEditorLuaHooks().resetUI();

    MountPluginDir(dir);

    // Every plugin body is about to run again, and modules they require must run again with it.
    // Without this the second load would hand plugins the module tables of the first load,
    // because require() answers from package.loaded before any searcher is consulted.
    if (packageLoader_)
        packageLoader_->ResetTracking();

    // Top-level only: everything in this folder is a plugin that must run on its own; anything in
    // a subfolder is a module the plugins opt into via require(), resolved through the mount.
    ea::vector<ea::string> files;
    fs->ScanDir(files, dir, "*.lua", SCAN_FILES);
    ea::sort(files.begin(), files.end());

    for (const ea::string& file : files)
        ExecuteFileAbsolute(dir + "/" + file);
}

void EditorLuaScript::MountPluginDir(const ea::string& absoluteDir)
{
    auto* vfs = context_->GetSubsystem<VirtualFileSystem>();
    if (!vfs || absoluteDir.empty())
        return;

    if (pluginMount_ && mountedPluginDir_ == absoluteDir)
        return;

    if (pluginMount_)
    {
        vfs->Unmount(pluginMount_);
        pluginMount_ = nullptr;
        mountedPluginDir_.clear();
    }

    // The folder becomes a resource directory with a scheme of its own, which buys three things
    // over injecting a package.path entry: subfolders resolve without touching the working
    // directory, the plugin sources take part in the same watching/reloading pipeline as every
    // other asset, and packaged .luc siblings are preferred transparently.
    MountPoint* mountPoint = vfs->MountDir("editorlua", absoluteDir);
    if (!mountPoint)
    {
        URHO3D_LOGERRORF("Failed to mount the editor plugin directory '%s'", absoluteDir.c_str());
        return;
    }

    pluginMount_ = mountPoint;
    mountedPluginDir_ = absoluteDir;
}

bool EditorLuaScript::ExecuteString(const ea::string& code, const ea::string& chunkName)
{
    if (!luaState_)
    {
        URHO3D_LOGERROR("EditorLuaScript is not initialized.");
        return false;
    }

    try
    {
        const char* name = chunkName.empty() ? "=[editor-string]" : chunkName.c_str();
        sol::load_result loaded = luaState_->load(code.c_str(), name);
        if (!loaded.valid())
        {
            sol::error err = loaded;
            URHO3D_LOGERRORF("EditorLua load error in %s: %s", name, err.what());
            return false;
        }

        sol::protected_function script = loaded;
        sol::protected_function_result result = script();
        if (!result.valid())
        {
            sol::error err = result;
            URHO3D_LOGERRORF("EditorLua runtime error in %s: %s", name, err.what());
            return false;
        }
    }
    catch (const std::exception& e)
    {
        URHO3D_LOGERRORF("EditorLua exception: %s", e.what());
        return false;
    }

    return true;
}

bool EditorLuaScript::ExecuteFileAbsolute(const ea::string& absolutePath)
{
    if (!packageLoader_)
    {
        URHO3D_LOGERROR("EditorLuaScript is not initialized.");
        return false;
    }

    // Translate to a resource name instead of opening the file: plugin bodies then share one
    // execution path with required modules, which is what lets a change be attributed to a
    // module at all. Editor plugins are not registered as reload roots; Editor.reloadPlugins()
    // remains the entry point, because re-running a plugin body has to come with the UI reset.
    auto* vfs = context_->GetSubsystem<VirtualFileSystem>();
    const FileIdentifier identifier = vfs ? vfs->GetIdentifierFromAbsoluteName(absolutePath) : FileIdentifier::Empty;
    if (!identifier)
    {
        URHO3D_LOGERRORF("EditorLua script '%s' is not reachable through the mounted resource directories.", absolutePath.c_str());
        return false;
    }

    return packageLoader_->ExecuteScript(identifier.ToUri(), false);
}

sol::state& EditorLuaScript::GetState()
{
    return *luaState_;
}

void EditorLuaScript::SubscribeGlobalEvent(const char* eventName, sol::protected_function callback)
{
    if (!luaState_)
    {
        URHO3D_LOGERROR("EditorLuaScript: cannot subscribe, Lua state is not initialized");
        return;
    }
    if (!callback.valid())
    {
        URHO3D_LOGERROR("EditorLuaScript: subscribe expects a Lua function as callback");
        return;
    }

    // The lambda copies the sol reference; handlers are removed in the destructor before
    // the Lua state is torn down, so callbacks can never fire on a destroyed state.
    SubscribeToEvent(StringHash(eventName),
        [this, callback](Object*, StringHash, VariantMap& eventData) mutable
        {
            InvokeEventCallback(callback, eventData);
        });
}

void EditorLuaScript::UnsubscribeEvent(const char* eventName)
{
    UnsubscribeFromEvent(StringHash(eventName));
}

unsigned long long EditorLuaScript::RegisterUICallback(sol::protected_function callback)
{
    const unsigned long long handle = NextUICallbackHandle();
    UICallbacks()[handle] = std::move(callback);
    return handle;
}

void EditorLuaScript::InvokeUICallback(unsigned long long handle)
{
    if (!luaState_)
        return;

    const auto iter = UICallbacks().find(handle);
    if (iter == UICallbacks().end())
        return; // Callback dropped by a reload; ignore so the editor stays alive.

    sol::protected_function_result result = iter->second();
    if (!result.valid())
    {
        sol::error err = result;
        URHO3D_LOGERROR("EditorLua UI callback error: {}", err.what());
    }
}

void EditorLuaScript::InvokeEventCallback(sol::protected_function& callback, VariantMap& eventData)
{
    if (!luaState_)
        return;

    sol::protected_function_result result = callback(LuaEventData{&eventData});
    if (!result.valid())
    {
        sol::error err = result;
        URHO3D_LOGERROR("EditorLua event handler error: {}", err.what());
    }
}

void EditorLuaScript::RegisterEngineBindings()
{
    // Reuse the engine's exported binding modules so editor plugins get the full engine
    // API (Node/Component/Scene/resources/...) in the same environment as the "Editor" API.
    // Order matters: Resource must precede Graphics (Model/Material derive from Resource).
    RegisterMathBindings(*luaState_);
    RegisterCoreBindings(*luaState_, context_);
    RegisterNodeBindings(*luaState_, context_);
    RegisterResourceBindings(*luaState_, context_);
    RegisterGraphicsBindings(*luaState_, context_);
    RegisterInputBindings(*luaState_, context_);
    RegisterUIBindings(*luaState_, context_);
    RegisterPhysicsBindings(*luaState_, context_);
    RegisterUrho2DBindings(*luaState_, context_);
    RegisterPhysics2DBindings(*luaState_, context_);
    RegisterAudioBindings(*luaState_, context_);
    RegisterNavigationBindings(*luaState_, context_);
    RegisterNetworkBindings(*luaState_, context_);

    // Event data wrapper: parameters are looked up by name via dynamic indexing,
    // e.g. data.TimeStep, data.Node.
    luaState_->new_usertype<LuaEventData>("EventData",
        sol::no_constructor,
        sol::meta_function::index,
        [](LuaEventData& self, const char* name, sol::this_state s) -> sol::object
        {
            if (!self.eventData_)
                return sol::lua_nil;
            const auto iter = self.eventData_->find(StringHash(name));
            if (iter == self.eventData_->end())
                return sol::lua_nil;
            return VariantToLua(sol::state_view(s), iter->second);
        },
        "Contains", [](LuaEventData& self, const char* name) {
            return self.eventData_ && self.eventData_->find(StringHash(name)) != self.eventData_->end();
        });

    // Global subscription API, mirroring the game LuaScript state so shared helper
    // scripts behave the same way in the editor environment.
    luaState_->set_function("SubscribeToEvent",
        [this](const char* eventName, sol::protected_function callback)
        {
            SubscribeGlobalEvent(eventName, std::move(callback));
        });
    luaState_->set_function("UnsubscribeEvent",
        [this](const char* eventName) { UnsubscribeEvent(eventName); });
}

void EditorLuaScript::RegisterEditorBindings()
{
    sol::table editor = luaState_->create_table();

    // Logging helpers, prefixed so plugin output is easy to spot in the editor console.
    editor.set_function("log", [](const char* message) { URHO3D_LOGINFO("[EditorLua] {}", message); });
    editor.set_function("logWarning", [](const char* message) { URHO3D_LOGWARNING("[EditorLua] {}", message); });
    editor.set_function("logError", [](const char* message) { URHO3D_LOGERROR("[EditorLua] {}", message); });

    // Project access, forwarded to the editor through the hook table. Empty results when
    // no hook is installed or no project is open.
    editor.set_function("hasProject", []() {
        const auto& hooks = GetEditorLuaHooks();
        return hooks.hasProject ? hooks.hasProject() : false;
    });
    editor.set_function("getProjectDataPath", []() -> std::string {
        const auto& hooks = GetEditorLuaHooks();
        return hooks.getProjectDataPath ? std::string(hooks.getProjectDataPath().c_str()) : std::string();
    });
    editor.set_function("getProjectPath", []() -> std::string {
        const auto& hooks = GetEditorLuaHooks();
        return hooks.getProjectPath ? std::string(hooks.getProjectPath().c_str()) : std::string();
    });

    // Event bridge aliases so plugins can use Editor.subscribe(...) as well as the global one.
    editor.set_function("subscribe", [this](const char* eventName, sol::protected_function callback) {
        SubscribeGlobalEvent(eventName, std::move(callback));
    });
    editor.set_function("unsubscribe", [this](const char* eventName) { UnsubscribeEvent(eventName); });

    // Evaluate a Lua chunk on demand (handy for console-driven experimentation).
    editor.set_function("exec", [this](const char* code) { return ExecuteString(code); });

    // Re-run the last plugin directory (e.g. after editing plugin scripts).
    editor.set_function("reloadPlugins", [this]() {
        if (!pluginDir_.empty())
            LoadPlugins(pluginDir_);
    });

    // Create (or update, for an existing title) a dockable panel whose content a Lua function
    // draws every frame. Runs inside the ImGui frame, so any ui.* call is valid there.
    editor.set_function("addTab",
        [this](const std::string& title, sol::protected_function drawFunction) -> bool
        {
            if (!drawFunction.valid())
            {
                URHO3D_LOGERROR("Editor.addTab expects a Lua function as the draw callback");
                return false;
            }
            const auto& hooks = GetEditorLuaHooks();
            if (!hooks.addTab)
                return false;
            const auto handle = RegisterUICallback(std::move(drawFunction));
            return hooks.addTab(ea::string(title.c_str()), handle);
        });

    // Append a clickable item to the Project menu backed by a Lua function.
    editor.set_function("addMenuItem",
        [this](const std::string& label, sol::protected_function clickFunction) -> bool
        {
            if (!clickFunction.valid())
            {
                URHO3D_LOGERROR("Editor.addMenuItem expects a Lua function as the click callback");
                return false;
            }
            const auto& hooks = GetEditorLuaHooks();
            if (!hooks.addMenuItem)
                return false;
            const auto handle = RegisterUICallback(std::move(clickFunction));
            return hooks.addMenuItem(ea::string(label.c_str()), handle);
        });

    // Persistent floating window drawn every frame by the editor. The content function should
    // only emit widgets (no Begin/End); the editor wraps them and owns the title-bar close.
    // The window starts hidden; show it from a menu click via Editor.showWindow(title).
    editor.set_function("addWindow",
        [this](const std::string& title, sol::protected_function drawFunction, sol::optional<unsigned> flags) -> bool
        {
            if (!drawFunction.valid())
            {
                URHO3D_LOGERROR("Editor.addWindow expects a Lua function as the draw callback");
                return false;
            }
            const auto& hooks = GetEditorLuaHooks();
            if (!hooks.addWindow)
                return false;
            const auto handle = RegisterUICallback(std::move(drawFunction));
            return hooks.addWindow(ea::string(title.c_str()), handle, flags.value_or(0u));
        });
    editor.set_function("showWindow", [](const std::string& title) -> bool {
        const auto& hooks = GetEditorLuaHooks();
        return hooks.showWindow ? hooks.showWindow(ea::string(title.c_str())) : false;
    });
    editor.set_function("hideWindow", [](const std::string& title) -> bool {
        const auto& hooks = GetEditorLuaHooks();
        return hooks.hideWindow ? hooks.hideWindow(ea::string(title.c_str())) : false;
    });

    // Live editor context. Wrapped engine objects come back as the same usertypes the engine
    // bindings expose, so plugins can call e.g. node.Name or component.node directly.
    editor.set_function("getActiveScene", [this]() -> sol::object {
        const auto& hooks = GetEditorLuaHooks();
        Scene* scene = hooks.getActiveScene ? hooks.getActiveScene() : nullptr;
        return WrapLuaObject(*luaState_, scene);
    });
    editor.set_function("getActiveNode", [this]() -> sol::object {
        const auto& hooks = GetEditorLuaHooks();
        Node* node = hooks.getActiveNode ? hooks.getActiveNode() : nullptr;
        return WrapLuaObject(*luaState_, node);
    });
    editor.set_function("getSelection", [this](sol::this_state s) -> sol::object {
        sol::state_view lua(s);
        sol::table result = lua.create_table();
        const auto& hooks = GetEditorLuaHooks();

        Scene* scene = hooks.getActiveScene ? hooks.getActiveScene() : nullptr;
        Node* activeNode = hooks.getActiveNode ? hooks.getActiveNode() : nullptr;
        result["scene"] = WrapLuaObject(lua, scene);
        result["activeNode"] = WrapLuaObject(lua, activeNode);

        sol::table nodes = lua.create_table();
        sol::table components = lua.create_table();
        if (hooks.getSelected)
        {
            ea::vector<Node*> selectedNodes;
            ea::vector<Component*> selectedComponents;
            hooks.getSelected(selectedNodes, selectedComponents);
            for (size_t i = 0; i < selectedNodes.size(); ++i)
                nodes[static_cast<int>(i) + 1] = WrapLuaObject(lua, selectedNodes[i]);
            for (size_t i = 0; i < selectedComponents.size(); ++i)
                components[static_cast<int>(i) + 1] = WrapLuaObject(lua, selectedComponents[i]);
        }
        result["nodes"] = nodes;
        result["components"] = components;
        return result;
    });

    (*luaState_)["Editor"] = editor;
}

} // namespace Urho3D

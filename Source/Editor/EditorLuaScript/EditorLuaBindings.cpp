//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#include "EditorLuaBindings.h"

#include "EditorLuaVMHost.h"
#include "LuaEditorTab.h"
#include "LuaUIState.h"

#include <LuaScript/LuaBindings.h>

#include "../Tabs/SceneViewTab.h"
#include "../Build/BuildSettings.h"
#include "../Build/BuildSystem.h"
#include "../Project/Project.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/Core/Timer.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Scene/Component.h>
#include <Urho3D/Scene/Node.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/Utility/SceneSelection.h>

#include <sol/sol.hpp>

#include <string>

namespace Urho3D
{

// The "Editor" Lua table, created and fully owned by the editor. The host (LuaVMHost) is a
// generic VM with no API tables of its own; this fills the state with everything a plugin
// sees: VM plumbing (log, subscribe, exec) plus the capabilities that need editor types
// (project access, UI registration, live scene context, build pipeline). Each lambda resolves
// the live subsystems on call, so the functions stay correct as projects open and close.
namespace
{

// Absolute stamp from the Time subsystem's elapsed clock; 0 before the subsystem exists.
float NowElapsed(Context* context)
{
    auto* time = context->GetSubsystem<Time>();
    return time ? time->GetElapsedTime() : 0.0f;
}

// Resolve a Lua value that came from WrapLuaObject back to a live engine object pointer. The
// wrappers are either a concrete bound usertype (Node/Component/...) or the generic LuaObjectRef
// fallback; both funnel down to Object*. Returns null for anything that is not a live object.
Object* ToEngineObject(const sol::object& value)
{
    if (!value.valid() || value.get_type() != sol::type::userdata)
        return nullptr;
    if (value.is<LuaObjectRef>())
        return value.as<LuaObjectRef>().Get();
    if (value.is<Node*>())
        return value.as<Node*>();
    if (value.is<Component*>())
        return value.as<Component*>();
    if (value.is<Object*>())
        return value.as<Object*>();
    return nullptr;
}

// Select a single object or every object in an array (1-based Lua table), leaving the rest of the
// selection as it is. Non-object entries are ignored.
void ApplySelection(SceneSelection& selection, const sol::object& target, bool activate)
{
    if (target.is<sol::table>())
    {
        sol::table array = target.as<sol::table>();
        for (auto& pair : array)
            if (Object* object = ToEngineObject(pair.second))
                selection.SetSelected(object, true, activate);
        return;
    }
    if (Object* object = ToEngineObject(target))
        selection.SetSelected(object, true, activate);
}

// Unique ImGui popup id for a modal; the ## suffix hides the counter from the visible title.
ea::string MakeModalId(const std::string& title)
{
    static unsigned counter = 0;
    return ea::string(("LuaModal##" + std::to_string(++counter) + "_" + title).c_str());
}

} // namespace

void RegisterEditorLuaAPI(Context* context)
{
    auto* editorLua = context->GetSubsystem<EditorLuaVMHost>();
    if (!editorLua)
        return;

    sol::state_view lua(editorLua->GetState());
    sol::table editor = lua.create_named_table("Editor");

    // Logging helpers, prefixed so plugin output is easy to spot in the editor console.
    editor.set_function("log", [](const char* message) { URHO3D_LOGINFO("[EditorLua] {}", message); });
    editor.set_function("logWarning", [](const char* message) { URHO3D_LOGWARNING("[EditorLua] {}", message); });
    editor.set_function("logError", [](const char* message) { URHO3D_LOGERROR("[EditorLua] {}", message); });

    // Event bridge aliases so plugins can use Editor.subscribe(...) as well as the global one.
    editor.set_function("subscribe",
        [host = editorLua](const char* eventName, sol::protected_function callback)
        {
            host->SubscribeGlobalEvent(eventName, std::move(callback));
        });
    editor.set_function("unsubscribe",
        [host = editorLua](const char* eventName) { host->UnsubscribeEvent(eventName); });

    // Evaluate a Lua chunk on demand (handy for console-driven experimentation).
    editor.set_function("exec", [host = editorLua](const char* code) { return host->ExecuteString(code); });

    // Project access. Empty results when no project is open.
    editor.set_function("hasProject", [context]() {
        return context->GetSubsystem<Project>() != nullptr;
    });
    editor.set_function("getProjectDataPath", [context]() -> std::string {
        auto* project = context->GetSubsystem<Project>();
        return project ? std::string(project->GetDataPath().c_str()) : std::string();
    });
    editor.set_function("getProjectPath", [context]() -> std::string {
        auto* project = context->GetSubsystem<Project>();
        return project ? std::string(project->GetProjectPath().c_str()) : std::string();
    });

    // Re-run the last plugin directory. Resetting the editor-side UI bookkeeping has to happen
    // on this side of the boundary, which is why the function lives here.
    editor.set_function("reloadPlugins", [context]() {
        auto* lua = context->GetSubsystem<EditorLuaVMHost>();
        if (!lua)
            return;
        Detail::ResetLuaUI();
        lua->ReloadPlugins();
    });

    // Create (or update, for an existing title) a dockable panel whose content a Lua function
    // draws every frame. Runs inside the ImGui frame, so any imgui.* call is valid there.
    editor.set_function("addTab",
        [context, editorLua](const std::string& title, sol::protected_function drawFunction) -> bool
        {
            if (!drawFunction.valid())
            {
                URHO3D_LOGERROR("Editor.addTab expects a Lua function as the draw callback");
                return false;
            }
            auto* project = context->GetSubsystem<Project>();
            if (!project)
                return false;

            const ea::string titleStr = ea::string(title.c_str());
            for (WeakPtr<LuaEditorTab>& weak : Detail::LuaTabs())
            {
                if (LuaEditorTab* tab = weak.Get())
                {
                    if (tab->GetTitle() == titleStr)
                    {
                        tab->SetHandle(editorLua->RegisterCallback(std::move(drawFunction)));
                        return true;
                    }
                }
            }

            const auto tab = MakeShared<LuaEditorTab>(context, titleStr,
                editorLua->RegisterCallback(std::move(drawFunction)));
            project->AddTab(tab);
            // OpenByDefault only takes effect during a layout reset, which does not re-run for
            // tabs added after project construction. Focus explicitly so the panel actually
            // shows up and its content renders every frame.
            tab->Focus();
            Detail::LuaTabs().push_back(tab);
            return true;
        });

    // Append a clickable item to the Project menu backed by a Lua function. The label may
    // encode a menu path: "Tools/Test" puts "Test" into a top-level "Tools" menu, which is
    // reused when the editor already has one and created otherwise; deeper segments become
    // nested submenus. A label without '/' is placed in the Project menu.
    editor.set_function("addMenuItem",
        [context, editorLua](const std::string& label, sol::protected_function clickFunction) -> bool
        {
            if (!clickFunction.valid())
            {
                URHO3D_LOGERROR("Editor.addMenuItem expects a Lua function as the click callback");
                return false;
            }
            if (!context->GetSubsystem<Project>())
                return false;
            const auto handle = editorLua->RegisterCallback(std::move(clickFunction));
            Detail::LuaMenuItems().push_back(Detail::LuaMenuItem{ ea::string(label.c_str()), handle });
            return true;
        });

    // Persistent floating window drawn every frame by the editor. The content function should
    // only emit widgets (no Begin/End); the editor wraps them and owns the title-bar close.
    // The window starts hidden; show it from a menu click via Editor.showWindow(title).
    editor.set_function("addWindow",
        [context, editorLua](const std::string& title, sol::protected_function drawFunction,
            sol::optional<unsigned> flags) -> bool
        {
            if (!drawFunction.valid())
            {
                URHO3D_LOGERROR("Editor.addWindow expects a Lua function as the draw callback");
                return false;
            }
            if (!context->GetSubsystem<Project>())
                return false;
            const ea::string titleStr = ea::string(title.c_str());
            const auto handle = editorLua->RegisterCallback(std::move(drawFunction));
            for (Detail::LuaWindow& window : Detail::LuaWindows())
            {
                if (window.title == titleStr)
                {
                    window.handle = handle;
                    window.flags = flags.value_or(0u);
                    return true;
                }
            }
            Detail::LuaWindows().push_back(
                Detail::LuaWindow{ titleStr, handle, false, flags.value_or(0u) });
            return true;
        });
    editor.set_function("showWindow", [](const std::string& title) -> bool {
        for (Detail::LuaWindow& window : Detail::LuaWindows())
            if (window.title == ea::string(title.c_str()))
            {
                window.visible = true;
                return true;
            }
        return false;
    });
    editor.set_function("hideWindow", [](const std::string& title) -> bool {
        for (Detail::LuaWindow& window : Detail::LuaWindows())
            if (window.title == ea::string(title.c_str()))
            {
                window.visible = false;
                return true;
            }
        return false;
    });

    // Live editor context. Wrapped engine objects come back as the same usertypes the engine
    // bindings expose, so plugins can call e.g. node.Name or component.node directly.
    editor.set_function("getActiveScene", [context, editorLua]() -> sol::object {
        SceneViewPage* page = Detail::ActiveSceneViewPage(context);
        return WrapLuaObject(editorLua->GetState(), page ? page->scene_.Get() : nullptr);
    });
    editor.set_function("getActiveNode", [context, editorLua]() -> sol::object {
        SceneViewPage* page = Detail::ActiveSceneViewPage(context);
        return WrapLuaObject(editorLua->GetState(), page ? page->selection_.GetActiveNode() : nullptr);
    });
    editor.set_function("getSelection", [context](sol::this_state s) -> sol::object {
        sol::state_view lua(s);
        sol::table result = lua.create_table();
        SceneViewPage* page = Detail::ActiveSceneViewPage(context);

        Scene* scene = page ? page->scene_.Get() : nullptr;
        Node* activeNode = page ? page->selection_.GetActiveNode() : nullptr;
        result["scene"] = WrapLuaObject(lua, scene);
        result["activeNode"] = WrapLuaObject(lua, activeNode);

        sol::table nodes = lua.create_table();
        sol::table components = lua.create_table();
        if (page)
        {
            SceneSelection& selection = page->selection_;
            int index = 0;
            for (const WeakPtr<Node>& node : selection.GetNodes())
                if (Node* raw = node.Get())
                    nodes[++index] = WrapLuaObject(lua, raw);
            index = 0;
            for (const WeakPtr<Component>& component : selection.GetComponents())
                if (Component* raw = component.Get())
                    components[++index] = WrapLuaObject(lua, raw);
        }
        result["nodes"] = nodes;
        result["components"] = components;
        return result;
    });

    // Build pipeline. The editor keeps all of its state, so a plugin never holds a piece of a
    // running build: it asks what can be built, asks for a build, and looks at the status.
    editor.set_function("buildPlatforms", [context](sol::this_state s) -> sol::object
    {
        sol::state_view lua(s);
        sol::table result = lua.create_table();
        auto* project = context->GetSubsystem<Project>();
        auto* settings = project ? project->GetBuildSettings() : nullptr;
        if (settings)
        {
            const ea::vector<ea::string> names = settings->GetPlatformNames();
            for (size_t i = 0; i < names.size(); ++i)
                result[static_cast<int>(i) + 1] = std::string(names[i].c_str());
        }
        return result;
    });

    // Starting a build is asynchronous: the call answers whether it was accepted, and the outcome
    // arrives later either through the optional callback (as the same EventData table the
    // "buildFinished" event carries) or through that event alone. A plugin that builds several
    // platforms in sequence chains them from the callback, which is why the callback is one-shot.
    editor.set_function("build",
        [context, editorLua](const std::string& platform,
            sol::optional<sol::protected_function> callback) -> bool
        {
            if (callback && !callback->valid())
            {
                URHO3D_LOGERROR("Editor.build expects a Lua function as the completion callback");
                return false;
            }
            auto* project = context->GetSubsystem<Project>();
            auto* build = project ? project->GetBuildSystem() : nullptr;
            if (!build)
            {
                URHO3D_LOGERROR("Editor.build needs an open project, and none is open");
                return false;
            }

            const ea::string platformName = ea::string(platform.c_str());
            const unsigned long long handle =
                callback ? editorLua->RegisterCallback(std::move(*callback)) : 0ull;
            // The reason a refused build is not reported here is that BuildNow already logged it:
            // a missing platform names the platforms that do exist, and a running build says which.
            if (!build->BuildNow(platformName, EMPTY_STRING,
                [context, handle, platformName](bool success, const ea::string& message, const ea::string& outputDir)
                {
                    auto* lua = context->GetSubsystem<EditorLuaVMHost>();
                    if (!lua || handle == 0ull)
                        return;
                    // Spelled out rather than forwarded from the event, because the handler of a
                    // build runs before the build is torn down and gets the same values.
                    VariantMap eventData;
                    eventData["Success"] = success;
                    eventData["Platform"] = platformName;
                    eventData["Message"] = success ? EMPTY_STRING : message;
                    eventData["OutputDir"] = outputDir;
                    lua->InvokeOneShotCallback(handle, eventData);
                }))
            {
                // The build was refused before it ran, so nothing will ever echo the handle back.
                editorLua->DropCallback(handle);
                return false;
            }
            return true;
        });

    editor.set_function("buildStatus", [context](sol::this_state s) -> sol::object
    {
        sol::state_view lua(s);
        sol::table result = lua.create_table();

        auto* project = context->GetSubsystem<Project>();
        auto* build = project ? project->GetBuildSystem() : nullptr;
        result["building"] = build && build->IsBuilding();
        result["progress"] = build ? build->GetProgress() : 0.0f;
        result["stage"] = std::string(build ? build->GetStageName().c_str() : "");
        result["platform"] = std::string(build ? build->GetPlatformName().c_str() : "");
        result["outputDir"] = std::string(build ? build->GetOutputDir().c_str() : "");

        sol::table errors = lua.create_table();
        if (build)
        {
            const ea::vector<ea::string>& buildErrors = build->GetErrors();
            for (size_t i = 0; i < buildErrors.size(); ++i)
                errors[static_cast<int>(i) + 1] = std::string(buildErrors[i].c_str());
        }
        result["errors"] = errors;
        return result;
    });

    // ---------------------------------------------------------------------------
    // Editor.project -- dirty marking and save (P0).
    // ---------------------------------------------------------------------------
    sol::table projectApi = editor.create_named("project");
    projectApi.set_function("hasProject", [context]() -> bool {
        return context->GetSubsystem<Project>() != nullptr;
    });
    projectApi.set_function("path", [context]() -> std::string {
        auto* project = context->GetSubsystem<Project>();
        return project ? std::string(project->GetProjectPath().c_str()) : std::string();
    });
    projectApi.set_function("dataPath", [context]() -> std::string {
        auto* project = context->GetSubsystem<Project>();
        return project ? std::string(project->GetDataPath().c_str()) : std::string();
    });
    // Mark the project as having unsaved changes so the title bar shows the dirty indicator and a
    // close prompts. Use it after mutating the scene from a plugin (the engine bindings do not mark
    // it on their own).
    projectApi.set_function("markDirty", [context]() -> bool {
        auto* project = context->GetSubsystem<Project>();
        if (!project)
            return false;
        project->MarkUnsaved();
        return true;
    });
    projectApi.set_function("isDirty", [context]() -> bool {
        auto* project = context->GetSubsystem<Project>();
        return project && project->HasUnsavedChanges();
    });
    // Save the active scene through the very routine Ctrl+S uses (the scene view tab's current
    // resource). Returns false when no project or scene view is open.
    projectApi.set_function("save", [context]() -> bool {
        auto* project = context->GetSubsystem<Project>();
        if (!project)
            return false;
        auto* view = project->FindTab<SceneViewTab>();
        if (!view)
            return false;
        view->SaveCurrentResource();
        return true;
    });

    // ---------------------------------------------------------------------------
    // Editor.selection -- read and write the live scene selection, plus a change callback.
    // Objects flow in and out through the same wrappers the engine bindings use.
    // ---------------------------------------------------------------------------
    sol::table selectionApi = editor.create_named("selection");
    selectionApi.set_function("scene", [context, editorLua]() -> sol::object {
        SceneViewPage* page = Detail::ActiveSceneViewPage(context);
        return WrapLuaObject(editorLua->GetState(), page ? page->scene_.Get() : nullptr);
    });
    selectionApi.set_function("activeNode", [context, editorLua]() -> sol::object {
        SceneViewPage* page = Detail::ActiveSceneViewPage(context);
        return WrapLuaObject(editorLua->GetState(), page ? page->selection_.GetActiveNode() : nullptr);
    });
    selectionApi.set_function("nodes", [context](sol::this_state s) -> sol::object {
        sol::state_view lua(s);
        sol::table result = lua.create_table();
        SceneViewPage* page = Detail::ActiveSceneViewPage(context);
        if (page)
        {
            int index = 0;
            for (const WeakPtr<Node>& node : page->selection_.GetNodes())
                if (Node* raw = node.Get())
                    result[++index] = WrapLuaObject(lua, raw);
        }
        return result;
    });
    selectionApi.set_function("components", [context](sol::this_state s) -> sol::object {
        sol::state_view lua(s);
        sol::table result = lua.create_table();
        SceneViewPage* page = Detail::ActiveSceneViewPage(context);
        if (page)
        {
            int index = 0;
            for (const WeakPtr<Component>& component : page->selection_.GetComponents())
                if (Component* raw = component.Get())
                    result[++index] = WrapLuaObject(lua, raw);
        }
        return result;
    });
    // set replaces the selection, add extends it; each takes one object or an array of objects.
    selectionApi.set_function("set", [context](sol::object target, sol::optional<bool> activate) -> bool {
        SceneViewPage* page = Detail::ActiveSceneViewPage(context);
        if (!page)
            return false;
        page->selection_.Clear();
        ApplySelection(page->selection_, target, activate.value_or(true));
        return true;
    });
    selectionApi.set_function("add", [context](sol::object target, sol::optional<bool> activate) -> bool {
        SceneViewPage* page = Detail::ActiveSceneViewPage(context);
        if (!page)
            return false;
        ApplySelection(page->selection_, target, activate.value_or(true));
        return true;
    });
    selectionApi.set_function("clear", [context]() -> bool {
        SceneViewPage* page = Detail::ActiveSceneViewPage(context);
        if (!page)
            return false;
        page->selection_.Clear();
        return true;
    });
    // Register a callback fired whenever the active selection changes (the editor polls the packed
    // selection each frame, so it also catches changes made by the user or other tabs). Callbacks
    // read the new selection via the getters; they are cleared on plugin reload.
    selectionApi.set_function("onChanged",
        [host = editorLua](sol::protected_function callback) -> bool {
            if (!callback.valid())
                return false;
            Detail::LuaSelectionCallbacks().push_back(host->RegisterCallback(std::move(callback)));
            return true;
        });

    // ---------------------------------------------------------------------------
    // Editor.tick -- run a callback later on the editor loop. Pumped every frame from the plugin
    // window pass; handles are the host's callback handles so a reload can never invoke a stale one.
    // ---------------------------------------------------------------------------
    sol::table tickApi = editor.create_named("tick");
    tickApi.set_function("defer", [host = editorLua](sol::protected_function callback) -> bool {
        if (!callback.valid())
            return false;
        Detail::LuaScheduledTask task;
        task.handle = host->RegisterCallback(std::move(callback));
        task.fireTime = 0.0f;
        task.interval = 0.0f;
        task.nextFrame = true;
        Detail::LuaScheduledTasks().push_back(task);
        return true;
    });
    tickApi.set_function("after", [context, host = editorLua](double seconds,
                             sol::protected_function callback) -> bool {
        if (!callback.valid())
            return false;
        Detail::LuaScheduledTask task;
        task.handle = host->RegisterCallback(std::move(callback));
        task.fireTime = NowElapsed(context) + static_cast<float>(seconds);
        task.interval = 0.0f;
        task.nextFrame = false;
        Detail::LuaScheduledTasks().push_back(task);
        return true;
    });
    tickApi.set_function("every", [context, host = editorLua](double seconds,
                             sol::protected_function callback) -> unsigned long long {
        if (!callback.valid())
            return 0ull;
        Detail::LuaScheduledTask task;
        task.handle = host->RegisterCallback(std::move(callback));
        task.fireTime = NowElapsed(context) + static_cast<float>(seconds);
        task.interval = static_cast<float>(seconds);
        task.nextFrame = false;
        Detail::LuaScheduledTasks().push_back(task);
        return task.handle;
    });
    tickApi.set_function("cancel", [host = editorLua](unsigned long long handle) -> bool {
        auto& tasks = Detail::LuaScheduledTasks();
        for (size_t i = 0; i < tasks.size(); ++i)
        {
            if (tasks[i].handle == handle)
            {
                tasks.erase(tasks.begin() + i);
                host->DropCallback(handle);
                return true;
            }
        }
        return false;
    });

    // ---------------------------------------------------------------------------
    // Editor.ui -- transient toast notifications and simple modal dialogs (ImGui based). Toasts and
    // modals are drawn by the per-frame plugin pass.
    // ---------------------------------------------------------------------------
    sol::table uiApi = editor.create_named("ui");
    uiApi.set_function("notify", [context](const std::string& text,
                           sol::optional<double> seconds) -> bool {
        Detail::LuaToast toast;
        toast.text = ea::string(text.c_str());
        toast.expireTime = NowElapsed(context) + static_cast<float>(seconds.value_or(3.0));
        Detail::LuaToasts().push_back(toast);
        return true;
    });
    uiApi.set_function("confirm", [context, host = editorLua](const std::string& title,
                                   const std::string& text, sol::protected_function onYes,
                                   sol::optional<sol::protected_function> onNo) -> bool {
        if (!onYes.valid())
            return false;
        Detail::LuaModal modal;
        modal.kind = Detail::LuaModal::Confirm;
        modal.id = MakeModalId(title);
        modal.title = ea::string(title.c_str());
        modal.text = ea::string(text.c_str());
        modal.onConfirm = host->RegisterCallback(std::move(onYes));
        modal.onCancel = (onNo && onNo->valid()) ? host->RegisterCallback(std::move(*onNo)) : 0ull;
        modal.opened = false;
        Detail::LuaModals().push_back(modal);
        return true;
    });
    uiApi.set_function("input", [context, host = editorLua](const std::string& title,
                                 const std::string& label, const std::string& defaultText,
                                 sol::protected_function onDone) -> bool {
        if (!onDone.valid())
            return false;
        Detail::LuaModal modal;
        modal.kind = Detail::LuaModal::Input;
        modal.id = MakeModalId(title);
        modal.title = ea::string(title.c_str());
        modal.label = ea::string(label.c_str());
        modal.input = ea::string(defaultText.c_str());
        modal.onConfirm = host->RegisterCallback(std::move(onDone));
        modal.onCancel = 0ull;
        modal.opened = false;
        Detail::LuaModals().push_back(modal);
        return true;
    });

    // ---------------------------------------------------------------------------
    // Editor.undo -- reserved. Real undo integration (funneling Lua scene edits onto the editor's
    // UndoManager) is a deliberate follow-up; direct engine-binding writes are not undoable yet.
    // ---------------------------------------------------------------------------
    editor.create_named("undo");
}

} // namespace Urho3D

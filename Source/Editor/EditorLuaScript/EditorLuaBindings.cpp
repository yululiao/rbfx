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
#include <Urho3D/IO/Log.h>
#include <Urho3D/Scene/Component.h>
#include <Urho3D/Scene/Node.h>
#include <Urho3D/Scene/Scene.h>

#include <sol/sol.hpp>

#include <string>

namespace Urho3D
{

// The "Editor" Lua table, created and fully owned by the editor. The host (LuaVMHost) is a
// generic VM with no API tables of its own; this fills the state with everything a plugin
// sees: VM plumbing (log, subscribe, exec) plus the capabilities that need editor types
// (project access, UI registration, live scene context, build pipeline). Each lambda resolves
// the live subsystems on call, so the functions stay correct as projects open and close.
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
    editor.set_function("buildProfiles", [context](sol::this_state s) -> sol::object
    {
        sol::state_view lua(s);
        sol::table result = lua.create_table();
        auto* project = context->GetSubsystem<Project>();
        auto* settings = project ? project->GetBuildSettings() : nullptr;
        if (settings)
        {
            const ea::vector<ea::string> names = settings->GetProfileNames();
            for (size_t i = 0; i < names.size(); ++i)
                result[static_cast<int>(i) + 1] = std::string(names[i].c_str());
        }
        return result;
    });

    // Starting a build is asynchronous: the call answers whether it was accepted, and the outcome
    // arrives later either through the optional callback (as the same EventData table the
    // "buildFinished" event carries) or through that event alone. A plugin that builds several
    // profiles in sequence chains them from the callback, which is why the callback is one-shot.
    editor.set_function("build",
        [context, editorLua](const std::string& profile,
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

            const ea::string profileName = ea::string(profile.c_str());
            const unsigned long long handle =
                callback ? editorLua->RegisterCallback(std::move(*callback)) : 0ull;
            // The reason a refused build is not reported here is that BuildNow already logged it:
            // a missing profile names the profiles that do exist, and a running build says which.
            if (!build->BuildNow(profileName, EMPTY_STRING,
                [context, handle, profileName](bool success, const ea::string& message, const ea::string& outputDir)
                {
                    auto* lua = context->GetSubsystem<EditorLuaVMHost>();
                    if (!lua || handle == 0ull)
                        return;
                    // Spelled out rather than forwarded from the event, because the handler of a
                    // build runs before the build is torn down and gets the same values.
                    VariantMap eventData;
                    eventData["Success"] = success;
                    eventData["Profile"] = profileName;
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
        result["profile"] = std::string(build ? build->GetProfileName().c_str() : "");
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
}

} // namespace Urho3D

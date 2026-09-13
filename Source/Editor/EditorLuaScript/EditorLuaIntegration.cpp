//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#include "EditorLuaIntegration.h"

#ifdef URHO3D_LUA

#include <LuaScript/EditorLuaHooks.h>
#include <LuaScript/EditorLuaScript.h>

#include "LuaEditorTab.h"

#include "../Project/Project.h"
#include "../Foundation/SceneViewTab.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/Core/Object.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/SystemUI/SystemUI.h>

#include <EASTL/algorithm.h>
#include <EASTL/map.h>

namespace Urho3D
{

namespace
{

// Menu item registered by a Lua plugin; clicking invokes the DLL-side callback by handle.
struct LuaMenuItem
{
    ea::string label;
    unsigned long long handle;
};

ea::vector<LuaMenuItem>& LuaMenuItems()
{
    static ea::vector<LuaMenuItem> items;
    return items;
}

// Weak references to the tabs created for the current project, so a plugin reload can update an
// existing tab's callback by title instead of stacking duplicates. Expired entries are pruned on
// each (re)load, which also covers the case where the owning project has been closed.
ea::vector<WeakPtr<LuaEditorTab>>& LuaTabs()
{
    static ea::vector<WeakPtr<LuaEditorTab>> tabs;
    return tabs;
}

// Floating window registered by a Lua plugin. The editor draws it every frame (independent of
// any dock tab), wrapping the content callback between its own Begin/End. 'visible' is owned by
// the editor so the title-bar close works; Lua toggles it by title via showWindow/hideWindow.
struct LuaWindow
{
    ea::string title;
    unsigned long long handle;
    bool visible;
    unsigned int flags;
};

ea::vector<LuaWindow>& LuaWindows()
{
    static ea::vector<LuaWindow> windows;
    return windows;
}

/// Return the scene-view page currently being edited (its scene + selection), or null.
SceneViewPage* ActiveSceneViewPage(Context* context)
{
    auto* project = context->GetSubsystem<Project>();
    if (!project)
        return nullptr;
    auto* view = project->FindTab<SceneViewTab>();
    return view ? view->GetActivePage() : nullptr;
}

// A menu label expressed relative to the menu level currently being rendered, i.e. the leading
// path segments of an "a/b/Item" style label have already been consumed by parent menus.
struct LuaMenuSlice
{
    ea::string path;
    unsigned long long handle;
};

/// Render one menu level inside the currently open menu: slices without '/' become clickable
/// items, the rest are grouped by their next segment into submenus rendered recursively.
void RenderLuaMenuLevel(const ea::vector<LuaMenuSlice>& slices, EditorLuaScript* lua)
{
    ea::vector<const LuaMenuSlice*> leaves;
    ea::map<ea::string, ea::vector<const LuaMenuSlice*>> submenus;
    for (const LuaMenuSlice& slice : slices)
    {
        const auto slash = slice.path.find('/');
        if (slash == ea::string::npos)
            leaves.push_back(&slice);
        else
            submenus[slice.path.substr(0, slash)].push_back(&slice);
    }

    for (const LuaMenuSlice* leaf : leaves)
    {
        if (ui::MenuItem(leaf->path.c_str()))
            lua->InvokeUICallback(leaf->handle);
    }
    for (auto& pair : submenus)
    {
        if (ui::BeginMenu(pair.first.c_str()))
        {
            ea::vector<LuaMenuSlice> children;
            for (const LuaMenuSlice* item : pair.second)
            {
                const auto slash = item->path.find('/');
                children.push_back(LuaMenuSlice{ item->path.substr(slash + 1), item->handle });
            }
            RenderLuaMenuLevel(children, lua);
            ui::EndMenu();
        }
    }
}

/// Collect the Lua menu items belonging to a top-level menu, with that first segment stripped off.
ea::vector<LuaMenuSlice> CollectLuaMenuChildren(const ea::string& topName)
{
    ea::vector<LuaMenuSlice> slices;
    for (const LuaMenuItem& item : LuaMenuItems())
    {
        const auto slash = item.label.find('/');
        if (slash == ea::string::npos)
            continue;
        if (item.label.substr(0, slash) == topName)
            slices.push_back(LuaMenuSlice{ item.label.substr(slash + 1), item.handle });
    }
    return slices;
}

} // namespace

void SetupEditorLua(Context* context)
{
    // Provide editor capabilities to the DLL-side "Editor" API. Each hook queries the live
    // subsystems on call, so it stays correct as projects open/close. No editor type crosses
    // into the DLL; only allocator-safe engine values are exchanged.
    EditorLuaHooks hooks;
    hooks.hasProject = [context]() { return context->GetSubsystem<Project>() != nullptr; };
    hooks.getProjectDataPath = [context]() -> ea::string
    {
        auto* project = context->GetSubsystem<Project>();
        return project ? project->GetDataPath() : ea::string();
    };
    hooks.getProjectPath = [context]() -> ea::string
    {
        auto* project = context->GetSubsystem<Project>();
        return project ? project->GetProjectPath() : ea::string();
    };

    // A Lua plugin asks for a dockable panel; create it once per title and, on reload, retarget
    // the existing tab at the newly registered callback so its content refreshes in place.
    hooks.addTab = [context](const ea::string& title, unsigned long long handle) -> bool
    {
        auto* project = context->GetSubsystem<Project>();
        if (!project)
            return false;

        for (WeakPtr<LuaEditorTab>& weak : LuaTabs())
        {
            if (LuaEditorTab* tab = weak.Get())
            {
                if (tab->GetTitle() == title)
                {
                    tab->SetHandle(handle);
                    return true;
                }
            }
        }

        const auto tab = MakeShared<LuaEditorTab>(context, title, handle);
        project->AddTab(tab);
        // OpenByDefault only takes effect during a layout reset, which does not re-run for tabs
        // added after project construction. Focus explicitly so the panel actually shows up and
        // its content renders every frame.
        tab->Focus();
        LuaTabs().push_back(tab);
        return true;
    };

    hooks.addMenuItem = [context](const ea::string& label, unsigned long long handle) -> bool
    {
        if (!context->GetSubsystem<Project>())
            return false;
        LuaMenuItems().push_back(LuaMenuItem{ label, handle });
        return true;
    };

    hooks.addWindow = [context](const ea::string& title, unsigned long long handle, unsigned flags) -> bool
    {
        if (!context->GetSubsystem<Project>())
            return false;
        for (LuaWindow& window : LuaWindows())
        {
            if (window.title == title)
            {
                window.handle = handle;
                window.flags = flags;
                return true;
            }
        }
        LuaWindows().push_back(LuaWindow{ title, handle, false, flags });
        return true;
    };
    hooks.showWindow = [](const ea::string& title) -> bool
    {
        for (LuaWindow& window : LuaWindows())
            if (window.title == title)
            {
                window.visible = true;
                return true;
            }
        return false;
    };
    hooks.hideWindow = [](const ea::string& title) -> bool
    {
        for (LuaWindow& window : LuaWindows())
            if (window.title == title)
            {
                window.visible = false;
                return true;
            }
        return false;
    };

    hooks.getActiveScene = [context]() -> Scene*
    {
        auto* page = ActiveSceneViewPage(context);
        return page ? page->scene_.Get() : nullptr;
    };
    hooks.getActiveNode = [context]() -> Node*
    {
        auto* page = ActiveSceneViewPage(context);
        return page ? page->selection_.GetActiveNode() : nullptr;
    };
    hooks.getSelected = [context](ea::vector<Node*>& outNodes, ea::vector<Component*>& outComponents)
    {
        auto* page = ActiveSceneViewPage(context);
        if (!page)
            return;
        SceneSelection& selection = page->selection_;
        for (const WeakPtr<Node>& node : selection.GetNodes())
        {
            if (Node* raw = node.Get())
                outNodes.push_back(raw);
        }
        for (const WeakPtr<Component>& component : selection.GetComponents())
        {
            if (Component* raw = component.Get())
                outComponents.push_back(raw);
        }
    };

    // Build pipeline. Every hook resolves the project afresh, so closing a project turns them
    // into refusals rather than accesses through a pointer to a destroyed BuildSystem.
    hooks.getBuildProfiles = [context]() -> ea::vector<ea::string>
    {
        auto* project = context->GetSubsystem<Project>();
        auto* settings = project ? project->GetBuildSettings() : nullptr;
        return settings ? settings->GetProfileNames() : ea::vector<ea::string>();
    };

    hooks.buildProfile = [context](const ea::string& profile, unsigned long long handle) -> bool
    {
        auto* project = context->GetSubsystem<Project>();
        auto* build = project ? project->GetBuildSystem() : nullptr;
        if (!build)
        {
            URHO3D_LOGERROR("Editor.build needs an open project, and none is open");
            return false;
        }

        // The reason a refused build is not reported here is that BuildNow already logged it: a
        // missing profile names the profiles that do exist, and a running build says which.
        return build->BuildNow(profile, EMPTY_STRING,
            [context, handle, profile](bool success, const ea::string& message, const ea::string& outputDir)
            {
                auto* lua = context->GetSubsystem<EditorLuaScript>();
                if (!lua || handle == 0ull)
                    return;
                // Spelled out rather than forwarded from the event, because the handler of a build
                // runs before the build is torn down and gets the same values the event carried.
                VariantMap eventData;
                eventData["Success"] = success;
                eventData["Profile"] = profile;
                eventData["Message"] = success ? EMPTY_STRING : message;
                eventData["OutputDir"] = outputDir;
                lua->InvokeOneShotCallback(handle, eventData);
            });
    };

    hooks.getBuildStatus = [context]() -> EditorBuildStatus
    {
        EditorBuildStatus status;
        auto* project = context->GetSubsystem<Project>();
        auto* build = project ? project->GetBuildSystem() : nullptr;
        if (build)
        {
            status.building = build->IsBuilding();
            status.progress = build->GetProgress();
            status.stage = build->GetStageName();
            status.profile = build->GetProfileName();
            status.outputDir = build->GetOutputDir();
            status.errors = build->GetErrors();
        }
        return status;
    };

    hooks.resetUI = []()
    {
        LuaMenuItems().clear();
        LuaWindows().clear();
        auto& tabs = LuaTabs();
        tabs.erase(ea::remove_if(tabs.begin(), tabs.end(),
                       [](const WeakPtr<LuaEditorTab>& weak) { return !weak.Get(); }),
            tabs.end());
    };

    SetEditorLuaHooks(hooks);

    // Bring up the dedicated editor Lua VM (owned and driven inside RbfxLuaScript).
    const auto editorLua = MakeShared<EditorLuaScript>(context);
    context->RegisterSubsystem(editorLua);
    editorLua->Initialize();
}

void ReloadEditorLuaPlugins(Context* context)
{
    auto* project = context->GetSubsystem<Project>();
    auto* editorLua = context->GetSubsystem<EditorLuaScript>();
    if (!project || !editorLua)
        return;

    // Render registered Lua menu items at the end of the Project menu. The subscription uses the
    // editor Lua subsystem as its receiver so it is dropped if that outlives the project; a fresh
    // subscription is added for every newly opened project (Project is recreated on each open).
    project->OnRenderProjectMenu.Subscribe(editorLua, [context]()
    {
        auto* lua = context->GetSubsystem<EditorLuaScript>();
        if (!lua || LuaMenuItems().empty())
            return;

        // Only plain labels (no '/') live in the Project menu. Path labels such as "Tools/Item"
        // describe their own top-level menu and are rendered by RenderLuaMenuEntries /
        // RenderLuaTopMenus in the main menu bar instead.
        bool separatorDrawn = false;
        for (const LuaMenuItem& item : LuaMenuItems())
        {
            if (item.label.find('/') != ea::string::npos)
                continue;
            if (!separatorDrawn)
            {
                ui::Separator();
                separatorDrawn = true;
            }
            if (ui::MenuItem(item.label.c_str()))
                lua->InvokeUICallback(item.handle);
        }
    });

    // Convention: per-project editor plugins live in an "EditorScripts" folder at the project root
    // (kept separate from the game's Data/Scripts so editor-only tooling never ships with a build).
    // GetProjectPath() already ends with a '/', so no separator is added here.
    editorLua->LoadPlugins(project->GetProjectPath() + "EditorScripts");
}

void RenderLuaWindows(Context* context)
{
    // Draw every Lua window whose flag is set. This runs at the top level of the editor frame
    // (like the About dialog), so the windows persist regardless of which dock tab is focused.
    if (LuaWindows().empty())
        return;
    auto* lua = context->GetSubsystem<EditorLuaScript>();
    if (!lua)
        return;

    for (LuaWindow& window : LuaWindows())
    {
        if (!window.visible)
            continue;
        bool open = true;
        const bool expanded = ui::Begin(window.title.c_str(), &open, static_cast<ImGuiWindowFlags>(window.flags));
        if (expanded)
            lua->InvokeUICallback(window.handle);
        ui::End();
        if (!open)
            window.visible = false; // User closed it from the title bar.
    }
}

void RenderLuaMenuEntries(Context* context, const char* topName)
{
    auto* lua = context->GetSubsystem<EditorLuaScript>();
    if (!lua)
        return;
    const ea::vector<LuaMenuSlice> slices = CollectLuaMenuChildren(ea::string(topName));
    if (!slices.empty())
        RenderLuaMenuLevel(slices, lua);
}

void RenderLuaTopMenus(Context* context, const char* skipTopName)
{
    auto* lua = context->GetSubsystem<EditorLuaScript>();
    if (!lua)
        return;

    // Distinct top-level menu names taken from the plugin paths. ea::map keeps them ordered and
    // merges items that plugins put under the same heading.
    ea::map<ea::string, ea::vector<LuaMenuSlice>> menus;
    for (const LuaMenuItem& item : LuaMenuItems())
    {
        const auto slash = item.label.find('/');
        if (slash == ea::string::npos)
            continue;
        menus[item.label.substr(0, slash)].push_back(LuaMenuSlice{ item.label.substr(slash + 1), item.handle });
    }

    const ea::string skip = skipTopName ? ea::string(skipTopName) : ea::string();
    for (auto& pair : menus)
    {
        // Already rendered by the caller through RenderLuaMenuEntries (built-in menu reuse).
        if (!skip.empty() && pair.first == skip)
            continue;
        if (ui::BeginMenu(pair.first.c_str()))
        {
            RenderLuaMenuLevel(pair.second, lua);
            ui::EndMenu();
        }
    }
}

void ShutdownEditorLua(Context* context)
{
    context->RemoveSubsystem<EditorLuaScript>();
}

} // namespace Urho3D

#endif

//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#include "EditorLuaIntegration.h"

#ifdef URHO3D_LUA

#include "EditorLuaVMHost.h"
#include "EditorLuaBindings.h"
#include "LuaUIState.h"

#include "../Project/Project.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/Core/Object.h>
#include <Urho3D/SystemUI/SystemUI.h>

#include <EASTL/map.h>

namespace Urho3D
{

namespace
{

// A menu label expressed relative to the menu level currently being rendered, i.e. the leading
// path segments of an "a/b/Item" style label have already been consumed by parent menus.
struct LuaMenuSlice
{
    ea::string path;
    unsigned long long handle;
};

/// Render one menu level inside the currently open menu: slices without '/' become clickable
/// items, the rest are grouped by their next segment into submenus rendered recursively.
void RenderLuaMenuLevel(const ea::vector<LuaMenuSlice>& slices, LuaVMHost* lua)
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
            lua->InvokeCallback(leaf->handle);
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
    for (const Detail::LuaMenuItem& item : Detail::LuaMenuItems())
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
    // Bring up the dedicated Lua VM for editor plugins. EditorLuaVMHost is the editor's flavor
    // of the generic engine-side VM (engine bindings, print redirection, event bridge, plugin
    // pipeline -- no API tables of its own). Everything plugin-visible is registered right
    // here by the editor: the imgui table and the whole "Editor" table.
    const auto editorLua = MakeShared<EditorLuaVMHost>(context);
    context->RegisterSubsystem(editorLua);
    editorLua->Initialize();

    RegisterImGuiLuaBindings(editorLua->GetState());
    RegisterEditorLuaAPI(context);
}

void ReloadEditorLuaPlugins(Context* context)
{
    auto* project = context->GetSubsystem<Project>();
    auto* editorLua = context->GetSubsystem<EditorLuaVMHost>();
    if (!project || !editorLua)
        return;

    // Render registered Lua menu items at the end of the Project menu. The subscription uses the
    // editor Lua subsystem as its receiver so it is dropped if that outlives the project; a fresh
    // subscription is added for every newly opened project (Project is recreated on each open).
    project->OnRenderProjectMenu.Subscribe(editorLua, [context]()
    {
        auto* lua = context->GetSubsystem<EditorLuaVMHost>();
        if (!lua || Detail::LuaMenuItems().empty())
            return;

        // Only plain labels (no '/') live in the Project menu. Path labels such as "Tools/Item"
        // describe their own top-level menu and are rendered by RenderLuaMenuEntries /
        // RenderLuaTopMenus in the main menu bar instead.
        bool separatorDrawn = false;
        for (const Detail::LuaMenuItem& item : Detail::LuaMenuItems())
        {
            if (item.label.find('/') != ea::string::npos)
                continue;
            if (!separatorDrawn)
            {
                ui::Separator();
                separatorDrawn = true;
            }
            if (ui::MenuItem(item.label.c_str()))
                lua->InvokeCallback(item.handle);
        }
    });

    // Reload starts from a clean editor-side UI slate, mirroring the Lua-side callback registry
    // clear inside LoadPlugins; the plugins below re-register their tabs, menus and windows.
    Detail::ResetLuaUI();

    // Convention: per-project editor plugins live in an "EditorScripts" folder at the project root
    // (kept separate from the game's Data/Scripts so editor-only tooling never ships with a build).
    // GetProjectPath() already ends with a '/', so no separator is added here.
    editorLua->LoadPlugins(project->GetProjectPath() + "EditorScripts");
}

void RenderLuaWindows(Context* context)
{
    // Draw every Lua window whose flag is set. This runs at the top level of the editor frame
    // (like the About dialog), so the windows persist regardless of which dock tab is focused.
    if (Detail::LuaWindows().empty())
        return;
    auto* lua = context->GetSubsystem<EditorLuaVMHost>();
    if (!lua)
        return;

    for (Detail::LuaWindow& window : Detail::LuaWindows())
    {
        if (!window.visible)
            continue;
        bool open = true;
        const bool expanded = ui::Begin(window.title.c_str(), &open, static_cast<ImGuiWindowFlags>(window.flags));
        if (expanded)
            lua->InvokeCallback(window.handle);
        ui::End();
        if (!open)
            window.visible = false; // User closed it from the title bar.
    }
}

void RenderLuaMenuEntries(Context* context, const char* topName)
{
    auto* lua = context->GetSubsystem<EditorLuaVMHost>();
    if (!lua)
        return;
    const ea::vector<LuaMenuSlice> slices = CollectLuaMenuChildren(ea::string(topName));
    if (!slices.empty())
        RenderLuaMenuLevel(slices, lua);
}

void RenderLuaTopMenus(Context* context, const char* skipTopName)
{
    auto* lua = context->GetSubsystem<EditorLuaVMHost>();
    if (!lua)
        return;

    // Distinct top-level menu names taken from the plugin paths. ea::map keeps them ordered and
    // merges items that plugins put under the same heading.
    ea::map<ea::string, ea::vector<LuaMenuSlice>> menus;
    for (const Detail::LuaMenuItem& item : Detail::LuaMenuItems())
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
    context->RemoveSubsystem<EditorLuaVMHost>();
}

} // namespace Urho3D

#endif

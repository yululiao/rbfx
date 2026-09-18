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
#include "../Tabs/SceneViewTab.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/Core/Object.h>
#include <Urho3D/Core/Timer.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/SystemUI/SystemUI.h>
#include <Urho3D/Utility/SceneSelection.h>

#include <EASTL/algorithm.h>
#include <EASTL/map.h>
#include <EASTL/optional.h>

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

namespace
{

float LuaNow(Context* context)
{
    auto* time = context->GetSubsystem<Time>();
    return time ? time->GetElapsedTime() : 0.0f;
}

// Run due scheduled tasks (Editor.tick). Fired work is snapshotted out of the live list before it
// is invoked, so a callback that registers more tasks (a defer inside a defer) or cancels a
// sibling mutates the shared list, never the copy being called.
void PumpLuaScheduledTasks(Context* context, LuaVMHost* lua)
{
    auto& tasks = Detail::LuaScheduledTasks();
    if (tasks.empty())
        return;

    const float now = LuaNow(context);
    ea::vector<Detail::LuaScheduledTask> fire;
    ea::vector<Detail::LuaScheduledTask> reschedule;

    for (auto it = tasks.begin(); it != tasks.end();)
    {
        Detail::LuaScheduledTask task = *it;
        if (task.nextFrame || now >= task.fireTime)
        {
            it = tasks.erase(it);
            task.nextFrame = false;
            if (task.interval > 0.0f)
            {
                task.fireTime = now + task.interval;
                reschedule.push_back(task);
            }
            fire.push_back(task);
        }
        else
        {
            ++it;
        }
    }

    for (const Detail::LuaScheduledTask& task : reschedule)
        tasks.push_back(task);
    for (const Detail::LuaScheduledTask& task : fire)
        lua->InvokeCallback(task.handle);
}

// Fire Editor.selection.onChanged callbacks when the active selection changes. The packed selection
// is compared every frame so edits from the user, other tabs, or Lua all funnel through one path.
// A scene switch only re-baselines (no callback) so opening a project never looks like an edit.
void PollLuaSelectionChange(Context* context, LuaVMHost* lua)
{
    static Scene* lastScene = nullptr;
    static ea::optional<PackedSceneSelection> lastPack;

    SceneViewPage* page = Detail::ActiveSceneViewPage(context);
    Scene* scene = page ? page->scene_.Get() : nullptr;
    const ea::optional<PackedSceneSelection> current = page
        ? ea::optional<PackedSceneSelection>(page->selection_.Pack())
        : ea::optional<PackedSceneSelection>();

    const bool rebaselined = scene != lastScene;
    const bool changed = current && lastPack && !(current == lastPack);
    lastScene = scene;
    lastPack = current;

    if (rebaselined || !changed)
        return;

    auto callbacks = Detail::LuaSelectionCallbacks();
    for (unsigned long long handle : callbacks)
        lua->InvokeCallback(handle);
}

// Draw queued Editor.ui.notify toasts as a borderless bottom-right stack, expiring by time.
void RenderLuaToasts(Context* context)
{
    auto& toasts = Detail::LuaToasts();
    if (toasts.empty())
        return;

    const float now = LuaNow(context);
    toasts.erase(ea::remove_if(toasts.begin(), toasts.end(),
                     [now](const Detail::LuaToast& t) { return now >= t.expireTime; }),
        toasts.end());
    if (toasts.empty())
        return;

    const ImVec2 display = ui::GetIO().DisplaySize;
    ui::SetNextWindowPos(ImVec2(display.x - 16.0f, display.y - 16.0f), ImGuiCond_Always, ImVec2(1.0f, 1.0f));
    ui::SetNextWindowBgAlpha(0.95f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (ui::Begin("EditorLuaNotify", nullptr, flags))
    {
        for (const Detail::LuaToast& toast : toasts)
            ui::TextUnformatted(toast.text.c_str());
    }
    ui::End();
}

// Present the front-most Editor.ui modal; resolving it pops the queue and fires the stored one-shot
// callback. An input dialog hands its text back as an EventData with a Text field; cancelling an
// input, or pressing No on a confirm, runs the cancel path (no callback for input).
void RenderLuaModals(Context* context, LuaVMHost* lua)
{
    auto& modals = Detail::LuaModals();
    if (modals.empty())
        return;

    Detail::LuaModal& modal = modals.front();
    if (!modal.opened)
    {
        ui::OpenPopup(modal.id.c_str());
        modal.opened = true;
    }

    bool resolved = false;
    bool confirmed = false;
    if (ui::BeginPopupModal(modal.id.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        if (!modal.title.empty())
            ui::TextUnformatted(modal.title.c_str());
        if (modal.kind == Detail::LuaModal::Confirm)
        {
            if (!modal.text.empty())
                ui::TextWrapped("%s", modal.text.c_str());
            ui::Separator();
            if (ui::Button("OK"))
                confirmed = resolved = true;
            if (modal.onCancel)
            {
                ui::SameLine();
                if (ui::Button("Cancel"))
                    resolved = true;
            }
            if (ui::IsKeyPressed(KEY_ESCAPE))
                resolved = true;
        }
        else // Input
        {
            ui::InputText(modal.label.c_str(), &modal.input);
            ui::Separator();
            if (ui::Button("OK"))
                confirmed = resolved = true;
            if (ui::IsKeyPressed(KEY_ESCAPE))
                resolved = true;
        }
        ui::EndPopup();
    }

    if (!resolved)
        return;

    const bool wasInput = modal.kind == Detail::LuaModal::Input;
    const ea::string text = modal.input;
    const unsigned long long okHandle = modal.onConfirm;
    const unsigned long long cancelHandle = modal.onCancel;
    modals.erase(modals.begin());
    ui::CloseCurrentPopup();

    VariantMap data;
    if (confirmed)
    {
        if (wasInput)
            data["Text"] = text;
        if (okHandle)
            lua->InvokeOneShotCallback(okHandle, data);
        if (cancelHandle)
            lua->DropCallback(cancelHandle);
    }
    else
    {
        if (cancelHandle)
            lua->InvokeOneShotCallback(cancelHandle, data);
        if (okHandle)
            lua->DropCallback(okHandle);
    }
}

} // namespace

void RenderLuaWindows(Context* context)
{
    // Single per-frame entry point for the whole Lua plugin UI, run at the top level of the editor
    // frame (like the About dialog): drive scheduling, detect selection changes, then draw the
    // registered windows, toasts and modals. Scheduling runs first so a deferred callback acts
    // before anything it queued is rendered this frame.
    auto* lua = context->GetSubsystem<EditorLuaVMHost>();
    if (!lua)
        return;

    PumpLuaScheduledTasks(context, lua);
    PollLuaSelectionChange(context, lua);

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

    RenderLuaToasts(context);
    RenderLuaModals(context, lua);
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

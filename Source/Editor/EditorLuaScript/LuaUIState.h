//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#pragma once

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <Urho3D/Container/Ptr.h>
#include <Urho3D/Core/Variant.h>

namespace Urho3D
{

class Context;
class LuaEditorTab;
class SceneViewPage;

// Editor-side bookkeeping for the Lua plugin UI, shared between the two parties that touch it:
// EditorLuaBindings.cpp registers items into these registries (from Editor.addTab and friends),
// while EditorLuaIntegration.cpp renders them (menus, floating windows) and resets them on plugin
// reload. Internal to the editor's Lua integration.

namespace Detail
{

/// Menu item registered by a Lua plugin; clicking invokes the callback by handle.
struct LuaMenuItem
{
    ea::string label;
    unsigned long long handle;
};

/// Floating window registered by a Lua plugin. The editor draws it every frame (independent of
/// any dock tab), wrapping the content callback between its own Begin/End. 'visible' is owned by
/// the editor so the title-bar close works; Lua toggles it by title via showWindow/hideWindow.
struct LuaWindow
{
    ea::string title;
    unsigned long long handle;
    bool visible;
    unsigned int flags;
};

/// Callback registered by Editor.selection.onChanged; invoked when the active scene selection
/// changes. Persist across frames, one-shot-free: they fire on every change until a plugin reload.
/// (Handles live in the host callback registry; ResetLuaUI drops the list, the reload drops the
/// registry, so a stale handle can never be invoked.)

/// A scheduled task registered through Editor.tick.{defer,after,every}. 'nextFrame' tasks run on
/// the very next pump regardless of 'fireTime'; 'interval' > 0 marks a repeating task (every),
/// 0 a one-shot (defer/after) that is dropped after firing. 'fireTime' is an absolute stamp from
/// the Time subsystem's elapsed clock.
struct LuaScheduledTask
{
    unsigned long long handle;
    float fireTime;
    float interval;
    bool nextFrame;
};

/// A transient notification queued by Editor.ui.notify, drawn as a floating toast and expired by
/// 'expireTime' (absolute Time elapsed stamp).
struct LuaToast
{
    ea::string text;
    float expireTime;
};

/// A modal dialog requested by Editor.ui.{confirm,input}. Only the first entry is presented each
/// frame; resolving it (button / enter / escape) pops it and invokes the stored one-shot callback
/// handles. 'id' is the ImGui popup name; 'opened' guards the one-time OpenPopup call; 'input'
/// is the mutable text buffer for the Input kind.
struct LuaModal
{
    enum Kind
    {
        Confirm,
        Input,
    };

    Kind kind;
    ea::string id;
    ea::string title;
    ea::string text;
    ea::string label;
    ea::string input;
    unsigned long long onConfirm; // Confirm: yes / Input: done(text)
    unsigned long long onCancel;  // Confirm: no (may be 0)
    bool opened;
};

ea::vector<LuaMenuItem>& LuaMenuItems();
ea::vector<WeakPtr<LuaEditorTab>>& LuaTabs();
ea::vector<LuaWindow>& LuaWindows();
ea::vector<unsigned long long>& LuaSelectionCallbacks();
ea::vector<LuaScheduledTask>& LuaScheduledTasks();
ea::vector<LuaToast>& LuaToasts();
ea::vector<LuaModal>& LuaModals();

/// Handle of the single Editor.assets.onProcessed callback (0 = none). It fires on the falling
/// edge of AssetManager::IsProcessing, so 'wasProcessing' keeps the previously observed state.
/// Like the selection callbacks, the handle lives in the host registry and ResetLuaUI drops it.
unsigned long long& LuaAssetProcessedCallback();
bool& LuaWasProcessing();

/// Plugin-owned persistent key->variant store backing Editor.settings. It is loaded from disk when
/// a project's plugins (re)load and flushed by the per-frame pump whenever 'dirty' is set, so a
/// plugin looping over set() only writes the file once per frame. Unlike the transient UI state
/// above, ResetLuaUI deliberately leaves these untouched -- the (re)load path owns them.
StringVariantMap& LuaPluginSettings();
bool& LuaPluginSettingsDirty();

/// Return the scene-view page currently being edited (its scene + selection), or null.
SceneViewPage* ActiveSceneViewPage(Context* context);

/// Drop accumulated bookkeeping (menu items, windows, pruned tab references, and the P0 UI state:
/// selection callbacks, scheduled tasks, toasts and modals) at the start of a plugin (re)load,
/// mirroring the Lua side clearing its own UI-callback registry.
void ResetLuaUI();

}

}

//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#pragma once

#include "Export.h"

#include <EASTL/string.h>
#include <EASTL/functional.h>
#include <EASTL/vector.h>

namespace Urho3D
{

class Node;
class Component;
class Scene;

/// Snapshot of the build pipeline, spelled out here because the DLL may not include the editor
/// headers. Fields are empty or false before the first build of the session has run.
struct EditorBuildStatus
{
    /// Whether a build is walking its plan right now.
    bool building = false;
    /// Fraction of the plan of the current (or last) build, in [0, 1].
    float progress = 0.0f;
    /// Stage being executed, or the last one finished.
    ea::string stage;
    /// Profile of the current (or last) build.
    ea::string profile;
    /// Directory that build writes into, absolute with a trailing slash.
    ea::string outputDir;
    /// Problems found, kept after the build ends so a plugin can report them without having been
    /// subscribed when the failure happened.
    ea::vector<ea::string> errors;
};

/// Editor capabilities that the DLL-side "Editor" Lua API forwards to. All sol3/Lua code
/// lives inside RbfxLuaScript (single Lua VM, single sol3 instance); the editor cannot be
/// referenced from here (it would invert the EditorLibrary -> RbfxLuaScript dependency), so
/// editor behavior is supplied at runtime through this hook table. The std::function bodies
/// are implemented in the editor and only exchange allocator-safe engine types (ea::string,
/// primitives) across the module boundary, mirroring the Widgets provider-injection pattern.
struct EditorLuaHooks
{
    /// Absolute path of the active project's Data directory, or empty when no project.
    ea::function<ea::string()> getProjectDataPath;
    /// Root path of the active project folder, or empty when no project.
    ea::function<ea::string()> getProjectPath;
    /// Whether a project is currently open.
    ea::function<bool()> hasProject;

    // UI extension hooks. The DLL registers a Lua callback and receives an opaque handle from
    // RegisterUICallback(); it then forwards that handle here so the editor can create the
    // matching UI element. When the element must run its Lua logic (draw frame / click), the
    // editor calls EditorLuaScript::InvokeUICallback(handle). Editor types never leak into the
    // DLL, and the handle is a plain integer, keeping the module boundary allocator-safe.

    /// Create (or, for an existing title, update) a dockable tab that renders via the Lua
    /// callback identified by handle. Returns false when no project is open.
    ea::function<bool(const ea::string& title, unsigned long long handle)> addTab;
    /// Register a clickable menu item that invokes the Lua callback identified by handle.
    /// The label may encode a menu path: "Tools/Test" puts "Test" into a top-level "Tools" menu,
    /// which is reused when the editor already has one and created otherwise; deeper segments
    /// become nested submenus. A label without '/' is placed in the Project menu.
    /// Returns false when no project is open.
    ea::function<bool(const ea::string& label, unsigned long long handle)> addMenuItem;
    /// Drop accumulated editor-side UI bookkeeping (menu items, windows, pruned tab references)
    /// at the start of a plugin (re)load, mirroring the DLL clearing its own UI-callback registry.
    ea::function<void()> resetUI;

    // Persistent floating windows. Unlike tabs (which only render while their dock node is
    // visible), these are drawn every frame by the editor between its own ui::Begin/ui::End so
    // the content callback just emits widgets. The editor owns the visible flag and the title
    // bar close (X); the Lua side toggles it through showWindow()/hideWindow() by title.

    /// Register (or, for an existing title, update) a persistent floating window drawn via the
    /// Lua callback identified by handle. Starts hidden. Returns false when no project is open.
    ea::function<bool(const ea::string& title, unsigned long long handle, unsigned int flags)> addWindow;
    /// Show a previously registered window by title. Returns false when no such window.
    ea::function<bool(const ea::string& title)> showWindow;
    /// Hide a previously registered window by title. Returns false when no such window.
    ea::function<bool(const ea::string& title)> hideWindow;

    // Live editor context. Engine object pointers (Scene/Node/Component) are owned by
    // Urho3D.dll, not the editor, so exchanging raw pointers across the boundary is safe; the
    // DLL wraps them into Lua usertypes. Each returns null / leaves outputs empty when there is
    // no scene currently being edited.

    /// The scene shown by the active scene-view page, or null.
    ea::function<Scene*()> getActiveScene;
    /// The active (last focused) node of the current selection, or null.
    ea::function<Node*()> getActiveNode;
    /// Append every selected node/component to the outputs (leaving existing contents intact).
    ea::function<void(ea::vector<Node*>& outNodes, ea::vector<Component*>& outComponents)> getSelected;

    // Build pipeline. The editor owns BuildSettings/BuildSystem; the DLL only ever sees profile
    // names and the plain status struct below, so no editor type crosses the boundary and the
    // values stay allocator-safe. Completion is announced twice on purpose: through the optional
    // one-shot callback of buildProfile (per invocation) and through the engine event
    // "buildFinished" (for whoever is interested), which is what lets a plugin that only observes
    // a build stay completely unaware of who started it.

    /// Names of the build profiles of the open project, in the order the editor shows them. Empty
    /// when no project is open.
    ea::function<ea::vector<ea::string>()> getBuildProfiles;
    /// Start a build of the named profile. Returns false when the profile does not exist, when no
    /// project is open or when a build is already running; the reason is in the log either way.
    /// 'handle' identifies the Lua callback to invoke once when the build reports back, or is zero
    /// to leave the event as the only notification.
    ea::function<bool(const ea::string& profile, unsigned long long handle)> buildProfile;
    /// State of the build pipeline of the open project, idle when there is none.
    ea::function<EditorBuildStatus()> getBuildStatus;
};

/// Install the editor hooks. Called once by the editor during startup, before any plugin
/// runs. Passing an empty table disables the editor-only "Editor" API functions gracefully.
RBFXLUA_API void SetEditorLuaHooks(const EditorLuaHooks& hooks);

/// Return the currently installed editor hooks (may contain empty std::function targets).
RBFXLUA_API EditorLuaHooks& GetEditorLuaHooks();

} // namespace Urho3D

//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#pragma once

namespace Urho3D
{

class Context;

#ifdef URHO3D_LUA

/// Install the editor capability hooks, then create and initialize the DLL-side
/// EditorLuaScript subsystem (the dedicated editor Lua VM). Call once at editor startup.
void SetupEditorLua(Context* context);

/// (Re)load editor plugins from the "EditorScripts" folder at the project root. Call every time a
/// project finishes loading so per-project editor plugins are picked up. The folder deliberately
/// stays outside Data: editor tooling is a development-time asset and must not be packaged with
/// the game, and it reaches the Lua VM through its own mount scheme rather than the resource cache.
void ReloadEditorLuaPlugins(Context* context);

/// Draw the persistent floating windows registered by Lua plugins. Call once per frame from the
/// top-level editor render, outside any dock tab, so menu-opened windows stay visible.
void RenderLuaWindows(Context* context);

/// Render the Lua menu items whose path starts with "topName/" into the top-level menu the caller
/// has already opened (call inside BeginMenu/EndMenu). Lets plugins merge into an existing editor
/// menu, e.g. Editor.addMenuItem("Tools/...") appending to the editor's built-in Tools menu.
void RenderLuaMenuEntries(Context* context, const char* topName);

/// Create top-level menu bar entries for every Lua menu path whose first segment has no built-in
/// counterpart yet. Pass 'skipTopName' for a menu the caller already rendered via
/// RenderLuaMenuEntries, so it is not created twice.
void RenderLuaTopMenus(Context* context, const char* skipTopName = nullptr);

/// Tear down the editor Lua subsystem. Call before the LuaScript subsystem is removed.
void ShutdownEditorLua(Context* context);

#endif

} // namespace Urho3D

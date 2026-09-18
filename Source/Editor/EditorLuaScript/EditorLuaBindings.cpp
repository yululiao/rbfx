//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#include "EditorLuaBindings.h"

#include "EditorLuaVMHost.h"
#include "EditorLuaSettingsPage.h"
#include "LuaEditorTab.h"
#include "LuaUIState.h"

#include <LuaScript/LuaBindings.h>

#include "../Tabs/SceneViewTab.h"
#include "../Core/CommonEditorActions.h"
#include "../Core/CommonEditorActionBuilders.h"
#include "../Core/HotkeyManager.h"
#include "../Core/SettingsManager.h"
#include "../Build/BuildSettings.h"
#include "../Build/BuildSystem.h"
#include "../Project/Project.h"
#include "../Project/AssetManager.h"
#include "../Project/ProjectRequest.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/Core/Timer.h>
#include <Urho3D/Core/Variant.h>
#include <Urho3D/Input/Input.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Scene/Component.h>
#include <Urho3D/Scene/Node.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/Utility/SceneSelection.h>

#include <IconFontCppHeaders/IconsFontAwesome6.h>

#include <sol/sol.hpp>

#include <EASTL/algorithm.h>
#include <EASTL/optional.h>

#include <string>
#include <string_view>

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

// Project data root as an absolute path ending in exactly one '/' (or empty with no project).
ea::string DataRoot(const Project* project)
{
    ea::string path = project->GetDataPath();
    if (!path.empty() && path.back() != '/')
        path.push_back('/');
    return path;
}

// Normalize a project-relative sub-path to forward slashes, dropping leading slashes and any
// trailing slash unless one is requested (used to turn a 'dir' scope into a scan prefix).
ea::string NormalizeResourcePath(std::string_view in, bool trailingSlash)
{
    size_t start = 0;
    while (start < in.size() && (in[start] == '/' || in[start] == '\\'))
        ++start;
    ea::string result;
    result.reserve(static_cast<unsigned>(in.size() - start));
    for (size_t i = start; i < in.size(); ++i)
        result.push_back(in[i] == '\\' ? '/' : in[i]);
    while (!result.empty() && result.back() == '/')
        result.pop_back();
    if (trailingSlash && !result.empty())
        result.push_back('/');
    return result;
}

// Lowercased ".ext" tail of a resource name (empty when it has none).
ea::string GetResourceExtension(const ea::string& name)
{
    const auto dot = name.rfind('.');
    if (dot == ea::string::npos)
        return ea::string();
    ea::string ext = name.substr(dot);
    ext.to_lower();
    return ext;
}

// Turn a user-supplied extension ("png" or ".png") into a lowercase ".png" filter tail.
ea::string NormalizeExtension(std::string_view in)
{
    ea::string ext = NormalizeResourcePath(std::string(in.data(), in.size()), false);
    if (ext.empty())
        return ext;
    if (ext.front() != '.')
        ext = ea::string(".") + ext;
    ext.to_lower();
    return ext;
}

// Whether a resource name is an editor-managed byproduct that should never surface as an asset:
// import metadata, pipeline definitions, and per-source ".d" output folders ("foo.fbx.d/...").
bool IsSatelliteResource(const ea::string& name)
{
    static const char* const suffixes[] = { ".meta", ".assetpipeline", ".AssetPipeline.json", ".texmeta" };
    for (const char* suffix : suffixes)
        if (name.ends_with(suffix))
            return true;
    return name.find(".d/") != ea::string::npos;
}

// Ask the editor to open (or just locate, when revealOnly) a resource in its editor tab by posting
// the same request the Resource Browser uses. Returns false with no project or an empty name.
bool OpenAssetResource(Context* context, const std::string& name, bool revealOnly)
{
    auto* project = context->GetSubsystem<Project>();
    if (!project)
        return false;
    const ea::string resource = NormalizeResourcePath(name, false);
    if (resource.empty())
        return false;
    project->ProcessRequest(MakeShared<OpenResourceRequest>(context, resource, revealOnly).Get());
    return true;
}

// A redoable/undoable step whose "do" and "undo" bodies are Lua callbacks held by the plugin host
// (registered through EditorLuaVMHost::RegisterCallback). It lives here rather than as an engine
// action because its body is opaque script. After a plugin reload the stored handle is unknown to
// the rebuilt callback registry, so InvokeCallback is a safe no-op; CanUndo/CanRedo intentionally
// stay true so a stale entry never wedges the UndoManager top group (which gates undo all_of on
// every action in the newest frame).
class LuaUndoAction : public EditorAction
{
public:
    LuaUndoAction(Context* context, unsigned long long redoHandle, unsigned long long undoHandle,
        const ea::string& name)
        : context_(context)
        , redo_(redoHandle)
        , undo_(undoHandle)
        , name_(name)
    {
    }

    const ea::string& GetName() const { return name_; }
    void Redo() const override { Invoke(redo_); }
    void Undo() const override { Invoke(undo_); }

private:
    void Invoke(unsigned long long handle) const
    {
        if (auto* host = context_->GetSubsystem<EditorLuaVMHost>())
            host->InvokeCallback(handle);
    }

    Context* context_;
    unsigned long long redo_;
    unsigned long long undo_;
    ea::string name_;
};

// Route a finished action onto the editor undo stack: through the scene-view tab when an editable
// page is up (so the selection is preserved and the save frame is marked), otherwise straight to
// the UndoManager -- a pure-Lua action needs no scene.
void PushEditorAction(Context* context, const EditorActionPtr& action)
{
    auto* project = context->GetSubsystem<Project>();
    if (!project)
        return;
    auto* view = project->FindTab<SceneViewTab>();
    if (view && view->GetActivePage())
        view->PushAction(action);
    else if (auto* undoManager = project->GetUndoManager())
        undoManager->PushAction(action);
}

// Hand a built action to the innermost open Editor.undo.batch() (appended, so one undo reverts the
// whole group) or, with no batch open, push it live.
void CommitEditorAction(Context* context, const EditorActionPtr& action)
{
    auto& stack = Detail::LuaUndoBatch();
    if (!stack.empty())
    {
        if (auto* composite = static_cast<CompositeEditorAction*>(stack.back().Get()))
            composite->AddAction(action);
        return;
    }
    PushEditorAction(context, action);
}

// Shared body of Editor.undo.setComponentAttribute / setNodeAttribute: capture the old value, apply
// the new one so the world updates immediately, and record a Change*AttributesAction for undo/redo.
// Both engine actions take the same (scene, attributeName, objects, oldValues, newValues) shape.
template <class ObjectT, class ActionT>
bool SetAttributeUndoable(Context* context, ObjectT* object, const std::string& attributeName,
    const Variant& newValue)
{
    SceneViewPage* page = Detail::ActiveSceneViewPage(context);
    if (!page || !page->scene_ || !object)
        return false;

    const ea::string attribute(attributeName.c_str());
    const Variant oldValue = object->GetAttribute(attribute);
    object->SetAttribute(attribute, newValue);
    object->ApplyAttributes();

    ea::vector<ObjectT*> objects{ object };
    VariantVector oldValues{ oldValue };
    VariantVector newValues{ newValue };
    const auto action = MakeShared<ActionT>(page->scene_.Get(), ea::string(attributeName.c_str()),
        objects, oldValues, newValues);
    CommitEditorAction(context, action);
    return true;
}

// Throwaway owner every Lua hotkey is bound against. The HotkeyManager drops a binding once the
// owner's WeakPtr expires, so holding exactly one of these per plugin generation (in
// Detail::LuaHotkeyOwner) gives reload-clean unbinding: ResetLuaUI releases it, the manager prunes
// the orphaned bindings, and the next Editor.hotkey.bind creates a fresh one.
class LuaHotkeyOwner : public Object
{
    URHO3D_OBJECT(LuaHotkeyOwner, Object)
public:
    explicit LuaHotkeyOwner(Context* context)
        : Object(context)
    {
    }
};

// Resolve a plugin-supplied toolbar icon name to its FontAwesome glyph. There is no runtime
// name->glyph lookup in the engine (the icons are compile-time ICON_FA_* macros), so this curated
// table covers the common editor-facing glyphs; an unknown name yields an empty string (text-only
// button). Kept ASCII to match the editor font.
struct ToolbarIconEntry
{
    const char* name;
    const char* glyph;
};

// Curated name -> glyph table, shared by the lookup and the discovery helper below.
const ToolbarIconEntry kLuaToolbarIcons[] = {
        { "cube", ICON_FA_CUBE },
        { "cubes", ICON_FA_CUBES },
        { "grid", ICON_FA_BORDER_ALL },
        { "lightbulb", ICON_FA_LIGHTBULB },
        { "camera", ICON_FA_CAMERA },
        { "video", ICON_FA_VIDEO },
        { "play", ICON_FA_PLAY },
        { "pause", ICON_FA_PAUSE },
        { "stop", ICON_FA_STOP },
        { "refresh", ICON_FA_ROTATE_RIGHT },
        { "save", ICON_FA_FLOPPY_DISK },
        { "folder", ICON_FA_FOLDER },
        { "folder-open", ICON_FA_FOLDER_OPEN },
        { "file", ICON_FA_FILE },
        { "search", ICON_FA_MAGNIFYING_GLASS },
        { "plus", ICON_FA_PLUS },
        { "minus", ICON_FA_MINUS },
        { "trash", ICON_FA_TRASH },
        { "wrench", ICON_FA_WRENCH },
        { "cog", ICON_FA_GEAR },
        { "gear", ICON_FA_GEAR },
        { "eye", ICON_FA_EYE },
        { "eye-slash", ICON_FA_EYE_SLASH },
        { "lock", ICON_FA_LOCK },
        { "star", ICON_FA_STAR },
        { "heart", ICON_FA_HEART },
        { "flag", ICON_FA_FLAG },
        { "bolt", ICON_FA_BOLT },
        { "magnet", ICON_FA_MAGNET },
        { "code", ICON_FA_CODE },
        { "paint", ICON_FA_PAINTBRUSH },
        { "brush", ICON_FA_PAINTBRUSH },
        { "music", ICON_FA_MUSIC },
        { "globe", ICON_FA_GLOBE },
        { "map", ICON_FA_MAP },
        { "chart", ICON_FA_CHART_COLUMN },
        { "layers", ICON_FA_LAYER_GROUP },
        { "target", ICON_FA_BULLSEYE },
        { "arrow-up", ICON_FA_ARROW_UP },
        { "arrow-down", ICON_FA_ARROW_DOWN },
        { "arrow-left", ICON_FA_ARROW_LEFT },
        { "arrow-right", ICON_FA_ARROW_RIGHT },
        { "check", ICON_FA_CHECK },
        { "times", ICON_FA_XMARK },
        { "exclamation", ICON_FA_EXCLAMATION },
        { "question", ICON_FA_QUESTION },
        { "info", ICON_FA_INFO },
        { "warning", ICON_FA_TRIANGLE_EXCLAMATION },
        { "sun", ICON_FA_SUN },
        { "moon", ICON_FA_MOON },
};

// Return the FontAwesome glyph for a plugin-supplied toolbar icon name, or empty when unknown.
ea::string LookupLuaToolbarIcon(const std::string& name)
{
    for (const ToolbarIconEntry& entry : kLuaToolbarIcons)
        if (name == entry.name)
            return ea::string(entry.glyph);
    return ea::string();
}

// The names accepted by Editor.toolbar.add opts.icon, so a plugin can discover what exists.
const ea::vector<ea::string>& LuaToolbarIconNames()
{
    static const ea::vector<ea::string> names = []
    {
        ea::vector<ea::string> result;
        for (const ToolbarIconEntry& entry : kLuaToolbarIcons)
            result.push_back(ea::string(entry.name));
        return result;
    }();
    return names;
}

// Parse a "+"-separated hotkey string ("ctrl+shift+k", "alt+f5", "mouse1") into an EditorHotkey.
// Modifiers are ctrl/shift/alt (spelled several ways); the final non-modifier segment is the main
// key: a mouse button (mouse0/1/2), a named special key, or any letter/digit resolved through
// Input::GetScancodeFromName. Returns nullopt when nothing usable is found. 'command' is stored on
// the hotkey so the manager can de-duplicate invocations per frame; callers pass a unique string.
ea::optional<EditorHotkey> ParseLuaHotkey(const std::string& combo, const ea::string& command)
{
    // Split on '+', trimming surrounding spaces and lowercasing each token (ASCII input only).
    ea::vector<std::string> tokens;
    std::string current;
    for (char c : combo)
    {
        if (c == '+')
        {
            tokens.push_back(current);
            current.clear();
        }
        else
            current.push_back(c);
    }
    tokens.push_back(current);

    ea::string main;
    bool ctrl = false, shift = false, alt = false;
    for (std::string& raw : tokens)
    {
        // Trim and lowercase.
        size_t begin = raw.find_first_not_of(" \t");
        if (begin == std::string::npos)
            continue;
        size_t end = raw.find_last_not_of(" \t");
        std::string token = raw.substr(begin, end - begin + 1);
        for (char& c : token)
            c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));

        if (token == "ctrl" || token == "control")
            ctrl = true;
        else if (token == "shift")
            shift = true;
        else if (token == "alt" || token == "option")
            alt = true;
        else
            main = ea::string(token.c_str());
    }

    if (main.empty())
        return ea::nullopt;

    EditorHotkey hotkey;
    hotkey.command_ = command;
    if (ctrl)
        hotkey.Ctrl();
    if (shift)
        hotkey.Shift();
    if (alt)
        hotkey.Alt();

    // Mouse buttons.
    if (main == "mouse0")
    {
        hotkey.Press(MOUSEB_LEFT);
        return hotkey;
    }
    if (main == "mouse1")
    {
        hotkey.Press(MOUSEB_RIGHT);
        return hotkey;
    }
    if (main == "mouse2")
    {
        hotkey.Press(MOUSEB_MIDDLE);
        return hotkey;
    }

    // Named special keys SDL's scancode-name table spells differently or not at all.
    struct KeyEntry
    {
        const char* name;
        Scancode scancode;
    };
    static const KeyEntry specials[] = {
        { "escape", SCANCODE_ESCAPE }, { "esc", SCANCODE_ESCAPE },
        { "enter", SCANCODE_RETURN }, { "return", SCANCODE_RETURN },
        { "space", SCANCODE_SPACE }, { "tab", SCANCODE_TAB },
        { "backspace", SCANCODE_BACKSPACE }, { "delete", SCANCODE_DELETE },
        { "insert", SCANCODE_INSERT }, { "home", SCANCODE_HOME },
        { "end", SCANCODE_END }, { "pageup", SCANCODE_PAGEUP },
        { "pagedown", SCANCODE_PAGEDOWN }, { "up", SCANCODE_UP },
        { "down", SCANCODE_DOWN }, { "left", SCANCODE_LEFT },
        { "right", SCANCODE_RIGHT },
        { "f1", SCANCODE_F1 }, { "f2", SCANCODE_F2 }, { "f3", SCANCODE_F3 },
        { "f4", SCANCODE_F4 }, { "f5", SCANCODE_F5 }, { "f6", SCANCODE_F6 },
        { "f7", SCANCODE_F7 }, { "f8", SCANCODE_F8 }, { "f9", SCANCODE_F9 },
        { "f10", SCANCODE_F10 }, { "f11", SCANCODE_F11 }, { "f12", SCANCODE_F12 },
    };
    for (const KeyEntry& entry : specials)
    {
        if (main == entry.name)
        {
            hotkey.Press(entry.scancode);
            return hotkey;
        }
    }

    // Letters, digits and simple punctuation resolve through the engine's SDL-backed lookup.
    const Scancode scancode = Input::GetScancodeFromName(main);
    if (scancode != SCANCODE_UNKNOWN)
    {
        hotkey.Press(scancode);
        return hotkey;
    }
    return ea::nullopt;
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
    // Editor.assets -- the project asset database. Paths are project-relative resource
    // names ("Textures/foo.png"). Every call degrades to a safe empty result with no
    // project open. Raw file reads/writes are out of scope: plugins already have the
    // engine's fileSystem/file bindings for that.
    // ---------------------------------------------------------------------------
    sol::table assetsApi = editor.create_named("assets");

    // Re-run the import pipeline for one asset or, when given a directory (trailing slash),
    // everything under it. Reprocessing happens on the AssetManager's next update, so watch
    // status()/onProcessed for completion.
    assetsApi.set_function("reimport", [context](const std::string& path) -> bool {
        auto* project = context->GetSubsystem<Project>();
        auto* assets = project ? project->GetAssetManager() : nullptr;
        if (!assets)
            return false;
        assets->MarkCacheDirty(ea::string(path.c_str()));
        return true;
    });

    // Import activity snapshot: { processing:boolean, processed:integer, total:integer }.
    assetsApi.set_function("status", [context](sol::this_state s) -> sol::object {
        sol::state_view lua(s);
        sol::table result = lua.create_table();
        auto* project = context->GetSubsystem<Project>();
        auto* assets = project ? project->GetAssetManager() : nullptr;
        const AssetManager::ProgressInfo progress =
            assets ? assets->GetProgress() : AssetManager::ProgressInfo{ 0u, 0u };
        result["processing"] = assets && assets->IsProcessing();
        result["processed"] = static_cast<int>(progress.first);
        result["total"] = static_cast<int>(progress.second);
        return result;
    });

    // One persistent callback fired whenever an import run finishes (processing -> idle). Pass
    // nil to clear; replaced on re-registration and dropped on plugin reload. It takes no
    // argument -- call status()/list() from the handler to inspect what changed.
    assetsApi.set_function("onProcessed",
        [host = editorLua](sol::optional<sol::protected_function> callback) -> bool {
            auto& stored = Detail::LuaAssetProcessedCallback();
            if (stored)
            {
                host->DropCallback(stored);
                stored = 0ull;
            }
            if (callback && callback->valid())
                stored = host->RegisterCallback(std::move(*callback));
            return true;
        });

    // List project assets. opts = { dir?, type?, extension? }.
    //  * default is cheap: names/paths/extension only, no per-file type sniffing.
    //  * giving 'type' resolves each candidate through Project::GetResourceDescriptor and
    //    filters to that resource type (base types match too) -- slower on big projects, so
    //    pair it with 'dir'/'extension' to narrow the scan.
    // Each entry: { name, path, extension, isDirectory } plus, when types were resolved,
    // { type = most-derived, types = { all matched type names } }.
    assetsApi.set_function("list",
        [context](sol::this_state s, sol::optional<sol::table> opts) -> sol::object {
            sol::state_view lua(s);
            sol::table result = lua.create_table();
            auto* project = context->GetSubsystem<Project>();
            auto* fs = context->GetSubsystem<FileSystem>();
            if (!project || !fs)
                return result;

            ea::string dirPrefix;
            ea::string typeFilter;
            ea::string extFilter;
            if (opts)
            {
                sol::table o = *opts;
                if (auto dir = o["dir"].get<sol::optional<std::string>>(); dir && !dir->empty())
                    dirPrefix = NormalizeResourcePath(*dir, true);
                if (auto type = o["type"].get<sol::optional<std::string>>(); type && !type->empty())
                    typeFilter = ea::string(type->c_str());
                if (auto ext = o["extension"].get<sol::optional<std::string>>(); ext && !ext->empty())
                    extFilter = NormalizeExtension(*ext);
            }

            const ea::string root = DataRoot(project);
            const ea::string pattern = extFilter.empty() ? ea::string("*") : ("*" + extFilter);
            ea::vector<ea::string> files;
            fs->ScanDir(files, root + dirPrefix, pattern, SCAN_FILES | SCAN_RECURSIVE);

            const bool resolveTypes = !typeFilter.empty();
            int index = 0;
            for (const ea::string& relative : files)
            {
                const ea::string resourceName = dirPrefix + relative;
                if (IsSatelliteResource(resourceName) || project->IsFileNameIgnored(resourceName))
                    continue;

                ResourceFileDescriptor desc;
                if (resolveTypes)
                {
                    desc = project->GetResourceDescriptor(resourceName);
                    if (desc.isAutomatic_ || !desc.HasObjectType(typeFilter))
                        continue;
                }

                sol::table entry = lua.create_table();
                entry["name"] = std::string(resourceName.c_str());
                entry["path"] = std::string((root + resourceName).c_str());
                entry["extension"] = std::string(GetResourceExtension(resourceName).c_str());
                entry["isDirectory"] = false;
                if (resolveTypes)
                {
                    entry["type"] = std::string(desc.mostDerivedType_.c_str());
                    sol::table types = lua.create_table();
                    int typeIndex = 0;
                    for (const ea::string& typeName : desc.typeNames_)
                        types[++typeIndex] = std::string(typeName.c_str());
                    entry["types"] = types;
                }
                result[++index] = entry;
            }
            return result;
        });

    // Metadata for one resource (types always resolved), or nil when it does not exist.
    assetsApi.set_function("info", [context](sol::this_state s, const std::string& name) -> sol::object {
        sol::state_view lua(s);
        auto* project = context->GetSubsystem<Project>();
        auto* fs = context->GetSubsystem<FileSystem>();
        if (!project || !fs)
            return sol::make_object(lua, sol::nil);
        const ea::string root = DataRoot(project);
        const ea::string resource = NormalizeResourcePath(name, false);
        if (resource.empty() || !fs->Exists(root + resource))
            return sol::make_object(lua, sol::nil);

        const ResourceFileDescriptor desc = project->GetResourceDescriptor(resource);
        sol::table entry = lua.create_table();
        entry["name"] = std::string(resource.c_str());
        entry["path"] = std::string((root + resource).c_str());
        entry["extension"] = std::string(GetResourceExtension(resource).c_str());
        entry["isDirectory"] = desc.isDirectory_;
        entry["type"] = std::string(desc.mostDerivedType_.c_str());
        sol::table types = lua.create_table();
        int typeIndex = 0;
        for (const ea::string& typeName : desc.typeNames_)
            types[++typeIndex] = std::string(typeName.c_str());
        entry["types"] = types;
        return entry;
    });

    // Cheap existence check (a file or directory under Data), no type resolution.
    assetsApi.set_function("exists", [context](const std::string& name) -> bool {
        auto* project = context->GetSubsystem<Project>();
        auto* fs = context->GetSubsystem<FileSystem>();
        if (!project || !fs)
            return false;
        const ea::string resource = NormalizeResourcePath(name, false);
        return !resource.empty() && fs->Exists(DataRoot(project) + resource);
    });

    // Open the resource in its editor tab / just highlight it in the browser without stealing the
    // Inspector. Both return false with no project or an empty name.
    assetsApi.set_function("open", [context](const std::string& name) -> bool {
        return OpenAssetResource(context, name, false);
    });
    assetsApi.set_function("reveal", [context](const std::string& name) -> bool {
        return OpenAssetResource(context, name, true);
    });

    // ---------------------------------------------------------------------------
    // Editor.settings -- a namespaced, persistent key->variant store a plugin owns, backed by a
    // single JSON file under <Project>/EditorScripts/plugin-settings.json (loaded before plugins
    // run, flushed at most once per frame). Keys are dotted ASCII strings; values round-trip the
    // scalars LuaToVariant understands (bool / int / float / string, and homogeneous scalar arrays).
    // Everything degrades safely with no project: get->default, set->false, keys->empty, path->"".
    // ---------------------------------------------------------------------------
    sol::table settingsApi = editor.create_named("settings");

    // Read a key, or 'default' (nil when omitted) when it is absent.
    settingsApi.set_function("get", [](sol::this_state s, const std::string& key,
                                sol::optional<sol::object> fallback) -> sol::object {
        sol::state_view lua(s);
        auto it = Detail::LuaPluginSettings().find(ea::string(key.c_str()));
        if (it != Detail::LuaPluginSettings().end())
            return VariantToLua(lua, it->second);
        if (fallback && fallback->valid() && *fallback != sol::lua_nil)
            return *fallback;
        return sol::make_object(lua, sol::nil);
    });

    // Store a key. Returns false (and leaves the store untouched) when the value is not
    // convertible -- e.g. nil, a function or arbitrary userdata.
    settingsApi.set_function("set", [](sol::this_state s, const std::string& key, sol::object value) -> bool {
        const Variant variant = LuaToVariant(sol::state_view(s), value);
        if (variant.IsEmpty())
            return false;
        Detail::LuaPluginSettings()[ea::string(key.c_str())] = variant;
        Detail::LuaPluginSettingsDirty() = true;
        return true;
    });

    settingsApi.set_function("has", [](const std::string& key) -> bool {
        return Detail::LuaPluginSettings().find(ea::string(key.c_str())) != Detail::LuaPluginSettings().end();
    });

    // Remove a key; true when one was actually erased (which marks the store dirty).
    settingsApi.set_function("erase", [](const std::string& key) -> bool {
        auto& store = Detail::LuaPluginSettings();
        if (store.erase(ea::string(key.c_str())) == 0)
            return false;
        Detail::LuaPluginSettingsDirty() = true;
        return true;
    });

    // All keys, optionally limited to a prefix, sorted lexicographically for stable output.
    settingsApi.set_function("keys",
        [](sol::this_state s, sol::optional<std::string> prefix) -> sol::object {
            sol::state_view lua(s);
            sol::table result = lua.create_table();
            const ea::string filter = prefix ? ea::string(prefix->c_str()) : ea::string();
            ea::vector<ea::string> names;
            for (const auto& pair : Detail::LuaPluginSettings())
                if (filter.empty() || pair.first.starts_with(filter))
                    names.push_back(pair.first);
            ea::sort(names.begin(), names.end());
            int index = 0;
            for (const ea::string& name : names)
                result[++index] = std::string(name.c_str());
            return result;
        });

    // Absolute path of the backing JSON file, or "" with no project (for transparency / debugging).
    settingsApi.set_function("path", [context]() -> std::string {
        auto* project = context->GetSubsystem<Project>();
        if (!project)
            return std::string();
        const ea::string path = project->GetProjectPath() + "EditorScripts/plugin-settings.json";
        return std::string(path.c_str());
    });

    // Register a page in the editor's Settings window whose body the plugin draws itself. The page
    // nests under Editor > Lua > <title>; drawFn is called each frame the page is open, in the
    // settings tab's ImGui context, so it can use the imgui table and persist values through
    // Editor.settings.get/set. The page object is owned by the SettingsManager and outlives reloads,
    // so the first registration adds it and every later one (after a reload) only re-points the
    // draw callback -- registering the same title twice never stacks duplicate pages.
    settingsApi.set_function("registerPage", [context, host = editorLua](const std::string& title,
                                                sol::protected_function drawFn) -> bool {
        if (title.empty() || !drawFn.valid())
            return false;
        auto* project = context->GetSubsystem<Project>();
        auto* settingsManager = project ? project->GetSettingsManager() : nullptr;
        if (!settingsManager)
            return false;

        const unsigned long long handle = host->RegisterCallback(std::move(drawFn));
        const ea::string name = ea::string("Editor.Lua:") + ea::string(title.c_str());
        if (auto* page = dynamic_cast<LuaSettingsPage*>(settingsManager->FindPage(name)))
        {
            page->SetHandle(handle);
            return true;
        }
        settingsManager->AddPage(MakeShared<LuaSettingsPage>(context, name, handle));
        return true;
    });

    // ---------------------------------------------------------------------------
    // Editor.undo -- funnel plugin edits onto the editor's shared undo stack. Two kinds of step:
    // native scene edits (attributes / create / remove) reuse the engine's own actions so the
    // inspector refreshes and the selection is preserved; arbitrary Lua-state changes go through
    // perform(), which stores a do/undo closure pair. batch() groups any of them into one step.
    // All degrade safely: with no project/scene nothing is recorded, and after a plugin reload a
    // stale closure resolves to a no-op (the callback handle is gone) instead of crashing.
    // ---------------------------------------------------------------------------
    sol::table undoApi = editor.create_named("undo");

    // Run doFn now and record an undoable step that re-runs doFn on redo and undoFn on undo. Works
    // without a scene (the state is whatever the closures touch). Returns false if either is missing.
    undoApi.set_function("perform", [context, host = editorLua](const std::string& label,
                                       sol::protected_function doFn,
                                       sol::protected_function undoFn) -> bool {
        if (!doFn.valid() || !undoFn.valid())
            return false;
        const unsigned long long redo = host->RegisterCallback(std::move(doFn));
        const unsigned long long undo = host->RegisterCallback(std::move(undoFn));
        const auto action = MakeShared<LuaUndoAction>(context, redo, undo, ea::string(label.c_str()));
        host->InvokeCallback(redo); // execute the "do" immediately, like Godot's commit_action
        CommitEditorAction(context, action);
        return true;
    });

    // Group every undoable operation the body performs into a single undo step. Nested batches fold
    // into the outermost one. The body runs even if it records nothing (a plain no-op step results).
    undoApi.set_function("batch", [context](const std::string& /*label*/,
                             sol::protected_function body) -> bool {
        if (!body.valid())
            return false;
        const auto composite = MakeShared<CompositeEditorAction>();
        Detail::LuaUndoBatch().push_back(composite);
        const sol::protected_function_result result = body();
        Detail::LuaUndoBatch().pop_back();
        if (!result.valid())
        {
            const sol::error error = result;
            URHO3D_LOGERROR("[EditorLua] Editor.undo.batch body error: {}", error.what());
        }
        auto& stack = Detail::LuaUndoBatch();
        if (!stack.empty())
        {
            // Nested: hand the finished group to the enclosing batch as one child.
            if (auto* parent = static_cast<CompositeEditorAction*>(stack.back().Get()))
                parent->AddAction(composite);
        }
        else
        {
            PushEditorAction(context, composite);
        }
        return true;
    });

    undoApi.set_function("setComponentAttribute",
        [context, host = editorLua](sol::object target, const std::string& attributeName,
            sol::object value) -> bool {
            auto* component = dynamic_cast<Component*>(ToEngineObject(target));
            const Variant variant = LuaToVariant(sol::state_view(host->GetState()), value);
            if (!component || variant.IsEmpty())
                return false;
            return SetAttributeUndoable<Component, ChangeComponentAttributesAction>(
                context, component, attributeName, variant);
        });

    undoApi.set_function("setNodeAttribute",
        [context, host = editorLua](sol::object target, const std::string& attributeName,
            sol::object value) -> bool {
            auto* node = dynamic_cast<Node*>(ToEngineObject(target));
            const Variant variant = LuaToVariant(sol::state_view(host->GetState()), value);
            if (!node || variant.IsEmpty())
                return false;
            return SetAttributeUndoable<Node, ChangeNodeAttributesAction>(
                context, node, attributeName, variant);
        });

    // Create a node under 'parent' (defaults to the scene root), optionally named; selects it and
    // returns the new node, or nil with no scene.
    undoApi.set_function("createNode",
        [context, host = editorLua](sol::optional<sol::object> parentObj,
            sol::optional<std::string> name) -> sol::object {
            sol::state_view lua(host->GetState());
            SceneViewPage* page = Detail::ActiveSceneViewPage(context);
            if (!page || !page->scene_)
                return sol::make_object(lua, sol::nil);
            Node* parent = parentObj && parentObj->valid()
                ? dynamic_cast<Node*>(ToEngineObject(*parentObj))
                : nullptr;
            if (!parent)
                parent = page->scene_.Get();

            const CreateNodeActionBuilder builder{ page->scene_.Get(), AttributeScopeHint::Attribute };
            Node* node = parent->CreateChild();
            if (name && !name->empty())
                node->SetName(ea::string(name->c_str()));
            page->selection_.Clear();
            page->selection_.SetSelected(node, true);
            CommitEditorAction(context, builder.Build(node));
            return WrapLuaObject(lua, node);
        });

    undoApi.set_function("removeNode", [context](sol::object target) -> bool {
        SceneViewPage* page = Detail::ActiveSceneViewPage(context);
        if (!page || !page->scene_)
            return false;
        auto* node = dynamic_cast<Node*>(ToEngineObject(target));
        if (!node || !node->GetParent())
            return false;
        const RemoveNodeActionBuilder builder(node);
        node->Remove();
        CommitEditorAction(context, builder.Build());
        return true;
    });

    // Add a component of the named type to a node; selects it and returns it, or nil on failure.
    undoApi.set_function("addComponent",
        [context, host = editorLua](sol::object target, const std::string& componentType) -> sol::object {
            sol::state_view lua(host->GetState());
            SceneViewPage* page = Detail::ActiveSceneViewPage(context);
            auto* node = dynamic_cast<Node*>(ToEngineObject(target));
            if (!page || !page->scene_ || !node)
                return sol::make_object(lua, sol::nil);
            const StringHash type(componentType.c_str());
            const CreateComponentActionBuilder builder(node, type);
            Component* component = node->CreateComponent(type);
            if (!component)
                return sol::make_object(lua, sol::nil);
            page->selection_.Clear();
            page->selection_.SetSelected(component, true);
            CommitEditorAction(context, builder.Build(component));
            return WrapLuaObject(lua, component);
        });

    undoApi.set_function("removeComponent", [context](sol::object target) -> bool {
        SceneViewPage* page = Detail::ActiveSceneViewPage(context);
        if (!page || !page->scene_)
            return false;
        auto* component = dynamic_cast<Component*>(ToEngineObject(target));
        if (!component)
            return false;
        const RemoveComponentActionBuilder builder(component);
        component->Remove();
        CommitEditorAction(context, builder.Build());
        return true;
    });

    // Stack queries -- forwarded to the editor's own UndoManager, so these drive the very same
    // history Ctrl+Z / Ctrl+Y use (including native actions pushed by other tabs).
    undoApi.set_function("canUndo", [context]() -> bool {
        auto* project = context->GetSubsystem<Project>();
        auto* undoManager = project ? project->GetUndoManager() : nullptr;
        return undoManager && undoManager->CanUndo();
    });
    undoApi.set_function("canRedo", [context]() -> bool {
        auto* project = context->GetSubsystem<Project>();
        auto* undoManager = project ? project->GetUndoManager() : nullptr;
        return undoManager && undoManager->CanRedo();
    });
    undoApi.set_function("undo", [context]() -> bool {
        auto* project = context->GetSubsystem<Project>();
        auto* undoManager = project ? project->GetUndoManager() : nullptr;
        return undoManager && undoManager->Undo();
    });
    undoApi.set_function("redo", [context]() -> bool {
        auto* project = context->GetSubsystem<Project>();
        auto* undoManager = project ? project->GetUndoManager() : nullptr;
        return undoManager && undoManager->Redo();
    });

    // ---------------------------------------------------------------------------
    // Editor.toolbar -- add buttons to the project toolbar (the row next to Save). Each button
    // shows its resolved icon (or its label when there is no icon) and invokes the callback on
    // click. Buttons are drawn by the toolbar render subscription in EditorLuaIntegration and
    // cleared on plugin reload, so re-registering on each load never duplicates them. opts =
    // { icon?, tooltip? } where 'icon' is a name from Editor.toolbar.iconNames().
    // ---------------------------------------------------------------------------
    sol::table toolbarApi = editor.create_named("toolbar");
    toolbarApi.set_function("add", [host = editorLua](const std::string& label,
                             sol::protected_function callback, sol::optional<sol::table> opts) -> bool {
        if (!callback.valid() || label.empty())
            return false;
        Detail::LuaToolbarButton button;
        button.label = ea::string(label.c_str());
        if (opts)
        {
            sol::table o = *opts;
            if (auto icon = o["icon"].get<sol::optional<std::string>>(); icon && !icon->empty())
                button.glyph = LookupLuaToolbarIcon(*icon);
            if (auto tip = o["tooltip"].get<sol::optional<std::string>>(); tip && !tip->empty())
                button.tooltip = ea::string(tip->c_str());
        }
        button.handle = host->RegisterCallback(std::move(callback));
        Detail::LuaToolbarButtons().push_back(button);
        return true;
    });
    // The icon names Editor.toolbar.add accepts, so a plugin can list what is available.
    toolbarApi.set_function("iconNames", [](sol::this_state s) -> sol::object {
        sol::state_view lua(s);
        sol::table result = lua.create_table();
        const ea::vector<ea::string>& names = LuaToolbarIconNames();
        for (size_t i = 0; i < names.size(); ++i)
            result[static_cast<int>(i) + 1] = std::string(names[i].c_str());
        return result;
    });

    // ---------------------------------------------------------------------------
    // Editor.hotkey -- bind a Lua callback to a key/mouse combination using the editor's own
    // HotkeyManager, so it coexists with the built-in shortcuts. All Lua hotkeys share one throwaway
    // owner object; releasing it on reload (ResetLuaUI) lets the manager prune the bindings once the
    // owner's weak reference expires, and the stored callback handle is invalid anyway after a reload.
    // combo is a "+"-separated string such as "ctrl+shift+k", "alt+f5" or "mouse1".
    // ---------------------------------------------------------------------------
    sol::table hotkeyApi = editor.create_named("hotkey");
    hotkeyApi.set_function("bind", [context, host = editorLua](const std::string& combo,
                                     sol::protected_function callback) -> bool {
        if (!callback.valid())
            return false;
        auto* project = context->GetSubsystem<Project>();
        auto* hotkeyManager = project ? project->GetHotkeyManager() : nullptr;
        if (!hotkeyManager)
            return false;

        const unsigned long long handle = host->RegisterCallback(std::move(callback));
        const ea::string command = ea::string(("Lua." + std::to_string(handle)).c_str());
        const ea::optional<EditorHotkey> hotkey = ParseLuaHotkey(combo, command);
        if (!hotkey)
        {
            host->DropCallback(handle);
            URHO3D_LOGWARNING("[EditorLua] Editor.hotkey.bind: could not parse combo '{}'", combo);
            return false;
        }

        if (!Detail::LuaHotkeyOwner())
            Detail::LuaHotkeyOwner() = MakeShared<LuaHotkeyOwner>(context);
        hotkeyManager->BindHotkey(Detail::LuaHotkeyOwner().Get(), *hotkey,
            [host, handle]() { host->InvokeCallback(handle); });
        Detail::LuaHotkeyBindings().push_back(handle);
        return true;
    });
    // Echo a combo back in the editor's canonical display form ("Ctrl+Shift+K"), or "" when it does
    // not parse -- handy for showing a bound shortcut in a menu or tooltip.
    hotkeyApi.set_function("comboLabel", [](const std::string& combo) -> std::string {
        const ea::optional<EditorHotkey> hotkey = ParseLuaHotkey(combo, ea::string("Lua.label"));
        if (!hotkey)
            return std::string();
        return std::string(hotkey->ToString().c_str());
    });
}

} // namespace Urho3D

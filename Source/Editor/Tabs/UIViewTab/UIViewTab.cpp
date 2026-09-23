//
// Copyright (c) 2017-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#include "UIViewTab.h"

#include "../../Core/IniHelpers.h"
#include "../../Core/WidgetHelpers.h"
#include "../../Project/Project.h"
#include "../HierarchyBrowserTab.h"
#include "../InspectorTab.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/File.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/SystemUI/Widgets.h>

#include <IconFontCppHeaders/IconsFontAwesome6.h>
#include <nfd.h>

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <math.h>
#include <algorithm>
#include <cctype>
#include <utility>

#include <EASTL/sort.h>

namespace Urho3D
{

namespace
{
// On-screen radius (in pixels) for grabbing a gizmo handle.
constexpr float kHandleGrabPx = 7.0f;

// How many UIViewTab instances have ever been created. Tab identity (ImGui
// window id, ini section, Project tab registry) is keyed on the title, so
// every instance takes a unique one: the plugin-bootstrapped "UI" tab and
// then "UI (2)", "UI (3)", ... for editor tabs spawned per document.

// Normalize native path separators to '/' (resource names are '/'-delimited).
void NormalizePath(ea::string& s)
{
    for (char& c : s)
    {
        if (c == '\\')
            c = '/';
    }
}

// ASCII-lowercased copy, for case-insensitive path prefix / suffix tests on Windows.
ea::string LowerCopy(const ea::string& s)
{
    ea::string r = s;
    for (char& c : r)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}
// Half on-screen size of a drawn gizmo handle square.
constexpr float kHandleDrawPx = 4.0f;

// Overlay colors (RGBA ImU32).
constexpr ImU32 kHoverColor = IM_COL32(90, 170, 255, 200);
constexpr ImU32 kSelectColor = IM_COL32(80, 200, 130, 255);
constexpr ImU32 kSelectFill = IM_COL32(80, 200, 130, 28);
constexpr ImU32 kGizmoHandle = IM_COL32(255, 255, 255, 235);
constexpr ImU32 kGizmoHandleBorder = IM_COL32(30, 30, 30, 255);
constexpr ImU32 kGizmoLine = IM_COL32(255, 255, 255, 160);
constexpr ImU32 kGizmoText = IM_COL32(255, 255, 255, 255);
constexpr ImU32 kMoveColor = IM_COL32(255, 220, 90, 255);
constexpr ImU32 kRotateColor = IM_COL32(255, 150, 60, 255);
constexpr ImU32 kScaleColor = IM_COL32(90, 200, 255, 255);

Vector2 V2(const Rml::Vector2f& v) { return Vector2{v.x, v.y}; }
Vector2 V2(const ImVec2& v) { return Vector2{v.x, v.y}; }
ImVec2 IV2(const Vector2& v) { return ImVec2{v.x_, v.y_}; }

// The widget palette is a data table so the toolbar renders and dispatches
// without a chain of per-control branches. Entries also carry the widget
// recipe the document layer turns into nodes. Styling policy is
// honest-minimum: looks come from the project stylesheet; the editor only
// authors what keeps the element usable before the project has rules for it.
struct PaletteEntry
{
    const char* label_;
    const char* group_;
    const char* tag_;
    const char* attrName_;      // single default attribute (type, src, ...), null = none
    const char* attrValue_;
    const char* childText_;     // default text child ("Button"), null = none
    const char* childElemTag_;  // repeated child element (<option>), null = none
    const char* childElemText_;
    unsigned childElemCount_;
    UiWidgetStylePolicy stylePolicy_;  // placeholder styling (see UiWidgetStylePolicy)
    bool materialize_;          // born with a centered position box
    float sizeX_;
    float sizeY_;
};
// Styling honesty: looks live in the project stylesheet, but the document may
// not link one with rules for these tags - a native control without rules is
// fully invisible. Containers, img and unknown tags get the placeholder panel
// (they are nothing without rules); native form controls get an outline-only
// border (shape without a fill, so project-styled internal chrome - the
// select's arrow, the progress's fill - is never covered); label and text
// render their own content and get nothing. Text has no element of its own in
// RmlUi (it lives inside a block), so the Text entry mints a <p> with a text
// child: p is the semantic name for a paragraph and, like div, a pure
// stylesheet key - the default rml.rcss gives it the same display: block, so
// the look and flow are identical while the tag keeps its meaning. No fake
// styling, no pinned position - it joins the flow.
// Buttons: the engine has no registered button control, and does not need one -
// the shipped 107_HelloRmlUI sample spells all three button forms as "element +
// text child", with behaviour coming from RCSS (:hover/:active, focus/nav via
// CoreData/UI/layout.rcss, which also styles button[disabled]) and from either
// onclick="event:|sound:" (Factory event listener instancer) or
// data-event-click (data-model binding). <input type="submit"> never draws its
// value attribute (InputTypeButton has no widget and no render path, unlike
// InputTypeText which renders through its widget), but nothing strips child
// nodes, so its label has to be a text child like everywhere else - hence the
// child text on the submit row below.
const PaletteEntry kPalette[] = {
    {ICON_FA_SQUARE "  div", "Structure", "div", nullptr, nullptr, nullptr, nullptr, nullptr, 0, UiWidgetStylePolicy::Panel, true, 160.0f, 48.0f},
    {ICON_FA_TABLE_LIST "  form", "Structure", "form", nullptr, nullptr, nullptr, nullptr, nullptr, 0, UiWidgetStylePolicy::Panel, true, 240.0f, 96.0f},
    {ICON_FA_IMAGE "  img", "Content", "img", "src", "", nullptr, nullptr, nullptr, 0, UiWidgetStylePolicy::Panel, true, 160.0f, 48.0f},
    {ICON_FA_FONT "  Text (p)", "Content", "p", nullptr, nullptr, "Text", nullptr, nullptr, 0, UiWidgetStylePolicy::None, false, 0.0f, 0.0f},
    {ICON_FA_TOGGLE_ON "  button", "Controls", "button", nullptr, nullptr, "Button", nullptr, nullptr, 0, UiWidgetStylePolicy::Outline, true, 160.0f, 48.0f},
    {ICON_FA_KEYBOARD "  input (text)", "Controls", "input", "type", "text", nullptr, nullptr, nullptr, 0, UiWidgetStylePolicy::Outline, true, 160.0f, 28.0f},
    {ICON_FA_SQUARE_CHECK "  input (checkbox)", "Controls", "input", "type", "checkbox", nullptr, nullptr, nullptr, 0, UiWidgetStylePolicy::Outline, true, 20.0f, 20.0f},
    {ICON_FA_CIRCLE_DOT "  input (radio)", "Controls", "input", "type", "radio", nullptr, nullptr, nullptr, 0, UiWidgetStylePolicy::Outline, true, 20.0f, 20.0f},
    {ICON_FA_SLIDERS "  input (range)", "Controls", "input", "type", "range", nullptr, nullptr, nullptr, 0, UiWidgetStylePolicy::Outline, true, 160.0f, 20.0f},
    {ICON_FA_PAPER_PLANE "  input (submit)", "Controls", "input", "type", "submit", "Submit", nullptr, nullptr, 0, UiWidgetStylePolicy::Outline, true, 120.0f, 36.0f},
    {ICON_FA_LIST "  select", "Controls", "select", nullptr, nullptr, nullptr, "option", "Option", 2, UiWidgetStylePolicy::Outline, true, 160.0f, 32.0f},
    {ICON_FA_ALIGN_LEFT "  textarea", "Controls", "textarea", nullptr, nullptr, nullptr, nullptr, nullptr, 0, UiWidgetStylePolicy::Outline, true, 240.0f, 96.0f},
    {ICON_FA_BARS_PROGRESS "  progress", "Controls", "progress", "value", "0.5", nullptr, nullptr, nullptr, 0, UiWidgetStylePolicy::Outline, true, 160.0f, 16.0f},
    {ICON_FA_TAGS "  label", "Controls", "label", nullptr, nullptr, "Label", nullptr, nullptr, 0, UiWidgetStylePolicy::None, false, 0.0f, 0.0f},
};

// PaletteEntry stays POD (const char* fields) so the table needs no static
// initializers; this converts one entry into the spec the document consumes.
UiWidgetSpec MakeWidgetSpec(const PaletteEntry& entry)
{
    UiWidgetSpec spec;
    spec.tag_ = entry.tag_;
    if (entry.attrName_)
    {
        spec.attrName_ = entry.attrName_;
        spec.attrValue_ = entry.attrValue_ ? entry.attrValue_ : "";
    }
    if (entry.childText_)
        spec.childText_ = entry.childText_;
    if (entry.childElemTag_)
    {
        spec.childElemTag_ = entry.childElemTag_;
        spec.childElemText_ = entry.childElemText_ ? entry.childElemText_ : "";
        spec.childElemCount_ = entry.childElemCount_;
    }
    spec.stylePolicy_ = entry.stylePolicy_;
    spec.materialize_ = entry.materialize_;
    spec.size_ = Vector2{entry.sizeX_, entry.sizeY_};
    return spec;
}

// Row-scoped context menu popup (shared by every hierarchy row; the request
// is recorded by RenderNode and opened at the end of RenderContent, see the
// comments there).
const char* const kUiNodePopupId = "##uiNodeCtx";
// Drag-drop payload type carrying the source row's child-index path.
const char* const kUiNodeDragType = "UIEDITOR_NODE_PATH";

// Resolve an RmlUi href against the document's resource path, mirroring how
// RmlUi's SystemInterface::JoinPath resolves document-relative references
// (the same rule RmlFile uses when loading <link>/<template> targets).
ea::string ResolveRelativeResourcePath(const ea::string& basePath, const ea::string& href)
{
    ea::string clean = Trim(href);
    const size_t extra = clean.find_first_of("?#"); // Rml::URL strips query/anchor
    if (extra != ea::string::npos)
        clean.resize(extra);
    if (clean.empty())
        return ea::string();
    if (clean.front() == '/') // absolute in RmlUi terms
        return clean.substr(1);

    ea::vector<ea::string> segments;
    const size_t slash = basePath.find_last_of('/');
    const ea::string dir = slash == ea::string::npos ? ea::string() : basePath.substr(0, slash);
    size_t begin = 0;
    while (begin < dir.length())
    {
        const size_t end = dir.find('/', begin);
        const ea::string seg = dir.substr(begin, (end == ea::string::npos ? dir.length() : end) - begin);
        if (!seg.empty())
            segments.push_back(seg);
        if (end == ea::string::npos)
            break;
        begin = end + 1;
    }

    begin = 0;
    while (begin <= clean.length())
    {
        const size_t end = clean.find('/', begin);
        const ea::string seg = clean.substr(begin, (end == ea::string::npos ? clean.length() : end) - begin);
        if (seg == ".." )
        {
            if (!segments.empty())
                segments.pop_back();
        }
        else if (!seg.empty() && seg != ".")
            segments.push_back(seg);
        if (end == ea::string::npos)
            break;
        begin = end + 1;
    }

    ea::string out;
    for (const ea::string& seg : segments)
    {
        if (!out.empty())
            out += '/';
        out += seg;
    }
    return out;
}

// Whether a resource name can be read right now (registered in the cache, or
// present under the project's Data folder). Cache lookup is in-memory; the
// filesystem probe is one stat per open template link per frame.
bool ResourceExists(Context* context, const ea::string& resourceName)
{
    auto* cache = context->GetSubsystem<ResourceCache>();
    if (cache && !cache->GetResourceFileName(resourceName).empty())
        return true;
    auto* project = context->GetSubsystem<Project>();
    auto* fs = context->GetSubsystem<FileSystem>();
    return project && fs && fs->FileExists(project->GetDataPath() + resourceName);
}

// Pick a file under the project's Data folder and hand it back as a '/'-rooted
// resource path ("/Textures/foo.png") - the one spelling that resolves the same
// however far the editing document sits from the target (ResolveRelativeResource
// Path treats a leading '/' as Data-rooted, exactly like RmlUi). Cancelling, or
// choosing something outside Data, yields nullopt: RmlUi can only load files
// that live under Data, so writing an outside path would plant a reference that
// silently never resolves. 'current' seeds the starting folder from the field's
// present value so fixing a reference opens where it already points.
ea::optional<ea::string> PickDataResource(Context* context, const ea::string& current,
    const char* filter)
{
    auto* project = context->GetSubsystem<Project>();
    auto* fs = context->GetSubsystem<FileSystem>();
    if (!project || !fs)
        return ea::nullopt;

    ea::string dataDir = project->GetDataPath().c_str();
    NormalizePath(dataDir);
    if (!dataDir.empty() && dataDir.back() != '/')
        dataDir += '/';

    ea::string seed = dataDir;
    if (!current.empty())
    {
        ea::string rel = current;
        if (rel.front() == '/')
            rel = rel.substr(1);
        const ea::string abs = dataDir + rel;
        if (fs->FileExists(abs) || fs->DirExists(abs))
            seed = abs; // PickNativePath opens the folder that holds it
    }

    const auto picked = PickNativePath(false, filter ? ea::string(filter) : ea::string(), seed);
    if (!picked)
        return ea::nullopt; // cancelled, or the dialog failed (already logged)

    ea::string chosen = *picked;
    NormalizePath(chosen);
    if (dataDir.empty())
        return ea::nullopt;
    const ea::string lowerChosen = LowerCopy(chosen);
    const ea::string lowerData = LowerCopy(dataDir);
    if (lowerChosen.compare(0, lowerData.size(), lowerData) != 0)
    {
        URHO3D_LOGWARNING("UIViewTab: '{}' is outside the project Data folder '{}'; RmlUi cannot load it.",
            chosen.c_str(), dataDir.c_str());
        return ea::nullopt;
    }
    const ea::string rel = chosen.substr(dataDir.length());
    if (rel.empty())
        return ea::nullopt;
    return ea::optional<ea::string>(ea::string("/") + rel);
}

// A folder-open button parked on the line after a resource-path field. On a
// pick it returns the new '/'-rooted path; a cancel returns nullopt, so the
// caller can tell "nothing happened" from "repoint at this file". 'id' keeps
// the button's ImGui identity distinct from its field.
ea::optional<ea::string> ResourceBrowseWidget(const char* id, Context* context,
    const ea::string& current, const char* filter)
{
    ui::SameLine();
    ui::PushID(id);
    const bool clicked = ui::SmallButton(ICON_FA_FOLDER_OPEN);
    ui::PopID();
    if (ui::IsItemHovered())
        ui::SetTooltip("Browse...");
    if (!clicked)
        return ea::nullopt;
    return PickDataResource(context, current, filter);
}

// Open the nested document referenced by \a href (or, when revealOnly is set,
// just locate and highlight it in the Resource Browser). ProcessRequest
// defers the handling to the frame loop, so calling this from inside ImGui
// rendering is safe.
void OpenNestedDocumentFile(Context* context, UIViewDocument* doc, const ea::string& href, bool revealOnly)
{
    const ea::string resPath = ResolveRelativeResourcePath(doc->GetSourcePath(), href);
    if (resPath.empty())
        return;
    auto* project = context->GetSubsystem<Project>();
    if (project)
        project->ProcessRequest(MakeShared<OpenResourceRequest>(context, resPath, revealOnly).Get());
}
} // namespace

void Tabs_UIViewTab(Context* context, Project* project)
{
    project->AddTab(MakeShared<UIViewTab>(context));
}

// ---------------------------------------------------------------------------
// UIViewTab: view + controller over one UIViewDocument
// ---------------------------------------------------------------------------

UIViewTab::UIViewTab(Context* context)
    : ResourceEditorTab(context, "", "8f2b1c9e-7d34-4a5b-9c10-ui0preview",
        EditorTabFlags{}, EditorTabPlacement::DockCenter)
{
    title_ = GetProject()->GetUniqTabName(this->GetTypeName(), "Ui");
    guid_ = Format("8f2b1c9e-7d34-4a5b-9c10-uipreview{}", title_);
    uniqueId_ = Format("{}###{}", title_, guid_);
    // The first-ever instance is the persistent entry point: it keeps its
    // "open/new document" empty state after its document closes, while
    // instances spawned later close together with their document.
    isPrimary_ = (GetProject()->GetTabsByTypeName(this->GetTypeName()).size() == 0);

    // The document is created per resource in OnResourceLoaded: each open
    // .rml owns its own UIViewDocument with its private preview context,
    // and undo actions are routed through PushAction below so the base can
    // attribute every action to this instance's resource.
    hierarchySource_ = MakeShared<UIViewHierarchy>(this);
    inspectorSource_ = MakeShared<UIViewInspector>(this);
}

UIViewTab::~UIViewTab()
{
    selected_ = nullptr;
    document_ = nullptr;
}

void UIViewTab::OpenInBestInstance(Project* project, const ea::string& resourceName)
{
    if (!project)
        return;

    // Already open: focus the instance editing it (idempotent under the
    // request broadcast - later invocations end up here).
    for (const SharedPtr<EditorTab>& tab : project->GetTabs())
    {
        auto* uiTab = dynamic_cast<UIViewTab*>(tab.Get());
        if (uiTab && uiTab->IsResourceOpen(resourceName))
        {
            uiTab->OpenResource(resourceName);
            uiTab->Focus();
            // No OnResourceLoaded for an already-registered resource: take
            // the shared panels back explicitly (see ConnectSharedPanels).
            uiTab->ConnectSharedPanels();
            return;
        }
    }

    // An idle instance takes the document: the primary tab in its empty
    // state, or a secondary instance whose document was closed (Focus
    // reopens its window).
    for (const SharedPtr<EditorTab>& tab : project->GetTabs())
    {
        auto* uiTab = dynamic_cast<UIViewTab*>(tab.Get());
        if (uiTab && uiTab->GetActiveResourceName().empty())
        {
            uiTab->OpenResource(resourceName);
            uiTab->Focus();
            return;
        }
    }

    // Every instance is busy: spawn a fresh editor tab for the document
    // (one document per editor tab, VS Code style).
    const auto newTab = MakeShared<UIViewTab>(project->GetContext());
    project->AddTab(newTab);
    newTab->ApplyPlugins(); // glue the shared Hierarchy/Inspector to this instance
    newTab->OpenResource(resourceName);
    newTab->Focus();
}

void UIViewTab::OnProjectRequest(ProjectRequest* request)
{
    const auto openResourceRequest = dynamic_cast<OpenResourceRequest*>(request);
    if (!openResourceRequest || openResourceRequest->IsRevealOnly())
    {
        ResourceEditorTab::OnProjectRequest(request);
        return;
    }

    const ResourceFileDescriptor& desc = openResourceRequest->GetResource();
    if (desc.isDirectory_ || !CanOpenResource(desc))
        return;

    // One document per editor tab: route through the static arbiter instead
    // of the base behavior (which would open the resource in every
    // subscribed instance). Deferred to the frame loop, so calling this
    // from inside ImGui rendering is safe.
    Project* project = GetProject();
    const ea::string resourceName = desc.resourceName_;
    request->QueueProcessCallback([project, resourceName]()
    {
        OpenInBestInstance(project, resourceName);
    });
}

ea::vector<unsigned> UIViewTab::NodePath(const UiNode* node) const
{
    ea::vector<unsigned> path;
    if (document_)
        document_->GetModel().BuildPath(node, path);
    return path;
}

void UIViewTab::OnDocumentEdited()
{
    if (!document_)
        return;
    const UiDocumentModel& model = document_->GetModel();
    // Every command (and every undo/redo) rebuilds the whole model tree from
    // text, so no node pointer survives an edit; re-resolve the selection
    // from its stable child-index path.
    selected_ = model.ResolvePath(selPath_);
    if (!selected_ && !selPath_.empty())
    {
        // The node is gone (deleted, or an undo removed it): fall back to its
        // parent so the panel keeps some context. Never snap silently to the
        // root - the <body> panel looks like any element panel, and an edit
        // meant for the vanished node would land on the document root and
        // corrupt it instead (seen in the wild: class="image" appended to
        // <body> while the user believed they were editing an <img>).
        selPath_.pop_back();
        selected_ = model.ResolvePath(selPath_);
    }
    model.BuildPath(selected_, selPath_);
    if (inspectorSource_)
        inspectorSource_->InvalidateCaches();
}

void UIViewTab::SetSelectedNode(UiNode* node)
{
    selected_ = node;
    if (document_)
        document_->GetModel().BuildPath(node, selPath_);
    if (hierarchySource_)
        hierarchySource_->ExpandAncestors(selPath_);
}

// ---------------------------------------------------------------------------
// Resource lifecycle (driven by ResourceEditorTab)
// ---------------------------------------------------------------------------

bool UIViewTab::CanOpenResource(const ResourceFileDescriptor& desc)
{
    return !desc.isDirectory_ && desc.HasExtension(".rml");
}

ea::string UIViewTab::ReadResourceFile(const ea::string& resourceName) const
{
    auto* cache = GetSubsystem<ResourceCache>();
    ea::string abs = cache->GetResourceFileName(resourceName);
    if (abs.empty())
    {
        // Not registered in the cache yet (e.g. a file New just wrote): resolve against the
        // project Data folder, mirroring WriteResourceFile, so freshly created documents load.
        auto* project = GetProject();
        abs = project ? project->GetDataPath() + resourceName : resourceName;
    }

    File file(context_, abs, FILE_READ);
    if (!file.IsOpen())
        return ea::string();
    return file.ReadText();
}

bool UIViewTab::WriteResourceFile(const ea::string& resourceName, const ea::string& text)
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* fs = GetSubsystem<FileSystem>();
    ea::string abs = cache->GetResourceFileName(resourceName);
    if (abs.empty())
    {
        // New file: write under the project DataPath (mirrors AssetManager).
        auto* project = GetProject();
        if (!project)
            return false;
        abs = project->GetDataPath() + resourceName;
        const size_t sep = abs.find_last_of("/\\");
        if (sep != ea::string::npos)
            fs->CreateDirsRecursive(abs.substr(0, sep));
    }

    File file(context_, abs, FILE_WRITE);
    if (!file.IsOpen())
    {
        URHO3D_LOGERROR("UIViewTab: cannot open '{}' for writing.", abs.c_str());
        return false;
    }
    file.Write(text.data(), text.size());

    // Drop any cached File so a later load sees the new bytes.
    cache->ReleaseResource(resourceName, true);
    return true;
}

bool UIViewTab::WriteFileAt(const ea::string& absPath, const ea::string& text)
{
    auto* fs = GetSubsystem<FileSystem>();

    // File never creates intermediate directories, and the save dialog happily
    // accepts a path whose folders do not exist yet.
    const size_t sep = absPath.find_last_of("/\\");
    if (sep != ea::string::npos)
        fs->CreateDirsRecursive(absPath.substr(0, sep));

    {
        File file(context_, absPath, FILE_WRITE);
        if (!file.IsOpen())
        {
            URHO3D_LOGERROR("UIViewTab: cannot open '{}' for writing.", absPath.c_str());
            return false;
        }
        file.Write(text.data(), text.size());
    }

    // Close-and-reopen is the only honest confirmation the bytes landed: a path
    // that merely *looks* writable (read-only folder, wrong volume, antivirus)
    // would otherwise report success and leave the user staring at an empty folder.
    if (!fs->FileExists(absPath.c_str()))
    {
        URHO3D_LOGERROR("UIViewTab: '{}' was written but is not visible on disk", absPath.c_str());
        return false;
    }
    return true;
}

void UIViewTab::ResetViewToDocument()
{
    selected_ = document_ ? document_->GetModel().root_.Get() : nullptr;
    selPath_.clear();
    hoveredPath_.clear();
    gizmoNode_ = nullptr;
    dragging_ = false;
}

void UIViewTab::ConnectSharedPanels()
{
    Project* project = GetProject();
    if (auto hierarchyTab = project->FindTab<HierarchyBrowserTab>())
        hierarchyTab->ConnectToSource(hierarchySource_.Get());
    if (auto inspectorTab = project->FindTab<InspectorTab>())
        inspectorTab->ConnectToSource(inspectorSource_.Get());
}

void UIViewTab::OnResourceLoaded(const ea::string& resourceName)
{
    // One document per tab instance. The base has already registered the
    // resource by the time this runs, so undo actions pushed below attribute
    // to it correctly.
    if (!document_)
    {
        document_ = MakeShared<UIViewDocument>(context_);
        document_->OnModelEdited.Subscribe(this, &UIViewTab::OnDocumentEdited);
        // Route editing commands through the tab so ResourceEditorTab
        // attributes each action to this instance's resource (dirty tracking
        // + undo focus). When no resource is open the pusher declines and
        // the document falls back to the raw project undo manager, so edits
        // stay undoable.
        document_->SetUndoPusher([this](const SharedPtr<EditorAction>& action) -> bool
        {
            const ea::string& active = GetActiveResourceName();
            if (!active.empty() && IsResourceOpen(active))
                return PushAction(action).has_value();
            return false;
        });
    }

    const ea::string contents = ReadResourceFile(resourceName);
    if (contents.empty())
    {
        URHO3D_LOGERROR("UIViewTab: failed to read UI document '{}'", resourceName.c_str());
        return;
    }

    if (document_->LoadFromText(contents, resourceName))
    {
        // Fresh open: start with the root selected. Not gated on the active
        // resource: at runtime the base activates the resource only after
        // this callback returns, and a single-document instance hosts no
        // other document this selection could belong to.
        ResetViewToDocument();
        if (hierarchySource_)
            hierarchySource_->ExpandAncestors(selPath_);
        // The freshly loaded document is the active editing target: take the
        // shared panels right away instead of waiting for a focus-driven
        // rebind (covers ini restore, runtime open and spawned instances).
        ConnectSharedPanels();
    }
    else
    {
        URHO3D_LOGERROR("UIViewTab: failed to parse UI document '{}'", resourceName.c_str());
    }
}

void UIViewTab::OnResourceUnloaded(const ea::string& resourceName)
{
    (void)resourceName; // single-document instance: at most one resource open
    document_ = nullptr;
    selected_ = nullptr;
    selPath_.clear();
    hoveredPath_.clear();
    gizmoNode_ = nullptr;
    dragging_ = false;

    // Secondary instances exist only to host their document: when it closes,
    // so does the editor tab. The primary instance stays around as the
    // "new/open document" entry point (and is reused by OpenInBestInstance).
    if (!isPrimary_)
        Close();
}

void UIViewTab::OnActiveResourceChanged(const ea::string& oldResourceName, const ea::string& newResourceName)
{
    (void)oldResourceName;
    // A drag cannot span an activation change (the change re-opens the view).
    dragging_ = false;
    gizmoNode_ = nullptr;

    // Single-document instance: an activation change can only happen around
    // load/unload of this instance's one document. Attach the view when the
    // document already exists; OnResourceLoaded resets it right after load.
    if (newResourceName.empty() || !document_ || document_->GetSourcePath() != newResourceName)
    {
        document_ = nullptr;
        selected_ = nullptr;
        selPath_.clear();
        hoveredPath_.clear();
        return;
    }
    ResetViewToDocument();
}

void UIViewTab::OnResourceSaved(const ea::string& resourceName)
{
    // One document per instance: emit this instance's document.
    if (!document_ || !document_->GetRmlDocument())
        return;
    WriteResourceFile(resourceName, document_->EmitRml());
    document_->MarkSaved();
}

void UIViewTab::OnResourceShallowSaved(const ea::string& resourceName)
{
    // No per-resource "shallow" data distinct from the emitted .rml text.
    (void)resourceName;
}

void UIViewTab::WriteIniSettings(ImGuiTextBuffer& output)
{
    ResourceEditorTab::WriteIniSettings(output);

    // Only the primary instance persists the editor's document layout:
    // the active document of every UIViewTab instance, in tab order, under
    // the primary instance's own ini section. Secondary instances write
    // nothing - after a restart they do not exist until this section
    // re-creates them, so their sections would have no reader.
    if (!isPrimary_)
        return;

    Project* project = GetProject();
    if (!project)
        return;

    StringVector documents;
    for (const SharedPtr<EditorTab>& tab : project->GetTabs())
    {
        auto* uiTab = dynamic_cast<UIViewTab*>(tab.Get());
        if (!uiTab || uiTab == this)
            continue;
        const ea::string& name = uiTab->GetActiveResourceName();
        if (!name.empty())
            documents.push_back(name);
    }
    // This instance's own document comes last so the restored active tab
    // (the primary instance) is the one the user last had focused.
    if (!GetActiveResourceName().empty())
        documents.push_back(GetActiveResourceName());

    WriteStringToIni(output, "Documents", ea::string::joined(documents, "|"));
}

void UIViewTab::ReadIniSettings(const char* line)
{
    ResourceEditorTab::ReadIniSettings(line);

    // Only the primary instance reads the shared document list: it re-creates
    // the secondary instances (which have no ini section of their own) and
    // then opens its own last document through the base's "ActiveResourceName".
    if (!isPrimary_)
        return;

    if (const auto value = ReadStringFromIni(line, "Documents"))
    {
        Project* project = GetProject();
        for (const ea::string& resourceName : value->split('|'))
        {
            if (resourceName.empty() || IsResourceOpen(resourceName))
                continue;
            OpenInBestInstance(project, resourceName);
        }
    }
}

void UIViewTab::NewDocument()
{
    // RmlUi has no built-in default font: without a font-family rule, text
    // added to a fresh document renders as blank. Declare the same family the
    // project's own default.rcss uses (shipped in core Data/Fonts).
    static const ea::string kTemplate =
        "<rml>\n"
        "  <head>\n"
        "    <style>\n"
        "    body { font-family: \"Noto Sans\"; }\n"
        "    </style>\n"
        "  </head>\n"
        "  <body style=\"width: 1024px; height: 768px;\">\n"
        "  </body>\n"
        "</rml>\n";

    auto* project = GetProject();
    if (!project)
        return;
    auto* fs = GetSubsystem<FileSystem>();

    // Native "Save As" rooted at the project's Data folder. UI documents must live under
    // Data to be loadable resources (and to appear in the Resource Browser), so a path that
    // resolves outside it is rejected rather than silently rewritten.
    ea::string dataDir = project->GetDataPath().c_str();
    NormalizePath(dataDir);
    if (!dataDir.empty() && dataDir.back() != '/')
        dataDir += '/';

    nfdu8filteritem_t filterItem;
    filterItem.name = "RmlUi document";
    filterItem.spec = "rml";

    // The shell aborts the dialog with NFD_ERROR when handed a starting directory
    // that does not exist, so only seed it with a folder that really is there.
    const char* initialDir = !dataDir.empty() && fs->DirExists(dataDir) ? dataDir.c_str() : nullptr;

    nfdu8char_t* outPath = nullptr;
    const nfdresult_t res = NFD_SaveDialogU8(&outPath, &filterItem, 1,
        initialDir, "NewDocument.rml");
    if (res == NFD_ERROR)
    {
        URHO3D_LOGERROR("UIViewTab: save dialog failed: {}", NFD_GetError());
        return;
    }
    if (res != NFD_OKAY)
        return; // canceled: leave the current view untouched

    ea::string chosen = outPath;
    NFD_FreePathU8(outPath);
    NormalizePath(chosen);
    if (chosen.empty())
    {
        URHO3D_LOGERROR("UIViewTab: the save dialog returned an empty path");
        return;
    }
    if (chosen.size() < 4 || LowerCopy(chosen).compare(chosen.size() - 4, 4, ".rml") != 0)
        chosen += ".rml";

    const ea::string lowerChosen = LowerCopy(chosen);
    const ea::string lowerData = LowerCopy(dataDir);
    if (!dataDir.empty() && lowerChosen.compare(0, lowerData.size(), lowerData) != 0)
    {
        URHO3D_LOGWARNING("UIViewTab: UI documents must be created under the project Data folder "
            "('{}'); '{}' was ignored.",
            dataDir.c_str(), chosen.c_str());
        return;
    }
    ea::string resourceName = chosen.substr(dataDir.length());
    if (resourceName.empty())
    {
        URHO3D_LOGERROR("UIViewTab: cannot derive a resource name from '{}' against Data path '{}'",
            chosen.c_str(), dataDir.c_str());
        return;
    }

    // "New" owes the user a file at exactly the path they picked (the dialog already
    // asked before replacing anything). The old rule skipped the write whenever this
    // instance merely *tracked* the name - which also skipped it when that file had
    // been deleted on disk or had never landed, so New silently produced no file and
    // no error, just an empty folder.
    const bool onDisk = fs->FileExists(chosen);
    if (!onDisk || !IsResourceOpen(resourceName))
    {
        // Write through the absolute path: the dialog resolved where the bytes go,
        // so re-deriving it from the resource name only adds ways to miss - and a
        // mismatch would drop the file in a folder the user is not looking at.
        if (!WriteFileAt(chosen, kTemplate))
        {
            URHO3D_LOGERROR("UIViewTab: failed to create UI document '{}' at '{}'",
                resourceName.c_str(), chosen.c_str());
            return;
        }
        // Forget a cached copy of a replaced file so the load below sees fresh bytes;
        // a brand-new name has nothing cached and releasing it would only log noise.
        auto* cache = GetSubsystem<ResourceCache>();
        if (onDisk || !cache->GetResourceFileName(resourceName).empty())
            cache->ReleaseResource(resourceName, true);
    }

    URHO3D_LOGINFO("UIViewTab: UI document '{}' ready at '{}'", resourceName.c_str(), chosen.c_str());

    // Route through the project request exactly like a double-click open
    // (all instances arbitrate in OpenInBestInstance: idle ones take the
    // document, a busy project spawns a fresh tab). This must not call
    // OpenInBestInstance directly: we run inside a tab's Render here, and the
    // spawn path's AddTab would land mid-iteration of the render loop, so
    // the fresh tab is not rendered this frame - the frame-end
    // CheckRemoveTab would then see it as closed (its window has never
    // opened) and destroy it. ProcessRequest defers the routing to the
    // beginning of the next frame, where AddTab happens before tabs are
    // iterated. A document that is already open is merely focused (closing it
    // would discard its undo history and unsaved edits), and a name that the
    // resource index has not picked up yet still resolves, because the request
    // is built from the name and the load falls back to the project Data path.
    auto request = MakeShared<OpenResourceRequest>(context_, resourceName, false);
    GetProject()->ProcessRequest(request.Get());
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void UIViewTab::RenderContent()
{
    RenderToolbar();
    ui::Separator();
    RenderPreview();
}

void UIViewTab::RenderToolbar()
{
    // Read-only display of the document being edited (empty until one is opened).
    const ea::string& activeResource = GetActiveResourceName();
    ui::AlignTextToFramePadding();
    ui::TextDisabled("Editing: %s", activeResource.empty() ? "(no document open)" : activeResource.c_str());

    if (ui::Button(ICON_FA_FILE_LINES " New"))
        NewDocument();

    const bool hasDoc = document_ && document_->GetRmlDocument() != nullptr;
    const bool hasActive = !GetActiveResourceName().empty();
    ui::SameLine();
    ui::BeginDisabled(!hasActive);
    if (ui::Button(ICON_FA_FLOPPY_DISK " Save"))
        SaveCurrentResource();
    ui::SameLine();
    if (ui::Button(ICON_FA_ROTATE " Reload"))
    {
        const ea::string name = GetActiveResourceName();
        if (!name.empty())
        {
            CloseResource(name);
            OpenResource(name);
        }
    }
    ui::EndDisabled();

    if (hasActive && IsResourceUnsaved(GetActiveResourceName()))
    {
        ui::SameLine();
        ui::TextDisabled("(unsaved)");
    }

    // Second row: structural editing, driven by the palette table.
    ui::BeginDisabled(!hasDoc);
    if (ui::BeginCombo(ICON_FA_PLUS " Add Widget", "Select..."))
    {
        // Compare by content, not by pointer: identical string literals are
        // only merged into one address when the compiler pools strings, so a
        // pointer comparison would re-print the header before every entry on
        // builds without pooling.
        ea::string lastGroup;
        for (const PaletteEntry& entry : kPalette)
        {
            if (lastGroup != entry.group_)
            {
                ui::SeparatorText(entry.group_);
                lastGroup = entry.group_;
            }
            if (ui::Selectable(entry.label_))
            {
                if (UiNode* added = document_->AddWidget(selected_, MakeWidgetSpec(entry)))
                    SetSelectedNode(added);
            }
        }
        // Any tag: RmlUi tag names are only stylesheet lookup keys, so any name
        // the project styles is valid. The entry gets the generic placeholder
        // look until the project's stylesheet defines it.
        ui::SeparatorText("Arbitrary tag");
        static char anyTag[32] = "";
        const bool enterPressed = ui::InputText("##anyTag", anyTag, sizeof(anyTag),
            ImGuiInputTextFlags_EnterReturnsTrue);
        bool tagLooksValid = anyTag[0] != '\0';
        for (const char* p = anyTag; *p; ++p)
        {
            if (!std::isalnum(static_cast<unsigned char>(*p)) && *p != '-' && *p != '_')
            {
                tagLooksValid = false;
                break;
            }
        }
        ui::SameLine();
        ui::BeginDisabled(!tagLooksValid);
        if (tagLooksValid && (enterPressed || ui::Button(ICON_FA_PLUS " Add")))
        {
            UiWidgetSpec spec;
            spec.tag_ = anyTag;
            spec.stylePolicy_ = UiWidgetStylePolicy::Panel;
            if (UiNode* added = document_->AddWidget(selected_, spec))
                SetSelectedNode(added);
            anyTag[0] = '\0';
        }
        ui::EndDisabled();
        ui::EndCombo();
    }
    ui::SameLine();
    const bool isRealElement = selected_ && document_
        && selected_ != document_->GetModel().root_.Get()
        && !selected_->IsNestedDoc() && !selected_->IsHeadLink();
    ui::BeginDisabled(!isRealElement);
    if (ui::Button(ICON_FA_COPY " Copy"))
    {
        if (UiNode* copy = document_->DuplicateNode(selected_))
            SetSelectedNode(copy);
    }
    ui::EndDisabled();
    // Delete also covers the virtual link nodes: deleting one removes its
    // <link> line from <head> (text-level, undoable). Copy does not - a
    // duplicated link line would be pointless noise.
    const bool canDelete = selected_ && document_
        && selected_ != document_->GetModel().root_.Get();
    ui::SameLine();
    ui::BeginDisabled(!canDelete);
    if (ui::Button(ICON_FA_TRASH " Delete"))
        document_->DeleteNode(selected_); // OnModelEdited revalidates the selection
    ui::EndDisabled();
    ui::EndDisabled();

    // Element context menu (opened by a right-click on the preview).
    if (ui::BeginPopup("##uiElemCtx"))
    {
        UiNode* node = selected_;
        if (node && document_ && node != document_->GetModel().root_.Get() && !node->IsNestedDoc()
            && !node->IsHeadLink())
        {
            if (ui::MenuItem(ICON_FA_COPY " Copy"))
            {
                if (UiNode* copy = document_->DuplicateNode(node))
                    SetSelectedNode(copy);
            }
            if (ui::MenuItem(ICON_FA_TRASH " Delete"))
                document_->DeleteNode(node); // OnModelEdited revalidates the selection
        }
        ui::EndPopup();
    }
}

void UIViewTab::RenderPreview()
{
    if (!document_ || !document_->GetRmlDocument())
    {
        ui::TextUnformatted("No UI document open.\nDouble-click a .rml in the Resource Browser to edit it, or click New\nto create one (a Save As dialog picks the location under the project Data).");
        return;
    }

    // The document renders into the texture from E_BEGINRENDERING; here we
    // only sample the produced texture (never issue draws during widget build).
    const IntVector2 previewSize = document_->GetPreviewSize();
    const ImVec2 avail = ui::GetContentRegionAvail();
    float scale = ea::min(avail.x / static_cast<float>(previewSize.x_),
                          avail.y / static_cast<float>(previewSize.y_));
    if (scale <= 0.0f)
        scale = 0.1f;
    const ImVec2 displaySize(previewSize.x_ * scale, previewSize.y_ * scale);

    Widgets::Image(document_->GetPreviewTexture(), displaySize);

    DocViewport vp;
    vp.origin_ = V2(ui::GetItemRectMin());
    vp.scale_ = scale;

    HandlePreviewPointer(vp);
    DrawOverlay(vp);
}

// ---------------------------------------------------------------------------
// Pointer / drag routing
// ---------------------------------------------------------------------------

void UIViewTab::HandlePreviewPointer(const DocViewport& vp)
{
    ImGuiIO& io = ui::GetIO();
    // Hover is computed from the image rect directly rather than
    // ImGui::IsItemHovered(): Widgets::Image submits a zero-ID plain Image, and
    // item-hover on such an item is unreliable across ImGui versions (it silently
    // returned false here, disabling preview picking and the hover outline).
    const IntVector2 previewSize = document_->GetPreviewSize();
    const ImVec2 imageMin = IV2(vp.origin_);
    const ImVec2 imageMax(imageMin.x + previewSize.x_ * vp.scale_,
                          imageMin.y + previewSize.y_ * vp.scale_);
    const bool overImage = ui::IsMouseHoveringRect(imageMin, imageMax);
    const Vector2 doc = vp.ToDoc(V2(io.MousePos));

    if (dragging_)
    {
        UpdateDrag(vp);
        if (ui::IsMouseReleased(ImGuiMouseButton_Left))
            CommitDrag();
        return;
    }

    // "Our tab window is the front-most one under the cursor" is the correct
    // in-editor gate. The usual !io.WantCaptureMouse guard is useless here: the
    // whole rbfx editor is ImGui, so hovering any window (this docked tab) makes
    // WantCaptureMouse true, which silently disabled picking, hover and drag.
    // IsWindowHovered() is false when a popup/tooltip/other window is on top,
    // so we still defer to the context menu and neighbouring panels.
    const bool active = overImage && ui::IsWindowHovered(ImGuiHoveredFlags_None);

    UiNode* hover = active ? document_->HitTest(doc) : nullptr;
    hoveredPath_ = hover ? NodePath(hover) : ea::vector<unsigned>{};

    if (!active)
        return;

    if (ui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        if (hover)
            SetSelectedNode(hover);
        ui::OpenPopup("##uiElemCtx");
        return;
    }

    if (ui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        // A gizmo handle on the current selection wins over re-picking. The
        // drag box is left/top-space; shift the absolute mouse point into that
        // frame before picking.
        UiBox box;
        Vector2 base;
        if (document_ && document_->TryGetDragBox(selected_, box, base))
        {
            const float grab = kHandleGrabPx / vp.scale_;
            const GizmoHandle handle = PickGizmoHandle(box, doc - base, grab);
            if (handle.op_ != GizmoOp::None)
            {
                BeginDrag(handle, selected_, vp);
                return;
            }
        }
        SetSelectedNode(hover); // clicking empty space (hover==null) clears
        // Double-click on the template chrome (the hover resolves to the
        // nested-doc virtual node) opens the nested file, mirroring the
        // hierarchy's double-click behavior.
        if (hover && hover->IsNestedDoc() && ui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            OpenNestedDocumentFile(context_, document_, hover->nestedDocHref_, /*revealOnly=*/false);
    }
}

void UIViewTab::BeginDrag(const GizmoHandle& handle, UiNode* node, const DocViewport& vp)
{
    gizmoNode_ = node;
    gizmoDrag_ = handle;
    gizmoStartBox_ = UiBox{};
    // Seed the box and its frame origin together (authored px, else the
    // rendered box against the containing block) so a freshly-positioned
    // element drags from where it already is.
    document_->TryGetDragBox(node, gizmoStartBox_, gizmoBase_);
    gizmoPressDoc_ = gizmoCurDoc_ = vp.ToDoc(V2(ui::GetIO().MousePos));
    gizmoLiveBox_ = gizmoStartBox_;
    dragging_ = true;
}

void UIViewTab::UpdateDrag(const DocViewport& vp)
{
    gizmoCurDoc_ = vp.ToDoc(V2(ui::GetIO().MousePos));
    const UiBox solved = SolveDrag(gizmoStartBox_, gizmoDrag_, gizmoPressDoc_, gizmoCurDoc_);
    gizmoLiveBox_ = solved;

    // Live preview into the DOM projection; the model is only touched on
    // release. Layout re-flows next E_POSTUPDATE (Context::Update).
    document_->SetLiveBox(gizmoNode_, solved);
}

void UIViewTab::CommitDrag()
{
    if (gizmoNode_)
        document_->CommitBoxEdit(gizmoNode_, SolveDrag(gizmoStartBox_, gizmoDrag_, gizmoPressDoc_, gizmoCurDoc_));
    dragging_ = false;
    gizmoNode_ = nullptr;
}

// ---------------------------------------------------------------------------
// Overlay / gizmo drawing (ImGui foreground draw list - presentation only)
// ---------------------------------------------------------------------------

namespace
{
void TransformedCorners(const DocViewport& vp, const UiBox& box, ImVec2 out[4])
{
    const Vector2 corners[4] = {
        box.pos_,
        Vector2{box.pos_.x_ + box.size_.x_, box.pos_.y_},
        Vector2{box.pos_.x_ + box.size_.x_, box.pos_.y_ + box.size_.y_},
        Vector2{box.pos_.x_, box.pos_.y_ + box.size_.y_},
    };
    for (int i = 0; i < 4; i++)
        out[i] = IV2(vp.ToScreen(ForwardMapPoint(corners[i], box)));
}

void DrawHandleSquare(ImDrawList* dl, const ImVec2& center, ImU32 fill)
{
    const ImVec2 a(center.x - kHandleDrawPx, center.y - kHandleDrawPx);
    const ImVec2 b(center.x + kHandleDrawPx, center.y + kHandleDrawPx);
    dl->AddRectFilled(a, b, fill);
    dl->AddRect(a, b, kGizmoHandleBorder);
}
} // namespace

void UIViewTab::DrawOverlay(const DocViewport& vp)
{
    ImDrawList* dl = ui::GetWindowDrawList();
    UiNode* sel = selected_;
    UiNode* hover = document_->GetModel().ResolvePath(hoveredPath_);

    if (hover && hover != sel && hover->dom_ && !hover->IsText())
    {
        ea::vector<UiBox> boxes;
        if (document_->TryGetDomBoxes(hover, boxes))
        {
            for (const UiBox& box : boxes)
            {
                ImVec2 c[4];
                TransformedCorners(vp, box, c);
                dl->AddPolyline(c, 4, kHoverColor, ImDrawFlags_Closed, 1.0f);
            }
        }
    }

    if (sel && sel->dom_)
    {
        const bool dragging = dragging_ && gizmoNode_ == sel;
        ea::vector<UiBox> boxes;
        if (dragging)
        {
            // The live box is left/top-space; lift it into document space with
            // the frame origin captured at press.
            UiBox box = gizmoLiveBox_;
            box.pos_ += gizmoBase_;
            boxes.push_back(box);
        }
        else
            document_->TryGetDomBoxes(sel, boxes);

        for (const UiBox& box : boxes)
        {
            if (box.size_.x_ <= 0.0f || box.size_.y_ <= 0.0f)
                continue;
            ImVec2 c[4];
            TransformedCorners(vp, box, c);
            dl->AddConvexPolyFilled(c, 4, kSelectFill);
            dl->AddPolyline(c, 4, kSelectColor, ImDrawFlags_Closed, 2.0f);
        }

        // The gizmo rect is the selection rect (regular nodes yield a single
        // box, so front() is it). Handles are offered only for an absolutely
        // positioned node - the same rule that gates picking/dragging - or
        // while a drag is live.
        if (!boxes.empty() && (dragging || sel->GetStyle("position") == "absolute"))
            DrawGizmo(vp, boxes.front());
    }
}

void UIViewTab::DrawGizmo(const DocViewport& vp, const UiBox& box)
{
    ImDrawList* dl = ui::GetWindowDrawList();
    ImVec2 c[4];
    TransformedCorners(vp, box, c);

    auto anchorScreen = [&vp, &box](const GizmoHandle& h) {
        return IV2(vp.ToScreen(ForwardMapPoint(
            Vector2{box.pos_.x_ + h.u_ * box.size_.x_, box.pos_.y_ + h.v_ * box.size_.y_}, box)));
    };

    // Connector to the rotate handle, drawn behind the squares.
    const ImVec2 topMid((c[0].x + c[1].x) * 0.5f, (c[0].y + c[1].y) * 0.5f);
    const ImVec2 rotPos = anchorScreen(kRotateHandle);
    dl->AddLine(topMid, rotPos, kGizmoLine, 1.0f);

    for (const GizmoHandle& h : kResizeHandles)
    {
        const bool active = dragging_ && gizmoDrag_.op_ == GizmoOp::Resize && gizmoDrag_.mask_ == h.mask_;
        DrawHandleSquare(dl, anchorScreen(h), active ? kMoveColor : kGizmoHandle);
    }
    const ImVec2 centerPos = anchorScreen(kMoveHandle);
    dl->AddCircleFilled(centerPos, kHandleDrawPx, kGizmoHandle);
    dl->AddCircle(centerPos, kHandleDrawPx, kGizmoHandleBorder);
    DrawHandleSquare(dl, rotPos, kRotateColor);
    DrawHandleSquare(dl, anchorScreen(kScaleHandle), kScaleColor);
    DrawHandleSquare(dl, anchorScreen(kScaleXHandle), kScaleColor);

    if (dragging_)
    {
        ea::string tip;
        switch (gizmoDrag_.op_)
        {
        case GizmoOp::Move:
            tip = Format("x %s  y %s", FormatPx(gizmoLiveBox_.pos_.x_).c_str(), FormatPx(gizmoLiveBox_.pos_.y_).c_str());
            break;
        case GizmoOp::Resize:
            tip = Format("%s × %s", FormatPx(gizmoLiveBox_.size_.x_).c_str(), FormatPx(gizmoLiveBox_.size_.y_).c_str());
            break;
        case GizmoOp::Rotate:
            tip = Format("%s°", FormatCssNumber(gizmoLiveBox_.xform_.rotateDeg_).c_str());
            break;
        case GizmoOp::Scale:
        case GizmoOp::ScaleX:
            tip = Format("× %s", FormatCssNumber(gizmoLiveBox_.xform_.scaleX_).c_str());
            break;
        default:
            break;
        }
        const ImVec2 m = ui::GetMousePos();
        dl->AddText(ImVec2(m.x + 14, m.y + 12), kGizmoText, tip.c_str());
    }
}

// ---------------------------------------------------------------------------
// UIViewHierarchy: walks the editor model (not the DOM)
// ---------------------------------------------------------------------------

UIViewHierarchy::UIViewHierarchy(UIViewTab* owner)
    : Object(owner->GetContext())
    , owner_(owner)
{
}

bool UIViewHierarchy::PathIn(const ea::vector<ea::vector<unsigned>>& set, const ea::vector<unsigned>& path)
{
    for (const ea::vector<unsigned>& p : set)
    {
        if (p == path)
            return true;
    }
    return false;
}

void UIViewHierarchy::ExpandAncestors(const ea::vector<unsigned>& path)
{
    // Open every prefix of the selected path so the tree reveals the
    // selection. A manually collapsed prefix is re-opened: revealing the
    // selection wins over the override, otherwise a collapsed ancestor
    // would hide the very node that was just selected.
    ea::vector<unsigned> prefix;
    for (size_t i = 0; i <= path.size(); i++)
    {
        if (!PathIn(openedPaths_, prefix))
            openedPaths_.push_back(prefix);
        closedPaths_.erase(std::remove(closedPaths_.begin(), closedPaths_.end(), prefix), closedPaths_.end());
        if (i < path.size())
            prefix.push_back(path[i]);
    }
}

bool UIViewHierarchy::IsOpen(UiNode* node, const ea::vector<unsigned>& path) const
{
    UIViewTab* tab = owner_;
    if (!tab)
        return true;
    if (focusPathOnly_)
    {
        // Open only if this node is a proper ancestor of the selection.
        const ea::vector<unsigned>& sel = tab->GetSelectedPath();
        if (sel.size() <= path.size())
            return false;
        for (size_t i = 0; i < path.size(); i++)
        {
            if (sel[i] != path[i])
                return false;
        }
        return true;
    }
    if (PathIn(closedPaths_, path))
        return false;
    if (PathIn(openedPaths_, path))
        return true;
    return true; // default expanded
}

void UIViewHierarchy::RenderContent()
{
    UIViewTab* tab = owner_;
    if (!tab || !tab->GetDocument() || !tab->GetDocument()->GetModel().root_)
    {
        ui::TextDisabled("(no document)");
        return;
    }

    ui::Checkbox("Focus Path Only", &focusPathOnly_);
    RenderNode(tab->GetDocument()->GetModel().root_.Get(), ea::vector<unsigned>{});

    // Row-scoped context menu. The menu itself is shared with the tab-level
    // context: it resolves the recorded target path on every use, falling
    // back to the selection. Opening is deferred to here, right before
    // BeginPopup: ImGui hashes a named popup ID against the current ID-stack
    // seed, and tree rows render below every expanded ancestor's PushID, so
    // an OpenPopup issued inside a row would hash a different ID than this
    // Begin site (window base stack) and the popup would sit orphaned on the
    // stack. RenderNode only records the request.
    if (openNodeMenuRequested_)
    {
        ui::OpenPopup(kUiNodePopupId);
        openNodeMenuRequested_ = false;
    }
    if (ui::BeginPopup(kUiNodePopupId))
    {
        RenderContextMenuItems();
        ui::EndPopup();
    }
}

void UIViewHierarchy::RenderNode(UiNode* node, const ea::vector<unsigned>& path)
{
    UIViewTab* tab = owner_;
    if (!tab || !node)
        return;

    ea::string label;
    if (node->IsHeadLink())
    {
        // A head link reads as "kind + what it pulls in": the two kinds RmlUi
        // supports differ in what the href points at (a .rcss look vs a .rml
        // window template). The path is edited in the Inspector panel.
        const ea::string type = node->GetAttribute("type");
        const ea::string kind = type == "text/template" ? "rml"
            : (type == "text/rcss" ? "css" : (type.empty() ? "link" : type));
        label = ea::string(ICON_FA_LINK) + " " + kind + " " + node->GetAttribute("href");
    }
    else if (node->IsNestedDoc())
    {
        // The template-chrome virtual node reads as the file it comes from;
        // the full resolved path lives in the Inspector panel.
        ea::string name = node->nestedDocHref_;
        const size_t slash = name.find_last_of('/');
        if (slash != ea::string::npos)
            name = name.substr(slash + 1);
        label = ea::string(ICON_FA_CUBES) + " " + name;
    }
    else
    {
        label = node->IsText() ? ea::string("#text") : node->tag_;
        // An <input> is a whole family of controls behind one tag name; the
        // tree labels it by its type so a stack of them is legible at a glance
        // (checkbox vs radio vs range all render as a bare "input" otherwise).
        // This is display text only - tag_ and the type attribute in the
        // document are untouched. An absent type means the engine's default.
        if (node->tag_ == "input")
        {
            const ea::string type = node->GetAttribute("type");
            label += "(" + (type.empty() ? ea::string("text") : LowerCopy(type)) + ")";
        }
        if (!node->id_.empty())
            label += "#" + node->id_;
        if (!node->classes_.empty())
        {
            ea::string cls = node->classes_;
            cls.replace(" ", ".");
            label += "." + cls;
        }
    }

    // Children that matter for the tree (a lone text child is folded into the
    // parent's row for brevity).
    bool hasElementChild = false;
    for (const SharedPtr<UiNode>& child : node->children_)
    {
        if (!child->IsText())
        {
            hasElementChild = true;
            break;
        }
    }

    const bool selected = tab->GetSelectedNode() == node;
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (hasElementChild)
    {
        ui::SetNextItemOpen(IsOpen(node, path));
    }
    else
    {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (selected)
        flags |= ImGuiTreeNodeFlags_Selected;

    const bool open = ui::TreeNodeEx(node, flags, "%s", label.c_str());

    if (ui::IsItemClicked(ImGuiMouseButton_Left) && !ui::IsItemToggledOpen())
        tab->SetSelectedNode(node);
    // Row context menu. The target is recorded at release time: the press may
    // have started on a different row, and ImGui opens the popup where the
    // button is released. Store the path, not the pointer: commands rebuild
    // the whole tree. The popup is opened at the end of RenderContent (see
    // there for why the request is deferred instead of opened right here).
    if (ui::IsItemHovered() && ui::IsMouseReleased(ImGuiMouseButton_Right))
    {
        contextMenuTargetPath_ = path;
        contextMenuTargetValid_ = true;
        openNodeMenuRequested_ = true;
    }
    // Double-click on the nested-doc node jumps straight into the nested file
    // (opens alongside; multi-document keeps this document open).
    if (node->IsNestedDoc() && ui::IsItemClicked(ImGuiMouseButton_Left)
        && ui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
        if (UIViewDocument* doc = tab->GetDocument())
            OpenNestedDocumentFile(tab->GetContext(), doc, node->nestedDocHref_, /*revealOnly=*/false);
    }
    // Double-clicking a template link opens the linked .rml alongside, same
    // as the nested-doc node (the link is that template's head declaration).
    if (node->IsHeadLink() && node->GetAttribute("type") == "text/template"
        && ui::IsItemClicked(ImGuiMouseButton_Left)
        && ui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
        if (UIViewDocument* doc = tab->GetDocument())
            OpenNestedDocumentFile(tab->GetContext(), doc, node->GetAttribute("href"), /*revealOnly=*/false);
    }

    // Drag the row out of its container. Root and the virtual nodes are not
    // draggable: root IS the document, the nested-doc node has no authored
    // source here to move, and head links are <head> constructs with a fixed
    // place (their order is the stylesheet cascade order).
    if (node != tab->GetDocument()->GetModel().root_.Get() && !node->IsNestedDoc()
        && !node->IsHeadLink() && ui::BeginDragDropSource())
    {
        // Payload bytes must be contiguous; the local copy is swallowed
        // (copied) by SetDragDropPayload within the same frame.
        ea::vector<unsigned> dragPath = path;
        ui::SetDragDropPayload(kUiNodeDragType, dragPath.data(),
            dragPath.size() * sizeof(unsigned));
        ui::TextUnformatted(label.c_str());
        ui::EndDragDropSource();
    }
    // Drop on a row re-parents the dragged node into that container (appended
    // at the end). MoveNode rejects the illegal cases (drop on oneself, on a
    // descendant, on the virtual node). Hover feedback: the row outlines as
    // the pending container while the payload is over it.
    if (!node->IsNestedDoc() && !node->IsHeadLink() && !node->IsText() && ui::BeginDragDropTarget())
    {
        ui::GetWindowDrawList()->AddRect(ui::GetItemRectMin(), ui::GetItemRectMax(),
            kHoverColor, 0.0f, 0, 2.0f);
        if (const ImGuiPayload* payload = ui::AcceptDragDropPayload(kUiNodeDragType))
        {
            const unsigned* srcPath = static_cast<const unsigned*>(payload->Data);
            ea::vector<unsigned> src(srcPath, srcPath + payload->DataSize / sizeof(unsigned));
            UIViewDocument* doc = tab->GetDocument();
            if (UiNode* moved = doc->MoveNode(doc->GetModel().ResolvePath(src), node,
                static_cast<unsigned>(node->children_.size())))
            {
                tab->SetSelectedNode(moved);
            }
        }
        ui::EndDragDropTarget();
    }

    if (hasElementChild && ui::IsItemToggledOpen())
    {
        // Persist the manual toggle as an override, in either direction:
        // \a open is already the post-toggle state, so a node expanded by
        // hand moves from closedPaths_ to openedPaths_ and one collapsed by
        // hand moves the other way. IsOpen honors closedPaths_ first, which
        // is what keeps the node folded on the next frame's SetNextItemOpen.
        auto& from = open ? closedPaths_ : openedPaths_;
        auto& to = open ? openedPaths_ : closedPaths_;
        from.erase(std::remove(from.begin(), from.end(), path), from.end());
        to.push_back(path);
    }

    if (hasElementChild && open)
    {
        ea::vector<unsigned> childPath = path;
        for (unsigned i = 0; i < node->children_.size(); i++)
        {
            const SharedPtr<UiNode>& child = node->children_[i];
            if (child->IsText())
                continue;
            childPath.resize(path.size());
            childPath.push_back(i);
            RenderNode(child.Get(), childPath);
        }
        ui::TreePop();
    }
}

void UIViewHierarchy::RenderContextMenuItems()
{
    UIViewTab* tab = owner_;
    if (!tab || !tab->GetDocument())
        return;
    UIViewDocument* doc = tab->GetDocument();
    // Resolve the right-click target from its path on every use; the node the
    // click landed on may already have been rebuilt away by an edit.
    UiNode* target = contextMenuTargetValid_
        ? doc->GetModel().ResolvePath(contextMenuTargetPath_)
        : nullptr;
    if (!target)
        target = tab->GetSelectedNode();
    if (!target)
        return;

    // The nested-doc node is the instantiated template link: navigation plus
    // removal (the generic position/copy machinery does not apply to it).
    if (target->IsNestedDoc())
    {
        if (ui::MenuItem(ICON_FA_ARROW_UP_RIGHT_FROM_SQUARE " Open Nested Document"))
            OpenNestedDocumentFile(context_, doc, target->nestedDocHref_, /*revealOnly=*/false);
        if (ui::MenuItem(ICON_FA_TRASH " Delete Link"))
        {
            tab->SetSelectedNode(target);
            doc->DeleteNode(target); // routed to the text-level head removal
        }
        contextMenuTargetValid_ = false;
        return;
    }

    // A #head-link node stands for <head> bytes: the position/copy machinery
    // does not apply, only navigation (template links) and removal do.
    if (target->IsHeadLink())
    {
        if (target->GetAttribute("type") == "text/template")
        {
            if (ui::MenuItem(ICON_FA_ARROW_UP_RIGHT_FROM_SQUARE " Open Linked Document"))
                OpenNestedDocumentFile(context_, doc, target->GetAttribute("href"), /*revealOnly=*/false);
        }
        if (ui::MenuItem(ICON_FA_TRASH " Delete Link"))
        {
            tab->SetSelectedNode(target);
            doc->DeleteNode(target); // routed to the text-level head removal
        }
        contextMenuTargetValid_ = false;
        return;
    }

    if (target != doc->GetModel().root_.Get())
    {
        if (ui::MenuItem(ICON_FA_TRASH " Delete"))
        {
            tab->SetSelectedNode(target);
            doc->DeleteNode(target);
        }
    }
    contextMenuTargetValid_ = false;
}

// ---------------------------------------------------------------------------
// UIViewInspector: edits the selected model node
// ---------------------------------------------------------------------------

UIViewInspector::UIViewInspector(UIViewTab* owner)
    : Object(owner->GetContext())
    , owner_(owner)
{
}

void UIViewInspector::RenderContent()
{
    UIViewTab* tab = owner_;

    // Document-level editing applies with or without a selection, so it sits
    // above the per-node editors.
    RenderHeadLinks();
    ui::Separator();

    UiNode* node = tab ? tab->GetSelectedNode() : nullptr;
    if (!node)
    {
        ui::TextDisabled("Select an element to edit its attributes.");
        return;
    }

    // The nested-doc virtual node has no authored source in this document:
    // show the navigation panel instead of the attribute/style editors.
    if (node->IsNestedDoc())
    {
        RenderNestedDoc(node);
        return;
    }

    // A #head-link node edits its <head> bytes through the text-level link
    // commands; the generic attribute/style editors below do not apply.
    if (node->IsHeadLink())
    {
        RenderHeadLink(node);
        return;
    }

    ea::string header = node->tag_;
    if (!node->id_.empty())
        header += "#" + node->id_;
    UIViewDocument* doc = tab ? tab->GetDocument() : nullptr;
    if (doc && node == doc->GetModel().root_.Get())
    {
        // The document root is a real element (inline style on <body> is worth
        // editing), but it must never read as just another widget panel: edits
        // meant for a selected widget have landed on <body> unnoticed.
        ui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
            ICON_FA_TRIANGLE_EXCLAMATION " Document Root (%s)", header.c_str());
    }
    else
        ui::Text(ICON_FA_HAND_POINTER " %s", header.c_str());
    // The file this element's source lives in (matters with several documents
    // open and for template-minted areas, whose source lives elsewhere).
    if (doc)
        ui::TextDisabled(ICON_FA_FILE " %s", doc->GetSourcePath().c_str());
    ui::Separator();

    // A committed edit rebuilds the model tree and dangles `node`; stop
    // rendering for this frame instead of touching freed memory below.
    if (RenderTextContent(node))
        return;
    if (RenderAttributes(node))
        return;
    ui::Separator();
    if (RenderLayout(node))
        return;
    if (RenderAppearance(node))
        return;
    if (RenderInlineStyle(node))
        return;
    ui::Separator();
    RenderTemplates(node);
    ui::Separator();
    RenderComputed(node);
}

void UIViewInspector::RenderHeadLinks()
{
    UIViewTab* tab = owner_;
    UIViewDocument* doc = tab ? tab->GetDocument() : nullptr;

    ui::SeparatorText(ICON_FA_LINK " Head Links");
    if (!doc || !doc->GetRmlDocument())
    {
        ui::TextDisabled("(no document open)");
        return;
    }

    // The links themselves are #head-link nodes at the top of the Hierarchy
    // (edit each there); this row is only the spigot for one more <link>.
    ui::PushItemWidth(-40.0f);
    const bool committed = ui::InputTextWithHint("##headLinkHref", "/UI/default.rcss",
        headLinkHrefBuf_, sizeof(headLinkHrefBuf_), ImGuiInputTextFlags_EnterReturnsTrue);
    ui::PopItemWidth();
    // Pick an existing file instead of typing its path; either kind can be
    // chosen, so the filter lists both and the + buttons below record which.
    if (auto picked = ResourceBrowseWidget("##browse", context_, "", "rcss,rml"))
        snprintf(headLinkHrefBuf_, sizeof(headLinkHrefBuf_), "%s", picked->c_str());
    const bool wantCss = committed || ui::Button(ICON_FA_PLUS " Stylesheet (.rcss)");
    ui::SameLine();
    const bool wantRml = ui::Button(ICON_FA_PLUS " Template (.rml)");
    if (wantCss || wantRml)
    {
        // Enter in the field adds the common case: a stylesheet link.
        const ea::string href = Trim(ea::string(headLinkHrefBuf_));
        if (!href.empty() && doc->AddHeadLink(wantCss ? "text/rcss" : "text/template", href))
            headLinkHrefBuf_[0] = '\0';
    }
}

void UIViewInspector::InvalidateCaches()
{
    // Whole-tree rebuilds (every command / undo) replace all nodes; the cached
    // inline-style text must be reseeded from the new node on the next render.
    styleSeedValid_ = false;
}

void UIViewInspector::RenderNestedDoc(UiNode* node)
{
    UIViewTab* tab = owner_;
    UIViewDocument* doc = tab ? tab->GetDocument() : nullptr;
    if (!doc)
        return;

    const ea::string resPath = ResolveRelativeResourcePath(doc->GetSourcePath(), node->nestedDocHref_);
    const bool exists = !resPath.empty() && ResourceExists(context_, resPath);

    ui::Text(ICON_FA_CUBES " Nested Document");
    ui::TextDisabled(ICON_FA_FILE " %s", resPath.c_str());
    ui::Separator();

    ui::TextWrapped(
        "The window frame of this document (title bar, close button, ...) is\n"
        "minted from this template file. Open it to edit the frame itself.");
    ui::Spacing();

    // The <link type="text/template"> href this node stands for: editing it
    // repoints the window frame at another template file.
    char hrefBuf[512];
    snprintf(hrefBuf, sizeof(hrefBuf), "%s", node->nestedDocHref_.c_str());
    ui::PushItemWidth(-40.0f);
    const bool hrefEdited = ui::InputText("href", hrefBuf, sizeof(hrefBuf), ImGuiInputTextFlags_EnterReturnsTrue);
    ui::PopItemWidth();
    // This link always names a template, so the picker lists .rml only.
    const auto pickedHref = ResourceBrowseWidget("##browse", context_, node->nestedDocHref_, "rml");
    if (hrefEdited || pickedHref)
    {
        const ea::string newHref = hrefEdited ? Trim(ea::string(hrefBuf)) : *pickedHref;
        if (!newHref.empty())
        {
            doc->EditHeadLink(node, "text/template", newHref);
            return; // the model was rebuilt; re-render from the new node
        }
    }

    ui::BeginDisabled(!exists);
    if (ui::Button(ICON_FA_MAGNIFYING_GLASS " Reveal in Resource Browser"))
        OpenNestedDocumentFile(context_, doc, node->nestedDocHref_, /*revealOnly=*/true);
    ui::SameLine();
    if (ui::Button(ICON_FA_ARROW_UP_RIGHT_FROM_SQUARE " Open"))
        OpenNestedDocumentFile(context_, doc, node->nestedDocHref_, /*revealOnly=*/false);
    ui::EndDisabled();
    if (!exists)
        ui::TextDisabled("(nested file not found)");

    ui::Spacing();
    if (ui::Button(ICON_FA_TRASH " Delete Link"))
    {
        doc->DeleteNode(node); // routed to the text-level head removal
        return; // the model was rebuilt
    }
}

void UIViewInspector::RenderHeadLink(UiNode* node)
{
    UIViewTab* tab = owner_;
    UIViewDocument* doc = tab ? tab->GetDocument() : nullptr;
    if (!doc)
        return;

    const ea::string type = node->GetAttribute("type");
    const ea::string href = node->GetAttribute("href");
    const bool isTemplate = type == "text/template";

    ui::Text(ICON_FA_LINK " %s", isTemplate ? "RML Template Link" : "Stylesheet Link");
    ui::TextDisabled(ICON_FA_FILE " %s", doc->GetSourcePath().c_str());
    ui::Separator();

    const ea::string resPath = ResolveRelativeResourcePath(doc->GetSourcePath(), href);
    const bool exists = !resPath.empty() && ResourceExists(context_, resPath);

    // The two link kinds RmlUi supports differ in what the href points at: a
    // stylesheet dresses the widgets, a template provides the window chrome
    // that <body template="..."> instantiates. Switching rewrites the type
    // attribute of the same <link> element.
    static const char* kLinkTypes[] = {"text/rcss", "text/template"};
    int current = -1;
    if (type == "text/rcss")
        current = 0;
    else if (isTemplate)
        current = 1;
    int pendingType = -1;
    if (ui::BeginCombo("type", current >= 0 ? kLinkTypes[current]
        : (type.empty() ? "(no type)" : type.c_str())))
    {
        for (int i = 0; i < 2; i++)
        {
            const bool isSelected = (current == i);
            if (ui::Selectable(i == 0 ? "Stylesheet (.rcss)" : "RML Template (.rml)", isSelected)
                && !isSelected)
                pendingType = i;
        }
        ui::EndCombo();
    }
    if (pendingType >= 0)
    {
        doc->EditHeadLink(node, kLinkTypes[pendingType], href);
        return; // the model was rebuilt; re-render from the new node
    }

    // Resource path, resolved like RmlUi resolves it (a leading '/' is
    // rooted at the project's Data directory).
    char hrefBuf[512];
    snprintf(hrefBuf, sizeof(hrefBuf), "%s", href.c_str());
    ui::PushItemWidth(-40.0f);
    const bool hrefEdited = ui::InputText("href", hrefBuf, sizeof(hrefBuf), ImGuiInputTextFlags_EnterReturnsTrue);
    ui::PopItemWidth();
    // A picker offers the same Data-rooted spelling the loader expects,
    // narrowed to the kind this link claims to be (stylesheet vs template).
    const auto pickedHref = ResourceBrowseWidget("##browse", context_, href, isTemplate ? "rml" : "rcss");
    if (hrefEdited || pickedHref)
    {
        const ea::string newHref = hrefEdited ? Trim(ea::string(hrefBuf)) : *pickedHref;
        if (!newHref.empty())
        {
            doc->EditHeadLink(node, type, newHref);
            return; // the model was rebuilt; re-render from the new node
        }
    }

    ui::Spacing();
    if (isTemplate)
        ui::TextWrapped("The window frame (title bar, close button, ...) is minted from "
            "this template file when <body template=\"...\"> names it.");
    else
        ui::TextWrapped("The linked stylesheet provides the looks of this document's "
            "widgets; rules authored below still override it.");

    ui::BeginDisabled(!exists);
    if (ui::Button(ICON_FA_MAGNIFYING_GLASS " Reveal in Resource Browser"))
        OpenNestedDocumentFile(context_, doc, href, /*revealOnly=*/true);
    if (isTemplate)
    {
        ui::SameLine();
        if (ui::Button(ICON_FA_ARROW_UP_RIGHT_FROM_SQUARE " Open"))
            OpenNestedDocumentFile(context_, doc, href, /*revealOnly=*/false);
    }
    ui::EndDisabled();
    if (!exists)
        ui::TextDisabled("(linked file not found)");

    ui::Spacing();
    if (ui::Button(ICON_FA_TRASH " Delete Link"))
    {
        doc->DeleteNode(node); // routed to the text-level head removal
        return; // the model was rebuilt
    }
}

void UIViewInspector::RenderTemplates(UiNode* node)
{
    UIViewTab* tab = owner_;
    UIViewDocument* doc = tab ? tab->GetDocument() : nullptr;
    if (!doc)
        return;

    // Template-minted elements (window frames, title bars, close buttons) have
    // no source in THIS document - they are instantiated from the nested .rml
    // files referenced by <head> <link type="text/template">. Surface those
    // file paths here, with a jump-in button, so editing them is one click
    // instead of a resource-browser hunt.
    const ea::vector<ea::string> links = doc->GetModel().GetTemplateLinks();
    if (links.empty())
        return;

    if (!ui::CollapsingHeader(ICON_FA_CUBES " Nested Documents", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    ui::TextDisabled(
        "Elements minted by a template (window frame, title bar, close button)\n"
        "live in the template file, not in this document.");

    for (const ea::string& href : links)
    {
        const ea::string resPath = ResolveRelativeResourcePath(doc->GetSourcePath(), href);
        const bool exists = !resPath.empty() && ResourceExists(context_, resPath);

        ui::TextUnformatted(href.c_str());
        ui::SameLine();
        ui::TextDisabled("-> %s", resPath.c_str());
        ui::SameLine();
        ui::BeginDisabled(!exists);
        if (ui::SmallButton(ICON_FA_ARROW_UP_RIGHT_FROM_SQUARE " Open"))
        {
            // Multi-document: opens the template alongside this document
            // (no graceful close of the current workspace).
            auto* project = context_->GetSubsystem<Project>();
            if (project)
                project->ProcessRequest(MakeShared<OpenResourceRequest>(context_, resPath).Get());
        }
        ui::EndDisabled();
    }
}

bool UIViewInspector::RenderTextContent(UiNode* node)
{
    UIViewTab* tab = owner_;
    if (!tab || !tab->GetDocument())
        return false;

    // The visible text of a label/button lives on a #text model node. Expose a
    // single editable field when the selection is itself a text node, or an
    // element whose only child is a text node (the "Text"/"Button" widgets and
    // plain <div>text</div> all take this shape). Mixed containers are skipped.
    UiNode* textNode = nullptr;
    if (node->IsText())
        textNode = node;
    else if (node->children_.size() == 1 && node->children_[0]->IsText())
        textNode = node->children_[0].Get();
    if (!textNode)
        return false;

    if (!ui::CollapsingHeader(ICON_FA_FONT " Content", ImGuiTreeNodeFlags_DefaultOpen))
        return false;

    // Same seed-per-frame + commit-on-deactivate model as the id/class rows:
    // ImGui keeps its own edit buffer while focused, so re-seeding is safe.
    char textBuf[1024];
    snprintf(textBuf, sizeof(textBuf), "%s", textNode->text_.c_str());
    ui::PushItemWidth(-1.0f);
    ui::InputText("##textContent", textBuf, sizeof(textBuf), ImGuiInputTextFlags_EnterReturnsTrue);
    ui::PopItemWidth();
    if (ui::IsItemDeactivatedAfterEdit())
    {
        UiNodePayload payload = SnapshotUiNodePayload(*textNode);
        payload.text_ = ea::string(textBuf);
        tab->GetDocument()->EditNodePayload(textNode, payload);
        return true; // the model was rebuilt; the node is dangling now
    }
    return false;
}

// ---------------------------------------------------------------------------
// Element attributes the engine itself reads
// ---------------------------------------------------------------------------
// The Inspector renders these as fixed rows, visible whether or not the
// document happens to carry them: an attribute that exists but has no row is
// invisible, and an invented attribute with an engine-looking name reads as a
// control that silently does nothing. Every row below is backed by the source
// line that reads it (RmlUi Source/Core/Elements/*).
//
// Deliberately absent - things this RmlUi build does not read: input
// placeholder/size/focus/autofocus (placeholder does not exist in the library
// at all), form action/method/enctype (ElementForm reads no attribute
// whatsoever), button disabled/name (it is not a control: CoreData/UI/
// layout.rcss styles button[disabled] but the engine never tests it), and the
// img width/height attributes, which would fight the style declarations of the
// same name. Also absent: data-* and on* - those are the binding and event
// surfaces and get designed as a unit elsewhere.
enum class AttrKind
{
    Text, ///< free-form string
    Decimal, ///< number; the engine clamps, this is only a typed-input filter
    Flag, ///< presence-only: ticked writes the attribute, unticked removes it
    Enum, ///< one of a fixed keyword set
    IdRef, ///< one of the ids present in the document (label's for)
};

struct AttrRow
{
    const char* tag;
    const char* type; ///< input only: the type it applies to; null = any type
    const char* name;
    AttrKind kind;
    const char* const* keywords; ///< Enum only, null-terminated
    const char* browseFilter; ///< non-null: file-picker button narrowed by this NFD spec ("png,jpg")
};

// Raster formats RmlUi's image decoder can load through the engine's Image.
const char* const kImageFilter = "png,jpg,jpeg,tga,bmp,webp,gif,dds";

const char* const kInputTypes[] = { "text", "password", "checkbox", "radio", "range", "submit", "button", nullptr };
const char* const kTextAreaWrap[] = { "normal", "free", nullptr };
const char* const kProgressDirections[] = { "left", "up", nullptr };

const AttrRow kAttrRows[] = {
    // src is a file under Data, so it gets a picker. sprite is NOT a path: the
    // engine resolves it as a name against sprites declared in loaded RCSS
    // (style_sheet->GetSprite), and rect is four numbers - neither is a file.
    { "img", nullptr, "src", AttrKind::Text, nullptr, kImageFilter },
    { "img", nullptr, "sprite", AttrKind::Text, nullptr },
    { "img", nullptr, "rect", AttrKind::Text, nullptr },

    { "input", nullptr, "type", AttrKind::Enum, kInputTypes },
    { "input", "text", "value", AttrKind::Text, nullptr },
    { "input", "text", "name", AttrKind::Text, nullptr },
    { "input", "text", "disabled", AttrKind::Flag, nullptr },
    { "input", "password", "value", AttrKind::Text, nullptr },
    { "input", "password", "name", AttrKind::Text, nullptr },
    { "input", "password", "disabled", AttrKind::Flag, nullptr },
    // checked is presence-only in the engine (InputTypeCheckbox tests
    // HasAttribute), so a hand-written checked="false" means CHECKED. That is
    // why this row is a tick box and never writes a literal true/false.
    { "input", "checkbox", "checked", AttrKind::Flag, nullptr },
    { "input", "checkbox", "value", AttrKind::Text, nullptr },
    { "input", "checkbox", "name", AttrKind::Text, nullptr },
    { "input", "checkbox", "disabled", AttrKind::Flag, nullptr },
    // name is what groups radios; without it each one stands alone.
    { "input", "radio", "checked", AttrKind::Flag, nullptr },
    { "input", "radio", "name", AttrKind::Text, nullptr },
    { "input", "radio", "value", AttrKind::Text, nullptr },
    { "input", "radio", "disabled", AttrKind::Flag, nullptr },
    { "input", "range", "value", AttrKind::Decimal, nullptr },
    { "input", "range", "min", AttrKind::Decimal, nullptr },
    { "input", "range", "max", AttrKind::Decimal, nullptr },
    { "input", "range", "step", AttrKind::Decimal, nullptr },
    { "input", "range", "disabled", AttrKind::Flag, nullptr },
    // submit/button: value is submitted with the form but never drawn (an
    // input of these types has no render path) - the visible label is a text
    // child, which the Text Content section edits.
    { "input", "submit", "name", AttrKind::Text, nullptr },
    { "input", "submit", "value", AttrKind::Text, nullptr },
    { "input", "submit", "disabled", AttrKind::Flag, nullptr },
    { "input", "button", "name", AttrKind::Text, nullptr },
    { "input", "button", "value", AttrKind::Text, nullptr },
    { "input", "button", "disabled", AttrKind::Flag, nullptr },

    { "select", nullptr, "value", AttrKind::Text, nullptr },
    { "select", nullptr, "name", AttrKind::Text, nullptr },
    { "select", nullptr, "disabled", AttrKind::Flag, nullptr },

    { "textarea", nullptr, "value", AttrKind::Text, nullptr },
    { "textarea", nullptr, "rows", AttrKind::Decimal, nullptr },
    { "textarea", nullptr, "cols", AttrKind::Decimal, nullptr },
    { "textarea", nullptr, "wrap", AttrKind::Enum, kTextAreaWrap },
    { "textarea", nullptr, "maxlength", AttrKind::Decimal, nullptr },
    { "textarea", nullptr, "name", AttrKind::Text, nullptr },
    { "textarea", nullptr, "disabled", AttrKind::Flag, nullptr },

    { "progress", nullptr, "value", AttrKind::Decimal, nullptr },
    { "progress", nullptr, "max", AttrKind::Decimal, nullptr },
    { "progress", nullptr, "direction", AttrKind::Enum, kProgressDirections },

    { "label", nullptr, "for", AttrKind::IdRef, nullptr },
};

constexpr size_t kNoAttr = static_cast<size_t>(-1);

size_t FindAttrIndex(const ea::vector<ea::pair<ea::string, ea::string>>& attributes, const ea::string& name)
{
    for (size_t i = 0; i < attributes.size(); ++i)
    {
        if (attributes[i].first == name)
            return i;
    }
    return kNoAttr;
}

void SetPayloadAttr(UiNodePayload& payload, const char* name, const ea::string& value)
{
    for (auto& attribute : payload.attributes_)
    {
        if (attribute.first == name)
        {
            attribute.second = value;
            return;
        }
    }
    payload.attributes_.emplace_back(name, value);
    // Keep the model's documented invariant (attributes sorted by name); this
    // only orders the payload vector, never the source text - a new attribute
    // is patched onto the element's own open tag wherever it sorts.
    ea::sort(payload.attributes_.begin(), payload.attributes_.end(),
        [](const ea::pair<ea::string, ea::string>& a, const ea::pair<ea::string, ea::string>& b)
        { return a.first < b.first; });
}

void DropPayloadAttr(UiNodePayload& payload, const ea::string& name)
{
    const size_t at = FindAttrIndex(payload.attributes_, name);
    if (at != kNoAttr)
        payload.attributes_.erase(payload.attributes_.begin() + at);
}

// The type an <input> presents to the engine: an absent attribute means text.
ea::string InputTypeOf(const UiNode& node)
{
    if (node.tag_ != "input")
        return ea::string();
    const size_t at = FindAttrIndex(node.attributes_, "type");
    const ea::string type = at != kNoAttr ? node.attributes_[at].second : ea::string("text");
    return LowerCopy(type);
}

void CollectIds(const UiNode& node, ea::vector<ea::string>& out)
{
    if (!node.id_.empty())
        out.push_back(node.id_);
    for (const SharedPtr<UiNode>& child : node.children_)
    {
        if (child)
            CollectIds(*child, out);
    }
}

bool UIViewInspector::RenderAttributes(UiNode* node)
{
    UIViewTab* tab = owner_;
    if (!ui::CollapsingHeader(ICON_FA_LIST " Attributes", ImGuiTreeNodeFlags_DefaultOpen))
        return false;

    // Edits accumulate into a payload COPY; the model node is only touched by
    // the undoable command, which snapshots the pristine "old" state itself.
    UiNodePayload payload = SnapshotUiNodePayload(*node);
    bool structural = false;

    // id / class are dedicated fields; editing them is structural (emitted).
    // Same row layout as the attribute rows below (name left, input right):
    // a bare InputText("id") would put ImGui's label after the field and the
    // section would read as two misaligned tables.
    char idBuf[256];
    snprintf(idBuf, sizeof(idBuf), "%s", node->id_.c_str());
    ui::Text("id");
    ui::SameLine();
    ui::PushItemWidth(-40.0f);
    ui::InputText("##id", idBuf, sizeof(idBuf), ImGuiInputTextFlags_EnterReturnsTrue);
    ui::PopItemWidth();
    if (ui::IsItemDeactivatedAfterEdit())
    {
        payload.id_ = Trim(idBuf);
        structural = true;
    }
    char classBuf[256];
    snprintf(classBuf, sizeof(classBuf), "%s", node->classes_.c_str());
    ui::Text("class");
    ui::SameLine();
    ui::PushItemWidth(-40.0f);
    ui::InputText("##class", classBuf, sizeof(classBuf), ImGuiInputTextFlags_EnterReturnsTrue);
    ui::PopItemWidth();
    if (ui::IsItemDeactivatedAfterEdit())
    {
        payload.classes_ = Trim(classBuf);
        structural = true;
    }

    // Structured rows the engine understands for this element, rendered whether
    // or not the document carries them: an attribute that exists but has no row
    // is invisible, and a row that only appears once the attribute exists makes
    // the affordance depend on how the element was born. Typing a value writes
    // the attribute and clearing the field drops it, so panel and text stay in
    // step whichever was edited first. A presence flag (checked/disabled) is a
    // tick box: the engine only tests that the attribute is there, so it is
    // written bare ("") and never as ="true"/"false" - the latter would flip a
    // checkbox's meaning, since checked="false" still counts as present.
    const ea::string inputType = InputTypeOf(*node);
    ea::vector<ea::string> rootIds;
    if (tab && tab->GetDocument() && tab->GetDocument()->GetModel().root_)
        CollectIds(*tab->GetDocument()->GetModel().root_, rootIds);

    ea::vector<ea::string> covered;
    for (const AttrRow& row : kAttrRows)
    {
        if (node->tag_ != row.tag || (row.type && inputType != row.type))
            continue;
        covered.push_back(row.name);

        const size_t at = FindAttrIndex(payload.attributes_, row.name);
        const bool present = at != kNoAttr;
        const ea::string value = present ? payload.attributes_[at].second : ea::string();

        ui::PushID(row.name);
        ui::Text("%s", row.name);
        ui::SameLine();
        switch (row.kind)
        {
        case AttrKind::Flag:
        {
            bool on = present; // presence is the entire state
            if (ui::Checkbox("##flag", &on))
            {
                if (on)
                    SetPayloadAttr(payload, row.name, ea::string());
                else
                    DropPayloadAttr(payload, row.name);
                structural = true;
            }
            break;
        }

        case AttrKind::Enum:
        case AttrKind::IdRef:
        {
            // Slot 0 lets the engine's own default stand by dropping the
            // attribute; the rest are the fixed keywords, or every id in the
            // document for a reference (label's for).
            ea::vector<ea::string> items;
            items.push_back(row.kind == AttrKind::IdRef ? ea::string("(none)") : ea::string("(default)"));
            if (row.kind == AttrKind::Enum)
            {
                for (const char* const* kw = row.keywords; kw && *kw; ++kw)
                    items.push_back(ea::string(*kw));
            }
            else
            {
                for (const ea::string& id : rootIds)
                    items.push_back(id);
            }
            int current = 0;
            if (present)
            {
                for (int i = 1; i < static_cast<int>(items.size()); ++i)
                {
                    if (items[i] == value)
                    {
                        current = i;
                        break;
                    }
                }
            }
            // A hand-written value outside the list still previews verbatim, so
            // the row never lies about what the text carries.
            const ea::string preview = present ? value : items[0];
            ui::PushItemWidth(-40.0f);
            if (ui::BeginCombo("##value", preview.c_str()))
            {
                for (int i = 0; i < static_cast<int>(items.size()); ++i)
                {
                    if (ui::Selectable(items[i].c_str(), i == current))
                    {
                        if (i == 0)
                        {
                            if (present)
                                DropPayloadAttr(payload, row.name);
                        }
                        else
                        {
                            SetPayloadAttr(payload, row.name, items[i]);
                        }
                        structural = true;
                    }
                }
                ui::EndCombo();
            }
            ui::PopItemWidth();
            break;
        }

        default: // Text / Decimal: a free field, Decimal filtered to numbers
        {
            char valBuf[1024];
            snprintf(valBuf, sizeof(valBuf), "%s", value.c_str());
            const ImGuiInputTextFlags extra =
                row.kind == AttrKind::Decimal ? ImGuiInputTextFlags_CharsDecimal : static_cast<ImGuiInputTextFlags>(0);
            ui::PushItemWidth(-40.0f);
            if (ui::InputText("##value", valBuf, sizeof(valBuf), extra | ImGuiInputTextFlags_EnterReturnsTrue))
            {
                const ea::string next = Trim(valBuf);
                if (next.empty())
                {
                    if (present)
                    {
                        DropPayloadAttr(payload, row.name);
                        structural = true;
                    }
                }
                else if (!present || next != value)
                {
                    SetPayloadAttr(payload, row.name, next);
                    structural = true;
                }
            }
            ui::PopItemWidth();
            // A resource-path row gets a picker beside the free field. A pick
            // commits at once - it names a file that is really there, which a
            // half-typed path is not.
            if (row.browseFilter)
            {
                if (auto picked = ResourceBrowseWidget("##browse", context_, value, row.browseFilter))
                {
                    SetPayloadAttr(payload, row.name, *picked);
                    structural = true;
                }
            }
            break;
        }
        }
        ui::PopID();
    }

    // Attributes no structured row claims (data-*, on*, or anything a tag
    // carries outside kAttrRows): still editable and removable, so the panel
    // never silently drops text it does not understand. Located by name rather
    // than index, because the rows above may have reordered the payload.
    for (size_t i = 0; i < node->attributes_.size(); ++i)
    {
        const ea::string name = node->attributes_[i].first;
        bool handled = false;
        for (const ea::string& c : covered)
        {
            if (c == name)
            {
                handled = true;
                break;
            }
        }
        if (handled)
            continue;
        const size_t at = FindAttrIndex(payload.attributes_, name);
        if (at == kNoAttr)
            continue; // dropped earlier this frame
        char valBuf[1024];
        snprintf(valBuf, sizeof(valBuf), "%s", payload.attributes_[at].second.c_str());
        ui::PushID(name.c_str());
        ui::Text("%s", name.c_str());
        ui::SameLine();
        ui::PushItemWidth(-40.0f);
        if (ui::InputText("##value", valBuf, sizeof(valBuf), ImGuiInputTextFlags_EnterReturnsTrue))
        {
            payload.attributes_[at].second = valBuf;
            structural = true;
        }
        ui::PopItemWidth();
        ui::SameLine();
        if (ui::SmallButton(ICON_FA_TRASH))
        {
            DropPayloadAttr(payload, name);
            structural = true;
            ui::PopID();
            break; // re-snapshot next frame
        }
        ui::PopID();
    }

    // Add-new row.
    ui::PushID("__new__");
    ui::InputText("name##newAttrName", attributeKeyBuf_, sizeof(attributeKeyBuf_));
    ui::SameLine();
    ui::PushItemWidth(-40.0f);
    ui::InputText("##newAttrValue", attributeValueBuf_, sizeof(attributeValueBuf_));
    ui::PopItemWidth();
    ui::SameLine();
    if (ui::SmallButton(ICON_FA_PLUS) && !Trim(attributeKeyBuf_).empty())
    {
        payload.attributes_.emplace_back(Trim(attributeKeyBuf_), attributeValueBuf_);
        ea::sort(payload.attributes_.begin(), payload.attributes_.end(),
            [](const ea::pair<ea::string, ea::string>& a, const ea::pair<ea::string, ea::string>& b)
            { return a.first < b.first; });
        structural = true;
        attributeKeyBuf_[0] = '\0';
        attributeValueBuf_[0] = '\0';
    }
    ui::PopID();

    if (structural && tab && tab->GetDocument())
    {
        tab->GetDocument()->EditNodePayload(node, payload);
        return true; // the model was rebuilt; the node is dangling now
    }
    return false;
}

// ---------------------------------------------------------------------------
// Inline style (RCSS) declarations the engine recognises
// ---------------------------------------------------------------------------
// Same discipline as kAttrRows, but this writes the style="..." channel
// (node->style_), never element attributes - the two do not cross (a src:
// typed here is a declaration for which RCSS has no property, and the engine
// drops it in silence). Every row is a property actually registered in RmlUi's
// StyleSheetSpecification, carrying its real default (shown as the field
// placeholder, never written down) and its inheritance flag. Values a row
// cannot represent - !important, or functional values like transform /
// decorator / animation, which get no row at all - are left to the raw editor
// below; a structured row only ever adds or removes the one declaration it
// edits, never disturbing the authored order or its siblings.
enum class StyleKind
{
    Keyword, ///< one of a fixed keyword set -> combo
    Length,  ///< <length>/<percentage> (or auto/none) -> free field
    Number,  ///< bare number -> number-filtered field
    Color,   ///< <color> -> free field
    Text,    ///< free string (font-family, cursor) -> free field
};

// The structured panel is an opinionated subset, not a CSS browser: only the
// few look properties worth a knob on every element get a row here (the layout
// kit has its own bespoke controls, see RenderLayout). Everything else RmlUi
// registers - borders, radius, the long tail of typography, box sizing, min/max,
// the rest of flex, ... - is left to the raw editor below; a structured row only
// ever adds or removes the one declaration it edits, never disturbing the
// authored order or its siblings.
struct StyleRow
{
    const char* name;
    const char* def;      ///< engine default: the placeholder; "" = no default text
    bool inherited;       ///< cascades to descendants
    StyleKind kind;
    const char* keywords; ///< Keyword only: ", "-separated, exactly as registered
};

const StyleRow kAppearanceRows[] = {
    { "background-color", "transparent", false, StyleKind::Color, nullptr },
    { "opacity", "1", true, StyleKind::Number, nullptr },
    { "color", "white", true, StyleKind::Color, nullptr },
    { "font-size", "12px", true, StyleKind::Length, nullptr },
};

int FindStyleIndexIn(const ea::vector<UiStyleDecl>& decls, const ea::string& name)
{
    for (int i = 0; i < static_cast<int>(decls.size()); ++i)
    {
        if (decls[i].name_ == name)
            return i;
    }
    return -1;
}

// Order-preserving, single-declaration edits (mirror UiNode::SetStyle/
// RemoveStyle but on the payload copy), so a structured row never disturbs the
// authored order or the declarations it does not own.
void SetPayloadStyle(UiNodePayload& payload, const ea::string& name, const ea::string& value)
{
    const int at = FindStyleIndexIn(payload.style_, name);
    if (at >= 0)
        payload.style_[at].value_ = value;
    else
        payload.style_.push_back(UiStyleDecl{name, value});
}

void DropPayloadStyle(UiNodePayload& payload, const ea::string& name)
{
    const int at = FindStyleIndexIn(payload.style_, name);
    if (at >= 0)
        payload.style_.erase(payload.style_.begin() + at);
}

// Split a registered keyword list (", "-separated, straight from the source)
// into trimmed items. Hand-rolled rather than via a split() so it depends on
// nothing but find/substr.
void SplitKeywords(const char* text, ea::vector<ea::string>& out)
{
    const ea::string s(text);
    size_t begin = 0;
    while (begin <= s.length())
    {
        const size_t comma = s.find(',', begin);
        size_t b = begin;
        size_t e = comma == ea::string::npos ? s.length() : comma;
        while (b < e && (s[b] == ' ' || s[b] == '\t'))
            ++b;
        while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t'))
            --e;
        if (e > b)
            out.push_back(s.substr(b, e - b));
        if (comma == ea::string::npos)
            break;
        begin = comma + 1;
    }
}

// display value that turns an element into a flex container / makes its children
// flex items (compared against an already lowercased, trimmed string).
bool IsFlexValue(const ea::string& display)
{
    return display == "flex" || display == "inline-flex";
}

// Render one structured style row (label + its control) against the payload
// copy. Keyword rows offer "(default)" as slot 0, which drops the declaration
// so the engine default stands; a hand-written value outside the list still
// previews verbatim so the row never lies. Length/Number/Color/Text rows are
// free fields (Number is digit-filtered); clearing one drops its declaration
// rather than writing the default. Nothing commits here - callers accumulate
// into one payload and EditNodePayload once (commit-then-return).
void RenderOneStyleRow(const StyleRow& row, UiNodePayload& payload, bool& structural)
{
    const int at = FindStyleIndexIn(payload.style_, row.name);
    const bool present = at >= 0;
    const ea::string value = present ? payload.style_[at].value_ : ea::string();

    ui::PushID(row.name);
    // Surface inheritance: a color or font on a container is the engine feeding
    // its whole subtree, not a per-node quirk.
    ui::Text("%s%s", row.name, row.inherited ? "  *" : "");
    if (row.inherited && ui::IsItemHovered())
        ui::SetTooltip("Inherited: descendants take this value unless they override it.");
    ui::SameLine();

    if (row.kind == StyleKind::Keyword)
    {
        ea::vector<ea::string> items;
        items.push_back("(default)"); // drops the declaration, engine default stands
        SplitKeywords(row.keywords, items);
        int current = 0;
        if (present)
        {
            for (int i = 1; i < static_cast<int>(items.size()); ++i)
            {
                if (items[i] == Trim(value))
                {
                    current = i;
                    break;
                }
            }
        }
        const ea::string preview = present ? value
            : (row.def[0] != '\0' ? ea::string(row.def) : ea::string("(default)"));
        ui::PushItemWidth(-8.0f);
        if (ui::BeginCombo("##v", preview.c_str()))
        {
            for (int i = 0; i < static_cast<int>(items.size()); ++i)
            {
                if (ui::Selectable(items[i].c_str(), i == current))
                {
                    if (i == 0)
                    {
                        if (present)
                            DropPayloadStyle(payload, row.name);
                    }
                    else
                    {
                        SetPayloadStyle(payload, row.name, items[i]);
                    }
                    structural = true;
                }
            }
            ui::EndCombo();
        }
        ui::PopItemWidth();
    }
    else
    {
        char valBuf[256];
        snprintf(valBuf, sizeof(valBuf), "%s", value.c_str());
        const ImGuiInputTextFlags extra =
            row.kind == StyleKind::Number ? ImGuiInputTextFlags_CharsDecimal : static_cast<ImGuiInputTextFlags>(0);
        ui::PushItemWidth(-8.0f);
        if (ui::InputTextWithHint("##v", row.def[0] != '\0' ? row.def : "(none)",
                valBuf, sizeof(valBuf), extra | ImGuiInputTextFlags_EnterReturnsTrue))
        {
            const ea::string next = Trim(valBuf);
            if (next.empty())
            {
                if (present)
                {
                    DropPayloadStyle(payload, row.name);
                    structural = true;
                }
            }
            else if (!present || next != value)
            {
                SetPayloadStyle(payload, row.name, next);
                structural = true;
            }
        }
        ui::PopItemWidth();
    }
    ui::PopID();
}

// A combo bound to a single style property whose raw CSS values are mapped to
// friendlier display labels (the auto-layout kit's align pickers). Slot 0 is
// "(default)": choosing it drops the declaration so the engine default stands.
void LayoutCombo(UiNodePayload& payload, bool& structural, const char* label,
    const char* name, const char* const* displayLabels, const char* const* cssValues,
    int count, const char* defaultText)
{
    const int at = FindStyleIndexIn(payload.style_, name);
    const bool present = at >= 0;
    const ea::string value = present ? Trim(payload.style_[at].value_) : ea::string();

    int current = 0; // 0 == (default); n+1 == cssValues[n]
    if (present)
    {
        current = -1;
        for (int i = 0; i < count; ++i)
        {
            if (value == cssValues[i])
            {
                current = i + 1;
                break;
            }
        }
    }
    const ea::string preview = present ? value
        : (defaultText && defaultText[0] ? ea::string(defaultText) : ea::string("(default)"));

    ui::PushID(label);
    ui::TextUnformatted(label);
    ui::SameLine();
    ui::PushItemWidth(-8.0f);
    if (ui::BeginCombo("##v", preview.c_str()))
    {
        if (ui::Selectable("(default)", current == 0))
        {
            if (present)
                DropPayloadStyle(payload, name);
            structural = true;
        }
        for (int i = 0; i < count; ++i)
        {
            if (ui::Selectable(displayLabels[i], current == i + 1))
            {
                SetPayloadStyle(payload, name, cssValues[i]);
                structural = true;
            }
        }
        ui::EndCombo();
    }
    ui::PopItemWidth();
    ui::PopID();
}

// One free length/keyword field (width / height). Text form, so "200px", "50%",
// "auto" all pass through untouched; clearing it drops the declaration.
void LayoutSizeField(UiNodePayload& payload, bool& structural, const char* label,
    const char* name, const char* defaultText)
{
    const int at = FindStyleIndexIn(payload.style_, name);
    const bool present = at >= 0;
    const ea::string value = present ? payload.style_[at].value_ : ea::string();

    char buf[64];
    snprintf(buf, sizeof(buf), "%s", value.c_str());
    ui::PushID(label);
    ui::TextUnformatted(label);
    ui::SameLine();
    ui::PushItemWidth(-8.0f);
    if (ui::InputTextWithHint("##v", defaultText, buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue))
    {
        const ea::string next = Trim(buf);
        if (next.empty())
        {
            if (present)
            {
                DropPayloadStyle(payload, name);
                structural = true;
            }
        }
        else if (!present || next != value)
        {
            SetPayloadStyle(payload, name, next);
            structural = true;
        }
    }
    ui::PopItemWidth();
    ui::PopID();
}

// Which edge(s) an absolutely-positioned element measures from, on one axis.
// The anchor grid is the cross product of a horizontal and a vertical mode, so
// the highlighted cell always matches the authored top/right/bottom/left +
// width/height exactly - there is no "custom" state it could misreport.
enum AnchorAxis
{
    AnchorNear,    // pinned to the leading edge (top / left); size as authored
    AnchorStretch, // both edges pinned and size auto: resizes with the container
    AnchorFar,     // pinned to the trailing edge (bottom / right)
};

// The size declaration counts as "authored" only if present and not auto.
bool IsSizedAxis(const UiNodePayload& payload, const char* name)
{
    const int at = FindStyleIndexIn(payload.style_, name);
    return at >= 0 && Trim(payload.style_[at].value_) != "auto";
}

bool HasInset(const UiNodePayload& payload, const char* name)
{
    return FindStyleIndexIn(payload.style_, name) >= 0;
}

void ResolveAnchorAxes(const UiNodePayload& payload, AnchorAxis& horiz, AnchorAxis& vert)
{
    const bool l = HasInset(payload, "left"), r = HasInset(payload, "right");
    const bool t = HasInset(payload, "top"), b = HasInset(payload, "bottom");
    horiz = (l && r && !IsSizedAxis(payload, "width")) ? AnchorStretch : (!l && r ? AnchorFar : AnchorNear);
    vert = (t && b && !IsSizedAxis(payload, "height")) ? AnchorStretch : (!t && b ? AnchorFar : AnchorNear);
}

// Pin an edge, seeding a flush 0 when it had no authored value yet (the offset
// field sits right there to nudge).
void PinAnchorEdge(UiNodePayload& payload, const char* inset)
{
    if (!HasInset(payload, inset))
        SetPayloadStyle(payload, inset, "0px");
}

// Rewrite one axis to match the chosen mode. Only insets and an auto size are
// authored - never margin or transform - so anchoring keeps a single code path
// and the grid, not the gizmo, owns non-top/left placement.
void ApplyAnchorAxis(UiNodePayload& payload, AnchorAxis axis,
    const char* nearEdge, const char* farEdge, const char* size)
{
    switch (axis)
    {
    case AnchorNear:
        PinAnchorEdge(payload, nearEdge);
        DropPayloadStyle(payload, farEdge);
        break;
    case AnchorFar:
        PinAnchorEdge(payload, farEdge);
        DropPayloadStyle(payload, nearEdge);
        break;
    case AnchorStretch:
        PinAnchorEdge(payload, nearEdge);
        PinAnchorEdge(payload, farEdge);
        DropPayloadStyle(payload, size); // auto size is what makes it stretch
        break;
    }
}

bool UIViewInspector::RenderAppearance(UiNode* node)
{
    UIViewTab* tab = owner_;
    if (!ui::CollapsingHeader(ICON_FA_PAINT_ROLLER " Appearance", ImGuiTreeNodeFlags_DefaultOpen))
        return false;

    UiNodePayload payload = SnapshotUiNodePayload(*node);
    bool structural = false;
    for (const StyleRow& row : kAppearanceRows)
        RenderOneStyleRow(row, payload, structural);

    if (structural && tab && tab->GetDocument())
    {
        // A structured commit changes style_; force the raw editor below to
        // reseed from the rebuilt model instead of showing its stale buffer.
        styleSeedValid_ = false;
        tab->GetDocument()->EditNodePayload(node, payload);
        return true; // the model was rebuilt; the node is dangling now
    }
    return false;
}

// The auto-layout kit: one opinionated, intent-named set of controls that each
// write the correct cluster of standard style declarations (a control can set or
// drop several). Everything is read back off the authored style_, so the panel
// never carries state the document does not. Long-tail properties are left raw.
bool UIViewInspector::RenderLayout(UiNode* node)
{
    UIViewTab* tab = owner_;
    if (!ui::CollapsingHeader(ICON_FA_TABLE_CELLS " Layout", ImGuiTreeNodeFlags_DefaultOpen))
        return false;

    UiNodePayload payload = SnapshotUiNodePayload(*node);
    bool structural = false;

    // Read current authored values off the payload copy so the controls reflect
    // what is set, independent of the model about to be rebuilt.
    auto cur = [&](const char* name) -> ea::string
    {
        const int at = FindStyleIndexIn(payload.style_, name);
        return at >= 0 ? Trim(payload.style_[at].value_) : ea::string();
    };

    // --- Positioning: per-element, written as the standard `position`
    // declaration. The gizmo keys off absolute; relative makes this the anchor
    // for absolutely-positioned descendants without leaving the flow. ---
    {
        const ea::string pos = LowerCopy(cur("position"));
        int mode = (pos == "absolute") ? 2 : (pos == "relative") ? 1 : 0;
        const char* modes[] = { "In flow", "Anchor", "Free (absolute)" };
        ui::Text(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT " Position");
        ui::SameLine();
        ui::PushItemWidth(-8.0f);
        if (ui::BeginCombo("##pos", modes[mode]))
        {
            for (int i = 0; i < 3; ++i)
            {
                if (ui::Selectable(modes[i], i == mode))
                {
                    if (i == 0)
                    {
                        // Back to flow: pull the positioning nail and its
                        // offsets, but keep the authored size (deliberate).
                        DropPayloadStyle(payload, "position");
                        DropPayloadStyle(payload, "top");
                        DropPayloadStyle(payload, "right");
                        DropPayloadStyle(payload, "bottom");
                        DropPayloadStyle(payload, "left");
                    }
                    else
                    {
                        SetPayloadStyle(payload, "position", i == 1 ? "relative" : "absolute");
                    }
                    structural = true;
                }
            }
            ui::EndCombo();
        }
        ui::PopItemWidth();
        if (ui::IsItemHovered())
            ui::SetTooltip("In flow: laid out by its container. Anchor: stays in flow but is the\n"
                "reference for absolutely-positioned descendants. Free: out of flow, draggable\n"
                "in the preview against the nearest Anchor/Free ancestor.");
    }

    // --- Anchor (absolutely-positioned elements only): pick which edges this box
    // pins to and whether it stretches with its container. The 3x3 grid is the
    // cross product of the two per-axis modes, so a cell is always highlighted to
    // match the authored insets + size. The gizmo only ever writes top/left, so
    // re-pick a cell after dragging to re-seat on another edge. ---
    if (LowerCopy(cur("position")) == "absolute")
    {
        AnchorAxis h = AnchorNear, v = AnchorNear;
        ResolveAnchorAxes(payload, h, v);

        ui::Text("Anchor");
        const char* cellTip[3][3] = {
            { "top-left corner", "top edge, stretch across", "top-right corner" },
            { "left edge, stretch down", "fill the container", "right edge, stretch down" },
            { "bottom-left corner", "bottom edge, stretch across", "bottom-right corner" },
        };
        for (int row = 0; row < 3; ++row)
        {
            for (int col = 0; col < 3; ++col)
            {
                const AnchorAxis ch = col == 0 ? AnchorNear : col == 1 ? AnchorStretch : AnchorFar;
                const AnchorAxis cv = row == 0 ? AnchorNear : row == 1 ? AnchorStretch : AnchorFar;
                ui::PushID(row * 3 + col);
                if (ui::RadioButton("##a", h == ch && v == cv))
                {
                    ApplyAnchorAxis(payload, ch, "left", "right", "width");
                    ApplyAnchorAxis(payload, cv, "top", "bottom", "height");
                    structural = true;
                }
                if (ui::IsItemHovered())
                    ui::SetTooltip("%s", cellTip[row][col]);
                ui::PopID();
                if (col < 2)
                    ui::SameLine(0.0f, 24.0f);
            }
        }
        if (ui::IsItemHovered())
            ui::SetTooltip("Middle row / column stretch the box on that axis (size becomes\n"
                "auto). Centering a small element on both axes is left to a flex parent\n"
                "(Align) or raw margin:auto.");

        ui::TextDisabled("Insets (offset from each pinned edge; px or %)");
        LayoutSizeField(payload, structural, "Top", "top", "auto");
        LayoutSizeField(payload, structural, "Right", "right", "auto");
        LayoutSizeField(payload, structural, "Bottom", "bottom", "auto");
        LayoutSizeField(payload, structural, "Left", "left", "auto");
    }

    // --- Direction: picking Row/Column makes this a flex container (display:
    // flex), which also blocks-ifies its children so the inline-div trap cannot
    // bite. The container knobs below only apply once this is flex. ---
    {
        const bool flex = IsFlexValue(LowerCopy(cur("display")));
        const ea::string dir = LowerCopy(cur("flex-direction"));
        int d = flex ? ((dir == "column") ? 1 : 0) : -1;
        const char* dirs[] = { "(plain box)", "Row", "Column" };
        ui::Text(ICON_FA_UP_DOWN " Direction");
        ui::SameLine();
        ui::PushItemWidth(-8.0f);
        if (ui::BeginCombo("##dir", dirs[d < 0 ? 0 : d + 1]))
        {
            for (int i = 0; i < 3; ++i)
            {
                if (ui::Selectable(dirs[i], i == (d < 0 ? 0 : d + 1)))
                {
                    if (i == 0)
                    {
                        DropPayloadStyle(payload, "display");
                        DropPayloadStyle(payload, "flex-direction");
                    }
                    else
                    {
                        SetPayloadStyle(payload, "display", "flex");
                        SetPayloadStyle(payload, "flex-direction", i == 1 ? "row" : "column");
                    }
                    structural = true;
                }
            }
            ui::EndCombo();
        }
        ui::PopItemWidth();
        if (ui::IsItemHovered())
            ui::SetTooltip("Row / Column arranges this element's children. (plain box) leaves the\n"
                "display to the engine default (edit it or the -reverse variants via raw).");
    }

    // The flex container knobs and child sizing stay visible but are disabled
    // outside their context, so the panel does not reshuffle as you edit.
    const bool isFlex = IsFlexValue(LowerCopy(cur("display")));
    bool isFlexItem = false;
    if (tab && tab->GetDocument())
    {
        if (const UiNode* parent = tab->GetDocument()->GetModel().FindParent(node))
            isFlexItem = IsFlexValue(LowerCopy(Trim(parent->GetStyle("display"))));
    }

    ui::BeginDisabled(!isFlex);
    {
        // Gap: one field writes both axes (a single-axis stack ignores the other).
        {
            const ea::string gv = cur("row-gap");
            char buf[64];
            snprintf(buf, sizeof(buf), "%s", gv.c_str());
            ui::Text("Gap");
            ui::SameLine();
            ui::PushItemWidth(-8.0f);
            if (ui::InputTextWithHint("##gap", "0px", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue))
            {
                const ea::string next = Trim(buf);
                if (next.empty())
                {
                    DropPayloadStyle(payload, "row-gap");
                    DropPayloadStyle(payload, "column-gap");
                }
                else
                {
                    SetPayloadStyle(payload, "row-gap", next);
                    SetPayloadStyle(payload, "column-gap", next);
                }
                structural = true;
            }
            ui::PopItemWidth();
        }

        static const char* const jl[] = { "Start", "Center", "End", "Between", "Around" };
        static const char* const jv[] = { "flex-start", "center", "flex-end", "space-between", "space-around" };
        LayoutCombo(payload, structural, "Align (main axis)", "justify-content", jl, jv, 5, "flex-start");

        static const char* const al[] = { "Start", "Center", "End", "Stretch" };
        static const char* const av[] = { "flex-start", "center", "flex-end", "stretch" };
        LayoutCombo(payload, structural, "Align (cross axis)", "align-items", al, av, 4, "stretch");
    }
    ui::EndDisabled();

    // Scroll: one checkbox = overflow-y:auto + overflow-x:hidden. Deliberately
    // outside the flex gate - the engine's own #content scrolls a plain block box
    // (block + fixed height + overflow), the more reliable path. Flex is only
    // wanted for Gap/alignment; a simple list does not need it.
    {
        bool on = LowerCopy(cur("overflow-y")) == "auto";
        ui::Text("Scroll");
        ui::SameLine();
        if (ui::Checkbox(" vertically##scroll", &on))
        {
            if (on)
            {
                SetPayloadStyle(payload, "overflow-y", "auto");
                SetPayloadStyle(payload, "overflow-x", "hidden");
            }
            else
            {
                DropPayloadStyle(payload, "overflow-y");
                DropPayloadStyle(payload, "overflow-x");
            }
            structural = true;
        }
        if (ui::IsItemHovered())
            ui::SetTooltip("Give the box a fixed Height and the rows a real size so their total\n"
                "exceeds it and scrolls. In a plain block box that is all that is needed; in a\n"
                "Row/Column container also pin the rows with Hug/Fill (flex-shrink:0) so they\n"
                "are not squeezed to fit.");
    }

    // Padding: one field writes all four sides (four-way tuning lives in raw).
    {
        const ea::string pv = cur("padding-top");
        char buf[64];
        snprintf(buf, sizeof(buf), "%s", pv.c_str());
        ui::Text("Padding");
        ui::SameLine();
        ui::PushItemWidth(-8.0f);
        if (ui::InputTextWithHint("##pad", "0px", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue))
        {
            const ea::string next = Trim(buf);
            if (next.empty())
            {
                DropPayloadStyle(payload, "padding-top");
                DropPayloadStyle(payload, "padding-right");
                DropPayloadStyle(payload, "padding-bottom");
                DropPayloadStyle(payload, "padding-left");
            }
            else
            {
                SetPayloadStyle(payload, "padding-top", next);
                SetPayloadStyle(payload, "padding-right", next);
                SetPayloadStyle(payload, "padding-bottom", next);
                SetPayloadStyle(payload, "padding-left", next);
            }
            structural = true;
        }
        ui::PopItemWidth();
    }

    LayoutSizeField(payload, structural, "Width", "width", "auto");
    LayoutSizeField(payload, structural, "Height", "height", "auto");

    // Child sizing: meaningful only when this element is itself a flex item.
    ui::BeginDisabled(!isFlexItem);
    {
        const ea::string grow = cur("flex-grow");
        int s = -1; // 0 hug, 1 fill
        if (grow == "0")
            s = 0;
        else if (grow == "1")
            s = 1;
        const char* sl[] = { "(auto)", "Hug (content)", "Fill (parent)" };
        ui::Text("Size in parent");
        ui::SameLine();
        ui::PushItemWidth(-8.0f);
        if (ui::BeginCombo("##sizing", sl[s < 0 ? 0 : s + 1]))
        {
            for (int i = 0; i < 3; ++i)
            {
                if (ui::Selectable(sl[i], i == (s < 0 ? 0 : s + 1)))
                {
                    if (i == 0)
                    {
                        DropPayloadStyle(payload, "flex-grow");
                        DropPayloadStyle(payload, "flex-shrink");
                        DropPayloadStyle(payload, "flex-basis");
                    }
                    else
                    {
                        // Both pin flex-shrink:0 - the flex trap that otherwise
                        // squeezes rows to fit and kills scrolling.
                        SetPayloadStyle(payload, "flex-grow", i == 1 ? "0" : "1");
                        SetPayloadStyle(payload, "flex-shrink", "0");
                    }
                    structural = true;
                }
            }
            ui::EndCombo();
        }
        ui::PopItemWidth();
        if (ui::IsItemHovered())
            ui::SetTooltip("Available when this element's parent is a Row/Column container.");
    }
    ui::EndDisabled();

    if (structural && tab && tab->GetDocument())
    {
        // A structured commit changes style_; force the raw editor below to
        // reseed from the rebuilt model instead of showing its stale buffer.
        styleSeedValid_ = false;
        tab->GetDocument()->EditNodePayload(node, payload);
        return true; // the model was rebuilt; the node is dangling now
    }
    return false;
}

bool UIViewInspector::RenderInlineStyle(UiNode* node)
{
    UIViewTab* tab = owner_;
    if (!ui::CollapsingHeader(ICON_FA_CODE " Inline Style (raw)", ImGuiTreeNodeFlags_DefaultOpen))
        return false;

    ui::TextDisabled("The full style=\"...\" list, verbatim. Use it for values the "
        "structured Style rows above cannot express (transform, decorator, "
        "animation, !important). Applying this replaces every declaration it lists.");

    const ea::vector<unsigned> curPath = tab ? tab->GetSelectedPath() : ea::vector<unsigned>{};
    if (!styleSeedValid_ || curPath != lastStylePath_)
    {
        // Seed from the model's ordered declarations (source of truth).
        ea::string text;
        for (const UiStyleDecl& decl : node->style_)
            text += decl.name_ + ": " + decl.value_ + ";\n";
        snprintf(styleBuf_, sizeof(styleBuf_), "%s", text.c_str());
        lastStylePath_ = curPath;
        styleSeedValid_ = true;
    }

    bool edited = false;
    ui::InputTextMultiline("##style", styleBuf_, sizeof(styleBuf_), ImVec2(-1.0f, 120.0f));
    if (ui::Button(ICON_FA_CHECK " Apply Style"))
        edited = true;
    if (edited)
    {
        ea::vector<UiStyleDecl> parsed;
        ParseStyleDeclarations(ea::string(styleBuf_), parsed);
        styleSeedValid_ = false;
        if (tab && tab->GetDocument())
        {
            UiNodePayload payload = SnapshotUiNodePayload(*node);
            payload.style_ = ea::move(parsed);
            tab->GetDocument()->EditNodePayload(node, payload);
            return true; // the model was rebuilt; the node is dangling now
        }
    }
    return false;
}

void UIViewInspector::RenderComputed(UiNode* node)
{
    if (!ui::CollapsingHeader(ICON_FA_CALCULATOR " Computed", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    if (node->dom_)
    {
        const Vector2 pos = V2(node->dom_->GetAbsoluteOffset(Rml::BoxArea::Border));
        const Vector2 size = V2(node->dom_->GetBox().GetSize(Rml::BoxArea::Border));
        ui::Text("left %s  top %s", FormatPx(pos.x_).c_str(), FormatPx(pos.y_).c_str());
        ui::Text("width %s  height %s", FormatPx(size.x_).c_str(), FormatPx(size.y_).c_str());
    }

    // The gizmo now keys off the position declaration alone, so surface that
    // here: an absolutely positioned node can be dragged/resized in the preview.
    if (node->GetStyle("position") == "absolute")
        ui::TextDisabled("(absolutely positioned - drag the handles in the preview to move or resize)");
}

}

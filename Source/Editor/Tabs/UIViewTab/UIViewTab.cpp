//
// Copyright (c) 2017-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#include "UIViewTab.h"

#include "../../Core/IniHelpers.h"
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
// RmlUi (it lives inside a block), so the entry is an honest <div> with a
// text child: no fake styling, no pinned position - it joins the flow.
const PaletteEntry kPalette[] = {
    {ICON_FA_SQUARE "  div", "Structure", "div", nullptr, nullptr, nullptr, nullptr, nullptr, 0, UiWidgetStylePolicy::Panel, true, 160.0f, 48.0f},
    {ICON_FA_TABLE_LIST "  form", "Structure", "form", nullptr, nullptr, nullptr, nullptr, nullptr, 0, UiWidgetStylePolicy::Panel, true, 240.0f, 96.0f},
    {ICON_FA_IMAGE "  img", "Content", "img", "src", "", nullptr, nullptr, nullptr, 0, UiWidgetStylePolicy::Panel, true, 160.0f, 48.0f},
    {ICON_FA_FONT "  Text (div)", "Content", "div", nullptr, nullptr, "Text", nullptr, nullptr, 0, UiWidgetStylePolicy::None, false, 0.0f, 0.0f},
    {ICON_FA_TOGGLE_ON "  button", "Controls", "button", nullptr, nullptr, "Button", nullptr, nullptr, 0, UiWidgetStylePolicy::Outline, true, 160.0f, 48.0f},
    {ICON_FA_KEYBOARD "  input (text)", "Controls", "input", "type", "text", nullptr, nullptr, nullptr, 0, UiWidgetStylePolicy::Outline, true, 160.0f, 28.0f},
    {ICON_FA_SQUARE_CHECK "  input (checkbox)", "Controls", "input", "type", "checkbox", nullptr, nullptr, nullptr, 0, UiWidgetStylePolicy::Outline, true, 20.0f, 20.0f},
    {ICON_FA_CIRCLE_DOT "  input (radio)", "Controls", "input", "type", "radio", nullptr, nullptr, nullptr, 0, UiWidgetStylePolicy::Outline, true, 20.0f, 20.0f},
    {ICON_FA_SLIDERS "  input (range)", "Controls", "input", "type", "range", nullptr, nullptr, nullptr, 0, UiWidgetStylePolicy::Outline, true, 160.0f, 20.0f},
    {ICON_FA_PAPER_PLANE "  input (submit)", "Controls", "input", "type", "submit", nullptr, nullptr, nullptr, 0, UiWidgetStylePolicy::Outline, true, 120.0f, 36.0f},
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
    // from its stable child-index path, falling back to the root.
    selected_ = model.ResolvePath(selPath_);
    if (!selected_)
        selected_ = model.root_.Get();
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

    nfdu8char_t* outPath = nullptr;
    const nfdresult_t res = NFD_SaveDialogU8(&outPath, &filterItem, 1,
        dataDir.empty() ? nullptr : dataDir.c_str(), "NewDocument.rml");
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
    const ea::string resourceName = chosen.substr(dataDir.length());
    if (resourceName.empty())
        return;

    // (Re)open so the freshly written file becomes the active, tracked
    // document. An already-open document is only focused - closing it would
    // discard its undo history and any unsaved edits.
    if (!IsResourceOpen(resourceName)
        && !WriteResourceFile(resourceName, kTemplate))
    {
        URHO3D_LOGERROR("UIViewTab: failed to create UI document '{}'", resourceName.c_str());
        return;
    }

    // Route through the project request exactly like a double-click open
    // (all instances arbitrate in OpenInBestInstance: idle ones take the
    // document, a busy project spawns a fresh tab). This must not call
    // OpenInBestInstance directly: we run inside a tab's Render here, and the
    // spawn path's AddTab would land mid-iteration of the render loop, so
    // the fresh tab is not rendered this frame - the frame-end
    // CheckRemoveTab would then see it as closed (its window has never
    // opened) and destroy it. ProcessRequest defers the routing to the
    // beginning of the next frame, where AddTab happens before tabs are
    // iterated.
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
        if (node && !node->IsText() && !node->IsNestedDoc() && !node->IsHeadLink()
            && !node->IsMaterialized())
        {
            if (ui::MenuItem(ICON_FA_LOCATION_PIN " Add Explicit Position"))
                document_->MaterializeNode(node);
        }
        else if (node && !node->IsText() && !node->IsNestedDoc() && !node->IsHeadLink()
            && node->IsMaterialized())
        {
            if (ui::MenuItem(ICON_FA_ARROWS_TO_DOT " Remove Explicit Position"))
                document_->DematerializeNode(node);
        }
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
        // materialized box is left/top-space; shift the absolute mouse point
        // into that frame before picking.
        UiBox box;
        if (selected_ && selected_->dom_ && TryGetMaterializedBox(*selected_, box))
        {
            const float grab = kHandleGrabPx / vp.scale_;
            const Vector2 base = document_->GetInlineStyleBase(selected_);
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
    TryGetMaterializedBox(*node, gizmoStartBox_);
    // Capture the left/top frame origin once: the live box and the element
    // itself both move through this base during the drag.
    gizmoBase_ = document_->GetInlineStyleBase(node);
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

        // The gizmo rect is the selection rect; handles are only offered when
        // the node is materialized (or being dragged into shape). Regular
        // nodes yield a single box, so front() is the selection rect.
        UiBox materializedBox;
        if (!boxes.empty() && (dragging || TryGetMaterializedBox(*sel, materializedBox)))
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

    if (!target->IsText() && !target->IsMaterialized())
    {
        if (ui::MenuItem(ICON_FA_LOCATION_PIN " Add Explicit Position"))
            doc->MaterializeNode(target);
    }
    else if (!target->IsText() && target->IsMaterialized())
    {
        if (ui::MenuItem(ICON_FA_ARROWS_TO_DOT " Remove Explicit Position"))
            doc->DematerializeNode(target);
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
    ui::Text(ICON_FA_HAND_POINTER " %s", header.c_str());
    // The file this element's source lives in (matters with several documents
    // open and for template-minted areas, whose source lives elsewhere).
    if (UIViewDocument* doc = tab ? tab->GetDocument() : nullptr)
        ui::TextDisabled(ICON_FA_FILE " %s", doc->GetSourcePath().c_str());
    ui::Separator();

    RenderTextContent(node);
    RenderAttributes(node);
    ui::Separator();
    RenderInlineStyle(node);
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
    const bool committed = ui::InputTextWithHint("##headLinkHref", "/UI/default.rcss",
        headLinkHrefBuf_, sizeof(headLinkHrefBuf_), ImGuiInputTextFlags_EnterReturnsTrue);
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
    if (ui::InputText("href", hrefBuf, sizeof(hrefBuf), ImGuiInputTextFlags_EnterReturnsTrue))
    {
        const ea::string newHref = Trim(ea::string(hrefBuf));
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
    if (ui::InputText("href", hrefBuf, sizeof(hrefBuf), ImGuiInputTextFlags_EnterReturnsTrue))
    {
        const ea::string newHref = Trim(ea::string(hrefBuf));
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

void UIViewInspector::RenderTextContent(UiNode* node)
{
    UIViewTab* tab = owner_;
    if (!tab || !tab->GetDocument())
        return;

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
        return;

    if (!ui::CollapsingHeader(ICON_FA_FONT " Content", ImGuiTreeNodeFlags_DefaultOpen))
        return;

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
    }
}

void UIViewInspector::RenderAttributes(UiNode* node)
{
    UIViewTab* tab = owner_;
    if (!ui::CollapsingHeader(ICON_FA_LIST " Attributes", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    // Edits accumulate into a payload COPY; the model node is only touched by
    // the undoable command, which snapshots the pristine "old" state itself.
    UiNodePayload payload = SnapshotUiNodePayload(*node);
    bool structural = false;

    // id / class are dedicated fields; editing them is structural (emitted).
    char idBuf[256];
    snprintf(idBuf, sizeof(idBuf), "%s", node->id_.c_str());
    ui::InputText("id", idBuf, sizeof(idBuf), ImGuiInputTextFlags_EnterReturnsTrue);
    if (ui::IsItemDeactivatedAfterEdit())
    {
        payload.id_ = Trim(idBuf);
        structural = true;
    }
    char classBuf[256];
    snprintf(classBuf, sizeof(classBuf), "%s", node->classes_.c_str());
    ui::InputText("class", classBuf, sizeof(classBuf), ImGuiInputTextFlags_EnterReturnsTrue);
    if (ui::IsItemDeactivatedAfterEdit())
    {
        payload.classes_ = Trim(classBuf);
        structural = true;
    }

    const size_t count = node->attributes_.size();
    for (size_t i = 0; i < count; i++)
    {
        const ea::string name = node->attributes_[i].first;
        char valBuf[1024];
        snprintf(valBuf, sizeof(valBuf), "%s", node->attributes_[i].second.c_str());
        ui::PushID(name.c_str());
        ui::Text("%s", name.c_str());
        ui::SameLine();
        ui::PushItemWidth(-40.0f);
        if (ui::InputText("##value", valBuf, sizeof(valBuf), ImGuiInputTextFlags_EnterReturnsTrue))
        {
            payload.attributes_[i].second = valBuf;
            structural = true;
        }
        ui::PopItemWidth();
        ui::SameLine();
        if (ui::SmallButton(ICON_FA_TRASH))
        {
            payload.attributes_.erase(payload.attributes_.begin() + i);
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
        tab->GetDocument()->EditNodePayload(node, payload);
}

void UIViewInspector::RenderInlineStyle(UiNode* node)
{
    UIViewTab* tab = owner_;
    if (!ui::CollapsingHeader(ICON_FA_PAINTBRUSH " Inline Style", ImGuiTreeNodeFlags_DefaultOpen))
        return;

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
        }
    }
}

void UIViewInspector::RenderComputed(UiNode* node)
{
    UIViewTab* tab = owner_;
    if (!ui::CollapsingHeader(ICON_FA_CALCULATOR " Computed", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    if (node->dom_)
    {
        const Vector2 pos = V2(node->dom_->GetAbsoluteOffset(Rml::BoxArea::Border));
        const Vector2 size = V2(node->dom_->GetBox().GetSize(Rml::BoxArea::Border));
        ui::Text("left %s  top %s", FormatPx(pos.x_).c_str(), FormatPx(pos.y_).c_str());
        ui::Text("width %s  height %s", FormatPx(size.x_).c_str(), FormatPx(size.y_).c_str());
    }

    if (node->IsMaterialized())
    {
        ui::TextDisabled("(explicit / editable)");
        if (tab && tab->GetDocument())
        {
            if (ui::Button(ICON_FA_ARROWS_TO_DOT " Remove Explicit Position"))
                tab->GetDocument()->DematerializeNode(node);
        }
    }
    else if (!node->IsText() && tab && tab->GetDocument())
    {
        if (ui::Button(ICON_FA_LOCATION_PIN " Add Explicit Position"))
            tab->GetDocument()->MaterializeNode(node);
    }
}

}

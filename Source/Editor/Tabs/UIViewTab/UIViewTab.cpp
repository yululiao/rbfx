//
// Copyright (c) 2017-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#include "UIViewTab.h"
#include "UIViewDropMath.h"
#include "UIViewParagraphText.h"

#include "../../Core/IniHelpers.h"
#include "../../Core/WidgetHelpers.h"
#include "../../Project/Project.h"
#include "../HierarchyBrowserTab.h"
#include "../InspectorTab.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/File.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Graphics/Texture2D.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/SystemUI/DragDropPayload.h>
#include <Urho3D/SystemUI/SystemUI.h>
#include <Urho3D/SystemUI/Widgets.h>

#include <IconFontCppHeaders/IconsFontAwesome6.h>
#include <nfd.h>

#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/StyleTypes.h>

#include <float.h>
#include <math.h>
#include <stdio.h>
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
constexpr ImU32 kMarqueeColor = IM_COL32(120, 180, 255, 220);
constexpr ImU32 kMarqueeFill = IM_COL32(120, 180, 255, 28);
// Box-model band fills (DevTools palette): margin orange, border yellow,
// padding green; the content area keeps showing the document itself.
constexpr ImU32 kBoxMarginFill = IM_COL32(246, 178, 107, 72);
constexpr ImU32 kBoxBorderFill = IM_COL32(255, 229, 153, 72);
constexpr ImU32 kBoxPaddingFill = IM_COL32(195, 231, 167, 72);
constexpr ImU32 kBoxLabelText = IM_COL32(255, 255, 255, 245);
constexpr ImU32 kBoxLabelBg = IM_COL32(24, 24, 24, 210);
// Drop feedback for structural drags and external payloads (flow editor):
// the fill tints the container a drop lands inside, the solid color draws
// the slot line and the outline of the node being moved.
constexpr ImU32 kDropColor = IM_COL32(90, 230, 140, 240);
constexpr ImU32 kDropFill = IM_COL32(90, 230, 140, 22);
// Inline text editing: the marker around the element whose #text child is
// being edited in place.
constexpr ImU32 kTextEditColor = IM_COL32(255, 220, 90, 220);

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

// Convert a resource-root-relative path ("Textures/foo.png") into a path
// relative to the document that references it ("../Textures/foo.png"): the
// spelling an <img src> needs, since RmlUi resolves references against the
// .rml's own location (SystemInterface::JoinPath).
ea::string MakeDocRelativePath(const ea::string& docPath, const ea::string& resourcePath)
{
    ea::vector<ea::string> docDirs;
    const size_t docSlash = docPath.find_last_of('/');
    const ea::string docDir = docSlash == ea::string::npos ? ea::string() : docPath.substr(0, docSlash);
    size_t begin = 0;
    while (begin < docDir.length())
    {
        const size_t end = docDir.find('/', begin);
        docDirs.push_back(docDir.substr(begin, (end == ea::string::npos ? docDir.length() : end) - begin));
        if (end == ea::string::npos)
            break;
        begin = end + 1;
    }

    ea::vector<ea::string> targetDirs;
    begin = 0;
    while (begin <= resourcePath.length())
    {
        const size_t end = resourcePath.find('/', begin);
        if (end == ea::string::npos)
        {
            targetDirs.push_back(resourcePath.substr(begin));
            break;
        }
        targetDirs.push_back(resourcePath.substr(begin, end - begin));
        begin = end + 1;
    }
    if (targetDirs.empty())
        return resourcePath;

    // Longest common directory prefix; the file name itself never counts.
    size_t common = 0;
    while (common < docDirs.size() && common + 1 < targetDirs.size() && docDirs[common] == targetDirs[common])
        ++common;

    ea::string out2;
    for (size_t i = common; i < docDirs.size(); i++)
        out2 += "../";
    for (size_t i = common; i < targetDirs.size(); i++)
    {
        if (i > common)
            out2 += '/';
        out2 += targetDirs[i];
    }
    return out2;
}

// Build the kUiNodeDragType payload: the source document's instance id
// followed by the dragged node's child-index path. The id lets a target on
// another document (another tab's canvas or hierarchy) reject the payload
// instead of resolving the path against the wrong model.
ea::vector<unsigned> MakeNodeDragData(const UIViewDocument* doc, const ea::vector<unsigned>& path)
{
    ea::vector<unsigned> data;
    data.reserve(path.size() + 1);
    data.push_back(doc->GetInstanceId());
    data.insert(data.end(), path.begin(), path.end());
    return data;
}

// Parse a kUiNodeDragType payload for \a doc. False for payloads from
// another document and for malformed payloads; on success \a outPath gets
// the dragged node's child-index path.
bool ParseNodeDragData(const ImGuiPayload* payload, const UIViewDocument* doc, ea::vector<unsigned>& outPath)
{
    const unsigned count = static_cast<unsigned>(payload->DataSize) / sizeof(unsigned);
    if (!doc || count == 0)
        return false;
    const unsigned* data = static_cast<const unsigned*>(payload->Data);
    if (data[0] != doc->GetInstanceId())
        return false;
    outPath.assign(data + 1, data + count);
    return true;
}

// Full child-list index of \a node inside \a parent (M_MAX_UNSIGNED when not
// among its children). Text nodes count: the index numbering MoveNode and
// AddWidget take is the model's full children_ numbering.
unsigned ChildIndexOf(const UiNode* parent, const UiNode* node)
{
    if (!parent)
        return M_MAX_UNSIGNED;
    for (unsigned i = 0; i < parent->children_.size(); i++)
    {
        if (parent->children_[i].Get() == node)
            return i;
    }
    return M_MAX_UNSIGNED;
}

// True when \a node is \a top or lives inside its subtree.
bool IsInSubtree(const UiDocumentModel& model, const UiNode* top, const UiNode* node)
{
    for (const UiNode* n = node; n; n = model.FindParent(n))
    {
        if (n == top)
            return true;
    }
    return false;
}

// Flow axis of a node's children: a flex row lays them out horizontally,
// everything else (block / inline / no live DOM) stacks them vertically.
DropAxis FlowAxisOf(const UiNode* node)
{
    if (node && node->dom_)
    {
        const Rml::ComputedValues& computed = node->dom_->GetComputedValues();
        // The computed-value enums live in Rml::Style (StyleTypes.h).
        const Rml::Style::Display display = computed.display();
        if (display == Rml::Style::Display::Flex || display == Rml::Style::Display::InlineFlex)
        {
            const Rml::Style::FlexDirection direction = computed.flex_direction();
            if (direction == Rml::Style::FlexDirection::Row
                || direction == Rml::Style::FlexDirection::RowReverse)
                return DropAxis::Horizontal;
        }
    }
    return DropAxis::Vertical;
}

// The unique non-blank #text child of a pure-text element: exactly the shape
// the inline canvas editor may open on ("children are whitespace text plus a
// single text run"). Mixed content (any element child) and text-less elements
// return null - double-clicking those only selects.
UiNode* FindPureTextChild(UiNode* node)
{
    if (!node || node->IsText() || node->IsNestedDoc() || node->IsHeadLink())
        return nullptr;
    UiNode* text = nullptr;
    for (const SharedPtr<UiNode>& child : node->children_)
    {
        if (!child->IsText())
            return nullptr; // an element child makes this mixed content
        if (Trim(child->text_).empty())
            continue; // authored whitespace between tags is not content
        if (text)
            return nullptr; // several text runs: not the simple shape
        text = child.Get();
    }
    return text;
}

// External payload acceptance: exactly one image file. Folders and
// multi-file drags have no single authored element to become; the extension
// list is what the engine's texture loaders cover.
bool IsSupportedImageDrop(const ResourceDragDropPayload& payload)
{
    if (payload.resources_.size() != 1)
        return false;
    const ResourceFileDescriptor& desc = payload.resources_[0];
    if (desc.isDirectory_)
        return false;
    return desc.HasExtension({"png", "jpg", "jpeg", "tga", "dds", "bmp"});
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

// Canvas-size preference of the preview (see the declaration for the contract):
// one value shared by every instance, persisted by the primary instance only.
IntVector2 UIViewTab::sViewCanvasSize_{1024, 768};

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
    selPaths_.clear();
    sels_.clear();
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

void UIViewTab::SyncPrimary()
{
    selected_ = sels_.empty() ? nullptr : sels_.back();
    selPath_ = selPaths_.empty() ? ea::vector<unsigned>() : selPaths_.back();
}

void UIViewTab::OnDocumentEdited()
{
    if (!document_)
        return;
    const UiDocumentModel& model = document_->GetModel();

    // A rebuild invalidates every node pointer a live gesture holds, and undo
    // / redo can land mid-gesture (global shortcuts, menu). Only the stale
    // pointers are cleared here - the model is never touched, and a command
    // whose own commit caused this rebuild finishes its cleanup right after.
    dragging_ = false;
    gizmoNode_ = nullptr;
    extraDragNodes_.clear();
    extraDragStarts_.clear();
    CancelStructDrag();
    // The inline editor re-resolves its text run instead of closing, so a
    // typed buffer survives an unrelated edit elsewhere. It ends only when
    // the run itself did not survive the rebuild (e.g. an undo removed it).
    if (textEditActive_)
    {
        UiNode* textNode = model.ResolvePath(textEditPath_);
        if (textNode && textNode->IsText())
            textEditNode_ = textNode;
        else
            EndInlineTextEdit(false);
    }

    // Every command (and every undo/redo) rebuilds the whole model tree from
    // text, so no node pointer survives an edit; re-resolve every selected
    // path from its stable child-index path and drop the vanished ones.
    ea::vector<ea::vector<unsigned>> keptPaths;
    ea::vector<UiNode*> kept;
    for (size_t i = 0; i < selPaths_.size(); i++)
    {
        if (UiNode* n = model.ResolvePath(selPaths_[i]))
        {
            keptPaths.push_back(selPaths_[i]);
            kept.push_back(n);
        }
    }
    // The primary (last) path may have been dropped while earlier ones lived:
    // remember the pre-edit primary so a total loss can fall back to its parent.
    const ea::vector<unsigned> prevPrimary = selPath_;
    selPaths_ = ea::move(keptPaths);
    sels_ = ea::move(kept);

    if (sels_.empty() && !prevPrimary.empty())
    {
        // Nothing survived (deleted, or an undo removed it): fall back to the
        // old primary's parent so the panel keeps context. Never snap silently
        // to the root - an edit meant for a vanished node would otherwise land
        // on the document root and corrupt it.
        ea::vector<unsigned> fallback = prevPrimary;
        fallback.pop_back();
        if (UiNode* n = model.ResolvePath(fallback))
        {
            selPaths_.push_back(fallback);
            sels_.push_back(n);
        }
    }
    SyncPrimary();
    model.BuildPath(selected_, selPath_);
    if (inspectorSource_)
        inspectorSource_->InvalidateCaches();
}

void UIViewTab::SetSelectedNode(UiNode* node)
{
    // Single-select: replace the whole selection with this one node.
    selPaths_.clear();
    sels_.clear();
    if (node)
    {
        selPaths_.push_back(NodePath(node));
        sels_.push_back(node);
    }
    SyncPrimary();
    if (hierarchySource_)
        hierarchySource_->ExpandAncestors(selPath_);
}

void UIViewTab::SetSelection(const ea::vector<UiNode*>& nodes)
{
    selPaths_.clear();
    sels_.clear();
    if (document_)
    {
        const UiDocumentModel& model = document_->GetModel();
        for (UiNode* node : nodes)
        {
            ea::vector<unsigned> path;
            if (node && model.BuildPath(node, path))
            {
                selPaths_.push_back(path);
                sels_.push_back(node);
            }
        }
    }
    SyncPrimary();
    if (!sels_.empty() && hierarchySource_)
        hierarchySource_->ExpandAncestors(selPath_);
}

void UIViewTab::ToggleSelectNode(UiNode* node)
{
    if (!document_)
        return;
    if (!node)
    {
        selPaths_.clear();
        sels_.clear();
        SyncPrimary();
        return;
    }
    const ea::vector<unsigned> path = NodePath(node);
    for (size_t i = 0; i < selPaths_.size(); i++)
    {
        if (selPaths_[i] == path)
        {
            selPaths_.erase(selPaths_.begin() + i);
            sels_.erase(sels_.begin() + i);
            SyncPrimary();
            return;
        }
    }
    selPaths_.push_back(path);
    sels_.push_back(node);
    SyncPrimary();
    if (hierarchySource_)
        hierarchySource_->ExpandAncestors(path);
}

bool UIViewTab::IsSelected(const ea::vector<unsigned>& path) const
{
    for (const ea::vector<unsigned>& p : selPaths_)
    {
        if (p == path)
            return true;
    }
    return false;
}

ea::vector<UiNode*> UIViewTab::GetSelectedNodes() const
{
    return sels_;
}

ea::vector<UiNode*> UIViewTab::GetTopLevelSelectedNodes() const
{
    ea::vector<UiNode*> out;
    if (!document_)
        return out;
    const UiDocumentModel& model = document_->GetModel();
    UiNode* root = model.root_.Get();
    for (UiNode* node : sels_)
    {
        if (!node || node == root || node->IsText() || node->IsNestedDoc() || node->IsHeadLink())
            continue;
        // Skip a node whose selected ancestor already covers it: the batch
        // operation on the ancestor carries this subtree along, so applying it
        // here too would double-move / duplicate.
        bool covered = false;
        for (UiNode* p = model.FindParent(node); p && p != root; p = model.FindParent(p))
        {
            for (UiNode* s : sels_)
            {
                if (s == p)
                {
                    covered = true;
                    break;
                }
            }
            if (covered)
                break;
        }
        if (!covered)
            out.push_back(node);
    }
    return out;
}

void UIViewTab::CopySelection()
{
    if (!document_)
        return;
    const ea::vector<UiNode*> targets = GetTopLevelSelectedNodes();
    if (targets.empty())
        return; // copy does not apply to a virtual / root-only selection
    if (targets.size() == 1)
    {
        if (UiNode* copy = document_->DuplicateNode(targets[0]))
            SetSelectedNode(copy);
        return;
    }
    const ea::vector<UiNode*> copies = document_->DuplicateNodes(targets);
    if (!copies.empty())
        SetSelection(copies);
}

void UIViewTab::DeleteSelection()
{
    if (!document_)
        return;
    const ea::vector<UiNode*> targets = GetTopLevelSelectedNodes();
    if (!targets.empty())
    {
        document_->DeleteNodes(targets); // one undo step for the whole batch
        SetSelectedNode(document_->GetModel().root_.Get());
        return;
    }
    // Only virtual (head-link / nested-doc) or root selected: route through the
    // single-node command, which handles the text-level link removal.
    if (selected_)
        document_->DeleteNode(selected_); // OnDocumentEdited revalidates
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
    UiNode* root = document_ ? document_->GetModel().root_.Get() : nullptr;
    selected_ = root;
    selPath_.clear();
    // Seed the multi-selection set with the root (its path is the empty path)
    // so the batch APIs see a consistent single selection right after (re)load.
    selPaths_.clear();
    sels_.clear();
    if (root)
    {
        selPaths_.push_back(selPath_);
        sels_.push_back(root);
    }
    hoveredPath_.clear();
    gizmoNode_ = nullptr;
    dragging_ = false;
    marqueeActive_ = false;
    CancelStructDrag();
    extraDragNodes_.clear();
    extraDragStarts_.clear();
    // The canvas view always starts fitted to the (re)loaded document, and a
    // live inline edit would still point at a node of the dying generation.
    viewFit_ = true;
    viewPan_ = Vector2::ZERO;
    panningActive_ = false;
    textEditActive_ = false;
    textEditJustOpened_ = false;
    textEditNode_ = nullptr;
    textEditPath_.clear();
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
        // The canvas size is an editor-wide view preference (restored from the
        // ini), applied to the fresh surface so the document lays out against
        // exactly the canvas the user last authored against; the toolbar
        // combo's custom W/H fields start from the same value.
        document_->SetPreviewSize(sViewCanvasSize_);
        customCanvasW_ = sViewCanvasSize_.x_;
        customCanvasH_ = sViewCanvasSize_.y_;
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
    selPaths_.clear();
    sels_.clear();
    hoveredPath_.clear();
    gizmoNode_ = nullptr;
    dragging_ = false;
    marqueeActive_ = false;
    CancelStructDrag();
    extraDragNodes_.clear();
    extraDragStarts_.clear();

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
    CancelStructDrag();

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
    // The canvas size leads the section, ahead of every base key: the ini is
    // replayed line by line in file order, and the base's ActiveResourceName
    // line opens this instance's document - OnResourceLoaded sizes the fresh
    // surface from the value, so a later line would land one document too
    // late. Primary only, like the Documents list below.
    if (isPrimary_)
    {
        WriteIntToIni(output, "CanvasWidth", sViewCanvasSize_.x_);
        WriteIntToIni(output, "CanvasHeight", sViewCanvasSize_.y_);
    }

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
    // Parsed ahead of the base call: WriteIniSettings orders the file the same
    // way, so by the time the base's resource keys open this instance's
    // document, OnResourceLoaded already sees the restored size.
    if (isPrimary_)
    {
        if (const auto width = ReadIntFromIni(line, "CanvasWidth"))
            sViewCanvasSize_.x_ = Clamp(*width, 16, 8192);
        if (const auto height = ReadIntFromIni(line, "CanvasHeight"))
            sViewCanvasSize_.y_ = Clamp(*height, 16, 8192);
    }

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
        // Filter box at the top: typing narrows the palette to entries whose
        // label or group matches (case-insensitive), so the longer palette
        // stays quick to scan.
        ui::SetNextItemWidth(-FLT_MIN);
        ui::InputTextWithHint("##paletteFilter", ICON_FA_FILTER " Filter...", paletteFilter_,
            sizeof(paletteFilter_));
        const ea::string filter = LowerCopy(paletteFilter_);
        // Compare by content, not by pointer: identical string literals are
        // only merged into one address when the compiler pools strings, so a
        // pointer comparison would re-print the header before every entry on
        // builds without pooling.
        ea::string lastGroup;
        for (const PaletteEntry& entry : kPalette)
        {
            if (!filter.empty() && LowerCopy(entry.label_).find(filter) == ea::string::npos
                && LowerCopy(entry.group_).find(filter) == ea::string::npos)
                continue;
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
    const int selCount = static_cast<int>(GetTopLevelSelectedNodes().size());
    ui::BeginDisabled(selCount == 0);
    if (selCount > 1)
    {
        if (ui::Button(Format(ICON_FA_COPY " Copy (%d)", selCount).c_str()))
            CopySelection();
    }
    else if (ui::Button(ICON_FA_COPY " Copy"))
        CopySelection();
    ui::EndDisabled();
    // Delete also covers the virtual link nodes: deleting one removes its
    // <link> line from <head> (text-level, undoable). Copy does not - a
    // duplicated link line would be pointless noise.
    const bool canDelete = selected_ && document_
        && selected_ != document_->GetModel().root_.Get();
    ui::SameLine();
    ui::BeginDisabled(!canDelete);
    if (selCount > 1)
    {
        if (ui::Button(Format(ICON_FA_TRASH " Delete (%d)", selCount).c_str()))
            DeleteSelection();
    }
    else if (ui::Button(ICON_FA_TRASH " Delete"))
        DeleteSelection(); // single real / virtual: DeleteSelection routes both
    ui::EndDisabled();
    ui::EndDisabled();

    // Structural commands on the selection: wrap the set into a container, or
    // trade the primary's slot with a sibling. The disabled states carry the
    // reason as a tooltip so the rules (one parent, no absolute nodes) stay
    // visible instead of the buttons silently doing nothing.
    const ea::string wrapReason = WrapUnavailableReason();
    ui::BeginDisabled(!wrapReason.empty());
    if (ui::Button(ICON_FA_OBJECT_GROUP " Wrap"))
        ui::OpenPopup("##uiWrapMenu");
    ui::EndDisabled();
    if (!wrapReason.empty())
        ui::SetItemTooltip("%s", wrapReason.c_str());
    if (ui::BeginPopup("##uiWrapMenu"))
    {
        if (ui::MenuItem("Wrap in Row"))
            WrapSelection(UiWrapMode::Row);
        if (ui::MenuItem("Wrap in Column"))
            WrapSelection(UiWrapMode::Column);
        if (ui::MenuItem("Wrap in Box"))
            WrapSelection(UiWrapMode::Box);
        ui::EndPopup();
    }
    ui::SameLine();
    ui::BeginDisabled(!CanMoveInFlow(-1));
    if (ui::Button(ICON_FA_ARROW_UP))
        MoveSelectionInFlow(-1);
    ui::EndDisabled();
    ui::SameLine();
    ui::BeginDisabled(!CanMoveInFlow(1));
    if (ui::Button(ICON_FA_ARROW_DOWN))
        MoveSelectionInFlow(1);
    ui::EndDisabled();
    ui::SameLine();
    ui::TextDisabled("Alt+Up/Down");

    // View controls: how the canvas is sampled, not what it contains. Their
    // single consumer is the DocViewport built in RenderPreview.
    ui::BeginDisabled(!hasDoc);
    if (ui::Button(ICON_FA_EXPAND " Fit"))
        viewFit_ = true;
    ui::SameLine();
    if (ui::Button("100%"))
    {
        viewZoom_ = 1.0f;
        viewFit_ = false;
    }
    ui::SameLine();
    ui::TextDisabled("%d%%", static_cast<int>(lroundf(viewZoom_ * 100.0f)));
    ui::SameLine();
    const IntVector2 canvasNow = document_ ? document_->GetPreviewSize() : sViewCanvasSize_;
    const ea::string canvasLabel = Format("Canvas %d x %d", canvasNow.x_, canvasNow.y_);
    if (ui::BeginCombo("##uiViewCanvasSize", canvasLabel.c_str()))
    {
        static const IntVector2 presets[] = {{1024, 768}, {1280, 720}, {1920, 1080}, {768, 1024}};
        for (const IntVector2& preset : presets)
        {
            const ea::string label = Format("%d x %d", preset.x_, preset.y_);
            if (ui::Selectable(label.c_str(), preset == canvasNow))
                ApplyCanvasSize(preset);
        }
        ui::SeparatorText("Custom");
        ui::SetNextItemWidth(70.0f);
        ui::InputInt("##canvasW", &customCanvasW_, 0, 0);
        ui::SameLine();
        ui::SetNextItemWidth(70.0f);
        ui::InputInt("##canvasH", &customCanvasH_, 0, 0);
        ui::SameLine();
        if (ui::Button("Set"))
            ApplyCanvasSize(IntVector2{Clamp(customCanvasW_, 16, 8192), Clamp(customCanvasH_, 16, 8192)});
        ui::EndCombo();
    }
    ui::EndDisabled();

    // Element context menu (opened by a right-click on the preview). The right-
    // click handler already collapsed the selection to the picked node unless it
    // was part of a multi-selection, so acting on the whole selection is correct.
    if (ui::BeginPopup("##uiElemCtx"))
    {
        UiNode* node = selected_;
        const int ctxCount = static_cast<int>(GetTopLevelSelectedNodes().size());
        const bool real = node && document_ && node != document_->GetModel().root_.Get();
        if (ctxCount > 1)
        {
            if (ui::MenuItem(Format(ICON_FA_COPY " Copy %d Items", ctxCount).c_str()))
                CopySelection();
            if (ui::MenuItem(Format(ICON_FA_TRASH " Delete %d Items", ctxCount).c_str()))
                DeleteSelection();
        }
        else if (real && !node->IsNestedDoc() && !node->IsHeadLink())
        {
            if (ui::MenuItem(ICON_FA_COPY " Copy"))
                CopySelection();
            if (ui::MenuItem(ICON_FA_TRASH " Delete"))
                DeleteSelection();
        }
        else if (real)
        {
            // Virtual link / nested-doc node: delete routes to the text-level
            // head removal; copy does not apply.
            if (ui::MenuItem(ICON_FA_TRASH " Delete"))
                DeleteSelection();
        }
        // Structural commands act on the real-element selection: wrap the set
        // (rules in WrapNodes) or trade the primary's slot with a sibling.
        if (ctxCount > 0 && real)
        {
            ui::Separator();
            const ea::string reason = WrapUnavailableReason();
            ui::BeginDisabled(!reason.empty());
            if (ui::MenuItem(ICON_FA_OBJECT_GROUP " Wrap in Row"))
                WrapSelection(UiWrapMode::Row);
            if (ui::MenuItem(ICON_FA_OBJECT_GROUP " Wrap in Column"))
                WrapSelection(UiWrapMode::Column);
            if (ui::MenuItem(ICON_FA_OBJECT_GROUP " Wrap in Box"))
                WrapSelection(UiWrapMode::Box);
            ui::EndDisabled();
            if (!reason.empty())
                ui::SetItemTooltip("%s", reason.c_str());
            if (ctxCount == 1)
            {
                ui::BeginDisabled(!CanMoveInFlow(-1));
                if (ui::MenuItem("Move Up (Alt+Up)"))
                    MoveSelectionInFlow(-1);
                ui::EndDisabled();
                ui::BeginDisabled(!CanMoveInFlow(1));
                if (ui::MenuItem("Move Down (Alt+Down)"))
                    MoveSelectionInFlow(1);
                ui::EndDisabled();
            }
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

    // Drop feedback is recomputed from scratch every frame - by the in-canvas
    // structural drag or by an external payload hovering the canvas - so a
    // stale indicator can never outlive the gesture that produced it.
    dropKind_ = 0;

    // The document renders into the texture from E_BEGINRENDERING; here we
    // only sample the produced texture (never issue draws during widget
    // build). The canvas is a plain draw-list image, not an ImGui item: no
    // item means nothing inflates the window's content size, so zooming past
    // the panel and panning never spawn scrollbars.
    const IntVector2 previewSize = document_->GetPreviewSize();
    const ImVec2 canvasMin = ui::GetCursorScreenPos();
    const ImVec2 avail = ui::GetContentRegionAvail();

    // The document always opens "fit": the zoom follows the panel size until
    // the user zooms or pans by hand, which switches to the explicit viewZoom_.
    float fitScale = ea::min(avail.x / static_cast<float>(previewSize.x_),
                             avail.y / static_cast<float>(previewSize.y_));
    if (fitScale <= 0.0f)
        fitScale = 0.1f;
    if (viewFit_)
        viewZoom_ = fitScale;

    const bool canvasHovered = ui::IsMouseHoveringRect(canvasMin,
                                    ImVec2(canvasMin.x + avail.x, canvasMin.y + avail.y))
        && ui::IsWindowHovered(ImGuiHoveredFlags_None);
    // The inline editor owns the pointer while it is live: a stray click on
    // the canvas must not also zoom / pan behind it.
    if (!textEditActive_)
        HandleViewInput(canvasMin, avail, canvasHovered);

    // One mapping for every consumer below: picking, marquee, gizmo, drop
    // solving and the overlay all read the same DocViewport.
    const ImVec2 imageMin = CanvasOrigin(canvasMin, avail);
    const ImVec2 imageMax(imageMin.x + previewSize.x_ * viewZoom_,
                          imageMin.y + previewSize.y_ * viewZoom_);
    DocViewport vp;
    vp.origin_ = V2(imageMin);
    vp.scale_ = viewZoom_;

    // Clip the canvas (and its overlay) to the preview region: at high zoom
    // or with pan the image extends past the panel and must not paint over
    // the toolbar.
    ImDrawList* dl = ui::GetWindowDrawList();
    dl->PushClipRect(canvasMin, ImVec2(canvasMin.x + avail.x, canvasMin.y + avail.y), true);
    if (Texture2D* texture = document_->GetPreviewTexture())
    {
        // ReferenceTexture tags the SRV for the frame exactly like
        // Widgets::Image did; the draw-list image itself has no item.
        GetSubsystem<SystemUI>()->ReferenceTexture(texture);
        dl->AddImage(ToImTextureID(texture), imageMin, imageMax);
    }

    // While the inline editor is live (including the frame that just ended
    // it) the canvas pointer stays out of the way: the input's own focus
    // rules decide when a click lands on it.
    if (textEditActive_)
    {
        RenderInlineTextEdit(vp);
        DrawOverlay(vp);
        dl->PopClipRect();
        return;
    }

    HandlePreviewDrop(vp);
    // Shortcuts run before the pointer handler: an Esc that cancels a live
    // drag must be consumed by that drag exactly once.
    HandleShortcuts();
    HandlePreviewPointer(vp);
    DrawOverlay(vp);
    dl->PopClipRect();
}

ImVec2 UIViewTab::CanvasOrigin(const ImVec2& canvasMin, const ImVec2& avail) const
{
    const IntVector2 previewSize = document_->GetPreviewSize();
    const ImVec2 displaySize(previewSize.x_ * viewZoom_, previewSize.y_ * viewZoom_);
    // Centered while it fits; the user's pan is added on top.
    return ImVec2(canvasMin.x + (avail.x - displaySize.x) * 0.5f + viewPan_.x_,
                  canvasMin.y + (avail.y - displaySize.y) * 0.5f + viewPan_.y_);
}

void UIViewTab::HandleViewInput(const ImVec2& canvasMin, const ImVec2& avail, bool canvasHovered)
{
    ImGuiIO& io = ui::GetIO();
    const IntVector2 previewSize = document_->GetPreviewSize();

    // Middle-drag pans; the wheel zooms around the pointer. Both only while
    // this window is the front-most one under the cursor, so docked
    // neighbours never fight for the same gesture.
    if (panningActive_)
    {
        if (ui::IsMouseDown(ImGuiMouseButton_Middle))
            viewPan_ = panStartOffset_ + (V2(io.MousePos) - panStartMouse_);
        else
            panningActive_ = false;
        return;
    }

    if (!canvasHovered)
        return;

    if (ui::IsMouseClicked(ImGuiMouseButton_Middle))
    {
        panningActive_ = true;
        panStartMouse_ = V2(io.MousePos);
        panStartOffset_ = viewPan_;
        // A manual pan detaches from "fit": the offset survives a resize.
        viewFit_ = false;
        return;
    }

    // Ctrl+wheel belongs to the editor's global UI zoom (if enabled), not to
    // the canvas.
    if (io.MouseWheel != 0.0f && !io.KeyCtrl)
    {
        const float oldZoom = viewZoom_;
        float newZoom = oldZoom * (io.MouseWheel > 0.0f ? 1.1f : 1.0f / 1.1f);
        newZoom = Clamp(newZoom, 0.1f, 8.0f);
        if (newZoom == oldZoom)
            return;
        // Keep the document point under the cursor stationary: solve the pan
        // that satisfies origin' + doc * newZoom == mouse.
        const ImVec2 oldOrigin = CanvasOrigin(canvasMin, avail);
        const Vector2 docUnderMouse = (V2(io.MousePos) - V2(oldOrigin)) / oldZoom;
        viewFit_ = false;
        viewZoom_ = newZoom;
        const ImVec2 displaySize(previewSize.x_ * newZoom, previewSize.y_ * newZoom);
        viewPan_ = V2(io.MousePos) - V2(canvasMin)
            - Vector2{(avail.x - displaySize.x) * 0.5f, (avail.y - displaySize.y) * 0.5f}
            - docUnderMouse * newZoom;
    }
}

void UIViewTab::ApplyCanvasSize(const IntVector2& size)
{
    sViewCanvasSize_ = size;
    customCanvasW_ = size.x_;
    customCanvasH_ = size.y_;
    if (document_)
        document_->SetPreviewSize(size);
    // A new canvas invalidates a hand-tuned view: re-fit so the whole canvas
    // is visible again.
    viewFit_ = true;
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
        // Esc aborts a gizmo drag: the DOM-only live preview returns to the
        // press box and nothing is committed.
        if (ui::IsKeyPressed(ImGuiKey_Escape))
        {
            CancelDrag();
            return;
        }
        UpdateDrag(vp);
        if (ui::IsMouseReleased(ImGuiMouseButton_Left))
            CommitDrag();
        return;
    }

    // A rubber-band selection is live: track the cursor and finish on release.
    // (Drawing happens in DrawOverlay -> DrawMarquee.)
    if (marqueeActive_)
    {
        marqueeCurDoc_ = doc;
        if (ui::IsMouseReleased(ImGuiMouseButton_Left))
            FinishMarquee(vp);
        return;
    }

    // A structural (flow) drag is live: solve the drop every frame and commit
    // on release. Esc and the right button cancel; a sub-threshold release
    // degrades to a plain click on the pressed node. (Drawing happens in
    // DrawOverlay -> the drag outline / DrawDropIndicator.)
    if (structDragActive_)
    {
        if (ui::IsKeyPressed(ImGuiKey_Escape) || ui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            CancelStructDrag();
            return;
        }
        if (!ui::IsMouseDown(ImGuiMouseButton_Left))
        {
            if (!structDragging_)
            {
                // Never crossed the threshold: behave as a plain click.
                UiNode* node = structDragNode_;
                CancelStructDrag();
                if (overImage)
                    SetSelectedNode(node);
                else
                    SetSelectedNode(nullptr); // an empty click clears
                return;
            }
            ApplyDrop(structDragNode_); // no-op unless a valid slot was solved
            CancelStructDrag();
            return;
        }
        if (!structDragging_)
        {
            const Vector2 startScreen = vp.ToScreen(structDragStartDoc_);
            const Vector2 now = V2(io.MousePos);
            if (fabsf(now.x_ - startScreen.x_) <= 4.0f && fabsf(now.y_ - startScreen.y_) <= 4.0f)
                return; // still a click candidate
            structDragging_ = true;
        }
        EvaluateDrop(structDragNode_, doc, overImage);
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
        // A right-click keeps an existing multi-selection when it lands on one
        // of its members; otherwise it collapses the selection to the picked
        // node (so the context menu acts on what the user just pointed at).
        if (hover && !IsSelected(NodePath(hover)))
            SetSelectedNode(hover);
        ui::OpenPopup("##uiElemCtx");
        return;
    }

    if (ui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        // A double-click on a pure-text element opens the floating inline
        // editor. Hooked on the press (before the gizmo pick and the
        // struct-drag arm): a flow element's second press would otherwise
        // just re-arm the drag candidate and never reach FinishMarquee.
        if (!io.KeyCtrl && ui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            UiNode* element = hover;
            if (element && element->IsText())
                element = document_->GetModel().FindParent(element);
            // The root is the document scaffold, not a widget: a double-click
            // on blank canvas must not open the editor on <body>'s own text.
            if (element == document_->GetModel().root_.Get())
                element = nullptr;
            if (UiNode* textNode = FindPureTextChild(element))
            {
                SetSelectedNode(element);
                BeginInlineTextEdit(textNode);
                return;
            }
        }
        // A gizmo handle on the current primary selection wins over re-picking.
        // The drag box is left/top-space; DocToGizmoFrame shifts the mouse point
        // into that frame with the ancestor transform chain stripped.
        UiBox box;
        Vector2 base;
        if (document_ && document_->TryGetDragBox(selected_, box, base))
        {
            // Constant on-screen grab radius: undo both the preview zoom and any
            // ancestor transform scale captured on the box.
            const float grab = kHandleGrabPx / (vp.scale_ * box.WindowScale());
            const GizmoHandle handle =
                PickGizmoHandle(box, document_->DocToGizmoFrame(selected_, base, doc), grab);
            if (handle.op_ != GizmoOp::None)
            {
                BeginDrag(handle, selected_, vp);
                return;
            }
        }
        // A press on a flow element arms a structural drag candidate: past
        // the threshold the node re-orders / re-parents (see the
        // structDragActive_ branch above). Ctrl keeps the additive-selection
        // marquee path, and absolutely positioned nodes stay with their gizmo.
        UiNode* pressNode = hover;
        if (pressNode && pressNode->IsText())
            pressNode = document_->GetModel().FindParent(pressNode);
        if (!io.KeyCtrl && IsStructDragSource(pressNode))
        {
            structDragActive_ = true;
            structDragging_ = false;
            structDragNode_ = pressNode;
            structDragStartDoc_ = doc;
            hoveredPath_.clear();
            return;
        }
        // Otherwise begin a rubber-band selection from this point. Ctrl makes it
        // additive; a sub-threshold band degrades to a click in FinishMarquee
        // (toggle under Ctrl, plain select / clear otherwise).
        marqueeActive_ = true;
        marqueeAdditive_ = io.KeyCtrl;
        marqueeStartDoc_ = marqueeCurDoc_ = doc;
    }
}

void UIViewTab::FinishMarquee(const DocViewport& vp)
{
    ImGuiIO& io = ui::GetIO();
    marqueeActive_ = false;

    // Decide click vs band from the on-screen extent of the drag.
    const Vector2 a = vp.ToScreen(marqueeStartDoc_);
    const Vector2 b = vp.ToScreen(marqueeCurDoc_);
    const bool isBand = fabsf(b.x_ - a.x_) > 4.0f || fabsf(b.y_ - a.y_) > 4.0f;

    if (!isBand)
    {
        // A click: resolve the node under the release point.
        UiNode* hover = document_->HitTest(vp.ToDoc(V2(io.MousePos)));
        if (marqueeAdditive_)
            ToggleSelectNode(hover);
        else if (hover)
            SetSelectedNode(hover);
        else
            SetSelectedNode(nullptr); // empty click clears
        // Double-click on the template chrome opens the nested file.
        if (hover && hover->IsNestedDoc() && ui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            OpenNestedDocumentFile(context_, document_, hover->nestedDocHref_, /*revealOnly=*/false);
        return;
    }

    // Band: every real element fully enclosed by the rectangle.
    const ea::vector<UiNode*> inside = document_->CollectNodesInRect(marqueeStartDoc_, marqueeCurDoc_);
    if (marqueeAdditive_)
    {
        ea::vector<UiNode*> merged = sels_;
        for (UiNode* n : inside)
        {
            bool dup = false;
            for (UiNode* m : merged)
            {
                if (m == n)
                {
                    dup = true;
                    break;
                }
            }
            if (!dup)
                merged.push_back(n);
        }
        SetSelection(merged);
    }
    else
    {
        SetSelection(inside); // an empty band clears the selection
    }
}

// ---------------------------------------------------------------------------
// Inline text editing (canvas)
// ---------------------------------------------------------------------------

void UIViewTab::BeginInlineTextEdit(UiNode* textNode)
{
    if (!textNode || !textNode->IsText())
        return;
    textEditActive_ = true;
    textEditJustOpened_ = true;
    textEditNode_ = textNode;
    // The child-index path is the node's stable identity across the whole-
    // tree rebuilds every command performs (OnDocumentEdited re-resolves the
    // live pointer from it).
    textEditPath_.clear();
    if (document_)
        document_->GetModel().BuildPath(textNode, textEditPath_);
    // Snapshot into a fixed buffer; longer runs are truncated (the field is a
    // convenience editor, not a text-area replacement).
    snprintf(textEditBuf_, sizeof(textEditBuf_), "%s", textNode->text_.c_str());
}

void UIViewTab::EndInlineTextEdit(bool commit)
{
    if (!textEditActive_)
        return;
    UiNode* textNode = textEditNode_;
    textEditActive_ = false;
    textEditJustOpened_ = false;
    textEditNode_ = nullptr;
    textEditPath_.clear();
    if (!commit || !document_ || !textNode)
        return;
    // Nothing effectively changed (Esc already reverted the buffer): no
    // edit, no undo step.
    if (textEditBuf_ == textNode->text_)
        return;
    UiNodePayload payload = SnapshotUiNodePayload(*textNode);
    payload.text_ = textEditBuf_;
    // The merge key is the text node's path: consecutive edits of the same
    // text run collapse into one undo step where the undo manager's input-
    // frame grouping allows it.
    document_->EditNodePayload(textNode, payload);
}

void UIViewTab::RenderInlineTextEdit(const DocViewport& vp)
{
    UiNode* textNode = textEditNode_;
    UiNode* element = textNode && document_ ? document_->GetModel().FindParent(textNode) : nullptr;
    // The editor floats on the element's rect: the text run itself has no box
    // of its own to anchor to.
    ea::vector<UiBox> boxes;
    if (!element || !document_->TryGetDomBoxes(element, boxes) || boxes.empty())
    {
        EndInlineTextEdit(false);
        return;
    }
    const UiBox& box = boxes.front();
    const ImVec2 min = IV2(vp.ToScreen(box.MapToWindow(box.pos_)));
    const ImVec2 max = IV2(vp.ToScreen(box.MapToWindow(box.pos_ + box.size_)));
    const float width = ea::max(fabsf(max.x - min.x), 48.0f);

    // Font follows the zoom (clamped 11-24 px) so the field reads at roughly
    // the rendered text size. SetWindowFontScale affects this window for the
    // current frame; restore it right after the field.
    const float baseFont = ui::GetFontSize();
    const float fontPx = Clamp(baseFont * vp.scale_, 11.0f, 24.0f);
    ui::SetWindowFontScale(fontPx / baseFont);

    ui::SetCursorScreenPos(min);
    ui::SetNextItemWidth(width);
    if (textEditJustOpened_)
        ui::SetKeyboardFocusHere();
    const bool submit = ui::InputText("##uiViewInlineText", textEditBuf_, sizeof(textEditBuf_),
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    ui::SetWindowFontScale(1.0f);

    // Marker around the element, so the floating field reads as editing it.
    ui::GetWindowDrawList()->AddRect(ImVec2(min.x - 1.0f, min.y - 1.0f),
        ImVec2(max.x + 1.0f, max.y + 1.0f), kTextEditColor, 0.0f, ImDrawFlags_None, 1.0f);

    if (!textEditJustOpened_)
    {
        // Enter (submit) and losing focus (a click elsewhere) both commit;
        // ImGui's built-in Esc revert leaves the buffer equal to the node's
        // text, which EndInlineTextEdit treats as a cancel.
        if (submit || !ui::IsItemActive())
        {
            EndInlineTextEdit(true);
            return;
        }
    }
    textEditJustOpened_ = false;
}

// ---------------------------------------------------------------------------
// Structural commands (wrap / reorder) and canvas shortcuts
// ---------------------------------------------------------------------------

ea::string UIViewTab::WrapUnavailableReason() const
{
    if (!document_ || !document_->GetRmlDocument())
        return "No document open";
    const ea::vector<UiNode*> targets = GetTopLevelSelectedNodes();
    if (targets.empty())
        return "Select at least one element to wrap";
    const UiDocumentModel& model = document_->GetModel();
    UiNode* parent = model.FindParent(targets[0]);
    for (UiNode* node : targets)
    {
        if (node->GetStyle("position") == "absolute")
            return "Selection contains an absolutely positioned element";
        if (model.FindParent(node) != parent)
            return "Selection spans several parents";
    }
    return ea::string();
}

void UIViewTab::WrapSelection(UiWrapMode mode)
{
    if (!document_ || !WrapUnavailableReason().empty())
        return;
    if (UiNode* container = document_->WrapNodes(GetTopLevelSelectedNodes(), mode))
        SetSelectedNode(container);
}

unsigned UIViewTab::FindFlowNeighborIndex(UiNode* node, int delta) const
{
    // Only real flow elements trade slots: the root, the virtual nodes and
    // absolutely positioned elements have no flow position of their own.
    if (!document_ || !node || delta == 0 || !IsStructDragSource(node))
        return M_MAX_UNSIGNED;
    const UiDocumentModel& model = document_->GetModel();
    UiNode* parent = model.FindParent(node);
    if (!parent)
        return M_MAX_UNSIGNED;
    unsigned index = M_MAX_UNSIGNED;
    for (unsigned i = 0; i < parent->children_.size(); i++)
    {
        if (parent->children_[i].Get() == node)
        {
            index = i;
            break;
        }
    }
    if (index == M_MAX_UNSIGNED)
        return M_MAX_UNSIGNED;
    // The neighbor is the nearest ELEMENT sibling: trading slots with a
    // whitespace text run would rewrite bytes without moving anything.
    for (unsigned i = index;;)
    {
        if (delta < 0)
        {
            if (i == 0)
                return M_MAX_UNSIGNED;
            --i;
        }
        else
        {
            ++i;
            if (i >= parent->children_.size())
                return M_MAX_UNSIGNED;
        }
        const SharedPtr<UiNode>& sibling = parent->children_[i];
        if (!sibling->IsText() && !sibling->IsHeadLink() && !sibling->IsNestedDoc())
            return i;
    }
}

bool UIViewTab::CanMoveInFlow(int delta) const
{
    return selected_ && FindFlowNeighborIndex(selected_, delta) != M_MAX_UNSIGNED;
}

void UIViewTab::MoveSelectionInFlow(int delta)
{
    UiNode* node = selected_;
    if (!document_ || !node || delta == 0)
        return;
    UiNode* parent = document_->GetModel().FindParent(node);
    const unsigned neighbor = FindFlowNeighborIndex(node, delta);
    if (!parent || neighbor == M_MAX_UNSIGNED)
        return;
    // MoveNode inserts "before this slot" and shifts a same-parent index that
    // sits behind the removed slot: asking for the neighbour's own slot moves
    // the node in front of it; asking for the slot after it moves the node
    // behind it.
    const unsigned target = delta < 0 ? neighbor : neighbor + 1;
    if (UiNode* moved = document_->MoveNode(node, parent, target))
        SetSelectedNode(moved);
}

void UIViewTab::HandleShortcuts()
{
    ImGuiIO& io = ui::GetIO();
    if (!document_ || !document_->GetRmlDocument())
        return;
    // Any live text input (inspector fields, the inline editor) owns the
    // keyboard: nothing below may steal Delete or letters from it.
    if (io.WantTextInput || textEditActive_)
        return;
    // Live canvas gestures keep their own Esc handling (the pointer path);
    // only the marquee and the pan are cancelled right here, without touching
    // the selection.
    if (dragging_ || structDragActive_ || marqueeActive_ || panningActive_)
    {
        if (ui::IsKeyPressed(ImGuiKey_Escape))
        {
            marqueeActive_ = false; // the stale release selects nothing
            panningActive_ = false;
        }
        return;
    }
    // The shortcuts belong to the canvas: while any other editor window is
    // focused (hierarchy, inspector, console) its keys must stay its own.
    if (!ui::IsWindowFocused(ImGuiFocusedFlags_None))
        return;

    if (ui::IsKeyPressed(ImGuiKey_Escape))
    {
        SetSelectedNode(nullptr); // clear the selection
        return;
    }
    if (io.KeyCtrl && !io.KeyShift && ui::IsKeyPressed(ImGuiKey_D))
    {
        if (!GetTopLevelSelectedNodes().empty())
            CopySelection();
        return;
    }
    if (io.KeyAlt && ui::IsKeyPressed(ImGuiKey_UpArrow))
    {
        MoveSelectionInFlow(-1);
        return;
    }
    if (io.KeyAlt && ui::IsKeyPressed(ImGuiKey_DownArrow))
    {
        MoveSelectionInFlow(1);
        return;
    }
    if (ui::IsKeyPressed(ImGuiKey_Delete))
    {
        if (selected_ && selected_ != document_->GetModel().root_.Get())
            DeleteSelection();
        return;
    }
    if (!io.KeyCtrl && !io.KeyAlt && ui::IsKeyPressed(ImGuiKey_F))
        viewFit_ = true;
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
    gizmoPressDoc_ = gizmoCurDoc_ =
        document_->DocToGizmoFrame(node, gizmoBase_, vp.ToDoc(V2(ui::GetIO().MousePos)));
    gizmoLiveBox_ = gizmoStartBox_;
    dragging_ = true;

    // Multi-move: capture every other top-level selected node's start box so the
    // same drag delta can be previewed live and committed in one undo step.
    // Only Move propagates; resize / rotate / scale act on the primary alone.
    extraDragNodes_.clear();
    extraDragStarts_.clear();
    if (handle.op_ == GizmoOp::Move)
    {
        for (UiNode* n : GetTopLevelSelectedNodes())
        {
            if (n == node)
                continue;
            UiBox b;
            Vector2 b2;
            if (document_->TryGetDragBox(n, b, b2))
            {
                extraDragNodes_.push_back(n);
                extraDragStarts_.push_back(b);
            }
        }
    }
}

void UIViewTab::UpdateDrag(const DocViewport& vp)
{
    gizmoCurDoc_ = document_->DocToGizmoFrame(gizmoNode_, gizmoBase_, vp.ToDoc(V2(ui::GetIO().MousePos)));
    const UiBox solved = SolveDrag(gizmoStartBox_, gizmoDrag_, gizmoPressDoc_, gizmoCurDoc_);
    gizmoLiveBox_ = solved;

    // Live preview into the DOM projection; the model is only touched on
    // release. Layout re-flows next E_POSTUPDATE (Context::Update).
    document_->SetLiveBox(gizmoNode_, solved);

    if (gizmoDrag_.op_ == GizmoOp::Move && !extraDragNodes_.empty())
    {
        // Same layout-space translation as the primary (its base cancels out of
        // pos_-pos_, so this is exact for siblings sharing a containing block).
        const Vector2 delta = solved.pos_ - gizmoStartBox_.pos_;
        for (size_t i = 0; i < extraDragNodes_.size(); i++)
        {
            UiBox b = extraDragStarts_[i];
            b.pos_ += delta;
            document_->SetLiveBox(extraDragNodes_[i], b);
        }
    }
}

void UIViewTab::CommitDrag()
{
    if (gizmoNode_)
    {
        const UiBox solved = SolveDrag(gizmoStartBox_, gizmoDrag_, gizmoPressDoc_, gizmoCurDoc_);
        if (gizmoDrag_.op_ == GizmoOp::Move && !extraDragNodes_.empty())
        {
            const Vector2 delta = solved.pos_ - gizmoStartBox_.pos_;
            ea::vector<ea::pair<UiNode*, UiBox>> edits;
            edits.push_back(ea::make_pair(gizmoNode_, solved));
            for (size_t i = 0; i < extraDragNodes_.size(); i++)
            {
                UiBox b = extraDragStarts_[i];
                b.pos_ += delta;
                edits.push_back(ea::make_pair(extraDragNodes_[i], b));
            }
            document_->CommitBoxEdits(edits); // one undo step for the whole group
        }
        else
        {
            document_->CommitBoxEdit(gizmoNode_, solved);
        }
    }
    dragging_ = false;
    gizmoNode_ = nullptr;
    extraDragNodes_.clear();
    extraDragStarts_.clear();
}

void UIViewTab::CancelDrag()
{
    // Esc during a gizmo drag: put the DOM-only live preview back to the
    // press box. The drag never touched the model, so there is no undo step
    // to roll back.
    if (gizmoNode_)
        document_->SetLiveBox(gizmoNode_, gizmoStartBox_);
    dragging_ = false;
    gizmoNode_ = nullptr;
    extraDragNodes_.clear();
    extraDragStarts_.clear();
}

// ---------------------------------------------------------------------------
// Structural (flow) drag / drop
// ---------------------------------------------------------------------------

bool UIViewTab::IsStructDragSource(const UiNode* node) const
{
    if (!node || !document_)
        return false;
    // Root IS the document, the virtual link nodes live in <head>, and an
    // absolutely positioned element belongs to its gizmo (dragging it means
    // writing left/top, not reflowing the document).
    return node != document_->GetModel().root_.Get() && !node->IsNestedDoc()
        && !node->IsHeadLink() && !node->IsText()
        && node->GetStyle("position") != "absolute";
}

void UIViewTab::CancelStructDrag()
{
    structDragActive_ = false;
    structDragging_ = false;
    structDragNode_ = nullptr;
    structDragStartDoc_ = Vector2::ZERO;
    dropKind_ = 0;
    dropParent_ = nullptr;
    dropIndex_ = 0;
    dropHaveHitBox_ = false;
}

void UIViewTab::EvaluateDrop(UiNode* source, const Vector2& mouseDoc, bool overCanvas)
{
    // Reset to "no feedback"; everything below either fills the state or
    // leaves it empty (drawing and ApplyDrop both key on dropKind_).
    dropKind_ = 0;
    dropParent_ = nullptr;
    dropIndex_ = 0;
    dropHaveHitBox_ = false;
    dropSiblingAxisIsRow_ = false;
    dropChildAxisIsRow_ = false;
    dropChildBoxes_.clear();
    dropChildIndices_.clear();
    if (!overCanvas || !document_ || !document_->GetRmlDocument())
        return;

    const UiDocumentModel& model = document_->GetModel();
    UiNode* hit = document_->HitTest(mouseDoc);
    if (hit && hit->IsText())
        hit = model.FindParent(hit);
    // A hit inside the dragged subtree is not a target: fall back to the
    // subtree's parent, so hovering the node still offers a slot beside it.
    if (hit && source && IsInSubtree(model, source, hit))
        hit = model.FindParent(source);
    if (!hit || hit->IsHeadLink() || (source && hit == source))
        return;

    // The hit's own border box (the nested-doc virtual node projects onto its
    // chrome; a template-less document renders no chrome and offers no drop).
    UiBox hitBox;
    {
        ea::vector<UiBox> boxes;
        if (!document_->TryGetDomBoxes(hit, boxes) || boxes.empty())
            return;
        hitBox = boxes.front();
        if (hitBox.size_.x_ <= 0.0f || hitBox.size_.y_ <= 0.0f)
            return;
    }

    DropTargetFacts facts;
    facts.rectMin_ = hitBox.pos_;
    facts.rectMax_ = hitBox.pos_ + hitBox.size_;
    facts.childAxis_ = FlowAxisOf(hit);
    UiNode* hitParent = model.FindParent(hit);
    facts.siblingAxis_ = FlowAxisOf(hitParent);
    facts.selfIndex_ = ChildIndexOf(hitParent, hit);
    facts.childCount_ = static_cast<unsigned>(hit->children_.size());
    const bool isRoot = hit == model.root_.Get();
    // Only block-level containers (or nodes that already hold element
    // children) take a drop inside; the root always does.
    unsigned elementChildren = 0;
    for (const SharedPtr<UiNode>& child : hit->children_)
    {
        if (!child->IsText() && !child->IsHeadLink())
            ++elementChildren;
    }
    facts.hostable_ = !hit->IsNestedDoc()
        && (isRoot || elementChildren > 0 || IsFlowContainerTag(hit->tag_));
    facts.forceInside_ = isRoot;
    // The children's slot geometry: full indices + centers on the child axis.
    for (unsigned i = 0; i < hit->children_.size(); i++)
    {
        const SharedPtr<UiNode>& child = hit->children_[i];
        if (child->IsText() || child->IsHeadLink())
            continue;
        ea::vector<UiBox> boxes;
        if (!document_->TryGetDomBoxes(child.Get(), boxes) || boxes.empty())
            continue;
        const UiBox& box = boxes.front();
        if (box.size_.x_ <= 0.0f || box.size_.y_ <= 0.0f)
            continue;
        const Vector2 center = box.Center();
        facts.children_.push_back(DropChild{
            facts.childAxis_ == DropAxis::Horizontal ? center.x_ : center.y_, i});
        dropChildBoxes_.push_back(box);
        dropChildIndices_.push_back(i);
    }

    const DropSolution solution = SolveFlowDrop(mouseDoc, facts);
    switch (solution.kind_)
    {
    case DropSolution::Kind::Inside:
        dropKind_ = 3;
        dropParent_ = hit;
        break;
    case DropSolution::Kind::Before:
    case DropSolution::Kind::After:
        if (!hitParent || facts.selfIndex_ == M_MAX_UNSIGNED)
            return;
        dropKind_ = solution.kind_ == DropSolution::Kind::Before ? 1 : 2;
        dropParent_ = hitParent;
        break;
    default:
        return;
    }
    dropIndex_ = solution.index_;
    dropHitBox_ = hitBox;
    dropHaveHitBox_ = true;
    dropSiblingAxisIsRow_ = facts.siblingAxis_ == DropAxis::Horizontal;
    dropChildAxisIsRow_ = facts.childAxis_ == DropAxis::Horizontal;

    // The final safety net: a drop into the dragged subtree is a cycle that
    // MoveNode would reject - never promise it.
    if (source && IsInSubtree(model, source, dropParent_))
    {
        dropKind_ = 0;
        dropParent_ = nullptr;
        dropHaveHitBox_ = false;
    }
}

void UIViewTab::ApplyDrop(UiNode* source)
{
    if (!document_ || !source || dropKind_ == 0 || !dropParent_)
        return;
    const UiDocumentModel& model = document_->GetModel();
    // Inserting at the node's own slot (or right after it) changes nothing:
    // skip the no-op instead of pushing an empty undo step.
    UiNode* oldParent = model.FindParent(source);
    const unsigned oldIndex = ChildIndexOf(oldParent, source);
    if (dropParent_ == oldParent && (dropIndex_ == oldIndex || dropIndex_ == oldIndex + 1))
        return;
    if (UiNode* moved = document_->MoveNode(source, dropParent_, dropIndex_))
        SetSelectedNode(moved);
}

void UIViewTab::ApplyResourceDrop(const ResourceFileDescriptor& desc)
{
    if (!document_ || dropKind_ == 0 || !dropParent_)
        return;
    UiWidgetSpec spec;
    spec.tag_ = "img";
    spec.attrName_ = "src";
    spec.attrValue_ = MakeDocRelativePath(document_->GetSourcePath(), desc.resourceName_);
    // Same placeholder policy as the palette's img: a box that marks the
    // element until the project stylesheet has rules for it.
    spec.stylePolicy_ = UiWidgetStylePolicy::Panel;
    spec.materialize_ = false;
    // Explicit pixel size: RmlUi has no intrinsic image size, an unsized
    // <img> collapses to nothing. Fall back when the texture cannot be read.
    Vector2 size{160.0f, 48.0f};
    if (auto* cache = GetSubsystem<ResourceCache>())
    {
        if (Texture2D* texture = cache->GetResource<Texture2D>(desc.resourceName_))
        {
            size = Vector2{static_cast<float>(texture->GetWidth()),
                static_cast<float>(texture->GetHeight())};
        }
    }
    spec.flowSize_ = size;
    if (UiNode* node = document_->AddWidget(dropParent_, spec, dropIndex_))
        SetSelectedNode(node);
}

void UIViewTab::HandlePreviewDrop(const DocViewport& vp)
{
    // The whole canvas is one drop target: node moves dragged out of the
    // hierarchy, and single image files from the Resource Browser or the OS.
    // The preview item itself is a zero-ID Image that ImGui's item-based
    // targeting cannot key on, so the target is custom: the image rect plus
    // an explicit id.
    const IntVector2 previewSize = document_->GetPreviewSize();
    const ImVec2 min = IV2(vp.origin_);
    const ImVec2 max(min.x + previewSize.x_ * vp.scale_, min.y + previewSize.y_ * vp.scale_);
    if (!ui::BeginDragDropTargetCustom(ImRect(min, max), ui::GetID("UIViewPreview")))
        return;

    const Vector2 doc = vp.ToDoc(V2(ui::GetIO().MousePos));

    // Hierarchy rows carry [document id, child-index path]: one structural
    // drop, solved per frame so the indicator previews what a release commits.
    if (const ImGuiPayload* payload = ui::AcceptDragDropPayload(kUiNodeDragType,
        ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
    {
        ea::vector<unsigned> srcPath;
        UiNode* src = ParseNodeDragData(payload, document_, srcPath)
            ? document_->GetModel().ResolvePath(srcPath)
            : nullptr;
        if (src)
        {
            EvaluateDrop(src, doc, true);
            if (ui::GetDragDropPayload()->IsDelivery())
            {
                ApplyDrop(src);
                dropKind_ = 0; // the model was rebuilt; the feedback is spent
            }
        }
    }
    // System payloads (Resource Browser / OS file drags): a single image
    // becomes an <img>; other payloads have no authored meaning here (yet).
    else if (auto* resPayload = dynamic_cast<ResourceDragDropPayload*>(DragDropPayload::Get()))
    {
        if (IsSupportedImageDrop(*resPayload))
        {
            if (ui::AcceptDragDropPayload(DragDropPayloadType.c_str(),
                ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
            {
                EvaluateDrop(nullptr, doc, true);
                if (ui::GetDragDropPayload()->IsDelivery())
                {
                    ApplyResourceDrop(resPayload->resources_.front());
                    dropKind_ = 0; // the model was rebuilt; the feedback is spent
                }
            }
        }
    }

    ui::EndDragDropTarget();
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
    // MapToWindow runs the point through the element's FULL accumulated
    // transform (own + ancestors), captured from the live DOM.
    for (int i = 0; i < 4; i++)
        out[i] = IV2(vp.ToScreen(box.MapToWindow(corners[i])));
}

void DrawHandleSquare(ImDrawList* dl, const ImVec2& center, ImU32 fill)
{
    const ImVec2 a(center.x - kHandleDrawPx, center.y - kHandleDrawPx);
    const ImVec2 b(center.x + kHandleDrawPx, center.y + kHandleDrawPx);
    dl->AddRectFilled(a, b, fill);
    dl->AddRect(a, b, kGizmoHandleBorder);
}

// A small dark chip with text, used by the hover tag and the box-model
// readouts. \a pos is the top-left of the text; the chip pads it by 2px.
void DrawChip(ImDrawList* dl, const ImVec2& pos, const char* text)
{
    const ImVec2 size = ui::CalcTextSize(text);
    dl->AddRectFilled(ImVec2(pos.x - 2.0f, pos.y - 2.0f), ImVec2(pos.x + size.x + 2.0f, pos.y + size.y + 2.0f), kBoxLabelBg);
    dl->AddText(pos, kBoxLabelText, text);
}

// The rectangle corner closest to the top of the screen; labels hang off it
// so a transformed (rotated) box cannot push its own label over its content.
ImVec2 TopmostCorner(const ImVec2 c[4])
{
    ImVec2 top = c[0];
    for (int i = 1; i < 4; i++)
    {
        if (c[i].y < top.y)
            top = c[i];
    }
    return top;
}

// Tint the four edge strips of the band between two layout rectangles (the
// CSS box-model look): only the space itself is filled, so the inner area
// keeps showing the document.
void FillBand(const DocViewport& vp, ImDrawList* dl, const UiBox& outer, const UiBox& inner, ImU32 color)
{
    const float ox = outer.pos_.x_, oy = outer.pos_.y_;
    const float ow = outer.size_.x_, oh = outer.size_.y_;
    const float ix = inner.pos_.x_, iy = inner.pos_.y_;
    const float iw = inner.size_.x_, ih = inner.size_.y_;
    const struct { float x_, y_, w_, h_; } strips[4] = {
        {ox, oy, ow, iy - oy},                    // top
        {ox, iy + ih, ow, (oy + oh) - (iy + ih)}, // bottom
        {ox, iy, ix - ox, ih},                    // left
        {ix + iw, iy, (ox + ow) - (ix + iw), ih}, // right
    };
    for (const auto& s : strips)
    {
        if (s.w_ <= 0.0f || s.h_ <= 0.0f)
            continue;
        UiBox strip = outer;
        strip.pos_ = Vector2{s.x_, s.y_};
        strip.size_ = Vector2{s.w_, s.h_};
        ImVec2 c[4];
        TransformedCorners(vp, strip, c);
        dl->AddConvexPolyFilled(c, 4, color);
    }
}

// Box-model overlay of the primary selection: margin / border / padding
// bands, the border-box size, and per-edge values on bands thick enough
// (>= 10 screen px) to label.
void DrawBoxModelOverlay(const DocViewport& vp, const UiBoxModel& model)
{
    ImDrawList* dl = ui::GetWindowDrawList();
    FillBand(vp, dl, model.margin_, model.border_, kBoxMarginFill);
    FillBand(vp, dl, model.border_, model.padding_, kBoxBorderFill);
    FillBand(vp, dl, model.padding_, model.content_, kBoxPaddingFill);

    ImVec2 border[4];
    TransformedCorners(vp, model.border_, border);
    const ImVec2 top = TopmostCorner(border);
    const ea::string sizeText = Format("%d × %d", (int)lroundf(model.border_.size_.x_), (int)lroundf(model.border_.size_.y_));
    DrawChip(dl, ImVec2(top.x, top.y - ui::GetTextLineHeight() - 6.0f), sizeText.c_str());

    // Per-edge readouts, anchored at each band's midpoint; [0]=left, [1]=top,
    // [2]=right, [3]=bottom. Values are layout px.
    const float proj = vp.scale_ * model.border_.WindowScale();
    const auto drawBand = [&](const UiBox& outer, const UiBox& inner) {
        const float left = inner.pos_.x_ - outer.pos_.x_;
        const float topEdge = inner.pos_.y_ - outer.pos_.y_;
        const float right = (outer.pos_.x_ + outer.size_.x_) - (inner.pos_.x_ + inner.size_.x_);
        const float bottom = (outer.pos_.y_ + outer.size_.y_) - (inner.pos_.y_ + inner.size_.y_);
        const float midX = inner.pos_.x_ + inner.size_.x_ * 0.5f;
        const float midY = inner.pos_.y_ + inner.size_.y_ * 0.5f;
        const struct { float value_; Vector2 at_; } readouts[4] = {
            {left, Vector2{(outer.pos_.x_ + inner.pos_.x_) * 0.5f, midY}},
            {topEdge, Vector2{midX, (outer.pos_.y_ + inner.pos_.y_) * 0.5f}},
            {right, Vector2{((outer.pos_.x_ + outer.size_.x_) + (inner.pos_.x_ + inner.size_.x_)) * 0.5f, midY}},
            {bottom, Vector2{midX, ((outer.pos_.y_ + outer.size_.y_) + (inner.pos_.y_ + inner.size_.y_)) * 0.5f}},
        };
        for (const auto& r : readouts)
        {
            if (r.value_ * proj < 10.0f)
                continue;
            const ea::string text = Format("%d", (int)lroundf(r.value_));
            const ImVec2 size = ui::CalcTextSize(text.c_str());
            const ImVec2 at = IV2(vp.ToScreen(outer.MapToWindow(r.at_)));
            DrawChip(dl, ImVec2(at.x - size.x * 0.5f, at.y - size.y * 0.5f), text.c_str());
        }
    };
    drawBand(model.margin_, model.border_);
    drawBand(model.padding_, model.content_);
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

            // DevTools-style hover tag: what this element is and how big it
            // renders, so nested flow containers stay identifiable without
            // selecting anything.
            if (!hover->IsNestedDoc() && !boxes.empty() && boxes.front().size_.x_ > 0.0f)
            {
                ea::string label = hover->tag_;
                if (!hover->classes_.empty())
                {
                    label += ".";
                    for (char ch : hover->classes_)
                        label += ch == ' ' ? '.' : ch;
                }
                label += Format(" %d × %d", (int)lroundf(boxes.front().size_.x_), (int)lroundf(boxes.front().size_.y_));
                ImVec2 c[4];
                TransformedCorners(vp, boxes.front(), c);
                const ImVec2 top = TopmostCorner(c);
                DrawChip(dl, ImVec2(top.x, top.y - ui::GetTextLineHeight() - 6.0f), label.c_str());
            }
        }
    }

    // Non-primary selected nodes: outline each one so a multi-selection reads
    // as a set. The primary (last) gets the full-weight outline + gizmo below.
    for (UiNode* n : sels_)
    {
        if (!n || n == sel || !n->dom_)
            continue;
        ea::vector<UiBox> boxes;
        if (document_->TryGetDomBoxes(n, boxes))
        {
            for (const UiBox& box : boxes)
            {
                if (box.size_.x_ <= 0.0f || box.size_.y_ <= 0.0f)
                    continue;
                ImVec2 c[4];
                TransformedCorners(vp, box, c);
                dl->AddConvexPolyFilled(c, 4, kSelectFill);
                dl->AddPolyline(c, 4, kSelectColor, ImDrawFlags_Closed, 1.5f);
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
            // Keep the layout->window map live: rotate/scale gestures change
            // the element's own transform mid-drag and the overlay must follow.
            document_->RefreshWindowMap(gizmoNode_, box);
            boxes.push_back(box);
        }
        else
            document_->TryGetDomBoxes(sel, boxes);

        // In a flow layout the interesting facts are where margin, border and
        // padding put the element, so the box-model bands replace the flat fill
        // for the primary selection. While a gizmo drag is live the box is a
        // DOM-preview snapshot with no reliable model, and the nested-doc
        // virtual node has no box model at all: both keep the fill.
        UiBoxModel boxModel;
        const bool withBoxModel = !dragging && !structDragging_
            && document_->TryGetBoxModel(sel, boxModel);
        if (withBoxModel)
            DrawBoxModelOverlay(vp, boxModel);

        for (const UiBox& box : boxes)
        {
            if (box.size_.x_ <= 0.0f || box.size_.y_ <= 0.0f)
                continue;
            ImVec2 c[4];
            TransformedCorners(vp, box, c);
            if (!withBoxModel)
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

    // While a structural drag is live, outline the node being moved so the
    // dragged subject stays identifiable wherever the pointer wanders.
    if (structDragging_ && structDragNode_ && structDragNode_->dom_)
    {
        ea::vector<UiBox> dragBoxes;
        if (document_->TryGetDomBoxes(structDragNode_, dragBoxes))
        {
            for (const UiBox& box : dragBoxes)
            {
                ImVec2 c[4];
                TransformedCorners(vp, box, c);
                dl->AddPolyline(c, 4, kDropColor, ImDrawFlags_Closed, 2.0f);
            }
        }
    }

    // One indicator for every drop feedback source: the in-canvas drag and
    // the external payloads both fill the same state (dropKind_).
    DrawDropIndicator(vp);

    DrawMarquee(vp);
}

void UIViewTab::DrawMarquee(const DocViewport& vp)
{
    if (!marqueeActive_)
        return;
    ImDrawList* dl = ui::GetWindowDrawList();
    const ImVec2 a = IV2(vp.ToScreen(marqueeStartDoc_));
    const ImVec2 b = IV2(vp.ToScreen(marqueeCurDoc_));
    dl->AddRectFilled(a, b, kMarqueeFill);
    dl->AddRect(a, b, kMarqueeColor, 0.0f, ImDrawFlags_None, 1.0f);
}

void UIViewTab::DrawDropIndicator(const DocViewport& vp)
{
    if (dropKind_ == 0 || !dropHaveHitBox_)
        return;
    ImDrawList* dl = ui::GetWindowDrawList();
    const UiBox& box = dropHitBox_;
    // Keep a slot line 2 screen px clear of the neighbor it hugs, so it reads
    // as a gap rather than as part of that neighbor's outline.
    const float scale = vp.scale_ > 0.001f ? vp.scale_ : 0.001f;
    const float pad = 2.0f / scale;

    if (dropKind_ == 3)
    {
        // Inside: tint the receiving container and outline it, then draw the
        // insertion slot itself.
        ImVec2 c[4];
        TransformedCorners(vp, box, c);
        dl->AddConvexPolyFilled(c, 4, kDropFill);
        dl->AddPolyline(c, 4, kDropColor, ImDrawFlags_Closed, 2.0f);

        // The slot sits right after the child that precedes it on the child
        // axis, or right before the one that follows it; an empty container
        // (or one whose children all failed to measure) gets a centered line.
        const UiBox* before = nullptr;
        const UiBox* after = nullptr;
        for (size_t i = 0; i < dropChildBoxes_.size(); i++)
        {
            if (dropChildIndices_[i] < dropIndex_)
                before = &dropChildBoxes_[i];
            else if (!after)
                after = &dropChildBoxes_[i];
        }
        Vector2 a, b;
        if (before || after)
        {
            const UiBox& ref = before ? *before : *after;
            if (dropChildAxisIsRow_)
            {
                const float x = before ? ref.pos_.x_ + ref.size_.x_ + pad : ref.pos_.x_ - pad;
                a = Vector2{x, ref.pos_.y_};
                b = Vector2{x, ref.pos_.y_ + ref.size_.y_};
            }
            else
            {
                const float y = before ? ref.pos_.y_ + ref.size_.y_ + pad : ref.pos_.y_ - pad;
                a = Vector2{ref.pos_.x_, y};
                b = Vector2{ref.pos_.x_ + ref.size_.x_, y};
            }
            dl->AddLine(IV2(vp.ToScreen(ref.MapToWindow(a))), IV2(vp.ToScreen(ref.MapToWindow(b))),
                kDropColor, 3.0f);
        }
        else
        {
            if (dropChildAxisIsRow_)
            {
                const float x = box.pos_.x_ + box.size_.x_ * 0.5f;
                a = Vector2{x, box.pos_.y_};
                b = Vector2{x, box.pos_.y_ + box.size_.y_};
            }
            else
            {
                const float y = box.pos_.y_ + box.size_.y_ * 0.5f;
                a = Vector2{box.pos_.x_, y};
                b = Vector2{box.pos_.x_ + box.size_.x_, y};
            }
            dl->AddLine(IV2(vp.ToScreen(box.MapToWindow(a))), IV2(vp.ToScreen(box.MapToWindow(b))),
                kDropColor, 3.0f);
        }
        return;
    }

    // Before / After: a thick line along the hit node's leading (1) or
    // trailing (2) edge on its parent's flow axis, spanning the hit's extent
    // on the cross axis.
    Vector2 a, b;
    if (dropSiblingAxisIsRow_)
    {
        const float x = dropKind_ == 1 ? box.pos_.x_ - pad : box.pos_.x_ + box.size_.x_ + pad;
        a = Vector2{x, box.pos_.y_};
        b = Vector2{x, box.pos_.y_ + box.size_.y_};
    }
    else
    {
        const float y = dropKind_ == 1 ? box.pos_.y_ - pad : box.pos_.y_ + box.size_.y_ + pad;
        a = Vector2{box.pos_.x_, y};
        b = Vector2{box.pos_.x_ + box.size_.x_, y};
    }
    dl->AddLine(IV2(vp.ToScreen(box.MapToWindow(a))), IV2(vp.ToScreen(box.MapToWindow(b))),
        kDropColor, 3.0f);
}

void UIViewTab::DrawGizmo(const DocViewport& vp, const UiBox& box)
{
    ImDrawList* dl = ui::GetWindowDrawList();
    ImVec2 c[4];
    TransformedCorners(vp, box, c);

    auto anchorScreen = [&vp, &box](const GizmoHandle& h) {
        return IV2(vp.ToScreen(box.MapToWindow(
            Vector2{box.pos_.x_ + h.u_ * box.size_.x_, box.pos_.y_ + h.v_ * box.size_.y_})));
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

    const bool selected = tab->IsSelected(path);
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
    {
        // Ctrl-click toggles membership so several rows can be selected at once;
        // a plain click replaces the selection with this single row.
        if (ui::GetIO().KeyCtrl)
            tab->ToggleSelectNode(node);
        else
            tab->SetSelectedNode(node);
    }
    // Row context menu. The target is recorded at release time: the press may
    // have started on a different row, and ImGui opens the popup where the
    // button is released. Store the path, not the pointer: commands rebuild
    // the whole tree. The popup is opened at the end of RenderContent (see
    // there for why the request is deferred instead of opened right here).
    if (ui::IsItemHovered() && ui::IsMouseReleased(ImGuiMouseButton_Right))
    {
        // Keep a multi-selection when the click lands on one of its members;
        // otherwise collapse the selection to the right-clicked row.
        if (!tab->IsSelected(path))
            tab->SetSelectedNode(node);
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
        // (copied) by SetDragDropPayload within the same frame. The data is
        // [source document id, child-index path], so a target can reject a
        // drag that originated in another tab's document.
        ea::vector<unsigned> dragData = MakeNodeDragData(tab->GetDocument(), path);
        ui::SetDragDropPayload(kUiNodeDragType, dragData.data(),
            dragData.size() * sizeof(unsigned));
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
            // The payload is [source document id, child-index path]: resolve
            // it against this model only when the id matches.
            UIViewDocument* doc = tab->GetDocument();
            ea::vector<unsigned> srcPath;
            UiNode* src = ParseNodeDragData(payload, doc, srcPath)
                ? doc->GetModel().ResolvePath(srcPath)
                : nullptr;
            if (src)
            {
                if (UiNode* moved = doc->MoveNode(src, node,
                    static_cast<unsigned>(node->children_.size())))
                {
                    tab->SetSelectedNode(moved);
                }
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
        const int count = static_cast<int>(tab->GetTopLevelSelectedNodes().size());
        if (count > 1)
        {
            if (ui::MenuItem(Format(ICON_FA_COPY " Copy %d Items", count).c_str()))
                tab->CopySelection();
            if (ui::MenuItem(Format(ICON_FA_TRASH " Delete %d Items", count).c_str()))
                tab->DeleteSelection();
        }
        else
        {
            if (ui::MenuItem(ICON_FA_COPY " Copy"))
            {
                tab->SetSelectedNode(target);
                tab->CopySelection();
            }
            if (ui::MenuItem(ICON_FA_TRASH " Delete"))
            {
                tab->SetSelectedNode(target);
                tab->DeleteSelection();
            }
        }

        // Structural flow commands mirror the canvas context menu: the same
        // tab entry points and enablement rules (the right-click collapsed
        // the selection to the target unless it was part of one).
        ui::Separator();
        const ea::string wrapReason = tab->WrapUnavailableReason();
        ui::BeginDisabled(!wrapReason.empty());
        if (ui::BeginMenu(ICON_FA_OBJECT_GROUP " Wrap in"))
        {
            if (ui::MenuItem("Row"))
                tab->WrapSelection(UiWrapMode::Row);
            if (ui::MenuItem("Column"))
                tab->WrapSelection(UiWrapMode::Column);
            if (ui::MenuItem("Box"))
                tab->WrapSelection(UiWrapMode::Box);
            ui::EndMenu();
        }
        ui::EndDisabled();
        if (!wrapReason.empty())
            ui::SetItemTooltip("%s", wrapReason.c_str());
        const bool canMoveUp = tab->CanMoveInFlow(-1);
        const bool canMoveDown = tab->CanMoveInFlow(1);
        ui::BeginDisabled(!canMoveUp);
        if (ui::MenuItem(ICON_FA_ARROW_UP " Move Up", "Alt+Up"))
            tab->MoveSelectionInFlow(-1);
        ui::EndDisabled();
        if (!canMoveUp)
            ui::SetItemTooltip("No element sibling above");
        ui::BeginDisabled(!canMoveDown);
        if (ui::MenuItem(ICON_FA_ARROW_DOWN " Move Down", "Alt+Down"))
            tab->MoveSelectionInFlow(1);
        ui::EndDisabled();
        if (!canMoveDown)
            ui::SetItemTooltip("No element sibling below");
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
    if (auto picked = ResourceBrowseWidget("##hlAddBrowse", context_, "", "rcss,rml"))
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
    // inline-style and paragraph-content texts must be reseeded from the new
    // node on the next render.
    styleSeedValid_ = false;
    contentSeedValid_ = false;
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
    const auto pickedHref = ResourceBrowseWidget("##nestedBrowse", context_, node->nestedDocHref_, "rml");
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
    const auto pickedHref = ResourceBrowseWidget("##hlEditBrowse", context_, href, isTemplate ? "rml" : "rcss");
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

namespace
{

/// std::string copy of an eastl string (the paragraph unit is std-only).
std::string Std(const ea::string& s)
{
    return std::string(s.c_str(), s.length());
}

/// True when the node's computed white-space keeps newlines (the pre kinds):
/// such paragraphs already render raw \n as line breaks, so their raw editor
/// stays the WYSIWYG form and the structural <br/> editor is not offered.
bool ParagraphUsesPreWhitespace(const UiNode& node)
{
    if (node.dom_)
    {
        const Rml::Style::WhiteSpace ws = node.dom_->GetComputedValues().white_space();
        return ws == Rml::Style::WhiteSpace::Pre || ws == Rml::Style::WhiteSpace::Prewrap
            || ws == Rml::Style::WhiteSpace::Preline;
    }
    // No live projection: fall back to the authored inline declaration.
    return LowerCopy(node.GetStyle("white-space")).find("pre") == 0;
}

/// Read the editable paragraph shape (children are text runs and bare <br/>
/// only) into the editor's normalized lines. False for anything else.
bool TryGetParagraphLines(const UiNode& host, std::vector<std::string>& lines)
{
    std::vector<ParagraphChildState> children;
    children.reserve(host.children_.size());
    for (const SharedPtr<UiNode>& child : host.children_)
    {
        ParagraphChildState state;
        state.srcNode = child->srcNode_;
        state.isText = child->IsText();
        if (state.isText)
        {
            state.text = Std(child->text_);
        }
        else if (child->tag_ != "br" || !child->attributes_.empty() || !child->style_.empty()
            || !child->id_.empty() || !child->classes_.empty() || !child->children_.empty())
        {
            return false;
        }
        children.push_back(std::move(state));
    }
    if (children.empty())
        return false;
    return ParagraphLinesOfChildren(children, lines);
}

} // namespace

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

    // A <p> whose children are text runs and bare <br/> takes the structural
    // multi-line editor: one buffer line per run, and Enter covers a line break
    // by inserting a <br/> on commit (under white-space: normal a raw \n would
    // collapse to a space). pre-formatted paragraphs keep the raw editor -
    // their \n already renders as breaks, so that form is their WYSIWYG one.
    const UiDocumentModel& model = tab->GetDocument()->GetModel();
    UiNode* host = node->IsText() ? model.FindParent(node) : node;
    const bool paragraph = host && LowerCopy(host->tag_) == "p";
    std::vector<std::string> paraLines;
    std::string paraBuffer;
    bool structural = false;
    if (paragraph && !ParagraphUsesPreWhitespace(*host) && TryGetParagraphLines(*host, paraLines))
    {
        paraBuffer = JoinParagraphLines(paraLines);
        // The fixed editor buffer must hold the whole paragraph; longer ones
        // fall back rather than committing a truncated rewrite.
        structural = paraBuffer.size() + 1 <= sizeof(contentBuf_);
    }

    if (!textNode && !structural)
        return false;

    if (!ui::CollapsingHeader(ICON_FA_FONT " Content", ImGuiTreeNodeFlags_DefaultOpen))
        return false;

    if (structural)
    {
        // Same seed-per-selection + explicit-Apply model as the raw inline
        // style editor: ImGui keeps its own buffer while the field is live, so
        // the lines are only re-read when the selection changes or a rebuild
        // invalidated the cache.
        const ea::vector<unsigned> curPath = tab->GetSelectedPath();
        if (!contentSeedValid_ || curPath != lastContentPath_)
        {
            snprintf(contentBuf_, sizeof(contentBuf_), "%s", paraBuffer.c_str());
            lastContentPath_ = curPath;
            contentSeedValid_ = true;
        }

        ui::InputTextMultiline("##textContent", contentBuf_, sizeof(contentBuf_), ImVec2(-1.0f, 120.0f));
        if (ui::Button(ICON_FA_CHECK " Apply Content"))
        {
            contentSeedValid_ = false;
            // An untouched buffer commits nothing (no edit, no undo step).
            if (NormalizedParagraphLines(contentBuf_) != paraLines)
            {
                tab->GetDocument()->SetParagraphText(host, ea::string(contentBuf_));
                return true; // the model was rebuilt; the nodes are dangling now
            }
        }
        return false;
    }

    if (!textNode)
        return false;

    if (paragraph)
    {
        // A pre-formatted paragraph keeps the raw multi-line editor: its \n is
        // rendered as authored, so the text is edited verbatim.
        if (textNode->text_.length() + 1 > sizeof(contentBuf_))
        {
            ui::TextDisabled("Text is too long for the inline editor.");
            return false;
        }
        // Same seed-per-selection + explicit-Apply model as the structural
        // editor above.
        const ea::vector<unsigned> curPath = tab->GetSelectedPath();
        if (!contentSeedValid_ || curPath != lastContentPath_)
        {
            snprintf(contentBuf_, sizeof(contentBuf_), "%s", textNode->text_.c_str());
            lastContentPath_ = curPath;
            contentSeedValid_ = true;
        }

        ui::InputTextMultiline("##textContent", contentBuf_, sizeof(contentBuf_), ImVec2(-1.0f, 120.0f));
        if (ui::Button(ICON_FA_CHECK " Apply Content"))
        {
            contentSeedValid_ = false;
            // An untouched buffer commits nothing (no edit, no undo step).
            if (ea::string(contentBuf_) != textNode->text_)
            {
                UiNodePayload payload = SnapshotUiNodePayload(*textNode);
                payload.text_ = ea::string(contentBuf_);
                tab->GetDocument()->EditNodePayload(textNode, payload);
                return true; // the model was rebuilt; the node is dangling now
            }
        }
        return false;
    }

    // Everything else keeps the single-line field: same seed-per-frame +
    // commit-on-deactivate model as the id/class rows - ImGui keeps its own
    // edit buffer while focused, so re-seeding is safe.
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
                if (auto picked = ResourceBrowseWidget((ea::string("##browse_") + row.name).c_str(), context_, value, row.browseFilter))
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

    // --- Background image: a single-value picker that authors the whole
    // `decorator` shorthand as one image(...). RmlUi has no background-image;
    // the decorator channel is what paints behind an element's content. Only
    // the "one image, no siblings" shape is editable here - mixed decorators
    // (image + gradient/box/tiled, or with an inner ')') render read-only so
    // we never silently drop what we cannot represent. State variants
    // (:hover/:active) need a .rcss selector, which inline style cannot carry;
    // those stay raw too.
    {
        const int at = FindStyleIndexIn(payload.style_, "decorator");
        const ea::string dec = at >= 0 ? Trim(payload.style_[at].value_) : ea::string();

        ea::string display;
        bool editable = true;
        const ea::string pfx = "image(";
        bool singleImage = false;
        ea::string inner;
        if (dec.size() >= pfx.size() + 2 && dec.compare(0, pfx.size(), pfx) == 0 && dec.back() == ')')
        {
            inner = Trim(dec.substr(pfx.size(), dec.size() - pfx.size() - 1));
            if (inner.find(')') == ea::string::npos)
                singleImage = true;
        }
        if (at >= 0 && !singleImage)
        {
            editable = false;
            display = dec; // show the authored string verbatim so the row does not lie
        }
        else if (singleImage)
        {
            if (inner.size() >= 2 && (inner.front() == '"' || inner.front() == '\'') && inner.front() == inner.back())
                display = inner.substr(1, inner.size() - 2);
            else
                display = inner;
        }
        // else: no decorator authored at all -> empty editable field

        ui::TextUnformatted("Background image");
        if (ui::IsItemHovered())
        {
            if (editable)
                ui::SetTooltip("Paints a single image(...) behind this element's content via the\n"
                    "decorator channel (RmlUi has no background-image). Type a '/'-rooted path\n"
                    "under Data/ or a sprite name, or use the folder button. For image-fit,\n"
                    "mixed decorators, or :hover/:active variants, use Inline Style raw or a\n"
                    ".rcss rule (inline style carries no selectors).");
            else
                ui::SetTooltip("This element's decorator is not a single image(...) - it is\n"
                    "layered with something else. Edit it in Inline Style raw so the\n"
                    "other decorators survive.");
        }
        ui::SameLine();
        ui::PushItemWidth(-40.0f);
        char buf[1024];
        snprintf(buf, sizeof(buf), "%s", display.c_str());
        const char* hint = editable ? "path or sprite name" : "(mixed - edit in raw)";
        const bool submit = ui::InputTextWithHint("##bgimg", hint, buf, sizeof(buf),
            ImGuiInputTextFlags_EnterReturnsTrue
                | (editable ? 0 : ImGuiInputTextFlags_ReadOnly));
        ui::PopItemWidth();
        ea::optional<ea::string> picked;
        ui::BeginDisabled(!editable);
        picked = ResourceBrowseWidget("##bgBrowse", context_, display, kImageFilter);
        ui::EndDisabled();
        if (editable && (submit || picked))
        {
            const ea::string next = picked ? *picked : Trim(buf);
            if (next.empty())
            {
                if (at >= 0)
                {
                    DropPayloadStyle(payload, "decorator");
                    structural = true;
                }
            }
            else
            {
                // Author the argument UNQUOTED, exactly as RmlUi's own theme does
                // (image(arrow-down), src: /Textures/x.png). Two reasons:
                //  * The decorator tokenizer keeps any quote chars inside image(...)
                //    verbatim as part of the name, so a quoted src never resolves to a
                //    texture/sprite. Bare is the only form that actually renders.
                //  * The whole style value sits in a double-quoted style="..." attribute,
                //    so emitting a double quote would close that attribute early and
                //    corrupt the document (and our own byte-span re-parse of it).
                // The parenthesis state protects whitespace and commas from splitting, so
                // '/'-paths and sprite+orientation pairs are all fine written bare.
                const ea::string write = "image(" + next + ")";
                if (at < 0 || write != dec)
                {
                    SetPayloadStyle(payload, "decorator", write);
                    structural = true;
                }
            }
        }
    }

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

    // --- Z-order: stacking within a positioned context. Always visible (a common
    // HUD/overlay/modal need), not gated on position. Empty = auto (drops the
    // declaration); a number - positive or negative - is written verbatim.
    LayoutSizeField(payload, structural, "Z-order", "z-index", "auto");
    if (ui::IsItemHovered())
        ui::SetTooltip("Stacking order among siblings in the same context. CSS honours it\n"
            "only once the element is positioned: leave it empty for the document default,\n"
            "or set Position to Anchor (relative) / Free (absolute) for it to take effect.");

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

    // Margin: one field writes all four sides, mirroring Padding (four-way
    // tuning lives in raw). Adjacent vertical margins collapse per CSS, so
    // the rendered gap between neighbors can be smaller than the value here.
    {
        const ea::string mv = cur("margin-top");
        char buf[64];
        snprintf(buf, sizeof(buf), "%s", mv.c_str());
        ui::Text("Margin");
        ui::SameLine();
        ui::PushItemWidth(-8.0f);
        if (ui::InputTextWithHint("##margin", "0px", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue))
        {
            const ea::string next = Trim(buf);
            if (next.empty())
            {
                DropPayloadStyle(payload, "margin-top");
                DropPayloadStyle(payload, "margin-right");
                DropPayloadStyle(payload, "margin-bottom");
                DropPayloadStyle(payload, "margin-left");
            }
            else
            {
                SetPayloadStyle(payload, "margin-top", next);
                SetPayloadStyle(payload, "margin-right", next);
                SetPayloadStyle(payload, "margin-bottom", next);
                SetPayloadStyle(payload, "margin-left", next);
            }
            structural = true;
        }
        if (ui::IsItemHovered())
            ui::SetTooltip("Adjacent vertical margins collapse (CSS): the rendered gap between\n"
                "neighbors is the larger of the two, not their sum. Four-way tuning\n"
                "lives in the raw style.");
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

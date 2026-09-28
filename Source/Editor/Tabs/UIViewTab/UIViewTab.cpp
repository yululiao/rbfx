//
// Copyright (c) 2017-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#include "UIViewTab.h"
#include "UIViewDropMath.h"
#include "UIViewHelpers.h"
#include "UIViewParagraphText.h"

#include "../../Core/IniHelpers.h"
#include "../../Core/WidgetHelpers.h"
#include "../../Project/Project.h"
#include "../GameViewTab.h"
#include "../HierarchyBrowserTab.h"
#include "../InspectorTab.h"
#include "../SceneViewTab.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/File.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Graphics/Texture2D.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Resource/ResourceEvents.h>
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

// On-screen radius (in pixels) a dragged box's edge/center snaps to a
// sibling's or parent's edge/center from (alignment guides).
constexpr float kSnapThresholdPx = 6.0f;

// How many UIViewTab instances have ever been created. Tab identity (ImGui
// window id, ini section, Project tab registry) is keyed on the title, so
// every instance takes a unique one: the plugin-bootstrapped "UI" tab and
// then "UI (2)", "UI (3)", ... for editor tabs spawned per document.

// Half on-screen size of a drawn gizmo handle square.
constexpr float kHandleDrawPx = 4.0f;

// Overlay colors (RGBA ImU32).
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
// Alignment guides: lines drawn across the canvas at a snapped alignment.
constexpr ImU32 kGuideColor = IM_COL32(255, 80, 110, 230);
// Inline text editing: the marker around the element whose #text child is
// being edited in place.
constexpr ImU32 kTextEditColor = IM_COL32(255, 220, 90, 220);

// Walk the model tree to the direct parent of \a node (UiNode carries no
// parent pointer; the tree is small enough for a linear walk per drag frame).
UiNode* FindParentNode(UiNode* root, const UiNode* node)
{
    for (const SharedPtr<UiNode>& child : root->children_)
    {
        if (child.Get() == node)
            return root;
        if (UiNode* hit = FindParentNode(child.Get(), node))
            return hit;
    }
    return nullptr;
}

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

// Modal of the save guard (CanSaveResource): the file changed on disk while
// the document was open and a save wanted to write over it.
const char* const kUiDiskChangePopupId = "File changed on disk###uiViewDiskChange";

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

    // Hot refresh: react to file-watcher events so an .rcss sheet or .rml
    // template saved on disk (from the text editor tab or an external tool)
    // refreshes the canvas without reopening the document. The preview's
    // private RmlUI instance stays unsubscribed from E_FILECHANGED (reload
    // immunity, see UIViewDocument) - this subscription owns the reaction.
    SubscribeToEvent(E_FILECHANGED, URHO3D_HANDLER(UIViewTab, HandleFileChanged));
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

void UIViewTab::CopySelectionToClipboard()
{
    if (!document_)
        return;
    const ea::vector<UiNode*> targets = GetTopLevelSelectedNodes();
    if (targets.empty())
        return; // a virtual / root-only selection has no serialized form
    // One subtree per line: standalone RML that stays human-readable when
    // pasted into an external editor and parses straight back on paste.
    ea::string text;
    for (UiNode* node : targets)
    {
        if (!text.empty())
            text += "\n";
        text += document_->GetModel().SerializeSubtree(*node);
    }
    ui::SetClipboardText(text.c_str());
}

void UIViewTab::CutSelection()
{
    // A virtual selection cannot be serialized: cutting it would silently
    // degrade to a plain delete, so refuse the whole gesture instead.
    if (!document_ || GetTopLevelSelectedNodes().empty())
        return;
    CopySelectionToClipboard();
    DeleteSelection();
}

void UIViewTab::PasteFromClipboard()
{
    if (!document_ || !document_->GetRmlDocument())
        return;
    const char* clip = ui::GetClipboardText();
    if (!clip || !clip[0])
        return;
    const ea::vector<SharedPtr<UiNode>> fragment =
        document_->GetModel().ParseFragment(ea::string(clip));
    if (fragment.empty())
        return; // prose / garbage on the clipboard never pastes

    // Paste lands next to the selection: appended to the selection's parent
    // (the text case targets the text's own element). A virtual / root-only
    // (or empty) selection pastes into the body - never into a virtual node.
    const UiDocumentModel& model = document_->GetModel();
    UiNode* parent = nullptr;
    if (selected_ && selected_ != model.root_.Get())
    {
        if (!selected_->IsNestedDoc() && !selected_->IsHeadLink())
            parent = model.FindParent(selected_);
    }
    if (!parent || parent->IsNestedDoc() || parent->IsHeadLink())
        parent = model.root_.Get();

    const ea::vector<UiNode*> pasted = document_->PasteNodes(parent, fragment);
    if (!pasted.empty())
        SetSelection(pasted);
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

bool UIViewTab::CanSaveResource(const ea::string& resourceName)
{
    // One-shot approval from the overwrite modal. Consume it either way: a
    // stale approval must never auto-pass a later save.
    const bool approved = !overwriteApproved_.empty() && overwriteApproved_ == resourceName;
    overwriteApproved_.clear();
    if (approved)
        return true;

    // No baseline (nothing loaded or written through this instance yet):
    // there is no write this guard could protect.
    if (resourceName.empty() || diskTextName_ != resourceName)
        return true;

    // Bytes different from what this instance last loaded or wrote mean
    // someone else touched the file while it was open. The check reads the
    // disk directly, so it does not depend on any cache refresh.
    if (ReadResourceFile(resourceName) == diskText_)
        return true;

    // Never clobber silently: record the decision and let the modal ask.
    pendingExternalOverwrite_ = resourceName;
    return false;
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
        // Baseline for the save guard: the bytes this instance and the disk
        // agree on at load time.
        diskText_ = contents;
        diskTextName_ = resourceName;
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
        // No trustworthy baseline after a failed load; the guard passes this
        // resource until a load or save re-establishes one.
        diskText_.clear();
        diskTextName_.clear();
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

    // The save-guard state dies with the document: a fresh baseline is taken
    // on the next load, and a pending overwrite decision no longer applies.
    diskText_.clear();
    diskTextName_.clear();
    pendingExternalOverwrite_.clear();
    overwriteApproved_.clear();

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
    const ea::string text = document_->EmitRml();
    if (WriteResourceFile(resourceName, text))
    {
        // New save-guard baseline: the editor and the disk agree on these
        // bytes as of this write.
        diskText_ = text;
        diskTextName_ = resourceName;
    }
    document_->MarkSaved();
}

void UIViewTab::OnResourceShallowSaved(const ea::string& resourceName)
{
    // No per-resource "shallow" data distinct from the emitted .rml text.
    (void)resourceName;
}

bool UIViewTab::PointerInteractionActive() const
{
    // Any held-button or in-canvas editing gesture: a model rebuild under it
    // would free the node pointers the gesture state machines still hold.
    return dragging_ || structDragActive_ || structDragging_ || marqueeActive_ || panningActive_
        || textEditActive_;
}

void UIViewTab::HandleFileChanged(StringHash eventType, VariantMap& eventData)
{
    using namespace FileChanged;
    (void)eventType;
    const ea::string& changed = eventData[P_RESOURCENAME].GetString();
    const ea::string& active = GetActiveResourceName();
    if (active.empty() || !document_)
        return;

    // The open document itself changed on disk. Our own save echoes through
    // the watcher too, so the apply step narrows this by comparing the disk
    // bytes with the model. Never while unsaved edits exist: the overwrite
    // guard owns that conflict at save time, and an automatic reload must
    // not discard work.
    if (changed.comparei(active) == 0)
    {
        if (!IsResourceUnsaved(active))
            pendingHotReloadFromDisk_ = true;
        return;
    }

    // A referenced asset changed: a linked .rcss stylesheet or an .rml
    // template (nested documents). The document is re-projected wholesale
    // rather than tracking the reference set - cheap at editor scale.
    const ea::string ext = GetExtension(changed);
    if (ext == ".rcss" || ext == ".rml")
        pendingHotAssetRefresh_ = true;
}

void UIViewTab::ApplyPendingHotRefresh()
{
    if (!pendingHotReloadFromDisk_ && !pendingHotAssetRefresh_)
        return;
    // File-watcher events land on BeginFrame, mid-gesture included. Defer
    // every rebuild until no pointer interaction is live.
    if (PointerInteractionActive())
        return;

    const ea::string active = GetActiveResourceName();
    if (active.empty() || !document_)
    {
        // The document closed while the change was pending: nothing to do.
        pendingHotReloadFromDisk_ = false;
        pendingHotAssetRefresh_ = false;
        return;
    }

    if (pendingHotReloadFromDisk_)
    {
        pendingHotReloadFromDisk_ = false;
        // Re-check at apply time: edits may have started (or the file been
        // saved again by us) between the watcher event and this frame.
        if (IsResourceUnsaved(active))
            return;
        const ea::string diskText = ReadResourceFile(active);
        if (diskText.empty() || diskText == document_->EmitRml())
            return; // our own save echoing back, or the file vanished

        URHO3D_LOGINFO("UIViewTab: '{}' changed on disk - reloading.", active.c_str());
        // In-place reload, deliberately not Close/Open: closing the resource
        // would also close a secondary instance tab, which a disk change
        // must never do. Mirrors OnResourceLoaded minus the fresh-document
        // setup; undo keeps its snapshots (undoing past the external change
        // is an explicit user choice).
        if (!document_->LoadFromText(diskText, active))
        {
            URHO3D_LOGERROR("UIViewTab: failed to reload '{}' from disk - keeping the last good document",
                active.c_str());
            // LoadFromText cleared the path on failure; re-project the
            // unchanged model so the editing session survives broken bytes.
            document_->LoadFromText(document_->EmitRml(), active);
            return;
        }
        diskText_ = diskText; // new save-guard baseline
        diskTextName_ = active;
        ResetViewToDocument();
        if (hierarchySource_)
            hierarchySource_->ExpandAncestors(selPath_);
        ConnectSharedPanels();
        return;
    }

    pendingHotAssetRefresh_ = false;
    document_->RefreshProjection();
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
    ApplyPendingHotRefresh();
    RenderContentToolbar();
    ui::Separator();
    RenderPreview();
    RenderExternalChangeDialog();
}

void UIViewTab::RenderExternalChangeDialog()
{
    if (!externalDialogOpen_)
    {
        if (pendingExternalOverwrite_.empty())
            return;
        externalDialogOpen_ = true;
        ui::OpenPopup(kUiDiskChangePopupId);
    }

    if (ui::BeginPopupModal(kUiDiskChangePopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        if (pendingExternalOverwrite_.empty())
        {
            // The document was closed while the modal was open: nothing left
            // to decide.
            ui::CloseCurrentPopup();
        }
        else
        {
            const ea::string name = pendingExternalOverwrite_;
            ui::Text("'%s' was modified outside the editor.", name.c_str());
            ui::Text("Overwriting discards the external changes.");
            ui::Separator();

            if (ui::Button(ICON_FA_FLOPPY_DISK " Overwrite"))
            {
                externalDialogOpen_ = false;
                overwriteApproved_ = name;
                pendingExternalOverwrite_.clear();
                ui::CloseCurrentPopup();
                // Re-run the interrupted save: CanSaveResource consumes the
                // approval and the write goes through.
                SaveResource(name, true);
            }
            ui::SameLine();
            if (ui::Button(ICON_FA_BAN " Cancel") || ui::IsKeyPressed(KEY_ESCAPE))
            {
                externalDialogOpen_ = false;
                pendingExternalOverwrite_.clear();
                ui::CloseCurrentPopup();
            }
        }
        ui::EndPopup();
    }
    else if (externalDialogOpen_)
    {
        // Dismissed without a choice (Esc closes the modal): same as Cancel.
        // The document stays unsaved and the next save asks again.
        externalDialogOpen_ = false;
        pendingExternalOverwrite_.clear();
    }
}

void UIViewTab::ToggleRunInGameView()
{
    const ea::string document = GetActiveResourceName();
    if (document.empty())
        return;

    auto* gameViewTab = GetProject()->FindTab<GameViewTab>();
    if (!gameViewTab)
        return;

    // Toggle off while this document is the active preview. Every stop path
    // runs the session bookkeeping (focus, Game View open/close) through the
    // OnSimulationStopped subscriber in ProjectGlue.
    if (gameViewTab->IsPlaying() && gameViewTab->GetActiveUiPreviewDocument() == document)
    {
        gameViewTab->Stop();
        return;
    }

    // The session loads the document from disk, so persist it first. A vetoed
    // save (the overwrite guard modal - its dialog is on screen already)
    // aborts the run.
    if (!SaveResource(document))
        return;

    // Session start, focus and Game View visibility are owned by ProjectGlue,
    // which runs the Launch pipeline for this request. A running session of
    // any kind (a different document, or a plain Launch) is superseded there.
    Project* project = GetProject();
    project->OnRequestUiPreview(project, this, document);
}

bool UIViewTab::IsPreviewingInGameView() const
{
    const ea::string& document = GetActiveResourceName();
    auto* gameViewTab = GetProject()->FindTab<GameViewTab>();
    return !document.empty() && gameViewTab && gameViewTab->IsPlaying()
        && gameViewTab->GetActiveUiPreviewDocument() == document;
}

ea::string UIViewTab::RunInGameViewUnavailableReason() const
{
    // Stop stays enabled while this document is being previewed.
    if (IsPreviewingInGameView())
        return {};

    if (GetActiveResourceName().empty())
        return "Open or create a document to preview in the game";

    auto* sceneViewTab = GetProject()->FindTab<SceneViewTab>();
    if (!sceneViewTab || !sceneViewTab->GetActivePage())
        return "Open a SceneViewTab  first";
    Scene* scene = sceneViewTab ? sceneViewTab->GetActivePage()->scene_.Get() : nullptr;
    if (!scene)
        return "Open a scene in the Scene View first";

    return {};
}

void UIViewTab::RenderToolbar()
{
    // EditorTab override: rendered into the application toolbar strip while
    // this tab is focused. The strip is a fixed-height window, so everything
    // must stay on one row: the document commands first, then the read-only
    // "Editing:" label (the row that used to sit inside the tab content,
    // above the canvas).
    if (Widgets::ToolbarButton(ICON_FA_FILE_LINES, "New document"))
        NewDocument();

    const bool hasActive = !GetActiveResourceName().empty();
    ui::BeginDisabled(!hasActive);
    if (Widgets::ToolbarButton(ICON_FA_FLOPPY_DISK, "Save document"))
        SaveCurrentResource();
    if (Widgets::ToolbarButton(ICON_FA_ROTATE, "Reload the document from disk (discards unsaved edits)"))
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
        Widgets::ToolbarSeparator();
        ui::AlignTextToFramePadding();
        ui::TextDisabled("(unsaved)");
        ui::SameLine();
    }

    // Runtime preview: Run plays the edited scene plus this document in the
    // Game View (see ToggleRunInGameView); while that session previews this
    // document the button turns into Stop. A disabled button carries its
    // reason in the tooltip.
    Widgets::ToolbarSeparator();
    const bool previewingInGameView = IsPreviewingInGameView();
    const ea::string runUnavailableReason = RunInGameViewUnavailableReason();
    ui::BeginDisabled(!runUnavailableReason.empty());
    if (Widgets::ToolbarButton(previewingInGameView ? ICON_FA_STOP : ICON_FA_PLAY, nullptr))
        ToggleRunInGameView();
    if (ui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        if (previewingInGameView)
            ui::SetTooltip("Stop the Game View session previewing this document");
        else if (!runUnavailableReason.empty())
            ui::SetTooltip("%s", runUnavailableReason.c_str());
        else
            ui::SetTooltip("Play the edited scene and this document in the Game View");
    }
    ui::EndDisabled();

    // Read-only display of the document being edited, trailing the commands.
    // It is resolved after the buttons ran: a New/Reload click above may have
    // repopulated the resources this frame, so an earlier lookup must not be
    // reused.
    const ea::string& activeResource = GetActiveResourceName();
    Widgets::ToolbarSeparator();
    ui::AlignTextToFramePadding();
    ui::TextDisabled("Editing: %s", activeResource.empty() ? "(no document open)" : activeResource.c_str());
}

void UIViewTab::RenderContentToolbar()
{
    // The tab-content command row above the canvas: structural commands
    // (wrap / flow move) on the left and the view controls (fit, zoom, canvas
    // size) on the right, merged into one line. The document commands live in
    // the editor toolbar strip (RenderToolbar); Add Widget and copy/delete
    // live in the hierarchy context menu.
    const bool hasDoc = document_ && document_->GetRmlDocument() != nullptr;

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
    // single consumer is the DocViewport built in RenderPreview. A wider gap
    // marks the group border inside the merged single-line row.
    ui::SameLine(0.0f, ui::GetStyle().ItemSpacing.x * 3.0f);
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

void UIViewTab::RenderAddWidgetPalette(UiNode* parent)
{
    if (!document_)
        return;

    // Filter box at the top: typing narrows the palette to entries whose
    // label or group matches (case-insensitive), so the longer palette
    // stays quick to scan.
    ui::SetNextItemWidth(180.0f);
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
        if (ui::MenuItem(entry.label_))
        {
            if (UiNode* added = document_->AddWidget(parent, MakeWidgetSpec(entry)))
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
        if (UiNode* added = document_->AddWidget(parent, spec))
            SetSelectedNode(added);
        anyTag[0] = '\0';
    }
    ui::EndDisabled();
}

void UIViewTab::RenderPreview()
{
    if (!document_ || !document_->GetRmlDocument())
    {
        ui::TextUnformatted("No UI document open.\nDouble-click a .rml in the Resource Browser to edit it, or click New\nin the toolbar to create one (a Save As dialog picks the location under\nthe project Data).");
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
    if (io.KeyCtrl && !io.KeyShift && ui::IsKeyPressed(ImGuiKey_C))
    {
        CopySelectionToClipboard();
        return;
    }
    if (io.KeyCtrl && !io.KeyShift && ui::IsKeyPressed(ImGuiKey_X))
    {
        CutSelection();
        return;
    }
    if (io.KeyCtrl && !io.KeyShift && ui::IsKeyPressed(ImGuiKey_V))
    {
        PasteFromClipboard();
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

    // Alignment snap (move only): adjusts the live box and the pointer
    // position the eventual commit re-solves from, so the live preview, the
    // committed box and the drawn guides can never disagree.
    if (gizmoDrag_.op_ == GizmoOp::Move)
        ApplyDragSnap(vp);

    // Live preview into the DOM projection; the model is only touched on
    // release. Layout re-flows next E_POSTUPDATE (Context::Update).
    document_->SetLiveBox(gizmoNode_, gizmoLiveBox_);

    if (gizmoDrag_.op_ == GizmoOp::Move && !extraDragNodes_.empty())
    {
        // Same layout-space translation as the primary (its base cancels out of
        // pos_-pos_, so this is exact for siblings sharing a containing block).
        // gizmoLiveBox_ is post-snap, so the extras follow the snapped primary.
        const Vector2 delta = gizmoLiveBox_.pos_ - gizmoStartBox_.pos_;
        for (size_t i = 0; i < extraDragNodes_.size(); i++)
        {
            UiBox b = extraDragStarts_[i];
            b.pos_ += delta;
            document_->SetLiveBox(extraDragNodes_[i], b);
        }
    }
}

void UIViewTab::ApplyDragSnap(const DocViewport& vp)
{
    snapGuidesX_.clear();
    snapGuidesY_.clear();
    if (!gizmoNode_ || !gizmoNode_->dom_)
        return;

    // Candidate alignment boxes, in the dragged node's authored (gizmo)
    // frame: the offset parent and its element children. Doc-space boxes
    // lifted by -gizmoBase_ - the same pure translation DrawOverlay applies
    // in reverse when it lifts the live box into document space.
    const UiNode* parent = FindParentNode(document_->GetModel().root_.Get(), gizmoNode_);
    if (!parent)
        return;
    auto isDragged = [this](const UiNode* n)
    {
        if (n == gizmoNode_)
            return true;
        for (UiNode* extra : extraDragNodes_)
            if (extra == n)
                return true;
        return false;
    };
    ea::vector<UiBox> targets;
    auto addBox = [&](const UiNode* n)
    {
        ea::vector<UiBox> boxes;
        if (document_->TryGetDomBoxes(n, boxes))
        {
            for (const UiBox& b : boxes)
            {
                UiBox t = b;
                t.pos_ -= gizmoBase_;
                targets.push_back(t);
            }
        }
    };
    addBox(parent);
    for (const SharedPtr<UiNode>& child : parent->children_)
    {
        const UiNode* c = child.Get();
        if (isDragged(c) || c->IsText() || c->IsNestedDoc() || c->IsHeadLink() || !c->dom_)
            continue;
        addBox(c);
    }

    // Best alignment per axis within the screen-constant snap threshold:
    // each of the dragged box's two edges and center against each target's.
    const float threshold = kSnapThresholdPx / vp.scale_;
    const float dragX[3] = { gizmoLiveBox_.pos_.x_,
        gizmoLiveBox_.pos_.x_ + 0.5f * gizmoLiveBox_.size_.x_,
        gizmoLiveBox_.pos_.x_ + gizmoLiveBox_.size_.x_ };
    const float dragY[3] = { gizmoLiveBox_.pos_.y_,
        gizmoLiveBox_.pos_.y_ + 0.5f * gizmoLiveBox_.size_.y_,
        gizmoLiveBox_.pos_.y_ + gizmoLiveBox_.size_.y_ };
    float bestDx = FLT_MAX, snapDx = 0.0f, guideX = 0.0f;
    float bestDy = FLT_MAX, snapDy = 0.0f, guideY = 0.0f;
    for (const UiBox& t : targets)
    {
        const float tx[3] = { t.pos_.x_, t.pos_.x_ + 0.5f * t.size_.x_, t.pos_.x_ + t.size_.x_ };
        const float ty[3] = { t.pos_.y_, t.pos_.y_ + 0.5f * t.size_.y_, t.pos_.y_ + t.size_.y_ };
        for (float dx : dragX)
            for (float cx : tx)
            {
                const float d = Abs(dx - cx);
                if (d <= threshold && d < bestDx)
                {
                    bestDx = d;
                    snapDx = cx - dx;
                    guideX = cx;
                }
            }
        for (float dy : dragY)
            for (float cy : ty)
            {
                const float d = Abs(dy - cy);
                if (d <= threshold && d < bestDy)
                {
                    bestDy = d;
                    snapDy = cy - dy;
                    guideY = cy;
                }
            }
    }

    if (bestDx <= threshold)
    {
        gizmoLiveBox_.pos_.x_ += snapDx;
        gizmoCurDoc_.x_ += snapDx; // the commit re-solves from this
        snapGuidesX_.push_back(guideX + gizmoBase_.x_); // doc space, for drawing
    }
    if (bestDy <= threshold)
    {
        gizmoLiveBox_.pos_.y_ += snapDy;
        gizmoCurDoc_.y_ += snapDy;
        snapGuidesY_.push_back(guideY + gizmoBase_.y_);
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
    snapGuidesX_.clear();
    snapGuidesY_.clear();
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
    snapGuidesX_.clear();
    snapGuidesY_.clear();
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

    DrawSnapGuides(vp);
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

void UIViewTab::DrawSnapGuides(const DocViewport& vp)
{
    if (!dragging_ || gizmoDrag_.op_ != GizmoOp::Move)
        return;
    if (snapGuidesX_.empty() && snapGuidesY_.empty())
        return;
    ImDrawList* dl = ui::GetWindowDrawList();
    const IntVector2 previewSize = document_->GetPreviewSize();
    for (float x : snapGuidesX_)
        dl->AddLine(IV2(vp.ToScreen(Vector2(x, 0.0f))),
            IV2(vp.ToScreen(Vector2(x, (float)previewSize.y_))), kGuideColor, 1.0f);
    for (float y : snapGuidesY_)
        dl->AddLine(IV2(vp.ToScreen(Vector2(0.0f, y))),
            IV2(vp.ToScreen(Vector2((float)previewSize.x_, y))), kGuideColor, 1.0f);
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

}

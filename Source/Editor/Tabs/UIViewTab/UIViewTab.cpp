//
// Copyright (c) 2017-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#include "UIViewTab.h"

#include "../../Project/Project.h"

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
// without a chain of per-control branches.
struct PaletteEntry
{
    const char* label_;
    const char* tag_;
};
const PaletteEntry kPalette[] = {
    {ICON_FA_SQUARE "  div", "div"},
    {ICON_FA_IMAGE "  img", "img"},
    {ICON_FA_TOGGLE_ON "  button", "button"},
    {ICON_FA_FONT "  Text", "text"},
};
} // namespace

void Tabs_UIViewTab(Context* context, Project* project)
{
    project->AddTab(MakeShared<UIViewTab>(context));
}

// ---------------------------------------------------------------------------
// UIViewTab: view + controller over one UIViewDocument
// ---------------------------------------------------------------------------

UIViewTab::UIViewTab(Context* context)
    : ResourceEditorTab(context, ICON_FA_BEZIER_CURVE " UI", "8f2b1c9e-7d34-4a5b-9c10-ui0preview",
        EditorTabFlags{}, EditorTabPlacement::DockCenter)
{
    document_ = MakeShared<UIViewDocument>(context_);
    document_->OnModelEdited.Subscribe(this, &UIViewTab::OnModelEdited);

    // Route editing commands through the tab so ResourceEditorTab attributes
    // each action to the active resource (per-document dirty tracking + undo
    // focus). When no resource is open the pusher declines and the document
    // falls back to the raw project undo manager, so edits stay undoable.
    document_->SetUndoPusher([this](const SharedPtr<EditorAction>& action) -> bool
    {
        const ea::string& active = GetActiveResourceName();
        if (!active.empty() && IsResourceOpen(active))
            return PushAction(action).has_value();
        return false;
    });

    hierarchySource_ = MakeShared<UIViewHierarchy>(this);
    inspectorSource_ = MakeShared<UIViewInspector>(this);
}

UIViewTab::~UIViewTab()
{
    selected_ = nullptr;
}

UIViewTab* UIViewTab::GetActive(Project* project)
{
    return project ? project->FindTab<UIViewTab>() : nullptr;
}

ea::vector<unsigned> UIViewTab::NodePath(const UiNode* node) const
{
    ea::vector<unsigned> path;
    if (document_)
        document_->GetModel().BuildPath(node, path);
    return path;
}

void UIViewTab::OnModelEdited()
{
    // A command or undo/redo may have invalidated the selection pointer.
    // Restore it from the stable child-index path when possible.
    if (!document_)
        return;
    const UiDocumentModel& model = document_->GetModel();
    if (selected_ && model.FindParent(selected_) != nullptr) // still attached
        return;
    selected_ = model.ResolvePath(selPath_);
    if (!selected_)
        selected_ = model.root_.Get();
    model.BuildPath(selected_, selPath_);
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

void UIViewTab::OnResourceLoaded(const ea::string& resourceName)
{
    const ea::string contents = ReadResourceFile(resourceName);
    if (contents.empty())
    {
        URHO3D_LOGERROR("UIViewTab: failed to read UI document '{}'", resourceName.c_str());
        return;
    }

    if (document_->LoadFromText(contents, resourceName))
    {
        // Fresh open: start with the root selected.
        resourcePath_ = resourceName;
        ResetViewToDocument();
        if (hierarchySource_)
            hierarchySource_->ExpandAncestors(selPath_);
    }
    else
    {
        URHO3D_LOGERROR("UIViewTab: failed to parse UI document '{}'", resourceName.c_str());
    }
}

void UIViewTab::OnResourceUnloaded(const ea::string& resourceName)
{
    // Single-document mode: dropping the active resource clears the view. The
    // base unloads the outgoing document before loading the next one.
    if (resourceName == resourcePath_)
    {
        resourcePath_.clear();
        selected_ = nullptr;
        selPath_.clear();
        hoveredPath_.clear();
        gizmoNode_ = nullptr;
        dragging_ = false;
    }
}

void UIViewTab::OnActiveResourceChanged(const ea::string& oldResourceName, const ea::string& newResourceName)
{
    // In single-resource mode a change of the active document is always
    // bracketed by OnResourceLoaded/Unloaded, which already reseed selection.
    // Keep it consistent if the two ever disagree.
    if (resourcePath_ != newResourceName)
        ResetViewToDocument();
}

void UIViewTab::OnResourceSaved(const ea::string& resourceName)
{
    if (!document_ || !document_->GetRmlDocument())
        return;
    const ea::string emitted = document_->EmitRml();
    WriteResourceFile(resourceName, emitted);
    document_->MarkSaved();
}

void UIViewTab::OnResourceShallowSaved(const ea::string& resourceName)
{
    // No per-resource "shallow" data distinct from the emitted .rml text.
    (void)resourceName;
}

void UIViewTab::NewDocument()
{
    static const ea::string kTemplate =
        "<rml>\n"
        "  <head>\n"
        "    <style>\n"
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

    if (!WriteResourceFile(resourceName, kTemplate))
    {
        URHO3D_LOGERROR("UIViewTab: failed to create UI document '{}'", resourceName.c_str());
        return;
    }

    // (Re)open so the freshly written file becomes the active, tracked document.
    if (IsResourceOpen(resourceName))
        CloseResource(resourceName);
    OpenResource(resourceName);
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
    // Read-only display of the document being edited (empty until one is opened). This
    // replaces the old type-a-path field: double-clicking a .rml in the Resource Browser now
    // opens it, and New pops a native Save As, so there is nothing left to type here.
    ui::AlignTextToFramePadding();
    ui::TextDisabled("Editing: %s", resourcePath_.empty() ? "(no document open)" : resourcePath_.c_str());

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
        for (const PaletteEntry& entry : kPalette)
        {
            if (ui::Selectable(entry.label_))
            {
                if (UiNode* added = document_->AddWidget(selected_, entry.tag_))
                    SetSelectedNode(added);
            }
        }
        ui::EndCombo();
    }
    ui::SameLine();
    const bool canEdit = selected_ && document_ && selected_ != document_->GetModel().root_.Get();
    ui::BeginDisabled(!canEdit);
    if (ui::Button(ICON_FA_COPY " Copy"))
    {
        if (UiNode* copy = document_->DuplicateNode(selected_))
            SetSelectedNode(copy);
    }
    ui::SameLine();
    if (ui::Button(ICON_FA_TRASH " Delete"))
        document_->DeleteNode(selected_); // OnModelEdited revalidates the selection
    ui::EndDisabled();
    ui::EndDisabled();

    // Element context menu (opened by a right-click on the preview).
    if (ui::BeginPopup("##uiElemCtx"))
    {
        UiNode* node = selected_;
        if (node && !node->IsText() && !node->IsMaterialized())
        {
            if (ui::MenuItem(ICON_FA_LOCATION_PIN " Add Explicit Position"))
                document_->MaterializeNode(node);
        }
        if (node && document_ && node != document_->GetModel().root_.Get())
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
        UiBox box;
        if (document_->TryGetDomBox(hover, box))
        {
            ImVec2 c[4];
            TransformedCorners(vp, box, c);
            dl->AddPolyline(c, 4, kHoverColor, ImDrawFlags_Closed, 1.0f);
        }
    }

    if (sel && sel->dom_)
    {
        UiBox box;
        const bool dragging = dragging_ && gizmoNode_ == sel;
        if (dragging)
        {
            // The live box is left/top-space; lift it into document space with
            // the frame origin captured at press.
            box = gizmoLiveBox_;
            box.pos_ += gizmoBase_;
        }
        else
            document_->TryGetDomBox(sel, box);

        if (box.size_.x_ > 0.0f && box.size_.y_ > 0.0f)
        {
            ImVec2 c[4];
            TransformedCorners(vp, box, c);
            dl->AddConvexPolyFilled(c, 4, kSelectFill);
            dl->AddPolyline(c, 4, kSelectColor, ImDrawFlags_Closed, 2.0f);
        }

        // The gizmo rect is the selection rect; handles are only offered when
        // the node is materialized (or being dragged into shape).
        UiBox materializedBox;
        if (dragging || TryGetMaterializedBox(*sel, materializedBox))
            DrawGizmo(vp, box);
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
    // Open every prefix of the selected path so the tree reveals the selection.
    ea::vector<unsigned> prefix;
    for (size_t i = 0; i <= path.size(); i++)
    {
        if (!PathIn(openedPaths_, prefix))
            openedPaths_.push_back(prefix);
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
}

void UIViewHierarchy::RenderNode(UiNode* node, const ea::vector<unsigned>& path)
{
    UIViewTab* tab = owner_;
    if (!tab || !node)
        return;

    ea::string label = node->IsText() ? ea::string("#text") : node->tag_;
    if (!node->id_.empty())
        label += "#" + node->id_;
    if (!node->classes_.empty())
    {
        ea::string cls = node->classes_;
        cls.replace(" ", ".");
        label += "." + cls;
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
    if (ui::IsItemClicked(ImGuiMouseButton_Right))
        contextMenuTarget_ = node;

    if (hasElementChild && open && ui::IsItemToggledOpen())
    {
        // Persist the manual toggle as an override.
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
    UiNode* target = contextMenuTarget_ ? contextMenuTarget_ : tab->GetSelectedNode();
    if (!target)
        return;

    if (!target->IsText() && !target->IsMaterialized())
    {
        if (ui::MenuItem(ICON_FA_LOCATION_PIN " Add Explicit Position"))
            doc->MaterializeNode(target);
    }
    if (target != doc->GetModel().root_.Get())
    {
        if (ui::MenuItem(ICON_FA_TRASH " Delete"))
        {
            tab->SetSelectedNode(target);
            doc->DeleteNode(target);
        }
    }
    contextMenuTarget_ = nullptr;
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
    UiNode* node = tab ? tab->GetSelectedNode() : nullptr;
    if (!node)
    {
        ui::TextDisabled("Select an element to edit its attributes.");
        return;
    }

    ea::string header = node->tag_;
    if (!node->id_.empty())
        header += "#" + node->id_;
    ui::Text(ICON_FA_HAND_POINTER " %s", header.c_str());
    ui::Separator();

    RenderTextContent(node);
    RenderAttributes(node);
    ui::Separator();
    RenderInlineStyle(node);
    ui::Separator();
    RenderComputed(node);
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
    }
    else if (!node->IsText() && tab && tab->GetDocument())
    {
        if (ui::Button(ICON_FA_LOCATION_PIN " Add Explicit Position"))
            tab->GetDocument()->MaterializeNode(node);
    }
}

}

//
// Copyright (c) 2017-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#include "UIViewHierarchy.h"
#include "UIViewHelpers.h"

#include "UIViewTab.h"

#include <Urho3D/SystemUI/DragDropPayload.h>
#include <Urho3D/SystemUI/SystemUI.h>

#include <IconFontCppHeaders/IconsFontAwesome6.h>

namespace Urho3D
{

EditorTab* UIViewHierarchy::GetOwnerTab()
{
    return owner_;
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

    // Add Widget: the palette the toolbar combo used to carry, now adding
    // into the right-clicked container (a root target appends into the
    // document). It is the menu's constructive entry and applies to any
    // element target - including the root, which has no other commands - so
    // it sits above the selection commands.
    if (ui::BeginMenu(ICON_FA_PLUS " Add Widget"))
    {
        tab->RenderAddWidgetPalette(target);
        ui::EndMenu();
    }

    // Paste is selection-relative (lands beside the current selection, into
    // the body when nothing is selected), so it applies to any target,
    // including the root.
    if (ui::MenuItem(ICON_FA_PASTE " Paste"))
        tab->PasteFromClipboard();

    if (target != doc->GetModel().root_.Get())
    {
        ui::Separator();
        const int count = static_cast<int>(tab->GetTopLevelSelectedNodes().size());
        if (count > 1)
        {
            if (ui::MenuItem(Format(ICON_FA_COPY " Copy %d Items", count).c_str()))
                tab->CopySelectionToClipboard();
            if (ui::MenuItem(Format(ICON_FA_SCISSORS " Cut %d Items", count).c_str()))
                tab->CutSelection();
            if (ui::MenuItem(Format(ICON_FA_TRASH " Delete %d Items", count).c_str()))
                tab->DeleteSelection();
        }
        else
        {
            // "Copy" writes the OS clipboard (standalone RML); the in-place
            // duplicate stays on Ctrl+D only.
            if (ui::MenuItem(ICON_FA_COPY " Copy"))
            {
                tab->SetSelectedNode(target);
                tab->CopySelectionToClipboard();
            }
            if (ui::MenuItem(ICON_FA_SCISSORS " Cut"))
            {
                tab->SetSelectedNode(target);
                tab->CutSelection();
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

}

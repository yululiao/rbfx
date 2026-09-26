//
// Copyright (c) 2017-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#pragma once

#include "../Shared/InspectorSource.h"

#include <Urho3D/Core/Object.h>
#include <Urho3D/Container/Ptr.h>

namespace Urho3D
{

struct UiNode;
class UIViewTab;

/// InspectorSource: edits attributes and inline style of the selected model node.
///
/// Bound to the shared InspectorTab by the focus glue (UIViewGlue). Renders the
/// document-level head-link section first, then the per-node editors for the
/// tab's primary selection. Every commit rebuilds the whole model tree (see
/// UIViewDocument::CommitAndReload), so each section reports whether it
/// committed and the frame stops rendering, instead of touching pointers the
/// rebuild just invalidated.
class UIViewInspector : public Object, public InspectorSource
{
    URHO3D_OBJECT(UIViewInspector, Object)

public:
    explicit UIViewInspector(UIViewTab* owner);

    /// Implement InspectorSource
    /// @{
    EditorTab* GetOwnerTab() override;
    void RenderContent() override;
    /// Attribute and inline-style edits push onto the same project-wide
    /// UndoManager as the owner tab.
    bool IsUndoSupported() override { return true; }
    /// @}

    /// Drop cached per-node edit state (inline-style seed) so the next render
    /// re-reads it from the rebuilt model.
    void InvalidateCaches();

private:
    /// Document-level "add head link" row. The links themselves are
    /// #head-link nodes at the top of the Hierarchy; this is only the spigot
    /// that appends one more <link> to <head>. Rendered above the per-node
    /// editors because it applies with or without a selection.
    void RenderHeadLinks();
    /// Per-link panel for a #head-link node: type/href editing, navigation to
    /// the linked file, removal. The generic attribute/style editors do not
    /// apply - head bytes are edited through the text-level link commands.
    void RenderHeadLink(UiNode* node);
    /// Navigation panel for the nested-doc virtual node (path + reveal/open).
    void RenderNestedDoc(UiNode* node);
    /// The sections below return true when they committed an edit: any
    /// commit rebuilds the whole model tree, which invalidates every UiNode
    /// pointer - including the caller's. On true, RenderContent stops rendering
    /// for this frame and re-renders from the rebuilt model on the next one
    /// (the commit-then-return pattern the link panels already use).
    /// The visibility row sits above the rest: show/hide is the one property
    /// with a multi-selection meaning (the checkbox reflects the primary node,
    /// the click toggles every selected element as one undo step).
    bool RenderVisibility(UiNode* node);
    bool RenderTextContent(UiNode* node);
    bool RenderAttributes(UiNode* node);
    bool RenderLayout(UiNode* node);
    bool RenderAppearance(UiNode* node);
    bool RenderInlineStyle(UiNode* node);
    /// Read-only sections: they never commit, so the node stays valid.
    void RenderTemplates(UiNode* node);
    void RenderComputed(UiNode* node);

    WeakPtr<UIViewTab> owner_;
    char attributeKeyBuf_[128]{};
    char attributeValueBuf_[1024]{};
    char styleBuf_[2048]{};
    /// Resource path being typed into the head-link field.
    char headLinkHrefBuf_[256]{};
    // Cached inline-style text and the selection path it was seeded from, so
    // the multiline editor is only refreshed when the selection changes.
    ea::vector<unsigned> lastStylePath_;
    bool styleSeedValid_ = false;
    // Cached paragraph-content text: a <p>'s Content field is the multi-line
    // editor, seeded only when the selection changes or the model was rebuilt.
    char contentBuf_[4096]{};
    ea::vector<unsigned> lastContentPath_;
    bool contentSeedValid_ = false;
};

}

//
// Copyright (c) 2017-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#pragma once

#include "UIViewLayoutMath.h"

#include <Urho3D/Core/Object.h>
#include <Urho3D/Core/Signal.h>
#include <Urho3D/Graphics/Texture2D.h>
#include <Urho3D/Math/Vector2.h>

namespace Rml
{
class Element;
class ElementDocument;
}

namespace Urho3D
{

class RmlUI;
class EditorAction;

/// One editable UI document: the editor model (source of truth) together with
/// its live RmlUi projection and the offscreen preview surface. Extracted from
/// UIViewTab so the tab keeps only ImGui drawing and input routing.
///
/// Editing goes through the undoable commands (AddWidget / DuplicateNode /
/// DeleteNode / MaterializeNode / EditNodePayload / CommitBoxEdit), which
/// mutate the model, synchronize the live DOM in place (never re-emit/reload,
/// so template and data-bound subtrees are not re-instantiated) and record an
/// EditorAction on the project's UndoManager.
class UIViewDocument : public Object
{
    URHO3D_OBJECT(UIViewDocument, Object)

public:
    /// Fired after any model mutation (commands, undo, redo). Views holding
    /// raw UiNode pointers must revalidate on this signal (selection paths may
    /// have become stale or nodes may have been destroyed).
    Signal<void()> OnModelEdited;

    explicit UIViewDocument(Context* context);
    ~UIViewDocument() override;

    /// Return properties of the document.
    /// @{
    const UiDocumentModel& GetModel() const { return model_; }
    Rml::ElementDocument* GetRmlDocument() const { return document_; }
    RmlUI* GetPreviewUI() const { return previewUI_; }
    Texture2D* GetPreviewTexture() const { return texture_; }
    const IntVector2& GetPreviewSize() const { return previewSize_; }
    bool IsDirty() const { return dirty_; }
    void MarkSaved() { dirty_ = false; }
    void MarkDirty() { dirty_ = true; }
    /// @}

    /// Seed the model + projection from raw RML source text loaded under its
    /// real resource path (so relative <link>/<template> hrefs resolve).
    bool LoadFromText(const ea::string& text, const ea::string& path);
    /// Serialize the model back to complete .rml text.
    ea::string EmitRml() const { return model_.EmitRml(); }

    /// Live-DOM editing primitives. Push model state onto the attached
    /// elements without any undo recording; used by the commands, by the undo
    /// actions and by the drag controller for live previews.
    /// @{
    void SyncStyleToDom(UiNode* node);
    void ApplyNodeToDom(UiNode* node);
    Rml::Element* CreateDomForNode(UiNode& node, Rml::Element* parentEl);
    /// Detach a subtree from the live DOM and null all its dom_ links.
    void DetachFromDom(UiNode* node);
    /// Synchronously update + re-layout the document.
    void RefreshLayout();
    /// During-drag DOM-only preview write (model is only touched on commit).
    void SetLiveBox(UiNode* node, const UiBox& box);
    /// @}

    /// DOM queries for the views (document-space boxes and hit testing).
    /// @{
    UiNode* HitTest(const Vector2& docPos) const;
    bool TryGetDomBox(const UiNode* node, UiBox& out) const;
    Vector2 GetInlineStyleBase(const UiNode* node) const;
    /// @}

    /// Undoable editing commands. Each mutates the model AND the live DOM,
    /// marks the document dirty, notifies views (OnModelEdited) and records an
    /// action on the project's UndoManager when available.
    /// @{
    UiNode* AddWidget(UiNode* parent, const char* tag);
    UiNode* DuplicateNode(UiNode* node);
    bool DeleteNode(UiNode* node);
    bool MaterializeNode(UiNode* node);
    bool EditNodePayload(UiNode* node, const UiNodePayload& newData);
    /// Commit a solved gizmo box (drag release) as one recorded style edit.
    bool CommitBoxEdit(UiNode* node, const UiBox& box);
    /// @}

    /// Apply helpers used by the undo actions: apply without recording.
    /// @{
    UiNode* LookupNode(const ea::vector<unsigned>& path) const { return model_.ResolvePath(path); }
    bool ApplyNodePayloadInternal(const ea::vector<unsigned>& path, const UiNodePayload& payload);
    bool InsertNodeInternal(const ea::vector<unsigned>& parentPath, unsigned index,
        const SharedPtr<UiNode>& node);
    bool RemoveNodeInternal(const ea::vector<unsigned>& parentPath, unsigned index,
        const SharedPtr<UiNode>& expected);
    /// @}

private:
    /// Renders the offscreen preview at a valid render-phase event.
    void HandleBeginRendering(StringHash eventType, VariantMap& eventData);
    /// Unique data-model name for this document instance.
    ea::string SubstituteDataModelToken(const ea::string& text) const;
    /// (Re)create the preview render-target texture.
    void Rebuild();
    /// Record a payload change for \a node given its pre-edit snapshot.
    bool PushChangeNodeAction(UiNode* node, const UiNodePayload& oldData);
    bool PushUndoAction(const SharedPtr<EditorAction>& action);

    SharedPtr<RmlUI> previewUI_;
    SharedPtr<Texture2D> texture_;
    Rml::ElementDocument* document_ = nullptr;

    UiDocumentModel model_;
    IntVector2 previewSize_{1024, 768};
    bool dirty_ = false;
};

}

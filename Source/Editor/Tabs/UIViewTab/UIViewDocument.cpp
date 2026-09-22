//
// Copyright (c) 2017-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#include "UIViewDocument.h"

#include "UIViewActions.h"

#include "../../Project/Project.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/Core/CoreEvents.h>
#include <Urho3D/Graphics/GraphicsEvents.h>
#include <Urho3D/Graphics/RenderSurface.h>
#include <Urho3D/Graphics/Texture2D.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/RmlUI/RmlUI.h>
#include <Urho3D/Resource/ResourceEvents.h>

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Types.h>

namespace Urho3D
{

namespace
{

// Static preview resolution. The document lays out against this virtual
// viewport; the preview widget then scales it down to fit the tab.
constexpr int kPreviewWidth = 1024;
constexpr int kPreviewHeight = 768;

Vector2 V2(const Rml::Vector2f& v) { return Vector2{v.x, v.y}; }

// Origin of the coordinate frame an element's inline left/top resolve against,
// derived from the element's own placement (absOrigin = frameOrigin + left).
// Empirical, so it stays exact no matter which box area RmlUi uses as the
// containing block for absolute positioning.
Vector2 InlineStyleBase(Rml::Element* el, const UiNode* node)
{
    float l = 0.0f, t = 0.0f;
    if (el && node && TryParsePx(node->GetStyle("left"), l) && TryParsePx(node->GetStyle("top"), t))
        return V2(el->GetAbsoluteOffset(Rml::BoxArea::Border)) - Vector2{l, t};
    return Vector2::ZERO;
}

// The border box (document space) of a live DOM element, plus the node's own
// emitted transform.
bool TryGetDomBox(Rml::Element* element, const UiNode* node, UiBox& out)
{
    if (!element)
        return false;
    out.pos_ = V2(element->GetAbsoluteOffset(Rml::BoxArea::Border));
    out.size_ = V2(element->GetBox().GetSize(Rml::BoxArea::Border));
    out.xform_ = node ? ParseUiTransform(node->GetStyle("transform")) : UiTransform{};
    return out.size_.x_ > 0.0f && out.size_.y_ > 0.0f;
}

// True when the model carries at least one authored text node (not whitespace),
// used by the no-effective-font diagnostic in ReloadFromText.
bool HasAuthoredText(const UiNode& node)
{
    if (node.IsText())
        return !Trim(node.text_).empty();
    for (const SharedPtr<UiNode>& child : node.children_)
    {
        if (HasAuthoredText(*child))
            return true;
    }
    return false;
}

// Deepest element whose box contains the point, children first in reverse
// paint order. Template-minted elements (window frames, close buttons) have no
// model node of their own - they are not part of the authored source - so a
// hit on them bubbles to the nearest ancestor that has one. A bubble that can
// only reach the body is a hit on the nested document's chrome: it selects the
// nested-doc virtual node as a whole instead of the body.
UiNode* HitTestRecurse(Rml::Element* element, const UiDocumentModel* model, const Vector2& point)
{
    const int n = element->GetNumChildren(false);
    for (int i = n - 1; i >= 0; i--)
    {
        Rml::Element* child = element->GetChild(i);
        if (child->GetTagName() == "#text")
            continue; // raw text isn't independently selectable
        if (UiNode* hit = HitTestRecurse(child, model, point))
            return hit;
    }

    UiNode* node = model->FindByDom(element);
    UiBox box;
    if (!TryGetDomBox(element, node, box))
        return nullptr;
    const Vector2 local = InverseMapPoint(point, box);
    if (local.x_ < box.pos_.x_ || local.x_ > box.pos_.x_ + box.size_.x_ ||
        local.y_ < box.pos_.y_ || local.y_ > box.pos_.y_ + box.size_.y_)
        return nullptr;

    for (Rml::Element* ancestor = element; !node && ancestor; ancestor = ancestor->GetParentNode())
    {
        node = model->FindByDom(ancestor);
        if (node == model->root_.Get())
        {
            if (UiNode* nested = model->GetNestedDoc())
                node = nested;
            break;
        }
    }
    return node;
}

} // namespace

UIViewDocument::UIViewDocument(Context* context)
    : Object(context)
{
    // Private RmlUi context that renders the document under edit into a dynamic
    // texture. Deliberately not the master RmlUI subsystem so editing does not
    // leak into the running game view. Rml::CreateContext fails on duplicate
    // names, and several documents can be open at once, so give each instance
    // its own context name.
    static unsigned instanceCounter = 0;
    const ea::string contextName = Format("UIViewPreview-{}", ++instanceCounter);
    previewUI_ = new RmlUI(context_, contextName.c_str());
    // Input isolation: drop RmlUI's global input subscriptions so the preview
    // cannot steal editor focus. SetBlockEvents() must NOT be used - it blocks
    // E_POSTUPDATE too, stalling Context::Update so the document never renders.
    previewUI_->UnsubscribeFromEvent(E_MOUSEBUTTONDOWN);
    previewUI_->UnsubscribeFromEvent(E_MOUSEBUTTONUP);
    previewUI_->UnsubscribeFromEvent(E_MOUSEMOVE);
    previewUI_->UnsubscribeFromEvent(E_MOUSEWHEEL);
    previewUI_->UnsubscribeFromEvent(E_TOUCHBEGIN);
    previewUI_->UnsubscribeFromEvent(E_TOUCHEND);
    previewUI_->UnsubscribeFromEvent(E_TOUCHMOVE);
    previewUI_->UnsubscribeFromEvent(E_KEYDOWN);
    previewUI_->UnsubscribeFromEvent(E_KEYUP);
    previewUI_->UnsubscribeFromEvent(E_TEXTINPUT);
    previewUI_->UnsubscribeFromEvent(E_DROPFILE);
    // Reload immunity: keep the isolated preview from reacting to the file
    // watcher. We drive every rebuild ourselves through ReloadFromText, so a
    // disk change (e.g. our own Save) must not hand the engine back ownership
    // of document_ and dangle model_'s dom_ pointers. This is what lets us load
    // with a real source URL (required for <link>/template resolution below).
    previewUI_->UnsubscribeFromEvent(E_FILECHANGED);

    texture_ = MakeShared<Texture2D>(context_);

    // Design-time documents frequently bind content via data-model=
    // "{{__data_model_id}}". Register an empty placeholder model named
    // identically to the token SubstituteDataModelToken produces, so the binding
    // resolves instead of erroring and leaving the bound subtree unrendered.
    if (Rml::Context* ctx = previewUI_->GetRmlContext())
    {
        const ea::string modelName = SubstituteDataModelToken("{{__data_model_id}}");
        Rml::DataModelConstructor ctor = ctx->CreateDataModel(modelName, nullptr);
        (void)ctor.GetModelHandle();
    }

    Rebuild();

    // Redirect offscreen draws to E_BEGINRENDERING (a fresh frame); RmlUI's
    // default E_ENDALLVIEWSRENDER auto-render is too late to retarget.
    previewUI_->SetRendering(false);
    SubscribeToEvent(E_BEGINRENDERING, URHO3D_HANDLER(UIViewDocument, HandleBeginRendering));
}

UIViewDocument::~UIViewDocument()
{
    if (previewUI_ && previewUI_->GetRmlContext())
        previewUI_->GetRmlContext()->UnloadAllDocuments();
    document_ = nullptr;
}

void UIViewDocument::HandleBeginRendering(StringHash, VariantMap&)
{
    // Layout was already updated on E_POSTUPDATE (CPU-side); here, at the start
    // of the graphics frame, it is safe to issue GPU draws into the offscreen
    // surface. The preview widget samples the resulting texture later this frame.
    if (previewUI_ && previewActive_)
        previewUI_->Render();
}

ea::string UIViewDocument::SubstituteDataModelToken(const ea::string& text) const
{
    // Mirrors the engine's Detail::InsertVariablePlaceholders token/Format pair
    // without depending on that (non-exported) symbol.
    const ea::string id = Format("{}", static_cast<const void*>(this));
    ea::string result = text;
    result.replace("{{__data_model_id}}", id);
    return result;
}

void UIViewDocument::Rebuild()
{
    if (!texture_)
        return;

    // Single mip, set before SetSize (SetNumLevels only feeds the NEXT texture
    // creation). The ImGui backend binds the SRV directly and does not refresh a
    // render-target's mip chain, so a full chain shows never-written lower mips.
    texture_->SetNumLevels(1);
    texture_->SetSize(previewSize_.x_, previewSize_.y_, TextureFormat::TEX_FORMAT_RGBA8_UNORM,
                      TextureFlag::BindRenderTarget);
    texture_->SetFilterMode(FILTER_BILINEAR);
    texture_->SetAddressMode(TextureCoordinate::U, ADDRESS_CLAMP);
    texture_->SetAddressMode(TextureCoordinate::V, ADDRESS_CLAMP);

    RenderSurface* surface = texture_->GetRenderSurface();
    if (surface)
    {
        surface->SetUpdateMode(SURFACE_MANUALUPDATE);
        // Opaque editor-neutral background: RmlUI skips the clear for fully
        // transparent colors, which would leave stale pixels behind.
        previewUI_->SetRenderTarget(surface, Color(0.16f, 0.18f, 0.22f, 1.0f));
    }
    else
    {
        previewUI_->SetRenderTarget(nullptr);
        URHO3D_LOGERROR("UIViewDocument: failed to acquire RenderSurface for preview texture.");
    }
}

// ---------------------------------------------------------------------------
// Loading / whole-projection rebuild
// ---------------------------------------------------------------------------

bool UIViewDocument::LoadFromText(const ea::string& text, const ea::string& path)
{
    path_ = path;
    warnedNoFont_ = false; // per-open diagnostics state
    if (!ReloadFromText(text))
    {
        path_.clear();
        return false;
    }
    dirty_ = false;
    return true;
}

bool UIViewDocument::RestoreText(const ea::string& text)
{
    // Undo/redo snapshot restore: same rebuild path as an ordinary edit. The
    // document stays dirty - only an actual save clears the flag.
    if (!ReloadFromText(text))
        return false;
    dirty_ = true;
    OnModelEdited(this);
    return true;
}

bool UIViewDocument::ReloadFromText(const ea::string& text)
{
    Rml::Context* ctx = previewUI_->GetRmlContext();
    if (!ctx || path_.empty())
        return false;

    ctx->UnloadAllDocuments();
    document_ = nullptr;

    // Load under the document's real resource path so RmlUi resolves relative
    // <link>/<template> hrefs and theme imports the same way the runtime does
    // (RmlFile::Open joins the href onto the source-URL directory). An empty
    // URL would silently drop e.g. <link href="HelloRmlUI_Window.rml"> and
    // leave a template="..." body rendering as a bare, content-less box.
    // E_FILECHANGED is unsubscribed on previewUI_ (see ctor), so using the real
    // URL here stays immune to the engine's hot-reload path.
    document_ = ctx->LoadDocumentFromMemory(
        Rml::String(SubstituteDataModelToken(text).c_str()),
        Rml::String(path_.c_str(), path_.length()));
    if (!document_)
        return false;

    document_->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    // Synchronous layout so views see valid boxes immediately.
    document_->UpdateDocument();

    // Build the editor model from the ORIGINAL text (\a text), not the
    // token-substituted DOM used for the preview: this keeps {{bindings}} /
    // data-model tokens / comments / head verbatim as the source of truth.
    if (!model_.BuildFromText(text, document_))
        return false;

    // A document whose body resolves no font face cannot draw a single glyph
    // (RmlUi has no built-in default font): boxes and borders render, authored
    // text comes out blank. That looks like a rendering bug, so name the real
    // cause once per opened document.
    if (!warnedNoFont_ && model_.root_ && document_->GetFontFaceHandle() == 0
        && HasAuthoredText(*model_.root_))
    {
        warnedNoFont_ = true;
        URHO3D_LOGWARNING(
            "UIViewDocument: '{}' carries text but no effective font-family rule, so text renders "
            "as blank. Add e.g. 'body {{ font-family: \"Noto Sans\"; }}' to the document's <style> "
            "(or link an .rcss that declares one).",
            path_.c_str());
    }
    return true;
}

// ---------------------------------------------------------------------------
// Live-DOM primitives (no undo recording)
// ---------------------------------------------------------------------------

void UIViewDocument::SetLiveBox(UiNode* node, const UiBox& box)
{
    Rml::Element* el = node ? node->dom_ : nullptr;
    if (!el)
        return;
    el->SetProperty("position", "absolute");
    el->SetProperty("box-sizing", "border-box");
    el->SetProperty("left", FormatPx(box.pos_.x_).c_str());
    el->SetProperty("top", FormatPx(box.pos_.y_).c_str());
    el->SetProperty("width", FormatPx(box.size_.x_).c_str());
    el->SetProperty("height", FormatPx(box.size_.y_).c_str());
    const ea::string transform = FormatUiTransform(box.xform_);
    if (transform.empty())
        el->RemoveProperty("transform");
    else
        el->SetProperty("transform", transform.c_str());
}

// ---------------------------------------------------------------------------
// DOM queries
// ---------------------------------------------------------------------------

UiNode* UIViewDocument::HitTest(const Vector2& docPos) const
{
    if (!document_)
        return nullptr;
    return HitTestRecurse(document_, &model_, docPos);
}

bool UIViewDocument::TryGetDomBoxes(const UiNode* node, ea::vector<UiBox>& out) const
{
    out.clear();
    if (!node)
        return false;
    // The nested-doc virtual node outlines the chrome elements themselves
    // (one rect each) rather than the body its dom_ points at: the body's
    // box is the whole canvas and would read as "the entire outer document".
    if (node->IsNestedDoc())
    {
        for (Rml::Element* chrome : node->nestedChromeElems_)
        {
            UiBox box;
            if (Urho3D::TryGetDomBox(chrome, nullptr, box))
                out.push_back(box);
        }
        return !out.empty();
    }
    UiBox box;
    if (!Urho3D::TryGetDomBox(node->dom_, node, box))
        return false;
    out.push_back(box);
    return true;
}

Vector2 UIViewDocument::GetInlineStyleBase(const UiNode* node) const
{
    return node ? InlineStyleBase(node->dom_, node) : Vector2::ZERO;
}

// ---------------------------------------------------------------------------
// Undo plumbing
// ---------------------------------------------------------------------------

bool UIViewDocument::PushUndoAction(const SharedPtr<EditorAction>& action)
{
    // Preferred path: let the owning tab attribute the action to the active
    // resource so ResourceEditorTab can track dirty state and focus the right
    // document on undo/redo. It declines (returns false) when no resource is
    // open, in which case fall back to the raw project undo manager.
    if (undoPusher_ && undoPusher_(action))
        return true;

    // The tab is constructed during project plugin application, when the
    // Project subsystem is already registered, so the undo manager exists.
    // Guard anyway: without it edits simply become non-undoable.
    Project* project = GetSubsystem<Project>();
    if (!project)
        return false;
    project->GetUndoManager()->PushAction(action);
    return true;
}

// ---------------------------------------------------------------------------
// Edit commit: re-emit + whole rebuild + one undoable snapshot
// ---------------------------------------------------------------------------

bool UIViewDocument::EmitAndReload(ea::string& outText)
{
    outText = model_.EmitRml();
    return ReloadFromText(outText);
}

bool UIViewDocument::CorrectLanding(const ea::vector<unsigned>& path, const Vector2& desiredAbs)
{
    UiNode* node = model_.ResolvePath(path);
    if (!node || !node->dom_ || !node->IsMaterialized())
        return false;
    const Vector2 landed = V2(node->dom_->GetAbsoluteOffset(Rml::BoxArea::Border));
    if (landed == desiredAbs)
        return false;
    UiBox box;
    if (!TryGetMaterializedBox(*node, box))
        return false;
    box.pos_ += desiredAbs - landed;
    WriteBoxToStyle(*node, box);
    return true;
}

bool UIViewDocument::CommitAndReload(const ea::string& undoText, const ea::vector<unsigned>& mergeKey,
    const ea::vector<unsigned>* landingPath, const Vector2& desiredAbs)
{
    // Every edit funnels through here: emit the model to text, then rebuild the
    // model and the whole live projection from that text - the exact path a
    // hand-edited file takes when it is (re)opened. The preview can therefore
    // never drift from what the saved source renders.
    ea::string redoText;
    if (!EmitAndReload(redoText))
    {
        URHO3D_LOGERROR("UIViewDocument: projection reload failed; restoring the pre-edit document.");
        ReloadFromText(undoText); // best effort: keep model and projection in sync
        return false;
    }

    // Synthetic absolute boxes (born-centered widgets, baked materializations)
    // must land exactly where they were authored; correct left/top once if the
    // containing block shifted the element, then rebuild from the fixed text.
    if (landingPath && CorrectLanding(*landingPath, desiredAbs))
    {
        if (!EmitAndReload(redoText))
        {
            URHO3D_LOGERROR("UIViewDocument: projection reload failed; restoring the pre-edit document.");
            ReloadFromText(undoText);
            return false;
        }
    }

    PushUndoAction(MakeShared<UiDocumentSnapshotAction>(this, mergeKey, undoText, redoText));
    dirty_ = true;
    OnModelEdited(this);
    return true;
}

// ---------------------------------------------------------------------------
// Undoable editing commands
// ---------------------------------------------------------------------------

UiNode* UIViewDocument::AddWidget(UiNode* parent, const char* tag)
{
    if (!model_.root_ || !document_ || !tag)
        return nullptr;
    if (!parent || parent->IsText() || parent->IsNestedDoc())
        parent = model_.root_.Get();

    auto node = MakeShared<UiNode>();
    const ea::string kind = tag;
    if (kind == "text")
    {
        // RmlUi has no standalone text element: text lives inside a block.
        // Emit a plain <div> carrying a text node, styled as visible text
        // (no fill box) so it reads as a label rather than an empty panel.
        node->tag_ = "div";
        auto text = MakeShared<UiNode>();
        text->tag_ = "#text";
        text->text_ = "Text";
        node->children_.push_back(text);
        node->SetStyle("color", "#e8eef5");
    }
    else
    {
        node->tag_ = kind;
        if (kind == "button")
        {
            auto text = MakeShared<UiNode>();
            text->tag_ = "#text";
            text->text_ = "Button";
            node->children_.push_back(text);
        }
        else if (kind == "img")
        {
            node->attributes_.emplace_back("src", "");
        }
        node->SetStyle("background-color", "#3a4656");
        node->SetStyle("border", "1px solid #6f86a6");
    }

    // Born materialized: 160x48 centered inside the parent's rendered box when
    // it has one (else the preview viewport), so the widget lands in view and
    // is draggable at once.
    UiBox box;
    box.size_ = Vector2{160.0f, 48.0f};
    float cw = static_cast<float>(kPreviewWidth);
    float ch = static_cast<float>(kPreviewHeight);
    const Vector2 psz = parent->dom_ ? V2(parent->dom_->GetBox().GetSize(Rml::BoxArea::Border)) : Vector2::ZERO;
    if (psz.x_ > box.size_.x_)
        cw = psz.x_;
    if (psz.y_ > box.size_.y_)
        ch = psz.y_;
    box.pos_ = Vector2{Max(cw - box.size_.x_, 0.0f) * 0.5f, Max(ch - box.size_.y_, 0.0f) * 0.5f};
    WriteBoxToStyle(*node, box);

    const ea::string undoText = model_.EmitRml();
    ea::vector<unsigned> parentPath;
    if (!model_.BuildPath(parent, parentPath))
        return nullptr;
    const unsigned indexInParent = parent->children_.size();
    parent->children_.push_back(node);

    ea::vector<unsigned> nodePath = parentPath;
    nodePath.push_back(indexInParent);

    // Precise centering: where the widget should end up in absolute document
    // coordinates (centered inside the parent's own rendered box), used by the
    // one-shot landing correction after the rebuild.
    Vector2 desiredAbs = Vector2::ZERO;
    bool haveDesired = false;
    if (parent->dom_)
    {
        const Vector2 pAbs = V2(parent->dom_->GetAbsoluteOffset(Rml::BoxArea::Border));
        desiredAbs = pAbs + Vector2{Max(psz.x_ - box.size_.x_, 0.0f) * 0.5f,
                                    Max(psz.y_ - box.size_.y_, 0.0f) * 0.5f};
        haveDesired = true;
    }

    if (!CommitAndReload(undoText, {}, haveDesired ? &nodePath : nullptr, desiredAbs))
        return nullptr;
    return model_.ResolvePath(nodePath);
}

UiNode* UIViewDocument::DuplicateNode(UiNode* node)
{
    if (!node || node == model_.root_.Get() || node->IsText() || node->IsNestedDoc())
        return nullptr;
    UiNode* parent = model_.FindParent(node);
    if (!parent)
        return nullptr;

    const ea::string undoText = model_.EmitRml();
    ea::vector<unsigned> parentPath;
    if (!model_.BuildPath(parent, parentPath))
        return nullptr;

    SharedPtr<UiNode> copy = DeepCloneUiNode(*node);
    if (!copy->id_.empty())
        copy->id_ += "-2";
    // Nudge a materialized copy so it does not sit exactly on the original.
    UiBox b;
    if (TryGetMaterializedBox(*copy, b))
    {
        b.pos_ += Vector2{16.0f, 16.0f};
        WriteBoxToStyle(*copy, b);
    }

    const unsigned indexInParent = parent->children_.size();
    parent->children_.push_back(copy);

    ea::vector<unsigned> copyPath = parentPath;
    copyPath.push_back(indexInParent);

    // The copy should sit 16px off the original's rendered spot.
    Vector2 desiredAbs = Vector2::ZERO;
    bool haveDesired = false;
    if (node->dom_)
    {
        desiredAbs = V2(node->dom_->GetAbsoluteOffset(Rml::BoxArea::Border)) + Vector2{16.0f, 16.0f};
        haveDesired = true;
    }

    if (!CommitAndReload(undoText, {}, haveDesired ? &copyPath : nullptr, desiredAbs))
        return nullptr;
    return model_.ResolvePath(copyPath);
}

bool UIViewDocument::DeleteNode(UiNode* node)
{
    if (!node || node == model_.root_.Get() || node->IsText() || node->IsNestedDoc())
        return false;
    UiNode* parent = model_.FindParent(node);
    if (!parent)
        return false;

    unsigned indexInParent = 0;
    for (unsigned i = 0; i < parent->children_.size(); i++)
    {
        if (parent->children_[i] == node)
        {
            indexInParent = i;
            break;
        }
    }

    const ea::string undoText = model_.EmitRml();
    parent->children_.erase(parent->children_.begin() + indexInParent);
    return CommitAndReload(undoText, {});
}

void ResetSpineAnchors(UiNode& node)
{
    node.srcNode_ = -1;
    for (const SharedPtr<UiNode>& child : node.children_)
        ResetSpineAnchors(*child);
}

UiNode* UIViewDocument::MoveNode(UiNode* node, UiNode* newParent, unsigned index)
{
    if (!node || node == model_.root_.Get() || node->IsText() || node->IsNestedDoc())
        return nullptr;
    if (!newParent || newParent->IsText() || newParent->IsNestedDoc() || newParent == node)
        return nullptr;
    // Reparenting into the moved subtree would cut that subtree out of the
    // tree: reject when the new parent is the node itself or any descendant.
    for (UiNode* it = newParent; it; it = model_.FindParent(it))
    {
        if (it == node)
            return nullptr;
    }

    UiNode* oldParent = model_.FindParent(node);
    if (!oldParent)
        return nullptr;

    const ea::string undoText = model_.EmitRml();
    ea::vector<unsigned> newParentPath;
    if (!model_.BuildPath(newParent, newParentPath))
        return nullptr;

    unsigned oldIndex = 0;
    bool found = false;
    for (unsigned i = 0; i < oldParent->children_.size(); i++)
    {
        if (oldParent->children_[i].Get() == node)
        {
            oldIndex = i;
            found = true;
            break;
        }
    }
    if (!found)
        return nullptr;

    // Keep the subtree alive across the erase, then splice it in. Moving
    // within one container shifts the indices behind the removed slot.
    SharedPtr<UiNode> held = oldParent->children_[oldIndex];
    oldParent->children_.erase(oldParent->children_.begin() + oldIndex);
    // The moved subtree no longer lives where the spine anchors it: a kept
    // anchor would make emit diff both ends wrong - the old parent sees its
    // source child unreferenced and patches the bytes away, while the new
    // parent sees an anchored child and generates nothing in its place,
    // net effect: the node vanishes from the document. Drop every spine
    // anchor in the subtree: the old position is deleted as such, and the
    // new one is whole-subtree generated (GenSubtree) like any editor-made
    // widget. Rebuild re-anchors everything from the new text.
    ResetSpineAnchors(*held);
    if (oldParent == newParent && index > oldIndex)
        --index;
    index = Min(index, static_cast<unsigned>(newParent->children_.size()));
    newParent->children_.insert(newParent->children_.begin() + index, held);

    ea::vector<unsigned> newPath = newParentPath;
    newPath.push_back(index);
    // One undo step per move (empty merge key); the element is expected to
    // land at its flow position, so no landing correction.
    if (!CommitAndReload(undoText, {}))
        return nullptr;
    return model_.ResolvePath(newPath);
}

bool UIViewDocument::MaterializeNode(UiNode* node)
{
    if (!node || !node->dom_ || node->IsText() || node->IsNestedDoc() || node->IsMaterialized())
        return false;

    // Bake the computed border box into explicit px style, then let the reload
    // below land the element where the authored box says. CommitAndReload's
    // landing correction fixes any containing-block offset afterwards, so
    // nothing visibly moves (regardless of the containing block's padding or
    // border).
    const ea::string undoText = model_.EmitRml();
    ea::vector<unsigned> path;
    if (!model_.BuildPath(node, path))
        return false;

    Rml::Element* el = node->dom_;
    const Vector2 absBefore = V2(el->GetAbsoluteOffset(Rml::BoxArea::Border));
    const Vector2 size = V2(el->GetBox().GetSize(Rml::BoxArea::Border));
    Rml::Element* parent = el->GetOffsetParent();
    const Vector2 base = parent ? V2(parent->GetAbsoluteOffset(Rml::BoxArea::Border)) : Vector2::ZERO;

    UiBox box;
    box.pos_ = absBefore - base;
    box.size_ = size;
    box.xform_ = ParseUiTransform(node->GetStyle("transform"));
    WriteBoxToStyle(*node, box);

    return CommitAndReload(undoText, path, &path, absBefore);
}

bool UIViewDocument::DematerializeNode(UiNode* node)
{
    if (!node || node->IsNestedDoc() || !node->IsMaterialized())
        return false;

    // Drop only the pin - position + left/top. width/height/box-sizing and
    // transform stay behind: they remain meaningful (and commonly authored)
    // for the re-flowed element, e.g. a fixed-size button, so stripping them
    // would silently discard intent that may predate the materialize.
    // Routed through EditNodePayload, so this is one undoable, mergeable
    // style edit like any hand edit in the Inspector.
    UiNodePayload payload = SnapshotUiNodePayload(*node);
    payload.style_.erase(std::remove_if(payload.style_.begin(), payload.style_.end(),
        [](const UiStyleDecl& decl)
        {
            return decl.name_ == "position" || decl.name_ == "left" || decl.name_ == "top";
        }), payload.style_.end());
    return EditNodePayload(node, payload);
}

bool UIViewDocument::EditNodePayload(UiNode* node, const UiNodePayload& newData)
{
    if (!node || node->IsNestedDoc())
        return false;
    const ea::string undoText = model_.EmitRml();
    // Child-index path doubles as the merge key: consecutive payload edits of
    // the same node collapse into one undo step.
    ea::vector<unsigned> mergeKey;
    if (!model_.BuildPath(node, mergeKey))
        return false;
    ApplyUiNodePayload(*node, newData);
    return CommitAndReload(undoText, mergeKey);
}

bool UIViewDocument::CommitBoxEdit(UiNode* node, const UiBox& box)
{
    if (!node || node->IsNestedDoc())
        return false;
    const ea::string undoText = model_.EmitRml();
    ea::vector<unsigned> mergeKey;
    if (!model_.BuildPath(node, mergeKey))
        return false;
    // The live DOM already shows the dragged box (SetLiveBox); the reload just
    // makes model, text and projection canonically identical again.
    WriteBoxToStyle(*node, box);
    return CommitAndReload(undoText, mergeKey);
}

}

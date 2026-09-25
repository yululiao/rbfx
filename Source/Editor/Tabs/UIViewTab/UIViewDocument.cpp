//
// Copyright (c) 2017-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#include "UIViewDocument.h"

#include "UIViewActions.h"
#include "UIViewParagraphText.h"

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

#include <EASTL/sort.h>

namespace Urho3D
{

namespace
{

// Static preview resolution. The document lays out against this virtual
// viewport; the preview widget then scales it down to fit the tab.
constexpr int kPreviewWidth = 1024;
constexpr int kPreviewHeight = 768;

Vector2 V2(const Rml::Vector2f& v) { return Vector2{v.x, v.y}; }

std::string Std(const ea::string& s)
{
    return std::string(s.c_str(), s.length());
}

ea::string Ea(const std::string& s)
{
    return ea::string(s.c_str(), s.length());
}

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

// Fit the element's full accumulated transform (own + ancestors + their
// perspectives) as a layout->window affine. Element::Project runs the exact
// math RmlUi uses for event picking, so probe it at three window points, fit
// window->layout, and store the inverse. Exact for translate/rotate/scale/
// skew chains; under `perspective` a second pass anchored at the element keeps
// the fit locally accurate. Identity (window == layout) when the projection is
// unavailable or the fit turns singular.
void CaptureWindowMap(Rml::Element* element, UiBox& out)
{
    out.winBasisX_ = Vector2(1, 0);
    out.winBasisY_ = Vector2(0, 1);
    out.winOrigin_ = Vector2::ZERO;
    if (!element)
        return;

    const auto fit = [element](const Vector2& at, float d, Vector2& basisX, Vector2& basisY, Vector2& offset) {
        Rml::Vector2f p0{at.x_, at.y_};
        Rml::Vector2f p1{at.x_ + d, at.y_};
        Rml::Vector2f p2{at.x_, at.y_ + d};
        if (!element->Project(p0) || !element->Project(p1) || !element->Project(p2))
            return false;
        basisX = (Vector2(p1.x, p1.y) - Vector2(p0.x, p0.y)) * (1.0f / d);
        basisY = (Vector2(p2.x, p2.y) - Vector2(p0.x, p0.y)) * (1.0f / d);
        // p0 = M*at + b: the stored model is `layout = M*window + offset`, so the
        // probe must return the pure translation b = p0 - M*at. Returning p0
        // alone folds M*at into the offset for any probe away from the origin,
        // shifting the captured map by -at and drifting every overlay box toward
        // the top-left by its own center - even on documents with no transforms.
        offset = Vector2(p0.x, p0.y) - (at.x_ * basisX + at.y_ * basisY);
        return true;
    };
    // layout = M * window + offset, M's columns are basisX/basisY; store the inverse.
    const auto store = [&out](const Vector2& basisX, const Vector2& basisY, const Vector2& offset) {
        const float det = basisX.x_ * basisY.y_ - basisY.x_ * basisX.y_;
        if (det < 1e-9f && det > -1e-9f)
            return false; // singular: keep the previous fit
        const float inv = 1.0f / det;
        out.winBasisX_ = Vector2(basisY.y_, -basisX.y_) * inv;
        out.winBasisY_ = Vector2(-basisY.x_, basisX.x_) * inv;
        out.winOrigin_ = Vector2(basisY.x_ * offset.y_ - basisY.y_ * offset.x_,
            basisX.y_ * offset.x_ - basisX.x_ * offset.y_) * inv;
        return true;
    };

    Vector2 basisX, basisY, offset;
    if (!fit(Vector2::ZERO, 64.0f, basisX, basisY, offset) || !store(basisX, basisY, offset))
        return;
    // Refine at the element itself so perspective-heavy chains stay accurate.
    const Vector2 centerWin = out.MapToWindow(out.Center());
    if (fit(centerWin, 32.0f, basisX, basisY, offset))
        store(basisX, basisY, offset);
}

// The border box (document space) of a live DOM element, plus the node's own
// emitted transform and the element's full layout->window transform map.
bool TryGetDomBox(Rml::Element* element, const UiNode* node, UiBox& out)
{
    if (!element)
        return false;
    out.pos_ = V2(element->GetAbsoluteOffset(Rml::BoxArea::Border));
    out.size_ = V2(element->GetBox().GetSize(Rml::BoxArea::Border));
    out.xform_ = node ? ParseUiTransform(node->GetStyle("transform")) : UiTransform{};
    // GetAbsoluteOffset is pure layout math and ignores the transform chain
    // entirely; without the captured map the overlay drifts off the rendered
    // element under any ancestor transform (e.g. a scaled parent).
    CaptureWindowMap(element, out);
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
    // Pick through the element's FULL transform chain (own + ancestors +
    // perspective): Project maps the doc-space point into the untransformed
    // layout frame exactly the way RmlUi's own event dispatch does. A singular
    // chain leaves the element unpickable, mirroring Event::StopPropagation.
    Rml::Vector2f local{point.x_, point.y_};
    if (!element->Project(local))
        return nullptr;
    if (local.x < box.pos_.x_ || local.x > box.pos_.x_ + box.size_.x_ ||
        local.y < box.pos_.y_ || local.y > box.pos_.y_ + box.size_.y_)
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

// Collect every real element node (no text/virtual) whose rendered border box
// lies fully inside the document-space rect [lo,hi], in model pre-order. Root
// is never a candidate (its box is the whole canvas). Backs marquee selection.
void CollectInRectRecurse(UiNode* node, const Vector2& lo, const Vector2& hi, ea::vector<UiNode*>& out)
{
    for (const SharedPtr<UiNode>& child : node->children_)
    {
        if (child->IsText() || child->IsNestedDoc() || child->IsHeadLink())
            continue;
        if (Rml::Element* el = child->dom_)
        {
            const Vector2 pos = V2(el->GetAbsoluteOffset(Rml::BoxArea::Border));
            const Vector2 size = V2(el->GetBox().GetSize(Rml::BoxArea::Border));
            if (size.x_ > 0.0f && size.y_ > 0.0f && pos.x_ >= lo.x_ && pos.y_ >= lo.y_
                && pos.x_ + size.x_ <= hi.x_ && pos.y_ + size.y_ <= hi.y_)
            {
                out.push_back(child.Get());
            }
        }
        CollectInRectRecurse(child.Get(), lo, hi, out);
    }
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
    instanceId_ = ++instanceCounter;
    const ea::string contextName = Format("UIViewPreview-{}", instanceId_);
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

void UIViewDocument::SetPreviewSize(const IntVector2& size)
{
    if (size.x_ <= 0 || size.y_ <= 0 || size == previewSize_)
        return;
    previewSize_ = size;
    // The rebuild retargets the RmlUi context to the new surface size
    // (RmlUI::SetRenderTarget feeds the context dimensions), so the next
    // update lays the document out against the new canvas.
    Rebuild();
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

bool UIViewDocument::TryGetBoxModel(const UiNode* node, UiBoxModel& out) const
{
    // The box model describes a regular element's own four areas; the
    // nested-doc virtual node has no box of its own (its dom_ is the outer
    // body) and would only mislead.
    if (!node || node->IsNestedDoc())
        return false;
    if (!Urho3D::TryGetDomBox(node->dom_, node, out.border_))
        return false;
    // Every area shares the border box's layout->window map; the layout
    // rectangle of another area differs only by the Box's own edge offsets
    // (margin offsets come out negative, as in CSS).
    const Rml::Box& box = node->dom_->GetBox();
    const Vector2 borderPos = out.border_.pos_;
    out.padding_ = out.border_;
    out.padding_.pos_ = borderPos + V2(box.GetPosition(Rml::BoxArea::Padding));
    out.padding_.size_ = V2(box.GetSize(Rml::BoxArea::Padding));
    out.content_ = out.border_;
    out.content_.pos_ = borderPos + V2(box.GetPosition(Rml::BoxArea::Content));
    out.content_.size_ = V2(box.GetSize(Rml::BoxArea::Content));
    out.margin_ = out.border_;
    out.margin_.pos_ = borderPos + V2(box.GetPosition(Rml::BoxArea::Margin));
    out.margin_.size_ = V2(box.GetSize(Rml::BoxArea::Margin));
    return true;
}

Vector2 UIViewDocument::GetInlineStyleBase(const UiNode* node) const
{
    return node ? InlineStyleBase(node->dom_, node) : Vector2::ZERO;
}

bool UIViewDocument::TryGetDragBox(const UiNode* node, UiBox& out, Vector2& base) const
{
    base = Vector2::ZERO;
    // The gizmo is offered purely on the position declaration: only an
    // absolutely positioned box is dragged (dragging writes position:absolute +
    // left/top/width/height, so relative/fixed/none must not trigger it).
    if (!node || !node->dom_ || node->GetStyle("position") != "absolute")
        return false;
    // Authored geometry wins: exact, and it is the frame WriteBoxToStyle keeps.
    if (TryGetMaterializedBox(*node, out))
    {
        base = InlineStyleBase(node->dom_, node);
        CaptureWindowMap(node->dom_, out);
        return true;
    }
    // Positioned but not yet sized/offset (position freshly set from the Style
    // panel): seed the box from the rendered border box against its containing
    // block, so the element is immediately draggable from where it already is.
    Rml::Element* el = node->dom_;
    Rml::Element* containing = el->GetOffsetParent();
    base = containing ? V2(containing->GetAbsoluteOffset(Rml::BoxArea::Border)) : Vector2::ZERO;
    out.pos_ = V2(el->GetAbsoluteOffset(Rml::BoxArea::Border)) - base;
    out.size_ = V2(el->GetBox().GetSize(Rml::BoxArea::Border));
    out.xform_ = ParseUiTransform(node->GetStyle("transform"));
    CaptureWindowMap(el, out);
    return true;
}

void UIViewDocument::RefreshWindowMap(const UiNode* node, UiBox& box) const
{
    if (node && node->dom_ && !node->IsNestedDoc())
        CaptureWindowMap(node->dom_, box);
}

Vector2 UIViewDocument::DocToGizmoFrame(const UiNode* node, const Vector2& base, const Vector2& docPoint) const
{
    if (node && node->dom_ && !node->IsNestedDoc())
    {
        // Project unwinds the element's FULL transform chain (ancestors + own +
        // perspective) into untransformed layout space - the same math RmlUi's
        // event dispatch uses. ForwardMapPoint then re-applies the node's own
        // authored transform from the live DOM box (whose pivot matches
        // Project's), so the net effect strips ONLY the ancestor chain.
        UiBox box;
        if (Urho3D::TryGetDomBox(node->dom_, node, box))
        {
            Rml::Vector2f p{docPoint.x_, docPoint.y_};
            if (node->dom_->Project(p))
                return ForwardMapPoint(Vector2(p.x, p.y), box) - base;
        }
    }
    return docPoint - base;
}

ea::vector<UiNode*> UIViewDocument::CollectNodesInRect(const Vector2& a, const Vector2& b) const
{
    ea::vector<UiNode*> out;
    if (!model_.root_)
        return out;
    const Vector2 lo(Min(a.x_, b.x_), Min(a.y_, b.y_));
    const Vector2 hi(Max(a.x_, b.x_), Max(a.y_, b.y_));
    CollectInRectRecurse(model_.root_.Get(), lo, hi, out);
    return out;
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

UiNode* UIViewDocument::AddWidget(UiNode* parent, const UiWidgetSpec& spec, unsigned index)
{
    if (!model_.root_ || !document_ || spec.tag_.empty())
        return nullptr;
    if (!parent || parent->IsText() || parent->IsNestedDoc() || parent->IsHeadLink())
        parent = model_.root_.Get();

    auto node = MakeShared<UiNode>();
    node->tag_ = spec.tag_;
    if (!spec.attrName_.empty())
        node->attributes_.emplace_back(spec.attrName_, spec.attrValue_);
    // Default content: a single text child ("Button", "Text") or repeated
    // child elements (a <select> is only usable once it has <option>s).
    if (!spec.childText_.empty())
    {
        auto text = MakeShared<UiNode>();
        text->tag_ = "#text";
        text->text_ = spec.childText_;
        node->children_.push_back(text);
    }
    for (unsigned i = 0; i < spec.childElemCount_; i++)
    {
        auto elem = MakeShared<UiNode>();
        elem->tag_ = spec.childElemTag_;
        if (!spec.childElemText_.empty())
        {
            auto text = MakeShared<UiNode>();
            text->tag_ = "#text";
            text->text_ = spec.childElemText_;
            elem->children_.push_back(text);
        }
        node->children_.push_back(elem);
    }
    switch (spec.stylePolicy_)
    {
    case UiWidgetStylePolicy::Panel:
        node->SetStyle("background-color", "#3a4656");
        // RmlUi's `border` shorthand maps to width + color only; it has no
        // border-style property, so a CSS `solid` keyword here fails the whole
        // declaration parse (and the placeholder border never renders).
        node->SetStyle("border", "1px #6f86a6");
        break;
    case UiWidgetStylePolicy::Outline:
        // Border only: shape without a fill, so the control's internal chrome
        // (project-styled or not) stays untouched.
        node->SetStyle("border", "1px #6f86a6");
        break;
    case UiWidgetStylePolicy::None:
    default:
        break;
    }

    const Vector2 psz = parent->dom_
        ? V2(parent->dom_->GetBox().GetSize(Rml::BoxArea::Border)) : Vector2::ZERO;

    // Born materialized: explicitly sized, centered inside the parent's
    // rendered box when it has one (else the preview viewport), so the widget
    // lands in view and is draggable at once. Flow-born entries (text labels)
    // skip the box and join the flow at the requested slot.
    if (spec.materialize_)
    {
        UiBox box;
        box.size_ = spec.size_;
        float cw = static_cast<float>(kPreviewWidth);
        float ch = static_cast<float>(kPreviewHeight);
        if (psz.x_ > box.size_.x_)
            cw = psz.x_;
        if (psz.y_ > box.size_.y_)
            ch = psz.y_;
        box.pos_ = Vector2{Max(cw - box.size_.x_, 0.0f) * 0.5f, Max(ch - box.size_.y_, 0.0f) * 0.5f};
        WriteBoxToStyle(*node, box);
    }

    // Flow-born widgets have no position box, but some are nothing without
    // explicit dimensions: an <img> with no stylesheet rule has no intrinsic
    // size and would collapse to nothing. Width/height stay author-visible
    // inline styles so they remain editable in the Style panel.
    if (!spec.materialize_ && spec.flowSize_.x_ > 0.0f && spec.flowSize_.y_ > 0.0f)
    {
        node->SetStyle("width", FormatPx(spec.flowSize_.x_));
        node->SetStyle("height", FormatPx(spec.flowSize_.y_));
    }

    const ea::string undoText = model_.EmitRml();
    ea::vector<unsigned> parentPath;
    if (!model_.BuildPath(parent, parentPath))
        return nullptr;
    // Insert at the requested slot (a drop may land mid-list, not only at the
    // end); the full child-index numbering matches what MoveNode takes.
    const unsigned indexInParent = Min(index, static_cast<unsigned>(parent->children_.size()));
    parent->children_.insert(parent->children_.begin() + indexInParent, node);

    ea::vector<unsigned> nodePath = parentPath;
    nodePath.push_back(indexInParent);

    // Precise centering: where the widget should end up in absolute document
    // coordinates (centered inside the parent's own rendered box), used by the
    // one-shot landing correction after the rebuild. Flow widgets have no
    // authored position - the flow decides - so they take no correction.
    Vector2 desiredAbs = Vector2::ZERO;
    bool haveDesired = false;
    if (spec.materialize_ && parent->dom_)
    {
        const Vector2 pAbs = V2(parent->dom_->GetAbsoluteOffset(Rml::BoxArea::Border));
        desiredAbs = pAbs + Vector2{Max(psz.x_ - spec.size_.x_, 0.0f) * 0.5f,
                                    Max(psz.y_ - spec.size_.y_, 0.0f) * 0.5f};
        haveDesired = true;
    }

    if (!CommitAndReload(undoText, {}, haveDesired ? &nodePath : nullptr, desiredAbs))
        return nullptr;
    return model_.ResolvePath(nodePath);
}

UiNode* UIViewDocument::DuplicateNode(UiNode* node)
{
    if (!node || node == model_.root_.Get() || node->IsText() || node->IsNestedDoc()
        || node->IsHeadLink())
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

ea::vector<UiNode*> UIViewDocument::DuplicateNodes(const ea::vector<UiNode*>& nodes)
{
    ea::vector<UiNode*> results;
    if (!model_.root_ || !document_)
        return results;

    const ea::string undoText = model_.EmitRml();
    ea::vector<ea::vector<unsigned>> copyPaths;
    // Clone each top-level selection and append it to its own parent. Appending
    // never reallocates another selected node's heap object (each is held by a
    // SharedPtr), so every raw pointer stays valid until the single reload below.
    for (UiNode* node : nodes)
    {
        if (!node || node == model_.root_.Get() || node->IsText() || node->IsNestedDoc()
            || node->IsHeadLink())
            continue;
        UiNode* parent = model_.FindParent(node);
        if (!parent)
            continue;

        SharedPtr<UiNode> copy = DeepCloneUiNode(*node);
        if (!copy->id_.empty())
            copy->id_ += "-2";
        UiBox b;
        if (TryGetMaterializedBox(*copy, b))
        {
            b.pos_ += Vector2{16.0f, 16.0f};
            WriteBoxToStyle(*copy, b);
        }
        const unsigned indexInParent = static_cast<unsigned>(parent->children_.size());
        parent->children_.push_back(copy);

        ea::vector<unsigned> parentPath;
        if (model_.BuildPath(parent, parentPath))
        {
            parentPath.push_back(indexInParent);
            copyPaths.push_back(parentPath);
        }
    }

    if (copyPaths.empty())
        return results;
    if (!CommitAndReload(undoText, {}))
        return results;
    for (const ea::vector<unsigned>& path : copyPaths)
    {
        if (UiNode* r = model_.ResolvePath(path))
            results.push_back(r);
    }
    return results;
}

bool UIViewDocument::DeleteNode(UiNode* node)
{
    if (!node || node == model_.root_.Get() || node->IsText())
        return false;
    // Virtual nodes that stand for a <head> <link> line: removal is a
    // text-level edit, not a tree operation. (The nested-doc node is the
    // instantiated template link; deleting it removes the <link> line, so the
    // chrome disappears with it - exactly what the user asked for.)
    if (node->IsHeadLink() || node->IsNestedDoc())
    {
        const ea::string undoText = model_.EmitRml();
        ea::string redoText;
        if (!model_.RemoveHeadLinkAt(undoText, node->headLinkOrdinal_, redoText))
            return false; // ordinal went stale (rebuilt with fewer links)
        return CommitTextEdit(undoText, redoText);
    }
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

bool UIViewDocument::DeleteNodes(const ea::vector<UiNode*>& nodes)
{
    if (!model_.root_ || nodes.empty())
        return false;

    const ea::string undoText = model_.EmitRml();
    bool changed = false;
    // Callers guarantee no node is a descendant of another, so erasing one
    // never frees another's SharedPtr; every raw pointer stays live until the
    // single rebuild.
    for (UiNode* node : nodes)
    {
        if (!node || node == model_.root_.Get() || node->IsText() || node->IsNestedDoc()
            || node->IsHeadLink())
            continue;
        UiNode* parent = model_.FindParent(node);
        if (!parent)
            continue;
        for (size_t i = 0; i < parent->children_.size(); i++)
        {
            if (parent->children_[i].Get() == node)
            {
                parent->children_.erase(parent->children_.begin() + i);
                changed = true;
                break;
            }
        }
    }
    if (!changed)
        return false;
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
    if (!node || node == model_.root_.Get() || node->IsText() || node->IsNestedDoc()
        || node->IsHeadLink())
        return nullptr;
    if (!newParent || newParent->IsText() || newParent->IsNestedDoc() || newParent->IsHeadLink()
        || newParent == node)
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

UiNode* UIViewDocument::WrapNodes(const ea::vector<UiNode*>& nodes, UiWrapMode mode)
{
    if (nodes.empty())
        return nullptr;
    UiNode* parent = model_.FindParent(nodes[0]);
    if (!parent)
        return nullptr;

    // Every target must be a real element and share one parent: a cross-parent
    // wrap would tear siblings out of different flows, and absolutely
    // positioned nodes are rejected because their insets re-anchor against
    // the new containing block (the visual jump the menus warn about).
    for (UiNode* node : nodes)
    {
        if (!node || node == model_.root_.Get() || node->IsText() || node->IsNestedDoc()
            || node->IsHeadLink())
            return nullptr;
        if (model_.FindParent(node) != parent)
            return nullptr;
        if (node->GetStyle("position") == "absolute")
            return nullptr;
    }

    auto indexOf = [parent](UiNode* node) -> unsigned {
        for (unsigned i = 0; i < parent->children_.size(); i++)
        {
            if (parent->children_[i].Get() == node)
                return i;
        }
        return M_MAX_UNSIGNED;
    };

    // Operate in document order regardless of the selection order: the
    // wrapped children keep their authored sibling sequence, and the
    // container takes the slot of the first (topmost) one.
    ea::vector<UiNode*> ordered = nodes;
    ea::sort(ordered.begin(), ordered.end(),
        [&indexOf](UiNode* a, UiNode* b) { return indexOf(a) < indexOf(b); });
    const unsigned insertIndex = indexOf(ordered[0]);
    if (insertIndex == M_MAX_UNSIGNED)
        return nullptr;

    const ea::string undoText = model_.EmitRml();

    auto container = MakeShared<UiNode>();
    container->tag_ = "div";
    if (mode == UiWrapMode::Row || mode == UiWrapMode::Column)
    {
        container->SetStyle("display", "flex");
        container->SetStyle("flex-direction", mode == UiWrapMode::Row ? "row" : "column");
        container->SetStyle("gap", "8px");
    }
    // UiWrapMode::Box authors no look at all: a bare grouping <div>.

    // Detach the targets in document order, then splice them into the
    // container. Like MoveNode, the moved subtrees lose their spine anchors:
    // their old positions are deleted as such and the new ones are generated
    // whole (GenSubtree), which is what keeps the emit diff exact.
    ea::vector<SharedPtr<UiNode>> held;
    for (UiNode* node : ordered)
    {
        const unsigned index = indexOf(node);
        if (index == M_MAX_UNSIGNED)
            continue;
        held.push_back(parent->children_[index]);
        parent->children_.erase(parent->children_.begin() + index);
        ResetSpineAnchors(*held.back());
    }
    for (const SharedPtr<UiNode>& node : held)
        container->children_.push_back(node);
    parent->children_.insert(parent->children_.begin() + insertIndex, container);

    ea::vector<unsigned> containerPath;
    if (!model_.BuildPath(parent, containerPath))
        return nullptr;
    containerPath.push_back(insertIndex);
    // One undo step for the whole wrap (empty merge key, like MoveNode).
    if (!CommitAndReload(undoText, {}))
        return nullptr;
    return model_.ResolvePath(containerPath);
}

bool UIViewDocument::EditNodePayload(UiNode* node, const UiNodePayload& newData)
{
    if (!node || node->IsNestedDoc() || node->IsHeadLink())
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

bool UIViewDocument::SetParagraphText(UiNode* element, const ea::string& buffer)
{
    if (!element || element == model_.root_.Get() || element->IsText() || element->IsNestedDoc()
        || element->IsHeadLink())
    {
        return false;
    }

    // The node must be the editable paragraph shape: children are text runs and
    // bare <br/> only. Anything else (mixed content, attributed <br>, ...) is
    // out of scope for the structural Content editor.
    std::vector<ParagraphChildState> children;
    children.reserve(element->children_.size());
    for (const SharedPtr<UiNode>& child : element->children_)
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

    const std::vector<std::string> newLines = NormalizedParagraphLines(Std(buffer));

    std::vector<std::string> oldLines;
    std::vector<ParagraphPlanEntry> plan;
    if (ParagraphLinesOfChildren(children, oldLines) && oldLines == newLines)
        return false; // no-op: identical lines, nothing to record
    if (!PlanParagraphChildren(children, newLines, plan))
        plan = FreshParagraphPlan(newLines);

    // Materialize the plan: reused slots keep their node (spine anchor and
    // exact bytes), the rest are minted here and spliced at save time.
    const ea::string undoText = model_.EmitRml();
    ea::vector<SharedPtr<UiNode>> rebuilt;
    rebuilt.reserve(plan.size());
    for (const ParagraphPlanEntry& entry : plan)
    {
        if (entry.reuseChild >= 0)
        {
            rebuilt.push_back(element->children_[static_cast<unsigned>(entry.reuseChild)]);
        }
        else if (entry.isBreak)
        {
            auto br = MakeShared<UiNode>();
            br->tag_ = "br";
            rebuilt.push_back(br);
        }
        else
        {
            auto run = MakeShared<UiNode>();
            run->tag_ = "#text";
            run->text_ = Ea(entry.text);
            rebuilt.push_back(run);
        }
    }

    element->textRunsRebuilt_ = true;
    element->children_ = rebuilt;
    // Empty merge key: every Apply is its own undo step (empty keys never merge).
    return CommitAndReload(undoText, {});
}

bool UIViewDocument::CommitBoxEdit(UiNode* node, const UiBox& box)
{
    if (!node || node->IsNestedDoc() || node->IsHeadLink())
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

bool UIViewDocument::CommitBoxEdits(const ea::vector<ea::pair<UiNode*, UiBox>>& edits)
{
    if (!model_.root_ || edits.empty())
        return false;
    const ea::string undoText = model_.EmitRml();
    // Write every box into the model first (all node pointers are live), then
    // do the one rebuild - so a multi-selection drag is a single undo step.
    bool changed = false;
    for (const ea::pair<UiNode*, UiBox>& edit : edits)
    {
        UiNode* node = edit.first;
        if (!node || node->IsNestedDoc() || node->IsHeadLink())
            continue;
        WriteBoxToStyle(*node, edit.second);
        changed = true;
    }
    if (!changed)
        return false;
    // Empty merge key: a drag gesture is one discrete step regardless of count.
    return CommitAndReload(undoText, {});
}

bool UIViewDocument::CommitTextEdit(const ea::string& undoText, const ea::string& redoText)
{
    if (!ReloadFromText(redoText))
    {
        URHO3D_LOGERROR("UIViewDocument: projection reload failed; restoring the pre-edit document.");
        ReloadFromText(undoText); // best effort: keep model and projection in sync
        return false;
    }
    // One discrete undo step per link edit: consecutive link commands must
    // not collapse into each other.
    const ea::vector<unsigned> mergeKey;
    PushUndoAction(MakeShared<UiDocumentSnapshotAction>(this, mergeKey, undoText, redoText));
    dirty_ = true;
    OnModelEdited(this);
    return true;
}

bool UIViewDocument::AddHeadLink(const ea::string& type, const ea::string& href)
{
    if (!model_.root_ || !document_)
        return false;
    // <head> is not part of the editor tree (it starts at <body>), so the edit
    // happens on the emitted text directly; the reload below then makes the
    // new link canonical for both the spine and the live projection.
    const ea::string undoText = model_.EmitRml();
    ea::string redoText;
    if (!model_.InsertHeadLink(undoText, type, href, redoText))
    {
        URHO3D_LOGERROR(
            "UIViewDocument: cannot add head link '{}' (document has no <head>, or it is already linked).",
            Trim(href).c_str());
        return false;
    }
    return CommitTextEdit(undoText, redoText);
}

bool UIViewDocument::EditHeadLink(UiNode* node, const ea::string& type, const ea::string& href)
{
    if (!model_.root_ || !document_ || !node
        || (!node->IsHeadLink() && !node->IsNestedDoc()))
        return false;
    const ea::string trimmedType = Trim(type);
    const ea::string trimmedHref = Trim(href);
    if (node->GetAttribute("type") == trimmedType && node->GetAttribute("href") == trimmedHref)
        return true; // nothing changed; no reload, no undo noise
    const ea::string undoText = model_.EmitRml();
    ea::string redoText;
    if (!model_.EditHeadLinkAt(undoText, node->headLinkOrdinal_, type, href, redoText))
    {
        URHO3D_LOGERROR(
            "UIViewDocument: cannot edit head link '{}' (empty value, or the link was rebuilt away).",
            trimmedHref.c_str());
        return false;
    }
    return CommitTextEdit(undoText, redoText);
}

}

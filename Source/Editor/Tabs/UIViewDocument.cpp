//
// Copyright (c) 2017-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#include "UIViewDocument.h"

#include "UIViewActions.h"

#include "../Project/Project.h"

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

UiNode* HitTestRecurse(Rml::Element* element, const UiDocumentModel* model, const Vector2& point)
{
    // Children first, tested in reverse (later siblings paint on top).
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
    if (local.x_ >= box.pos_.x_ && local.x_ <= box.pos_.x_ + box.size_.x_ &&
        local.y_ >= box.pos_.y_ && local.y_ <= box.pos_.y_ + box.size_.y_)
    {
        return node;
    }
    return nullptr;
}

} // namespace

UIViewDocument::UIViewDocument(Context* context)
    : Object(context)
{
    // Private RmlUi context that renders the document under edit into a dynamic
    // texture. Deliberately not the master RmlUI subsystem so editing does not
    // leak into the running game view.
    previewUI_ = new RmlUI(context_, "UIViewPreview");
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
    // watcher. We drive every rebuild ourselves through LoadFromText, so a
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
    if (previewUI_)
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
// Loading
// ---------------------------------------------------------------------------

bool UIViewDocument::LoadFromText(const ea::string& text, const ea::string& path)
{
    Rml::Context* ctx = previewUI_->GetRmlContext();
    if (!ctx)
        return false;

    // Preserve the <head>...</head> block verbatim (styles / templates the
    // editor neither parses nor reorders).
    ea::string head;
    const size_t headBegin = text.find("<head");
    if (headBegin != ea::string::npos)
    {
        const size_t headEnd = text.find("</head>", headBegin);
        if (headEnd != ea::string::npos)
            head = text.substr(headBegin, headEnd + 7 /*len("</head>")*/ - headBegin);
    }

    ctx->UnloadAllDocuments();
    document_ = nullptr;

    // Load under the document's real resource path so RmlUi resolves relative
    // <link>/<template> hrefs and theme imports the same way the runtime does
    // (RmlFile::Open joins the href onto the source-URL directory). An empty
    // URL would silently drop e.g. <link href="HelloRmlUI_Window.rml"> and leave
    // a template="..." body rendering as a bare, content-less box.
    // E_FILECHANGED is unsubscribed on previewUI_ (see ctor), so using the real
    // URL here stays immune to the engine's hot-reload path.
    document_ = ctx->LoadDocumentFromMemory(
        Rml::String(SubstituteDataModelToken(text).c_str()),
        Rml::String(path.c_str(), path.length()));
    if (!document_)
        return false;

    document_->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    // Synchronous layout so the freshly built model sees valid boxes.
    document_->UpdateDocument();

    model_.BuildFromDom(document_);
    model_.headRaw_ = head;
    dirty_ = false;
    return true;
}

// ---------------------------------------------------------------------------
// Live-DOM editing primitives (no undo recording)
// ---------------------------------------------------------------------------

void UIViewDocument::SyncStyleToDom(UiNode* node)
{
    Rml::Element* el = node ? node->dom_ : nullptr;
    if (!el)
        return;
    // The model's inline style vector is the source of truth. Drop any
    // editor-managed property no longer present, then apply the current set via
    // SetProperty on the *attached* element (RmlUi marks it dirty and re-flows),
    // which is the same path the live drag uses and visibly updates the render.
    static const char* const managed[] = {
        "position", "box-sizing", "left", "top", "right", "bottom",
        "width", "height", "transform", "background-color", "border", "color",
    };
    for (const char* key : managed)
    {
        if (node->GetStyle(key).empty())
            el->RemoveProperty(key);
    }
    for (const UiStyleDecl& decl : node->style_)
        el->SetProperty(decl.name_.c_str(), decl.value_.c_str());
}

Rml::Element* UIViewDocument::CreateDomForNode(UiNode& node, Rml::Element* parentEl)
{
    Rml::ElementDocument* doc = parentEl->GetOwnerDocument();
    if (!doc)
        return nullptr;

    if (node.IsText())
    {
        Rml::ElementPtr text = doc->CreateTextNode(node.text_.c_str());
        node.dom_ = parentEl->AppendChild(std::move(text));
        return node.dom_;
    }

    Rml::ElementPtr el = doc->CreateElement(node.tag_.c_str());
    Rml::Element* raw = el.get();
    if (!node.id_.empty())
        raw->SetAttribute("id", node.id_.c_str());
    if (!node.classes_.empty())
        raw->SetAttribute("class", node.classes_.c_str());
    for (const auto& attr : node.attributes_)
        raw->SetAttribute(attr.first.c_str(), attr.second.c_str());
    node.dom_ = parentEl->AppendChild(std::move(el));
    // Apply inline style only after insertion: setting the 'style' attribute on a
    // detached element does not reliably parse into applied properties, so the
    // new node would render at its default/auto size (a 0x0 dot).
    SyncStyleToDom(&node);
    for (const SharedPtr<UiNode>& child : node.children_)
        CreateDomForNode(*child, node.dom_);
    return node.dom_;
}

void UIViewDocument::DetachFromDom(UiNode* node)
{
    if (!node)
        return;
    if (node->dom_)
    {
        if (Rml::Element* parent = node->dom_->GetParentNode())
            parent->RemoveChild(node->dom_);
        node->dom_ = nullptr;
    }
    for (const SharedPtr<UiNode>& child : node->children_)
        DetachFromDom(child.Get());
}

void UIViewDocument::ApplyNodeToDom(UiNode* node)
{
    Rml::Element* el = node ? node->dom_ : nullptr;
    if (!el)
        return;

    if (node->id_.empty())
        el->RemoveAttribute("id");
    else
        el->SetAttribute("id", node->id_.c_str());
    if (node->classes_.empty())
        el->RemoveAttribute("class");
    else
        el->SetAttribute("class", node->classes_.c_str());
    // Inline style is pushed as properties on the attached element, not via the
    // 'style' attribute (which does not reliably re-parse into applied props).
    SyncStyleToDom(node);

    // Drop attributes that no longer exist in the model, then rewrite the rest
    // (SetAttribute on a live element re-parses; style/class refresh in place).
    ea::vector<Rml::String> stale;
    for (const auto& pair : el->GetAttributes())
    {
        const ea::string name(pair.first.c_str(), pair.first.length());
        if (name == "id" || name == "class" || name == "style")
            continue;
        bool found = false;
        for (const auto& attr : node->attributes_)
        {
            if (attr.first == name)
            {
                found = true;
                break;
            }
        }
        if (!found)
            stale.push_back(pair.first);
    }
    for (const Rml::String& name : stale)
        el->RemoveAttribute(name);
    for (const auto& attr : node->attributes_)
        el->SetAttribute(attr.first.c_str(), attr.second.c_str());
}

void UIViewDocument::RefreshLayout()
{
    if (document_)
        document_->UpdateDocument();
}

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

bool UIViewDocument::TryGetDomBox(const UiNode* node, UiBox& out) const
{
    return node ? Urho3D::TryGetDomBox(node->dom_, node, out) : false;
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
    // The tab is constructed during project plugin application, when the
    // Project subsystem is already registered, so the undo manager exists.
    // Guard anyway: without it edits simply become non-undoable.
    Project* project = GetSubsystem<Project>();
    if (!project)
        return false;
    project->GetUndoManager()->PushAction(action);
    return true;
}

bool UIViewDocument::PushChangeNodeAction(UiNode* node, const UiNodePayload& oldData)
{
    if (!node)
        return false;
    ea::vector<unsigned> path;
    if (!model_.BuildPath(node, path))
        return false;
    return PushUndoAction(MakeShared<ChangeUiNodeAction>(this, path, oldData, SnapshotUiNodePayload(*node)));
}

// ---------------------------------------------------------------------------
// Undoable commands
// ---------------------------------------------------------------------------

UiNode* UIViewDocument::AddWidget(UiNode* parent, const char* tag)
{
    if (!model_.root_ || !document_ || !tag)
        return nullptr;
    parent = (parent && parent->dom_) ? parent : model_.root_;
    Rml::Element* parentEl = parent->dom_;
    if (!parentEl)
        return nullptr;

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

    // Born materialized: 160x48 centered inside the parent's rendered box so it
    // lands in view (not off a narrow/auto-sized body) and is draggable at once.
    UiBox box;
    box.size_ = Vector2{160.0f, 48.0f};
    float cw = static_cast<float>(kPreviewWidth);
    float ch = static_cast<float>(kPreviewHeight);
    const Vector2 psz = V2(parentEl->GetBox().GetSize(Rml::BoxArea::Border));
    if (psz.x_ > box.size_.x_)
        cw = psz.x_;
    if (psz.y_ > box.size_.y_)
        ch = psz.y_;
    box.pos_ = Vector2{Max(cw - box.size_.x_, 0.0f) * 0.5f, Max(ch - box.size_.y_, 0.0f) * 0.5f};
    WriteBoxToStyle(*node, box);

    CreateDomForNode(*node, parentEl);
    RefreshLayout();

    // Center precisely: measure where the widget actually landed and correct
    // its left/top once, so the centering is exact regardless of which
    // containing block the new absolute element resolves against.
    if (Rml::Element* el = node->dom_)
    {
        const Vector2 pAbs = V2(parentEl->GetAbsoluteOffset(Rml::BoxArea::Border));
        const Vector2 desired = pAbs + Vector2{Max(psz.x_ - box.size_.x_, 0.0f) * 0.5f,
                                               Max(psz.y_ - box.size_.y_, 0.0f) * 0.5f};
        const Vector2 landed = V2(el->GetAbsoluteOffset(Rml::BoxArea::Border));
        if (landed != desired)
        {
            box.pos_ += desired - landed;
            WriteBoxToStyle(*node, box);
            SyncStyleToDom(node.Get());
            RefreshLayout();
        }
    }

    const unsigned indexInParent = parent->children_.size();
    ea::vector<unsigned> parentPath;
    model_.BuildPath(parent, parentPath);
    PushUndoAction(MakeShared<CreateRemoveUiNodeAction>(this, parentPath, indexInParent, node, false));

    parent->children_.push_back(node);
    dirty_ = true;
    OnModelEdited(this);
    return node.Get();
}

UiNode* UIViewDocument::DuplicateNode(UiNode* node)
{
    if (!node || node == model_.root_)
        return nullptr;
    UiNode* parent = model_.FindParent(node);
    if (!parent || !parent->dom_)
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

    CreateDomForNode(*copy, parent->dom_);
    RefreshLayout();

    const unsigned indexInParent = parent->children_.size();
    ea::vector<unsigned> parentPath;
    model_.BuildPath(parent, parentPath);
    PushUndoAction(MakeShared<CreateRemoveUiNodeAction>(this, parentPath, indexInParent, copy, false));

    parent->children_.push_back(copy);
    dirty_ = true;
    OnModelEdited(this);
    return copy.Get();
}

bool UIViewDocument::DeleteNode(UiNode* node)
{
    if (!node || node == model_.root_)
        return false;
    UiNode* parent = model_.FindParent(node);
    if (!parent)
        return false;

    unsigned indexInParent = 0;
    SharedPtr<UiNode> victim; // keeps the subtree alive through the action
    for (unsigned i = 0; i < parent->children_.size(); i++)
    {
        if (parent->children_[i] == node)
        {
            indexInParent = i;
            victim = parent->children_[i];
            break;
        }
    }
    if (!victim)
        return false;

    // Remove from the live DOM first, then mirror in the model. No reload.
    DetachFromDom(node);

    ea::vector<unsigned> parentPath;
    model_.BuildPath(parent, parentPath);
    PushUndoAction(MakeShared<CreateRemoveUiNodeAction>(this, parentPath, indexInParent, victim, true));

    parent->children_.erase(parent->children_.begin() + indexInParent);
    RefreshLayout();
    dirty_ = true;
    OnModelEdited(this);
    return true;
}

bool UIViewDocument::MaterializeNode(UiNode* node)
{
    if (!node || !node->dom_ || node->IsMaterialized())
        return false;

    // Bake the computed border box. The guess for the left/top frame origin is
    // then verified against the re-laid-out element and corrected once, so the
    // element never shifts (regardless of containing-block padding/border).
    const UiNodePayload oldData = SnapshotUiNodePayload(*node);

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

    // Push straight onto the live element - never re-emit/reload, which would
    // re-instantiate templates and data-bound subtrees.
    ApplyNodeToDom(node);
    RefreshLayout();

    const Vector2 absAfter = V2(el->GetAbsoluteOffset(Rml::BoxArea::Border));
    if (absAfter != absBefore)
    {
        box.pos_ += absBefore - absAfter;
        WriteBoxToStyle(*node, box);
        ApplyNodeToDom(node);
        RefreshLayout();
    }

    PushChangeNodeAction(node, oldData);
    dirty_ = true;
    OnModelEdited(this);
    return true;
}

bool UIViewDocument::EditNodePayload(UiNode* node, const UiNodePayload& newData)
{
    if (!node)
        return false;
    const UiNodePayload oldData = SnapshotUiNodePayload(*node);
    ApplyUiNodePayload(*node, newData);
    ApplyNodeToDom(node);
    RefreshLayout();
    PushChangeNodeAction(node, oldData);
    dirty_ = true;
    OnModelEdited(this);
    return true;
}

bool UIViewDocument::CommitBoxEdit(UiNode* node, const UiBox& box)
{
    if (!node)
        return false;
    const UiNodePayload oldData = SnapshotUiNodePayload(*node);
    WriteBoxToStyle(*node, box);
    PushChangeNodeAction(node, oldData);
    dirty_ = true;
    OnModelEdited(this);
    return true;
}

// ---------------------------------------------------------------------------
// Apply helpers used by the undo actions
// ---------------------------------------------------------------------------

bool UIViewDocument::ApplyNodePayloadInternal(const ea::vector<unsigned>& path, const UiNodePayload& payload)
{
    UiNode* node = model_.ResolvePath(path);
    if (!node)
        return false;
    ApplyUiNodePayload(*node, payload);
    ApplyNodeToDom(node);
    RefreshLayout();
    dirty_ = true;
    OnModelEdited(this);
    return true;
}

bool UIViewDocument::InsertNodeInternal(const ea::vector<unsigned>& parentPath, unsigned index,
    const SharedPtr<UiNode>& node)
{
    UiNode* parent = model_.ResolvePath(parentPath);
    if (!parent || !node || !parent->dom_)
        return false;
    if (index > parent->children_.size())
        return false;
    if (index < parent->children_.size() && parent->children_[index]->tag_ != node->tag_)
        return false; // guard against a desynchronized path

    CreateDomForNode(*node, parent->dom_);
    RefreshLayout();
    parent->children_.insert(parent->children_.begin() + index, node);
    dirty_ = true;
    OnModelEdited(this);
    return true;
}

bool UIViewDocument::RemoveNodeInternal(const ea::vector<unsigned>& parentPath, unsigned index,
    const SharedPtr<UiNode>& expected)
{
    UiNode* parent = model_.ResolvePath(parentPath);
    if (!parent || index >= parent->children_.size())
        return false;
    if (parent->children_[index] != expected)
        return false; // guard against a desynchronized path

    DetachFromDom(parent->children_[index].Get());
    RefreshLayout();
    parent->children_.erase(parent->children_.begin() + index);
    dirty_ = true;
    OnModelEdited(this);
    return true;
}

}

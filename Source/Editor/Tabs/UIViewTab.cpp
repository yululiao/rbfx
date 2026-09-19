//
// Copyright (c) 2017-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#include "../Tabs/UIViewTab.h"

#include "../Project/Project.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/Graphics/GraphicsEvents.h>
#include <Urho3D/Graphics/Texture2D.h>
#include <Urho3D/IO/File.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Input/InputEvents.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Resource/ResourceEvents.h>
#include <Urho3D/RmlUI/RmlUI.h>
#include <Urho3D/SystemUI/Widgets.h>

#include <IconFontCppHeaders/IconsFontAwesome6.h>

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Property.h>
#include <RmlUi/Core/Types.h>
#include <RmlUi/Core/Variant.h>

#include <math.h>
#include <algorithm>
#include <utility>

#include <EASTL/sort.h>

namespace Urho3D
{

namespace
{
// Static preview resolution. The document lays out against this virtual
// viewport; the widget then scales it down to fit the tab.
constexpr int kPreviewWidth = 1024;
constexpr int kPreviewHeight = 768;

// On-screen radius (in pixels) for grabbing a gizmo handle.
constexpr float kHandleGrabPx = 7.0f;
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

// The border box (document space) of a live DOM element, plus the node's own
// emitted transform. This is the thin view adapter that turns projection
// numbers into the UiBox the pure logic consumes.
bool TryGetDomBox(Rml::Element* element, const UiNode* node, UiBox& out)
{
    if (!element)
        return false;
    out.pos_ = V2(element->GetAbsoluteOffset(Rml::BoxArea::Border));
    out.size_ = V2(element->GetBox().GetSize(Rml::BoxArea::Border));
    out.xform_ = node ? ParseUiTransform(node->GetStyle("transform")) : UiTransform{};
    return out.size_.x_ > 0.0f && out.size_.y_ > 0.0f;
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

SharedPtr<UiNode> DeepClone(const UiNode& src)
{
    auto copy = MakeShared<UiNode>();
    copy->tag_ = src.tag_;
    copy->text_ = src.text_;
    copy->id_ = src.id_;
    copy->classes_ = src.classes_;
    copy->attributes_ = src.attributes_;
    copy->style_ = src.style_;
    for (const SharedPtr<UiNode>& child : src.children_)
        copy->children_.push_back(DeepClone(*child));
    return copy;
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

void Tabs_UIViewTab(Context* context, Project* project)
{
    project->AddTab(MakeShared<UIViewTab>(context));
}

// ---------------------------------------------------------------------------
// UIViewTab: construction / offscreen preview plumbing
// ---------------------------------------------------------------------------

UIViewTab::UIViewTab(Context* context)
    : EditorTab(context, ICON_FA_BEZIER_CURVE " UI", "8f2b1c9e-7d34-4a5b-9c10-ui0preview",
        EditorTabFlags{}, EditorTabPlacement::DockCenter)
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
    // watcher. We drive every rebuild ourselves through ReloadPreview(), so a
    // disk change (e.g. our own Save) must not hand the engine back ownership
    // of document_ and dangle model_'s dom_ pointers. This is what lets us load
    // with a real source URL (required for <link>/template resolution below).
    previewUI_->UnsubscribeFromEvent(E_FILECHANGED);

    texture_ = MakeShared<Texture2D>(context_);

    hierarchySource_ = MakeShared<UIViewHierarchy>(this);
    inspectorSource_ = MakeShared<UIViewInspector>(this);

    Rebuild();

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

    // Redirect offscreen draws to E_BEGINRENDERING (a fresh frame); RmlUI's
    // default E_ENDALLVIEWSRENDER auto-render is too late to retarget.
    previewUI_->SetRendering(false);
    SubscribeToEvent(E_BEGINRENDERING, URHO3D_HANDLER(UIViewTab, HandleBeginRendering));
}

UIViewTab::~UIViewTab()
{
    if (previewUI_ && previewUI_->GetRmlContext())
        previewUI_->GetRmlContext()->UnloadAllDocuments();
    document_ = nullptr;
    selected_ = nullptr;
}

UIViewTab* UIViewTab::GetActive(Project* project)
{
    return project ? project->FindTab<UIViewTab>() : nullptr;
}

void UIViewTab::HandleBeginRendering(StringHash, VariantMap&)
{
    // Layout was already updated on E_POSTUPDATE (CPU-side); here, at the start
    // of the graphics frame, it is safe to issue GPU draws into the offscreen
    // surface. RenderPreview() samples the resulting texture later this frame.
    if (previewUI_)
        previewUI_->Render();
}

void UIViewTab::Rebuild()
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
        URHO3D_LOGERROR("UIViewTab: failed to acquire RenderSurface for preview texture.");
    }
}

ea::string UIViewTab::SubstituteDataModelToken(const ea::string& text) const
{
    // Mirrors the engine's Detail::InsertVariablePlaceholders token/Format pair
    // without depending on that (non-exported) symbol.
    const ea::string id = Format("{}", static_cast<const void*>(this));
    ea::string result = text;
    result.replace("{{__data_model_id}}", id);
    return result;
}

// ---------------------------------------------------------------------------
// Preview lifecycle: model (data) <-> DOM (runtime projection)
// ---------------------------------------------------------------------------

void UIViewTab::OpenResource(const ea::string& path)
{
    if (!path.empty())
        LoadDocument(path);
    Focus();
}

void UIViewTab::LoadDocument(const ea::string& path)
{
    if (path.empty())
        return;

    auto* cache = GetSubsystem<ResourceCache>();
    ea::string abs = cache->GetResourceFileName(path);
    if (abs.empty())
        abs = path;

    File file(context_, abs, FILE_READ);
    if (!file.IsOpen())
    {
        URHO3D_LOGERROR("UIViewTab: failed to open UI document '{}'", path.c_str());
        return;
    }
    const ea::string contents = file.ReadText();

    if (LoadDocumentFromText(contents, path))
    {
        // Fresh open: start with the root selected.
        selPath_.clear();
        selected_ = model_.root_;
        snprintf(pathInputBuf_, sizeof(pathInputBuf_), "%s", path.c_str());
    }
    else
    {
        URHO3D_LOGERROR("UIViewTab: failed to load UI document '{}'", path.c_str());
    }
}

bool UIViewTab::LoadDocumentFromText(const ea::string& text, const ea::string& path)
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

    resourcePath_ = path;
    dirty_ = false;
    hoveredPath_.clear();
    return true;
}

void UIViewTab::ReloadPreview()
{
    // Re-emit the model and re-project; restore the selection by its stable
    // child-index path (model identity survives; only dom_ pointers change).
    const ea::vector<unsigned> keepPath = selPath_;
    const ea::string emitted = model_.EmitRml();
    if (LoadDocumentFromText(emitted, resourcePath_))
    {
        selPath_ = keepPath;
        selected_ = model_.ResolvePath(selPath_);
        if (!selected_)
            selected_ = model_.root_;
    }
}

ea::vector<unsigned> UIViewTab::NodePath(const UiNode* node) const
{
    ea::vector<unsigned> path;
    model_.BuildPath(node, path);
    return path;
}

void UIViewTab::SetSelectedNode(UiNode* node)
{
    selected_ = node;
    model_.BuildPath(node, selPath_);
    if (hierarchySource_)
        hierarchySource_->ExpandAncestors(selPath_);
}

// ---------------------------------------------------------------------------
// Editing operations (mutate model, then re-project)
// ---------------------------------------------------------------------------

void UIViewTab::MaterializeNode(UiNode* node)
{
    if (!node || !node->dom_ || node->IsMaterialized())
        return;

    // Bake the computed border box. The guess for the left/top frame origin is
    // then verified against the re-laid-out element and corrected once, so the
    // element never shifts (regardless of containing-block padding/border).
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
    if (document_)
        document_->UpdateDocument();

    const Vector2 absAfter = V2(el->GetAbsoluteOffset(Rml::BoxArea::Border));
    if (absAfter != absBefore)
    {
        box.pos_ += absBefore - absAfter;
        WriteBoxToStyle(*node, box);
        ApplyNodeToDom(node);
        if (document_)
            document_->UpdateDocument();
    }
    dirty_ = true;
}

void UIViewTab::AddWidget(const char* tag)
{
    if (!model_.root_ || !document_)
        return;
    UiNode* parent = (selected_ && selected_->dom_) ? selected_ : model_.root_;
    Rml::Element* parentEl = parent ? parent->dom_ : nullptr;
    if (!parentEl)
        return;

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
    document_->UpdateDocument();

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
            SyncStyleToDom(node);
            document_->UpdateDocument();
        }
    }

    parent->children_.push_back(node);
    dirty_ = true;
    SetSelectedNode(node);
}

void UIViewTab::DeleteSelected()
{
    if (!selected_ || selected_ == model_.root_)
        return;
    UiNode* victim = selected_;
    UiNode* parent = model_.FindParent(victim);
    if (!parent)
        return;

    // Remove from the live DOM first, then mirror in the model. No reload.
    if (victim->dom_)
    {
        if (Rml::Element* pdom = victim->dom_->GetParentNode())
            pdom->RemoveChild(victim->dom_);
        victim->dom_ = nullptr;
    }
    for (size_t i = 0; i < parent->children_.size(); i++)
    {
        if (parent->children_[i] == victim)
        {
            parent->children_.erase(parent->children_.begin() + i);
            break;
        }
    }

    if (document_)
        document_->UpdateDocument();
    dirty_ = true;
    SetSelectedNode(parent);
}

void UIViewTab::DuplicateSelected()
{
    if (!selected_ || selected_ == model_.root_)
        return;
    UiNode* parent = model_.FindParent(selected_);
    if (!parent || !parent->dom_)
        return;

    SharedPtr<UiNode> copy = DeepClone(*selected_);
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
    if (document_)
        document_->UpdateDocument();
    parent->children_.push_back(copy);
    dirty_ = true;
    SetSelectedNode(copy);
}

void UIViewTab::SyncStyleToDom(UiNode* node)
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

Rml::Element* UIViewTab::CreateDomForNode(UiNode& node, Rml::Element* parentEl)
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

void UIViewTab::ApplyNodeToDom(UiNode* node)
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

void UIViewTab::CommitNodeEdit(UiNode* node)
{
    ApplyNodeToDom(node);
    if (document_)
        document_->UpdateDocument();
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

bool UIViewTab::SaveDocument()
{
    ea::string target = resourcePath_;
    if (target.empty())
        target = Trim(pathInputBuf_);
    if (target.empty())
    {
        URHO3D_LOGWARNING("UIViewTab: nothing to save - enter a resource path first.");
        return false;
    }
    return SaveDocumentTo(target);
}

bool UIViewTab::SaveDocumentTo(const ea::string& path)
{
    const ea::string emitted = model_.EmitRml();

    auto* cache = GetSubsystem<ResourceCache>();
    auto* fs = GetSubsystem<FileSystem>();
    ea::string abs = cache->GetResourceFileName(path);
    if (abs.empty())
    {
        // New file: write under the project DataPath (mirrors AssetManager).
        auto* project = GetProject();
        if (!project)
            return false;
        abs = project->GetDataPath() + path;
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
    file.Write(emitted.data(), emitted.size());

    // Drop any cached File so a later Load sees the new bytes.
    cache->ReleaseResource(path, true);

    resourcePath_ = path;
    dirty_ = false;
    snprintf(pathInputBuf_, sizeof(pathInputBuf_), "%s", path.c_str());
    return true;
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

    if (LoadDocumentFromText(kTemplate, ea::string()))
    {
        selPath_.clear();
        selected_ = model_.root_;
        pathInputBuf_[0] = '\0';
        dirty_ = true; // untitled document does not exist on disk yet
    }
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
    ui::PushItemWidth(-220.0f);
    if (ui::InputText("##uiPath", pathInputBuf_, sizeof(pathInputBuf_), ImGuiInputTextFlags_EnterReturnsTrue))
    {
        const ea::string path = Trim(pathInputBuf_);
        if (!path.empty() && document_)
            SaveDocumentTo(path);
    }
    ui::PopItemWidth();
    ui::SameLine();

    if (ui::Button(ICON_FA_FILE_LINES " New"))
        NewDocument();
    ui::SameLine();
    if (ui::Button(ICON_FA_FOLDER_OPEN " Load"))
    {
        const ea::string path = Trim(pathInputBuf_);
        if (!path.empty())
            LoadDocument(path);
    }

    const bool hasDoc = document_ != nullptr;
    ui::SameLine();
    ui::BeginDisabled(!hasDoc);
    if (ui::Button(ICON_FA_FLOPPY_DISK " Save"))
        SaveDocument();
    ui::SameLine();
    if (ui::Button(ICON_FA_ROTATE " Reload"))
    {
        if (!resourcePath_.empty())
            LoadDocument(resourcePath_);
        else
            ReloadPreview();
    }
    ui::EndDisabled();

    if (dirty_)
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
                AddWidget(entry.tag_);
        }
        ui::EndCombo();
    }
    ui::SameLine();
    const bool canEdit = selected_ && selected_ != model_.root_;
    ui::BeginDisabled(!canEdit);
    if (ui::Button(ICON_FA_COPY " Copy"))
        DuplicateSelected();
    ui::SameLine();
    if (ui::Button(ICON_FA_TRASH " Delete"))
        DeleteSelected();
    ui::EndDisabled();
    ui::EndDisabled();

    // Element context menu (opened by a right-click on the preview).
    if (ui::BeginPopup("##uiElemCtx"))
    {
        UiNode* node = selected_;
        if (node && !node->IsText() && !node->IsMaterialized())
        {
            if (ui::MenuItem(ICON_FA_LOCATION_PIN " Add Explicit Position"))
                MaterializeNode(node);
        }
        if (node && node != model_.root_)
        {
            if (ui::MenuItem(ICON_FA_COPY " Copy"))
                DuplicateSelected();
            if (ui::MenuItem(ICON_FA_TRASH " Delete"))
                DeleteSelected();
        }
        ui::EndPopup();
    }
}

void UIViewTab::RenderPreview()
{
    if (!document_)
    {
        ui::TextUnformatted("No UI document loaded.\nType a resource path (e.g. \"UI/HelloRmlUI.rml\") and press Load, or click New.");
        return;
    }

    // RmlUI renders the document into the texture from E_BEGINRENDERING; here we
    // only sample the produced texture (never issue draws during widget build).
    const ImVec2 avail = ui::GetContentRegionAvail();
    float scale = ea::min(avail.x / static_cast<float>(previewSize_.x_),
                          avail.y / static_cast<float>(previewSize_.y_));
    if (scale <= 0.0f)
        scale = 0.1f;
    const ImVec2 displaySize(previewSize_.x_ * scale, previewSize_.y_ * scale);

    Widgets::Image(texture_, displaySize);

    DocViewport vp;
    vp.origin_ = V2(ui::GetItemRectMin());
    vp.scale_ = scale;

    HandlePreviewPointer(vp);
    DrawOverlay(vp);
}

// ---------------------------------------------------------------------------
// Hit testing / pointer
// ---------------------------------------------------------------------------

UiNode* UIViewTab::HitTestPreview(const Vector2& docPos) const
{
    if (!document_)
        return nullptr;
    return HitTestRecurse(document_, &model_, docPos);
}

void UIViewTab::HandlePreviewPointer(const DocViewport& vp)
{
    ImGuiIO& io = ui::GetIO();
    // Hover is computed from the image rect directly rather than
    // ImGui::IsItemHovered(): Widgets::Image submits a zero-ID plain Image, and
    // item-hover on such an item is unreliable across ImGui versions (it silently
    // returned false here, disabling preview picking and the hover outline).
    const ImVec2 imageMin = IV2(vp.origin_);
    const ImVec2 imageMax(imageMin.x + previewSize_.x_ * vp.scale_,
                          imageMin.y + previewSize_.y_ * vp.scale_);
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

    UiNode* hover = active ? HitTestPreview(doc) : nullptr;
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
            const Vector2 base = InlineStyleBase(selected_->dom_, selected_);
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
    gizmoBase_ = InlineStyleBase(node->dom_, node);
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
    if (Rml::Element* el = gizmoNode_ ? gizmoNode_->dom_ : nullptr)
    {
        el->SetProperty("position", "absolute");
        el->SetProperty("box-sizing", "border-box");
        el->SetProperty("left", FormatPx(solved.pos_.x_).c_str());
        el->SetProperty("top", FormatPx(solved.pos_.y_).c_str());
        el->SetProperty("width", FormatPx(solved.size_.x_).c_str());
        el->SetProperty("height", FormatPx(solved.size_.y_).c_str());
        const ea::string transform = FormatUiTransform(solved.xform_);
        if (transform.empty())
            el->RemoveProperty("transform");
        else
            el->SetProperty("transform", transform.c_str());
    }
}

void UIViewTab::CommitDrag()
{
    if (gizmoNode_)
    {
        const UiBox solved = SolveDrag(gizmoStartBox_, gizmoDrag_, gizmoPressDoc_, gizmoCurDoc_);
        WriteBoxToStyle(*gizmoNode_, solved);
        dirty_ = true;
    }
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
    UiNode* hover = model_.ResolvePath(hoveredPath_);

    if (hover && hover != sel && hover->dom_ && !hover->IsText())
    {
        UiBox box;
        if (TryGetDomBox(hover->dom_, hover, box))
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
            TryGetDomBox(sel->dom_, sel, box);

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
        const ea::vector<unsigned>& sel = tab->selPath_;
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
    if (!tab || !tab->model_.root_)
    {
        ui::TextDisabled("(no document)");
        return;
    }

    ui::Checkbox("Focus Path Only", &focusPathOnly_);
    RenderNode(tab->model_.root_, ea::vector<unsigned>{});
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

    const bool selected = tab->selected_ == node;
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
            RenderNode(child, childPath);
        }
        ui::TreePop();
    }
}

void UIViewHierarchy::RenderContextMenuItems()
{
    UIViewTab* tab = owner_;
    if (!tab)
        return;
    UiNode* target = contextMenuTarget_ ? contextMenuTarget_ : tab->selected_;
    if (!target)
        return;

    if (!target->IsText() && !target->IsMaterialized())
    {
        if (ui::MenuItem(ICON_FA_LOCATION_PIN " Add Explicit Position"))
            tab->MaterializeNode(target);
    }
    if (target != tab->model_.root_)
    {
        if (ui::MenuItem(ICON_FA_TRASH " Delete"))
        {
            tab->SetSelectedNode(target);
            tab->DeleteSelected();
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
    UiNode* node = tab ? tab->selected_ : nullptr;
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

    RenderAttributes(node);
    ui::Separator();
    RenderInlineStyle(node);
    ui::Separator();
    RenderComputed(node);
}

void UIViewInspector::RenderAttributes(UiNode* node)
{
    UIViewTab* tab = owner_;
    if (!ui::CollapsingHeader(ICON_FA_LIST " Attributes", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    // id / class are dedicated fields; editing them is structural (emitted).
    bool structural = false;
    char idBuf[256];
    snprintf(idBuf, sizeof(idBuf), "%s", node->id_.c_str());
    ui::InputText("id", idBuf, sizeof(idBuf), ImGuiInputTextFlags_EnterReturnsTrue);
    if (ui::IsItemDeactivatedAfterEdit())
    {
        node->id_ = Trim(idBuf);
        structural = true;
    }
    char classBuf[256];
    snprintf(classBuf, sizeof(classBuf), "%s", node->classes_.c_str());
    ui::InputText("class", classBuf, sizeof(classBuf), ImGuiInputTextFlags_EnterReturnsTrue);
    if (ui::IsItemDeactivatedAfterEdit())
    {
        node->classes_ = Trim(classBuf);
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
            node->attributes_[i].second = valBuf;
            structural = true;
        }
        ui::PopItemWidth();
        ui::SameLine();
        if (ui::SmallButton(ICON_FA_TRASH))
        {
            node->attributes_.erase(node->attributes_.begin() + i);
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
        node->attributes_.emplace_back(Trim(attributeKeyBuf_), attributeValueBuf_);
        ea::sort(node->attributes_.begin(), node->attributes_.end(),
            [](const ea::pair<ea::string, ea::string>& a, const ea::pair<ea::string, ea::string>& b)
            { return a.first < b.first; });
        structural = true;
        attributeKeyBuf_[0] = '\0';
        attributeValueBuf_[0] = '\0';
    }
    ui::PopID();

    if (structural && tab)
    {
        tab->dirty_ = true;
        tab->CommitNodeEdit(node);
    }
}

void UIViewInspector::RenderInlineStyle(UiNode* node)
{
    UIViewTab* tab = owner_;
    if (!ui::CollapsingHeader(ICON_FA_PAINTBRUSH " Inline Style", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    const ea::vector<unsigned> curPath = tab ? tab->selPath_ : ea::vector<unsigned>{};
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
        node->style_ = parsed;
        styleSeedValid_ = false;
        if (tab)
        {
            tab->dirty_ = true;
            tab->CommitNodeEdit(node);
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
    else if (!node->IsText() && tab)
    {
        if (ui::Button(ICON_FA_LOCATION_PIN " Add Explicit Position"))
            tab->MaterializeNode(node);
    }
}

}

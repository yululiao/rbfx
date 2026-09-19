//
// Copyright (c) 2017-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#include "../Tabs/UIViewTab.h"

#include "../Project/Project.h"
#include "../Tabs/HierarchyBrowserTab.h"
#include "../Tabs/InspectorTab.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/Graphics/GraphicsEvents.h>
#include <Urho3D/Graphics/Texture2D.h>
#include <Urho3D/IO/File.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Input/InputEvents.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/RmlUI/RmlUI.h>
#include <Urho3D/SystemUI/Widgets.h>

#include <IconFontCppHeaders/IconsFontAwesome6.h>

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Property.h>
#include <RmlUi/Core/StyleSheetSpecification.h>
#include <RmlUi/Core/Types.h>
#include <RmlUi/Core/Variant.h>

namespace Urho3D
{

namespace
{
// Static preview resolution. The document lays out against this virtual
// viewport; the widget then scales it down to fit the tab.
constexpr int kPreviewWidth = 1024;
constexpr int kPreviewHeight = 768;

ea::string Trim(const ea::string& s)
{
    size_t begin = s.find_first_not_of(" \t\r\n");
    if (begin == ea::string::npos)
        return ea::string();
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}
}

void Tabs_UIViewTab(Context* context, Project* project)
{
    project->AddTab(MakeShared<UIViewTab>(context));
}

UIViewTab::UIViewTab(Context* context)
    : EditorTab(context, ICON_FA_BEZIER_CURVE " UI", "8f2b1c9e-7d34-4a5b-9c10-ui0preview",
        EditorTabFlags{}, EditorTabPlacement::DockCenter)
{
    // Private RmlUi context that renders the document under edit into a
    // dynamic texture. It is deliberately not the master RmlUI subsystem so
    // that editing does not leak into the running game view.
    previewUI_ = new RmlUI(context_, "UIViewPreview");
    // Input isolation for the offscreen context: drop the subscriptions RmlUI
    // made to global input events so the preview cannot steal focus from the
    // editor. SetBlockEvents() must NOT be used here: it blocks every event,
    // including E_POSTUPDATE, which stalls Context::Update - the root element
    // never becomes a stacking context and the document is never rendered.
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

    texture_ = MakeShared<Texture2D>(context_);

    hierarchySource_ = MakeShared<UIViewHierarchy>(this);
    inspectorSource_ = MakeShared<UIViewInspector>(this);

    Rebuild();

    // Design-time documents frequently bind their content to the runtime data
    // model via `data-model="{{__data_model_id}}"`. At runtime RmlUIComponent
    // substitutes that token with a registered model name; the editor has no
    // such component. Register an empty placeholder model named identically to
    // the token LoadDocument() will substitute (derived from `this`), so the
    // binding resolves instead of erroring "Could not locate data model" and
    // leaving the whole bound subtree unrendered.
    if (Rml::Context* ctx = previewUI_->GetRmlContext())
    {
        // Detail::InsertVariablePlaceholders() substitutes the token with
        // Format("{}", ptr); build the identical name locally so we do not
        // depend on that engine symbol (it is not URHO3D_API-exported).
        const ea::string modelName = Format("{}", static_cast<void*>(this));
        Rml::DataModelConstructor ctor = ctx->CreateDataModel(modelName, nullptr);
        (void)ctor.GetModelHandle();
    }

    // RmlUI auto-renders on E_ENDALLVIEWSRENDER, but by then the frame's render
    // state is already committed to the backbuffer, so redirecting draws into an
    // offscreen texture there is unreliable. Disable that auto-render and instead
    // drive the preview render from E_BEGINRENDERING (a fresh frame, the same
    // event TextureCubeInspectorWidget uses for offscreen render-to-texture).
    previewUI_->SetRendering(false);
    SubscribeToEvent(E_BEGINRENDERING, URHO3D_HANDLER(UIViewTab, HandleBeginRendering));
}

void UIViewTab::HandleBeginRendering(StringHash, VariantMap&)
{
    if (!previewUI_)
        return;

    // Layout was already updated on E_POSTUPDATE (CPU-side); here, at the start
    // of the graphics frame, it is safe to issue GPU draws into the offscreen
    // surface. RenderPreview() samples the resulting texture later this frame.
    previewUI_->Render();
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

    if (previewUI_->GetRmlContext())
        previewUI_->GetRmlContext()->UnloadAllDocuments();
    document_ = nullptr;
    selected_ = nullptr;

    // Pass `this` so the {{__data_model_id}} token is substituted with the
    // placeholder model registered in the constructor above.
    document_ = previewUI_->LoadDocument(path, this);
    if (document_)
    {
        document_->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
        resourcePath_ = path;
        dirty_ = false;
        snprintf(pathInputBuf_, sizeof(pathInputBuf_), "%s", path.c_str());
    }
    else
    {
        URHO3D_LOGERROR("UIViewTab: failed to load UI document '{}'", path.c_str());
        resourcePath_.clear();
    }
}

bool UIViewTab::SaveDocument()
{
    if (!document_ || resourcePath_.empty())
        return false;

    // Reconstruct the document body from the live DOM. Element serialization
    // preserves attributes and inline style overrides, so edits made in the
    // inspector round-trip back to the .rml source.
    Rml::String inner;
    document_->GetInnerRML(inner);

    ea::string content;
    content += "<rml src=\"" + resourcePath_ + "\">\n";
    content += "  <head>\n";
    content += "    <style>\n";
    content += "    </style>\n";
    content += "  </head>\n";
    content += "  <body>\n";
    content += inner.c_str();
    content += "\n  </body>\n";
    content += "</rml>\n";

    auto* cache = GetSubsystem<ResourceCache>();
    ea::string absPath = cache->GetResourceFileName(resourcePath_);
    if (absPath.empty())
    {
        // Fall back to treating the resource path as a plain file name.
        absPath = resourcePath_;
    }

    File file(context_, absPath, FILE_WRITE);
    if (!file.IsOpen())
    {
        URHO3D_LOGERROR("UIViewTab: cannot open '{}' for writing.", absPath.c_str());
        return false;
    }
    file.Write(content.data(), content.size());

    dirty_ = false;
    return true;
}

void UIViewTab::Rebuild()
{
    if (!texture_)
        return;

    // A single mip level: the offscreen render only ever writes mip 0, and the
    // preview is sampled through the ImGui backend, which - unlike engine draw
    // commands (DrawCommandQueue) - does not auto-refresh the mip chain of
    // render-target textures. With the default auto-generated chain the sampler
    // would pick the never-written lower mips at typical preview scale and the
    // image would stay blank. Note: SetNumLevels only feeds the NEXT texture
    // creation, so it must be called before SetSize.
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
        // Opaque editor-neutral background. Must stay opaque: RmlUI skips the
        // clear entirely for fully transparent colors, which would leave stale
        // pixels behind when the document shrinks or unloads.
        previewUI_->SetRenderTarget(surface, Color(0.16f, 0.18f, 0.22f, 1.0f));
    }
    else
    {
        previewUI_->SetRenderTarget(nullptr);
        URHO3D_LOGERROR("UIViewTab: failed to acquire RenderSurface for preview texture.");
    }
}

void UIViewTab::RenderContent()
{
    RenderToolbar();
    ui::Separator();
    RenderPreview();
}

void UIViewTab::RenderToolbar()
{
    ui::PushItemWidth(-140.0f);
    ui::InputText("##uiPath", pathInputBuf_, sizeof(pathInputBuf_),
                  ImGuiInputTextFlags_EnterReturnsTrue);
    ui::PopItemWidth();
    ui::SameLine();

    if (ui::Button(ICON_FA_FOLDER_OPEN " Load"))
    {
        const ea::string path = Trim(pathInputBuf_);
        if (!path.empty())
            LoadDocument(path);
    }

    ui::SameLine();
    const bool canOperate = document_ != nullptr;
    ui::BeginDisabled(!canOperate);
    if (ui::Button(ICON_FA_FLOPPY_DISK " Save"))
        SaveDocument();
    ui::SameLine();
    if (ui::Button(ICON_FA_ROTATE " Reload"))
        LoadDocument(resourcePath_);
    ui::EndDisabled();

    if (dirty_)
    {
        ui::SameLine();
        ui::TextDisabled("(unsaved)");
    }
}

void UIViewTab::RenderPreview()
{
    if (!document_)
    {
        ui::TextUnformatted("No UI document loaded.\nType a resource path (e.g. \"Interface/Main.rml\") and press Load.");
        return;
    }

    // RmlUI renders the document into the texture from E_BEGINRENDERING (see
    // HandleBeginRendering). We must NOT call Render() here: widget building runs
    // during E_UPDATE, before the graphics frame begins, so any draw issued now
    // has no active frame and is dropped. Just sample the texture that the
    // event-driven render produced.
    const ImVec2 avail = ui::GetContentRegionAvail();
    const float scale = ea::min(avail.x / static_cast<float>(previewSize_.x_),
                                avail.y / static_cast<float>(previewSize_.y_));
    const ImVec2 displaySize(previewSize_.x_ * scale, previewSize_.y_ * scale);

    // Scene previews sample a render-to-texture with default UVs and appear
    // upright, so no V flip is needed here either.
    Widgets::Image(texture_, displaySize);
}

// ---------------------------------------------------------------------------
// UIViewHierarchy
// ---------------------------------------------------------------------------

UIViewHierarchy::UIViewHierarchy(UIViewTab* owner)
    : Object(owner->GetContext())
    , owner_(owner)
{
}

void UIViewHierarchy::RenderContent()
{
    UIViewTab* tab = owner_;
    if (!tab)
        return;

    Rml::ElementDocument* document = tab->document_;
    if (!document)
    {
        ui::TextDisabled("(no document)");
        return;
    }

    if (ui::TreeNodeEx(static_cast<void*>(document),
                       ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth,
                       "%s", document->GetId().empty() ? ICON_FA_FILE " document"
                                                       : Format(ICON_FA_FILE " %s", document->GetId().c_str()).c_str()))
    {
        if (ui::IsItemClicked())
            tab->selected_ = document;
        for (Rml::Element* child = document->GetFirstChild(); child; child = child->GetNextSibling())
            RenderElement(child, 1);
        ui::TreePop();
    }
}

void UIViewHierarchy::RenderElement(Rml::Element* element, unsigned /*depth*/)
{
    UIViewTab* tab = owner_;
    if (!tab || !element)
        return;

    ea::string label = element->GetTagName().c_str();
    if (!element->GetId().empty())
    {
        label += "#";
        label += element->GetId().c_str();
    }

    const bool hasChildren = element->GetFirstChild() != nullptr;
    const bool selected = tab->selected_ == element;

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (hasChildren)
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
    else
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (selected)
        flags |= ImGuiTreeNodeFlags_Selected;

    bool open = ui::TreeNodeEx(static_cast<void*>(element), flags, "%s", label.c_str());

    if (ui::IsItemClicked() && !ui::IsItemToggledOpen())
    {
        tab->selected_ = element;
        contextMenuTarget_ = element;
    }

    if (hasChildren && open)
    {
        for (Rml::Element* child = element->GetFirstChild(); child; child = child->GetNextSibling())
            RenderElement(child, 0);
        ui::TreePop();
    }
}

void UIViewHierarchy::RenderContextMenuItems()
{
    UIViewTab* tab = owner_;
    if (!tab)
        return;

    Rml::Element* target = contextMenuTarget_ ? contextMenuTarget_ : tab->selected_;
    if (!target)
        return;

    if (ui::MenuItem(ICON_FA_CLIPBOARD " Copy RML"))
    {
        Rml::String rml;
        target->GetInnerRML(rml);
        // Expose to the clipboard via ImGui when available.
        if (!rml.empty())
            ui::SetClipboardText(rml.c_str());
    }
}

// ---------------------------------------------------------------------------
// UIViewInspector
// ---------------------------------------------------------------------------

UIViewInspector::UIViewInspector(UIViewTab* owner)
    : Object(owner->GetContext())
    , owner_(owner)
{
}

void UIViewInspector::RenderContent()
{
    UIViewTab* tab = owner_;
    if (!tab || !tab->selected_)
    {
        ui::TextDisabled("Select an element in the hierarchy to edit its attributes.");
        return;
    }

    Rml::Element* element = tab->selected_;
    ui::Text(ICON_FA_HAND_POINTER " %s", element->GetTagName().c_str());
    ui::Separator();

    RenderAttributes(element);
    ui::Separator();
    RenderInlineStyle(element);
}

void UIViewInspector::RenderAttributes(Rml::Element* element)
{
    UIViewTab* tab = owner_;
    if (ui::CollapsingHeader(ICON_FA_LIST " Attributes", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const Rml::ElementAttributes& attributes = element->GetAttributes();
        // Snapshot names first: editing may insert/erase entries in the map.
        ea::vector<ea::string> names;
        names.reserve(attributes.size());
        for (const auto& kv : attributes)
            names.push_back(kv.first.c_str());

        for (const ea::string& name : names)
        {
            Rml::Variant* value = element->GetAttribute(name.c_str());
            if (!value)
                continue;

            ea::string current = value->Get<Rml::String>().c_str();
            char keyBuf[128];
            char valBuf[1024];
            snprintf(keyBuf, sizeof(keyBuf), "%s", name.c_str());
            snprintf(valBuf, sizeof(valBuf), "%s", current.c_str());

            ui::PushID(name.c_str());
            ui::Text("%s", keyBuf);
            ui::SameLine();
            ui::PushItemWidth(-40.0f);
            if (ui::InputText("##value", valBuf, sizeof(valBuf), ImGuiInputTextFlags_EnterReturnsTrue))
            {
                element->SetAttribute(name.c_str(), valBuf);
                if (tab)
                    tab->dirty_ = true;
            }
            ui::PopItemWidth();
            ui::SameLine();
            if (ui::SmallButton(ICON_FA_TRASH))
            {
                // RmlUi exposes removal only through rebuilding the attribute set.
                Rml::ElementAttributes copy = element->GetAttributes();
                copy.erase(name.c_str());
                element->SetAttributes(copy);
                if (tab)
                    tab->dirty_ = true;
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
        if (ui::SmallButton(ICON_FA_PLUS) && Trim(attributeKeyBuf_).size())
        {
            element->SetAttribute(attributeKeyBuf_, attributeValueBuf_);
            if (tab)
                tab->dirty_ = true;
            attributeKeyBuf_[0] = '\0';
            attributeValueBuf_[0] = '\0';
        }
        ui::PopID();
    }
}

void UIViewInspector::RenderInlineStyle(Rml::Element* element)
{
    UIViewTab* tab = owner_;
    if (!ui::CollapsingHeader(ICON_FA_PAINTBRUSH " Inline Style", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    Rml::Element* current = tab ? tab->selected_ : nullptr;
    // Reseed the editor buffer only when the selection changes, so typing is
    // not clobbered every frame.
    if (current != lastStyleElement_)
    {
        lastStyleText_.clear();
        const Rml::PropertyMap& props = element->GetLocalStyleProperties();
        for (const auto& kv : props)
        {
            const Rml::String& propName = Rml::StyleSheetSpecification::GetPropertyName(kv.first);
            lastStyleText_ += ea::string(propName.c_str()) + ": " + ea::string(kv.second.ToString().c_str()) + ";\n";
        }
        snprintf(styleBuf_, sizeof(styleBuf_), "%s", lastStyleText_.c_str());
        lastStyleElement_ = current;
    }

    ui::InputTextMultiline("##style", styleBuf_, sizeof(styleBuf_), ImVec2(-1.0f, 120.0f));
    if (ui::Button(ICON_FA_CHECK " Apply Style"))
    {
        // Clear existing local overrides, then re-parse "name: value;" lines.
        const Rml::PropertyMap props = element->GetLocalStyleProperties();
        for (const auto& kv : props)
            element->RemoveProperty(kv.first);

        ea::string text = styleBuf_;
        size_t pos = 0;
        while (pos < text.size())
        {
            size_t lineEnd = text.find('\n', pos);
            if (lineEnd == ea::string::npos)
                lineEnd = text.size();
            ea::string line = Trim(text.substr(pos, lineEnd - pos));
            pos = lineEnd + 1;
            if (line.empty())
                continue;
            size_t colon = line.find(':');
            if (colon == ea::string::npos)
                continue;
            ea::string name = Trim(line.substr(0, colon));
            ea::string value = Trim(line.substr(colon + 1));
            if (!value.empty() && value.back() == ';')
                value.pop_back();
            if (!name.empty())
                element->SetProperty(name.c_str(), value.c_str());
        }
        if (tab)
            tab->dirty_ = true;
        lastStyleText_.clear();
        lastStyleElement_ = nullptr;
    }
}

}

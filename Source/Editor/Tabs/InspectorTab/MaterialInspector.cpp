//
// Copyright (c) 2017-2020 the rbfx project.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include "../../Tabs/InspectorTab/MaterialInspector.h"

#include <Urho3D/Graphics/AnimatedModel.h>
#include <Urho3D/Graphics/StaticModel.h>
#include <Urho3D/Resource/ResourceCache.h>

#include <IconFontCppHeaders/IconsFontAwesome6.h>

namespace Urho3D
{

void Tabs_MaterialInspector(Context* context, InspectorTab* inspectorTab)
{
    inspectorTab->RegisterAddon<MaterialInspector>(inspectorTab->GetProject());
}

MaterialInspector::MaterialInspector(Project* project)
    : Object(project->GetContext())
    , project_(project)
{
    project_->OnRequest.Subscribe(this, &MaterialInspector::OnProjectRequest);

    // Hook the model components' "Material" reference attribute so the inline material editor is
    // rendered immediately after the material path row (Unity-style) instead of at the end of the
    // whole component. Each referenced material then gets its own collapsible section, gated by
    // whether it is a project asset (editable) or a built-in engine material (greyed, read-only).
    SerializableInspectorWidget::RegisterAttributeHook<StaticModel>("Material",
        [this](const AttributeHookContext& ctx, Variant& value) { return OnMaterialAttribute(ctx, value); });
    SerializableInspectorWidget::RegisterAttributeHook<AnimatedModel>("Material",
        [this](const AttributeHookContext& ctx, Variant& value) { return OnMaterialAttribute(ctx, value); });
}

MaterialInspector::~MaterialInspector()
{
    SerializableInspectorWidget::UnregisterAttributeHook<StaticModel>("Material");
    SerializableInspectorWidget::UnregisterAttributeHook<AnimatedModel>("Material");
}

void MaterialInspector::OnProjectRequest(ProjectRequest* request)
{
    auto inspectResourceRequest = dynamic_cast<InspectResourceRequest*>(request);
    if (!inspectResourceRequest || inspectResourceRequest->GetResources().empty())
        return;

    const auto& resources = inspectResourceRequest->GetResources();

    const bool areAllMaterials = ea::all_of(resources.begin(), resources.end(),
        [](const ResourceFileDescriptor& desc) { return desc.HasObjectType<Material>(); });
    if (!areAllMaterials)
        return;

    request->QueueProcessCallback([=]()
    {
        const auto resourceNames = inspectResourceRequest->GetSortedResourceNames();
        if (resourceNames_ != resourceNames)
        {
            resourceNames_ = resourceNames;
            InspectResources();
        }
        OnActivated(this);
    });
}

void MaterialInspector::InspectResources()
{
    auto cache = GetSubsystem<ResourceCache>();

    MaterialInspectorWidget::MaterialVector materials;
    for (const ea::string& resourceName : resourceNames_)
    {
        if (auto material = cache->GetResource<Material>(resourceName))
            materials.emplace_back(material);
    }

    resourceNames_.clear();
    for (Material* material : materials)
        resourceNames_.emplace_back(material->GetName());

    if (materials.empty())
    {
        widget_ = nullptr;
        return;
    }

    widget_ = MakeShared<MaterialInspectorWidget>(context_, materials);
    widget_->UpdateTechniques(techniquePath_);
    widget_->OnEditBegin.Subscribe(this, &MaterialInspector::BeginEdit);
    widget_->OnEditEnd.Subscribe(this, &MaterialInspector::EndEdit);
}

void MaterialInspector::BeginEdit()
{
    // Incomplete action will include all the changes automatically
    if (pendingAction_ && !pendingAction_->IsComplete())
        return;

    auto undoManager = project_->GetUndoManager();

    pendingAction_ = MakeShared<ModifyResourceAction>(project_);
    for (Material* material : widget_->GetMaterials())
        pendingAction_->AddResource(material);

    // Initialization of "redo" state is delayed so it's okay to push the action here
    undoManager->PushAction(pendingAction_);
}

void MaterialInspector::EndEdit()
{
    for (Material* material : widget_->GetMaterials())
        project_->SaveFileDelayed(material);
}

bool MaterialInspector::OnMaterialAttribute(const AttributeHookContext& ctx, Variant& boxedValue)
{
    // Draw the attribute label the default renderer would have shown.
    Widgets::ItemLabel(ctx.info_->name_.c_str(),
        Widgets::GetItemLabelColor(ctx.isUndefined_, ctx.isDefaultValue_));

    // Single selection of a material reference list: render each slot as a collapsing header that
    // also carries the browse/reveal buttons, replacing the standalone path row (Unity-style).
    if (ctx.objects_ && ctx.objects_->size() == 1 && boxedValue.GetType() == VAR_RESOURCEREFLIST)
        return RenderInlineMaterials(boxedValue);

    // Multi-selection or unexpected layout: fall back to the standard resource-reference list editor.
    return Widgets::EditVariant(boxedValue, Widgets::EditVariantOptions());
}

bool MaterialInspector::RenderInlineMaterials(Variant& boxedValue)
{
    ResourceRefList list = boxedValue.GetResourceRefList();
    auto cache = GetSubsystem<ResourceCache>();
    const ea::string dataPath = project_ ? project_->GetDataPath() : EMPTY_STRING;

    // No slots referenced: show the plain list editor so an entry can still be managed.
    if (list.names_.empty())
        return Widgets::EditVariant(boxedValue, Widgets::EditVariantOptions());

    // Rebuild cached widgets only when the referenced set changes, so preview scenes are not
    // recreated every frame. One entry per geometry/material slot, mirroring the list order.
    if (inlineMaterialNames_ != list.names_ || inlineMaterials_.size() != list.names_.size())
    {
        inlineMaterials_.clear();
        inlineMaterialNames_ = list.names_;
        for (const ea::string& name : list.names_)
        {
            InlineMaterial entry;
            entry.name_ = name;
            // GetResource<Material> returns a raw pointer owned by the ResourceCache.
            Material* material = name.empty() ? nullptr : cache->GetResource<Material>(name);
            // A material is editable inline only when the file it was loaded from lives under the
            // project's Data directory. Built-in materials resolve from engine CoreData (a different
            // absolute root) and are shown greyed and non-expandable, mirroring Unity.
            const ea::string abs = material ? material->GetAbsoluteFileName() : EMPTY_STRING;
            entry.isProjectAsset_ = material && !dataPath.empty() && !abs.empty() && abs.starts_with(dataPath);
            if (entry.isProjectAsset_)
            {
                MaterialInspectorWidget::MaterialVector materials;
                materials.emplace_back(material);
                entry.widget_ = MakeShared<MaterialInspectorWidget>(context_, materials);
                entry.widget_->UpdateTechniques(techniquePath_);
                entry.widget_->OnEditBegin.Subscribe(this, &MaterialInspector::InlineBeginEdit);
                entry.widget_->OnEditEnd.Subscribe(this, &MaterialInspector::InlineEndEdit);
            }
            inlineMaterials_.push_back(entry);
        }
    }

    bool modified = false;
    for (unsigned i = 0; i < inlineMaterials_.size(); ++i)
    {
        InlineMaterial& entry = inlineMaterials_[i];

        // Scratch copy of the reference so a browse/drop can rewrite this slot in place.
        ea::string name = list.names_[i];
        StringHash type = list.type_;

        ui::Separator();

        bool open = false;
        if (entry.isProjectAsset_ && entry.widget_)
        {
            // Project material: collapsible header labeled with the path. SpanTextWidth narrows the
            // header hit box (and the drop target bound to it) to the label text only, so the
            // trailing browse/reveal buttons rendered on the same line are not overlapped and stay
            // clickable (a default CollapsingHeader is Framed and would span the whole row).
            open = ui::CollapsingHeader(entry.name_.c_str(),
                ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanTextWidth);
        }
        else
        {
            // Built-in or empty material: greyed leaf line (not expandable).
            if (!entry.name_.empty())
                ui::TextDisabled(ICON_FA_LOCK " %s (built-in material)", entry.name_.c_str());
            else
                ui::TextDisabled("(no material)");
        }

        // The drag-drop target binds to the header/text item rendered just above, so a resource can
        // be dropped onto the row to (re)assign this slot; the browse/reveal buttons follow on the
        // same line. Each may rewrite the scratch reference, which is committed back to the list.
        bool refModified = false;
        if (Widgets::EditResourceRefDropTarget(type, name, nullptr))
            refModified = true;
        // Invisible spacer between the path label and the trailing buttons so the browse/reveal
        // icons are not glued to the text. Placed after the drop target so the header stays the
        // item the drag-drop binds to; the buttons' own SameLine follows this spacer.
        ui::SameLine();
        ui::Dummy(ImVec2(8.0f, 1.0f));
        if (Widgets::EditResourceRefButtons(type, name, nullptr))
            refModified = true;
        if (refModified)
        {
            list.names_[i] = name;
            modified = true;
        }

        if (open)
        {
            ui::Indent();
            entry.widget_->RenderContent();
            ui::Unindent();
        }
    }

    if (modified)
        boxedValue = list;
    return modified;
}

void MaterialInspector::InlineBeginEdit()
{
    if (inlinePendingAction_ && !inlinePendingAction_->IsComplete())
        return;

    auto undoManager = project_->GetUndoManager();

    inlinePendingAction_ = MakeShared<ModifyResourceAction>(project_);
    for (InlineMaterial& entry : inlineMaterials_)
    {
        if (!entry.widget_)
            continue;
        for (Material* material : entry.widget_->GetMaterials())
            inlinePendingAction_->AddResource(material);
    }

    undoManager->PushAction(inlinePendingAction_);
}

void MaterialInspector::InlineEndEdit()
{
    for (InlineMaterial& entry : inlineMaterials_)
    {
        if (!entry.widget_)
            continue;
        for (Material* material : entry.widget_->GetMaterials())
            project_->SaveFileDelayed(material);
    }
}

void MaterialInspector::RenderContent()
{
    if (!widget_)
        return;

    if (updateTimer_.GetMSec(false) > updatePeriodMs_)
    {
        widget_->UpdateTechniques(techniquePath_);
        updateTimer_.Reset();
    }

    widget_->RenderTitle();
    ui::Separator();
    widget_->RenderContent();
}

void MaterialInspector::RenderContextMenuItems()
{
}

void MaterialInspector::RenderMenu()
{
}

void MaterialInspector::ApplyHotkeys(HotkeyManager* hotkeyManager)
{
}

}

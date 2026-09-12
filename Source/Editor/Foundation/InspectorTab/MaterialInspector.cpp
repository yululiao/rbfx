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

#include "../../Foundation/InspectorTab/MaterialInspector.h"

#include <Urho3D/Graphics/AnimatedModel.h>
#include <Urho3D/Graphics/StaticModel.h>
#include <Urho3D/Resource/ResourceCache.h>

#include <IconFontCppHeaders/IconsFontAwesome6.h>

namespace Urho3D
{

void Foundation_MaterialInspector(Context* context, InspectorTab* inspectorTab)
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
    // An attribute hook replaces the default rendering entirely, so reproduce it first: draw the
    // "Material" label and the resource-reference list. This keeps assignment/removal working and
    // still surfaces the browse + reveal buttons on every material slot.
    Widgets::ItemLabel(ctx.info_->name_.c_str(),
        Widgets::GetItemLabelColor(ctx.isUndefined_, ctx.isDefaultValue_));
    const bool pathModified = Widgets::EditVariant(boxedValue, Widgets::EditVariantOptions());

    // Inline per-material editing is only meaningful for a single selected component.
    if (ctx.objects_ && ctx.objects_->size() == 1)
    {
        if (auto* model = dynamic_cast<StaticModel*>(ctx.objects_->front().Get()))
            RenderInlineMaterials(model);
    }

    return pathModified;
}

void MaterialInspector::RenderInlineMaterials(StaticModel* model)
{
    const ea::string dataPath = project_ ? project_->GetDataPath() : EMPTY_STRING;

    // Collect unique referenced materials in geometry order, tagging each as project or built-in.
    StringVector names;
    MaterialInspectorWidget::MaterialVector materials;
    ea::vector<bool> isProject;
    for (unsigned i = 0; i < model->GetNumGeometries(); ++i)
    {
        Material* material = model->GetMaterial(i);
        if (!material)
            continue;
        const ea::string name = material->GetName();
        if (names.end() != ea::find(names.begin(), names.end(), name))
            continue;
        names.push_back(name);
        materials.emplace_back(material);
        // A material is editable inline only when the file it was loaded from lives under the
        // project's Data directory. Built-in materials resolve from engine CoreData (a different
        // absolute root) and are shown greyed and non-expandable, mirroring Unity.
        const ea::string& abs = material->GetAbsoluteFileName();
        isProject.push_back(!dataPath.empty() && !abs.empty() && abs.starts_with(dataPath));
    }

    if (names.empty())
    {
        inlineMaterials_.clear();
        inlineMaterialNames_.clear();
        return;
    }

    // Rebuild widgets only when the referenced set changes, so preview scenes are not recreated
    // every frame.
    if (inlineMaterialNames_ != names || inlineMaterials_.size() != names.size())
    {
        inlineMaterialNames_ = names;
        inlineMaterials_.clear();
        for (size_t k = 0; k < names.size(); ++k)
        {
            InlineMaterial entry;
            entry.name_ = names[k];
            entry.isProjectAsset_ = isProject[k];
            if (entry.isProjectAsset_)
            {
                entry.widget_ = MakeShared<MaterialInspectorWidget>(context_,
                    MaterialInspectorWidget::MaterialVector{ materials[k] });
                entry.widget_->UpdateTechniques(techniquePath_);
                entry.widget_->OnEditBegin.Subscribe(this, &MaterialInspector::InlineBeginEdit);
                entry.widget_->OnEditEnd.Subscribe(this, &MaterialInspector::InlineEndEdit);
            }
            inlineMaterials_.push_back(entry);
        }
    }

    // Render each material: project assets as expandable/editable sections, built-in as greyed.
    for (InlineMaterial& entry : inlineMaterials_)
    {
        if (entry.isProjectAsset_ && entry.widget_)
        {
            ui::Separator();
            if (ui::CollapsingHeader(entry.name_.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            {
                ui::Indent();
                entry.widget_->RenderContent();
                ui::Unindent();
            }
        }
        else
        {
            // Built-in material: greyed, no foldout, not editable (matches Unity's built-in assets).
            ui::TextDisabled(ICON_FA_LOCK " %s (built-in material)", entry.name_.c_str());
        }
    }
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

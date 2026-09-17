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

#include "../../Tabs/InspectorTab/GeometryInspector.h"

#include "../../Project/ModifyResourceAction.h"
#include "../../Project/Project.h"

#include <Urho3D/Graphics/AnimatedModel.h>
#include <Urho3D/Graphics/Geometry.h>
#include <Urho3D/Graphics/IndexBuffer.h>
#include <Urho3D/Graphics/Model.h>
#include <Urho3D/Graphics/StaticModel.h>
#include <Urho3D/Graphics/VertexBuffer.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/SystemUI/SerializableInspectorWidget.h>
#include <Urho3D/SystemUI/SystemUI.h>
#include <Urho3D/SystemUI/Widgets.h>

#include <IconFontCppHeaders/IconsFontAwesome6.h>

namespace Urho3D
{

void Tabs_GeometryInspector(Context* context, InspectorTab* inspectorTab)
{
    inspectorTab->RegisterAddon<GeometryInspector>(inspectorTab->GetProject());
}

GeometryInspector::GeometryInspector(Project* project)
    : Object(project->GetContext())
    , project_(project)
{
    // The material of the selected geometry slot is presented by this panel, so the raw
    // "Material" attribute row is hidden in every node/component inspector. Registered globally
    // on construction (before any inspector renders) and removed on destruction.
    SerializableInspectorWidget::RegisterAttributeHidden<StaticModel>("Material");
    SerializableInspectorWidget::RegisterAttributeHidden<AnimatedModel>("Material");

    project_->OnRequest.SubscribeWithSender(this, &GeometryInspector::OnProjectRequest);
}

GeometryInspector::~GeometryInspector()
{
    project_->OnRequest.Unsubscribe(this);

    SerializableInspectorWidget::UnregisterAttributeHidden<StaticModel>("Material");
    SerializableInspectorWidget::UnregisterAttributeHidden<AnimatedModel>("Material");
}

void GeometryInspector::OnProjectRequest(RefCounted* senderTab, ProjectRequest* request)
{
    auto inspectNodeComponentRequest = dynamic_cast<InspectNodeComponentRequest*>(request);
    if (!inspectNodeComponentRequest)
        return;

    Component* geometry = inspectNodeComponentRequest->GetActiveGeometryComponent();
    if (!geometry)
        return;

    if (GetGeometryCount(geometry) == M_MAX_UNSIGNED)
        return;

    auto inspectedTab = dynamic_cast<EditorTab*>(senderTab);
    const unsigned geometryIndex = inspectNodeComponentRequest->GetActiveGeometryIndex();

    request->QueueProcessCallback([=]()
    {
        if (geometryComponent_ != geometry || geometryIndex_ != geometryIndex || inspectedTab_ != inspectedTab)
        {
            geometryComponent_ = geometry;
            geometryIndex_ = geometryIndex;
            inspectedTab_ = inspectedTab;
            // Slot reference may have changed while the same geometry stayed selected.
            RebuildMaterialWidget();
        }
        OnActivated(this);
    });
}

unsigned GeometryInspector::GetGeometryCount(const Component* component) const
{
    if (const auto model = component->GetComponent<StaticModel>())
        return model->GetNumGeometries();
    if (const auto animated = component->GetComponent<AnimatedModel>())
        return animated->GetNumGeometries();
    return M_MAX_UNSIGNED;
}

Material* GeometryInspector::GetSlotMaterial() const
{
    Component* component = geometryComponent_;
    if (!component || geometryIndex_ >= GetGeometryCount(component))
        return nullptr;

    // AnimatedModel inherits StaticModel but keeps its own material list, so it must be checked first.
    if (auto animated = component->GetComponent<AnimatedModel>())
        return animated->GetMaterial(geometryIndex_);
    if (auto model = component->GetComponent<StaticModel>())
        return model->GetMaterial(geometryIndex_);
    return nullptr;
}

Model* GeometryInspector::GetSlotModel() const
{
    Component* component = geometryComponent_;
    if (!component)
        return nullptr;

    // AnimatedModel inherits StaticModel but is a distinct component type, so it must be checked first.
    if (auto animated = component->GetComponent<AnimatedModel>())
        return animated->GetModel();
    if (auto staticModel = component->GetComponent<StaticModel>())
        return staticModel->GetModel();
    return nullptr;
}

ea::string GeometryInspector::GetSlotGeometryName() const
{
    // The mesh name lives on the shared Model, indexed by the same geometry/material slot.
    Model* model = GetSlotModel();
    return model ? model->GetGeometryName(geometryIndex_) : EMPTY_STRING;
}

void GeometryInspector::RebuildMaterialWidget()
{
    Component* component = geometryComponent_;
    ea::string slotName;
    if (component)
    {
        const Variant listValue = component->GetAttribute("Material");
        const StringVector& names = listValue.GetResourceRefList().names_;
        if (geometryIndex_ < names.size())
            slotName = names[geometryIndex_];
    }

    // Rebuild only when the slot reference changes; the widget check makes built-in (read-only,
    // no widget) materials settle without re-resolving every frame.
    if (slotName == slotMaterialName_ && (!materialWidget_ || slotName.empty()))
        return;

    materialWidget_ = nullptr;
    slotMaterialName_ = slotName;
    if (slotName.empty())
        return;

    Material* material = GetSubsystem<ResourceCache>()->GetResource<Material>(slotName);
    // Only project materials (loaded from under the project's Data directory) are editable here;
    // built-in materials from engine CoreData are shown as a read-only reference row.
    const ea::string dataPath = project_ ? project_->GetDataPath() : EMPTY_STRING;
    const ea::string abs = material ? material->GetAbsoluteFileName() : EMPTY_STRING;
    if (!material || dataPath.empty() || !abs.starts_with(dataPath))
        return;

    MaterialInspectorWidget::MaterialVector materials;
    materials.emplace_back(material);
    materialWidget_ = MakeShared<MaterialInspectorWidget>(context_, materials);
    materialWidget_->UpdateTechniques(techniquePath_);
    materialWidget_->OnEditBegin.Subscribe(this, &GeometryInspector::BeginMaterialEdit);
    materialWidget_->OnEditEnd.Subscribe(this, &GeometryInspector::EndMaterialEdit);
    updateTimer_.Reset();
}

void GeometryInspector::BeginMaterialEdit()
{
    if (!project_)
        return;

    // Incomplete action will include all the changes automatically
    if (pendingAction_ && !pendingAction_->IsComplete())
        return;

    auto undoManager = project_->GetUndoManager();

    pendingAction_ = MakeShared<ModifyResourceAction>(project_);
    for (Material* material : materialWidget_->GetMaterials())
        pendingAction_->AddResource(material);

    // Initialization of "redo" state is delayed so it's okay to push the action here
    undoManager->PushAction(pendingAction_);
}

void GeometryInspector::EndMaterialEdit()
{
    for (Material* material : materialWidget_->GetMaterials())
        project_->SaveFileDelayed(material);
}

void GeometryInspector::RenderContent()
{
    Component* component = geometryComponent_;
    if (!component || geometryIndex_ >= GetGeometryCount(component))
    {
        ui::TextDisabled("(no geometry selected)");
        return;
    }

    if (updateTimer_.GetMSec(false) > updatePeriodMs_)
    {
        if (materialWidget_)
            materialWidget_->UpdateTechniques(techniquePath_);
        updateTimer_.Reset();
    }

    const Node* node = component->GetNode();
    const ea::string geometryName = GetSlotGeometryName();
    if (!geometryName.empty())
        ui::Text(ICON_FA_LAYER_GROUP " %s", geometryName.c_str());
    else
        ui::Text(ICON_FA_LAYER_GROUP " Geometry #%d", geometryIndex_);
    ui::Separator();

    // Material slot row: rewrite names_[geometryIndex_] in place with browse/drag-drop support.
    const AttributeInfo* attr = nullptr;
    if (const auto attributes = component->GetAttributes())
    {
        const auto iter = ea::find_if(attributes->begin(), attributes->end(),
            [](const AttributeInfo& info) { return info.name_ == "Material"; });
        if (iter != attributes->end())
            attr = &*iter;
    }
    if (!attr)
        return;

    ResourceRefList list = component->GetAttribute("Material").GetResourceRefList();
    if (geometryIndex_ >= list.names_.size())
        list.names_.resize(geometryIndex_ + 1);

    // Scratch copy of the slot reference so the browse/drag-drop widgets can rewrite it in place.
    ea::string name = list.names_[geometryIndex_];
    StringHash type = list.type_;

    // Unity-like material slot (mirrors MaterialInspector's inline layout): the path is the
    // collapsing-header label and the browse/reveal buttons ride the same line; expanding it reveals
    // the material parameters. SpanTextWidth narrows the header hit box to the label text so the
    // trailing buttons are not overlapped (a default CollapsingHeader would span the whole row).
    const ea::string label = name.empty() ? ea::string("(no material)") : name;
    bool open = ui::CollapsingHeader(label.c_str(),
        ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanTextWidth);

    bool modified = false;
    // Drag-drop target binds to the header rendered just above, so a resource can be dropped onto the
    // row to (re)assign the slot; the browse + navigate buttons follow on the same line.
    if (Widgets::EditResourceRefDropTarget(type, name, nullptr))
        modified = true;
    ui::SameLine();
    ui::Dummy(ImVec2(8.0f, 1.0f));
    if (Widgets::EditResourceRefButtons(type, name, nullptr))
        modified = true;

    if (modified)
    {
        list.names_[geometryIndex_] = name;

        // Snapshot the "old" state before the write so the action built afterwards contains both sides.
        const ea::vector<Component*> components{ component };
        ChangeComponentAttributesActionBuilder builder(actionBuffer_, component->GetScene(), components, *attr);
        component->SetAttribute("Material", Variant(list));
        component->ApplyAttributes();
        if (inspectedTab_)
            inspectedTab_->PushAction(builder.Build());
    }

    // (Re)create the nested widget; after a change the new reference is picked up in the same frame.
    RebuildMaterialWidget();

    if (open)
    {
        ui::Indent();
        Material* material = GetSlotMaterial();
        if (!material)
            ui::TextDisabled("(no material assigned)");
        else if (!materialWidget_)
            ui::TextDisabled(ICON_FA_LOCK " %s (built-in material)", material->GetName().c_str());
        else
            materialWidget_->RenderContent();
        ui::Unindent();
    }

    // Read-only statistics below the editable material slot.
    RenderGeometryStats();
}

void GeometryInspector::RenderGeometryStats()
{
    Model* model = GetSlotModel();
    if (!model || geometryIndex_ >= model->GetNumGeometries())
        return;

    Geometry* geometry = model->GetGeometry(geometryIndex_, 0);
    if (!geometry)
    {
        ui::TextDisabled("(no geometry data)");
        return;
    }

    // Read-only snapshot of the selected slot. These numbers describe the shared .mdl asset, so they
    // are informational only - geometry data edits belong to the ModelInspector asset editor, keeping
    // with "one feature, one entry point" (this panel only edits the per-instance material override).
    if (!ui::CollapsingHeader(ICON_FA_CHART_SIMPLE " Geometry Info"))
        return;

    const unsigned vertices = geometry->GetVertexCount();
    const unsigned indices = geometry->GetIndexCount();
    const unsigned primitives = geometry->GetPrimitiveCount();

    static const char* primitiveNames[] = {
        "Triangle List", "Line List", "Point List", "Triangle Strip", "Line Strip", "Triangle Fan",
    };
    const unsigned ptIndex = static_cast<unsigned>(geometry->GetPrimitiveType());
    const char* primitive = ptIndex < sizeof(primitiveNames) / sizeof(primitiveNames[0])
        ? primitiveNames[ptIndex] : "Unknown";

    // Aggregate the attribute layout across every vertex buffer the geometry draws from and total the
    // per-vertex stride. A missing normal/tangent is a common import problem, so it is flagged below.
    ea::vector<VertexElement> elements;
    unsigned stride = 0;
    const unsigned numBuffers = geometry->GetNumVertexBuffers();
    for (unsigned b = 0; b < numBuffers; ++b)
    {
        if (auto* vb = geometry->GetVertexBuffer(b))
        {
            const auto& bufferElements = vb->GetElements();
            elements.insert(elements.end(), bufferElements.begin(), bufferElements.end());
            stride += vb->GetVertexSize();
        }
    }

    bool hasPosition = false, hasNormal = false, hasTangent = false, hasBinormal = false;
    bool hasColor = false, hasWeights = false, hasBoneIndices = false;
    unsigned uvSets = 0;
    for (const auto& element : elements)
    {
        switch (element.semantic_)
        {
        case SEM_POSITION: hasPosition = true; break;
        case SEM_NORMAL: hasNormal = true; break;
        case SEM_TANGENT: hasTangent = true; break;
        case SEM_BINORMAL: hasBinormal = true; break;
        case SEM_TEXCOORD: ++uvSets; break;
        case SEM_COLOR: hasColor = true; break;
        case SEM_BLENDWEIGHTS: hasWeights = true; break;
        case SEM_BLENDINDICES: hasBoneIndices = true; break;
        default: break;
        }
    }

    // Per-slot GPU footprint estimate from the first vertex buffer stride and the index buffer stride.
    // Vertex buffers can be shared between slots, so this is a representative per-slot figure.
    unsigned long long vboBytes = 0;
    if (auto* vb = geometry->GetVertexBuffer(0))
        vboBytes = static_cast<unsigned long long>(vb->GetVertexSize()) * vertices;
    unsigned long long iboBytes = 0;
    if (auto* ib = geometry->GetIndexBuffer())
        iboBytes = static_cast<unsigned long long>(ib->GetIndexSize()) * indices;
    const double totalKb = static_cast<double>(vboBytes + iboBytes) / 1024.0;

    ui::Text("Vertices: %u", vertices);
    ui::Text("Indices: %u", indices);
    ui::Text("Primitives: %u", primitives);
    ui::Text("Primitive type: %s", primitive);
    ui::Text("Vertex stride: %u bytes", stride);
    ui::Text("GPU footprint: ~%.1f KB", totalKb);
    ui::Text("LOD levels: %u", model->GetNumGeometryLodLevels(geometryIndex_));

    const Vector3 center = model->GetGeometryCenter(geometryIndex_);
    ui::Text("Center: %.2f, %.2f, %.2f", center.x_, center.y_, center.z_);
    const Vector3 size = model->GetBoundingBox().Size();
    ui::Text("Model size: %.2f, %.2f, %.2f", size.x_, size.y_, size.z_);

    ea::string layout;
    auto add = [&layout](const char* name)
    {
        if (!layout.empty())
            layout += ", ";
        layout += name;
    };
    if (hasPosition) add("Position");
    if (hasNormal) add("Normal");
    if (hasTangent) add("Tangent");
    if (hasBinormal) add("Binormal");
    if (hasColor) add("Color");
    if (hasWeights) add("Weights");
    if (hasBoneIndices) add("BoneIndices");
    if (uvSets != 0)
    {
        if (!layout.empty())
            layout += ", ";
        char uvBuf[16];
        snprintf(uvBuf, sizeof(uvBuf), "UV x%u", uvSets);
        layout += uvBuf;
    }

    ui::Separator();
    ui::TextDisabled("Attributes: %s", layout.empty() ? "(none)" : layout.c_str());
    if (!hasNormal)
        ui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION " No normals - lighting may look flat");
}

void GeometryInspector::RenderContextMenuItems()
{
}

void GeometryInspector::RenderMenu()
{
}

void GeometryInspector::ApplyHotkeys(HotkeyManager* hotkeyManager)
{
}

}

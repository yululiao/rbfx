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

#pragma once

#include "../../Core/CommonEditorActions.h"
#include "../../Tabs/InspectorTab.h"
#include "../../Project/ModifyResourceAction.h"
#include "../../Project/ProjectRequest.h"

#include <Urho3D/Core/Timer.h>
#include <Urho3D/SystemUI/MaterialInspectorWidget.h>

namespace Urho3D
{

class Model;

void Tabs_GeometryInspector(Context* context, InspectorTab* inspectorTab);

/// Dedicated inspector for a geometry/material slot of a model component (StaticModel/AnimatedModel)
/// that was selected as a child item in the scene hierarchy. Shows the slot's material reference and
/// nests a MaterialInspectorWidget for the referenced material so its parameters can be edited
/// without opening the material asset separately. While a geometry slot is active the regular node
/// and component inspectors step aside (see NodeComponentInspector), and the components' "Material"
/// attribute rows are hidden globally (see SerializableInspectorWidget::RegisterAttributeHidden).
class GeometryInspector : public Object, public InspectorSource
{
    URHO3D_OBJECT(GeometryInspector, Object)

public:
    explicit GeometryInspector(Project* project);
    ~GeometryInspector() override;

    /// Implement InspectorSource
    /// @{
    EditorTab* GetOwnerTab() override { return inspectedTab_; }
    bool IsUndoSupported() override { return true; }

    void RenderContent() override;
    void RenderContextMenuItems() override;
    void RenderMenu() override;
    void ApplyHotkeys(HotkeyManager* hotkeyManager) override;
    /// @}

private:
    void OnProjectRequest(RefCounted* senderTab, ProjectRequest* request);

    /// Return the component geometry count, or M_MAX_UNSIGNED if the component is not a model with geometries.
    unsigned GetGeometryCount(const Component* component) const;
    /// Return the material referenced by the active geometry slot, or null.
    Material* GetSlotMaterial() const;
    /// Return the shared Model asset of the active component, or null.
    Model* GetSlotModel() const;
    /// Return the source mesh name stored on the model geometry for the active slot (may be empty).
    ea::string GetSlotGeometryName() const;
    /// (Re)create the nested material widget when the slot reference changed; project assets only.
    void RebuildMaterialWidget();

    /// Render the read-only geometry statistics panel (topology, attributes, bounds) for the active slot.
    void RenderGeometryStats();

    void BeginMaterialEdit();
    void EndMaterialEdit();

    WeakPtr<Project> project_;

    WeakPtr<EditorTab> inspectedTab_;
    WeakPtr<Component> geometryComponent_;
    unsigned geometryIndex_{M_MAX_UNSIGNED};

    SharedPtr<MaterialInspectorWidget> materialWidget_;
    ea::string slotMaterialName_;

    ChangeAttributeBuffer actionBuffer_;
    SharedPtr<ModifyResourceAction> pendingAction_;

    const unsigned updatePeriodMs_{1000};
    const ea::string techniquePath_{"Techniques/"};
    Timer updateTimer_;
};

}

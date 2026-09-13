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
#include "../../Foundation/InspectorTab.h"
#include "../../Project/ModifyResourceAction.h"

#include <Urho3D/Core/Timer.h>
#include <Urho3D/SystemUI/MaterialInspectorWidget.h>
#include <Urho3D/SystemUI/SerializableInspectorWidget.h>

namespace Urho3D
{

void Foundation_MaterialInspector(Context* context, InspectorTab* inspectorTab);

/// Scene hierarchy provider for hierarchy browser tab.
class MaterialInspector : public Object, public InspectorSource
{
    URHO3D_OBJECT(MaterialInspector, Object)

public:
    explicit MaterialInspector(Project* project);
    ~MaterialInspector() override;

    /// Implement InspectorSource
    /// @{
    bool IsUndoSupported() override { return true; }

    void RenderContent() override;
    void RenderContextMenuItems() override;
    void RenderMenu() override;
    void ApplyHotkeys(HotkeyManager* hotkeyManager) override;
    /// @}

private:
    void OnProjectRequest(ProjectRequest* request);
    void InspectResources();

    void BeginEdit();
    void EndEdit();

    /// Inline material editing embedded under StaticModel/AnimatedModel components in the node
    /// inspector (via a SerializableInspectorWidget attribute hook on the "Material" reference).
    /// This edits the same shared Material resource as the standalone inspector, so it reuses
    /// ModifyResourceAction for undo and SaveFileDelayed for persistence.
    ///
    /// Unity-like layout: the standalone material path row is removed and each referenced material
    /// slot is rendered as a collapsing header whose label is the material path and which carries
    /// the browse/reveal buttons on the same line. Expanding it shows the material properties inline.
    /// A project material (under the project's Data folder) is expandable/editable; a built-in
    /// material (loaded from engine CoreData) is shown greyed and cannot be expanded.
    /// @{
    bool OnMaterialAttribute(const AttributeHookContext& ctx, Variant& boxedValue);
    bool RenderInlineMaterials(Variant& boxedValue);
    void InlineBeginEdit();
    void InlineEndEdit();
    /// @}

    const unsigned updatePeriodMs_{1000};
    const ea::string techniquePath_{"Techniques/"};

    WeakPtr<Project> project_;

    StringVector resourceNames_;
    SharedPtr<MaterialInspectorWidget> widget_;
    Timer updateTimer_;

    SharedPtr<ModifyResourceAction> pendingAction_;

    /// Widgets and state for inline material editing under model components, one entry per
    /// referenced material so built-in and project materials can be gated independently.
    /// @{
    struct InlineMaterial
    {
        ea::string name_;
        bool isProjectAsset_{}; // false => built-in (CoreData): greyed, not expandable
        SharedPtr<MaterialInspectorWidget> widget_;
    };
    ea::vector<InlineMaterial> inlineMaterials_;
    StringVector inlineMaterialNames_; // material-name set the widgets were last built for
    SharedPtr<ModifyResourceAction> inlinePendingAction_;
    /// @}
};

}

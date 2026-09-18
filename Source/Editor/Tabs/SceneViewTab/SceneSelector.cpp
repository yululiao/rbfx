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

#include "../../Tabs/SceneViewTab/SceneSelector.h"

#include <Urho3D/Graphics/Camera.h>
#include <Urho3D/Graphics/Octree.h>
#include <Urho3D/Graphics/StaticModel.h>
#include <Urho3D/SystemUI/SystemUI.h>

namespace Urho3D
{

void Tabs_SceneSelector(Context* context, SceneViewTab* sceneViewTab)
{
    sceneViewTab->RegisterAddon<SceneSelector>();
}

SceneSelector::SceneSelector(SceneViewTab* owner)
    : SceneViewAddon(owner)
{
}

void SceneSelector::ProcessInput(SceneViewPage& scenePage, bool& mouseConsumed)
{
    Scene* scene = scenePage.scene_;

    if (!mouseConsumed)
    {
        // Alt + LMB is reserved for camera orbiting, do not select on click
        if (ui::IsItemHovered() && ui::IsMouseReleased(MOUSEB_LEFT) && !ui::IsMouseDragPastThreshold(MOUSEB_LEFT)
            && !ui::IsKeyDown(KEY_LALT) && !ui::IsKeyDown(KEY_RALT))
        {
            mouseConsumed = true;
            const bool toggle = ui::IsKeyDown(KEY_LCTRL) || ui::IsKeyDown(KEY_RCTRL);
            const bool append = ui::IsKeyDown(KEY_LSHIFT) || ui::IsKeyDown(KEY_RSHIFT);

            // A model is split into per-geometry (material slot) sub-items in the hierarchy; mirror
            // that here so clicking a slot selects just that geometry instead of the whole model.
            // The engine reports the hit material slot in subObject_ for any model, including a posed
            // AnimatedModel. Only a real triangle hit carries a slot index; anything else selects a node.
            const auto [model, geometryIndex] = QuerySelectedGeometry(scene, scenePage.cameraRay_);
            if (model)
            {
                scenePage.selection_.SetGeometrySelected(model, geometryIndex);
            }
            else
            {
                Node* selectedNode = QuerySelectedNode(scene, scenePage.cameraRay_);
                SelectNode(scenePage.selection_, selectedNode, toggle, append);
            }
        }
    }
}

Drawable* SceneSelector::QuerySelectedDrawable(Scene* scene, const Ray& cameraRay, RayQueryLevel level) const
{
    const auto results = QueryGeometriesFromScene(scene, cameraRay);

    for (const RayQueryResult& result : results)
    {
        if (result.drawable_->GetScene() != nullptr)
            return result.drawable_;
    }

    return nullptr;
}

Node* SceneSelector::QuerySelectedNode(Scene* scene, const Ray& cameraRay) const
{
    Drawable* selectedDrawable = QuerySelectedDrawable(scene, cameraRay, RAY_TRIANGLE);
    if (!selectedDrawable)
        selectedDrawable = QuerySelectedDrawable(scene, cameraRay, RAY_OBB);

    Node* selectedNode = selectedDrawable ? selectedDrawable->GetNode() : nullptr;

    while (selectedNode && selectedNode->IsTemporary())
        selectedNode = selectedNode->GetParent();

    return selectedNode;
}

ea::pair<StaticModel*, unsigned> SceneSelector::QuerySelectedGeometry(Scene* scene, const Ray& cameraRay) const
{
    // Results are front-to-back. The engine guarantees that a RAY_TRIANGLE hit on a model reports the
    // material-slot (geometry) index in subObject_, consistently for StaticModel and a posed
    // AnimatedModel; a hit that is not a model surface reports M_MAX_UNSIGNED.
    const auto results = QueryGeometriesFromScene(scene, cameraRay, RAY_TRIANGLE);
    for (const RayQueryResult& result : results)
    {
        Drawable* drawable = result.drawable_;
        if (!drawable || drawable->GetScene() == nullptr)
            continue;

        auto* model = dynamic_cast<StaticModel*>(drawable);
        if (model && !model->GetNode()->IsTemporary() && result.subObject_ < model->GetNumGeometries())
            return { model, result.subObject_ };

        // The front-most hit resolved to no geometry slot: select it as a node instead.
        return { nullptr, M_MAX_UNSIGNED };
    }
    return { nullptr, M_MAX_UNSIGNED };
}

void SceneSelector::SelectNode(SceneSelection& selection, Node* node, bool toggle, bool append) const
{
    selection.ConvertToNodes();

    if (toggle)
        selection.SetSelected(node, !selection.IsSelected(node));
    else if (append)
        selection.SetSelected(node, true);
    else
    {
        selection.Clear();
        selection.SetSelected(node, true);
    }
}

}

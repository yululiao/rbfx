//
// Copyright (c) 2022-2022 the rbfx project.
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
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//

#include "../SystemUI/AnimationInspectorWidget.h"

#include "../SystemUI/AnimationPreviewHelpers.h"
#include "../SystemUI/DragDropPayload.h"
#include "../SystemUI/SceneWidget.h"
#include "../SystemUI/SystemUI.h"

#include <Urho3D/Graphics/AnimatedModel.h>
#include <Urho3D/Graphics/Animation.h>
#include <Urho3D/Graphics/AnimationController.h>
#include <Urho3D/Graphics/Model.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Node.h>
#include <Urho3D/Scene/Scene.h>

namespace Urho3D
{

namespace
{

/// Height of the animation preview viewport.
constexpr float PreviewHeight = 250.0f;

} // namespace

const ea::vector<ResourceInspectorWidget::PropertyDesc> AnimationInspectorWidget::properties{
    {
        "Length",
        Variant{0.0f},
        [](const Resource* resource) { return Variant{static_cast<const Animation*>(resource)->GetLength()}; },
        [](Resource* resource, const Variant& value) { static_cast<Animation*>(resource)->SetLength(value.GetFloat()); },
        "Length in seconds",
    },
};

AnimationInspectorWidget::AnimationInspectorWidget(Context* context, const ResourceVector& resources)
    : BaseClassName(context, resources, ea::span(properties.begin(), properties.end()))
{
    // Preview is only available for a single animation
    if (resources.size() != 1)
        return;

    preview_ = MakeShared<SceneWidget>(context);
    auto scene = preview_->CreateDefaultScene();
    auto modelNode = scene->CreateChild("Model");
    animatedModel_ = modelNode->CreateComponent<AnimatedModel>();
    animatedModel_->SetCastShadows(true);
    animationController_ = modelNode->CreateComponent<AnimationController>();

    // Bind model automatically, e.g. model imported from the same fbx as the animation
    SetModel(FindModelForAnimation(context, GetAnimation()));
}

AnimationInspectorWidget::~AnimationInspectorWidget()
{
}

Animation* AnimationInspectorWidget::GetAnimation() const
{
    const auto& resources = GetResources();
    return resources.size() == 1 ? dynamic_cast<Animation*>(resources[0].Get()) : nullptr;
}

void AnimationInspectorWidget::SetModel(Model* model)
{
    model_ = model;

    if (animatedModel_)
        animatedModel_->SetModel(model_);

    if (!model_)
    {
        if (animationController_)
            animationController_->StopAll();
        return;
    }

    ApplyDefaultMaterials(animatedModel_);
    preview_->LookAt(model_->GetBoundingBox());
    RestartAnimation();
}

void AnimationInspectorWidget::RestartAnimation()
{
    auto* animation = GetAnimation();
    if (!animationController_ || !animation || !model_)
        return;

    auto params = AnimationParameters{animation}.Layer(0);
    params.looped_ = looped_;
    params.speed_ = paused_ ? 0.0f : 1.0f;
    animationController_->StopAll();
    animationController_->PlayNewExclusive(params);
}

void AnimationInspectorWidget::RenderContent()
{
    if (preview_ && GetAnimation())
    {
        const IdScopeGuard guard("AnimationPreview");
        if (ui::CollapsingHeader("Preview", ImGuiTreeNodeFlags_DefaultOpen))
        {
            RenderModelRef();
            RenderPlaybackControls();
            RenderPreviewViewport();
            ui::Separator();
        }
    }

    BaseClassName::RenderContent();
}

void AnimationInspectorWidget::RenderModelRef()
{
    static const StringVector allowedModelTypes{
        Model::GetTypeNameStatic(),
    };

    StringHash modelType = Model::GetTypeStatic();
    ea::string modelName = model_ ? model_->GetName() : "";

    Widgets::ItemLabel("Model", Widgets::GetItemLabelColor(false, !model_));
    if (Widgets::EditResourceRef(modelType, modelName, &allowedModelTypes))
    {
        // Resource can also be bound by dragging it onto the reference field
        auto* cache = context_->GetSubsystem<ResourceCache>();
        SetModel(cache->GetResource<Model>(modelName));
    }
}

void AnimationInspectorWidget::RenderPlaybackControls()
{
    if (ui::Button(paused_ ? "Play" : "Pause"))
    {
        paused_ = !paused_;
        if (auto* animation = GetAnimation())
            animationController_->UpdateAnimationSpeed(animation, paused_ ? 0.0f : 1.0f);
    }

    ui::SameLine();
    if (ui::Button("Restart"))
        RestartAnimation();

    ui::SameLine();
    if (ui::Checkbox("Looped", &looped_))
        RestartAnimation();
}

void AnimationInspectorWidget::RenderPreviewViewport()
{
    const ImVec2 previewSize{ui::GetContentRegionAvail().x, PreviewHeight};
    if (ui::BeginChild("animation_preview", previewSize))
        preview_->RenderContent();
    ui::EndChild();

    // Bind model by dragging it onto the preview viewport
    if (!ui::BeginDragDropTarget())
        return;

    const auto* payload = dynamic_cast<const ResourceDragDropPayload*>(DragDropPayload::Get());
    if (payload && payload->resources_.size() == 1 && payload->resources_[0].HasObjectType<Model>())
    {
        if (ui::AcceptDragDropPayload(DragDropPayloadType.c_str()))
        {
            auto* cache = context_->GetSubsystem<ResourceCache>();
            SetModel(cache->GetResource<Model>(payload->resources_[0].resourceName_));
        }
    }
    ui::EndDragDropTarget();
}

} // namespace Urho3D

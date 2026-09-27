// Copyright (c) 2022-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#include <Urho3D/Precompiled.h>

#include <Urho3D/Core/ObjectCategory.h>
#include <Urho3D/Graphics/AnimationController.h>
#include <Urho3D/IO/ArchiveSerialization.h>
#include <Urho3D/Resource/Resource.h>
#include <Urho3D/Scene/Node.h>
#include <Urho3D/Timeline/TimelineAnimationTrack.h>
#include <Urho3D/Timeline/TimelinePlayer.h>

namespace Urho3D
{

namespace
{

/// Find animation instance with given animation and instance index.
unsigned FindControllerInstance(AnimationController& controller, Animation* animation, unsigned instanceIndex)
{
    for (unsigned i = 0; i < controller.GetNumAnimations(); ++i)
    {
        const AnimationParameters& params = controller.GetAnimationParameters(i);
        if (params.GetAnimation() == animation && params.instanceIndex_ == instanceIndex)
            return i;
    }
    return M_MAX_UNSIGNED;
}

}

void TimelineAnimationClip::SerializeInBlock(Archive& archive)
{
    TimelineClip::SerializeInBlock(archive);

    if (!archive.IsInput() && animation_)
        animationRef_ = GetResourceRef(animation_, Animation::GetTypeStatic());

    SerializeResource(archive, "animation", animation_, animationRef_);
    SerializeOptionalValue(archive, "additive", additive_, false);
    SerializeOptionalValue(archive, "layer", layer_, 0u);
    SerializeOptionalValue(archive, "startBone", startBone_);
}

TimelineAnimationTrack::TimelineAnimationTrack(Context* context) :
    TimelineTrack(context)
{
}

TimelineAnimationTrack::~TimelineAnimationTrack() = default;

void TimelineAnimationTrack::RegisterObject(Context* context)
{
    context->AddFactoryReflection<TimelineAnimationTrack>(Category_Timeline);
}

void TimelineAnimationTrack::SerializeInBlock(Archive& archive)
{
    TimelineTrack::SerializeInBlock(archive);
    SerializeVectorAsObjects(archive, "clips", clips_, "clip");
}

float TimelineAnimationTrack::GetContentDuration() const
{
    float duration = 0.0f;
    for (const TimelineAnimationClip& clip : clips_)
        duration = Max(duration, clip.GetEndTime());
    return duration;
}

ea::unique_ptr<TimelineTrackRuntime> TimelineAnimationTrack::CreateRuntime() const
{
    return ea::make_unique<Runtime>();
}

AnimationController* TimelineAnimationTrack::ResolveController(TimelineTrackContext& context) const
{
    Runtime& runtime = static_cast<Runtime&>(context.runtime_);

    AnimationController* controller = runtime.controller_;
    if (!controller || context.runtime_.bindingDirty_)
    {
        Object* bound = context.player_.ResolveTrackBinding(*this, context.runtime_);
        if (auto* asController = dynamic_cast<AnimationController*>(bound))
            controller = asController;
        else if (auto* asNode = dynamic_cast<Node*>(bound))
            controller = asNode->GetComponent<AnimationController>();
        else
            controller = nullptr;
    }
    return controller;
}

void TimelineAnimationTrack::ReleaseInstances(TimelinePlayer& player, Runtime& runtime)
{
    AnimationController* controller = runtime.controller_;
    for (Runtime::ClipState& state : runtime.clips_)
    {
        if (!state.active_)
            continue;

        if (controller && state.controllerIndex_ < controller->GetNumAnimations())
            player.RemoveAnimationInstance(*controller, state.controllerIndex_);

        state = {};
    }
}

void TimelineAnimationTrack::OnTimelineStopped(TimelinePlayer& player, TimelineTrackRuntime& runtime, bool finished)
{
    ReleaseInstances(player, static_cast<Runtime&>(runtime));
}

void TimelineAnimationTrack::OnTimelinePaused(TimelinePlayer& player, TimelineTrackRuntime& runtime, bool paused)
{
    if (!paused)
        return;

    Runtime& animRuntime = static_cast<Runtime&>(runtime);
    AnimationController* controller = animRuntime.controller_;
    if (!controller)
        return;

    for (const Runtime::ClipState& state : animRuntime.clips_)
    {
        if (!state.active_ || state.controllerIndex_ >= controller->GetNumAnimations())
            continue;

        AnimationParameters params = controller->GetAnimationParameters(state.controllerIndex_);
        params.speed_ = 0.0f;
        controller->UpdateAnimation(state.controllerIndex_, params);
    }
}

void TimelineAnimationTrack::Evaluate(TimelineTrackContext& context)
{
    Runtime& runtime = static_cast<Runtime&>(context.runtime_);
    runtime.clips_.resize(clips_.size());

    AnimationController* controller = ResolveController(context);

    if (controller != runtime.controller_.Get())
    {
        // Binding changed: release instances owned in the previous controller.
        ReleaseInstances(context.player_, runtime);
        runtime.controller_ = controller;
    }

    if (!controller)
        return;

    if (IsInactive(context))
    {
        ReleaseInstances(context.player_, runtime);
        return;
    }

    const float sceneTimeStep = context.player_.GetSceneTimeStep();

    for (unsigned i = 0; i < clips_.size(); ++i)
    {
        const TimelineAnimationClip& clip = clips_[i];
        Runtime::ClipState& state = runtime.clips_[i];

        float localTime = 0.0f;
        const bool active = clip.animation_ && clip.EvaluateLocalTime(context.time_, localTime);
        const float weight = active ? clip.EvaluateWeight(context.time_) * weight_ * context.weightScale_ : 0.0f;

        if (!active || weight <= M_EPSILON)
        {
            if (state.active_)
            {
                context.player_.RemoveAnimationInstance(*controller, state.controllerIndex_);
                state = {};
            }
            continue;
        }

        Animation* animation = clip.animation_;

        // Source time range consumed by the clip.
        const float rangeMin = clip.clipIn_;
        float rangeMax = clip.clipIn_ + Abs(clip.duration_) * Abs(clip.timeScale_);
        rangeMax = Min(rangeMax, animation->GetLength());
        if (rangeMax <= rangeMin + M_EPSILON)
            rangeMax = rangeMin + M_EPSILON;

        const float desiredTime = Clamp(clip.GetSourceTime(localTime), rangeMin, rangeMax);
        // While paused (or scrubbing), freeze the pose at the exact time.
        const float effectiveSpeed = context.playing_ ? clip.timeScale_ : 0.0f;

        AnimationParameters params = AnimationParameters(animation)
            .TimeRange(rangeMin, rangeMax)
            .Speed(effectiveSpeed)
            .Weight(weight)
            .Layer(clip.layer_)
            .Additive(clip.additive_);
        if (!clip.startBone_.empty())
            params.StartBone(clip.startBone_);

        params.looped_ = false;
        params.removeOnCompletion_ = false;
        params.removeOnZeroWeight_ = false;

        // The controller advances instance time by (scene time step * speed) before committing the pose.
        // Pre-compensate so that the committed time matches the desired clip time exactly.
        const float preCompensatedTime = desiredTime - sceneTimeStep * effectiveSpeed;
        if (preCompensatedTime < rangeMin || preCompensatedTime > rangeMax)
        {
            // Crossing the source range boundary: drive the exact time without controller advance.
            params.SetTime(desiredTime);
            params.speed_ = 0.0f;
        }
        else
            params.SetTime(preCompensatedTime);

        if (!state.active_)
        {
            controller->AddAnimation(params);
            const unsigned index = controller->GetNumAnimations() - 1;
            state.controllerIndex_ = index;
            state.instanceIndex_ = controller->GetAnimationParameters(index).instanceIndex_;
            state.active_ = true;
        }
        else
        {
            unsigned index = state.controllerIndex_;
            if (index >= controller->GetNumAnimations() ||
                controller->GetAnimationParameters(index).GetAnimation() != animation)
            {
                index = FindControllerInstance(*controller, animation, state.instanceIndex_);
            }

            if (index == M_MAX_UNSIGNED)
            {
                controller->AddAnimation(params);
                const unsigned newIndex = controller->GetNumAnimations() - 1;
                state.controllerIndex_ = newIndex;
                state.instanceIndex_ = controller->GetAnimationParameters(newIndex).instanceIndex_;
            }
            else
            {
                state.controllerIndex_ = index;
                controller->UpdateAnimation(index, params);
            }
        }
    }
}

}

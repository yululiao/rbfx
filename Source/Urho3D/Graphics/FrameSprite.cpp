// Copyright (c) 2026 the rbfx project.
// Licensed under the MIT license.
#include "../Precompiled.h"
#include "FrameSprite.h"
#include "BillboardSet.h"
#include "FlipbookBaker.h"
#include "Material.h"
#include "Texture2D.h"
#include "../Core/Context.h"
#include "../Core/Timer.h"
#include "../Resource/ResourceCache.h"
#include "../Scene/Scene.h"
#include "../Scene/SceneEvents.h"
#include <cmath>
#include <EASTL/optional.h>

namespace Urho3D
{
SharedPtr<FrameSpriteClip> FrameSpriteClip::FromAtlas(FlipbookBaker* atlas, unsigned clip, bool looped)
{
    if (!atlas || !atlas->GetTexture() || clip >= atlas->GetNumClips()
        || !atlas->GetClipFrameCount(clip) || !std::isfinite(atlas->GetFps()) || atlas->GetFps() <= 0)
        return {};
    auto result = MakeShared<FrameSpriteClip>();
    result->fps_ = atlas->GetFps();
    result->looped_ = looped;
    for (unsigned i = 0; i < atlas->GetClipFrameCount(clip); ++i)
        result->frames_.push_back({SharedPtr<Texture2D>(atlas->GetTexture()), atlas->GetClipCellUV(clip, i, 0)});
    return result;
}

SharedPtr<FrameSpriteClip> FrameSpriteClip::FromImages(Context* context,
    const ea::vector<ea::string>& paths, float fps, bool looped)
{
    auto cache = context->GetSubsystem<ResourceCache>();
    if (!cache || paths.empty() || !std::isfinite(fps) || fps <= 0)
        return {};
    auto result = MakeShared<FrameSpriteClip>();
    result->fps_ = fps;
    result->looped_ = looped;
    for (const auto& path : paths)
    {
        auto texture = cache->GetResource<Texture2D>(path);
        if (!texture)
            return {};
        result->frames_.push_back({SharedPtr<Texture2D>(texture), Rect(0, 0, 1, 1)});
    }
    return result;
}

SharedPtr<FrameSpriteClip> FrameSpriteClip::FromGrid(Texture2D* texture, unsigned columns, unsigned rows,
    unsigned first, unsigned count, float fps, bool looped)
{
    const unsigned long long cells = static_cast<unsigned long long>(columns) * rows;
    if (!texture || !columns || !rows || !count || first >= cells || count > cells - first
        || !std::isfinite(fps) || fps <= 0)
        return {};
    auto result = MakeShared<FrameSpriteClip>();
    result->fps_ = fps;
    result->looped_ = looped;
    for (unsigned i = 0; i < count; ++i)
    {
        const auto cell = static_cast<unsigned long long>(first) + i;
        const float x = static_cast<float>(cell % columns), y = static_cast<float>(cell / columns);
        result->frames_.push_back({SharedPtr<Texture2D>(texture),
            Rect(x / columns, y / rows, (x + 1) / columns, (y + 1) / rows)});
    }
    return result;
}

FrameSprite::FrameSprite(Context* context) : Component(context) { SetTemporary(true); }
FrameSprite::~FrameSprite() = default;
void FrameSprite::RegisterObject(Context* context) { context->RegisterFactory<FrameSprite>(); }
void FrameSprite::SetMaterial(Material* material) { material_ = material; }
Material* FrameSprite::GetMaterial() const { return material_; }

bool FrameSprite::SetAtlasAnimation(FlipbookBaker* atlas, unsigned clip, bool looped)
{ return SetAnimation(FrameSpriteClip::FromAtlas(atlas, clip, looped)); }
bool FrameSprite::SetImageAnimation(const ea::vector<ea::string>& paths, float fps, bool looped)
{ return SetAnimation(FrameSpriteClip::FromImages(context_, paths, fps, looped)); }
bool FrameSprite::SetGridAnimation(Texture2D* texture, unsigned columns, unsigned rows,
    unsigned first, unsigned count, float fps, bool looped)
{ return SetAnimation(FrameSpriteClip::FromGrid(texture, columns, rows, first, count, fps, looped)); }

bool FrameSprite::SetAnimation(FrameSpriteClip* clip)
{
    if (!clip || clip->frames_.empty() || !std::isfinite(clip->fps_) || clip->fps_ <= 0)
        return false;
    clip_ = clip;
    Play();
    return true;
}
void FrameSprite::Play()
{
    time_ = 0;
    frame_ = 0;
    finished_ = false;
    playing_ = clip_ != nullptr;
}
void FrameSprite::Stop() { Play(); playing_ = false; }
void FrameSprite::SetSpeed(float speed) { if (std::isfinite(speed) && speed >= 0) speed_ = speed; }

void FrameSprite::Update(float timeStep)
{
    if (!playing_ || !clip_ || !std::isfinite(timeStep) || timeStep <= 0)
        return;
    const double duration = clip_->frames_.size() / static_cast<double>(clip_->fps_);
    time_ += static_cast<double>(timeStep) * speed_;
    if (clip_->looped_)
        time_ = std::fmod(time_, duration);
    else if (time_ >= duration)
    {
        time_ = duration;
        finished_ = true;
        playing_ = false;
    }
    frame_ = static_cast<unsigned>(ea::min(std::floor(time_ * clip_->fps_),
        static_cast<double>(clip_->frames_.size() - 1)));
}

void FrameSprite::OnSceneSet(Scene* previousScene, Scene* scene)
{
    if (batch_)
        batch_->RemoveSprite(this);
    batch_.Reset();
    if (scene)
    {
        batch_ = scene->GetOrCreateComponent<FrameSpriteBatch>();
        batch_->SetTemporary(true);
        batch_->AddSprite(this);
    }
}

FrameSpriteAnimator::FrameSpriteAnimator(Context* context) : Object(context) {}
void FrameSpriteAnimator::RegisterObject(Context* context) { context->RegisterFactory<FrameSpriteAnimator>(); }
void FrameSpriteAnimator::SetSprite(FrameSprite* sprite) { sprite_ = sprite; animationName_.clear(); }
FrameSprite* FrameSpriteAnimator::GetSprite() const { return sprite_; }
bool FrameSpriteAnimator::Add(const ea::string& name, SharedPtr<FrameSpriteClip> clip)
{
    if (name.empty() || !clip)
        return false;
    animations_[name] = clip;
    return true;
}
bool FrameSpriteAnimator::AddAtlasAnimation(const ea::string& name, FlipbookBaker* atlas, unsigned clip, bool looped)
{ return Add(name, FrameSpriteClip::FromAtlas(atlas, clip, looped)); }
bool FrameSpriteAnimator::AddImageAnimation(const ea::string& name, const ea::vector<ea::string>& paths, float fps, bool looped)
{ return Add(name, FrameSpriteClip::FromImages(context_, paths, fps, looped)); }
bool FrameSpriteAnimator::AddGridAnimation(const ea::string& name, Texture2D* texture, unsigned columns, unsigned rows,
    unsigned first, unsigned count, float fps, bool looped)
{ return Add(name, FrameSpriteClip::FromGrid(texture, columns, rows, first, count, fps, looped)); }
void FrameSpriteAnimator::CopyAnimations(FrameSpriteAnimator* source)
{ if (source) animations_ = source->animations_; }
bool FrameSpriteAnimator::PlayAni(const ea::string& name)
{
    const auto it = animations_.find(name);
    if (!sprite_ || it == animations_.end() || !sprite_->SetAnimation(it->second))
        return false;
    animationName_ = name;
    return true;
}

struct FrameSpriteBatch::Group : public RefCounted
{
    SharedPtr<Material> source_;
    SharedPtr<Texture2D> texture_;
    WeakPtr<BillboardSet> set_;
    ea::vector<FrameSprite*> visible_;
    unsigned previousCount_{};
};

FrameSpriteBatch::FrameSpriteBatch(Context* context) : Component(context) { SetTemporary(true); }
FrameSpriteBatch::~FrameSpriteBatch() = default;
void FrameSpriteBatch::RegisterObject(Context* context) { context->RegisterFactory<FrameSpriteBatch>(); }
void FrameSpriteBatch::OnSceneSet(Scene* previousScene, Scene* scene)
{
    UnsubscribeFromAllEvents();
    if (scene)
        SubscribeToEvent(scene, E_SCENEPOSTUPDATE, URHO3D_HANDLER(FrameSpriteBatch, HandleScenePostUpdate));
}
void FrameSpriteBatch::AddSprite(FrameSprite* sprite)
{
    for (const auto& existing : sprites_)
        if (existing == sprite) return;
    sprites_.push_back(WeakPtr<FrameSprite>(sprite));
}
void FrameSpriteBatch::RemoveSprite(FrameSprite* sprite)
{
    ea::erase_if(sprites_, [sprite](const WeakPtr<FrameSprite>& value) { return !value || value == sprite; });
}
FrameSpriteBatch::Group& FrameSpriteBatch::GetGroup(Material* material, Texture2D* texture)
{
    for (const auto& group : groups_)
        if (group->source_ == material && group->texture_ == texture && group->set_)
            return *group;
    auto group = MakeShared<Group>();
    group->source_ = material;
    group->texture_ = texture;
    auto set = GetNode()->CreateComponent<BillboardSet>();
    set->SetTemporary(true);
    set->SetRelative(false);
    set->SetScaled(false);
    set->SetCastShadows(false);
    auto clonedMaterial = material->Clone();
    clonedMaterial->SetTexture("Albedo", texture);
    set->SetMaterial(clonedMaterial);
    group->set_ = set;
    groups_.push_back(group);
    return *group;
}
void FrameSpriteBatch::HandleScenePostUpdate(StringHash, VariantMap& eventData)
{
    if (autoUpdate_ && IsEnabledEffective())
        Update(eventData[ScenePostUpdate::P_TIMESTEP].GetFloat());
}
void FrameSpriteBatch::Update(float timeStep)
{
    if (!GetScene()) return;
    ea::optional<HiresTimer> timer;
    if (statisticsEnabled_) timer.emplace();
    for (const auto& group : groups_) group->visible_.clear();
    ea::erase_if(sprites_, [this](const WeakPtr<FrameSprite>& value)
        { return !value || value->GetScene() != GetScene(); });
    numSprites_ = 0;
    for (const auto& weak : sprites_)
    {
        FrameSprite* sprite = weak;
        if (!sprite->IsEnabledEffective()) continue;
        const unsigned oldFrame = sprite->frame_;
        sprite->Update(timeStep);
        if (statisticsEnabled_ && oldFrame != sprite->frame_) ++frameChanges_;
        if (!sprite->clip_ || !sprite->material_) continue;
        auto texture = sprite->clip_->frames_[sprite->frame_].texture_.Get();
        if (!texture) continue;
        GetGroup(sprite->material_, texture).visible_.push_back(sprite);
        ++numSprites_;
    }
    numBatches_ = 0;
    for (const auto& group : groups_)
    {
        auto set = group->set_.Get();
        if (!set) continue;
        const unsigned count = group->visible_.size();
        if (count) ++numBatches_;
        bool dirty = false;
        if (count > set->GetNumBillboards())
        {
            set->SetNumBillboards(count);
            dirty = true;
        }
        for (unsigned i = 0; i < count; ++i)
        {
            const auto sprite = group->visible_[i];
            auto b = set->GetBillboard(i);
            const Vector3 position = sprite->GetNode()->GetWorldPosition() + sprite->offset_;
            const Rect& uv = sprite->clip_->frames_[sprite->frame_].uv_;
            if (!b->enabled_ || b->position_ != position || b->uv_ != uv
                || b->size_ != sprite->size_ || b->color_ != sprite->color_)
            {
                if (b->position_ != position) b->position_ = position;
                if (b->uv_ != uv) b->uv_ = uv;
                if (b->size_ != sprite->size_) b->size_ = sprite->size_;
                if (b->color_ != sprite->color_) b->color_ = sprite->color_;
                if (!b->enabled_) b->enabled_ = true;
                dirty = true;
            }
        }
        for (unsigned i = count; i < group->previousCount_; ++i)
        {
            set->GetBillboard(i)->enabled_ = false;
            dirty = true;
        }
        group->previousCount_ = count;
        if (dirty) set->Commit();
    }
    if (timer)
    {
        const double elapsed = timer->GetUSec(false) / 1000.0;
        ++updateCount_;
        totalUpdateMs_ += elapsed;
        maxUpdateMs_ = ea::max(maxUpdateMs_, elapsed);
    }
}
void FrameSpriteBatch::SetStatisticsEnabled(bool enabled)
{
    if (statisticsEnabled_ == enabled) return;
    statisticsEnabled_ = enabled;
    ResetStatistics();
}
void FrameSpriteBatch::ResetStatistics()
{
    updateCount_ = frameChanges_ = 0;
    totalUpdateMs_ = maxUpdateMs_ = 0;
}
}

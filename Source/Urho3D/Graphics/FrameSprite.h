// Copyright (c) 2026 the rbfx project.
// Licensed under the MIT license.
#pragma once

#include "../Scene/Component.h"
#include "../Math/Color.h"
#include "../Math/Rect.h"
#include <EASTL/unordered_map.h>

namespace Urho3D
{
class BillboardSet;
class FlipbookBaker;
class Material;
class Texture2D;
class FrameSpriteBatch;

/// Immutable frame data shared by players. Textures are loaded before playback.
struct URHO3D_API FrameSpriteClip : public RefCounted
{
    struct Frame
    {
        SharedPtr<Texture2D> texture_;
        Rect uv_;
    };
    ea::vector<Frame> frames_;
    float fps_{15.0f};
    bool looped_{true};

    static SharedPtr<FrameSpriteClip> FromAtlas(FlipbookBaker* atlas, unsigned clip, bool looped);
    static SharedPtr<FrameSpriteClip> FromImages(Context* context, const ea::vector<ea::string>& paths, float fps, bool looped);
    static SharedPtr<FrameSpriteClip> FromGrid(Texture2D* texture, unsigned columns, unsigned rows,
        unsigned first, unsigned count, float fps, bool looped);
};

/// Plays one frame animation on a camera-facing sprite. A scene-owned batch updates
/// all instances after gameplay and shares a BillboardSet per material/texture pair.
/// Runtime component: clip data is configured through the API, not serialized.
class URHO3D_API FrameSprite : public Component
{
    URHO3D_OBJECT(FrameSprite, Component);
public:
    explicit FrameSprite(Context* context);
    ~FrameSprite() override;
    static void RegisterObject(Context* context);

    bool SetAtlasAnimation(FlipbookBaker* atlas, unsigned clip, bool looped);
    bool SetImageAnimation(const ea::vector<ea::string>& paths, float fps, bool looped);
    bool SetGridAnimation(Texture2D* texture, unsigned columns, unsigned rows,
        unsigned first, unsigned count, float fps, bool looped);
    /// Bind shared immutable data and restart at frame zero. Null/empty data is rejected.
    bool SetAnimation(FrameSpriteClip* clip);
    void Play();
    void Pause() { playing_ = false; }
    void Resume() { if (clip_ && !finished_) playing_ = true; }
    void Stop();
    void SetSpeed(float speed);
    float GetSpeed() const { return speed_; }
    bool IsPlaying() const { return playing_; }
    bool IsFinished() const { return finished_; }
    unsigned GetFrame() const { return frame_; }
    /// Manual advance for previews/tests. Scene instances are advanced automatically.
    void Update(float timeStep);

    void SetMaterial(Material* material);
    Material* GetMaterial() const;
    void SetSize(const Vector2& size) { size_ = size; }
    const Vector2& GetSize() const { return size_; }
    /// World-space offset from the node, independent of its rotation/scale.
    void SetOffset(const Vector3& offset) { offset_ = offset; }
    const Vector3& GetOffset() const { return offset_; }
    void SetColor(const Color& color) { color_ = color; }
    const Color& GetColor() const { return color_; }

protected:
    void OnSceneSet(Scene* previousScene, Scene* scene) override;

private:
    friend class FrameSpriteBatch;
    SharedPtr<FrameSpriteClip> clip_;
    SharedPtr<Material> material_;
    WeakPtr<FrameSpriteBatch> batch_;
    Vector2 size_{1.0f, 1.0f};
    Vector3 offset_;
    Color color_{Color::WHITE};
    double time_{};
    float speed_{1.0f};
    unsigned frame_{};
    bool playing_{};
    bool finished_{};
};

/// Names and switches animations on a FrameSprite; it does not advance time.
/// Separate from the engine AnimationController, which drives Animation tracks.
class URHO3D_API FrameSpriteAnimator : public Object
{
    URHO3D_OBJECT(FrameSpriteAnimator, Object);
public:
    explicit FrameSpriteAnimator(Context* context);
    static void RegisterObject(Context* context);
    void SetSprite(FrameSprite* sprite);
    FrameSprite* GetSprite() const;
    bool AddAtlasAnimation(const ea::string& name, FlipbookBaker* atlas, unsigned clip, bool looped);
    bool AddImageAnimation(const ea::string& name, const ea::vector<ea::string>& paths, float fps, bool looped);
    bool AddGridAnimation(const ea::string& name, Texture2D* texture, unsigned columns, unsigned rows,
        unsigned first, unsigned count, float fps, bool looped);
    /// Copy names while sharing immutable frame/texture data (no per-character atlas copies).
    void CopyAnimations(FrameSpriteAnimator* source);
    /// Always restarts the requested clip. Missing names leave the current clip unchanged.
    bool PlayAni(const ea::string& name);
    const ea::string& GetAnimationName() const { return animationName_; }
private:
    bool Add(const ea::string& name, SharedPtr<FrameSpriteClip> clip);
    WeakPtr<FrameSprite> sprite_;
    ea::unordered_map<ea::string, SharedPtr<FrameSpriteClip>> animations_;
    ea::string animationName_;
};

/// Scene-owned rendering implementation. Gameplay uses FrameSprite/FrameSpriteAnimator.
class URHO3D_API FrameSpriteBatch : public Component
{
    URHO3D_OBJECT(FrameSpriteBatch, Component);
public:
    explicit FrameSpriteBatch(Context* context);
    ~FrameSpriteBatch() override;
    static void RegisterObject(Context* context);
    void AddSprite(FrameSprite* sprite);
    void RemoveSprite(FrameSprite* sprite);
    /// Default: follow ScenePostUpdate. Disable when gameplay supplies a scaled
    /// timestep and calls Update once after movement (e.g. hit-stop in FireGame).
    void SetAutoUpdate(bool enabled) { autoUpdate_ = enabled; }
    bool GetAutoUpdate() const { return autoUpdate_; }
    /// Opt-in timing and cumulative counters. Live sprite/batch counts remain available.
    void SetStatisticsEnabled(bool enabled);
    bool GetStatisticsEnabled() const { return statisticsEnabled_; }
    void Update(float timeStep);
    unsigned GetNumSprites() const { return numSprites_; }
    unsigned GetNumBatches() const { return numBatches_; }
    unsigned GetUpdateCount() const { return updateCount_; }
    double GetTotalUpdateMs() const { return totalUpdateMs_; }
    double GetMaxUpdateMs() const { return maxUpdateMs_; }
    unsigned GetFrameChanges() const { return frameChanges_; }
    void ResetStatistics();
protected:
    void OnSceneSet(Scene* previousScene, Scene* scene) override;
private:
    struct Group;
    Group& GetGroup(Material* material, Texture2D* texture);
    void HandleScenePostUpdate(StringHash eventType, VariantMap& eventData);
    ea::vector<WeakPtr<FrameSprite>> sprites_;
    ea::vector<SharedPtr<Group>> groups_;
    unsigned numSprites_{}, numBatches_{}, updateCount_{}, frameChanges_{};
    double totalUpdateMs_{}, maxUpdateMs_{};
    bool autoUpdate_{true};
    bool statisticsEnabled_{};
};
}

// Copyright (c) 2026 the rbfx project. Licensed under the MIT license.
#include "../CommonUtils.h"
#include <Urho3D/Graphics/FrameSprite.h>
#include <Urho3D/Graphics/BillboardSet.h>
#include <Urho3D/Graphics/Material.h>
#include <Urho3D/Graphics/Texture2D.h>
#include <Urho3D/Graphics/Octree.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>

TEST_CASE("FrameSprite plays, pauses, loops and clamps frame clips")
{
    auto context = Tests::GetOrCreateContext(Tests::CreateCompleteContext);
    auto texture = MakeShared<Texture2D>(context);
    auto sprite = MakeShared<FrameSprite>(context);
    REQUIRE(sprite->SetGridAnimation(texture, 4, 1, 0, 4, 4, true));
    sprite->Update(0.25f);
    CHECK(sprite->GetFrame() == 1);
    sprite->Pause();
    sprite->Update(1.0f);
    CHECK(sprite->GetFrame() == 1);
    sprite->Resume();
    sprite->Update(0.75f);
    CHECK(sprite->GetFrame() == 0);
    sprite->SetSpeed(2);
    sprite->Update(0.25f);
    CHECK(sprite->GetFrame() == 2);
    REQUIRE(sprite->SetGridAnimation(texture, 4, 1, 0, 4, 4, false));
    sprite->Update(10);
    CHECK(sprite->GetFrame() == 3);
    CHECK(sprite->IsFinished());
    CHECK_FALSE(sprite->IsPlaying());
    sprite->Play();
    CHECK(sprite->GetFrame() == 0);
    CHECK_FALSE(sprite->IsFinished());
    CHECK_FALSE(sprite->SetGridAnimation(texture, 0, 1, 0, 1, 4, true));
    CHECK_FALSE(sprite->SetGridAnimation(texture, 4, 1, 3, 2, 4, true));
    CHECK_FALSE(sprite->SetGridAnimation(texture, 4, 1, 0, 4, 0, true));
    CHECK(sprite->IsPlaying()); // Invalid replacement preserves playback.
}

TEST_CASE("FrameSpriteAnimator shares clips and switches named animations")
{
    auto context = Tests::GetOrCreateContext(Tests::CreateCompleteContext);
    auto texture = MakeShared<Texture2D>(context);
    auto source = MakeShared<FrameSpriteAnimator>(context);
    REQUIRE(source->AddGridAnimation("run", texture, 4, 1, 0, 4, 4, true));
    REQUIRE(source->AddGridAnimation("death", texture, 4, 1, 2, 2, 4, false));
    auto sprite = MakeShared<FrameSprite>(context);
    auto animator = MakeShared<FrameSpriteAnimator>(context);
    animator->CopyAnimations(source);
    source.Reset();
    animator->SetSprite(sprite);
    REQUIRE(animator->PlayAni("run"));
    sprite->Update(0.5f);
    CHECK(sprite->GetFrame() == 2);
    CHECK_FALSE(animator->PlayAni("missing"));
    CHECK(sprite->GetFrame() == 2);
    REQUIRE(animator->PlayAni("death"));
    CHECK(sprite->GetFrame() == 0);
    sprite->Update(1);
    CHECK(sprite->IsFinished());
    sprite.Reset();
    CHECK_FALSE(animator->PlayAni("run")); // Weak player reference.
}

TEST_CASE("FrameSprite batches positions, texture changes, removal and node transforms")
{
    auto context = Tests::GetOrCreateContext(Tests::CreateCompleteContext);
    auto scene = MakeShared<Scene>(context);
    scene->CreateComponent<Octree>();
    auto material = MakeShared<Material>(context);
    auto texture = MakeShared<Texture2D>(context);
    auto otherTexture = MakeShared<Texture2D>(context);
    auto cache = context->GetSubsystem<ResourceCache>();
    texture->SetName("FrameSpriteTest/first.png");
    otherTexture->SetName("FrameSpriteTest/second.png");
    cache->AddManualResource(texture);
    cache->AddManualResource(otherTexture);
    auto parent = scene->CreateChild();
    parent->SetPosition(Vector3(10, 0, 0));
    auto a = parent->CreateChild()->CreateComponent<FrameSprite>();
    auto b = parent->CreateChild()->CreateComponent<FrameSprite>();
    a->SetMaterial(material);
    b->SetMaterial(material);
    a->SetOffset(Vector3(0, 2, 0));
    REQUIRE(a->SetGridAnimation(texture, 4, 1, 0, 4, 4, true));
    REQUIRE(b->SetGridAnimation(texture, 4, 1, 0, 4, 4, true));
    auto batch = scene->GetComponent<FrameSpriteBatch>();
    REQUIRE(batch);
    batch->Update(0);
    CHECK(batch->GetNumSprites() == 2);
    CHECK(batch->GetNumBatches() == 1);
    auto set = scene->GetComponent<BillboardSet>();
    REQUIRE(set);
    CHECK(set->GetBillboard(0)->position_ == Vector3(10, 2, 0));
    const Rect firstUV = set->GetBillboard(0)->uv_;
    parent->SetPosition(Vector3(20, 0, 0));
    batch->Update(0.01f);
    CHECK(set->GetBillboard(0)->position_ == Vector3(20, 2, 0));
    CHECK(set->GetBillboard(0)->uv_ == firstUV);
    a->SetEnabled(false);
    batch->Update(0);
    CHECK(batch->GetNumSprites() == 1);
    CHECK_FALSE(set->GetBillboard(1)->enabled_);
    a->SetEnabled(true);
    b->GetNode()->Remove();
    batch->Update(0);
    CHECK(batch->GetNumSprites() == 1);
    REQUIRE(a->SetImageAnimation({texture->GetName(), otherTexture->GetName()}, 4, true));
    batch->Update(0.25f);
    CHECK(a->GetFrame() == 1);
    CHECK(batch->GetNumBatches() == 1);
    CHECK_FALSE(set->GetBillboard(0)->enabled_); // Old texture group was disabled.
    scene->RemoveAllChildren();
    batch->Update(0);
    CHECK(batch->GetNumSprites() == 0);
    CHECK(batch->GetNumBatches() == 0);
}

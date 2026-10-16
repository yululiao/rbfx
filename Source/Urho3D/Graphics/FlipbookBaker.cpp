//
// Copyright (c) 2026 the rbfx project.
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

#include "../Precompiled.h"

#include "../Graphics/FlipbookBaker.h"

#include "../Core/Context.h"
#include "../Core/CoreEvents.h"
#include "../Core/ProcessUtils.h"
#include "../Core/StringUtils.h"
#include "../Graphics/AnimatedModel.h"
#include "../Graphics/Animation.h"
#include "../Graphics/AnimationTrack.h"
#include "../Graphics/Camera.h"
#include "../Graphics/GraphicsEvents.h"
#include "../Graphics/Light.h"
#include "../Graphics/Material.h"
#include "../Graphics/Model.h"
#include "../Graphics/Octree.h"
#include "../Graphics/RenderSurface.h"
#include "../Graphics/Skeleton.h"
#include "../Graphics/Texture2D.h"
#include "../Graphics/Viewport.h"
#include "../Graphics/Zone.h"
#include "../IO/File.h"
#include "../IO/FileSystem.h"
#include "../IO/Log.h"
#include "../Math/Transform.h"
#include "../Resource/Image.h"
#include "../Resource/ResourceCache.h"
#include "../Scene/Node.h"
#include "../Scene/Scene.h"

#include "../DebugNew.h"

#include <cstring>

namespace Urho3D
{

namespace
{

/// Archive magic for the ".flipbook.meta" clip-table sidecar.
constexpr char kFlipbookMagic[4] = { 'F', 'L', 'B', 'K' };
/// Archive format version. 2 = pixels live in the sibling ".flipbook.png" (lossless, ~20x smaller
/// than raw RGBA8); the meta only carries the layout / clip table.
constexpr unsigned kFlipbookVersion = 2;

} // namespace

FlipbookBaker::FlipbookBaker(Context* context)
    : Object(context)
{
}

FlipbookBaker::~FlipbookBaker()
{
    DestroyBakeScene();
}

void FlipbookBaker::RegisterObject(Context* context)
{
    context->AddFactoryReflection<FlipbookBaker>();
}

void FlipbookBaker::Reset()
{
    fps_ = 15.0f;
    cellSize_ = 256;
    rotations_ = 1;
    yawRad_ = 0.0f;
    ClearBakeState();
    outputPath_.clear();
    bakeMaterial_.Reset();
}

void FlipbookBaker::ClearBakeState()
{
    DestroyBakeScene();
    columns_ = rows_ = 1;
    atlasWidth_ = atlasHeight_ = totalCells_ = 0;
    clips_.clear();
    atlasTexels_.clear();
    texture_.Reset();
    animations_.clear();
    running_ = finished_ = succeeded_ = false;
    currentCell_ = 0;
    bakeModel_.Reset();
}

float FlipbookBaker::GetProgress() const
{
    if (!totalCells_)
        return running_ ? 0.0f : 1.0f;
    return static_cast<float>(Min(currentCell_, totalCells_)) / static_cast<float>(totalCells_);
}

bool FlipbookBaker::PlanAtlas(Model* model, const ea::vector<Animation*>& animations, const FlipbookBakeParams& params)
{
    ea::string& error = lastError_;

    fps_ = params.fps_ > 0.0f ? params.fps_ : 15.0f;
    cellSize_ = params.cellSize_ ? params.cellSize_ : 256;
    rotations_ = params.rotations_ ? params.rotations_ : 1;
    yawRad_ = params.yaw_ * (2.0f * M_PI) / 360.0f;

    // Assign consecutive cell ranges to the clips (frames * rotations each) at the requested fps.
    unsigned total = 0;
    for (const Animation* animation : animations)
    {
        if (!animation)
        {
            error = "FlipbookBaker: null animation clip";
            return false;
        }
        const float duration = animation->GetLength();
        if (duration <= 0.0f)
        {
            error = Format("FlipbookBaker: animation '{}' has a non-positive duration", animation->GetName());
            return false;
        }

        BakedFlipbookClip clip;
        clip.name_ = GetFileNameAndExtension(animation->GetName());
        if (clip.name_.ends_with(".ani"))
            clip.name_.resize(clip.name_.size() - 4);
        clip.startCell_ = total;
        clip.frameCount_ = static_cast<unsigned>(Max(1.0f, RoundToInt(duration * fps_)));
        clip.duration_ = duration;
        clips_.push_back(clip);
        total += clip.frameCount_ * rotations_;
    }

    totalCells_ = total;

    // Pick a near-square grid, clamped so the atlas width stays within the device limit.
    const unsigned maxColumns = Max(1u, params.maxAtlasWidth_ / cellSize_);
    unsigned ideal = static_cast<unsigned>(RoundToInt(Sqrt(static_cast<float>(Max(1u, totalCells_)))));
    columns_ = Clamp(ideal, 1u, maxColumns);
    if (columns_ * cellSize_ > params.maxAtlasWidth_)
        columns_ = maxColumns;
    rows_ = (totalCells_ + columns_ - 1) / columns_;
    if (rows_ == 0)
        rows_ = 1;
    atlasWidth_ = columns_ * cellSize_;
    atlasHeight_ = rows_ * cellSize_;

    atlasTexels_.assign(static_cast<size_t>(atlasWidth_) * atlasHeight_ * 4, 0);
    return true;
}

void FlipbookBaker::DecomposeCell(unsigned cell, unsigned& outClip, unsigned& outFrame, unsigned& outRot) const
{
    outClip = 0;
    outFrame = 0;
    outRot = 0;
    for (unsigned c = 0; c < clips_.size(); ++c)
    {
        const BakedFlipbookClip& clip = clips_[c];
        const unsigned span = clip.frameCount_ * rotations_;
        if (cell >= clip.startCell_ && cell < clip.startCell_ + span)
        {
            const unsigned rem = cell - clip.startCell_;
            outClip = c;
            outFrame = rem / rotations_;
            outRot = rem % rotations_;
            return;
        }
    }
}

bool FlipbookBaker::BuildBakeScene(Model* model)
{
    scene_ = MakeShared<Scene>(context_);
    scene_->CreateComponent<Octree>();

    // Zone: bright ambient so the lit model is not pitch black, and a fully transparent fog color
    // because the forward pipeline clears the render target with the effective (fog) color - this is
    // what yields a transparent flipbook background around the model silhouette.
    Node* zoneNode = scene_->CreateChild("Zone");
    Zone* zone = zoneNode->CreateComponent<Zone>();
    zone->SetBoundingBox(BoundingBox(-1000.0f, 1000.0f));
    zone->SetAmbientColor(Color(0.9f, 0.9f, 0.9f));
    zone->SetFogColor(Color::TRANSPARENT_BLACK);
    zone->SetFogStart(2000.0f);
    zone->SetFogEnd(3000.0f);

    // A single directional light to give the frames some shading.
    Node* lightNode = scene_->CreateChild("Light");
    lightNode->SetDirection(Vector3(0.4f, -1.0f, 0.5f));
    Light* light = lightNode->CreateComponent<Light>();
    light->SetLightType(LIGHT_DIRECTIONAL);
    light->SetCastShadows(false);
    light->SetColor(Color(0.7f, 0.7f, 0.7f));

    // Model to render. The bake yaw is applied to the model itself (not the camera): models whose
    // front faces -Z (e.g. Quaternius FBX convention) bake with yaw 180 so the facing view shows the
    // face instead of the back.
    modelNode_ = scene_->CreateChild("BakeModel");
    modelNode_->SetRotation(Quaternion(yawRad_ * (180.0f / M_PI), Vector3::UP));
    animatedModel_ = modelNode_->CreateComponent<AnimatedModel>();
    animatedModel_->SetModel(model);
    if (bakeMaterial_)
        animatedModel_->SetMaterial(bakeMaterial_);
    animatedModel_->SetCastShadows(false);

    // Offscreen render target (square cell) + depth for correct self-occlusion.
    rtTexture_ = MakeShared<Texture2D>(context_);
    rtTexture_->SetNumLevels(1);
    rtTexture_->SetSize(static_cast<int>(cellSize_), static_cast<int>(cellSize_), TextureFormat::TEX_FORMAT_RGBA8_UNORM,
        TextureFlag::BindRenderTarget);
    depthTexture_ = MakeShared<Texture2D>(context_);
    depthTexture_->SetNumLevels(1);
    depthTexture_->SetSize(static_cast<int>(cellSize_), static_cast<int>(cellSize_),
        TextureFormat::TEX_FORMAT_D24_UNORM_S8_UINT, TextureFlag::BindDepthStencil);

    // Orthographic camera. Framing is computed below from the union of every baked pose.
    cameraNode_ = scene_->CreateChild("BakeCamera");
    camera_ = cameraNode_->CreateComponent<Camera>();
    camera_->SetOrthographic(true);
    camera_->SetAspectRatio(1.0f);

    // Frame the camera on the union of all animation poses, not just the bind pose: clips with a
    // large range of motion (death falls, overhead axe swings) would otherwise overflow their cell
    // and bleed into neighbouring cells of the atlas.
    ComputeBakeFraming(model);
    camera_->SetOrthoSize(bakeRadius_ * 1.15f);
    camera_->SetNearClip(Max(0.01f, bakeRadius_));
    camera_->SetFarClip(bakeRadius_ * 8.0f + 1.0f);

    viewport_ = MakeShared<Viewport>(context_, scene_, camera_);

    RenderSurface* surface = rtTexture_->GetRenderSurface();
    if (!surface)
    {
        lastError_ = "FlipbookBaker: failed to acquire the render target surface";
        return false;
    }
    surface->SetViewport(0, viewport_);
    surface->SetLinkedDepthStencil(depthTexture_->GetRenderSurface());
    surface->SetUpdateMode(SURFACE_MANUALUPDATE);

    cellImage_ = MakeShared<Image>(context_);
    return true;
}

void FlipbookBaker::ComputeBakeFraming(Model* model)
{
    // Start from the bind-pose box (in world space, i.e. through the model node's yaw rotation).
    BoundingBox framing = model->GetBoundingBox().Transformed(modelNode_->GetWorldTransform());

    Skeleton& skeleton = animatedModel_->GetSkeleton();
    const ea::vector<Bone>& bones = skeleton.GetBones();
    for (unsigned clipIndex = 0; clipIndex < clips_.size() && clipIndex < animations_.size(); ++clipIndex)
    {
        const BakedFlipbookClip& clip = clips_[clipIndex];
        for (unsigned frame = 0; frame < clip.frameCount_; ++frame)
        {
            const float time = Min(static_cast<float>(frame) / fps_, clip.duration_ - M_EPSILON);
            PoseSkeleton(clipIndex, time);
            for (const Bone& bone : bones)
            {
                // Per-bone box of that bone's skinned vertices (bone space, filled by the FBX
                // importer); the same skinning transform the GPU path applies puts it in world.
                if (!bone.node_ || !bone.boundingBox_.Defined())
                    continue;
                const Matrix3x4 skin = bone.node_->GetWorldTransform() * bone.offsetMatrix_;
                framing.Merge(bone.boundingBox_.Transformed(skin));
            }
        }
    }

    bakeCenter_ = framing.Center();
    bakeRadius_ = Max(0.25f, framing.Size().Length() * 0.5f);
}

void FlipbookBaker::PoseSkeleton(unsigned clipIndex, float time)
{
    const BakedFlipbookClip& clip = clips_[clipIndex];
    const Animation* animation = animations_[clipIndex];

    // Pose the skeleton for this frame by writing each bone's local transform straight from the
    // clip tracks (bind pose for non-animated bones). Moving the bone nodes auto-dirties skinning.
    Skeleton& skeleton = animatedModel_->GetSkeleton();
    const ea::vector<Bone>& bones = skeleton.GetBones();
    const auto& tracks = animation->GetTracks();
    for (unsigned boneIndex : skeleton.GetBonesOrder())
    {
        const Bone& bone = bones[boneIndex];
        Transform local{ bone.initialPosition_, bone.initialRotation_, bone.initialScale_ };
        if (bone.animated_)
        {
            const auto trackIt = tracks.find(bone.nameHash_);
            if (trackIt != tracks.end() && !trackIt->second.IsEmpty())
            {
                unsigned dummyFrame = 0;
                trackIt->second.Sample(time, clip.duration_, true, dummyFrame, local);
            }
        }
        if (Node* node = bone.node_)
        {
            node->SetPosition(local.position_);
            node->SetRotation(local.rotation_);
            node->SetScale(local.scale_);
        }
    }
}

void FlipbookBaker::DestroyBakeScene()
{
    viewport_.Reset();
    rtTexture_.Reset();
    depthTexture_.Reset();
    cellImage_.Reset();
    camera_ = nullptr;
    cameraNode_.Reset();
    animatedModel_ = nullptr;
    modelNode_.Reset();
    scene_.Reset();
}

bool FlipbookBaker::Start(Model* model, const ea::vector<Animation*>& animations, const FlipbookBakeParams& params)
{
    ea::string& error = lastError_;
    // Keep the externally configured inputs (bake material, output path) from the earlier setters.
    ClearBakeState();
    error.clear();

    if (!model)
    {
        error = "FlipbookBaker: model is null";
        return false;
    }
    if (animations.empty())
    {
        error = "FlipbookBaker: no animation clips given";
        return false;
    }
    if (model->GetSkeleton().GetNumBones() == 0)
    {
        error = "FlipbookBaker: model is not skinned (no bones)";
        return false;
    }

    if (!PlanAtlas(model, animations, params))
    {
        ClearBakeState();
        return false;
    }

    // Keep the clips alive for the duration of the asynchronous bake.
    animations_.assign(animations.begin(), animations.end());
    bakeModel_ = model;

    if (!BuildBakeScene(model))
    {
        ClearBakeState();
        return false;
    }

    currentCell_ = 0;
    running_ = true;
    finished_ = false;
    succeeded_ = false;

    // The bake advances one cell per engine frame: configure the cell before the render, read the
    // render target back after it. Prepared / consumed below are driven by these two events.
    SubscribeToEvent(E_UPDATE, [this](StringHash, VariantMap&) {
        if (running_)
            PrepareCurrentCell();
    });
    SubscribeToEvent(E_ENDRENDERING, [this](StringHash, VariantMap&) {
        if (running_)
            ConsumeCurrentCell();
    });

    URHO3D_LOGINFO("FlipbookBaker: baking {} clip(s) into a {}x{} atlas (cell {}px, {} rotation(s), {} fps)",
        clips_.size(), atlasWidth_, atlasHeight_, cellSize_, rotations_, fps_);
    return true;
}

void FlipbookBaker::PrepareCurrentCell()
{
    unsigned clipIndex = 0;
    unsigned frame = 0;
    unsigned rot = 0;
    DecomposeCell(currentCell_, clipIndex, frame, rot);
    if (clipIndex >= clips_.size() || clipIndex >= animations_.size())
        return;

    const BakedFlipbookClip& clip = clips_[clipIndex];
    const float time = Min(static_cast<float>(frame) / fps_, clip.duration_ - M_EPSILON);
    PoseSkeleton(clipIndex, time);

    // Aim the orthographic camera for this facing direction (the model carries the yaw; the camera
    // only orbits the rot/rotations step).
    const float angle = (static_cast<float>(rot) / static_cast<float>(rotations_)) * (2.0f * M_PI);
    const Vector3 dir(Sin(angle), 0.0f, Cos(angle));
    const float dist = bakeRadius_ * 3.0f + 1.0f;
    const Vector3 pos = bakeCenter_ + dir * dist;
    cameraNode_->SetPosition(pos);
    cameraNode_->SetDirection((bakeCenter_ - pos).Normalized());

    if (RenderSurface* surface = rtTexture_->GetRenderSurface())
        surface->QueueUpdate();
}

void FlipbookBaker::ConsumeCurrentCell()
{
    if (!rtTexture_->GetImage(*cellImage_))
    {
        lastError_ = "FlipbookBaker: failed to read back the render target";
        running_ = false;
        finished_ = true;
        succeeded_ = false;
        UnsubscribeFromEvent(E_UPDATE);
        UnsubscribeFromEvent(E_ENDRENDERING);
        DestroyBakeScene();
        return;
    }

    unsigned clipIndex = 0;
    unsigned frame = 0;
    unsigned rot = 0;
    DecomposeCell(currentCell_, clipIndex, frame, rot);
    const unsigned col = currentCell_ % columns_;
    const unsigned row = currentCell_ / columns_;

    const int cell = static_cast<int>(cellSize_);
    const int srcW = cellImage_->GetWidth();
    const int srcH = cellImage_->GetHeight();
    const unsigned char* src = cellImage_->GetData();
    unsigned char* dst = atlasTexels_.data();
    const int copyW = Min(cell, srcW);
    const int copyH = Min(cell, srcH);
    for (int y = 0; y < copyH; ++y)
    {
        const unsigned char* s = src + (static_cast<size_t>(y) * srcW) * 4;
        unsigned char* d = dst + (static_cast<size_t>(row * cell + y) * atlasWidth_ + col * cell) * 4;
        memcpy(d, s, static_cast<size_t>(copyW) * 4);
    }

    ++currentCell_;
    if (currentCell_ >= totalCells_)
        FinishBake();
}

void FlipbookBaker::FinishBake()
{
    UnsubscribeFromEvent(E_UPDATE);
    UnsubscribeFromEvent(E_ENDRENDERING);
    running_ = false;
    DestroyBakeScene();

    texture_ = MakeShared<Texture2D>(context_);
    texture_->SetName(Format("{}_Flipbook", bakeModel_ ? bakeModel_->GetName() : ea::string("Flipbook")));
    texture_->SetNumLevels(1);
    texture_->SetFilterMode(FILTER_NEAREST);
    texture_->SetAddressMode(TextureCoordinate::U, ADDRESS_CLAMP);
    texture_->SetAddressMode(TextureCoordinate::V, ADDRESS_CLAMP);
    if (!texture_->SetSize(static_cast<int>(atlasWidth_), static_cast<int>(atlasHeight_),
            TextureFormat::TEX_FORMAT_RGBA8_UNORM))
    {
        lastError_ = "FlipbookBaker: failed to allocate the atlas texture";
        finished_ = true;
        succeeded_ = false;
        return;
    }
    texture_->SetData(0, 0, 0, static_cast<int>(atlasWidth_), static_cast<int>(atlasHeight_), atlasTexels_.data());

    succeeded_ = true;
    finished_ = true;

    if (!outputPath_.empty())
    {
        ea::string error;
        if (!Save(outputPath_, error))
        {
            lastError_ = error;
            succeeded_ = false;
            URHO3D_LOGERROR("FlipbookBaker: bake finished but saving failed: {}", error);
            return;
        }
        URHO3D_LOGINFO("FlipbookBaker: baked {} clip(s), {} cell(s), atlas {}x{} -> '{}'", clips_.size(),
            totalCells_, atlasWidth_, atlasHeight_, outputPath_);
    }
}

ea::string FlipbookBaker::GetClipName(unsigned clipIndex) const
{
    return clipIndex < clips_.size() ? clips_[clipIndex].name_ : ea::string();
}

unsigned FlipbookBaker::GetClipFrameCount(unsigned clipIndex) const
{
    return clipIndex < clips_.size() ? clips_[clipIndex].frameCount_ : 0;
}

float FlipbookBaker::GetClipDuration(unsigned clipIndex) const
{
    return clipIndex < clips_.size() ? clips_[clipIndex].duration_ : 0.0f;
}

Rect FlipbookBaker::GetClipCellUV(unsigned clipIndex, unsigned frameIndex, unsigned rotIndex) const
{
    if (clipIndex >= clips_.size() || !atlasWidth_ || !atlasHeight_ || !cellSize_)
        return Rect(0.0f, 0.0f, 0.0f, 0.0f);

    const BakedFlipbookClip& clip = clips_[clipIndex];
    frameIndex = Min(frameIndex, Max(0u, clip.frameCount_ - 1u));
    rotIndex = Min(rotIndex, rotations_ - 1u);

    const unsigned cell = clip.startCell_ + frameIndex * rotations_ + rotIndex;
    const unsigned col = cell % columns_;
    const unsigned row = cell / columns_;

    const Vector2 min(static_cast<float>(col * cellSize_) / static_cast<float>(atlasWidth_),
        static_cast<float>(row * cellSize_) / static_cast<float>(atlasHeight_));
    const Vector2 max(static_cast<float>((col + 1) * cellSize_) / static_cast<float>(atlasWidth_),
        static_cast<float>((row + 1) * cellSize_) / static_cast<float>(atlasHeight_));
    return Rect(min, max);
}

bool FlipbookBaker::Save(const ea::string& path, ea::string& error) const
{
    error.clear();
    if (clips_.empty() || atlasTexels_.empty() || !atlasWidth_ || !atlasHeight_)
    {
        error = "FlipbookBaker::Save: nothing baked to save";
        return false;
    }

    SharedPtr<File> file = MakeShared<File>(context_);
    if (!file->Open(path + ".meta", FILE_WRITE))
    {
        error = "FlipbookBaker::Save: cannot open " + path + ".meta";
        return false;
    }

    file->Write(kFlipbookMagic, 4);
    const unsigned version = kFlipbookVersion;
    file->Write(&version, sizeof(version));
    file->Write(&fps_, sizeof(fps_));
    file->Write(&cellSize_, sizeof(cellSize_));
    file->Write(&rotations_, sizeof(rotations_));
    file->Write(&columns_, sizeof(columns_));
    file->Write(&rows_, sizeof(rows_));
    file->Write(&atlasWidth_, sizeof(atlasWidth_));
    file->Write(&atlasHeight_, sizeof(atlasHeight_));
    const unsigned clipCount = static_cast<unsigned>(clips_.size());
    file->Write(&clipCount, sizeof(clipCount));

    for (const BakedFlipbookClip& clip : clips_)
    {
        file->WriteString(clip.name_);
        file->Write(&clip.startCell_, sizeof(clip.startCell_));
        file->Write(&clip.frameCount_, sizeof(clip.frameCount_));
        file->Write(&clip.duration_, sizeof(clip.duration_));
    }
    file->Close();

    // Pixel source: a lossless PNG next to the meta. Saved as part of the same output so the two
    // files can never disagree; also serves as the eyeball preview.
    {
        Image preview(context_);
        preview.SetSize(static_cast<int>(atlasWidth_), static_cast<int>(atlasHeight_), 4);
        memcpy(preview.GetData(), atlasTexels_.data(), atlasTexels_.size());
        if (!preview.SavePNG(path + ".png"))
        {
            error = "FlipbookBaker::Save: cannot write " + path + ".png";
            return false;
        }
    }
    return true;
}

bool FlipbookBaker::Save(const ea::string& path)
{
    ea::string error;
    if (!Save(path, error))
    {
        lastError_ = error;
        return false;
    }
    return true;
}

bool FlipbookBaker::Load(const ea::string& path, ea::string& error)
{
    error.clear();
    Reset();

    // The ".flipbook" name is a base, not a file: layout comes from "<path>.meta", pixels from the
    // lossless "<path>.png".
    ea::string metaPath = path + ".meta";
    ea::string pngPath = path + ".png";

    // Open through the resource system so both the editor/desktop loose layout and the packaged
    // layout resolve: ResourceCache::GetFile searches the mounted resource paths and the in-package
    // manifest, which maps the engine-visible "...flipbook.meta"/"...flipbook.png" names back onto
    // the vendor-renamed physical files (e.g. ".meta.dat"). A raw filesystem File::Open only sees the
    // CWD and misses everything inside the package, so it silently fails on the minigame build.
    auto* cache = GetSubsystem<ResourceCache>();
    AbstractFilePtr file = cache ? cache->GetFile(metaPath) : nullptr;
    if (!file || !file->IsOpen())
    {
        // Fall back to a direct filesystem open, so an absolute path outside the resource tree
        // (as passed by the desktop baking tool) still loads.
        SharedPtr<File> direct = MakeShared<File>(context_);
        file = direct->Open(metaPath, FILE_READ) ? AbstractFilePtr(direct) : nullptr;
    }
    if (!file)
    {
        error = "FlipbookBaker::Load: cannot open " + metaPath;
        return false;
    }

    char magic[4] = {};
    file->Read(magic, 4);
    if (memcmp(magic, kFlipbookMagic, 4) != 0)
    {
        error = "FlipbookBaker::Load: bad archive magic";
        return false;
    }
    unsigned version = 0;
    file->Read(&version, sizeof(version));
    if (version != kFlipbookVersion)
    {
        error = Format("FlipbookBaker::Load: unsupported archive version {}", version);
        return false;
    }

    file->Read(&fps_, sizeof(fps_));
    file->Read(&cellSize_, sizeof(cellSize_));
    file->Read(&rotations_, sizeof(rotations_));
    file->Read(&columns_, sizeof(columns_));
    file->Read(&rows_, sizeof(rows_));
    file->Read(&atlasWidth_, sizeof(atlasWidth_));
    file->Read(&atlasHeight_, sizeof(atlasHeight_));
    unsigned clipCount = 0;
    file->Read(&clipCount, sizeof(clipCount));

    clips_.resize(clipCount);
    for (unsigned i = 0; i < clipCount; ++i)
    {
        BakedFlipbookClip& clip = clips_[i];
        clip.name_ = file->ReadString();
        file->Read(&clip.startCell_, sizeof(clip.startCell_));
        file->Read(&clip.frameCount_, sizeof(clip.frameCount_));
        file->Read(&clip.duration_, sizeof(clip.duration_));
    }

    totalCells_ = 0;
    for (const BakedFlipbookClip& clip : clips_)
        totalCells_ = Max(totalCells_, clip.startCell_ + clip.frameCount_ * rotations_);

    // Decode the PNG pixel source, opened the same package-aware way as the meta.
    AbstractFilePtr pngFile = cache ? cache->GetFile(pngPath) : nullptr;
    if (!pngFile || !pngFile->IsOpen())
    {
        SharedPtr<File> direct = MakeShared<File>(context_);
        pngFile = direct->Open(pngPath, FILE_READ) ? AbstractFilePtr(direct) : nullptr;
    }
    if (!pngFile)
    {
        error = "FlipbookBaker::Load: cannot open " + pngPath;
        return false;
    }
    SharedPtr<Image> image = MakeShared<Image>(context_);
    if (!image->BeginLoad(*pngFile))
    {
        error = "FlipbookBaker::Load: failed to decode " + pngPath;
        return false;
    }

    if (static_cast<unsigned>(image->GetWidth()) != atlasWidth_ ||
        static_cast<unsigned>(image->GetHeight()) != atlasHeight_)
    {
        error = Format("FlipbookBaker::Load: PNG size {}x{} does not match meta {}x{}",
            image->GetWidth(), image->GetHeight(), atlasWidth_, atlasHeight_);
        return false;
    }

    texture_ = MakeShared<Texture2D>(context_);
    texture_->SetName("FlipbookAtlas");
    texture_->SetNumLevels(1);
    texture_->SetFilterMode(FILTER_NEAREST);
    texture_->SetAddressMode(TextureCoordinate::U, ADDRESS_CLAMP);
    texture_->SetAddressMode(TextureCoordinate::V, ADDRESS_CLAMP);
    if (!texture_->SetData(image))
    {
        error = "FlipbookBaker::Load: failed to upload the atlas texture";
        return false;
    }
    return true;
}

bool FlipbookBaker::Load(const ea::string& path)
{
    ea::string error;
    if (!Load(path, error))
    {
        lastError_ = error;
        return false;
    }
    return true;
}

}

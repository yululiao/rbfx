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
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//

#include "../Precompiled.h"

#include "../Graphics/BoneMatrixBaker.h"

#include "../Core/Context.h"
#include "../Core/StringUtils.h"
#include "../IO/File.h"
#include "../IO/FileSystem.h"
#include "../IO/Log.h"
#include "../Resource/ResourceCache.h"

#include "../Graphics/Animation.h"
#include "../Graphics/AnimationTrack.h"
#include "../Graphics/Model.h"
#include "../Graphics/Skeleton.h"
#include "../Graphics/Texture2D.h"

#include "../Math/Transform.h"

#include "../DebugNew.h"

#include <cstring>

namespace Urho3D
{

namespace
{

/// Maximum atlas width in texels accepted by the baker: numBones * 3 must stay within this limit.
constexpr unsigned kMaxAtlasWidth = 16384;

} // namespace

BoneMatrixBaker::BoneMatrixBaker(Context* context)
    : Object(context)
{
}

BoneMatrixBaker::~BoneMatrixBaker() = default;

void BoneMatrixBaker::RegisterObject(Context* context)
{
    context->AddFactoryReflection<BoneMatrixBaker>();
}

void BoneMatrixBaker::Reset()
{
    numBones_ = 0;
    width_ = 0;
    frames_ = 0;
    clips_.clear();
    texels_.clear();
    texture_.Reset();
}

bool BoneMatrixBaker::Bake(Model* model, const ea::vector<Animation*>& animations, float fps)
{
    ea::string& error = lastError_;
    Reset();
    error.clear();

    if (!model)
    {
        error = "BoneMatrixBaker: model is null";
        return false;
    }
    if (animations.empty())
    {
        error = "BoneMatrixBaker: no animation clips given";
        return false;
    }
    if (fps <= 0.0f)
        fps = 30.0f;

    Skeleton& skeleton = model->GetSkeleton();
    const unsigned numBones = skeleton.GetNumBones();
    if (numBones == 0)
    {
        error = "BoneMatrixBaker: model is not skinned (no bones)";
        return false;
    }
    if (numBones * 3 > kMaxAtlasWidth)
    {
        error = Format("BoneMatrixBaker: {} bones exceed the atlas width limit of {} texels", numBones,
            kMaxAtlasWidth);
        return false;
    }

    const ea::vector<Bone>& bones = skeleton.GetBones();
    const ea::vector<unsigned>& boneOrder = skeleton.GetBonesOrder();

    // Plan the atlas: assign consecutive frame row ranges to the clips at the requested fps.
    unsigned totalFrames = 0;
    for (const Animation* animation : animations)
    {
        if (!animation)
        {
            error = "BoneMatrixBaker: null animation clip";
            Reset();
            return false;
        }

        const float duration = animation->GetLength();
        if (duration <= 0.0f)
        {
            error = Format("BoneMatrixBaker: animation '{}' has a non-positive duration", animation->GetName());
            Reset();
            return false;
        }

        BakedBoneClip clip;
        // Store the bare clip name (no path, no extension): it doubles as the atlas lookup key.
        clip.name_ = GetFileNameAndExtension(animation->GetName());
        if (clip.name_.ends_with(".ani"))
            clip.name_.resize(clip.name_.size() - 4);
        clip.startFrame_ = totalFrames;
        clip.frameCount_ = static_cast<unsigned>(Max(1.0f, RoundToInt(duration * fps)));
        clip.duration_ = duration;
        clips_.push_back(clip);
        totalFrames += clip.frameCount_;
    }

    numBones_ = numBones;
    width_ = numBones * 3;
    frames_ = totalFrames;
    texels_.assign(static_cast<size_t>(width_) * totalFrames * 4, 0.0f);

    // Scratch buffers reused across frames. The sampling below reconstructs the exact same matrices
    // AnimatedModel::UpdateSkinning computes at runtime, but without a scene: bone local transforms
    // start from the bind pose and are overridden by the clip tracks, then composed along the
    // hierarchy and folded with the bind-pose offset.
    ea::vector<Matrix3x4> localToComponent(numBones);
    ea::vector<Matrix3x4> skinMatrices(numBones);

    unsigned rowBase = 0;
    for (unsigned clipIndex = 0; clipIndex < animations.size(); ++clipIndex)
    {
        const Animation* animation = animations[clipIndex];
        const BakedBoneClip& clip = clips_[clipIndex];
        const auto& tracks = animation->GetTracks();

        for (unsigned f = 0; f < clip.frameCount_; ++f)
        {
            const float time = Min(f / fps, clip.duration_ - M_EPSILON);
            const size_t rowTexelOffset = static_cast<size_t>(rowBase + f) * width_ * 4;

            // Evaluate per-bone local-to-component transforms for this frame.
            for (unsigned boneIndex : boneOrder)
            {
                const Bone& bone = bones[boneIndex];
                Transform local{bone.initialPosition_, bone.initialRotation_, bone.initialScale_};

                if (bone.animated_)
                {
                    const auto trackIt = tracks.find(bone.nameHash_);
                    if (trackIt != tracks.end() && !trackIt->second.IsEmpty())
                    {
                        unsigned dummyFrame = 0;
                        trackIt->second.Sample(time, clip.duration_, true, dummyFrame, local);
                    }
                }

                const Matrix3x4 localMatrix = local.ToMatrix3x4();
                if (bone.parentIndex_ == boneIndex)
                    localToComponent[boneIndex] = localMatrix;
                else
                    localToComponent[boneIndex] = localToComponent[bone.parentIndex_] * localMatrix;
            }

            // Fold the bind-pose offset into the final skin matrix per bone.
            for (unsigned i = 0; i < numBones; ++i)
                skinMatrices[i] = localToComponent[i] * bones[i].offsetMatrix_;

            // Store the frame row: one mat3x4 (12 floats) per bone as 3 RGBA32F texels.
            for (unsigned i = 0; i < numBones; ++i)
                memcpy(&texels_[rowTexelOffset + i * 12], skinMatrices[i].Data(), 12 * sizeof(float));
        }

        rowBase += clip.frameCount_;
    }

    // Create the GPU-side copy so the atlas is immediately usable at runtime or for preview.
    texture_ = MakeShared<Texture2D>(context_);
    texture_->SetName(Format("{}_BoneTex", model->GetName()));
    texture_->SetNumLevels(1);
    texture_->SetFilterMode(FILTER_NEAREST);
    texture_->SetAddressMode(TextureCoordinate::U, ADDRESS_CLAMP);
    texture_->SetAddressMode(TextureCoordinate::V, ADDRESS_CLAMP);
    if (!texture_->SetSize(static_cast<int>(width_), static_cast<int>(frames_),
            TextureFormat::TEX_FORMAT_RGBA32_FLOAT))
    {
        error = "BoneMatrixBaker: failed to allocate the atlas texture";
        Reset();
        return false;
    }
    texture_->SetData(0, 0, 0, static_cast<int>(width_), static_cast<int>(frames_), texels_.data());

    return true;
}

bool BoneMatrixBaker::Save(const ea::string& path, ea::string& error) const
{
    error.clear();
    if (clips_.empty() || texels_.empty())
    {
        error = "BoneMatrixBaker::Save: nothing baked to save";
        return false;
    }

    SharedPtr<File> file = MakeShared<File>(context_);
    if (!file->Open(path, FILE_WRITE))
    {
        error = "BoneMatrixBaker::Save: cannot open " + path;
        return false;
    }

    const unsigned clipCount = static_cast<unsigned>(clips_.size());
    file->Write("BMTX1", 4);
    file->Write(&numBones_, sizeof(numBones_));
    file->Write(&width_, sizeof(width_));
    file->Write(&frames_, sizeof(frames_));
    file->Write(&clipCount, sizeof(clipCount));

    for (const BakedBoneClip& clip : clips_)
    {
        file->WriteString(clip.name_);
        file->Write(&clip.startFrame_, sizeof(clip.startFrame_));
        file->Write(&clip.frameCount_, sizeof(clip.frameCount_));
        file->Write(&clip.duration_, sizeof(clip.duration_));
    }

    file->Write(texels_.data(), static_cast<unsigned>(texels_.size() * sizeof(float)));
    file->Close();
    return true;
}

bool BoneMatrixBaker::Save(const ea::string& path)
{
    ea::string error;
    if (!Save(path, error))
    {
        lastError_ = error;
        return false;
    }
    return true;
}

bool BoneMatrixBaker::Load(const ea::string& path, ea::string& error)
{
    error.clear();
    Reset();

    // A relative ".bonetex" path is normally authored against the mounted resource directories
    // (same spelling as GetResource uses). A raw File::Open does not probe those virtual mount
    // points, so resolve the resource-relative name to its absolute on-disk path first; fall back
    // to the caller's string when it already is a plain filesystem path.
    ea::string resolved = path;
    if (auto* cache = GetSubsystem<ResourceCache>())
    {
        const ea::string absolute = cache->GetResourceFileName(path);
        if (!absolute.empty())
            resolved = absolute;
    }

    SharedPtr<File> file = MakeShared<File>(context_);
    if (!file->Open(resolved, FILE_READ))
    {
        error = "BoneMatrixBaker::Load: cannot open " + path;
        return false;
    }

    char magic[4] = {};
    file->Read(magic, 4);
    if (magic[0] != 'B' || magic[1] != 'M' || magic[2] != 'T' || magic[3] != 'X')
    {
        error = "BoneMatrixBaker::Load: bad archive magic";
        return false;
    }

    unsigned clipCount = 0;
    file->Read(&numBones_, sizeof(numBones_));
    file->Read(&width_, sizeof(width_));
    file->Read(&frames_, sizeof(frames_));
    file->Read(&clipCount, sizeof(clipCount));

    if (width_ == 0 || frames_ == 0 || clipCount == 0 || width_ != numBones_ * 3)
    {
        error = "BoneMatrixBaker::Load: inconsistent atlas header dimensions";
        Reset();
        return false;
    }

    clips_.reserve(clipCount);
    for (unsigned i = 0; i < clipCount; ++i)
    {
        BakedBoneClip clip;
        clip.name_ = file->ReadString();
        file->Read(&clip.startFrame_, sizeof(clip.startFrame_));
        file->Read(&clip.frameCount_, sizeof(clip.frameCount_));
        file->Read(&clip.duration_, sizeof(clip.duration_));
        clips_.push_back(eastl::move(clip));
    }

    const size_t texelCount = static_cast<size_t>(width_) * frames_ * 4;
    texels_.resize(texelCount);
    file->Read(texels_.data(), static_cast<unsigned>(texelCount * sizeof(float)));
    file->Close();

    // Rebuild the GPU atlas exactly as Bake() would, so a loaded baker is equivalent to a fresh one.
    texture_ = MakeShared<Texture2D>(context_);
    texture_->SetName(Format("{}_BoneTex", GetFileNameAndExtension(path)));
    texture_->SetNumLevels(1);
    texture_->SetFilterMode(FILTER_NEAREST);
    texture_->SetAddressMode(TextureCoordinate::U, ADDRESS_CLAMP);
    texture_->SetAddressMode(TextureCoordinate::V, ADDRESS_CLAMP);
    if (!texture_->SetSize(static_cast<int>(width_), static_cast<int>(frames_),
            TextureFormat::TEX_FORMAT_RGBA32_FLOAT))
    {
        error = "BoneMatrixBaker::Load: failed to allocate the atlas texture";
        Reset();
        return false;
    }
    if (!texture_->SetData(0, 0, 0, static_cast<int>(width_), static_cast<int>(frames_), texels_.data()))
    {
        error = "BoneMatrixBaker::Load: failed to upload the atlas texels";
        Reset();
        return false;
    }

    return true;
}

bool BoneMatrixBaker::Load(const ea::string& path)
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

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

#pragma once

#include "../Container/Ptr.h"
#include "../Container/Str.h"
#include "../Core/Object.h"

namespace Urho3D
{

class Animation;
class Context;
class Model;
class Texture2D;

/// One animation clip baked into the bone-matrix atlas: where its frame rows start and how they play.
struct BakedBoneClip
{
    /// Clip name (the animation resource name it was baked from).
    ea::string name_;
    /// Index of the first frame row of this clip inside the atlas.
    unsigned startFrame_{};
    /// Number of baked frame rows.
    unsigned frameCount_{};
    /// Clip length in seconds (frames span [startFrame, startFrame + frameCount) at the bake fps).
    float duration_{};
};

/// Offline baker that converts a skinned \p Model plus several \p Animation clips into a single
/// bone-matrix texture atlas for GPU texture-skinning instancing (the "skinned instancing" scheme of
/// GPU Gems 3, chapter 2: the skinning matrices travel through a texture instead of uniforms, so
/// instances playing different clips still batch into one draw call).
///
/// The atlas is one RGBA32F texture:
///  - width = numBones * 3 texels: the skin matrix of one bone is a mat3x4 (12 floats) stored as
///    three consecutive RGBA32F texels at texel triple boneIndex * 3;
///  - height = total frame count of all baked clips, clips stacked in row order with their metadata
///    reported by GetClips() (startFrame / frameCount / duration).
///
/// A frame row holds the exact same matrices AnimatedModel::UpdateSkinning would use at that moment
/// (bone local transforms sampled from the clip, composed along the skeleton hierarchy, folded with
/// the bind-pose offset matrix), with the model node assumed at the identity transform.
class URHO3D_API BoneMatrixBaker : public Object
{
    URHO3D_OBJECT(BoneMatrixBaker, Object);

public:
    /// Construct.
    explicit BoneMatrixBaker(Context* context);
    /// Destruct.
    ~BoneMatrixBaker() override;
    /// Register object factory.
    /// @nobind
    static void RegisterObject(Context* context);

    /// Bake the given clips into a single atlas, sampling at \p fps frames per second. Requires a
    /// skinned model. Returns false and records the reason in GetLastError() on failure.
    bool Bake(Model* model, const ea::vector<Animation*>& animations, float fps);

    /// Persist the atlas (RGBA32F texels + per-clip metadata) to a binary ".bonetex" archive.
    /// Requires a prior successful Bake().
    bool Save(const ea::string& path, ea::string& error) const;

    /// Convenience overload recording the failure reason in GetLastError().
    bool Save(const ea::string& path);

    /// Rehydrate an atlas previously written by Save(): fills the texture, clip metadata and the CPU
    /// float blob (kept so GetTexels()/a later per-bone world-matrix query work without re-baking).
    /// Returns false and records the reason in GetLastError() on failure.
    bool Load(const ea::string& path, ea::string& error);

    /// Convenience overload recording the failure reason in GetLastError().
    bool Load(const ea::string& path);

    /// Number of bones of the baked skeleton (the atlas width is numBones * 3 texels).
    /// @property
    unsigned GetNumBones() const { return numBones_; }
    /// Atlas width in texels.
    /// @property
    unsigned GetWidth() const { return width_; }
    /// Total number of baked frame rows (the atlas height).
    /// @property
    unsigned GetFrames() const { return frames_; }
    /// Baked clip metadata, in the order the clips were passed to Bake().
    const ea::vector<BakedBoneClip>& GetClips() const { return clips_; }
    /// The atlas texture (RGBA32F, FILTER_NEAREST, clamp addressing), valid after a successful Bake().
    Texture2D* GetTexture() const { return texture_; }
    /// Raw RGBA32F texels (width * frames * 4 floats), retained for persistence.
    const ea::vector<float>& GetTexels() const { return texels_; }

    /// Return the error message from the last Bake/Save call.
    const ea::string& GetLastError() const { return lastError_; }

    /// Clear all baked results.
    void Reset();

private:
    /// Bone count of the baked skeleton.
    unsigned numBones_{};
    /// Atlas width in texels (numBones_ * 3).
    unsigned width_{};
    /// Total baked frame rows (atlas height).
    unsigned frames_{};
    /// Per-clip metadata in bake order.
    ea::vector<BakedBoneClip> clips_;
    /// Raw RGBA32F atlas texels (width_ * frames_ * 4 floats).
    ea::vector<float> texels_;
    /// GPU-side copy of the atlas.
    SharedPtr<Texture2D> texture_;
    /// Error message recorded by the last Bake/Save call.
    ea::string lastError_;
};

}

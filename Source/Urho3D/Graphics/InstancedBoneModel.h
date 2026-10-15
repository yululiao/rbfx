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

#pragma once

#include "../Graphics/Drawable.h"

namespace Urho3D
{

class Geometry;
class Material;
class BoneMatrixBaker;

/// A group of GPU-instanced characters sharing one bone-matrix texture atlas, rendered as a single
/// draw call per geometry. Unlike \a InstancedVATModel (which stores pre-deformed vertex positions),
/// this keeps the original skinned geometry (per-vertex bone indices and weights) and reconstructs
/// the skinning in the vertex shader by sampling a bone-matrix atlas (BoneMatrixBaker / ".bonetex"):
/// each instance carries a world transform and a clip index + phase, and the CPU folds those into a
/// per-instance atlas frame-row index written to the trailing instancing slot. Instances playing
/// different clips of the same skin still batch together because only the sampled atlas row differs.
class URHO3D_API InstancedBoneModel : public Drawable
{
    URHO3D_OBJECT(InstancedBoneModel, Drawable);

public:
    /// Construct.
    explicit InstancedBoneModel(Context* context);
    /// Destruct.
    ~InstancedBoneModel() override;
    /// Register object factory. Drawable must be registered first.
    /// @nobind
    static void RegisterObject(Context* context);

    /// Set the source skinned geometry. Must carry bone blend indices/weights.
    void SetGeometry(Geometry* geometry);
    /// Return geometry.
    /// @property
    Geometry* GetGeometry() const { return geometry_; }

    /// Set the material. It is cloned so the atlas texture binding does not mutate the shared
    /// source material resource.
    void SetMaterial(Material* material);
    /// Return material.
    /// @property
    Material* GetMaterial() const { return material_; }

    /// Attach the baked bone-matrix atlas (texture + clip table). Binds the atlas texture into the
    /// material under the name "BoneMatrices". The baker must outlive edits (kept by shared pointer).
    void SetBoneAtlas(BoneMatrixBaker* baker);
    /// Return the attached atlas baker.
    /// @property
    BoneMatrixBaker* GetBoneAtlas() const { return baker_; }

    /// Add one instance playing clip \p animIndex. Returns its index. phase is the initial normalized
    /// time in [0, 1), playSpeed is a playback multiplier.
    unsigned AddInstance(const Matrix3x4& worldTransform, unsigned animIndex, float phase = 0.0f,
        float playSpeed = 1.0f);
    /// Add one instance from a translation/rotation/scale pose. Scripting-friendly wrapper around
    /// AddInstance, since Matrix3x4 is not exposed to script languages.
    unsigned AddInstanceTRS(const Vector3& position, const Quaternion& rotation, const Vector3& scale,
        unsigned animIndex, float phase = 0.0f, float playSpeed = 1.0f);
    /// Remove an instance by index.
    void RemoveInstance(unsigned index);
    /// Remove all instances.
    void ClearInstances();
    /// Return number of instances.
    /// @property
    unsigned GetNumInstances() const { return static_cast<unsigned>(worldTransforms_.size()); }

    /// Set an instance world transform.
    void SetInstanceTransform(unsigned index, const Matrix3x4& worldTransform);
    /// Set which clip an instance plays (index into the atlas clip table).
    void SetInstanceAnimIndex(unsigned index, unsigned animIndex);
    /// Set an instance normalized phase [0, 1) within its clip.
    void SetInstancePhase(unsigned index, float phase);
    /// Return an instance normalized phase.
    float GetInstancePhase(unsigned index) const;
    /// Set an instance playback speed multiplier.
    void SetInstancePlaySpeed(unsigned index, float playSpeed);

    /// Set the local-space bounding box used to build per-instance and world bounds (a skinned mesh
    /// bound from the source model is a good value).
    void SetLocalBoundingBox(const BoundingBox& box);

    /// Drawable overrides.
    void UpdateBatches(const FrameInfo& frame) override;
    unsigned GetNumOccluderTriangles() override;
    bool DrawOcclusion(OcclusionBuffer* buffer) override;

protected:
    /// Handle scene logic update: advance per-instance phase and refresh the atlas frame rows.
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    /// Recalculate the world-space bounding box from the instance transforms.
    void OnWorldBoundingBoxUpdate() override;
    /// Handle enabled/disabled state change (subscribe/unsubscribe update).
    void OnSetEnabled() override;

private:
    /// Ensure the batch reflects the current instances. Called on the main thread.
    void RefreshBatch();
    /// Recompute the per-instance atlas frame-row buffer from the current phase / clip table.
    void RefreshFrameRows();

    /// Per-instance animation state (clip index, normalized phase, playback multiplier).
    struct AnimState
    {
        unsigned clip_{};
        float phase_{};
        float speed_{ 1.0f };
    };

    /// Source skinned geometry shared by all instances.
    SharedPtr<Geometry> geometry_;
    /// Material shared by all instances (a clone with the atlas texture bound).
    SharedPtr<Material> material_;
    /// Baked bone-matrix atlas: owns the texture + clip table.
    SharedPtr<BoneMatrixBaker> baker_;
    /// Instance world transforms (parallel to animStates_ / frameRows_).
    ea::vector<Matrix3x4> worldTransforms_;
    /// Per-instance animation state (CPU side; drives frameRows_).
    ea::vector<AnimState> animStates_;
    /// Per-instance instancing data written to the trailing slot: x = atlas frame row, yzw = pad.
    ea::vector<Vector4> frameRows_;
};

}

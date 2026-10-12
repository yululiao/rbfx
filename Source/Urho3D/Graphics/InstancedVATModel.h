//
// Copyright (c) 2008-2022 the Urho3D project.
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

/// A group of GPU-instanced characters sharing one baked Vertex-Animation-Texture (VAT) mesh,
/// rendered as a single draw call. Each instance carries a world transform and a normalized
/// animation time in [0, 1); the vertex shader samples the VAT to reconstruct the skinned pose.
/// The geometry must be a baked static VAT geometry (GEOM_INSTANCED_VAT) whose material references
/// the VAT position/normal textures ("VatPosition" / "VatNormal").
class URHO3D_API InstancedVATModel : public Drawable
{
    URHO3D_OBJECT(InstancedVATModel, Drawable);

public:
    /// Construct.
    explicit InstancedVATModel(Context* context);
    /// Destruct.
    ~InstancedVATModel() override;
    /// Register object factory. Drawable must be registered first.
    /// @nobind
    static void RegisterObject(Context* context);

    /// Set the baked VAT geometry. Must be a static geometry intended for GEOM_INSTANCED_VAT.
    void SetGeometry(Geometry* geometry);
    /// Return geometry.
    /// @property
    Geometry* GetGeometry() const { return geometry_; }

    /// Set the VAT material (single material for all instances).
    void SetMaterial(Material* material);
    /// Return material.
    /// @property
    Material* GetMaterial() const { return material_; }

    /// Add one instance. Returns its index. animTime is normalized [0, 1), playSpeed is a playback multiplier.
    unsigned AddInstance(const Matrix3x4& worldTransform, float animTime = 0.0f, float playSpeed = 1.0f);
    /// Add one instance from a translation/rotation/scale pose. Scripting-friendly wrapper around
    /// AddInstance, since Matrix3x4 is not exposed to script languages.
    unsigned AddInstanceTRS(const Vector3& position, const Quaternion& rotation, const Vector3& scale,
        float animTime = 0.0f, float playSpeed = 1.0f);
    /// Remove an instance by index.
    void RemoveInstance(unsigned index);
    /// Remove all instances.
    void ClearInstances();
    /// Return number of instances.
    /// @property
    unsigned GetNumInstances() const { return static_cast<unsigned>(worldTransforms_.size()); }

    /// Set an instance world transform.
    void SetInstanceTransform(unsigned index, const Matrix3x4& worldTransform);
    /// Set an instance normalized animation time [0, 1).
    void SetInstanceAnimTime(unsigned index, float animTime);
    /// Return an instance normalized animation time.
    float GetInstanceAnimTime(unsigned index) const;
    /// Set an instance playback speed multiplier.
    void SetInstancePlaySpeed(unsigned index, float playSpeed);

    /// Set the baked clip duration in seconds (drives animation advance rate).
    void SetAnimationDuration(float seconds);
    /// Return the baked clip duration in seconds.
    /// @property
    float GetAnimationDuration() const { return duration_; }

    /// Set the local-space bounding box of the baked mesh (used to build per-instance and world bounds).
    void SetLocalBoundingBox(const BoundingBox& box);

    /// Drawable overrides.
    void UpdateBatches(const FrameInfo& frame) override;
    unsigned GetNumOccluderTriangles() override;
    bool DrawOcclusion(OcclusionBuffer* buffer) override;

protected:
    /// Handle scene logic update: advance per-instance animation time on the main thread.
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    /// Recalculate the world-space bounding box from the instance transforms.
    void OnWorldBoundingBoxUpdate() override;
    /// Handle enabled/disabled state change (subscribe/unsubscribe update).
    void OnSetEnabled() override;

private:
    /// Ensure the batch reflects the current instances. Called on the main thread.
    void RefreshBatch();

    /// Baked VAT geometry.
    SharedPtr<Geometry> geometry_;
    /// VAT material shared by all instances.
    SharedPtr<Material> material_;
    /// Instance world transforms (parallel to animParams_, packed for the batch).
    ea::vector<Matrix3x4> worldTransforms_;
    /// Per-instance animation params written to the trailing instancing slot: x = animTime [0,1),
    /// y = playSpeed, zw = pad. The vertex shader reads x; the CPU reads y to advance x.
    ea::vector<Vector4> animParams_;
    /// Baked clip duration in seconds.
    float duration_{ 1.0f };
};

}

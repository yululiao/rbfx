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

#include "../Container/Ptr.h"
#include "../Container/Str.h"
#include "../Core/Object.h"
#include "../Math/BoundingBox.h"

namespace Urho3D
{

class Animation;
class Context;
class Geometry;
class Material;
class Model;
class Texture2D;

/// Offline (runtime) baker that converts a skinned \p Model plus one \p Animation clip into a set of
/// Vertex-Animation-Texture (VAT) resources suitable for GPU-instanced playback via InstancedVATModel.
///
/// For each geometry (material slot) of the source model it produces:
///  - a baked static geometry that reuses the source vertex/index buffers and adds a per-vertex VAT
///    lookup UV in SEM_TEXCOORD index 2 (the "iVatUV" read by the VAT vertex shader path);
///  - a position VAT texture (RGBA32F, width = vertex count, height = frame count) holding the
///    object-space skinned position of every vertex for every baked frame;
///  - a normal VAT texture (same layout) holding the object-space skinned normal;
///  - a material cloned from the slot's source material with the two VAT textures bound under the
///    names "VatPosition" / "VatNormal".
///
/// The baker reconstructs the exact same skinning used at runtime (AnimatedModel::UpdateSkinning)
/// but with the model node assumed at the identity transform, so the VAT stores pure object-space
/// data. It is registered as an object so it can be driven from both the editor and Lua test scenes.
class URHO3D_API VATBaker : public Object
{
    URHO3D_OBJECT(VATBaker, Object);

public:
    /// Construct.
    explicit VATBaker(Context* context);
    /// Destruct.
    ~VATBaker() override;
    /// Register object factory.
    /// @nobind
    static void RegisterObject(Context* context);

    /// Bake the VAT resources from a skinned model and one animation clip. \p fps controls the frame
    /// sampling rate; \p durationSeconds overrides the clip length when positive. Returns false and
    /// records the reason in GetLastError() on failure (e.g. non-skinned input or missing CPU vertex data).
    bool Bake(Model* model, Animation* animation, float fps, float durationSeconds);

    /// Return the error message from the last Bake call.
    const ea::string& GetLastError() const { return lastError_; }

    /// Persist the baked VAT frame data (position/normal float texels + layout metadata) to a compact
    /// self-describing binary ".vat" archive at \p path. Requires a prior successful Bake().
    bool SaveVAT(const ea::string& path, ea::string& error) const;

    /// Convenience overload recording the failure reason in GetLastError().
    bool SaveVAT(const ea::string& path);

    /// Rehydrate VAT textures and rebuild baked geometry for each slot from \p sourceModel by reading a
    /// ".vat" archive written by SaveVAT. Lets the runtime skip the expensive per-frame skinning bake.
    bool LoadVAT(Model* sourceModel, const ea::string& path, ea::string& error);

    /// Convenience overload recording the failure reason in GetLastError().
    bool LoadVAT(Model* sourceModel, const ea::string& path);

    /// Return the number of baked geometry slots.
    /// @property
    unsigned GetNumSlots() const { return slotGeometries_.size(); }

    /// Return a baked geometry by slot.
    Geometry* GetGeometry(unsigned slot) const;
    /// Return the position VAT texture by slot.
    Texture2D* GetPositionTexture(unsigned slot) const;
    /// Return the normal VAT texture by slot.
    Texture2D* GetNormalTexture(unsigned slot) const;
    /// Return a cloned VAT material by slot. If \p sourceMaterial is null a default diffuse material is
    /// created. Binding is idempotent per slot.
    Material* GetMaterial(unsigned slot, Material* sourceMaterial);

    /// Return the baked frame count (VAT texture height).
    /// @property
    unsigned GetFrames() const { return frames_; }
    /// Return the baked clip duration in seconds.
    /// @property
    float GetDuration() const { return duration_; }
    /// Return the local-space bounding box covering all baked frames.
    /// @property
    const BoundingBox& GetLocalBoundingBox() const { return localBounds_; }

    /// Clear all baked results.
    void Reset();

private:
    /// Build the baked static VAT geometry for a source-model slot: reuse the source vertex/index buffers
    /// and append the per-vertex VAT lookup UV (SEM_TEXCOORD index 2) as an extra stream. Shared by
    /// Bake() (post-skinning) and LoadVAT() (texture-only rehydration).
    SharedPtr<Geometry> CreateBakedGeometry(Model* model, unsigned slot, unsigned width);

    /// Baked geometry per slot.
    ea::vector<SharedPtr<Geometry>> slotGeometries_;
    /// Baked VAT position texture per slot.
    ea::vector<SharedPtr<Texture2D>> slotPositionTextures_;
    /// Baked VAT normal texture per slot.
    ea::vector<SharedPtr<Texture2D>> slotNormalTextures_;
    /// Cached cloned VAT material per slot.
    ea::vector<SharedPtr<Material>> slotMaterials_;
    /// Raw RGBA32F VAT texel data per slot (width * frames * 4 floats) retained for persistence.
    ea::vector<ea::vector<float>> slotPositionData_;
    ea::vector<ea::vector<float>> slotNormalData_;
    /// VAT texture width (== baked vertex count) per slot.
    ea::vector<unsigned> slotWidths_;
    /// Baked frame count.
    unsigned frames_{ 0 };
    /// Baked clip duration in seconds.
    float duration_{ 0.0f };
    /// Combined local-space bounding box across all frames.
    BoundingBox localBounds_;
    /// Error message recorded by the last Bake call.
    ea::string lastError_;
};

}

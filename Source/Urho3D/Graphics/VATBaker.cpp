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

#include "../Precompiled.h"

#include "../Core/Context.h"
#include "../Core/CoreEvents.h"
#include "../Core/StringUtils.h"
#include "../IO/File.h"
#include "../IO/Log.h"
#include "../Resource/ResourceCache.h"

#include "../Graphics/Animation.h"
#include "../Graphics/AnimationTrack.h"
#include "../Graphics/Geometry.h"
#include "../Graphics/IndexBuffer.h"
#include "../Graphics/Material.h"
#include "../Graphics/Model.h"
#include "../Graphics/Skeleton.h"
#include "../Graphics/Technique.h"
#include "../Graphics/Texture2D.h"
#include "../Graphics/VATBaker.h"
#include "../Graphics/VertexBuffer.h"

#include "../Math/Transform.h"
#include "../Math/Vector4.h"

#include "../DebugNew.h"

namespace Urho3D
{

namespace
{

/// Maximum VAT texture width (== vertex count) accepted by the baker for this milestone. Larger meshes
/// would need height-chunked atlases which are intentionally out of scope here.
constexpr int kMaxVatWidth = 16384;

/// Bone influences baked per vertex (matches the UBYTE4 / VECTOR4 blend element width).
constexpr unsigned kBoneInfluences = 4;

/// Texture-space rotation of a skinned normal: apply the 3x3 part of the skin matrix only (no translation),
/// mirroring SoftwareModelAnimator::TransformNormal so the baked normals match the runtime skinning exactly.
inline Vector3 TransformSkinNormal(const Matrix3x4& m, const Vector3& v)
{
    return {
        m.m00_ * v.x_ + m.m01_ * v.y_ + m.m02_ * v.z_,
        m.m10_ * v.x_ + m.m11_ * v.y_ + m.m12_ * v.z_,
        m.m20_ * v.x_ + m.m21_ * v.y_ + m.m22_ * v.z_
    };
}

/// Locate a vertex element index within a buffer's element list by semantic. Return -1 if absent.
inline int FindElement(const ea::vector<VertexElement>& elements, VertexElementSemantic semantic, unsigned char index = 0)
{
    for (unsigned i = 0; i < elements.size(); ++i)
    {
        if (elements[i].semantic_ == semantic && elements[i].index_ == index)
            return static_cast<int>(i);
    }
    return -1;
}

/// Per-vertex bind data extracted from the source geometry (positions, normals, blend) as Vector4.
struct BindData
{
    unsigned vertexCount_ = 0;
    int positionIndex_ = -1;
    int normalIndex_ = -1;
    int blendIndexIndex_ = -1;
    int blendWeightIndex_ = -1;
    unsigned elementCount_ = 0;
    ea::vector<Vector4> unpacked_;
    BoundingBox bindBounds_;
};

} // namespace

VATBaker::VATBaker(Context* context)
    : Object(context)
{
}

VATBaker::~VATBaker() = default;

void VATBaker::RegisterObject(Context* context)
{
    context->AddFactoryReflection<VATBaker>();
}

Geometry* VATBaker::GetGeometry(unsigned slot) const
{
    return slot < slotGeometries_.size() ? slotGeometries_[slot] : nullptr;
}

Texture2D* VATBaker::GetPositionTexture(unsigned slot) const
{
    return slot < slotPositionTextures_.size() ? slotPositionTextures_[slot] : nullptr;
}

Texture2D* VATBaker::GetNormalTexture(unsigned slot) const
{
    return slot < slotNormalTextures_.size() ? slotNormalTextures_[slot] : nullptr;
}

void VATBaker::Reset()
{
    slotGeometries_.clear();
    slotPositionTextures_.clear();
    slotNormalTextures_.clear();
    slotMaterials_.clear();
    slotPositionData_.clear();
    slotNormalData_.clear();
    slotWidths_.clear();
    frames_ = 0;
    duration_ = 0.0f;
    localBounds_ = BoundingBox();
}

Material* VATBaker::GetMaterial(unsigned slot, Material* sourceMaterial)
{
    if (slot >= slotGeometries_.size())
        return nullptr;

    if (slot < slotMaterials_.size() && slotMaterials_[slot])
        return slotMaterials_[slot];

    SharedPtr<Material> material;
    if (sourceMaterial)
    {
        material = sourceMaterial->Clone();
    }
    else
    {
        // Fallback: a minimal lit material so the VAT geometry still renders when the caller has no
        // authored material for the slot.
        material = MakeShared<Material>(context_);
        Technique* tech = GetSubsystem<ResourceCache>()->GetResource<Technique>("Techniques/Diff.xml");
        if (tech)
            material->SetTechnique(0, tech);
    }

    material->SetTexture("VatPosition", slotPositionTextures_[slot]);
    material->SetTexture("VatNormal", slotNormalTextures_[slot]);

    if (slot >= slotMaterials_.size())
        slotMaterials_.resize(slot + 1);
    slotMaterials_[slot] = material;
    return material;
}

bool VATBaker::Bake(Model* model, Animation* animation, float fps, float durationSeconds)
{
    ea::string& error = lastError_;
    Reset();
    error.clear();

    if (!model || !animation)
    {
        error = "VATBaker: model or animation is null";
        return false;
    }
    if (fps <= 0.0f)
        fps = 30.0f;

    Skeleton& skeleton = model->GetSkeleton();
    const unsigned numBones = skeleton.GetNumBones();
    if (numBones == 0)
    {
        error = "VATBaker: model is not skinned (no bones)";
        return false;
    }

    const float duration = durationSeconds > 0.0f ? durationSeconds : animation->GetLength();
    if (duration <= 0.0f)
    {
        error = "VATBaker: non-positive animation duration";
        return false;
    }

    unsigned frames = static_cast<unsigned>(Max(1.0f, static_cast<float>(RoundToInt(duration * fps))));
    frames_ = frames;
    duration_ = duration;

    const ea::vector<Bone>& bones = skeleton.GetBones();
    const ea::vector<unsigned>& boneOrder = skeleton.GetBonesOrder();
    const auto& tracks = animation->GetTracks();

    // Scratch buffers reused across frames.
    ea::vector<Matrix3x4> localToComponent(numBones);
    ea::vector<Matrix3x4> skinMatrices(numBones);

    localBounds_ = BoundingBox();
    bool boundsInit = false;

    const auto& geometries = model->GetGeometries();
    const unsigned numSlots = geometries.size();
    slotGeometries_.resize(numSlots);
    slotPositionTextures_.resize(numSlots);
    slotNormalTextures_.resize(numSlots);
    slotPositionData_.resize(numSlots);
    slotNormalData_.resize(numSlots);
    slotWidths_.resize(numSlots, 0);

    for (unsigned slot = 0; slot < numSlots; ++slot)
    {
        Geometry* srcGeometry = model->GetGeometry(slot, 0);
        if (!srcGeometry || srcGeometry->GetNumVertexBuffers() == 0)
            continue;

        // Pick the vertex buffer that carries the skinning blend data (the "main" buffer for a skinned mesh).
        VertexBuffer* mainBuffer = nullptr;
        for (VertexBuffer* vb : srcGeometry->GetVertexBuffers())
        {
            if (vb && FindElement(vb->GetElements(), SEM_BLENDINDICES) >= 0)
            {
                mainBuffer = vb;
                break;
            }
        }
        if (!mainBuffer)
            mainBuffer = srcGeometry->GetVertexBuffer(0);

        const ea::vector<VertexElement>& elements = mainBuffer->GetElements();
        BindData bind;
        bind.vertexCount_ = mainBuffer->GetVertexCount();
        bind.elementCount_ = elements.size();
        bind.positionIndex_ = FindElement(elements, SEM_POSITION);
        bind.normalIndex_ = FindElement(elements, SEM_NORMAL);
        bind.blendIndexIndex_ = FindElement(elements, SEM_BLENDINDICES);
        bind.blendWeightIndex_ = FindElement(elements, SEM_BLENDWEIGHTS);

        if (bind.positionIndex_ < 0 || bind.blendIndexIndex_ < 0 || bind.blendWeightIndex_ < 0)
        {
            error = "VATBaker: geometry slot missing position or blend elements";
            Reset();
            return false;
        }

        bind.unpacked_ = mainBuffer->GetUnpackedData();
        if (bind.unpacked_.size() < bind.vertexCount_ * bind.elementCount_)
        {
            error = "VATBaker: main vertex buffer has no CPU shadow data (enable shadowing on the model)";
            Reset();
            return false;
        }

        const int width = static_cast<int>(bind.vertexCount_);
        if (width <= 0 || width > kMaxVatWidth)
        {
            error = "VATBaker: vertex count out of supported VAT width range [1, 16384]";
            Reset();
            return false;
        }

        // Allocate the two RGBA32F VAT textures: width = vertices, height = frames.
        SharedPtr<Texture2D> positionTex = MakeShared<Texture2D>(context_);
        SharedPtr<Texture2D> normalTex = MakeShared<Texture2D>(context_);
        positionTex->SetName(Format("{}_VAT_{}", model->GetName(), slot));
        normalTex->SetName(Format("{}_VATN_{}", model->GetName(), slot));
        positionTex->SetNumLevels(1);
        normalTex->SetNumLevels(1);
        positionTex->SetFilterMode(FILTER_NEAREST);
        normalTex->SetFilterMode(FILTER_NEAREST);
        positionTex->SetAddressMode(TextureCoordinate::U, ADDRESS_CLAMP);
        positionTex->SetAddressMode(TextureCoordinate::V, ADDRESS_CLAMP);
        normalTex->SetAddressMode(TextureCoordinate::U, ADDRESS_CLAMP);
        normalTex->SetAddressMode(TextureCoordinate::V, ADDRESS_CLAMP);
        if (!positionTex->SetSize(width, static_cast<int>(frames), TextureFormat::TEX_FORMAT_RGBA32_FLOAT)
            || !normalTex->SetSize(width, static_cast<int>(frames), TextureFormat::TEX_FORMAT_RGBA32_FLOAT))
        {
            error = "VATBaker: failed to allocate VAT textures";
            Reset();
            return false;
        }

        ea::vector<float> posTexels(static_cast<size_t>(width) * frames * 4, 0.0f);
        ea::vector<float> nrmTexels(static_cast<size_t>(width) * frames * 4, 0.0f);

        const unsigned ec = bind.elementCount_;

        for (unsigned f = 0; f < frames; ++f)
        {
            const float time = Min(f / fps, duration - M_EPSILON);

            // Evaluate per-bone local-to-component transforms for this frame.
            for (unsigned boneIndex : boneOrder)
            {
                const Bone& bone = bones[boneIndex];
                Transform local{ bone.initialPosition_, bone.initialRotation_, bone.initialScale_ };

                if (bone.animated_)
                {
                    const auto trackIt = tracks.find(bone.nameHash_);
                    if (trackIt != tracks.end() && !trackIt->second.IsEmpty())
                    {
                        unsigned dummyFrame = 0;
                        trackIt->second.Sample(time, duration, true, dummyFrame, local);
                    }
                }

                const Matrix3x4 localMatrix = local.ToMatrix3x4();
                if (bone.parentIndex_ == boneIndex)
                    localToComponent[boneIndex] = localMatrix;
                else
                    localToComponent[boneIndex] = localToComponent[bone.parentIndex_] * localMatrix;
            }

            // Fold the bind-pose offset into a final skin matrix per bone (model node assumed at identity).
            for (unsigned i = 0; i < numBones; ++i)
                skinMatrices[i] = localToComponent[i] * bones[i].offsetMatrix_;

            // Skin every vertex and write the frame row into the texture buffers.
            for (unsigned v = 0; v < bind.vertexCount_; ++v)
            {
                const Vector4* vtx = &bind.unpacked_[static_cast<size_t>(v) * ec];
                const Vector3 bindPos(vtx[bind.positionIndex_].x_, vtx[bind.positionIndex_].y_, vtx[bind.positionIndex_].z_);
                const Vector4& blendIdx = vtx[bind.blendIndexIndex_];
                Vector4 blendWt = vtx[bind.blendWeightIndex_];

                unsigned idx[kBoneInfluences] = {
                    static_cast<unsigned>(blendIdx.x_),
                    static_cast<unsigned>(blendIdx.y_),
                    static_cast<unsigned>(blendIdx.z_),
                    static_cast<unsigned>(blendIdx.w_)
                };
                float weights[kBoneInfluences] = { blendWt.x_, blendWt.y_, blendWt.z_, blendWt.w_ };

                // Normalize the influences to guard against un-summed data.
                float sum = 0.0f;
                for (unsigned b = 0; b < kBoneInfluences; ++b)
                    sum += weights[b];
                if (sum > M_EPSILON)
                {
                    for (unsigned b = 0; b < kBoneInfluences; ++b)
                        weights[b] /= sum;
                }

                // Blend skin matrix (linear blend of the bone global*offset matrices).
                Matrix3x4 matrix;
                bool first = true;
                for (unsigned b = 0; b < kBoneInfluences; ++b)
                {
                    if (weights[b] <= 0.0f || idx[b] >= numBones)
                        continue;
                    const Matrix3x4 contrib = skinMatrices[idx[b]] * weights[b];
                    if (first)
                    {
                        matrix = contrib;
                        first = false;
                    }
                    else
                        matrix = matrix + contrib;
                }

                const Vector3 skinnedPos = matrix * bindPos;
                size_t texel = (static_cast<size_t>(f) * width + v) * 4;
                posTexels[texel + 0] = skinnedPos.x_;
                posTexels[texel + 1] = skinnedPos.y_;
                posTexels[texel + 2] = skinnedPos.z_;
                posTexels[texel + 3] = 0.0f;

                if (bind.normalIndex_ >= 0)
                {
                    const Vector3 bindNormal(vtx[bind.normalIndex_].x_, vtx[bind.normalIndex_].y_, vtx[bind.normalIndex_].z_);
                    Vector3 skinnedNormal = TransformSkinNormal(matrix, bindNormal);
                    if (skinnedNormal.LengthSquared() > M_EPSILON)
                        skinnedNormal.Normalize();
                    nrmTexels[texel + 0] = skinnedNormal.x_;
                    nrmTexels[texel + 1] = skinnedNormal.y_;
                    nrmTexels[texel + 2] = skinnedNormal.z_;
                    nrmTexels[texel + 3] = 0.0f;

                    if (!boundsInit)
                    {
                        localBounds_ = BoundingBox(skinnedPos, skinnedPos);
                        boundsInit = true;
                    }
                    else
                        localBounds_.Merge(skinnedPos);
                }
            }
        }

        positionTex->SetData(0, 0, 0, width, static_cast<int>(frames), posTexels.data());
        normalTex->SetData(0, 0, 0, width, static_cast<int>(frames), nrmTexels.data());

        SharedPtr<Geometry> bakedGeometry = CreateBakedGeometry(model, slot, static_cast<unsigned>(width));
        if (!bakedGeometry)
        {
            error = "VATBaker: failed to build baked geometry for a slot";
            Reset();
            return false;
        }

        slotGeometries_[slot] = bakedGeometry;
        slotPositionTextures_[slot] = positionTex;
        slotNormalTextures_[slot] = normalTex;
        slotPositionData_[slot] = eastl::move(posTexels);
        slotNormalData_[slot] = eastl::move(nrmTexels);
        slotWidths_[slot] = static_cast<unsigned>(width);
    }

    // Prune empty slots that produced no geometry.
    for (unsigned slot = slotGeometries_.size(); slot > 0; --slot)
    {
        const unsigned i = slot - 1;
        if (!slotGeometries_[i])
        {
            slotGeometries_.erase(slotGeometries_.begin() + i);
            slotPositionTextures_.erase(slotPositionTextures_.begin() + i);
            slotNormalTextures_.erase(slotNormalTextures_.begin() + i);
            slotPositionData_.erase(slotPositionData_.begin() + i);
            slotNormalData_.erase(slotNormalData_.begin() + i);
            slotWidths_.erase(slotWidths_.begin() + i);
        }
    }

    if (slotGeometries_.empty())
    {
        error = "VATBaker: no skinned geometry was baked";
        return false;
    }

    if (!boundsInit)
        localBounds_ = model->GetBoundingBox();

    return true;
}

SharedPtr<Geometry> VATBaker::CreateBakedGeometry(Model* model, unsigned slot, unsigned width)
{
    Geometry* srcGeometry = model->GetGeometry(slot, 0);
    if (!srcGeometry || srcGeometry->GetNumVertexBuffers() == 0 || width == 0)
        return nullptr;

    // Per-vertex VAT lookup UV (SEM_TEXCOORD index 2) as an extra stream: x = (v + 0.5) / width.
    SharedPtr<VertexBuffer> uvBuffer = MakeShared<VertexBuffer>(context_);
    uvBuffer->SetShadowed(true);
    ea::vector<VertexElement> uvElements{ VertexElement(TYPE_VECTOR2, SEM_TEXCOORD, 2) };
    uvBuffer->SetSize(width, uvElements, true);

    ea::vector<Vector4> uvData(width);
    const float invWidth = 1.0f / static_cast<float>(width);
    for (unsigned v = 0; v < width; ++v)
        uvData[v] = Vector4((static_cast<float>(v) + 0.5f) * invWidth, 0.0f, 0.0f, 0.0f);
    uvBuffer->SetUnpackedData(uvData.data(), 0, width);

    SharedPtr<Geometry> bakedGeometry = MakeShared<Geometry>(context_);
    ea::vector<SharedPtr<VertexBuffer>> vertexBuffers = srcGeometry->GetVertexBuffers();
    vertexBuffers.push_back(uvBuffer);
    bakedGeometry->SetVertexBuffers(vertexBuffers);
    bakedGeometry->SetIndexBuffer(srcGeometry->GetIndexBuffer());
    bakedGeometry->SetDrawRange(srcGeometry->GetPrimitiveType(), srcGeometry->GetIndexStart(), srcGeometry->GetIndexCount());
    return bakedGeometry;
}

namespace
{
/// Create and upload one RGBA32F VAT texture (nearest sampling, clamped addressing, single mip level).
SharedPtr<Texture2D> MakeVATTexture(Context* context, const ea::string& name, int width, int height, const float* data)
{
    SharedPtr<Texture2D> texture = MakeShared<Texture2D>(context);
    texture->SetName(name);
    texture->SetNumLevels(1);
    texture->SetFilterMode(FILTER_NEAREST);
    texture->SetAddressMode(TextureCoordinate::U, ADDRESS_CLAMP);
    texture->SetAddressMode(TextureCoordinate::V, ADDRESS_CLAMP);
    if (!texture->SetSize(width, height, TextureFormat::TEX_FORMAT_RGBA32_FLOAT))
        return nullptr;
    if (data && !texture->SetData(0, 0, 0, width, height, data))
        return nullptr;
    return texture;
}

} // namespace

bool VATBaker::SaveVAT(const ea::string& path, ea::string& error) const
{
    error.clear();
    if (slotGeometries_.empty() || slotPositionData_.empty())
    {
        error = "VATBaker::SaveVAT: nothing baked to save";
        return false;
    }

    SharedPtr<File> file = MakeShared<File>(context_);
    if (!file->Open(path, FILE_WRITE))
    {
        error = "VATBaker::SaveVAT: cannot open " + path;
        return false;
    }

    const unsigned slotCount = static_cast<unsigned>(slotPositionData_.size());
    file->Write("VAT1", 4);
    file->Write(&slotCount, sizeof(slotCount));
    file->Write(&frames_, sizeof(frames_));
    file->Write(&duration_, sizeof(duration_));
    for (unsigned s = 0; s < slotCount; ++s)
        file->Write(&slotWidths_[s], sizeof(unsigned));

    for (unsigned s = 0; s < slotCount; ++s)
    {
        const ea::vector<float>& pos = slotPositionData_[s];
        const ea::vector<float>& nrm = slotNormalData_[s];
        file->Write(pos.data(), static_cast<unsigned>(pos.size() * sizeof(float)));
        file->Write(nrm.data(), static_cast<unsigned>(nrm.size() * sizeof(float)));
    }

    file->Close();
    return true;
}

bool VATBaker::LoadVAT(Model* sourceModel, const ea::string& path, ea::string& error)
{
    error.clear();
    Reset();

    if (!sourceModel)
    {
        error = "VATBaker::LoadVAT: source model is null";
        return false;
    }

    SharedPtr<File> file = MakeShared<File>(context_);
    if (!file->Open(path, FILE_READ))
    {
        error = "VATBaker::LoadVAT: cannot open " + path;
        return false;
    }

    char magic[4] = {};
    file->Read(magic, 4);
    if (magic[0] != 'V' || magic[1] != 'A' || magic[2] != 'T' || magic[3] != '1')
    {
        error = "VATBaker::LoadVAT: bad archive magic";
        return false;
    }

    unsigned slotCount = 0;
    file->Read(&slotCount, sizeof(slotCount));
    file->Read(&frames_, sizeof(frames_));
    file->Read(&duration_, sizeof(duration_));

    ea::vector<unsigned> widths(slotCount);
    for (unsigned s = 0; s < slotCount; ++s)
        file->Read(&widths[s], sizeof(unsigned));

    slotWidths_ = widths;
    slotGeometries_.resize(slotCount);
    slotPositionTextures_.resize(slotCount);
    slotNormalTextures_.resize(slotCount);
    slotPositionData_.resize(slotCount);
    slotNormalData_.resize(slotCount);

    const int height = static_cast<int>(frames_);
    for (unsigned s = 0; s < slotCount; ++s)
    {
        const size_t count = static_cast<size_t>(widths[s]) * frames_ * 4;
        ea::vector<float> pos(count);
        ea::vector<float> nrm(count);
        file->Read(pos.data(), static_cast<unsigned>(count * sizeof(float)));
        file->Read(nrm.data(), static_cast<unsigned>(count * sizeof(float)));

        slotPositionTextures_[s] = MakeVATTexture(context_, Format("{}_VAT_{}", sourceModel->GetName(), s),
            static_cast<int>(widths[s]), height, pos.data());
        slotNormalTextures_[s] = MakeVATTexture(context_, Format("{}_VATN_{}", sourceModel->GetName(), s),
            static_cast<int>(widths[s]), height, nrm.data());
        slotGeometries_[s] = CreateBakedGeometry(sourceModel, s, widths[s]);

        if (!slotPositionTextures_[s] || !slotNormalTextures_[s] || !slotGeometries_[s])
        {
            error = "VATBaker::LoadVAT: failed to rehydrate a slot";
            Reset();
            return false;
        }

        // Retain the raw frame data so a loaded baker matches a freshly baked one (it can be
        // re-saved without baking again).
        slotPositionData_[s] = eastl::move(pos);
        slotNormalData_[s] = eastl::move(nrm);
    }

    file->Close();

    // Rebuild the local-space bounding box from the loaded positions. The .vat archive stores only
    // frame data, not bounds; without this a cache-loaded model would hand the runtime drawable an
    // invalid (empty) box and break culling / world bounds. Mirrors Bake()'s accumulation.
    bool boundsInit = false;
    for (unsigned s = 0; s < slotCount; ++s)
    {
        const ea::vector<float>& posData = slotPositionData_[s];
        const size_t numTexels = posData.size() / 4;
        for (size_t i = 0; i < numTexels; ++i)
        {
            const Vector3 point(posData[i * 4 + 0], posData[i * 4 + 1], posData[i * 4 + 2]);
            if (!boundsInit)
            {
                localBounds_ = BoundingBox(point, point);
                boundsInit = true;
            }
            else
                localBounds_.Merge(point);
        }
    }
    if (!boundsInit)
        localBounds_ = sourceModel->GetBoundingBox();

    return true;
}

bool VATBaker::SaveVAT(const ea::string& path)
{
    return const_cast<const VATBaker*>(this)->SaveVAT(path, lastError_);
}

bool VATBaker::LoadVAT(Model* sourceModel, const ea::string& path)
{
    return LoadVAT(sourceModel, path, lastError_);
}

}


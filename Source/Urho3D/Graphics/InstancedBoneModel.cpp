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

#include "InstancedBoneModel.h"

#include "../Core/Context.h"
#include "../Core/CoreEvents.h"
#include "../Core/ObjectCategory.h"
#include "../Graphics/BoneMatrixBaker.h"
#include "../Graphics/Camera.h"
#include "../Graphics/Geometry.h"
#include "../Graphics/Material.h"
#include "../Graphics/OcclusionBuffer.h"
#include "../Graphics/OctreeQuery.h"
#include "../Graphics/Texture2D.h"
#include "../Graphics/VertexBuffer.h"
#include "../Math/Matrix3x4.h"
#include "../Math/Quaternion.h"
#include "../Math/Vector3.h"
#include "../Scene/Node.h"

#include "../DebugNew.h"

namespace Urho3D
{

InstancedBoneModel::InstancedBoneModel(Context* context)
    : Drawable(context, DRAWABLE_GEOMETRY)
{
}

InstancedBoneModel::~InstancedBoneModel() = default;

void InstancedBoneModel::RegisterObject(Context* context)
{
    context->AddFactoryReflection<InstancedBoneModel>(Category_Geometry);

    URHO3D_ACCESSOR_ATTRIBUTE("Is Enabled", IsEnabled, SetEnabled, bool, true, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Is Occluder", bool, occluder_, false, AM_DEFAULT);
    URHO3D_ACCESSOR_ATTRIBUTE("Can Be Occluded", IsOccludee, SetOccludee, bool, true, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Cast Shadows", bool, castShadows_, true, AM_DEFAULT);
    URHO3D_ACCESSOR_ATTRIBUTE("Draw Distance", GetDrawDistance, SetDrawDistance, float, 0.0f, AM_DEFAULT);
    URHO3D_ACCESSOR_ATTRIBUTE("Shadow Distance", GetShadowDistance, SetShadowDistance, float, 0.0f, AM_DEFAULT);
    URHO3D_ACCESSOR_ATTRIBUTE("LOD Bias", GetLodBias, SetLodBias, float, 1.0f, AM_DEFAULT);
    URHO3D_COPY_BASE_ATTRIBUTES(Drawable);
}

void InstancedBoneModel::OnSetEnabled()
{
    Drawable::OnSetEnabled();

    if (IsEnabledEffective() && geometry_ && baker_ && !worldTransforms_.empty())
        SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(InstancedBoneModel, HandleUpdate));
    else
        UnsubscribeFromEvent(E_UPDATE);
}

void InstancedBoneModel::SetGeometry(Geometry* geometry)
{
    if (geometry_ == geometry)
        return;

    geometry_ = geometry;
    RefreshBatch();

    if (IsEnabledEffective() && geometry_ && baker_ && !worldTransforms_.empty())
        SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(InstancedBoneModel, HandleUpdate));
    else
        UnsubscribeFromEvent(E_UPDATE);

    OnMarkedDirty(node_);
}

void InstancedBoneModel::SetMaterial(Material* material)
{
    // Clone so binding the atlas texture does not mutate the shared source material resource.
    material_ = material ? material->Clone() : nullptr;
    if (material_ && baker_ && baker_->GetTexture())
        material_->SetTexture("BoneMatrices", baker_->GetTexture());
    RefreshBatch();
    OnMarkedDirty(node_);
}

void InstancedBoneModel::SetBoneAtlas(BoneMatrixBaker* baker)
{
    baker_ = baker;
    if (material_ && baker && baker->GetTexture())
        material_->SetTexture("BoneMatrices", baker->GetTexture());

    // Re-derive the frame rows against the new clip table.
    RefreshFrameRows();
    RefreshBatch();

    if (IsEnabledEffective() && geometry_ && baker_ && !worldTransforms_.empty())
        SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(InstancedBoneModel, HandleUpdate));
    else
        UnsubscribeFromEvent(E_UPDATE);

    OnMarkedDirty(node_);
}

unsigned InstancedBoneModel::AddInstance(const Matrix3x4& worldTransform, unsigned animIndex, float phase,
    float playSpeed)
{
    worldTransforms_.push_back(worldTransform);
    animStates_.push_back(AnimState{ animIndex, phase - floorf(phase), playSpeed });
    frameRows_.push_back(Vector4::ZERO);
    RefreshFrameRows();
    RefreshBatch();

    if (IsEnabledEffective() && geometry_ && baker_ && !worldTransforms_.empty())
        SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(InstancedBoneModel, HandleUpdate));

    worldBoundingBoxDirty_ = true;
    OnMarkedDirty(node_);
    return static_cast<unsigned>(worldTransforms_.size() - 1);
}

unsigned InstancedBoneModel::AddInstanceTRS(const Vector3& position, const Quaternion& rotation, const Vector3& scale,
    unsigned animIndex, float phase, float playSpeed)
{
    return AddInstance(Matrix3x4(position, rotation, scale), animIndex, phase, playSpeed);
}

void InstancedBoneModel::RemoveInstance(unsigned index)
{
    if (index >= worldTransforms_.size())
        return;

    worldTransforms_.erase(worldTransforms_.begin() + index);
    animStates_.erase(animStates_.begin() + index);
    frameRows_.erase(frameRows_.begin() + index);
    RefreshBatch();

    if (worldTransforms_.empty())
        UnsubscribeFromEvent(E_UPDATE);

    worldBoundingBoxDirty_ = true;
    OnMarkedDirty(node_);
}

void InstancedBoneModel::ClearInstances()
{
    worldTransforms_.clear();
    animStates_.clear();
    frameRows_.clear();
    batches_.clear();
    UnsubscribeFromEvent(E_UPDATE);
    worldBoundingBoxDirty_ = true;
    OnMarkedDirty(node_);
}

void InstancedBoneModel::SetInstanceTransform(unsigned index, const Matrix3x4& worldTransform)
{
    if (index >= worldTransforms_.size())
        return;

    worldTransforms_[index] = worldTransform;
    worldBoundingBoxDirty_ = true;
    OnMarkedDirty(node_);
}

void InstancedBoneModel::SetInstanceAnimIndex(unsigned index, unsigned animIndex)
{
    if (index < animStates_.size())
    {
        animStates_[index].clip_ = animIndex;
        RefreshFrameRows();
        OnMarkedDirty(node_);
    }
}

void InstancedBoneModel::SetInstancePhase(unsigned index, float phase)
{
    if (index < animStates_.size())
    {
        animStates_[index].phase_ = phase - floorf(phase);
        RefreshFrameRows();
        OnMarkedDirty(node_);
    }
}

float InstancedBoneModel::GetInstancePhase(unsigned index) const
{
    return index < animStates_.size() ? animStates_[index].phase_ : 0.0f;
}

void InstancedBoneModel::SetInstancePlaySpeed(unsigned index, float playSpeed)
{
    if (index < animStates_.size())
        animStates_[index].speed_ = playSpeed;
}

void InstancedBoneModel::SetLocalBoundingBox(const BoundingBox& box)
{
    boundingBox_ = box;
    worldBoundingBoxDirty_ = true;
    OnMarkedDirty(node_);
}

void InstancedBoneModel::RefreshFrameRows()
{
    if (!baker_)
        return;

    const ea::vector<BakedBoneClip>& clips = baker_->GetClips();
    for (unsigned i = 0; i < animStates_.size(); ++i)
    {
        const AnimState& state = animStates_[i];
        float row = 0.0f;
        if (state.clip_ < clips.size())
        {
            const BakedBoneClip& clip = clips[state.clip_];
            float fi = state.phase_ * static_cast<float>(clip.frameCount_);
            if (fi >= static_cast<float>(clip.frameCount_))
                fi = static_cast<float>(clip.frameCount_ - 1);
            else if (fi < 0.0f)
                fi = 0.0f;
            row = static_cast<float>(clip.startFrame_) + floorf(fi);
        }
        frameRows_[i] = Vector4(row, 0.0f, 0.0f, 0.0f);
    }
}

void InstancedBoneModel::RefreshBatch()
{
    const bool hasDraw = geometry_ && !geometry_->GetVertexBuffers().empty() && !worldTransforms_.empty();
    if (!hasDraw)
    {
        batches_.clear();
        return;
    }

    if (batches_.empty())
        batches_.resize(1);

    SourceBatch& batch = batches_[0];
    batch.geometry_ = geometry_;
    batch.material_ = material_;
    batch.geometryType_ = GEOM_INSTANCED_BONE;
    batch.worldTransform_ = &worldTransforms_[0];
    batch.numWorldTransforms_ = static_cast<unsigned>(worldTransforms_.size());
    // Trailing per-instance atlas frame-row index (x).
    batch.instancingData_ = &frameRows_[0];
    batch.lightmapScaleOffset_ = nullptr;
    batch.lightmapIndex_ = M_MAX_UNSIGNED;
}

void InstancedBoneModel::HandleUpdate(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace Update;
    const float timeStep = eventData[P_TIMESTEP].GetFloat();
    if (!baker_ || timeStep <= 0.0f)
        return;

    const ea::vector<BakedBoneClip>& clips = baker_->GetClips();
    for (AnimState& state : animStates_)
    {
        const float duration = state.clip_ < clips.size() ? clips[state.clip_].duration_ : 0.0f;
        if (duration <= 0.0f)
            continue;
        state.phase_ += state.speed_ * timeStep / duration;
        state.phase_ -= floorf(state.phase_); // wrap into [0, 1)
    }

    RefreshFrameRows();
}

void InstancedBoneModel::UpdateBatches(const FrameInfo& frame)
{
    if (batches_.empty())
        return;

    // Re-point the batch in case the instance arrays were resized since the last RefreshBatch.
    RefreshBatch();
    if (batches_.empty())
        return;

    const BoundingBox& worldBoundingBox = GetWorldBoundingBox();
    distance_ = frame.camera_->GetDistance(worldBoundingBox.Center());
    batches_[0].distance_ = distance_;
}

void InstancedBoneModel::OnWorldBoundingBoxUpdate()
{
    BoundingBox worldBox;
    for (const Matrix3x4& transform : worldTransforms_)
        worldBox.Merge(boundingBox_.Transformed(transform));

    worldBoundingBox_ = worldBox;
}

unsigned InstancedBoneModel::GetNumOccluderTriangles()
{
    GetWorldBoundingBox();
    if (!geometry_)
        return 0;
    return static_cast<unsigned>(worldTransforms_.size()) * geometry_->GetIndexCount() / 3;
}

bool InstancedBoneModel::DrawOcclusion(OcclusionBuffer* buffer)
{
    GetWorldBoundingBox();
    if (!geometry_)
        return true;

    Material* material = batches_.empty() ? nullptr : batches_[0].material_;
    if (material)
    {
        if (!material->GetOcclusion())
            return true;
        buffer->SetCullMode(material->GetCullMode());
    }
    else
        buffer->SetCullMode(CULL_CCW);

    const unsigned char* vertexData;
    unsigned vertexSize;
    const unsigned char* indexData;
    unsigned indexSize;
    const ea::vector<VertexElement>* elements;

    geometry_->GetRawData(vertexData, vertexSize, indexData, indexSize, elements);
    if (!vertexData || !indexData || !elements
        || VertexBuffer::GetElementOffset(*elements, TYPE_VECTOR3, SEM_POSITION) != 0)
        return true;

    const unsigned indexStart = geometry_->GetIndexStart();
    const unsigned indexCount = geometry_->GetIndexCount();

    for (const Matrix3x4& transform : worldTransforms_)
        if (!buffer->AddTriangles(transform, vertexData, vertexSize, indexData, indexSize, indexStart, indexCount))
            return false;

    return true;
}

}

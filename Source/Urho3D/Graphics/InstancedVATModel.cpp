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

#include "InstancedVATModel.h"

#include "../Core/Context.h"
#include "../Core/CoreEvents.h"
#include "../Core/ObjectCategory.h"
#include "../Core/ProcessUtils.h"
#include "../Graphics/Camera.h"
#include "../Graphics/Geometry.h"
#include "../Graphics/Material.h"
#include "../Graphics/OcclusionBuffer.h"
#include "../Graphics/OctreeQuery.h"
#include "../Graphics/VertexBuffer.h"
#include "../Math/Matrix3x4.h"
#include "../Math/Quaternion.h"
#include "../Math/Vector3.h"
#include "../Scene/Node.h"

#include "../DebugNew.h"

namespace Urho3D
{

InstancedVATModel::InstancedVATModel(Context* context) :
    Drawable(context, DRAWABLE_GEOMETRY)
{
}

InstancedVATModel::~InstancedVATModel() = default;

void InstancedVATModel::RegisterObject(Context* context)
{
    context->AddFactoryReflection<InstancedVATModel>(Category_Geometry);

    URHO3D_ACCESSOR_ATTRIBUTE("Is Enabled", IsEnabled, SetEnabled, bool, true, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Animation Duration", float, duration_, 1.0f, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Is Occluder", bool, occluder_, false, AM_DEFAULT);
    URHO3D_ACCESSOR_ATTRIBUTE("Can Be Occluded", IsOccludee, SetOccludee, bool, true, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Cast Shadows", bool, castShadows_, true, AM_DEFAULT);
    URHO3D_ACCESSOR_ATTRIBUTE("Draw Distance", GetDrawDistance, SetDrawDistance, float, 0.0f, AM_DEFAULT);
    URHO3D_ACCESSOR_ATTRIBUTE("Shadow Distance", GetShadowDistance, SetShadowDistance, float, 0.0f, AM_DEFAULT);
    URHO3D_ACCESSOR_ATTRIBUTE("LOD Bias", GetLodBias, SetLodBias, float, 1.0f, AM_DEFAULT);
    URHO3D_COPY_BASE_ATTRIBUTES(Drawable);
}

void InstancedVATModel::OnSetEnabled()
{
    Drawable::OnSetEnabled();

    if (IsEnabledEffective() && geometry_ && !worldTransforms_.empty())
        SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(InstancedVATModel, HandleUpdate));
    else
        UnsubscribeFromEvent(E_UPDATE);
}

void InstancedVATModel::SetGeometry(Geometry* geometry)
{
    if (geometry_ == geometry)
        return;

    geometry_ = geometry;
    RefreshBatch();

    // Only animate when there is something to draw.
    if (IsEnabledEffective() && geometry_ && !worldTransforms_.empty())
        SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(InstancedVATModel, HandleUpdate));
    else
        UnsubscribeFromEvent(E_UPDATE);

    OnMarkedDirty(node_);
}

void InstancedVATModel::SetMaterial(Material* material)
{
    material_ = material;
    RefreshBatch();
    OnMarkedDirty(node_);
}

unsigned InstancedVATModel::AddInstance(const Matrix3x4& worldTransform, float animTime, float playSpeed)
{
    worldTransforms_.push_back(worldTransform);
    animParams_.push_back(Vector4(animTime, playSpeed, 0.0f, 0.0f));
    RefreshBatch();

    if (IsEnabledEffective() && geometry_ && !worldTransforms_.empty())
        SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(InstancedVATModel, HandleUpdate));

    worldBoundingBoxDirty_ = true;
    OnMarkedDirty(node_);
    return static_cast<unsigned>(worldTransforms_.size() - 1);
}

unsigned InstancedVATModel::AddInstanceTRS(const Vector3& position, const Quaternion& rotation, const Vector3& scale,
    float animTime, float playSpeed)
{
    return AddInstance(Matrix3x4(position, rotation, scale), animTime, playSpeed);
}

void InstancedVATModel::RemoveInstance(unsigned index)
{
    if (index >= worldTransforms_.size())
        return;

    worldTransforms_.erase(worldTransforms_.begin() + index);
    animParams_.erase(animParams_.begin() + index);
    RefreshBatch();

    if (worldTransforms_.empty())
        UnsubscribeFromEvent(E_UPDATE);

    worldBoundingBoxDirty_ = true;
    OnMarkedDirty(node_);
}

void InstancedVATModel::ClearInstances()
{
    worldTransforms_.clear();
    animParams_.clear();
    batches_.clear();
    UnsubscribeFromEvent(E_UPDATE);
    worldBoundingBoxDirty_ = true;
    OnMarkedDirty(node_);
}

void InstancedVATModel::SetInstanceTransform(unsigned index, const Matrix3x4& worldTransform)
{
    if (index >= worldTransforms_.size())
        return;

    worldTransforms_[index] = worldTransform;
    worldBoundingBoxDirty_ = true;
    OnMarkedDirty(node_);
}

void InstancedVATModel::SetInstanceAnimTime(unsigned index, float animTime)
{
    if (index < animParams_.size())
        animParams_[index].x_ = animTime - floorf(animTime);
}

float InstancedVATModel::GetInstanceAnimTime(unsigned index) const
{
    return index < animParams_.size() ? animParams_[index].x_ : 0.0f;
}

void InstancedVATModel::SetInstancePlaySpeed(unsigned index, float playSpeed)
{
    if (index < animParams_.size())
        animParams_[index].y_ = playSpeed;
}

void InstancedVATModel::SetAnimationDuration(float seconds)
{
    duration_ = seconds;
}

void InstancedVATModel::SetLocalBoundingBox(const BoundingBox& box)
{
    boundingBox_ = box;
    worldBoundingBoxDirty_ = true;
    OnMarkedDirty(node_);
}

void InstancedVATModel::RefreshBatch()
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
    batch.geometryType_ = GEOM_INSTANCED_VAT;
    batch.worldTransform_ = &worldTransforms_[0];
    batch.numWorldTransforms_ = static_cast<unsigned>(worldTransforms_.size());
    // Trailing per-instance VAT anim params (x = normalized animation time).
    batch.instancingData_ = &animParams_[0];
    batch.lightmapScaleOffset_ = nullptr;
    batch.lightmapIndex_ = M_MAX_UNSIGNED;
}

void InstancedVATModel::HandleUpdate(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace Update;
    const float timeStep = eventData[P_TIMESTEP].GetFloat();
    if (duration_ <= 0.0f)
        return;

    const float inv = timeStep / duration_;
    for (Vector4& p : animParams_)
    {
        p.x_ += p.y_ * inv;
        p.x_ -= floorf(p.x_); // wrap into [0, 1)
    }
}

void InstancedVATModel::UpdateBatches(const FrameInfo& frame)
{
    if (batches_.empty())
        return;

    // Re-point the batch in case the instance arrays were resized since RefreshBatch.
    RefreshBatch();
    if (batches_.empty())
        return;

    const BoundingBox& worldBoundingBox = GetWorldBoundingBox();
    distance_ = frame.camera_->GetDistance(worldBoundingBox.Center());
    batches_[0].distance_ = distance_;
}

void InstancedVATModel::OnWorldBoundingBoxUpdate()
{
    BoundingBox worldBox;
    for (unsigned i = 0; i < worldTransforms_.size(); ++i)
        worldBox.Merge(boundingBox_.Transformed(worldTransforms_[i]));

    worldBoundingBox_ = worldBox;
}

unsigned InstancedVATModel::GetNumOccluderTriangles()
{
    GetWorldBoundingBox();
    if (!geometry_)
        return 0;
    return static_cast<unsigned>(worldTransforms_.size()) * geometry_->GetIndexCount() / 3;
}

bool InstancedVATModel::DrawOcclusion(OcclusionBuffer* buffer)
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

    for (unsigned i = 0; i < worldTransforms_.size(); ++i)
        if (!buffer->AddTriangles(worldTransforms_[i], vertexData, vertexSize, indexData, indexSize, indexStart, indexCount))
            return false;

    return true;
}

}

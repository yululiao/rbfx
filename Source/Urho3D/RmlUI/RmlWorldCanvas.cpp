//
// Copyright (c) 2017-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#include "../Precompiled.h"

#include "../RmlUI/RmlWorldCanvas.h"

#include "../Core/Context.h"
#include "../Graphics/Graphics.h"
#include "../Graphics/Material.h"
#include "../Graphics/Technique.h"
#include "../Graphics/Texture2D.h"
#include "../IO/Log.h"
#include "../RenderPipeline/ShaderConsts.h"
#include "../Resource/BinaryFile.h"
#include "../Resource/ResourceCache.h"
#include "../RmlUI/RmlUI.h"
#include "../Scene/Node.h"
#include "../Scene/Scene.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>

#include "../DebugNew.h"

namespace Urho3D
{

namespace
{
constexpr int kMinLogicalSize = 16;
constexpr int kMaxLogicalSize = 8192;

const char* rmlCanvasModeNames[] = {
    "World",
    "Screen Camera",
    "Screen Overlay",
    nullptr,
};
}

RmlWorldCanvas::RmlWorldCanvas(Context* context)
    : CustomGeometry(context)
{
    offScreenUI_ = new RmlUI(context_, Format("RmlWorldCanvas_{:p}", static_cast<void*>(this)).c_str());
    texture_ = MakeShared<Texture2D>(context_);
    SetNumGeometries(1);
    SetDynamic(true);
}

RmlWorldCanvas::~RmlWorldCanvas()
{
    if (offScreenUI_ && offScreenUI_->GetRmlContext())
        offScreenUI_->GetRmlContext()->UnloadAllDocuments();
    document_ = nullptr;
}

void RmlWorldCanvas::RegisterObject(Context* context)
{
    context->AddFactoryReflection<RmlWorldCanvas>(Category_RmlUI);

    // clang-format off
    URHO3D_ACCESSOR_ATTRIBUTE("Resource", GetResource, SetResource, ResourceRef,
                              ResourceRef{BinaryFile::GetTypeStatic()}, AM_DEFAULT);
    URHO3D_ACCESSOR_ATTRIBUTE("Logical Size", GetLogicalSize, SetLogicalSize, IntVector2,
                              IntVector2(512, 512), AM_DEFAULT);
    URHO3D_ACCESSOR_ATTRIBUTE("Physical Size", GetPhysicalSize, SetPhysicalSize, Vector2,
                              Vector2(1.0f, 1.0f), AM_DEFAULT);
    URHO3D_ENUM_ATTRIBUTE("Mode", mode_, rmlCanvasModeNames, RML_CANVAS_WORLD, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Clear Color", Color, clearColor_, Color::TRANSPARENT_BLACK, AM_DEFAULT);
    // clang-format on
}

void RmlWorldCanvas::SetResource(const ResourceRef& resource)
{
    if (resource_.name_ == resource.name_ && resource_.type_ == resource.type_)
        return;
    resource_ = resource;
    pendingReload_ = true;
    ApplyPending();
}

void RmlWorldCanvas::SetLogicalSize(const IntVector2& size)
{
    const int w = ea::max(kMinLogicalSize, ea::min(kMaxLogicalSize, size.x_));
    const int h = ea::max(kMinLogicalSize, ea::min(kMaxLogicalSize, size.y_));
    const IntVector2 clamped(w, h);
    if (logicalSize_ == clamped)
        return;
    logicalSize_ = clamped;
    pendingReload_ = true;
    ApplyPending();
}

void RmlWorldCanvas::SetPhysicalSize(const Vector2& size)
{
    if ((physicalSize_ - size).LengthSquared() < M_EPSILON)
        return;
    physicalSize_ = size;
    pendingGeometryRebuild_ = true;
    ApplyPending();
}

void RmlWorldCanvas::OnSetEnabled()
{
    if (offScreenUI_)
    {
        offScreenUI_->SetRendering(enabled_);
        offScreenUI_->SetBlockEvents(!enabled_);
    }
    ApplyPending();
}

void RmlWorldCanvas::OnNodeSet(Node* /*previousNode*/, Node* /*currentNode*/)
{
    pendingReload_ = true;
    pendingGeometryRebuild_ = true;
    ApplyPending();
}

void RmlWorldCanvas::ApplyPending()
{
    if (node_ == nullptr || !IsEnabledEffective())
        return;

    if (pendingGeometryRebuild_)
    {
        BuildQuad();
        pendingGeometryRebuild_ = false;
    }

    if (pendingReload_)
    {
        EnsureTexture();
        ReleaseDocument();
        EnsureDocument();
        EnsureMaterial();
        pendingReload_ = false;
    }
}

void RmlWorldCanvas::EnsureTexture()
{
    if (texture_ == nullptr)
        return;
    texture_->SetSize(logicalSize_.x_, logicalSize_.y_,
                      TextureFormat::TEX_FORMAT_RGBA8_UNORM,
                      TextureFlag::BindRenderTarget);
    texture_->SetFilterMode(FILTER_BILINEAR);
    texture_->SetAddressMode(TextureCoordinate::U, ADDRESS_CLAMP);
    texture_->SetAddressMode(TextureCoordinate::V, ADDRESS_CLAMP);
    texture_->SetNumLevels(1);

    RenderSurface* surface = texture_->GetRenderSurface();
    if (surface != nullptr)
    {
        surface->SetUpdateMode(SURFACE_MANUALUPDATE);
        offScreenUI_->SetRenderTarget(surface, clearColor_);
    }
    else
    {
        offScreenUI_->SetRenderTarget(nullptr);
        URHO3D_LOGERROR("RmlWorldCanvas: Failed to acquire RenderSurface for offscreen texture.");
    }
}

void RmlWorldCanvas::EnsureDocument()
{
    if (resource_.name_.empty() || offScreenUI_ == nullptr)
        return;
    document_ = offScreenUI_->LoadDocument(resource_.name_, this);
    if (document_ != nullptr)
        document_->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    else
        URHO3D_LOGERROR("RmlWorldCanvas: Failed to load UI document '{}'", resource_.name_.c_str());
}

void RmlWorldCanvas::ReleaseDocument()
{
    document_ = nullptr;
    if (offScreenUI_ != nullptr && offScreenUI_->GetRmlContext() != nullptr)
        offScreenUI_->GetRmlContext()->UnloadAllDocuments();
}

void RmlWorldCanvas::EnsureMaterial()
{
    if (autoMaterial_ == nullptr)
    {
        auto* cache = GetSubsystem<ResourceCache>();
        auto* technique = cache->GetResource<Technique>("Techniques/DiffUnlitAlpha.xml");
        if (technique == nullptr)
        {
            URHO3D_LOGERROR("RmlWorldCanvas: missing Techniques/DiffUnlitAlpha.xml");
            return;
        }
        autoMaterial_ = MakeShared<Material>(context_);
        autoMaterial_->SetTechnique(0, technique);
    }
    autoMaterial_->SetTexture(ShaderResources::Albedo, texture_);
    // Only claim the batch material if the caller has not set one explicitly.
    if (batches_.empty() || batches_[0].material_ == nullptr || batches_[0].material_ == autoMaterial_)
        SetMaterial(autoMaterial_);
}

void RmlWorldCanvas::BuildQuad()
{
    const float hw = physicalSize_.x_ * 0.5f;
    const float hh = physicalSize_.y_ * 0.5f;

    SetNumGeometries(1);
    BeginGeometry(0, TRIANGLE_LIST);

    // XY-plane quad facing +Z, UV in [0, 1]. Two triangles, six vertices.
    // Note: texture V coordinate is flipped so that RmlUi's top-left origin
    // maps correctly to the standard rbfx texture convention.
    static const Vector3 kPositions[6] = {
        Vector3(-hw, -hh, 0.0f),
        Vector3( hw, -hh, 0.0f),
        Vector3( hw,  hh, 0.0f),
        Vector3(-hw, -hh, 0.0f),
        Vector3( hw,  hh, 0.0f),
        Vector3(-hw,  hh, 0.0f),
    };
    static const Vector2 kUVs[6] = {
        Vector2(0.0f, 1.0f),
        Vector2(1.0f, 1.0f),
        Vector2(1.0f, 0.0f),
        Vector2(0.0f, 1.0f),
        Vector2(1.0f, 0.0f),
        Vector2(0.0f, 0.0f),
    };
    for (unsigned i = 0; i < 6; ++i)
    {
        DefineVertex(kPositions[i]);
        DefineTexCoord(kUVs[i]);
    }
    Commit();
}

}

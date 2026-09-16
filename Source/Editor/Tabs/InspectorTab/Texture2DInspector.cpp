//
// Copyright (c) 2022-2022 the rbfx project.
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

#include "../../Tabs/InspectorTab/Texture2DInspector.h"

#include "../../Project/Project.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/Graphics/Texture2D.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/SystemUI/SystemUI.h>
#include <Urho3D/SystemUI/Texture2DWidget.h>
#include <Urho3D/SystemUI/Texture2DInspectorWidget.h>
#include <Urho3D/SystemUI/Widgets.h>

namespace Urho3D
{

namespace
{
/// Combo labels for TextureImportType, indexed by enum value.
const StringVector textureTypeNames{
    "Default",
    "Normal Map",
};
/// Combo labels for TextureImportColorSpace, indexed by enum value.
const StringVector colorSpaceNames{
    "sRGB",
    "Linear",
};
/// Combo labels for TextureImportMipmapMode, indexed by enum value.
const StringVector mipmapModeNames{
    "Inherit",
    "Enabled",
    "Disabled",
};
/// Combo labels for the optional sampler overrides. Index 0 means "keep the engine default".
const StringVector filterModeNames{
    "Default",
    "Nearest",
    "Bilinear",
    "Trilinear",
    "Anisotropic",
    "Nearest Anisotropic",
};
const StringVector addressModeNames{
    "Default",
    "Wrap",
    "Mirror",
    "Clamp",
};

/// Whether the given absolute source file lives inside the project's Data directory. Engine-shipped
/// textures do not, and writing import metadata next to those would leak into the engine tree.
bool IsProjectOwnedFile(Project* project, const ea::string& sourceFileName)
{
    if (!project)
        return false;
    // Canonical forward slashes plus a trailing slash, so "D:/proj/Data" only matches files
    // inside it and never a sibling directory that merely shares a prefix.
    ea::string projectData = project->GetDataPath();
    projectData.replace("\\", "/");
    if (!projectData.empty() && projectData.back() != '/')
        projectData += '/';
    return !projectData.empty() && sourceFileName.compare(0, projectData.size(), projectData) == 0;
}

} // namespace

void Tabs_Texture2DInspector(Context* context, InspectorTab* inspectorTab)
{
    inspectorTab->RegisterAddon<Texture2DInspector>(inspectorTab->GetProject());
}

Texture2DInspector::Texture2DInspector(Project* project)
    : BaseClassName(project)
{
}

StringHash Texture2DInspector::GetResourceType() const
{
    return Texture2D::GetTypeStatic();
}

SharedPtr<BaseWidget> Texture2DInspector::MakePreviewWidget(Resource* resource)
{
    return MakeShared<Texture2DWidget>(context_, static_cast<Texture2D*>(resource));
}

SharedPtr<ResourceInspectorWidget> Texture2DInspector::MakeInspectorWidget(const ResourceVector& resources)
{
    return MakeShared<Texture2DInspectorWidget>(context_, resources);
}

TextureImporterParams Texture2DInspector::LoadParamsForResource(const ea::string& resourceName) const
{
    auto* cache = GetSubsystem<ResourceCache>();
    const ea::string sourceFileName = cache ? cache->GetResourceFileName(resourceName) : ea::string{};
    if (sourceFileName.empty())
        return {};

    TextureImporterParams params;
    LoadTextureImporterParams(context_, sourceFileName, params);
    return params;
}

void Texture2DInspector::SaveParamsForSelection(const ea::function<void(TextureImporterParams&)>& applyField)
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* project = GetSubsystem<Project>();
    if (!cache || !project)
        return;

    for (const ea::string& resourceName : GetResourceNames())
    {
        // The metadata file lives next to the source file in Data/, so it is staged and read by
        // the build as-is.
        const ea::string sourceFileName = cache->GetResourceFileName(resourceName);
        if (sourceFileName.empty() || !IsProjectOwnedFile(project, sourceFileName))
            continue;

        TextureImporterParams params;
        LoadTextureImporterParams(context_, sourceFileName, params);
        applyField(params);
        if (!SaveTextureImporterParams(context_, sourceFileName, params))
        {
            URHO3D_LOGERROR("Could not save the texture metadata for '{}'.", resourceName);
            continue;
        }

        // The engine reads the very same file while loading, so a reload picks the new settings
        // up and the preview shows them immediately.
        if (auto* texture = cache->GetResource<Texture2D>(resourceName))
            cache->ReloadResource(texture);
    }
}

void Texture2DInspector::RenderExtraInspectorContent()
{
    const StringVector& resourceNames = GetResourceNames();
    if (resourceNames.empty())
        return;

    // Reload the displayed params whenever the selection changes.
    if (resourceNames != cachedResourceNames_)
    {
        cachedResourceNames_ = resourceNames;
        cachedParams_ = LoadParamsForResource(resourceNames.front());
    }

    if (!ui::CollapsingHeader("Import", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    auto* cache = GetSubsystem<ResourceCache>();
    auto* project = GetSubsystem<Project>();
    const ea::string firstSourceFileName =
        cache ? cache->GetResourceFileName(resourceNames.front()) : ea::string{};
    if (!project || firstSourceFileName.empty() || !IsProjectOwnedFile(project, firstSourceFileName))
    {
        ui::TextUnformatted("Engine resource: import settings are fixed.");
        return;
    }

    // Texture type. Switching also resets the color space to the type's canonical default, so
    // the metadata file never carries a combination the cook would have to override silently.
    Widgets::ItemLabel("Texture Type");
    {
        Variant value{static_cast<int>(cachedParams_.textureType_)};
        if (Widgets::EditVariant(value, Widgets::EditVariantOptions{}.Enum(textureTypeNames)))
        {
            const auto newType = static_cast<TextureImportType>(value.GetInt());
            const auto newColorSpace = newType == TextureImportType::NormalMap
                ? TextureImportColorSpace::Linear
                : TextureImportColorSpace::SRGB;
            SaveParamsForSelection([&](TextureImporterParams& params)
            {
                params.textureType_ = newType;
                params.colorSpace_ = newColorSpace;
            });
            cachedParams_.textureType_ = newType;
            cachedParams_.colorSpace_ = newColorSpace;
        }
        if (ui::IsItemHovered())
            ui::SetTooltip("How this texture is compressed at build time.\n"
                           "Normal maps are stored linear (BC5 on desktop); other textures are sRGB color.");
    }

    // Color space. One decision for cook and runtime alike; normal maps are always Linear.
    Widgets::ItemLabel("Color Space");
    {
        const bool colorSpaceLocked = cachedParams_.textureType_ == TextureImportType::NormalMap;
        ui::BeginDisabled(colorSpaceLocked);
        Variant value{static_cast<int>(colorSpaceLocked ? TextureImportColorSpace::Linear : cachedParams_.colorSpace_)};
        if (Widgets::EditVariant(value, Widgets::EditVariantOptions{}.Enum(colorSpaceNames)))
        {
            const auto newColorSpace = static_cast<TextureImportColorSpace>(value.GetInt());
            SaveParamsForSelection(
                [&](TextureImporterParams& params) { params.colorSpace_ = newColorSpace; });
            cachedParams_.colorSpace_ = newColorSpace;
        }
        ui::EndDisabled();
        if (ui::IsItemHovered())
            ui::SetTooltip("sRGB for color content, Linear for data such as masks or LUTs.\n"
                           "Picks the cooked format variant and the runtime sampling path.");
    }

    // Mipmaps. The runtime can only drop baked levels, never regenerate them.
    Widgets::ItemLabel("Mipmaps");
    {
        Variant value{static_cast<int>(cachedParams_.mipmapMode_)};
        if (Widgets::EditVariant(value, Widgets::EditVariantOptions{}.Enum(mipmapModeNames)))
        {
            const auto newMode = static_cast<TextureImportMipmapMode>(value.GetInt());
            SaveParamsForSelection([&](TextureImporterParams& params) { params.mipmapMode_ = newMode; });
            cachedParams_.mipmapMode_ = newMode;
        }
        if (ui::IsItemHovered())
            ui::SetTooltip("Mipmaps are baked into the cooked file; the runtime can only drop them,\n"
                           "never regenerate them. 'Inherit' follows the build profile setting.");
    }

    // Sampler overrides. 'Default' leaves the engine default in place.
    Widgets::ItemLabel("Filter Mode");
    {
        const int currentFilter = cachedParams_.filterMode_ ? 1 + static_cast<int>(*cachedParams_.filterMode_) : 0;
        Variant value{currentFilter};
        if (Widgets::EditVariant(value, Widgets::EditVariantOptions{}.Enum(filterModeNames)))
        {
            const int index = value.GetInt();
            const ea::optional<TextureFilterMode> newFilter = index <= 0
                ? ea::optional<TextureFilterMode>{}
                : ea::optional<TextureFilterMode>{static_cast<TextureFilterMode>(index - 1)};
            SaveParamsForSelection([&](TextureImporterParams& params) { params.filterMode_ = newFilter; });
            cachedParams_.filterMode_ = newFilter;
        }
    }

    Widgets::ItemLabel("U Address");
    {
        const int currentAddress = cachedParams_.addressModeU_ ? 1 + static_cast<int>(*cachedParams_.addressModeU_) : 0;
        Variant value{currentAddress};
        if (Widgets::EditVariant(value, Widgets::EditVariantOptions{}.Enum(addressModeNames)))
        {
            const int index = value.GetInt();
            const ea::optional<TextureAddressMode> newMode = index <= 0
                ? ea::optional<TextureAddressMode>{}
                : ea::optional<TextureAddressMode>{static_cast<TextureAddressMode>(index - 1)};
            SaveParamsForSelection([&](TextureImporterParams& params) { params.addressModeU_ = newMode; });
            cachedParams_.addressModeU_ = newMode;
        }
    }

    Widgets::ItemLabel("V Address");
    {
        const int currentAddress = cachedParams_.addressModeV_ ? 1 + static_cast<int>(*cachedParams_.addressModeV_) : 0;
        Variant value{currentAddress};
        if (Widgets::EditVariant(value, Widgets::EditVariantOptions{}.Enum(addressModeNames)))
        {
            const int index = value.GetInt();
            const ea::optional<TextureAddressMode> newMode = index <= 0
                ? ea::optional<TextureAddressMode>{}
                : ea::optional<TextureAddressMode>{static_cast<TextureAddressMode>(index - 1)};
            SaveParamsForSelection([&](TextureImporterParams& params) { params.addressModeV_ = newMode; });
            cachedParams_.addressModeV_ = newMode;
        }
    }
}

} // namespace Urho3D

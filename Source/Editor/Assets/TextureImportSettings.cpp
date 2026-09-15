//
// Copyright (c) 2017-2020 the rbfx project.
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

#include "../Assets/TextureImportSettings.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Resource/XMLFile.h>

namespace Urho3D
{

const char* TextureImportTypeToString(TextureImportType type)
{
    switch (type)
    {
    case TextureImportType::NormalMap:
        return "NormalMap";
    case TextureImportType::Default:
    default:
        return "Default";
    }
}

TextureImportType TextureImportTypeFromString(ea::string_view value)
{
    if (value == "NormalMap")
        return TextureImportType::NormalMap;
    return TextureImportType::Default;
}

const char* TextureImportColorSpaceToString(TextureImportColorSpace colorSpace)
{
    switch (colorSpace)
    {
    case TextureImportColorSpace::Linear:
        return "Linear";
    case TextureImportColorSpace::SRGB:
    default:
        return "SRGB";
    }
}

TextureImportColorSpace TextureImportColorSpaceFromString(ea::string_view value)
{
    if (value == "Linear")
        return TextureImportColorSpace::Linear;
    return TextureImportColorSpace::SRGB;
}

const char* TextureImportMipmapModeToString(TextureImportMipmapMode mode)
{
    switch (mode)
    {
    case TextureImportMipmapMode::Enabled:
        return "Enabled";
    case TextureImportMipmapMode::Disabled:
        return "Disabled";
    case TextureImportMipmapMode::Inherit:
    default:
        return "Inherit";
    }
}

TextureImportMipmapMode TextureImportMipmapModeFromString(ea::string_view value)
{
    if (value == "Enabled")
        return TextureImportMipmapMode::Enabled;
    if (value == "Disabled")
        return TextureImportMipmapMode::Disabled;
    return TextureImportMipmapMode::Inherit;
}

namespace
{

// These tokens must match the parser in Texture::SetParameters verbatim, so the metadata file is
// both the editor's import settings and the runtime's parameter file - one file, one truth.
const char* const filterModeXmlTokens[]{
    "nearest",
    "bilinear",
    "trilinear",
    "anisotropic",
    "nearestanisotropic",
    "default",
};
const char* const addressModeXmlTokens[]{
    "wrap",
    "mirror",
    "clamp",
};

template <typename T, size_t N>
ea::optional<T> ParseSamplerToken(const ea::string& token, const char* const (&tokens)[N])
{
    for (size_t i = 0; i < N; ++i)
    {
        if (token == tokens[i])
            return static_cast<T>(i);
    }
    return {};
}

} // namespace

bool TextureImporterParams::IsDefault() const
{
    return textureType_ == TextureImportType::Default
        && colorSpace_ == TextureImportColorSpace::SRGB
        && mipmapMode_ == TextureImportMipmapMode::Inherit
        && !filterMode_
        && !addressModeU_
        && !addressModeV_;
}

ea::string GetTextureMetaFileName(const ea::string& sourceFileName)
{
    return ReplaceExtension(sourceFileName, ".texmeta");
}

bool LoadTextureImporterParams(Context* context, const ea::string& sourceFileName, TextureImporterParams& params)
{
    params = {};

    auto* fs = context->GetSubsystem<FileSystem>();
    if (!fs)
        return false;
    const ea::string metaFileName = GetTextureMetaFileName(sourceFileName);
    if (!fs->FileExists(metaFileName))
        return false;

    XMLFile xmlFile(context);
    if (!xmlFile.LoadFile(metaFileName))
    {
        URHO3D_LOGERROR("Could not parse the texture metadata file '{}'.", metaFileName);
        return false;
    }

    const XMLElement rootElem = xmlFile.GetRoot();
    if (rootElem.IsNull())
        return false;

    // <import> carries the editor-only decisions the runtime has no use for; SetParameters skips
    // the unknown element, the cook and the inspector read it here.
    const XMLElement importElem = rootElem.GetChild("import");
    if (!importElem.IsNull())
    {
        if (importElem.HasAttribute("type"))
            params.textureType_ = TextureImportTypeFromString(importElem.GetAttribute("type"));
        if (importElem.HasAttribute("mipmaps"))
            params.mipmapMode_ = TextureImportMipmapModeFromString(importElem.GetAttribute("mipmaps"));
    }

    // Color space: exactly one flag element, whichever the file carries. Neither means sRGB,
    // the default for an ordinary color texture.
    const XMLElement linearElem = rootElem.GetChild("linear");
    if (!linearElem.IsNull() && linearElem.GetBool("enable"))
        params.colorSpace_ = TextureImportColorSpace::Linear;
    else
    {
        const XMLElement srgbElem = rootElem.GetChild("srgb");
        if (!srgbElem.IsNull() && srgbElem.GetBool("enable"))
            params.colorSpace_ = TextureImportColorSpace::SRGB;
    }

    // When the import element does not spell the mip mode out, the runtime element still tells
    // the one thing that matters: a disabled mip chain means "never bake one".
    if (importElem.IsNull() || !importElem.HasAttribute("mipmaps"))
    {
        const XMLElement mipmapElem = rootElem.GetChild("mipmap");
        if (!mipmapElem.IsNull() && mipmapElem.HasAttribute("enable") && !mipmapElem.GetBool("enable"))
            params.mipmapMode_ = TextureImportMipmapMode::Disabled;
    }

    const XMLElement filterElem = rootElem.GetChild("filter");
    if (!filterElem.IsNull())
        params.filterMode_ = ParseSamplerToken<TextureFilterMode>(filterElem.GetAttributeLower("mode"), filterModeXmlTokens);

    for (XMLElement addressElem = rootElem.GetChild("address"); !addressElem.IsNull();
         addressElem = addressElem.GetNext("address"))
    {
        const ea::optional<TextureAddressMode> mode =
            ParseSamplerToken<TextureAddressMode>(addressElem.GetAttributeLower("mode"), addressModeXmlTokens);
        if (!mode)
            continue;
        const ea::string coord = addressElem.GetAttributeLower("coord");
        if (coord == "u")
            params.addressModeU_ = mode;
        else if (coord == "v")
            params.addressModeV_ = mode;
    }

    // A normal map is data: whatever the file claims, it cooks and samples linear.
    if (params.textureType_ == TextureImportType::NormalMap)
        params.colorSpace_ = TextureImportColorSpace::Linear;

    return true;
}

bool SaveTextureImporterParams(Context* context, const ea::string& sourceFileName, const TextureImporterParams& params)
{
    auto* fs = context->GetSubsystem<FileSystem>();
    if (!fs)
        return false;
    const ea::string metaFileName = GetTextureMetaFileName(sourceFileName);

    // Normalize first: a normal map is always linear, whatever a stale panel state claims.
    TextureImporterParams normalized = params;
    if (normalized.textureType_ == TextureImportType::NormalMap)
        normalized.colorSpace_ = TextureImportColorSpace::Linear;

    // Default params need no metadata file; one left over from earlier edits is removed again.
    if (normalized.IsDefault())
        return !fs->FileExists(metaFileName) || fs->Delete(metaFileName);

    // Keep what is already there: the file is the user's, and elements this panel does not
    // manage (a hand-added <quality>, say) survive a round-trip untouched.
    XMLFile xmlFile(context);
    XMLElement rootElem;
    if (fs->FileExists(metaFileName))
    {
        if (!xmlFile.LoadFile(metaFileName))
            return false;
        rootElem = xmlFile.GetRoot();
    }
    if (rootElem.IsNull())
        rootElem = xmlFile.CreateRoot("texture");

    rootElem.RemoveChildren("import");
    rootElem.RemoveChildren("srgb");
    rootElem.RemoveChildren("linear");
    rootElem.RemoveChildren("mipmap");
    rootElem.RemoveChildren("filter");
    rootElem.RemoveChildren("address");

    if (normalized.textureType_ != TextureImportType::Default
        || normalized.mipmapMode_ != TextureImportMipmapMode::Inherit)
    {
        XMLElement importElem = rootElem.CreateChild("import");
        if (normalized.textureType_ != TextureImportType::Default)
            importElem.SetAttribute("type", TextureImportTypeToString(normalized.textureType_));
        if (normalized.mipmapMode_ != TextureImportMipmapMode::Inherit)
            importElem.SetAttribute("mipmaps", TextureImportMipmapModeToString(normalized.mipmapMode_));
    }

    // Exactly one color space flag, never both - this is the single decision the engine's
    // shader compositor would otherwise warn about ("cannot be both sRGB and Linear").
    XMLElement colorSpaceElem = rootElem.CreateChild(
        normalized.colorSpace_ == TextureImportColorSpace::Linear ? "linear" : "srgb");
    colorSpaceElem.SetBool("enable", true);

    // The mip chain is baked into the cooked file; the element only tells the uncompressed
    // preview to match what ships (SetNumLevels(1)) instead of building a runtime chain.
    if (normalized.mipmapMode_ == TextureImportMipmapMode::Disabled)
    {
        XMLElement mipmapElem = rootElem.CreateChild("mipmap");
        mipmapElem.SetBool("enable", false);
    }

    if (normalized.filterMode_)
    {
        XMLElement filterElem = rootElem.CreateChild("filter");
        filterElem.SetAttribute("mode", filterModeXmlTokens[static_cast<unsigned>(*normalized.filterMode_)]);
    }
    if (normalized.addressModeU_)
    {
        XMLElement addressElem = rootElem.CreateChild("address");
        addressElem.SetAttribute("coord", "u");
        addressElem.SetAttribute("mode", addressModeXmlTokens[static_cast<unsigned>(*normalized.addressModeU_)]);
    }
    if (normalized.addressModeV_)
    {
        XMLElement addressElem = rootElem.CreateChild("address");
        addressElem.SetAttribute("coord", "v");
        addressElem.SetAttribute("mode", addressModeXmlTokens[static_cast<unsigned>(*normalized.addressModeV_)]);
    }

    return xmlFile.SaveFile(metaFileName);
}

} // namespace Urho3D

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

#pragma once

#include <Urho3D/RenderAPI/RenderAPIDefs.h>

#include <EASTL/optional.h>
#include <EASTL/string.h>

namespace Urho3D
{

class Context;

/// Editor-only classification of a texture source file.
/// Persisted as an attribute of the <import> element in the per-file metadata file
/// ("<source file>.texmeta") and consumed at build time to choose the platform compression
/// format and color space. Not read at runtime: the decision is baked into the cooked texture
/// itself (format + sRGB/linear flag).
/// Named apart from the runtime Urho3D::TextureType (GPU texture dimension) it must not collide with.
enum class TextureImportType
{
    /// Ordinary color texture. Treated as sRGB. Compressed as BC1/BC3 (desktop) or ETC2_RGB/ETC2_RGBA (mobile).
    Default = 0,
    /// Normal map. Treated as linear (never sRGB). Compressed as BC5 (desktop) or ETC2_RGBA (mobile).
    NormalMap,
};

/// Stable tokens used for the "type" attribute. Unknown tokens map back to Default.
const char* TextureImportTypeToString(TextureImportType type);
/// Parse a "type" attribute. Anything unrecognized maps to Default.
TextureImportType TextureImportTypeFromString(ea::string_view value);

/// Color space the texture content is stored in. A single decision that replaces the engine's
/// mutually exclusive sRGB/linear flags: the cook picks the format variant from it and the
/// metadata file sets the matching flag - never both.
enum class TextureImportColorSpace
{
    /// Ordinary color content, sampled through an sRGB variant of the GPU format.
    SRGB = 0,
    /// Data content (normal maps, masks, LUTs, heightmaps), sampled as-is.
    Linear,
};

/// Whether a mip chain is baked into the cooked texture. Hardware formats can only drop baked
/// levels at runtime, never regenerate them, so mip generation is a cook-time decision by nature.
enum class TextureImportMipmapMode
{
    /// Follow the build platform setting (TextureCompressionSettings::mipmaps_).
    Inherit = 0,
    /// Always bake a full mip chain.
    Enabled,
    /// Bake a single level. Saves the ~33% of file size and VRAM a full chain costs, for textures
    /// that are always sampled near 1:1 (UI art, lookup tables).
    Disabled,
};

/// Stable tokens used for the "mipmaps" attribute. Unknown tokens map back to the defaults.
/// @{
const char* TextureImportColorSpaceToString(TextureImportColorSpace colorSpace);
TextureImportColorSpace TextureImportColorSpaceFromString(ea::string_view value);
const char* TextureImportMipmapModeToString(TextureImportMipmapMode mode);
TextureImportMipmapMode TextureImportMipmapModeFromString(ea::string_view value);
/// @}

/// Per-file texture import parameters, stored as "<source file>.texmeta" - a single XML file that
/// doubles as the engine's runtime parameter file (Texture::SetParameters reads the very same
/// elements) and as the editor's import settings. There is no second file to keep in sync.
struct TextureImporterParams
{
    /// Texture classification. Defaults to Default (a normal map is an explicit opt-in).
    TextureImportType textureType_{TextureImportType::Default};
    /// Color space of the content. Normal maps are always Linear; ordinary textures default to SRGB.
    TextureImportColorSpace colorSpace_{TextureImportColorSpace::SRGB};
    /// Mip chain baking. Inherit keeps the build platform in charge.
    TextureImportMipmapMode mipmapMode_{TextureImportMipmapMode::Inherit};

    /// Sampler overrides. Absent values keep the engine defaults.
    /// @{
    ea::optional<TextureFilterMode> filterMode_;
    ea::optional<TextureAddressMode> addressModeU_;
    ea::optional<TextureAddressMode> addressModeV_;
    /// @}

    /// True when nothing deviates from the engine defaults, so no metadata file is needed
    /// beside the texture.
    bool IsDefault() const;
};

/// Return the metadata file name for the given absolute source file name
/// ("Kachujin_normal.png" -> "Kachujin_normal.texmeta").
ea::string GetTextureMetaFileName(const ea::string& sourceFileName);
/// Load params from "<source file>.texmeta". A missing file yields defaults and returns false;
/// a malformed one logs and yields defaults rather than failing the caller.
/// Returns true if a metadata file was successfully read.
bool LoadTextureImporterParams(Context* context, const ea::string& sourceFileName, TextureImporterParams& params);
/// Save params to "<source file>.texmeta". Default params remove the file instead. Elements the
/// panel does not manage (a hand-added <quality> for instance) are preserved in an existing file.
/// Returns true on success.
bool SaveTextureImporterParams(Context* context, const ea::string& sourceFileName, const TextureImporterParams& params);

} // namespace Urho3D

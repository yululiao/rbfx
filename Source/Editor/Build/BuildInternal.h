// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#pragma once

#include <EASTL/string.h>

#include <cctype>
#include <cstring>

namespace Urho3D
{

// String helpers shared by more than one translation unit of the build pipeline. They stay out of
// BuildPlatform.h because they are an implementation detail of the steps, and out of any single .cpp
// because both the export step and the texture step reach for ExtensionIs; keeping one definition is
// what stops the two from matching file names against different rules.

/// Case insensitive extension test. Package entry names keep the case of the file on disk, so
/// comparing them to a literal would make the check depend on how somebody named a folder.
inline bool ExtensionIs(const ea::string& name, const char* lowerCaseExtension)
{
    const size_t dot = name.rfind('.');
    if (dot == ea::string::npos)
        return false;
    const size_t length = name.size() - dot - 1;
    if (length != strlen(lowerCaseExtension))
        return false;
    for (size_t i = 0; i < length; ++i)
    {
        if (tolower(name[dot + 1 + i]) != lowerCaseExtension[i])
            return false;
    }
    return true;
}

/// Texture sources the compressor reads. Already-cooked containers (.dds/.ktx/.pvr) are deliberately
/// absent: they are output rather than input, and running them through the tool again would only
/// discard quality a previous cook already spent.
inline bool IsTextureSourceFile(const ea::string& name)
{
    return ExtensionIs(name, "png") || ExtensionIs(name, "jpg") || ExtensionIs(name, "jpeg") ||
        ExtensionIs(name, "bmp") || ExtensionIs(name, "tga");
}

} // namespace Urho3D

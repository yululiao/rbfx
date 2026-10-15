//
// Copyright (c) 2017-2026 the rbfx project.
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

#include <EASTL/optional.h>
#include <EASTL/string.h>

namespace Urho3D
{

class Context;

/// Per-file FBX import parameters, stored as "<source file>.fbxmeta" (for example
/// "Characters_Anne.fbx.fbxmeta") - a single XML file next to the source asset. The editor
/// Inspector writes it, and the import pipeline reads it back on every (re)import and merges
/// the values into the importer command line. Because the file lives beside the FBX and outlives
/// the Cache, an FBX file updated on disk keeps its non-default import settings instead of being
/// silently reimported with defaults.
struct FbxImportSettings
{
    /// Simplify model geometry on import (meshopt_simplify). Defaults to false: no metadata
    /// file is written until the user opts in.
    bool simplify_{false};
    /// Target triangle ratio when simplify_ is enabled, kept within (0, 1) on both save and load.
    float simplifyRatio_{0.25f};

    /// True when nothing deviates from the defaults, so no metadata file is needed beside the FBX.
    bool IsDefault() const;
};

/// Return the metadata file name for the given absolute source file name
/// ("Characters_Anne.fbx" -> "Characters_Anne.fbx.fbxmeta").
ea::string GetFbxMetaFileName(const ea::string& sourceFileName);
/// Load params from "<source file>.fbxmeta". A missing file yields defaults and returns false;
/// a malformed one logs and yields defaults rather than failing the caller.
/// Returns true if a metadata file was successfully read.
bool LoadFbxImportSettings(Context* context, const ea::string& sourceFileName, FbxImportSettings& params);
/// Save params to "<source file>.fbxmeta". Default params remove the file instead. Attributes the
/// panel does not manage survive a round-trip untouched. Returns true on success.
bool SaveFbxImportSettings(Context* context, const ea::string& sourceFileName, const FbxImportSettings& params);

} // namespace Urho3D

//
// Copyright (c) 2017-2025 the rbfx project.
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

#include <Urho3D/Container/Str.h>

namespace Urho3D
{

class Context;
class Project;

/// Import single FBX file in-process using AssetImporter library.
/// File name containing '@' is imported as Animation (.ani), otherwise as Model (.mdl).
/// Output is placed next to the source file. Returns true on success.
bool ImportFbxFile(Project* project, const ea::string& fileName);

/// Import all FBX files found in given directory (recursively).
/// Returns number of successfully imported files.
unsigned ImportFbxFilesInDirectory(Project* project, const ea::string& directoryName);

/// Register FBX import settings page (assimp/ufbx backend selection).
void Assets_FbxImportSettings(Context* context, Project* project);

}

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

/// Import single FBX file in-process. Whether the file is a model, an animation, or both is
/// auto-detected by the importer (no '@' file-name convention anymore). Output is written into the
/// Cache satellite "<resourceName>.d/" exactly like the automatic asset pipeline, so the generated
/// runtime-format resources stay transparent to the user. Returns true on success.
bool ImportFbxFile(Project* project, const ea::string& fileName);

/// Import FBX 'fileName' with the ufbx backend into the satellite output directory 'outSatelliteDir',
/// which receives the auto-detected "Models/" and/or "Animations/" sub-trees (and "Materials/" unless
/// disabled). On success, when 'outContent' is non-null it receives the AssetImporterContentType bitmask
/// describing what the FBX actually contained. Shared by the automatic asset pipeline (ModelImporter) and
/// the manual inspector import so the two paths cannot drift. Returns true on success.
bool ImportFbxToSatellite(Project* project, const ea::string& fileName, const ea::string& outSatelliteDir,
    unsigned* outContent = nullptr);

/// Import all FBX files found in given directory (recursively).
/// Returns number of successfully imported files.
unsigned ImportFbxFilesInDirectory(Project* project, const ea::string& directoryName);

}

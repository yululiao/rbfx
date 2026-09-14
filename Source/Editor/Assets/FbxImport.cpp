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

#include "../Assets/FbxImport.h"

#include "../Project/Project.h"

#include <Tools/AssetImporter/AssetImporterLibrary.h>

#include <Urho3D/Core/StringUtils.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/Log.h>

#include <EASTL/sort.h>

namespace Urho3D
{

namespace
{

// Marshals arguments to the AssetImporter C API and reports failures uniformly. Not
// concurrent-safe (AssetImporterRun reuses the process-wide Context singleton); the default
// editor asset pipeline processes transformers inline on the main thread, so calls are already
// serialized. A host that injects an asynchronous process callback must serialize them itself.
bool RunAssetImporter(const ea::string& fileName, const StringVector& arguments)
{
    ea::vector<const char*> argv;
    argv.reserve(arguments.size());
    for (const ea::string& argument : arguments)
        argv.push_back(argument.c_str());

    // Import in-process through the AssetImporter library, so import failures
    // can be debugged directly in the Editor debugger session
    if (AssetImporterRun(static_cast<int>(argv.size()), argv.data()) != 0)
    {
        URHO3D_LOGERROR("Failed to import FBX file '{}':\n{}", fileName, AssetImporterGetLastError());
        return false;
    }
    return true;
}

}

bool ImportFbxToSatellite(Project* project, const ea::string& fileName, const ea::string& outSatelliteDir,
    unsigned* outContent)
{
    auto fs = project->GetContext()->GetSubsystem<FileSystem>();
    if (!fs->FileExists(fileName))
    {
        URHO3D_LOGERROR("Cannot import '{}': file not found", fileName);
        return false;
    }

    // Single content-driven import: the importer parses the FBX once and writes "Models/" and/or
    // "Animations/" under the satellite directory based on what the file actually contains. "-nm -nt"
    // keep the output to a bare model (+ its animations) with no material/texture files, matching the
    // previous pipeline behavior.
    StringVector arguments{"import", fileName, outSatelliteDir, "-nm", "-nt"};
    if (!RunAssetImporter(fileName, arguments))
        return false;

    if (outContent)
        *outContent = AssetImporterGetLastImportContent();

    URHO3D_LOGINFO("Imported FBX file '{}'", fileName);
    return true;
}

bool ImportFbxFile(Project* project, const ea::string& fileName)
{
    auto fs = project->GetContext()->GetSubsystem<FileSystem>();
    if (!fs->FileExists(fileName))
    {
        URHO3D_LOGERROR("Cannot import '{}': file not found", fileName);
        return false;
    }

    // Mirror the automatic pipeline (ModelImporter::ImportFBXEmbedded): resolve the resource name
    // relative to the Data/ root, then import into the Cache satellite "<resourceName>.d/". The importer
    // auto-detects model vs animation content, so there is no '@' name check here and the generated
    // runtime-format files never land next to the source in Data/.
    const ea::string dataPath = AddTrailingSlash(project->GetDataPath());
    ea::string resourceName = fileName;
    if (resourceName.starts_with(dataPath))
        resourceName = resourceName.substr(dataPath.size());

    const ea::string satelliteDir = AddTrailingSlash(project->GetCachePath() + resourceName + ".d");
    return ImportFbxToSatellite(project, fileName, satelliteDir);
}

unsigned ImportFbxFilesInDirectory(Project* project, const ea::string& directoryName)
{
    auto context = project->GetContext();
    auto fs = context->GetSubsystem<FileSystem>();

    if (!fs->DirExists(directoryName))
    {
        URHO3D_LOGERROR("Cannot import FBX files from '{}': directory not found", directoryName);
        return 0;
    }

    StringVector fileNames;
    fs->ScanDir(fileNames, directoryName, "*.fbx", SCAN_FILES | SCAN_RECURSIVE);
    ea::sort(fileNames.begin(), fileNames.end());

    const ea::string directoryWithSlash = AddTrailingSlash(directoryName);

    unsigned numImported = 0;
    unsigned numFailed = 0;
    for (const ea::string& relativeName : fileNames)
    {
        if (ImportFbxFile(project, directoryWithSlash + relativeName))
            ++numImported;
        else
            ++numFailed;
    }

    if (numImported == 0 && numFailed == 0)
        URHO3D_LOGWARNING("No FBX files found in '{}'", directoryName);
    else if (numFailed > 0)
        URHO3D_LOGWARNING("Imported {} of {} FBX files from '{}'", numImported, numImported + numFailed, directoryName);
    else
        URHO3D_LOGINFO("Imported {} FBX files from '{}'", numImported, directoryName);

    return numImported;
}

}

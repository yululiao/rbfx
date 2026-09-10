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

#include "../Core/SettingsManager.h"
#include "../Project/Project.h"

#include <Tools/AssetImporter/AssetImporterLibrary.h>

#include <Urho3D/Core/StringUtils.h>
#include <Urho3D/IO/ArchiveSerialization.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/SystemUI/SystemUI.h>

#include <EASTL/sort.h>

namespace Urho3D
{

namespace
{

struct FbxImportSettings
{
    ea::string GetUniqueName() { return "Editor.Assets:FbxImport"; }

    void SerializeInBlock(Archive& archive)
    {
        SerializeOptionalValue(archive, "UseUfbxBackend", useUfbxBackend_, FbxImportSettings{}.useUfbxBackend_);
    }

    void RenderSettings()
    {
        ui::Checkbox("Use ufbx backend", &useUfbxBackend_);
        ui::TextDisabled("FBX files are imported with assimp when disabled (default).");
    }

    bool useUfbxBackend_{true};
};
using FbxImportSettingsPage = SimpleSettingsPage<FbxImportSettings>;

bool ShouldUseUfbxBackend(Project* project)
{
    const auto settingsManager = project->GetSettingsManager();
    if (!settingsManager)
        return false;

    const auto page = dynamic_cast<FbxImportSettingsPage*>(settingsManager->FindPage("Editor.Assets:FbxImport"));
    return page && page->GetValues().useUfbxBackend_;
}

}

void Assets_FbxImportSettings(Context* context, Project* project)
{
    const auto settingsManager = project->GetSettingsManager();
    settingsManager->AddPage(MakeShared<FbxImportSettingsPage>(context));
}

bool ImportFbxFile(Project* project, const ea::string& fileName)
{
    auto context = project->GetContext();
    auto fs = context->GetSubsystem<FileSystem>();

    if (!fs->FileExists(fileName))
    {
        URHO3D_LOGERROR("Cannot import '{}': file not found", fileName);
        return false;
    }

    const ea::string baseName = GetFileName(fileName);
    const ea::string directoryName = GetPath(fileName);

    StringVector arguments;
    if (baseName.contains('@'))
        arguments = {"anim", fileName, directoryName + baseName + ".ani"};
    else
        arguments = {"model", fileName, directoryName + baseName + ".mdl", "-nm", "-nt"};

    // Backend selection: assimp by default, ufbx when enabled in the settings
    if (ShouldUseUfbxBackend(project))
        arguments.push_back("-ufbx");

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

    URHO3D_LOGINFO("Imported FBX file '{}'", fileName);
    return true;
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

// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

// The platform-agnostic data stages, each a BuildStep: validate, wait for cooking, clean and recreate
// the output, merge the Data/ trees into staging, compile and prune scripts, pack the resources and
// report. They reach the resolved paths, platform, error list and process runner through the owning
// BuildPlatform. Bodies are the pipeline's own, moved verbatim into the step types.

#include "../../Assets/TextureImportSettings.h"
#include "../BuildInternal.h"
#include "../BuildPlatform.h"
#include "../BuildSettings.h"
#include "BuildSteps.h"
#include "../../Project/AssetManager.h"
#include "../../Project/Project.h"

#include <Urho3D/Core/StringUtils.h>
#include <Urho3D/IO/File.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/IO/PackageFile.h>

#include <EASTL/algorithm.h>

#include <cstdlib>

namespace Urho3D
{

namespace
{

/// Copy a directory tree on top of another one. FileSystem::CopyDir is the merge this needs - it
/// creates missing directories and opens destinations for writing, so a later copy replaces what an
/// earlier one put there. It reports success for a source that does not exist though, which cannot
/// be told apart from an empty directory, so the existence check happens here.
bool MergeDirectory(FileSystem* fs, const ea::string& source, const ea::string& destination, ea::string& message)
{
    if (!fs->DirExists(source))
    {
        message = Format("Nothing to copy from '{}': the directory does not exist.", source);
        return false;
    }
    if (!fs->CopyDir(source, destination))
    {
        message = Format("Failed to copy '{}' into '{}'.", source, destination);
        return false;
    }
    return true;
}

} // namespace

bool ValidateStep::Run(ea::string& message)
{
    auto* fs = owner_.context()->GetSubsystem<FileSystem>();
    auto* settings = owner_.GetSettings();
    auto* project = owner_.context()->GetSubsystem<Project>();
    const BuildPlatformData* platform = owner_.platform();
    ea::vector<ea::string>& errors = owner_.errors();

    errors.clear();
    settings->Validate(*platform, errors);

    // Two facts a platform cannot speak for, because they are about the project rather than about
    // the toolchain: that there is a data tree at all, and that it names an entry point. Without
    // Game.json the shipped game starts, prints a warning and shows an empty scene, which a user
    // will read as "the build is broken" instead of "the project is incomplete".
    const ea::string dataPath = NormalizeDir(project->GetDataPath());
    if (!fs->DirExists(dataPath))
        errors.push_back(Format("The project has no data directory: '{}'.", dataPath));
    else if (!fs->FileExists(dataPath + "Game.json"))
    {
        errors.push_back(Format("'{}' has no Game.json. The shipped game would not know which scene "
            "to load or which script to run.", dataPath));
    }

    // The output directory is emptied at the start of a build, so a mistyped path must not be
    // allowed to cost somebody a source tree or a volume.
    const ea::string& outputDir = owner_.outputDir();
    const ea::string output = RemoveTrailingSlash(outputDir);
    const ea::string parent = RemoveTrailingSlash(GetPath(output));
    if (parent.empty() || parent == output)
    {
        errors.push_back(Format("Refusing to build into '{}' because the whole directory is deleted "
            "first. Point OutputDir at a dedicated build directory.", outputDir));
    }
    else
    {
        const ea::string protectedDirs[] = {
            RemoveTrailingSlash(ForwardSlashes(project->GetProjectPath())),
            RemoveTrailingSlash(ForwardSlashes(platform->engineData_)),
            RemoveTrailingSlash(ForwardSlashes(platform->engineBin_)),
        };
        for (const ea::string& protectedDir : protectedDirs)
        {
            if (!protectedDir.empty() && output == protectedDir)
            {
                errors.push_back(Format("Refusing to empty '{}' because a build reads from it.", outputDir));
                break;
            }
        }
    }

    if (errors.empty())
        return true;

    for (const ea::string& error : errors)
        URHO3D_LOGERROR("[Build] {}", error);
    message = Format("Platform '{}' is not buildable, {} problem(s) above.", owner_.GetPlatformName(), errors.size());
    return false;
}

bool AwaitAssetsStep::Run(ea::string& message)
{
    auto* project = owner_.context()->GetSubsystem<Project>();
    auto* assetManager = project ? project->GetAssetManager() : nullptr;

    // Without an asset manager there is nothing cooking to wait for; the Cache simply contributes no
    // outputs and StageData copies only the Data/ trees.
    if (!assetManager)
        return true;

    if (assetManager->IsProcessing())
    {
        const auto progress = assetManager->GetProgress();
        URHO3D_LOGINFO("[Build] Waiting for asset cooking ({}/{})", progress.first, progress.second);
        // Hold on this step; the platform re-runs it next frame instead of advancing.
        owner_.HoldStep();
    }
    return true;
}

bool CleanOutputStep::Run(ea::string& message)
{
    auto* fs = owner_.context()->GetSubsystem<FileSystem>();
    const ea::string& outputDir = owner_.outputDir();

    if (fs->DirExists(outputDir) && !fs->RemoveDir(outputDir, true))
    {
        message = Format("Could not empty '{}'. A game launched from a previous build is the usual "
            "reason; close it and build again.", outputDir);
        return false;
    }
    if (!fs->CreateDirsRecursive(outputDir))
    {
        message = Format("Could not create '{}'.", outputDir);
        return false;
    }
    return true;
}

bool StageDataStep::Run(ea::string& message)
{
    auto* fs = owner_.context()->GetSubsystem<FileSystem>();
    auto* project = owner_.context()->GetSubsystem<Project>();
    const BuildPlatformData* platform = owner_.platform();

    const ea::string staged = owner_.stagingDir() + DataDirName;

    // Engine resources first, project resources on top: a project can then replace a single engine
    // file by carrying a file of the same name, without anybody editing the engine working tree.
    if (platform->includeEngineData_)
    {
        const ea::string engineData = AddTrailingSlash(ForwardSlashes(platform->engineData_)) + DataDirName;
        if (!MergeDirectory(fs, engineData, staged, message))
            return false;
    }
    if (!MergeDirectory(fs, NormalizeDir(project->GetDataPath()), staged, message))
        return false;

    // Imported assets cook their runtime-format products (the .mdl/.ani behind a source .fbx) into the
    // Cache satellite directories, not into Data/, and scenes reference those satellite names directly.
    // AwaitAssets already guaranteed cooking finished, so every output the manifest records must exist;
    // copying it into the staged tree under the same relative name is what makes the packaged resource
    // keys resolve at runtime. Last so a cooked output wins any name clash with a plain Data/ file.
    if (auto* assetManager = project->GetAssetManager())
    {
        const ea::string cachePath = AddTrailingSlash(project->GetCachePath());
        for (const ea::string& output : assetManager->GetAllCacheOutputs())
        {
            const ea::string source = cachePath + output;
            if (!fs->FileExists(source))
            {
                message = Format("Cooked output '{}' is recorded in the manifest but missing from the "
                    "Cache. Re-import the asset before shipping.", source);
                return false;
            }
            const ea::string destination = staged + output;
            if (!fs->CreateDirsRecursive(GetPath(destination)))
            {
                message = Format("Could not create the staging directory for '{}'.", destination);
                return false;
            }
            if (!fs->Copy(source, destination))
            {
                message = Format("Could not stage cooked output '{}'.", destination);
                return false;
            }
        }
    }
    return true;
}

bool CompileScriptsStep::Run(ea::string& message)
{
    auto* settings = owner_.GetSettings();
    const BuildPlatformData* platform = owner_.platform();

    const ea::string compiler = settings->FindTool(*platform, "LuaCompiler");
    if (compiler.empty())
    {
        message = "LuaCompiler disappeared between validation and this stage.";
        return false;
    }

    const ea::string staged = owner_.stagingDir() + DataDirName;

    // Input root and output root are the same directory on purpose: the tool mirrors every source
    // path onto itself with a .luc extension, so the containers land exactly where the resource
    // names in the scenes already point. Encrypting the whole staged tree rather than only the
    // project's folder is also what makes the "no plain source ships" check afterwards meaningful,
    // because engine samples carry .lua files too.
    ea::vector<ea::string> arguments;
    arguments.push_back("--recursive");
    arguments.push_back("--encrypt");
    arguments.push_back("--out=" + staged);

    if (!platform->scriptKeyEnvVar_.empty())
    {
        if (const char* key = getenv(platform->scriptKeyEnvVar_.c_str()); key && *key)
            arguments.push_back("--key=" + ea::string(key));
    }
    arguments.push_back(staged);

    return owner_.StartProcess(compiler, arguments,
        [this](ea::string& resumeMessage) { return PruneStagedSources(resumeMessage); }, message);
}

bool CompileScriptsStep::PruneStagedSources(ea::string& message)
{
    auto* fs = owner_.context()->GetSubsystem<FileSystem>();
    const ea::string staged = owner_.stagingDir() + DataDirName;

    ea::vector<ea::string> sources;
    fs->ScanDir(sources, staged, "*.lua", SCAN_FILES | SCAN_RECURSIVE);

    unsigned removed = 0;
    for (const ea::string& relative : sources)
    {
        const ea::string source = staged + relative;
        const size_t dot = source.rfind('.');
        const ea::string container = source.substr(0, dot) + ".luc";

        // A source without its compiled twin would leave the game without that script if the plain
        // file were deleted, and shipping both would defeat the encryption. Neither is acceptable,
        // so an incomplete compile fails the build instead of warning about it.
        if (!fs->FileExists(container))
        {
            message = Format("LuaCompiler produced no '{}' for '{}'.", container, source);
            return false;
        }
        if (!fs->Delete(source))
        {
            message = Format("Could not delete the plain script '{}'.", source);
            return false;
        }
        ++removed;
    }

    URHO3D_LOGINFO("[Build] Compiled scripts now ship as .luc; removed {} plain source file(s)", removed);
    return true;
}

bool StageCoreDataStep::Run(ea::string& message)
{
    auto* fs = owner_.context()->GetSubsystem<FileSystem>();
    const BuildPlatformData* platform = owner_.platform();
    const ea::string source = AddTrailingSlash(ForwardSlashes(platform->engineData_)) + CoreDataDirName;
    return MergeDirectory(fs, source, owner_.stagingDir() + CoreDataDirName, message);
}

bool ExportDataStep::Run(ea::string& message)
{
    auto* fs = owner_.context()->GetSubsystem<FileSystem>();
    auto* settings = owner_.GetSettings();
    const BuildPlatformData* platform = owner_.platform();

    const ea::string dirName = coreData_ ? CoreDataDirName : DataDirName;
    const ea::string packageName = coreData_ ? CoreDataPackageName : DataPackageName;
    const ea::string staged = owner_.stagingDir() + dirName;
    const ea::string& resourceDir = owner_.resourceDir();

    if (!platform->packData_)
    {
        if (!MergeDirectory(fs, staged, resourceDir + dirName, message))
            return false;
        return VerifyExportedResources(message);
    }

    const ea::string package = resourceDir + packageName;
    const ea::string tool = settings->FindTool(*platform, "PackageTool");
    if (tool.empty())
    {
        message = "PackageTool disappeared between validation and this stage.";
        return false;
    }

    // PackageTool leaves an existing package alone when it considers it up to date, comparing
    // timestamps against whatever is already there. A package that another platform wrote into the
    // same destination is exactly the artifact that must not survive that check, so it goes first.
    if (fs->FileExists(package) && !fs->Delete(package))
    {
        message = Format("Could not delete the stale package '{}'.", package);
        return false;
    }
    if (!fs->CreateDirsRecursive(GetPath(package)))
    {
        message = Format("Could not create '{}'.", GetPath(package));
        return false;
    }

    // No basepath argument: package entries have to be relative to the directory being packed,
    // which is what makes them match the resource names the runtime asks for.
    ea::vector<ea::string> arguments{ staged, package };
    if (platform->compressPackages_)
        arguments.push_back("-c");

    return owner_.StartProcess(tool, arguments,
        [this](ea::string& resumeMessage) { return VerifyExportedResources(resumeMessage); }, message);
}

bool ExportDataStep::VerifyExportedResources(ea::string& message)
{
    auto* fs = owner_.context()->GetSubsystem<FileSystem>();
    const BuildPlatformData* platform = owner_.platform();

    const ea::string dirName = coreData_ ? CoreDataDirName : DataDirName;
    const ea::string packageName = coreData_ ? CoreDataPackageName : DataPackageName;
    const ea::string& resourceDir = owner_.resourceDir();

    if (!platform->packData_)
    {
        // Loose directories: proving the entry configuration arrived is enough, because a copy that
        // partially failed already made CopyDir report failure.
        const ea::string entry = resourceDir + dirName + "Game.json";
        if (!coreData_ && !fs->FileExists(entry))
        {
            message = Format("'{}' is missing from the output.", entry);
            return false;
        }
        return true;
    }

    const ea::string package = resourceDir + packageName;

    // Read the result back with the same class the game will use, rather than asking the bundler to
    // describe its own output: this proves the header, the entry table and the data offsets all
    // agree, which is more than a tool printing a number it just computed.
    PackageFile reader(owner_.context());
    if (!reader.Open(package))
    {
        message = Format("'{}' was written but cannot be read back.", package);
        return false;
    }
    if (reader.GetNumFiles() == 0)
    {
        message = Format("'{}' contains no files.", package);
        return false;
    }
    URHO3D_LOGINFO("[Build] {} holds {} file(s), {} bytes, {}", packageName, reader.GetNumFiles(),
        reader.GetTotalDataSize(), reader.IsCompressed() ? "LZ4 compressed" : "uncompressed");

    if (coreData_)
        return true;

    if (!reader.Exists("Game.json"))
    {
        message = Format("'{}' does not contain Game.json, so the shipped game cannot find its entry "
            "configuration.", package);
        return false;
    }

    // Only a platform that encrypts has something to prove about script formats: for every other
    // build the plain .lua file IS the shipped artifact, and refusing it would reject the common
    // case. When encryption is on, a surviving source file is worse than a missing container - the
    // runtime prefers the .luc, so the source is dead weight that reads as an unprotected script.
    if (platform->encryptScripts_)
    {
        unsigned containers = 0;
        for (const auto& entry : reader.GetEntries())
        {
            if (ExtensionIs(entry.first, "luc"))
                ++containers;
            else if (ExtensionIs(entry.first, "lua"))
            {
                message = Format("'{}' still contains the plain script '{}'.", package, entry.first);
                return false;
            }
        }
        if (containers == 0)
        {
            message = Format("EncryptScripts is on but '{}' holds no .luc container.", package);
            return false;
        }
    }
    return true;
}

bool SummaryStep::Run(ea::string& message)
{
    auto* fs = owner_.context()->GetSubsystem<FileSystem>();
    const ea::string& outputDir = owner_.outputDir();

    ea::vector<ea::string> entries;
    fs->ScanDir(entries, outputDir, "*", SCAN_DIRS | SCAN_FILES);
    // A scan that includes directories also answers with '.' and '..', which are this very output
    // directory and the directory around it. Left in, the summary would report the package once as
    // its contents and once again as a directory containing them.
    entries.erase(ea::remove_if(entries.begin(), entries.end(),
                    [](const ea::string& entry) { return entry == "." || entry == ".."; }),
        entries.end());
    ea::sort(entries.begin(), entries.end());

    unsigned long long total = 0;
    for (const ea::string& entry : entries)
    {
        const ea::string path = outputDir + entry;
        const unsigned long long size = fs->DirExists(path) ? owner_.DirectorySize(path) : [&]()
        {
            File file(owner_.context(), path, FILE_READ);
            return file.IsOpen() ? static_cast<unsigned long long>(file.GetSize()) : 0ULL;
        }();
        total += size;
        URHO3D_LOGINFO("[Build]   {:>14} bytes  {}", size, entry);
    }

    URHO3D_LOGINFO("[Build]   {:>14} bytes  total, {} top level entr{} in {}", total, entries.size(),
        entries.size() == 1 ? "y" : "ies", outputDir);
    return true;
}

} // namespace Urho3D

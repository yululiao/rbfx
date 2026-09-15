// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#include "../../Assets/TextureImportSettings.h"
#include "../../Project/Build/AndroidScaffold.h"
#include "../../Project/Build/BuildSettings.h"
#include "../../Project/Build/BuildSystem.h"
#include "../../Project/AssetManager.h"
#include "../../Project/Project.h"

#include <Urho3D/Core/CoreEvents.h>
#include <Urho3D/Core/ProcessUtils.h>
#include <Urho3D/Core/StringUtils.h>
#include <Urho3D/Core/Timer.h>
#include <Urho3D/IO/File.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/IOEvents.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/IO/PackageFile.h>
#include <Urho3D/Resource/Image.h>

#include <EASTL/algorithm.h>

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace Urho3D
{

const StringHash E_BUILD_FINISHED("buildFinished");

namespace
{

/// Names the runtime mounts by. LuaGamePlayer sets EP_RESOURCE_PATHS to "CoreData;Data" and the
/// virtual file system looks for "<prefix>/Data.pak" before "<prefix>/Data/", so a package named
/// anything else is simply invisible to the game. Stated once here because every stage that places
/// a resource file has to agree on it.
const ea::string DataDirName = "Data/";
const ea::string CoreDataDirName = "CoreData/";
const ea::string DataPackageName = "Data.pak";
const ea::string CoreDataPackageName = "CoreData.pak";

/// Host binary the package is built from and the two shared libraries it cannot start without.
/// Missing any of the three yields a directory that looks finished and does nothing.
const ea::string HostName = "LuaGamePlayer";
const ea::string EngineLibraryName = "Urho3D";
const ea::string LuaLibraryName = "RbfxLuaScript";

ea::string ForwardSlashes(ea::string text)
{
    text.replace("\\", "/");
    return text;
}

ea::string NormalizeDir(const ea::string& path)
{
    return AddTrailingSlash(RemoveTrailingSlash(ForwardSlashes(path)));
}

/// Case insensitive extension test. Package entry names keep the case of the file on disk, so
/// comparing them to a literal would make the check depend on how somebody named a folder.
bool ExtensionIs(const ea::string& name, const char* lowerCaseExtension)
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

bool StartsMinusKey(const ea::string& text)
{
    return strncmp(text.c_str(), "--key=", 6) == 0;
}

/// Texture sources the compressor reads. Already-cooked containers (.dds/.ktx/.pvr) are deliberately
/// absent: they are output rather than input, and running them through the tool again would only
/// discard quality a previous cook already spent.
bool IsTextureSourceFile(const ea::string& name)
{
    return ExtensionIs(name, "png") || ExtensionIs(name, "jpg") || ExtensionIs(name, "jpeg") ||
        ExtensionIs(name, "bmp") || ExtensionIs(name, "tga");
}

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

const char* BuildStageName(BuildStage stage)
{
    switch (stage)
    {
    case BuildStage::Validate:
        return "Validate profile";
    case BuildStage::AwaitAssets:
        return "Wait for asset cooking";
    case BuildStage::CleanOutput:
        return "Clean output directory";
    case BuildStage::StageData:
        return "Stage Data";
    case BuildStage::CompressTextures:
        return "Compress textures";
    case BuildStage::CompileScripts:
        return "Compile Lua scripts";
    case BuildStage::StageCoreData:
        return "Stage CoreData";
    case BuildStage::ExportData:
        return "Export Data";
    case BuildStage::ExportCoreData:
        return "Export CoreData";
    case BuildStage::StageRuntime:
        return "Copy runtime binaries";
    case BuildStage::AndroidProject:
        return "Write Android project";
    case BuildStage::Summary:
        return "Summary";
    case BuildStage::Idle:
        break;
    }
    return "Idle";
}

BuildSystem::BuildSystem(Context* context)
    : Object(context)
{
    // BeginFrame is the clock of this state machine. The timer subsystem sends it even when the
    // engine is headless, which is what makes `Editor --build` work without a window.
    SubscribeToEvent(E_BEGINFRAME, URHO3D_HANDLER(BuildSystem, HandleBeginFrame));
    SubscribeToEvent(E_ASYNCEXECFINISHED, URHO3D_HANDLER(BuildSystem, HandleAsyncExecFinished));
}

BuildSettings* BuildSystem::GetSettings() const
{
    const auto project = GetSubsystem<Project>();
    return project ? project->GetBuildSettings() : nullptr;
}

bool BuildSystem::BuildNow(const ea::string& profileName, const ea::string& outputOverride, DoneHandler onDone)
{
    auto* fs = GetSubsystem<FileSystem>();
    auto* project = GetSubsystem<Project>();
    auto* settings = GetSettings();

    if (IsBuilding())
    {
        URHO3D_LOGERROR("[Build] Profile '{}' is still building", profileName_);
        return false;
    }
    if (!project || !settings)
    {
        URHO3D_LOGERROR("[Build] No project is loaded, there is nothing to build");
        return false;
    }

    profile_ = settings->FindProfile(profileName);
    if (!profile_)
    {
        ea::string known;
        for (const ea::string& name : settings->GetProfileNames())
        {
            if (!known.empty())
                known += ", ";
            known += name;
        }
        URHO3D_LOGERROR("[Build] No build profile named '{}' (known profiles: {})", profileName,
            known.empty() ? "none" : known.c_str());
        return false;
    }

    profileName_ = profileName;
    onDone_ = ea::move(onDone);
    errors_.clear();
    cancelRequested_ = false;
    pendingRequest_ = 0;
    processFinished_ = false;
    stageHold_ = false;
    stageResume_ = nullptr;
    progress_ = 0.0f;
    textureQueue_.clear();
    textureQueueIndex_ = 0;
    textureToolPath_.clear();
    texturesCompressed_ = 0;
    texturesCached_ = 0;

    if (outputOverride.empty())
        outputDir_ = profile_->ResolveOutputDir(project->GetProjectPath());
    else
    {
        ea::string resolved = ForwardSlashes(outputOverride);
        if (!IsAbsolutePath(resolved))
            resolved = NormalizeDir(fs->GetCurrentDir()) + resolved;
        outputDir_ = NormalizeDir(resolved);
    }

    // Resources are found by name, not by position, so the only difference between the two
    // platforms is where the mounted directories live: beside the executable on a desktop, inside
    // the assets folder of the generated gradle project on a phone.
    resourceDir_ = profile_->IsAndroid() ? outputDir_ + "assets/" : outputDir_;
    stagingDir_ = NormalizeDir(project->GetRandomTemporaryPath());

    plan_.clear();
    plan_.push_back(BuildStage::Validate);
    // Cook imported assets before anything reads Cache: this is the stage that guarantees the Cache
    // satellite outputs StageData copies are present and current. Placing it after Validate means the
    // first BeginFrame runs Validate while AssetManager::Initialize (driven from the render phase of
    // the same frame) enqueues cooking, so AwaitAssets sees real IsProcessing() state from frame two.
    plan_.push_back(BuildStage::AwaitAssets);
    plan_.push_back(BuildStage::CleanOutput);
    plan_.push_back(BuildStage::StageData);
    // Textures compress before scripts and export so every later stage sees the cooked tree it ships.
    if (profile_->textureCompression_.enabled_)
        plan_.push_back(BuildStage::CompressTextures);
    if (profile_->encryptScripts_)
        plan_.push_back(BuildStage::CompileScripts);
    plan_.push_back(BuildStage::StageCoreData);
    plan_.push_back(BuildStage::ExportData);
    plan_.push_back(BuildStage::ExportCoreData);
    if (profile_->IsAndroid())
        plan_.push_back(BuildStage::AndroidProject);
    else
        plan_.push_back(BuildStage::StageRuntime);
    plan_.push_back(BuildStage::Summary);

    stageIndex_ = 0;
    stage_ = plan_[0];
    startTime_ = GetSubsystem<Time>()->GetElapsedTime();

    URHO3D_LOGINFO("[Build] Profile '{}' ({}) -> {}", profileName_, profile_->platform_, outputDir_);
    return true;
}

void BuildSystem::Cancel()
{
    if (!IsBuilding())
        return;
    cancelRequested_ = true;
    URHO3D_LOGWARNING("[Build] Cancellation requested; the build stops at the next stage boundary");
}

void BuildSystem::HandleBeginFrame(StringHash eventType, VariantMap& eventData)
{
    if (stage_ == BuildStage::Idle)
        return;

    if (cancelRequested_ && pendingRequest_ == 0)
    {
        Finish(false, "Build cancelled.");
        return;
    }

    // A stage that handed its work to a continuation is only advanced by the frame after the one in
    // which the process reported back, so the continuation never runs inside an event handler that
    // the file system is still iterating over.
    if (pendingRequest_ != 0)
    {
        if (!processFinished_)
            return;

        pendingRequest_ = 0;
        processFinished_ = false;

        if (processExitCode_ != 0)
        {
            stageResume_ = nullptr;
            Finish(false, Format("'{}' exited with code {}.", pendingCommandLine_, processExitCode_));
            return;
        }

        const auto resume = ea::move(stageResume_);
        stageResume_ = nullptr;
        if (resume)
        {
            ea::string message;
            if (!resume(message))
            {
                Finish(false, message);
                return;
            }
        }
        // A continuation that started the next process of a multi-step stage - texture compression runs
        // the tool once per texture - waits for it exactly like the stage that started the first one,
        // instead of advancing past work still in flight. Existing continuations never start a process,
        // so for them this is a no-op and the stage advances as before.
        if (pendingRequest_ != 0)
            return;
        AdvanceStage();
        return;
    }

    ea::string message;
    if (!RunStage(message))
    {
        Finish(false, message);
        return;
    }
    if (pendingRequest_ != 0)
        return;
    // A stage that asked to run again next frame (AwaitAssets while assets are still cooking) stays
    // put instead of advancing; the flag is one-shot so the stage is re-evaluated from scratch.
    if (stageHold_)
    {
        stageHold_ = false;
        return;
    }
    AdvanceStage();
}

void BuildSystem::HandleAsyncExecFinished(StringHash eventType, VariantMap& eventData)
{
    using namespace AsyncExecFinished;
    if (pendingRequest_ == 0 || eventData[P_REQUESTID].GetUInt() != pendingRequest_)
        return;
    processExitCode_ = eventData[P_EXITCODE].GetInt();
    processFinished_ = true;
}

bool BuildSystem::RunStage(ea::string& message)
{
    switch (stage_)
    {
    case BuildStage::Validate:
        return StageValidate(message);
    case BuildStage::AwaitAssets:
        return StageAwaitAssets(message);
    case BuildStage::CleanOutput:
        return StageCleanOutput(message);
    case BuildStage::StageData:
        return StageStageData(message);
    case BuildStage::CompressTextures:
        return StageCompressTextures(message);
    case BuildStage::CompileScripts:
        return StageCompileScripts(message);
    case BuildStage::StageCoreData:
        return StageStageCoreData(message);
    case BuildStage::ExportData:
        return StageExportData(message, false);
    case BuildStage::ExportCoreData:
        return StageExportData(message, true);
    case BuildStage::StageRuntime:
        return StageStageRuntime(message);
    case BuildStage::AndroidProject:
        return StageAndroidProject(message);
    case BuildStage::Summary:
        return StageSummary(message);
    case BuildStage::Idle:
        break;
    }
    message = "There is no stage to run.";
    return false;
}

bool BuildSystem::StageValidate(ea::string& message)
{
    auto* fs = GetSubsystem<FileSystem>();
    auto* settings = GetSettings();
    auto* project = GetSubsystem<Project>();

    errors_.clear();
    settings->Validate(*profile_, errors_);

    // Two facts a profile cannot speak for, because they are about the project rather than about
    // the toolchain: that there is a data tree at all, and that it names an entry point. Without
    // Game.json the shipped game starts, prints a warning and shows an empty scene, which a user
    // will read as "the build is broken" instead of "the project is incomplete".
    const ea::string dataPath = NormalizeDir(project->GetDataPath());
    if (!fs->DirExists(dataPath))
        errors_.push_back(Format("The project has no data directory: '{}'.", dataPath));
    else if (!fs->FileExists(dataPath + "Game.json"))
    {
        errors_.push_back(Format("'{}' has no Game.json. The shipped game would not know which scene "
            "to load or which script to run.", dataPath));
    }

    // The output directory is emptied at the start of a build, so a mistyped path must not be
    // allowed to cost somebody a source tree or a volume.
    const ea::string output = RemoveTrailingSlash(outputDir_);
    const ea::string parent = RemoveTrailingSlash(GetPath(output));
    if (parent.empty() || parent == output)
    {
        errors_.push_back(Format("Refusing to build into '{}' because the whole directory is deleted "
            "first. Point OutputDir at a dedicated build directory.", outputDir_));
    }
    else
    {
        const ea::string protectedDirs[] = {
            RemoveTrailingSlash(ForwardSlashes(project->GetProjectPath())),
            RemoveTrailingSlash(ForwardSlashes(profile_->engineData_)),
            RemoveTrailingSlash(ForwardSlashes(profile_->engineBin_)),
        };
        for (const ea::string& protectedDir : protectedDirs)
        {
            if (!protectedDir.empty() && output == protectedDir)
            {
                errors_.push_back(Format("Refusing to empty '{}' because a build reads from it.", outputDir_));
                break;
            }
        }
    }

    if (errors_.empty())
        return true;

    for (const ea::string& error : errors_)
        URHO3D_LOGERROR("[Build] {}", error);
    message = Format("Profile '{}' is not buildable, {} problem(s) above.", profileName_, errors_.size());
    return false;
}

bool BuildSystem::StageAwaitAssets(ea::string& message)
{
    auto* project = GetSubsystem<Project>();
    auto* assetManager = project ? project->GetAssetManager() : nullptr;

    // Without an asset manager there is nothing cooking to wait for; the Cache simply contributes no
    // outputs and StageData copies only the Data/ trees.
    if (!assetManager)
        return true;

    if (assetManager->IsProcessing())
    {
        const auto progress = assetManager->GetProgress();
        URHO3D_LOGINFO("[Build] Waiting for asset cooking ({}/{})", progress.first, progress.second);
        // Hold on this stage; HandleBeginFrame re-runs it next frame instead of advancing.
        stageHold_ = true;
    }
    return true;
}

bool BuildSystem::StageCleanOutput(ea::string& message)
{
    auto* fs = GetSubsystem<FileSystem>();

    if (fs->DirExists(outputDir_) && !fs->RemoveDir(outputDir_, true))
    {
        message = Format("Could not empty '{}'. A game launched from a previous build is the usual "
            "reason; close it and build again.", outputDir_);
        return false;
    }
    if (!fs->CreateDirsRecursive(outputDir_))
    {
        message = Format("Could not create '{}'.", outputDir_);
        return false;
    }
    return true;
}

bool BuildSystem::StageStageData(ea::string& message)
{
    auto* fs = GetSubsystem<FileSystem>();
    auto* project = GetSubsystem<Project>();

    const ea::string staged = stagingDir_ + DataDirName;

    // Engine resources first, project resources on top: a project can then replace a single engine
    // file by carrying a file of the same name, without anybody editing the engine working tree.
    if (profile_->includeEngineData_)
    {
        const ea::string engineData = AddTrailingSlash(ForwardSlashes(profile_->engineData_)) + DataDirName;
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

bool BuildSystem::StageCompileScripts(ea::string& message)
{
    auto* settings = GetSettings();

    const ea::string compiler = settings->FindTool(*profile_, "LuaCompiler");
    if (compiler.empty())
    {
        message = "LuaCompiler disappeared between validation and this stage.";
        return false;
    }

    const ea::string staged = stagingDir_ + DataDirName;

    // Input root and output root are the same directory on purpose: the tool mirrors every source
    // path onto itself with a .luc extension, so the containers land exactly where the resource
    // names in the scenes already point. Encrypting the whole staged tree rather than only the
    // project's folder is also what makes the "no plain source ships" check afterwards meaningful,
    // because engine samples carry .lua files too.
    ea::vector<ea::string> arguments;
    arguments.push_back("--recursive");
    arguments.push_back("--encrypt");
    arguments.push_back("--out=" + staged);

    if (!profile_->scriptKeyEnvVar_.empty())
    {
        if (const char* key = getenv(profile_->scriptKeyEnvVar_.c_str()); key && *key)
            arguments.push_back("--key=" + ea::string(key));
    }
    arguments.push_back(staged);

    return StartProcess(compiler, arguments,
        [this](ea::string& resumeMessage) { return PruneStagedSources(resumeMessage); }, message);
}

bool BuildSystem::PruneStagedSources(ea::string& message)
{
    auto* fs = GetSubsystem<FileSystem>();
    const ea::string staged = stagingDir_ + DataDirName;

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

bool BuildSystem::StageCompressTextures(ea::string& message)
{
    auto* fs = GetSubsystem<FileSystem>();
    auto* settings = GetSettings();
    auto* project = GetSubsystem<Project>();

    // Resolve the tool once, up front: a profile that enabled compression but cannot find the tool has
    // to fail before a single texture is decoded, not halfway through the queue.
    const ea::string tool = settings->FindTool(*profile_, "PVRTexToolCLI");
    if (tool.empty())
    {
        message = "PVRTexToolCLI disappeared between validation and this stage.";
        return false;
    }
    textureToolPath_ = tool;

    const TextureCompressionSettings tc = profile_->GetEffectiveTextureCompression();
    const ea::string container = tc.container_.empty() ? ea::string("dds") : tc.container_;
    const ea::string staged = stagingDir_ + DataDirName;
    // Artifacts survives between builds and, unlike Cache, is not mounted as a resource root, so a
    // cooked texture parked here can never be mistaken for one the game loads by name.
    const ea::string cacheRoot = NormalizeDir(project->GetArtifactsPath()) + "TextureCompression/";

    ea::vector<ea::string> found;
    fs->ScanDir(found, staged, "*", SCAN_FILES | SCAN_RECURSIVE);

    textureQueue_.clear();
    textureQueueIndex_ = 0;

    for (const ea::string& rawRelative : found)
    {
        const ea::string relative = ForwardSlashes(rawRelative);
        if (!IsTextureSourceFile(relative))
            continue;

        const ea::string stagedSource = staged + relative;
        // The original in Data/, when there is one, supplies both the import metadata and the half of
        // the cache key that has to outlive the build. The staged copy cannot: FileSystem::Copy rewrites
        // it and stamps it with the build time, so its mtime differs on every run.
        const ea::string original = FindOriginalDataFile(relative);
        const ea::string& keySource = original.empty() ? stagedSource : original;

        TextureImporterParams params;
        LoadTextureImporterParams(context_, keySource, params);

        // Normal maps are data and always cook linear, whatever a hand-edited metadata file claims.
        const bool isLinear = params.textureType_ == TextureImportType::NormalMap
            || params.colorSpace_ == TextureImportColorSpace::Linear;
        // The per-file mipmap mode resolves against the profile here, so the fingerprint and the
        // tool call always agree on what gets baked.
        const bool bakeMipmaps = params.mipmapMode_ == TextureImportMipmapMode::Enabled
            || (params.mipmapMode_ == TextureImportMipmapMode::Inherit && tc.mipmaps_);

        unsigned sourceSize = 0;
        {
            File probe(context_, stagedSource, FILE_READ);
            if (probe.IsOpen())
                sourceSize = probe.GetSize();
        }
        const unsigned sourceTime = fs->GetLastModifiedTime(keySource);

        // Everything that decides the output except the alpha channel, which only picks between the two
        // color formats. Alpha stays out of the key on purpose: an edit that changes it changes the
        // source mtime too, which the key already carries. Leaving it out is what lets a no-change
        // rebuild skip the image decode entirely and go straight to the cached product.
        const ea::string fingerprint = Format("{}|{}|{}|{}|{}|{}|{}|{}|{}", sourceTime, sourceSize,
            params.textureType_ == TextureImportType::NormalMap ? "normal" : "color", isLinear ? "lRGB" : "sRGB",
            tc.colorFormatNoAlpha_, tc.colorFormatAlpha_, tc.normalFormat_, bakeMipmaps ? "mip" : "nomip",
            tc.quality_);

        const size_t dot = relative.rfind('.');
        const ea::string relativeNoExt = relative.substr(0, dot);

        TextureJob job;
        job.relative_ = relative;
        job.stagedSource_ = stagedSource;
        job.stagedDest_ = staged + relativeNoExt + "." + container;
        job.legacySidecarDir_ = stagedSource + ".d";
        job.cacheProduct_ = cacheRoot + relativeNoExt + "-" +
            Format("{:08x}", StringHash(fingerprint + "|" + container, StringHash::NoReverse{}).Value()) + "." + container;
        job.isNormal_ = params.textureType_ == TextureImportType::NormalMap;
        job.params_ = params;
        job.mipmaps_ = bakeMipmaps;

        if (!fs->CreateDirsRecursive(GetPath(job.cacheProduct_)))
        {
            message = Format("Could not create the texture cache directory for '{}'.", job.relative_);
            return false;
        }
        textureQueue_.push_back(ea::move(job));
    }

    if (textureQueue_.empty())
    {
        URHO3D_LOGINFO("[Build] No texture sources were staged; compression did nothing");
        return true;
    }
    return RunTextureQueue(message);
}

bool BuildSystem::RunTextureQueue(ea::string& message)
{
    auto* fs = GetSubsystem<FileSystem>();
    const TextureCompressionSettings tc = profile_->GetEffectiveTextureCompression();

    while (textureQueueIndex_ < textureQueue_.size())
    {
        TextureJob& job = textureQueue_[textureQueueIndex_];

        // A product whose name still matches the fingerprint is byte-for-byte what this source and these
        // settings produce, so it goes straight into staging and the tool is never woken.
        if (fs->FileExists(job.cacheProduct_))
        {
            if (!InstallCookedTexture(job, message))
                return false;
            ++texturesCached_;
            ++textureQueueIndex_;
            continue;
        }

        // Cache miss: decode just enough to choose the format. A normal map always uses its own format
        // and stays linear; anything else is a color texture whose alpha picks between the two formats.
        bool hasAlpha = false;
        {
            File file(context_, job.stagedSource_, FILE_READ);
            if (!file.IsOpen())
            {
                message = Format("Could not open the staged texture '{}'.", job.stagedSource_);
                return false;
            }
            Image image(context_);
            if (!image.BeginLoad(file))
            {
                message = Format("'{}' is not a readable image, so it cannot be compressed.", job.stagedSource_);
                return false;
            }
            hasAlpha = image.HasAlphaChannel();
        }

        const ea::string format = job.isNormal_
            ? tc.normalFormat_
            : (hasAlpha ? tc.colorFormatAlpha_ : tc.colorFormatNoAlpha_);
        if (format.empty())
        {
            message = Format("No texture format is configured for '{}'.", job.relative_);
            return false;
        }
        // Normal maps store directions, not colors; encoding them as sRGB would bend every vector.
        // Linear color textures (masks, LUTs) keep their data unconverted for the same reason.
        const char* colorSpace = job.isNormal_ || job.params_.colorSpace_ == TextureImportColorSpace::Linear
            ? "lRGB"
            : "sRGB";

        ea::vector<ea::string> arguments;
        arguments.push_back("-i");
        arguments.push_back(job.stagedSource_);
        arguments.push_back("-o");
        arguments.push_back(job.cacheProduct_);
        arguments.push_back("-f");
        arguments.push_back(Format("{},UBN,{}", format, colorSpace));
        if (job.mipmaps_)
            arguments.push_back("-m");
        if (!tc.quality_.empty())
        {
            arguments.push_back("-q");
            arguments.push_back(tc.quality_);
        }
        // The tool reports progress on stderr, which the async runner would otherwise surface as noise.
        arguments.push_back("-shh");

        const unsigned index = textureQueueIndex_;
        return StartProcess(textureToolPath_, arguments,
            [this, index](ea::string& resumeMessage) { return FinalizeCookedTexture(index, resumeMessage); },
            message);
    }

    URHO3D_LOGINFO("[Build] Textures: {} compressed, {} reused from cache", texturesCompressed_, texturesCached_);
    return true;
}

bool BuildSystem::FinalizeCookedTexture(unsigned index, ea::string& message)
{
    auto* fs = GetSubsystem<FileSystem>();
    TextureJob& job = textureQueue_[index];

    // A zero exit already gated getting here, but the product is the only proof that matters: the tool
    // can exit cleanly and still write nothing when it dislikes the requested format.
    if (!fs->FileExists(job.cacheProduct_))
    {
        message = Format("PVRTexToolCLI finished but produced no '{}'.", job.cacheProduct_);
        return false;
    }
    if (!InstallCookedTexture(job, message))
        return false;
    ++texturesCompressed_;
    ++textureQueueIndex_;
    // Keep the queue moving; the next texture either hits the cache or suspends this stage again.
    return RunTextureQueue(message);
}

bool BuildSystem::InstallCookedTexture(const TextureJob& job, ea::string& message)
{
    auto* fs = GetSubsystem<FileSystem>();

    if (!fs->CreateDirsRecursive(GetPath(job.stagedDest_)))
    {
        message = Format("Could not create the staging directory for '{}'.", job.stagedDest_);
        return false;
    }
    if (!fs->Copy(job.cacheProduct_, job.stagedDest_))
    {
        message = Format("Could not place the cooked texture '{}'.", job.stagedDest_);
        return false;
    }
    // The source is dead weight now: the runtime router redirects its name to this product, so shipping
    // both would carry the uncompressed pixels for nothing and roughly double the package.
    if (!fs->Delete(job.stagedSource_))
    {
        message = Format("Could not remove the uncompressed source '{}'.", job.stagedSource_);
        return false;
    }
    // Import metadata is editor-only in the legacy scheme; its leftover ".d" directories must not
    // ship beside the cooked texture. The current ".texmeta" file is different: the runtime reads
    // it, so it stays in staging exactly as it was copied from Data/ - nothing to generate, nothing
    // to remove.
    if (fs->DirExists(job.legacySidecarDir_) && !fs->RemoveDir(job.legacySidecarDir_, true))
    {
        message = Format("Could not remove the legacy import metadata '{}'.", job.legacySidecarDir_);
        return false;
    }
    return true;
}

ea::string BuildSystem::FindOriginalDataFile(const ea::string& relative) const
{
    auto* fs = GetSubsystem<FileSystem>();
    auto* project = GetSubsystem<Project>();

    // Project Data/ is copied over engine Data/, so a file present in both came from the project.
    const ea::string projectData = NormalizeDir(project->GetDataPath());
    if (fs->FileExists(projectData + relative))
        return projectData + relative;
    if (profile_->includeEngineData_)
    {
        const ea::string engineData = AddTrailingSlash(ForwardSlashes(profile_->engineData_)) + DataDirName;
        if (fs->FileExists(engineData + relative))
            return engineData + relative;
    }
    return EMPTY_STRING;
}

bool BuildSystem::StageStageCoreData(ea::string& message)
{
    auto* fs = GetSubsystem<FileSystem>();
    const ea::string source = AddTrailingSlash(ForwardSlashes(profile_->engineData_)) + CoreDataDirName;
    return MergeDirectory(fs, source, stagingDir_ + CoreDataDirName, message);
}

bool BuildSystem::StageExportData(ea::string& message, bool coreData)
{
    auto* fs = GetSubsystem<FileSystem>();
    auto* settings = GetSettings();

    const ea::string dirName = coreData ? CoreDataDirName : DataDirName;
    const ea::string packageName = coreData ? CoreDataPackageName : DataPackageName;
    const ea::string staged = stagingDir_ + dirName;

    if (!profile_->packData_)
    {
        if (!MergeDirectory(fs, staged, resourceDir_ + dirName, message))
            return false;
        return VerifyExportedResources(message, coreData);
    }

    const ea::string package = resourceDir_ + packageName;
    const ea::string tool = settings->FindTool(*profile_, "PackageTool");
    if (tool.empty())
    {
        message = "PackageTool disappeared between validation and this stage.";
        return false;
    }

    // PackageTool leaves an existing package alone when it considers it up to date, comparing
    // timestamps against whatever is already there. A package that another profile wrote into the
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
    if (profile_->compressPackages_)
        arguments.push_back("-c");

    return StartProcess(tool, arguments,
        [this, coreData](ea::string& resumeMessage) { return VerifyExportedResources(resumeMessage, coreData); },
        message);
}

bool BuildSystem::VerifyExportedResources(ea::string& message, bool coreData)
{
    auto* fs = GetSubsystem<FileSystem>();

    const ea::string dirName = coreData ? CoreDataDirName : DataDirName;
    const ea::string packageName = coreData ? CoreDataPackageName : DataPackageName;

    if (!profile_->packData_)
    {
        // Loose directories: proving the entry configuration arrived is enough, because a copy that
        // partially failed already made CopyDir report failure.
        const ea::string entry = resourceDir_ + dirName + "Game.json";
        if (!coreData && !fs->FileExists(entry))
        {
            message = Format("'{}' is missing from the output.", entry);
            return false;
        }
        return true;
    }

    const ea::string package = resourceDir_ + packageName;

    // Read the result back with the same class the game will use, rather than asking the bundler to
    // describe its own output: this proves the header, the entry table and the data offsets all
    // agree, which is more than a tool printing a number it just computed.
    PackageFile reader(context_);
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

    if (coreData)
        return true;

    if (!reader.Exists("Game.json"))
    {
        message = Format("'{}' does not contain Game.json, so the shipped game cannot find its entry "
            "configuration.", package);
        return false;
    }

    // Only a profile that encrypts has something to prove about script formats: for every other
    // build the plain .lua file IS the shipped artifact, and refusing it would reject the common
    // case. When encryption is on, a surviving source file is worse than a missing container - the
    // runtime prefers the .luc, so the source is dead weight that reads as an unprotected script.
    if (profile_->encryptScripts_)
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

bool BuildSystem::StageStageRuntime(ea::string& message)
{
    auto* fs = GetSubsystem<FileSystem>();

    const ea::string bin = NormalizeDir(profile_->engineBin_);
    const ea::string suffix = GetExecutableSuffix();

    struct Artifact
    {
        ea::string source_;
        ea::string destination_;
    };

    const Artifact artifacts[] = {
        { bin + HostName + suffix, outputDir_ + profile_->executableName_ + suffix },
        { bin + EngineLibraryName + DYN_LIB_SUFFIX, outputDir_ + EngineLibraryName + DYN_LIB_SUFFIX },
        { bin + LuaLibraryName + DYN_LIB_SUFFIX, outputDir_ + LuaLibraryName + DYN_LIB_SUFFIX },
    };

    for (const Artifact& artifact : artifacts)
    {
        if (!fs->FileExists(artifact.source_))
        {
            message = Format("'{}' is not there to be copied.", artifact.source_);
            return false;
        }
        if (!fs->Copy(artifact.source_, artifact.destination_))
        {
            message = Format("Could not copy '{}' to '{}'.", artifact.source_, artifact.destination_);
            return false;
        }
    }
    return true;
}

bool BuildSystem::StageAndroidProject(ea::string& message)
{
    auto* project = GetSubsystem<Project>();

    ea::vector<ea::string> scaffoldErrors;
    if (GenerateAndroidScaffold(context_, *profile_, ForwardSlashes(project->GetProjectPath()), outputDir_,
        resourceDir_, scaffoldErrors))
    {
        return true;
    }

    errors_.insert(errors_.end(), scaffoldErrors.begin(), scaffoldErrors.end());
    for (const ea::string& error : scaffoldErrors)
        URHO3D_LOGERROR("[Build] {}", error);
    message = Format("The Android project could not be written, {} problem(s) above.", scaffoldErrors.size());
    return false;
}

bool BuildSystem::StageSummary(ea::string& message)
{
    auto* fs = GetSubsystem<FileSystem>();

    ea::vector<ea::string> entries;
    fs->ScanDir(entries, outputDir_, "*", SCAN_DIRS | SCAN_FILES);
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
        const ea::string path = outputDir_ + entry;
        const unsigned long long size = fs->DirExists(path) ? DirectorySize(path) : [&]()
        {
            File file(context_, path, FILE_READ);
            return file.IsOpen() ? static_cast<unsigned long long>(file.GetSize()) : 0ULL;
        }();
        total += size;
        URHO3D_LOGINFO("[Build]   {:>14} bytes  {}", size, entry);
    }

    URHO3D_LOGINFO("[Build]   {:>14} bytes  total, {} top level entr{} in {}", total, entries.size(),
        entries.size() == 1 ? "y" : "ies", outputDir_);
    return true;
}

void BuildSystem::AdvanceStage()
{
    ++stageIndex_;
    if (stageIndex_ >= plan_.size())
    {
        Finish(true, EMPTY_STRING);
        return;
    }
    stage_ = plan_[stageIndex_];
    progress_ = static_cast<float>(stageIndex_) / static_cast<float>(plan_.size());
    URHO3D_LOGINFO("[Build] {} ({}/{})", BuildStageName(stage_), stageIndex_ + 1, plan_.size());
}

bool BuildSystem::StartProcess(const ea::string& program, const ea::vector<ea::string>& arguments,
    ea::function<bool(ea::string&)> resume, ea::string& message)
{
    auto* fs = GetSubsystem<FileSystem>();

    // An asynchronously started process writes its output where nobody reads it, so the command
    // line is logged before it runs; that is the line a user pastes when a stage fails. A content
    // key is the one argument that must not be repeated here.
    ea::string commandLine = program;
    for (const ea::string& argument : arguments)
    {
        commandLine += ' ';
        commandLine += StartsMinusKey(argument) ? "--key=<redacted>" : argument;
    }

    const unsigned request = fs->SystemRunAsync(program, arguments);
    if (request == M_MAX_UNSIGNED)
    {
        message = Format("Could not start '{}'.", commandLine);
        return false;
    }

    URHO3D_LOGINFO("[Build] $ {}", commandLine);
    pendingRequest_ = request;
    pendingCommandLine_ = commandLine;
    processFinished_ = false;
    stageResume_ = ea::move(resume);
    return true;
}

unsigned long long BuildSystem::DirectorySize(const ea::string& directory) const
{
    auto* fs = GetSubsystem<FileSystem>();

    ea::vector<ea::string> files;
    fs->ScanDir(files, directory, "*", SCAN_FILES | SCAN_RECURSIVE);

    unsigned long long total = 0;
    for (const ea::string& file : files)
    {
        const ea::string path = AddTrailingSlash(directory) + file;
        if (!fs->FileExists(path))
            continue;
        File handle(context_, path, FILE_READ);
        if (handle.IsOpen())
            total += handle.GetSize();
    }
    return total;
}

void BuildSystem::Finish(bool success, const ea::string& message)
{
    const float elapsed = GetSubsystem<Time>()->GetElapsedTime() - startTime_;
    const bool autoRun = success && profile_ && profile_->autoRunAfterBuild_ && profile_->IsWindowsDesktop();
    const ea::string executable =
        profile_ ? outputDir_ + profile_->executableName_ + GetExecutableSuffix() : EMPTY_STRING;

    if (success)
    {
        progress_ = 1.0f;
        URHO3D_LOGINFO("[Build] Profile '{}' finished in {:.1f}s", profileName_, elapsed);
    }
    else
    {
        if (!message.empty() && ea::find(errors_.begin(), errors_.end(), message) == errors_.end())
            errors_.push_back(message);
        URHO3D_LOGERROR("[Build] Profile '{}' failed after {:.1f}s: {}", profileName_, elapsed, message);
    }

    CleanupStaging();

    stage_ = BuildStage::Idle;
    stageIndex_ = 0;
    plan_.clear();
    profile_ = nullptr;
    pendingRequest_ = 0;
    pendingCommandLine_.clear();
    stageResume_ = nullptr;
    processFinished_ = false;
    cancelRequested_ = false;

    VariantMap eventData;
    eventData["Success"] = success;
    eventData["Profile"] = profileName_;
    eventData["Message"] = success ? EMPTY_STRING : message;
    eventData["OutputDir"] = outputDir_;
    SendEvent(E_BUILD_FINISHED, eventData);

    const auto handler = ea::move(onDone_);
    onDone_ = nullptr;
    if (handler)
        handler(success, success ? EMPTY_STRING : message, outputDir_);

    // Launching is the last thing a successful build does, and the process is not waited for: the
    // point of the switch is to see the result, which means a window that stays open until the user
    // closes it.
    if (autoRun)
    {
        auto* fs = GetSubsystem<FileSystem>();
        if (fs->FileExists(executable))
        {
            URHO3D_LOGINFO("[Build] Launching {}", executable);
            fs->SystemRunAsync(executable, {});
        }
        else
            URHO3D_LOGERROR("[Build] Cannot run '{}': it is not in the output directory", executable);
    }
}

void BuildSystem::CleanupStaging()
{
    if (stagingDir_.empty())
        return;

    auto* fs = GetSubsystem<FileSystem>();
    if (!fs->RemoveDir(stagingDir_, true))
        URHO3D_LOGWARNING("[Build] Left temporary files behind in '{}'", stagingDir_);
    stagingDir_.clear();
}

} // namespace Urho3D

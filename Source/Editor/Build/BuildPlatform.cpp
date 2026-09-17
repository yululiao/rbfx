// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

// The build engine: the frame-driven state machine that walks a plan of BuildSteps, the process
// runner the resumable steps suspend on, and the resolved platform/paths every step shares. The
// editor-facing BuildSystem owns one of these and forwards the UI; the platform subclasses add only
// the terminal step and the handful of decisions that differ per platform.

#include "../Assets/TextureImportSettings.h"
#include "BuildInternal.h"
#include "BuildPlatform.h"
#include "BuildSettings.h"
#include "Stages/BuildSteps.h"
#include "../Project/AssetManager.h"
#include "../Project/Project.h"

#include <Urho3D/Core/ProcessUtils.h>
#include <Urho3D/Core/StringUtils.h>
#include <Urho3D/Core/Timer.h>
#include <Urho3D/IO/File.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/Log.h>

#include <EASTL/algorithm.h>

#include <cstring>

namespace Urho3D
{

// Names the runtime mounts by and the host binary every platform packages. Shared between the
// platform-agnostic steps and the per-platform subclasses so the two cannot drift apart: LuaGamePlayer
// sets EP_RESOURCE_PATHS to "CoreData;Data" and the virtual file system looks for "<prefix>/Data.pak"
// before "<prefix>/Data/", so a package named anything else is invisible to the game.
const ea::string DataDirName = "Data/";
const ea::string CoreDataDirName = "CoreData/";
const ea::string DataPackageName = "Data.pak";
const ea::string CoreDataPackageName = "CoreData.pak";
const ea::string HostName = "LuaGamePlayer";
const ea::string EngineLibraryName = "Urho3D";

ea::string ForwardSlashes(ea::string text)
{
    text.replace("\\", "/");
    return text;
}

ea::string NormalizeDir(const ea::string& path)
{
    return AddTrailingSlash(RemoveTrailingSlash(ForwardSlashes(path)));
}

namespace
{

bool StartsMinusKey(const ea::string& text)
{
    return strncmp(text.c_str(), "--key=", 6) == 0;
}

} // namespace

BuildPlatform::BuildPlatform(Context* context)
    : context_(context)
{
}

BuildPlatform::~BuildPlatform() = default;

BuildSettings* BuildPlatform::GetSettings() const
{
    const auto project = context_->GetSubsystem<Project>();
    return project ? project->GetBuildSettings() : nullptr;
}

bool BuildPlatform::StartBuild(const ea::string& platformName, const ea::string& outputOverride, DoneHandler onDone)
{
    auto* fs = context_->GetSubsystem<FileSystem>();
    auto* project = context_->GetSubsystem<Project>();
    auto* settings = GetSettings();

    if (running_)
    {
        URHO3D_LOGERROR("[Build] Platform '{}' is still building", platformName_);
        return false;
    }
    if (!project || !settings)
    {
        URHO3D_LOGERROR("[Build] No project is loaded, there is nothing to build");
        return false;
    }

    platform_ = settings->FindPlatform(platformName);
    if (!platform_)
    {
        ea::string known;
        for (const ea::string& name : settings->GetPlatformNames())
        {
            if (!known.empty())
                known += ", ";
            known += name;
        }
        URHO3D_LOGERROR("[Build] No build platform named '{}' (known platforms: {})", platformName,
            known.empty() ? "none" : known.c_str());
        return false;
    }

    platformName_ = platformName;
    onDone_ = ea::move(onDone);
    errors_.clear();
    cancelRequested_ = false;
    pendingRequest_ = 0;
    processFinished_ = false;
    stageHold_ = false;
    stepResume_ = nullptr;
    progress_ = 0.0f;

    if (outputOverride.empty())
        outputDir_ = platform_->ResolveOutputDir(project->GetProjectPath());
    else
    {
        ea::string resolved = ForwardSlashes(outputOverride);
        if (!IsAbsolutePath(resolved))
            resolved = NormalizeDir(fs->GetCurrentDir()) + resolved;
        outputDir_ = NormalizeDir(resolved);
    }

    // Resources are found by name, not by position, so the only difference between the platforms is
    // where the mounted directories live: beside the executable on a desktop, inside the assets folder
    // of the generated gradle project on a phone.
    resourceDir_ = ResolveResourceDir(outputDir_);
    stagingDir_ = NormalizeDir(project->GetRandomTemporaryPath());

    ComposePlan();
    stepIndex_ = 0;
    running_ = true;
    startTime_ = context_->GetSubsystem<Time>()->GetElapsedTime();

    URHO3D_LOGINFO("[Build] Platform '{}' ({}) -> {}", platformName_, platform_->platform_, outputDir_);
    return true;
}

void BuildPlatform::ComposePlan()
{
    plan_.clear();
    // The compile goes first when asked for: it produces the very artifacts the Validate step checks
    // for, so a platform that compiles never has to be built twice to get past validation. Android is
    // left out because its gradle project compiles the engine itself.
    if (platform_->engineBuild_ != EngineBuildMode::Never && UsesEngineBuild())
        plan_.push_back(ea::make_unique<EngineBuildStep>(*this));
    plan_.push_back(ea::make_unique<ValidateStep>(*this));
    // Cook imported assets before anything reads Cache: this is the step that guarantees the Cache
    // satellite outputs StageData copies are present and current. Placing it after Validate means the
    // first BeginFrame runs Validate while AssetManager::Initialize (driven from the render phase of
    // the same frame) enqueues cooking, so AwaitAssets sees real IsProcessing() state from frame two.
    plan_.push_back(ea::make_unique<AwaitAssetsStep>(*this));
    plan_.push_back(ea::make_unique<CleanOutputStep>(*this));
    plan_.push_back(ea::make_unique<StageDataStep>(*this));
    // Textures compress before scripts and export so every later step sees the cooked tree it ships.
    if (platform_->textureCompression_.enabled_)
        plan_.push_back(ea::make_unique<CompressTexturesStep>(*this));
    if (platform_->encryptScripts_)
        plan_.push_back(ea::make_unique<CompileScriptsStep>(*this));
    plan_.push_back(ea::make_unique<StageCoreDataStep>(*this));
    plan_.push_back(ea::make_unique<ExportDataStep>(*this, false));
    plan_.push_back(ea::make_unique<ExportDataStep>(*this, true));
    // The terminal packaging step is the one thing every platform does differently; it decides both
    // what runs and what the stage is called.
    plan_.push_back(MakeRuntimeStep());
    plan_.push_back(ea::make_unique<SummaryStep>(*this));
}

void BuildPlatform::Tick()
{
    if (!running_)
        return;

    if (cancelRequested_ && pendingRequest_ == 0)
    {
        Finish(false, "Build cancelled.");
        return;
    }

    // A step that handed its work to a continuation is only advanced by the frame after the one in
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
            stepResume_ = nullptr;
            Finish(false, Format("'{}' exited with code {}.", pendingCommandLine_, processExitCode_));
            return;
        }

        const auto resume = ea::move(stepResume_);
        stepResume_ = nullptr;
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
        // the tool once per texture - waits for it exactly like the step that started the first one,
        // instead of advancing past work still in flight.
        if (pendingRequest_ != 0)
            return;
        AdvanceStep();
        return;
    }

    ea::string message;
    if (!plan_[stepIndex_]->Run(message))
    {
        Finish(false, message);
        return;
    }
    if (pendingRequest_ != 0)
        return;
    // A step that asked to run again next frame (AwaitAssets while assets are still cooking) stays put
    // instead of advancing; the flag is one-shot so the step is re-evaluated from scratch.
    if (stageHold_)
    {
        stageHold_ = false;
        return;
    }
    AdvanceStep();
}

void BuildPlatform::OnProcessFinished(unsigned requestId, int exitCode)
{
    if (pendingRequest_ == 0 || requestId != pendingRequest_)
        return;
    processExitCode_ = exitCode;
    processFinished_ = true;
}

void BuildPlatform::AdvanceStep()
{
    ++stepIndex_;
    if (stepIndex_ >= plan_.size())
    {
        Finish(true, EMPTY_STRING);
        return;
    }
    progress_ = static_cast<float>(stepIndex_) / static_cast<float>(plan_.size());
    URHO3D_LOGINFO("[Build] {} ({}/{})", plan_[stepIndex_]->Name(), stepIndex_ + 1, plan_.size());
}

ea::string BuildPlatform::GetStageName() const
{
    if (!running_ || stepIndex_ >= plan_.size())
        return "Idle";
    return plan_[stepIndex_]->Name();
}

bool BuildPlatform::StartProcess(const ea::string& program, const ea::vector<ea::string>& arguments,
    ea::function<bool(ea::string&)> resume, ea::string& message)
{
    auto* fs = context_->GetSubsystem<FileSystem>();

    // An asynchronously started process writes its output where nobody reads it, so the command line
    // is logged before it runs; that is the line a user pastes when a step fails. A content key is the
    // one argument that must not be repeated here.
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
    stepResume_ = ea::move(resume);
    return true;
}

void BuildPlatform::Cancel()
{
    if (!running_)
        return;
    cancelRequested_ = true;
    URHO3D_LOGWARNING("[Build] Cancellation requested; the build stops at the next stage boundary");
}

void BuildPlatform::Finish(bool success, const ea::string& message)
{
    const float elapsed = context_->GetSubsystem<Time>()->GetElapsedTime() - startTime_;

    if (success)
    {
        progress_ = 1.0f;
        URHO3D_LOGINFO("[Build] Platform '{}' finished in {:.1f}s", platformName_, elapsed);
    }
    else
    {
        if (!message.empty() && ea::find(errors_.begin(), errors_.end(), message) == errors_.end())
            errors_.push_back(message);
        URHO3D_LOGERROR("[Build] Platform '{}' failed after {:.1f}s: {}", platformName_, elapsed, message);
    }

    // Whether the finished package should be launched is a platform decision, and what "launch" even
    // means differs: a desktop host runs the executable, a web package runs its serving script,
    // Android has nothing runnable on the machine that built it. Decided while the platform is alive.
    const bool autoRun = success && platform_ && platform_->autoRunAfterBuild_ && SupportsAutoRun();

    CleanupStaging();

    running_ = false;
    stepIndex_ = 0;
    plan_.clear();
    pendingRequest_ = 0;
    pendingCommandLine_.clear();
    stepResume_ = nullptr;
    processFinished_ = false;
    cancelRequested_ = false;

    // The handler is the owning BuildSystem's wrapper: it fires the finish event and then the caller's
    // own callback. The platform stays free of engine events, which it could not send anyway.
    const auto handler = ea::move(onDone_);
    onDone_ = nullptr;
    if (handler)
        handler(success, success ? EMPTY_STRING : message, outputDir_);

    // Launching is the last thing a successful build does, and the process is not waited for: the
    // point of the switch is to see the result, which means a window that stays open until the user
    // closes it. The subclass knows what running its own kind of package looks like.
    if (autoRun)
        LaunchAfterBuild(outputDir_);

    // The platform is cleared last: the launch above reads it to resolve the interpreter or executable
    // it needs, and the project owns it, so it would outlive this call regardless.
    platform_ = nullptr;
}

void BuildPlatform::CleanupStaging()
{
    if (stagingDir_.empty())
        return;

    auto* fs = context_->GetSubsystem<FileSystem>();
    if (!fs->RemoveDir(stagingDir_, true))
        URHO3D_LOGWARNING("[Build] Left temporary files behind in '{}'", stagingDir_);
    stagingDir_.clear();
}

bool BuildPlatform::LocateEngineBuildTree(ea::string& tree, ea::string& cmakeCommand,
    ea::string& generator, ea::string& message) const
{
    auto* fs = context_->GetSubsystem<FileSystem>();

    if (platform_->engineBin_.empty())
    {
        message = "Engine binary directory is not set, so there is no build tree to compile into.";
        return false;
    }

    // The binary directory sits somewhere inside the build tree (".../bin/Release"), so the tree is
    // the first directory at or above it holding a CMake cache. The search starts at the binaries
    // themselves because a tree with everything in its root is legal too.
    ea::string candidate = RemoveTrailingSlash(NormalizeDir(platform_->engineBin_));
    for (unsigned level = 0; level < 4; ++level)
    {
        const ea::string cache = candidate + "/CMakeCache.txt";
        if (fs->FileExists(cache))
        {
            tree = NormalizeDir(candidate);
            cmakeCommand = ReadCMakeCacheEntry(cache, "CMAKE_COMMAND");
            generator = ReadCMakeCacheEntry(cache, "CMAKE_GENERATOR");
            return true;
        }
        candidate = RemoveTrailingSlash(GetPath(candidate));
        if (candidate.empty() || candidate.size() < 2)
            break;
    }

    message = Format("Could not find a CMake build tree at or above '{}'. Configure one first, "
        "for example: cmake -S <engine sources> -B <build tree> -G <generator> [toolchain flags].",
        NormalizeDir(platform_->engineBin_));
    return false;
}

ea::string BuildPlatform::ReadCMakeCacheEntry(const ea::string& cachePath, const ea::string& key) const
{
    File file(context_, cachePath, FILE_READ);
    if (!file.IsOpen())
        return EMPTY_STRING;

    // Cache lines look like "KEY:TYPE=value"; the type never contains '=' so the first one is the
    // separator. A Windows cache can end the value with a carriage return, trimmed here so every
    // caller can compare and concatenate the result as-is.
    const ea::string prefix = key + ":";
    while (!file.IsEof())
    {
        const ea::string line = file.ReadLine();
        if (line.find(prefix) != 0)
            continue;
        const size_t separator = line.find('=');
        if (separator == ea::string::npos)
            continue;
        const ea::string value = line.substr(separator + 1);
        const size_t first = value.find_first_not_of(" \t\r\n");
        const size_t last = value.find_last_not_of(" \t\r\n");
        return first != ea::string::npos ? value.substr(first, last - first + 1) : EMPTY_STRING;
    }
    return EMPTY_STRING;
}

unsigned long long BuildPlatform::DirectorySize(const ea::string& directory) const
{
    auto* fs = context_->GetSubsystem<FileSystem>();

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

bool BuildPlatform::VerifyEngineArtifacts(const ea::string& bin, ea::string& message) const
{
    auto* fs = context_->GetSubsystem<FileSystem>();

    // The same artifacts Validate asks for, phrased for the step that just ran: a generator that
    // exited zero while skipping a broken target must not pass silently into packaging.
    const ea::string suffix = GetExecutableSuffix();
    const ea::string artifacts[] = {
        bin + HostName + suffix,
        bin + EngineLibraryName + DYN_LIB_SUFFIX,
    };
    for (const ea::string& artifact : artifacts)
    {
        if (!fs->FileExists(artifact))
        {
            message = Format("The engine build finished but '{}' is still not there.", artifact);
            return false;
        }
    }
    return true;
}

ea::string BuildPlatform::ResolveEmsdkPython() const
{
    // Only the web backend has an emsdk to prefer; everywhere else a build that asks still gets an
    // interpreter from PATH. A machine without any python cannot run the packager anyway.
#if defined(_WIN32)
    return "python";
#else
    return "python3";
#endif
}

void BuildPlatform::LaunchAfterBuild(const ea::string& outputDir) const
{
    // Desktop default: run the produced host binary and do not wait, so the window stays open until
    // the user closes it. Platforms with a different notion of "run the result" override this.
    const ea::string executable =
        platform_ ? outputDir + platform_->executableName_ + GetExecutableSuffix() : EMPTY_STRING;
    if (executable.empty())
        return;

    auto* fs = context_->GetSubsystem<FileSystem>();
    if (fs->FileExists(executable))
    {
        URHO3D_LOGINFO("[Build] Launching {}", executable);
        fs->SystemRunAsync(executable, {});
    }
    else
        URHO3D_LOGERROR("[Build] Cannot run '{}': it is not in the output directory", executable);
}

ea::unique_ptr<BuildPlatform> CreateBuildPlatform(Context* context, const ea::string& platformName)
{
    // Read just enough to pick a subclass: the platform's platform string. The authoritative lookup and
    // the "no such platform" report happen in StartBuild, so a missing platform still falls through to a
    // desktop object whose StartBuild will fail with the right message.
    ea::string platformKind;
    if (auto* project = context->GetSubsystem<Project>())
    {
        if (auto* settings = project->GetBuildSettings())
        {
            if (const BuildPlatformData* platform = settings->FindPlatform(platformName))
                platformKind = platform->platform_;
        }
    }
    if (platformKind == "Android")
        return ea::make_unique<AndroidBuildPlatform>(context);
    if (platformKind == "Web")
        return ea::make_unique<WebBuildPlatform>(context);
    return ea::make_unique<DesktopBuildPlatform>(context);
}

} // namespace Urho3D

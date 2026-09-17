// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#pragma once

#include <EASTL/functional.h>
#include <EASTL/string.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/vector.h>

namespace Urho3D
{

class BuildPlatformData;
class BuildSettings;
class BuildStep;
class Context;

/// Names the runtime mounts by and the host binary every platform packages. Shared between the
/// platform-agnostic steps and the per-platform backends below, so they cannot drift apart on where a
/// resource file goes or which executable has to be present.
/// @{
extern const ea::string DataDirName;
extern const ea::string CoreDataDirName;
extern const ea::string DataPackageName;
extern const ea::string CoreDataPackageName;
extern const ea::string HostName;
extern const ea::string EngineLibraryName;
/// @}

/// Backslashes to forward slashes; the staging tree is addressed with forward slashes everywhere.
ea::string ForwardSlashes(ea::string text);
/// A directory path with forward slashes and exactly one trailing slash.
ea::string NormalizeDir(const ea::string& path);

/// One stage of a build, as a type. A build is a plan of these walked in order; which ones are in the
/// plan, and what the terminal one does, is decided by the platform. A step either finishes on the
/// spot or calls the platform's StartProcess and hands the rest of its work to a continuation bound to
/// itself, which is what keeps the whole pipeline resumable frame by frame without a sub-state
/// machine. Per-step state (the texture queue, a hold request) lives in the step, not the platform.
///
/// Steps are driven by the BuildPlatform that owns the plan; they reach the build's resolved paths,
/// platform and process runner through owner_, whose public interface is exactly the surface a step is
/// meant to use.
class BuildStep
{
public:
    explicit BuildStep(class BuildPlatform& owner)
        : owner_(owner)
    {
    }
    virtual ~BuildStep() = default;

    /// Display name of the stage, as it appears in the log and the build tab.
    virtual const char* Name() const = 0;

    /// Run the stage. Returns false to abort the build with the reason in 'message'. Returning true
    /// means the stage is done unless it started a process (the platform then waits) or asked the
    /// platform to hold on this step for another frame (AwaitAssets while assets cook).
    virtual bool Run(ea::string& message) = 0;

protected:
    class BuildPlatform& owner_;
};

/// A build platform turned into a deliverable, and the state machine that walks the plan: this is the
/// whole build, not just the part that differs between platforms. The editor-facing BuildSystem is a
/// thin Object that owns one of these and forwards the buttons; everything about resolving a platform,
/// staging resources, cooking textures and shelling out to tools lives here so that BuildSystem never
/// has to know what a stage is.
///
/// The platform subclasses add nothing to that machinery - they answer the handful of questions that
/// genuinely differ (is the engine compiled first, where do resources mount, what does the compile
/// have to prove, how is the finished package assembled and launched) and supply the terminal step.
///
/// Not an Object: the owning BuildSystem drives it from E_BEGINFRAME and E_ASYNCEXECFINISHED, and it
/// reaches the engine through the Context it was given. Constructed fresh for each build because a
/// platform may target any platform, and replaced - not reset - when the next build names another.
class BuildPlatform
{
public:
    /// Called once per build with the outcome: a human readable reason (empty on success) and the
    /// output directory. The owning BuildSystem wraps its own handler around this to fire the finish
    /// event, so the platform itself never touches engine events.
    using DoneHandler = ea::function<void(bool success, const ea::string& message, const ea::string& outputDir)>;

    explicit BuildPlatform(Context* context);
    /// Out of line because plan_ holds unique_ptr to the incomplete BuildStep: destroying it where
    /// that type is still incomplete would not compile.
    virtual ~BuildPlatform();

    /// Resolve 'platformName' among the current project's build platforms and, on success, build the
    /// plan and start walking it. Returns false - without ever invoking the handler - when a build is
    /// already running, no project is loaded, or the platform does not exist; those are answered
    /// synchronously because the caller asked to start now. On success the handler runs at the end.
    bool StartBuild(const ea::string& platformName, const ea::string& outputOverride, DoneHandler onDone);

    /// The frame tick: advance the plan or run the continuation of a process that just reported.
    void Tick();
    /// Tell the platform that the asynchronous process 'requestId' exited with 'exitCode'.
    void OnProcessFinished(unsigned requestId, int exitCode);
    /// Stop at the next step boundary; a process already running is allowed to finish.
    void Cancel();

    bool IsBuilding() const { return running_; }
    const ea::string& GetPlatformName() const { return platformName_; }
    const ea::string& GetOutputDir() const { return outputDir_; }
    float GetProgress() const { return progress_; }
    ea::string GetStageName() const;
    const ea::vector<ea::string>& GetErrors() const { return errors_; }

    // --- Interface the steps use. Public because a step is a separate type operating on the build;
    // --- nothing outside the Build/ directory ever holds a BuildPlatform, so this is not API.
    ///@{
    Context* context() const { return context_; }
    const BuildPlatformData* platform() const { return platform_; }
    const ea::string& outputDir() const { return outputDir_; }
    const ea::string& resourceDir() const { return resourceDir_; }
    const ea::string& stagingDir() const { return stagingDir_; }
    ea::vector<ea::string>& errors() { return errors_; }
    /// Settings the platforms of the current project live in; resolved per use, never cached.
    BuildSettings* GetSettings() const;
    /// Start an external tool and suspend the step until it reports back, then run 'resume'. Returns
    /// false when the process could not be started at all.
    bool StartProcess(const ea::string& program, const ea::vector<ea::string>& arguments,
        ea::function<bool(ea::string&)> resume, ea::string& message);
    /// Ask the driver to re-run the current step next frame instead of advancing (see AwaitAssets).
    void HoldStep() { stageHold_ = true; }
    /// The CMake build tree the engine binary directory belongs to, plus its cmake and generator.
    bool LocateEngineBuildTree(ea::string& tree, ea::string& cmakeCommand, ea::string& generator,
        ea::string& message) const;
    /// One entry out of a CMakeCache.txt, or empty when the file has no such key.
    ea::string ReadCMakeCacheEntry(const ea::string& cachePath, const ea::string& key) const;
    /// Number of bytes of every file below a directory, recursively.
    unsigned long long DirectorySize(const ea::string& directory) const;
    ///@}

    // --- Platform differences. The base answers with the desktop behaviour; subclasses override.
    ///@{
    /// Whether the engine-compile step belongs in the plan at all. Android declines: its gradle
    /// project compiles the engine from source, so there is nothing for the editor to build.
    virtual bool UsesEngineBuild() const { return true; }
    /// Directory the resource packages are placed in, derived from the output directory. Desktop and
    /// web mount beside the page or executable; Android mounts inside the generated project's assets.
    virtual ea::string ResolveResourceDir(const ea::string& outputDir) const { return outputDir; }
    /// Let the platform prepend whatever the engine compile needs before the shared "--build"
    /// arguments. Web injects the emsdk environment the toolchain's scripts expect, reading the cache
    /// named by 'buildTree'. Returning false aborts the build with the reason.
    virtual bool ConfigureEngineBuildArgs(ea::vector<ea::string>& /*arguments*/, const ea::string& /*cmakeCommand*/,
        const ea::string& /*buildTree*/, ea::string& /*message*/) const
    {
        return true;
    }
    /// Prove the engine compile produced this platform's host artifacts before packaging relies on
    /// them. Default is the desktop pair: the host binary and the engine library.
    virtual bool VerifyEngineArtifacts(const ea::string& bin, ea::string& message) const;
    /// The terminal packaging step for this platform: copy host binaries (desktop), assemble the wasm
    /// package (web), or write the gradle project (Android).
    virtual ea::unique_ptr<BuildStep> MakeRuntimeStep() = 0;
    /// Interpreter to run the emsdk python scripts with, preferring the python emsdk ships. Only web
    /// has one; others answer with a PATH fallback so a caller that asks still gets something.
    virtual ea::string ResolveEmsdkPython() const;
    /// Whether the autoRunAfterBuild switch can do anything here. Android has no host binary to
    /// launch from the machine that built it, so it declines.
    virtual bool SupportsAutoRun() const { return true; }
    /// Launch the finished package. Desktop runs the host executable, web serves the page over a
    /// local http server; neither waits. Default is the desktop behaviour.
    virtual void LaunchAfterBuild(const ea::string& outputDir) const;
    ///@}

protected:
    /// Assemble the ordered plan for 'platform_' from the shared steps plus this platform's runtime
    /// step. Called once at the start of every build.
    void ComposePlan();
    /// Move to the next step of the plan; finishes the build when the plan is exhausted.
    void AdvanceStep();
    /// Finish the build: log it, hand the outcome to the handler, run the package if asked, and drop
    /// the temporary tree. Only reachable from a build that is still running.
    void Finish(bool success, const ea::string& message);
    /// Remove the staging tree. Called on every exit path so a build leaves nothing but its output.
    void CleanupStaging();

    Context* context_ = nullptr;
    /// Build platform currently in effect; owned by the BuildSettings of the project, which outlives
    /// the build because only one build runs at a time and nothing reloads settings mid-build.
    const BuildPlatformData* platform_{};
    /// Snapshot of the platform name, because the pointer above must not outlive its settings.
    ea::string platformName_;
    /// Output directory, absolute, trailing slash.
    ea::string outputDir_;
    /// Where resources go inside the output; a subdirectory of the output on Android.
    ea::string resourceDir_;
    /// Temporary tree the staged resources are assembled in before they are exported.
    ea::string stagingDir_;

    ea::vector<ea::unique_ptr<BuildStep>> plan_;
    /// Index of the step being executed within the plan.
    unsigned stepIndex_{};
    bool running_ = false;
    /// Request id of the running process, or zero while no step is waiting for one.
    unsigned pendingRequest_{};
    /// Command line of the running process, kept because it is the thing a user copies when a step
    /// fails - the process itself writes its output where nobody can see it.
    ea::string pendingCommandLine_;
    /// True once the waiting step learned how the process ended.
    bool processFinished_ = false;
    int processExitCode_{};
    /// Rest of the step that started the process, run once the process exited cleanly.
    ea::function<bool(ea::string&)> stepResume_;
    /// Set by a step that wants to run again next frame instead of advancing. Consumed by Tick.
    bool stageHold_ = false;
    bool cancelRequested_ = false;
    /// Completion of the plan so far, updated on every step boundary.
    float progress_{};
    DoneHandler onDone_;
    /// Problems found by the steps of the current build, kept for the tab after the build ended.
    ea::vector<ea::string> errors_;
    /// Seconds elapsed when the build started, so the summary can state the wall time honestly.
    float startTime_{};
};

/// Windows / Linux / macOS host: copies the built executable and engine library beside the data.
class DesktopBuildPlatform : public BuildPlatform
{
public:
    explicit DesktopBuildPlatform(Context* context)
        : BuildPlatform(context)
    {
    }

    ea::unique_ptr<BuildStep> MakeRuntimeStep() override;
};

/// Android: mounts resources under the gradle project's assets/, then writes that project. No engine
/// compile, no auto-run - the phone builds and launches it.
class AndroidBuildPlatform : public BuildPlatform
{
public:
    explicit AndroidBuildPlatform(Context* context)
        : BuildPlatform(context)
    {
    }

    bool UsesEngineBuild() const override { return false; }
    ea::string ResolveResourceDir(const ea::string& outputDir) const override;
    ea::unique_ptr<BuildStep> MakeRuntimeStep() override;
    bool SupportsAutoRun() const override { return false; }
    void LaunchAfterBuild(const ea::string& /*outputDir*/) const override { }
};

/// Web: builds the wasm host through emsdk, bundles the packages with file_packager into a preload
/// archive, and drops a serve.py beside the page (wasm only loads over http, never file://).
class WebBuildPlatform : public BuildPlatform
{
public:
    explicit WebBuildPlatform(Context* context)
        : BuildPlatform(context)
    {
    }

    bool ConfigureEngineBuildArgs(ea::vector<ea::string>& arguments, const ea::string& cmakeCommand,
        const ea::string& buildTree, ea::string& message) const override;
    bool VerifyEngineArtifacts(const ea::string& bin, ea::string& message) const override;
    ea::unique_ptr<BuildStep> MakeRuntimeStep() override;
    ea::string ResolveEmsdkPython() const override;
    void LaunchAfterBuild(const ea::string& outputDir) const override;

    // Reached by the web runtime step, which downcasts the BuildStep owner to get here.
    /// Root of the emscripten toolchain that owns file_packager.py, resolved from the platform, the
    /// emsdk environment variables, or the CMake cache of the web build - whichever answers first.
    ea::string ResolveEmscriptenRoot(ea::string& message) const;
    /// Interpreter shipped inside an emsdk root ("<emsdk>/python/<version>/python.exe"), or empty.
    ea::string FindBundledPython(const ea::string& emsdkRoot) const;
};

/// Resolve the platform of the build platform named 'platformName' to its backend, or null when no such
/// platform exists (the caller then reports the failure). Unknown platform strings fall back to desktop
/// so this stays total; an unknown name never reaches a real build because the platform is validated.
ea::unique_ptr<BuildPlatform> CreateBuildPlatform(Context* context, const ea::string& platformName);

} // namespace Urho3D

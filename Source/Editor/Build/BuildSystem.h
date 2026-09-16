// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#pragma once

#include "../Assets/TextureImportSettings.h"

#include <Urho3D/Core/Object.h>
#include <Urho3D/Core/Variant.h>

#include <EASTL/functional.h>
#include <EASTL/vector.h>

namespace Urho3D
{

class BuildProfile;
class BuildSettings;

/// One stage of a build. A build walks a plan of these in order; which stages are in the plan
/// depends on the profile, so progress is always measured against the current plan and never
/// against a fixed count.
///
/// Every stage either finishes on the spot or starts one external process and hands the rest of its
/// work to a continuation that runs when that process reports back. Keeping it to one process per
/// stage is what makes the pipeline resumable between frames without a sub-state machine.
enum class BuildStage
{
    Idle,
    /// Compiles the C++ engine host into its build tree; first when the profile asks for it,
    /// because it produces the artifacts the Validate stage checks for.
    EngineBuild,
    /// Preconditions of the profile; collects every problem instead of stopping at the first one.
    Validate,
    /// Blocks until the AssetManager has finished cooking imported assets, so StageData can copy a
    /// complete set of Cache outputs into the package. Held here rather than waited on inside a
    /// process because asset cooking is asynchronous across frames.
    AwaitAssets,
    /// Empties the output directory so a build can never ship a file an earlier build left behind.
    CleanOutput,
    /// Engine Data/ first, project Data/ on top of it, so a project can override any engine file.
    StageData,
    /// PVRTexTool over every staged texture, replacing each source with its platform format; only in
    /// the plan when the profile enables texture compression. The tool runs once per texture, so this
    /// stage chains its continuation across frames instead of finishing inside a single process.
    CompressTextures,
    /// LuaCompiler over the staged tree; only in the plan when the profile encrypts scripts.
    CompileScripts,
    /// Engine CoreData/ into staging. No project override: that directory belongs to the engine.
    StageCoreData,
    /// Data.pak (or a loose Data/ directory) into the package.
    ExportData,
    /// CoreData.pak (or a loose CoreData/ directory) into the package.
    ExportCoreData,
    /// Host executable and the shared libraries it needs; desktop only, gradle compiles its own.
    StageRuntime,
    /// The web host page and its sidecars plus the preloaded resource bundle; web only.
    WebRuntime,
    /// Self contained gradle project next to the assets; Android only.
    AndroidProject,
    /// What ended up where, and how long it took.
    Summary,
};

/// Name of a stage as it appears in the log and in the build tab.
const char* BuildStageName(BuildStage stage);

/// Event sent when a build finishes, whether it succeeded or not. Parameters: Success (bool),
/// Profile (String), Message (String, empty on success), OutputDir (String). The Lua side reaches
/// the same four fields through Editor.subscribe("buildFinished", fn).
extern const StringHash E_BUILD_FINISHED;

/// Turns a build profile into a deliverable. Driven by E_BEGINFRAME so that neither the editor UI
/// nor a headless run is ever blocked by a copy or by an external tool, which is the only reason
/// this is a state machine and not a function.
class BuildSystem : public Object
{
    URHO3D_OBJECT(BuildSystem, Object);

public:
    /// Called once per build with the outcome. The first argument is a human readable reason, empty
    /// on success; the second is the output directory the build was told to write into.
    using DoneHandler = ea::function<void(bool success, const ea::string& message, const ea::string& outputDir)>;

    explicit BuildSystem(Context* context);

    /// Start building a profile of the current project. Returns false when a build is already
    /// running or when the profile cannot be found, both of which are reported here rather than
    /// through the handler, because the caller asked synchronously and expects an answer now.
    /// outputOverride replaces the directory from the profile, which is what lets a verification
    /// run write outside the repository.
    bool BuildNow(const ea::string& profileName, const ea::string& outputOverride = EMPTY_STRING,
        DoneHandler onDone = DoneHandler());

    /// Stop at the next stage boundary. A process that is already running is allowed to finish:
    /// the API that started it hands out no handle that could kill it, and pretending otherwise
    /// would leave a half written package in the output directory.
    void Cancel();

    bool IsBuilding() const { return stage_ != BuildStage::Idle; }
    const ea::string& GetProfileName() const { return profileName_; }
    /// Directory the last build was told to write into, absolute with a trailing slash. Kept after
    /// the build ends so that the tab can point at what it produced.
    const ea::string& GetOutputDir() const { return outputDir_; }
    /// Fraction of the plan already finished, in [0, 1]. It stays where a build left it, so a tab
    /// can still show 1.0 right after a success instead of dropping back to zero.
    float GetProgress() const { return progress_; }
    BuildStage GetStage() const { return stage_; }
    ea::string GetStageName() const { return BuildStageName(stage_); }
    const ea::vector<ea::string>& GetErrors() const { return errors_; }
    /// Interpreter to run the emsdk python scripts with, preferring the python emsdk ships over
    /// whatever happens to be on PATH. Also what a web package is served with.
    ea::string ResolveEmsdkPython() const;

private:
    /// One staged texture waiting to be compressed. Built once per build by StageCompressTextures and
    /// walked by RunTextureQueue across as many frames as there are cache misses, which is why the
    /// queue and its cursor live on the object rather than on a stage's local stack.
    struct TextureJob
    {
        /// Path relative to the staged Data/, for logging.
        ea::string relative_;
        /// Staged source the tool reads, removed once its cooked product is in place.
        ea::string stagedSource_;
        /// Staged path of the cooked product: the same name with the container extension.
        ea::string stagedDest_;
        /// Staged legacy sidecar directory ("<source>.d"), removed so editor-only leftovers of the
        /// previous metadata scheme never ship. The current scheme has no sidecar: the ".texmeta"
        /// file staged from Data/ is both the import metadata and the runtime parameter file, so it
        /// ships as-is and needs no handling here.
        ea::string legacySidecarDir_;
        /// Persistent product under Artifacts, named by a hash of the source fingerprint and settings.
        ea::string cacheProduct_;
        /// Whether the per-file metadata tagged this texture as a normal map.
        bool isNormal_{};
        /// Per-file import params resolved at staging time: drive the color space variant and
        /// the mip chain.
        TextureImporterParams params_;
        /// Whether a mip chain is baked into the product: the per-file mode resolved against the profile.
        bool mipmaps_{};
    };

    /// Advance the plan. Runs on every frame while a build is active, and does nothing while the
    /// current stage is waiting for a process.
    void HandleBeginFrame(StringHash eventType, VariantMap& eventData);
    void HandleAsyncExecFinished(StringHash eventType, VariantMap& eventData);

    /// Execute the current stage. Returns false to abort the build with the reason in 'message'.
    bool RunStage(ea::string& message);
    /// Settings the profiles of the current project live in. Resolved through the project on every
    /// use rather than cached, because a build cannot outlive the project it belongs to and a cached
    /// pointer would have to be invalidated the moment one is closed.
    BuildSettings* GetSettings() const;
    /// Stage implementations, one per BuildStage value that can appear in a plan.
    bool StageEngineBuild(ea::string& message);
    bool StageValidate(ea::string& message);
    bool StageAwaitAssets(ea::string& message);
    bool StageCleanOutput(ea::string& message);
    bool StageStageData(ea::string& message);
    bool StageCompressTextures(ea::string& message);
    bool StageCompileScripts(ea::string& message);
    bool StageStageCoreData(ea::string& message);
    bool StageExportData(ea::string& message, bool coreData);
    bool StageStageRuntime(ea::string& message);
    bool StageAndroidProject(ea::string& message);
    bool StageSummary(ea::string& message);
    /// Continuations of the stages that shell out.
    bool PruneStagedSources(ea::string& message);
    bool VerifyExportedResources(ea::string& message, bool coreData);
    /// Walk the texture queue: copy every cache hit into staging and run the tool for the rest one at
    /// a time. Returns true only once all of them are placed; a tool run suspends it until the frame
    /// after FinalizeCookedTexture reports back.
    bool RunTextureQueue(ea::string& message);
    /// Continuation after one texture finished cooking: install its product, then keep the queue going.
    bool FinalizeCookedTexture(unsigned index, ea::string& message);
    /// Place a cooked product into staging and drop the source and metadata it replaced.
    bool InstallCookedTexture(const TextureJob& job, ea::string& message);
    /// Place the web host page and its sidecars, bundle the exported resources into the preloaded
    /// data archive, and write the local serving script.
    bool StageWebRuntime(ea::string& message);
    bool FinalizeWebRuntime(ea::string& message);
    /// Continuation of StageEngineBuild: prove the compile actually produced the host artifacts
    /// before the rest of the plan starts relying on them.
    bool FinalizeEngineBuild(ea::string& message);
    /// The CMake build tree the engine binary directory belongs to, plus the cmake executable and
    /// generator name its cache names. False (with a reason in 'message') when there is no cache
    /// above the binaries - a tree that was never configured.
    bool LocateEngineBuildTree(ea::string& tree, ea::string& cmakeCommand, ea::string& generator,
        ea::string& message) const;
    /// One entry out of a CMakeCache.txt, or empty when the file has no such key. The value keeps
    /// the case it was written with; surrounding whitespace is trimmed.
    ea::string ReadCMakeCacheEntry(const ea::string& cachePath, const ea::string& key) const;
    /// Directory of the emscripten toolchain that owns file_packager.py: the profile's emsdk root,
    /// the emsdk environment variables, or the CMake cache of the web build, whichever answers
    /// first. Empty (with a reason in 'message') when none of them does.
    ea::string ResolveEmscriptenRoot(ea::string& message) const;
    /// Interpreter shipped inside an emsdk root ("<emsdk>/python/<version>/python.exe"), or empty
    /// when that sdk bundles none.
    ea::string FindBundledPython(const ea::string& emsdkRoot) const;
    /// serve.py next to the page: a wasm module only loads over http, never from file://.
    bool WriteWebServeScript(ea::string& message) const;
    /// Absolute path of the Data/ file a staged texture was copied from, or empty when it came from
    /// somewhere else. Feeds both the import metadata lookup and the half of the cache key that has to
    /// survive a rebuild.
    ea::string FindOriginalDataFile(const ea::string& relative) const;

    /// Move to the next stage of the plan; finishes the build when the plan is exhausted.
    void AdvanceStage();
    /// Start an external tool and suspend the stage until it reports back, then run 'resume'.
    /// Returns false when the process could not be started at all.
    bool StartProcess(const ea::string& program, const ea::vector<ea::string>& arguments,
        ea::function<bool(ea::string&)> resume, ea::string& message);
    /// Number of bytes of every file below a directory, recursively.
    unsigned long long DirectorySize(const ea::string& directory) const;
    /// Finish the build: log it, send the finish event, run the handler and drop the temporary
    /// tree. Only reachable from a build that is still running, which is what keeps it from being
    /// reported twice.
    void Finish(bool success, const ea::string& message);
    /// Remove the staging tree. Called on every exit path so a build leaves nothing but its output.
    void CleanupStaging();

    /// Build profile currently in effect. Owned by the BuildSettings of the project, which outlives
    /// the build because only one build runs at a time and nothing reloads settings mid-build.
    const BuildProfile* profile_{};
    /// Snapshot of the profile name, because the pointer above must not outlive its settings.
    ea::string profileName_;
    /// Output directory, absolute, trailing slash.
    ea::string outputDir_;
    /// Where resources go inside the output; a subdirectory of the output on Android.
    ea::string resourceDir_;
    /// Temporary tree the staged resources are assembled in before they are exported.
    ea::string stagingDir_;
    ea::vector<BuildStage> plan_;
    /// Index of the stage being executed within the plan.
    unsigned stageIndex_{};
    BuildStage stage_ = BuildStage::Idle;
    /// Request id of the running process, or zero while no stage is waiting for one.
    unsigned pendingRequest_{};
    /// Command line of the running process, kept because it is the thing a user has to copy when a
    /// stage fails - the process itself writes its output where nobody can see it.
    ea::string pendingCommandLine_;
    /// True once the waiting stage learned how the process ended.
    bool processFinished_ = false;
    /// Set by a stage that wants to run again on the next frame instead of advancing (see
    /// StageAwaitAssets). Consumed by HandleBeginFrame, which then skips AdvanceStage for one frame.
    bool stageHold_ = false;
    int processExitCode_{};
    /// Rest of the stage that started the process, run once the process exited cleanly.
    ea::function<bool(ea::string&)> stageResume_;
    bool cancelRequested_ = false;
    /// Completion of the plan so far, updated on every stage boundary.
    float progress_{};
    DoneHandler onDone_;
    /// Problems found by the stages of the current build, kept for the tab after the build ended.
    ea::vector<ea::string> errors_;
    /// Seconds elapsed when the build started, so the summary can state the wall time honestly.
    float startTime_{};

    /// Textures waiting to be compressed this build, and how far the walk has gotten through them.
    ea::vector<TextureJob> textureQueue_;
    unsigned textureQueueIndex_{};
    /// PVRTexToolCLI resolved once per build, so a failure to find it aborts before any texture is touched.
    ea::string textureToolPath_;
    /// Cooked by the tool versus reused from the persistent cache, for the one-line summary.
    unsigned texturesCompressed_{};
    unsigned texturesCached_{};
};

} // namespace Urho3D

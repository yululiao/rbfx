// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#pragma once

#include "../../Assets/TextureImportSettings.h"
#include "../BuildPlatform.h"

#include <EASTL/string.h>
#include <EASTL/vector.h>

namespace Urho3D
{

/// Every stage of a build as a BuildStep subtype. The generic ones below are shared verbatim by all
/// platforms; the terminal packaging step at the bottom is the one each platform supplies through its
/// MakeRuntimeStep. A step never becomes an Object - it reaches subsystems through owner_.context()
/// and shells out through owner_.StartProcess, which registers the step's own continuation. This
/// header is the vocabulary of the pipeline; the frame driver that walks these lives in
/// BuildPlatform.cpp.

/// Preconditions of the platform; collects every problem into the platform's error list instead of
/// stopping at the first one, then fails if the list is non-empty.
class ValidateStep : public BuildStep
{
public:
    using BuildStep::BuildStep;
    const char* Name() const override { return "Validate platform"; }
    bool Run(ea::string& message) override;
};

/// Blocks until the AssetManager has finished cooking imported assets, so StageData can copy a
/// complete set of Cache outputs into the package. Held here rather than waited on inside a process
/// because asset cooking is asynchronous across frames.
class AwaitAssetsStep : public BuildStep
{
public:
    using BuildStep::BuildStep;
    const char* Name() const override { return "Wait for asset cooking"; }
    bool Run(ea::string& message) override;
};

/// Empties the output directory so a build can never ship a file an earlier build left behind.
class CleanOutputStep : public BuildStep
{
public:
    using BuildStep::BuildStep;
    const char* Name() const override { return "Clean output directory"; }
    bool Run(ea::string& message) override;
};

/// Engine Data/ first, project Data/ on top of it, so a project can override any engine file, then
/// the cooked Cache outputs under the resource names the scenes already point at.
class StageDataStep : public BuildStep
{
public:
    using BuildStep::BuildStep;
    const char* Name() const override { return "Stage Data"; }
    bool Run(ea::string& message) override;
};

/// LuaCompiler over the staged tree; only in the plan when the platform encrypts scripts. Its
/// continuation removes the plain sources that now ship compiled.
class CompileScriptsStep : public BuildStep
{
public:
    using BuildStep::BuildStep;
    const char* Name() const override { return "Compile Lua scripts"; }
    bool Run(ea::string& message) override;

private:
    bool PruneStagedSources(ea::string& message);
};

/// Engine CoreData/ into staging. No project override: that directory belongs to the engine.
class StageCoreDataStep : public BuildStep
{
public:
    using BuildStep::BuildStep;
    const char* Name() const override { return "Stage CoreData"; }
    bool Run(ea::string& message) override;
};

/// Data.pak (or a loose Data/ directory) into the package; the same step packs CoreData when the plan
/// built it with coreData set, so the two export stages share one implementation.
class ExportDataStep : public BuildStep
{
public:
    ExportDataStep(BuildPlatform& owner, bool coreData)
        : BuildStep(owner)
        , coreData_(coreData)
    {
    }
    const char* Name() const override { return coreData_ ? "Export CoreData" : "Export Data"; }
    bool Run(ea::string& message) override;

private:
    /// Continuation after PackageTool: read the result back with the class the game will use.
    bool VerifyExportedResources(ea::string& message);

    bool coreData_{};
};

/// Compiles the C++ engine host into its build tree; first when the platform asks for it, because it
/// produces the artifacts the Validate stage then checks for. The compile itself is shared; the
/// arguments the toolchain needs and the proof of what it produced are the platform's (see
/// BuildPlatform::ConfigureEngineBuildArgs / VerifyEngineArtifacts).
class EngineBuildStep : public BuildStep
{
public:
    using BuildStep::BuildStep;
    const char* Name() const override { return "Compile engine host"; }
    bool Run(ea::string& message) override;

private:
    /// Continuation after the compile: prove it actually produced the host artifacts before the rest
    /// of the plan starts relying on them.
    bool Finalize(ea::string& message);
};

/// What ended up where, and how long it took. Reads the output tree back so the numbers describe the
/// package rather than what the steps think they wrote.
class SummaryStep : public BuildStep
{
public:
    using BuildStep::BuildStep;
    const char* Name() const override { return "Summary"; }
    bool Run(ea::string& message) override;
};

/// PVRTexTool over every staged texture, replacing each source with its platform format; only in the
/// plan when the platform enables texture compression. The tool runs once per texture, so this step
/// chains its continuation across as many frames as there are cache misses - and owns all of that
/// walk's state, which lives nowhere else in the build.
class CompressTexturesStep : public BuildStep
{
public:
    using BuildStep::BuildStep;
    const char* Name() const override { return "Compress textures"; }
    bool Run(ea::string& message) override;

private:
    /// One staged texture waiting to be compressed.
    struct Job
    {
        /// Path relative to the staged Data/, for logging.
        ea::string relative_;
        /// Staged source the tool reads, removed once its cooked product is in place.
        ea::string stagedSource_;
        /// Staged path of the cooked product: the same name with the container extension.
        ea::string stagedDest_;
        /// Staged legacy sidecar directory ("<source>.d"), removed so editor-only leftovers of the
        /// previous metadata scheme never ship.
        ea::string legacySidecarDir_;
        /// Persistent product under Artifacts, named by a hash of the source fingerprint and settings.
        ea::string cacheProduct_;
        /// Whether the per-file metadata tagged this texture as a normal map.
        bool isNormal_{};
        /// Per-file import params resolved at staging time: drive the color space variant and the mip
        /// chain.
        TextureImporterParams params_{};
        /// Whether a mip chain is baked into the product: the per-file mode resolved against the platform.
        bool mipmaps_{};
    };

    /// Walk the queue: copy every cache hit into staging and run the tool for the rest one at a time.
    /// Returns true only once all are placed; a tool run suspends it until the next frame.
    bool RunQueue(ea::string& message);
    /// Continuation after one texture finished cooking: install its product, then keep the queue going.
    bool FinalizeCooked(unsigned index, ea::string& message);
    /// Place a cooked product into staging and drop the source and metadata it replaced.
    bool InstallCooked(const Job& job, ea::string& message);
    /// Absolute path of the Data/ file a staged texture was copied from, or empty when it came from
    /// somewhere else. Feeds both the import metadata lookup and the cache key's durable half.
    ea::string FindOriginalDataFile(const ea::string& relative) const;

    ea::vector<Job> queue_;
    unsigned index_{};
    /// PVRTexToolCLI resolved once, so a failure to find it aborts before any texture is touched.
    ea::string toolPath_;
    /// Cooked by the tool versus reused from the persistent cache, for the one-line summary.
    unsigned compressed_{};
    unsigned cached_{};
};

// --- Terminal packaging steps. Each platform's MakeRuntimeStep returns the one below that fits it;
// --- these are the last step before the summary, and the only stage whose body is not shared.

/// Desktop host: copy the built executable and the engine library beside the packaged data so the
/// output runs on its own.
class DesktopRuntimeStep : public BuildStep
{
public:
    using BuildStep::BuildStep;
    const char* Name() const override { return "Copy runtime binaries"; }
    bool Run(ea::string& message) override;
};

/// Android: write the gradle project around the staged resources. The engine and the game are
/// compiled on the phone's side, so assembling that tree is the whole platform-specific payload.
class AndroidRuntimeStep : public BuildStep
{
public:
    using BuildStep::BuildStep;
    const char* Name() const override { return "Write Android project"; }
    bool Run(ea::string& message) override;
};

/// Web: copy the wasm host trio, bundle the packages into a preload archive with file_packager - a
/// process, so the tail runs in a continuation - and drop serve.py beside the page.
class WebRuntimeStep : public BuildStep
{
public:
    explicit WebRuntimeStep(class WebBuildPlatform& owner)
        : BuildStep(owner)
        , web_(owner)
    {
    }
    const char* Name() const override { return "Assemble web package"; }
    bool Run(ea::string& message) override;

private:
    /// Continuation after file_packager: prove the loader and archive arrived, discard the files that
    /// were inlined into them, and write serve.py.
    bool Finalize(ea::string& message);
    /// Emit the local http server the page needs - a wasm module refuses to load over file://.
    bool WriteServeScript(ea::string& message);

    class WebBuildPlatform& web_;
};

} // namespace Urho3D

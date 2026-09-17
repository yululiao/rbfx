// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

// Engine-host compilation stage as a BuildStep: the one step that shells out to CMake to produce the
// host binaries the rest of the plan then packages. The shared "--build" driving lives here; the
// toolchain arguments and the proof of what the compile produced are the platform's, reached through
// owner_.ConfigureEngineBuildArgs / VerifyEngineArtifacts. The build-tree probing it leans on
// (LocateEngineBuildTree, ReadCMakeCacheEntry) is on BuildPlatform because the web backend needs it
// too.

#include "../../Assets/TextureImportSettings.h"
#include "../BuildPlatform.h"
#include "../BuildSettings.h"
#include "BuildSteps.h"

#include <Urho3D/Core/StringUtils.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/Log.h>

#include <EASTL/algorithm.h>

namespace Urho3D
{

bool EngineBuildStep::Run(ea::string& message)
{
    const BuildPlatformData* platform = owner_.platform();

    ea::string tree, cmakeCommand, generator;
    if (!owner_.LocateEngineBuildTree(tree, cmakeCommand, generator, message))
        return false;

    ea::vector<ea::string> arguments;
    if (!owner_.ConfigureEngineBuildArgs(arguments, cmakeCommand, tree, message))
        return false;

    arguments.push_back("--build");
    arguments.push_back(RemoveTrailingSlash(tree));
    arguments.push_back("--target");
    arguments.push_back(HostName);

    // Multi-config generators pick the configuration at build time, and the engine binary
    // directory already names it (".../bin/Release"). Single-config trees baked theirs in at
    // configure time and ignore --config, so it is not passed to them at all.
    const bool multiConfig = generator.find("Visual Studio") != ea::string::npos
        || generator.find("Xcode") != ea::string::npos
        || generator.find("Multi-Config") != ea::string::npos;
    if (multiConfig)
    {
        const ea::string config = GetFileNameAndExtension(RemoveTrailingSlash(NormalizeDir(platform->engineBin_)));
        const char* const knownConfigs[] = {"Debug", "Release", "RelWithDebInfo", "MinSizeRel"};
        const char* const* const configEnd = knownConfigs + 4;
        const bool configKnown = ea::find_if(knownConfigs, configEnd,
            [&config](const char* known) { return config.comparei(known) == 0; }) != configEnd;
        arguments.push_back("--config");
        arguments.push_back(configKnown ? config : ea::string("Release"));
    }

    if (platform->engineBuild_ == EngineBuildMode::Rebuild)
        arguments.push_back("--clean-first");

    // The cache names the exact cmake that configured the tree; only a tree configured by a cmake
    // since gone from the machine falls back to whatever the editor finds on PATH.
    return owner_.StartProcess(cmakeCommand.empty() ? ea::string("cmake") : cmakeCommand, arguments,
        [this](ea::string& resumeMessage) { return Finalize(resumeMessage); }, message);
}

bool EngineBuildStep::Finalize(ea::string& message)
{
    const ea::string bin = NormalizeDir(owner_.platform()->engineBin_);
    if (!owner_.VerifyEngineArtifacts(bin, message))
        return false;

    URHO3D_LOGINFO("[Build] Engine host is up to date in '{}'", bin);
    return true;
}

} // namespace Urho3D

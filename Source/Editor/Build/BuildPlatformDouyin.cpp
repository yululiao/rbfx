// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

// Douyin backend. The compile side is the shared emsdk machinery; what is specific here is the
// payload: the compile target is the vendor module rather than a complete host application, the
// game files are staged into a subpackage because the vendor caps the main package well below the
// size of the game data, and nothing on this machine can launch the result - the vendor devtool
// plays it.

#include "BuildPlatform.h"
#include "BuildSettings.h"
#include "Stages/BuildSteps.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/Core/StringUtils.h>
#include <Urho3D/IO/FileSystem.h>

namespace Urho3D
{

namespace
{

// The engine binary directory sits under a "bin" segment of its build tree - "<tree>/bin/Release"
// or "<tree>/bin" - which is the layout every configure of this project produces, presets
// included. Walking up to that segment turns "where the binaries live", which is all the platform
// field names, back into "where the tree lives". Nothing else is consulted: the tree is allowed
// to not exist yet, which is the very situation this serves.
ea::string DeriveBuildTree(const ea::string& engineBin)
{
    ea::string candidate = RemoveTrailingSlash(NormalizeDir(engineBin));
    for (unsigned level = 0; level < 4; ++level)
    {
        if (GetFileNameAndExtension(candidate) == "bin")
            return NormalizeDir(GetPath(candidate));
        candidate = RemoveTrailingSlash(GetPath(candidate));
        if (candidate.empty() || candidate.size() < 2)
            break;
    }
    return EMPTY_STRING;
}

// The source directory cannot be recovered from a tree that does not exist, so it is found the
// way a person would: walking up from the running editor towards the checkout, which is
// recognizable by the preset file and the sources sitting next to each other. Six levels cover
// the "<checkout>/<build tree>/bin/<config>" depth of the layout the project ships with.
ea::string FindRepositoryRoot(FileSystem* fs, const ea::string& startDir)
{
    ea::string candidate = RemoveTrailingSlash(NormalizeDir(startDir));
    for (unsigned level = 0; level < 6; ++level)
    {
        if (fs->FileExists(candidate + "/CMakePresets.json")
            && fs->FileExists(candidate + "/Source/CMakeLists.txt"))
        {
            return NormalizeDir(candidate);
        }
        candidate = RemoveTrailingSlash(GetPath(candidate));
        if (candidate.empty() || candidate.size() < 2)
            break;
    }
    return EMPTY_STRING;
}

} // namespace

ea::string DouyinBuildPlatform::ResolveResourceDir(const ea::string& outputDir) const
{
    // The staged resources are exported straight into the root of the data subpackage: the runtime
    // downloads that subpackage on demand, and the assembler maps the engine-visible file names
    // back onto it (see DouyinRuntimeStep).
    return outputDir + MinigameDataSubpackageName + "/";
}

bool DouyinBuildPlatform::VerifyEngineArtifacts(const ea::string& bin, ea::string& message) const
{
    auto* fs = context()->GetSubsystem<FileSystem>();
    for (const char* extension : {".js", ".wasm"})
    {
        const ea::string artifact = bin + MinigameHostName + extension;
        if (!fs->FileExists(artifact))
        {
            message = Format("The engine build finished but '{}' is still not there.", artifact);
            return false;
        }
    }
    return true;
}

bool DouyinBuildPlatform::ConfigureEngineBuildTreeFromScratch(ea::string& tree, ea::string& cmakeCommand,
    ea::vector<ea::string>& arguments, const ea::vector<ea::string>& featureDefines,
    ea::string& message) const
{
    auto* fs = context()->GetSubsystem<FileSystem>();

    // Without a binary directory there is no way to say where the tree would live, and the
    // caller's own "not set" report is already the actionable one; leave it standing.
    if (platform_->engineBin_.empty())
        return false;

    tree = DeriveBuildTree(platform_->engineBin_);
    if (tree.empty())
    {
        message = Format("Cannot configure a build tree from scratch: the engine binary directory "
            "'{}' does not follow the '<build tree>/bin/<config>' layout, so there is no directory "
            "to configure into.", platform_->engineBin_);
        return false;
    }

    const ea::string sourceRoot = FindRepositoryRoot(fs, fs->GetProgramDir());
    if (sourceRoot.empty())
    {
        message = "Cannot configure a build tree from scratch: the engine sources were not found "
            "above the editor directory. Configure the tree manually first, for example "
            "'cmake --preset minigame-douyin', and try again.";
        return false;
    }

    // The editor's own build tree - one of the parents of the running editor - holds the exact
    // cmake that configured this machine, and on a Visual Studio machine names the instance whose
    // bundled Ninja the tree can be configured with. The first cache found on the way up wins,
    // and a miss fails nothing here: cmake and ninja fall back to PATH, which is where a machine
    // that reaches this point either has them or reports their absence on its own.
    ea::string ninja;
    ea::string candidate = RemoveTrailingSlash(NormalizeDir(fs->GetProgramDir()));
    for (unsigned level = 0; level < 6; ++level)
    {
        const ea::string cache = candidate + "/CMakeCache.txt";
        if (fs->FileExists(cache))
        {
            const ea::string cmake = ReadCMakeCacheEntry(cache, "CMAKE_COMMAND");
            if (!cmake.empty() && fs->FileExists(cmake))
                cmakeCommand = cmake;
#if defined(_WIN32)
            const ea::string instance = ReadCMakeCacheEntry(cache, "CMAKE_GENERATOR_INSTANCE");
            if (!instance.empty())
            {
                const ea::string bundled = RemoveTrailingSlash(instance)
                    + "/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe";
                if (fs->FileExists(bundled))
                    ninja = bundled;
            }
#endif
            break;
        }
        candidate = RemoveTrailingSlash(GetPath(candidate));
        if (candidate.empty() || candidate.size() < 2)
            break;
    }

    // The toolchain the configure has to name before any tree exists to read it back from.
    ea::string toolchainMessage;
    const ea::string toolchain = ResolveEmscriptenRoot(toolchainMessage);
    if (toolchain.empty())
    {
        message = toolchainMessage;
        return false;
    }

    // The same emsdk environment injection a compile of an existing tree gets; the configure
    // invocation itself goes after it, as the command the injected environment runs.
    if (!ConfigureEngineBuildArgs(arguments, cmakeCommand, tree, message))
        return false;

    // Everything below mirrors the "minigame-base" preset from CMakePresets.json, spelled out
    // rather than picked by preset name: no shell runs inside the checkout, so the source and
    // tree directories are named absolutely, and the tree lands where the configured binary
    // directory says rather than where a preset convention would put it. The module selection
    // arrives as the same "-D<module>=<ON|OFF>" list a reconfigure would apply.
    arguments.push_back("-S");
    arguments.push_back(RemoveTrailingSlash(sourceRoot));
    arguments.push_back("-B");
    arguments.push_back(RemoveTrailingSlash(tree));
    arguments.push_back("-G");
    arguments.push_back("Ninja");
    arguments.push_back("-DCMAKE_BUILD_TYPE=Release");
    arguments.push_back("-DCMAKE_TOOLCHAIN_FILE=" + RemoveTrailingSlash(toolchain)
        + "/cmake/Modules/Platform/Emscripten.cmake");
    arguments.push_back("-DEMSCRIPTEN_ROOT_PATH=" + toolchain);
    if (!ninja.empty())
        arguments.push_back("-DCMAKE_MAKE_PROGRAM=" + ninja);
    arguments.push_back("-DURHO3D_MINIGAME=ON");
    arguments.push_back("-DURHO3D_THREADING=OFF");
    arguments.push_back("-DURHO3D_ENABLE_ALL=ON");
    arguments.push_back("-DURHO3D_TESTING=OFF");
    arguments.push_back("-DURHO3D_SAMPLES=OFF");
    arguments.push_back("-DURHO3D_PLAYER=OFF");
    arguments.push_back("-DURHO3D_EDITOR=OFF");
    for (const ea::string& define : featureDefines)
        arguments.push_back("-D" + define);
    return true;
}

ea::unique_ptr<BuildStep> DouyinBuildPlatform::MakeRuntimeStep()
{
    return ea::make_unique<DouyinRuntimeStep>(*this);
}

} // namespace Urho3D

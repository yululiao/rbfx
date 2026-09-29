// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

// Shared emsdk support for the emscripten-backed platforms. They all face one problem: the engine
// compile and the python scripts running beside it expect an emsdk environment the editor process
// knows nothing about. Everything needed to reach it lives here - resolving the toolchain root,
// injecting the environment its scripts expect, and preferring the interpreter the sdk ships - so
// the web and minigame platforms share the probing instead of cloning it.

#include "BuildPlatform.h"
#include "BuildSettings.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/Core/StringUtils.h>
#include <Urho3D/IO/FileSystem.h>

#include <EASTL/algorithm.h>

#include <cstdlib>

namespace Urho3D
{

bool EmscriptenBuildPlatform::ConfigureEngineBuildArgs(ea::vector<ea::string>& arguments,
    const ea::string& cmakeCommand, const ea::string& buildTree, ea::string& message) const
{
    // The toolchain lives on paths the editor process knows nothing about, and its scripts expect
    // the variables an `emsdk activate` shell would export. `cmake -E env` injects them for the
    // compile and for nothing else. The cache that configured the tree names the toolchain, which is
    // the one that compiled everything already in it.
    ea::string ignored;
    ea::string toolchain = ReadCMakeCacheEntry(RemoveTrailingSlash(buildTree) + "/CMakeCache.txt",
        "EMSCRIPTEN_ROOT_PATH");
    if (toolchain.empty())
        toolchain = ResolveEmscriptenRoot(ignored);
    if (toolchain.empty())
    {
        message = "Could not determine the emsdk toolchain for the build. Set the "
            "EmsdkRoot platform field or activate emsdk before starting the editor.";
        return false;
    }

    // The sdk root sits two levels above the toolchain directory ("<emsdk>/upstream/emscripten").
    const ea::string emsdkRoot = NormalizeDir(
        GetPath(RemoveTrailingSlash(GetPath(RemoveTrailingSlash(toolchain)))));
    arguments.push_back("-E");
    arguments.push_back("env");
    arguments.push_back("EMSDK=" + RemoveTrailingSlash(emsdkRoot));
    arguments.push_back("EM_CONFIG=" + RemoveTrailingSlash(emsdkRoot) + "/.emscripten");
    // An activated shell also exports EMSDK_PYTHON and puts the bundled interpreter first on PATH.
    // Both are reproduced here so the toolchain's python scripts (emcc, file_packager, ...) never
    // depend on whatever python the user happens to have installed.
    ea::string pythonDir;
    const ea::string emsdkPython = FindBundledPython(emsdkRoot);
    if (!emsdkPython.empty())
    {
        arguments.push_back("EMSDK_PYTHON=" + emsdkPython);
        pythonDir = NormalizeDir(GetPath(RemoveTrailingSlash(emsdkPython)));
    }
    const char* const pathSeparator =
#if defined(_WIN32)
        ";";
#else
        ":";
#endif
    const char* const currentPath = getenv("PATH");
    ea::string path = RemoveTrailingSlash(toolchain);
    if (!pythonDir.empty())
        path += pathSeparator + RemoveTrailingSlash(pythonDir);
    if (currentPath && *currentPath)
        path += ea::string(pathSeparator) + currentPath;
    arguments.push_back("PATH=" + path);
    arguments.push_back("--");
    // `cmake -E env` runs the word after `--` as its command, so the build has to be spelled out as
    // another cmake invocation inside the injected environment. Without it the `--build` that follows
    // would be the command cmake tries and fails to execute.
    arguments.push_back(cmakeCommand.empty() ? ea::string("cmake") : cmakeCommand);
    return true;
}

bool EmscriptenBuildPlatform::CollectFeatureReconfigure(const ea::string& buildTree,
    ea::vector<ea::string>& defines, bool& needsReconfigure, ea::string& /*message*/) const
{
    needsReconfigure = false;

    // The selection is spelled out for every module, enabled or not. Passing only the disabled ones
    // would be enough to prune, but it would leave an "=OFF" behind in the cache for a checkbox
    // that was unchecked once and is checked again: the cache remembers it, and the tree would keep
    // producing a pruned module that nothing on screen asks for anymore.
    const auto& disabled = platform_->disabledEngineModules_;
    const ea::string cachePath = RemoveTrailingSlash(buildTree) + "/CMakeCache.txt";
    for (const EngineModuleInfo& module : GetEngineModules())
    {
        const bool enabled = ea::find(disabled.begin(), disabled.end(), module.name_) == disabled.end();
        defines.push_back(module.name_ + (enabled ? "=ON" : "=OFF"));

        // A cache that disagrees - including one from before the option existed, which reads as
        // absent - means the tree was configured for a different module selection than the one on
        // screen, so it cannot produce the module the platform describes until it is reconfigured.
        const ea::string cached = ReadCMakeCacheEntry(cachePath, module.name_);
        if (cached.comparei(enabled ? "ON" : "OFF") != 0)
            needsReconfigure = true;
    }
    return true;
}

ea::string EmscriptenBuildPlatform::ResolveEmscriptenRoot(ea::string& message) const
{
    auto* fs = context()->GetSubsystem<FileSystem>();

    // A candidate is only as good as the packager inside it.
    const auto hasPackager = [&fs](const ea::string& dir)
    {
        return dir.empty() || !fs->FileExists(NormalizeDir(dir) + "tools/file_packager.py")
            ? EMPTY_STRING : NormalizeDir(dir);
    };

    ea::string result;
    // What the platform says beats what the machine happens to have activated right now. Accept both
    // the emsdk root and the toolchain directory itself; the field is a path people paste, and a
    // paste of either should just work.
    if (!platform_->emsdkRoot_.empty())
    {
        result = hasPackager(platform_->emsdkRoot_);
        if (result.empty())
            result = hasPackager(NormalizeDir(platform_->emsdkRoot_) + "upstream/emscripten");
    }
    // The variables below are what emsdk activation exports, so on a machine where emsdk was
    // activated before the editor started no platform field is needed at all.
    if (result.empty())
    {
        if (const char* emscripten = getenv("EMSCRIPTEN"); emscripten && *emscripten)
            result = hasPackager(emscripten);
    }
    if (result.empty())
    {
        if (const char* emsdk = getenv("EMSDK"); emsdk && *emsdk)
            result = hasPackager(NormalizeDir(emsdk) + "upstream/emscripten");
    }
    if (result.empty())
    {
        // Last resort: the build tree is somewhere above the engine binary directory, and its
        // CMake cache names the toolchain even on a machine where emsdk was never activated.
        ea::string tree, cmakeCommand, generator;
        ea::string ignored;
        if (LocateEngineBuildTree(tree, cmakeCommand, generator, ignored))
        {
            result = hasPackager(ReadCMakeCacheEntry(RemoveTrailingSlash(tree) + "/CMakeCache.txt",
                "EMSCRIPTEN_ROOT_PATH"));
        }
    }
    if (result.empty())
    {
        message = "Could not find file_packager.py. Point the EmsdkRoot platform field at the "
            "emsdk directory, or activate emsdk before starting the editor.";
        return EMPTY_STRING;
    }
    return result;
}

ea::string EmscriptenBuildPlatform::FindBundledPython(const ea::string& emsdkRoot) const
{
    if (emsdkRoot.empty())
        return EMPTY_STRING;
    auto* fs = context()->GetSubsystem<FileSystem>();

    // "<emsdk>/python/<version>" is where the sdk keeps the interpreter its own scripts are tested
    // with. One version at a time is installed, so first hit wins.
    ea::vector<ea::string> versions;
    fs->ScanDir(versions, NormalizeDir(emsdkRoot) + "python", "*", SCAN_DIRS);
    for (const ea::string& version : versions)
    {
        if (version == "." || version == "..")
            continue;
#if defined(_WIN32)
        const ea::string candidate = NormalizeDir(emsdkRoot) + "python/" + version + "/python.exe";
#else
        const ea::string candidate = NormalizeDir(emsdkRoot) + "python/" + version + "/bin/python3";
#endif
        if (fs->FileExists(candidate))
            return candidate;
    }
    return EMPTY_STRING;
}

ea::string EmscriptenBuildPlatform::FindBundledNode(const ea::string& emsdkRoot) const
{
    if (emsdkRoot.empty())
        return EMPTY_STRING;
    auto* fs = context()->GetSubsystem<FileSystem>();

    // "<emsdk>/node/<version>_64bit" is where the sdk keeps the runtime its own toolchain scripts
    // run on. One version at a time is installed, so first hit wins.
    ea::vector<ea::string> versions;
    fs->ScanDir(versions, NormalizeDir(emsdkRoot) + "node", "*", SCAN_DIRS);
    for (const ea::string& version : versions)
    {
        if (version == "." || version == "..")
            continue;
#if defined(_WIN32)
        const ea::string candidate = NormalizeDir(emsdkRoot) + "node/" + version + "/node.exe";
#else
        const ea::string candidate = NormalizeDir(emsdkRoot) + "node/" + version + "/bin/node";
#endif
        if (fs->FileExists(candidate))
            return candidate;
    }
    return EMPTY_STRING;
}

ea::string EmscriptenBuildPlatform::ResolveEmsdkPython() const
{
    // The toolchain scripts need nothing beyond the standard library, but the interpreter emsdk
    // ships is the one they are tested with, so it wins when it is there. The sdk root is two
    // levels above the toolchain directory ResolveEmscriptenRoot answers with.
    ea::string ignored;
    const ea::string emscriptenRoot = ResolveEmscriptenRoot(ignored);
    if (!emscriptenRoot.empty())
    {
        const ea::string emsdkRoot = NormalizeDir(
            GetPath(RemoveTrailingSlash(GetPath(RemoveTrailingSlash(emscriptenRoot)))));
        const ea::string bundled = FindBundledPython(emsdkRoot);
        if (!bundled.empty())
            return bundled;
    }
    // Whatever is on PATH then; a machine without any python at all cannot run those scripts anyway.
#if defined(_WIN32)
    return "python";
#else
    return "python3";
#endif
}

ea::string EmscriptenBuildPlatform::ResolveEmsdkNode() const
{
    // The compression step rides the runtime emsdk itself runs on, for the same reason the python
    // resolution prefers the bundled interpreter: it is the one the sdk is tested with, and it is
    // where a brotli encoder is guaranteed to be present. Unlike python, no script depends on the
    // exact version here, only on the encoder it carries.
    ea::string ignored;
    const ea::string emscriptenRoot = ResolveEmscriptenRoot(ignored);
    if (!emscriptenRoot.empty())
    {
        const ea::string emsdkRoot = NormalizeDir(
            GetPath(RemoveTrailingSlash(GetPath(RemoveTrailingSlash(emscriptenRoot)))));
        const ea::string bundled = FindBundledNode(emsdkRoot);
        if (!bundled.empty())
            return bundled;
    }
    // Whatever is on PATH then; a machine that built a minigame module at all has a node somewhere,
    // and a too-old one surfaces as the script's own error output.
    return "node";
}

} // namespace Urho3D

// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

// Web backend. It is the platform with the most genuine differences: the engine compile has to run
// inside an emsdk environment the editor process knows nothing about, the artifacts to prove are
// html/js/wasm rather than an executable, the finished package is a browser payload assembled by
// file_packager (the terminal WebRuntimeStep, in Stages/), and "run it" means serving it over http.
// The emsdk probing below is shared by the compile hook and that step, so it lives on the platform.

#include "BuildPlatform.h"
#include "BuildSettings.h"
#include "Stages/BuildSteps.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/Core/StringUtils.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/Log.h>

#include <cstdlib>

namespace Urho3D
{

bool WebBuildPlatform::ConfigureEngineBuildArgs(ea::vector<ea::string>& arguments, const ea::string& cmakeCommand,
    const ea::string& buildTree, ea::string& message) const
{
    // The emsdk toolchain lives on paths the editor process knows nothing about, and its scripts
    // expect the variables an `emsdk activate` shell would export. `cmake -E env` injects them for
    // the compile and for nothing else. The cache that configured the tree names the toolchain, which
    // is the one that compiled everything already in it.
    ea::string ignored;
    ea::string toolchain = ReadCMakeCacheEntry(RemoveTrailingSlash(buildTree) + "/CMakeCache.txt",
        "EMSCRIPTEN_ROOT_PATH");
    if (toolchain.empty())
        toolchain = ResolveEmscriptenRoot(ignored);
    if (toolchain.empty())
    {
        message = "Could not determine the emsdk toolchain for the web build. Set the "
            "WebEmsdkRoot platform field or activate emsdk before starting the editor.";
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

bool WebBuildPlatform::VerifyEngineArtifacts(const ea::string& bin, ea::string& message) const
{
    auto* fs = context()->GetSubsystem<FileSystem>();
    for (const char* extension : {".html", ".js", ".wasm"})
    {
        const ea::string artifact = bin + HostName + extension;
        if (!fs->FileExists(artifact))
        {
            message = Format("The engine build finished but '{}' is still not there.", artifact);
            return false;
        }
    }
    return true;
}

ea::unique_ptr<BuildStep> WebBuildPlatform::MakeRuntimeStep()
{
    return ea::make_unique<WebRuntimeStep>(*this);
}

ea::string WebBuildPlatform::ResolveEmscriptenRoot(ea::string& message) const
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
    if (!platform_->webEmsdkRoot_.empty())
    {
        result = hasPackager(platform_->webEmsdkRoot_);
        if (result.empty())
            result = hasPackager(NormalizeDir(platform_->webEmsdkRoot_) + "upstream/emscripten");
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
        // Last resort: the web build tree is somewhere above the engine binary directory, and its
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
        message = "Could not find file_packager.py. Point the WebEmsdkRoot platform field at the "
            "emsdk directory, or activate emsdk before starting the editor.";
        return EMPTY_STRING;
    }
    return result;
}

ea::string WebBuildPlatform::FindBundledPython(const ea::string& emsdkRoot) const
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

ea::string WebBuildPlatform::ResolveEmsdkPython() const
{
    // The packager needs nothing beyond the standard library, but the interpreter emsdk ships is the
    // one its own scripts are tested with, so it wins when it is there. The sdk root is two levels
    // above the toolchain directory ResolveEmscriptenRoot answers with.
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
    // Whatever is on PATH then; a machine without any python at all cannot run the packager anyway.
#if defined(_WIN32)
    return "python";
#else
    return "python3";
#endif
}

void WebBuildPlatform::LaunchAfterBuild(const ea::string& outputDir) const
{
    // The web equivalent of running the result is the serving script, which opens the browser once it
    // is listening, and which stays running for exactly the same reason a game window does. The
    // interpreter that packaged the resources is the one that should serve them.
    const ea::string serveScript = outputDir + "serve.py";
    auto* fs = context()->GetSubsystem<FileSystem>();
    if (fs->FileExists(serveScript))
    {
        URHO3D_LOGINFO("[Build] Launching {}", serveScript);
        fs->SystemRunAsync(ResolveEmsdkPython(), {serveScript});
    }
    else
        URHO3D_LOGERROR("[Build] Cannot run '{}': it is not in the output directory", serveScript);
}

} // namespace Urho3D

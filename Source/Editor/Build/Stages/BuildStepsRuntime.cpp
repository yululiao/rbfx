// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

// The terminal packaging step of each platform as a BuildStep: copy the host binaries (desktop),
// write the gradle project (Android), or assemble the wasm preload package and its serving script
// (web). Everything before this is shared; these are the last stage before the summary and the one
// thing each BuildPlatform subclass contributes beyond its handful of overridden questions. A step
// reaches the resolved paths and the process runner through owner_; the web step also needs its
// platform's emsdk probing, so it holds the concrete WebBuildPlatform it was made by.

#include "../AndroidScaffold.h"
#include "../BuildPlatform.h"
#include "../BuildSettings.h"
#include "BuildSteps.h"
#include "../../Project/Project.h"

#include <Urho3D/Core/ProcessUtils.h>
#include <Urho3D/Core/StringUtils.h>
#include <Urho3D/IO/File.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/Log.h>

namespace Urho3D
{

bool DesktopRuntimeStep::Run(ea::string& message)
{
    auto* fs = owner_.context()->GetSubsystem<FileSystem>();

    const BuildPlatformData* platform = owner_.platform();
    const ea::string bin = NormalizeDir(platform->engineBin_);
    const ea::string outputDir = owner_.outputDir();
    const ea::string suffix = GetExecutableSuffix();

    struct Artifact
    {
        ea::string source_;
        ea::string destination_;
    };

    const Artifact artifacts[] = {
        { bin + HostName + suffix, outputDir + platform->executableName_ + suffix },
        { bin + EngineLibraryName + DYN_LIB_SUFFIX, outputDir + EngineLibraryName + DYN_LIB_SUFFIX },
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

bool AndroidRuntimeStep::Run(ea::string& message)
{
    auto* project = owner_.context()->GetSubsystem<Project>();

    ea::vector<ea::string> scaffoldErrors;
    if (GenerateAndroidScaffold(owner_.context(), *owner_.platform(),
            ForwardSlashes(project->GetProjectPath()), owner_.outputDir(), owner_.resourceDir(), scaffoldErrors))
    {
        return true;
    }

    owner_.errors().insert(owner_.errors().end(), scaffoldErrors.begin(), scaffoldErrors.end());
    for (const ea::string& error : scaffoldErrors)
        URHO3D_LOGERROR("[Build] {}", error);
    message = Format("The Android project could not be written, {} problem(s) above.", scaffoldErrors.size());
    return false;
}

bool WebRuntimeStep::Run(ea::string& message)
{
    auto* fs = owner_.context()->GetSubsystem<FileSystem>();

    const BuildPlatformData* platform = owner_.platform();
    const ea::string bin = NormalizeDir(platform->engineBin_);
    const ea::string& outputDir = owner_.outputDir();
    const ea::string& resourceDir = owner_.resourceDir();

    // Only the page carries the platform's executable name. The script cannot: it references the wasm
    // by the file name baked into it at link time, so those two keep the host's names.
    struct Artifact
    {
        ea::string source_;
        ea::string destination_;
    };
    const Artifact artifacts[] = {
        { bin + HostName + ".html", outputDir + platform->executableName_ + ".html" },
        { bin + HostName + ".js", outputDir + HostName + ".js" },
        { bin + HostName + ".wasm", outputDir + HostName + ".wasm" },
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

    // The page was linked against a preRun hook that fetches Resources.js, which in turn downloads
    // the data archive next to it. Rebuilding that pair around the project's packages with the same
    // packager the engine build used is the whole difference between a web package and a desktop one
    // that happens to contain a browser.
    const ea::string emscriptenRoot = web_.ResolveEmscriptenRoot(message);
    if (emscriptenRoot.empty())
        return false;

    ea::vector<ea::string> arguments;
    arguments.push_back(emscriptenRoot + "tools/file_packager.py");
    // Target first: that is where the archive goes, the --js-output names the loader beside it.
    arguments.push_back(outputDir + "Resources.js.data");
    arguments.push_back("--preload");
    // Mount names are what LuaGamePlayer asks the virtual file system for. The packages sit in the
    // root of the preloaded filesystem, exactly where package_resources_web put them during the engine
    // build; loose directories mount under their EP_RESOURCE_PATHS names instead.
    if (platform->packData_)
    {
        arguments.push_back(resourceDir + DataPackageName + "@" + DataPackageName);
        arguments.push_back(resourceDir + CoreDataPackageName + "@" + CoreDataPackageName);
    }
    else
    {
        arguments.push_back(resourceDir + DataDirName + "@/" + RemoveTrailingSlash(DataDirName));
        arguments.push_back(resourceDir + CoreDataDirName + "@/" + RemoveTrailingSlash(CoreDataDirName));
    }
    arguments.push_back("--js-output=" + outputDir + "Resources.js");
    arguments.push_back("--use-preload-cache");

    return owner_.StartProcess(web_.ResolveEmsdkPython(), arguments,
        [this](ea::string& resumeMessage) { return Finalize(resumeMessage); }, message);
}

bool WebRuntimeStep::Finalize(ea::string& message)
{
    auto* fs = owner_.context()->GetSubsystem<FileSystem>();
    const BuildPlatformData* platform = owner_.platform();
    const ea::string& outputDir = owner_.outputDir();
    const ea::string& resourceDir = owner_.resourceDir();

    // An exit code of zero proves little here - a packager that disliked its arguments can still
    // write nothing - so the loader and the archive it should have produced are checked by hand.
    const char* const artifacts[] = {"Resources.js", "Resources.js.data"};
    for (const char* artifact : artifacts)
    {
        if (!fs->FileExists(outputDir + artifact))
        {
            message = Format("file_packager produced no '{}'.", outputDir + artifact);
            return false;
        }
    }

    // Everything the archive swallowed is dead weight beside the page: the browser downloads it
    // inside Resources.js.data, so a copy next to it only doubles the package. A file somebody holds
    // open stays behind with a warning rather than failing an otherwise finished build.
    const auto discard = [&fs](const ea::string& path)
    {
        if (fs->DirExists(path))
        {
            if (!fs->RemoveDir(path, true))
                URHO3D_LOGWARNING("[Build] Could not remove the inlined '{}'; delete it by hand", path);
        }
        else if (fs->FileExists(path) && !fs->Delete(path))
            URHO3D_LOGWARNING("[Build] Could not remove the inlined '{}'; delete it by hand", path);
    };
    discard(resourceDir + DataPackageName);
    discard(resourceDir + CoreDataPackageName);
    if (!platform->packData_)
    {
        discard(resourceDir + DataDirName);
        discard(resourceDir + CoreDataDirName);
    }

    if (!WriteServeScript(message))
        return false;

    URHO3D_LOGINFO("[Build] Web package is ready; run 'python serve.py' in '{}' and the browser "
        "opens {} by itself", outputDir, platform->executableName_ + ".html");
    return true;
}

bool WebRuntimeStep::WriteServeScript(ea::string& message)
{
    const BuildPlatformData* platform = owner_.platform();
    const ea::string& outputDir = owner_.outputDir();

    // A raw literal keeps the script readable where it is written; the single substitution is the
    // page name, which is the platform's executable name, matching what Run copied.
    const char* const script = R"PY(import http.server
import os
import socket
import socketserver
import webbrowser

# Generated by the editor next to the game page it serves. A wasm module refuses to load over
# file://, so playing a web package always goes through a local http server like this one.
PAGE = "{page}"

DEFAULT_PORT = 8000


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True


def pick_port():
    # A previous build may still be serving on the default port; take the next free one instead
    # of failing, so a rebuild-and-play loop never stalls on a stale server.
    for port in range(DEFAULT_PORT, DEFAULT_PORT + 20):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
            try:
                probe.bind(("", port))
                return port
            except OSError:
                continue
    raise SystemExit(f"No free port in {DEFAULT_PORT}..{DEFAULT_PORT + 19}")


if __name__ == "__main__":
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    port = pick_port()
    url = f"http://localhost:{port}/{PAGE}"
    print(f"Serving {url} - press Ctrl+C to stop")
    webbrowser.open(url)
    with Server(("", port), http.server.SimpleHTTPRequestHandler) as server:
        server.serve_forever()
)PY";

    const ea::string path = outputDir + "serve.py";
    File file(owner_.context(), path, FILE_WRITE);
    if (!file.IsOpen())
    {
        message = Format("Could not write '{}'.", path);
        return false;
    }
    ea::string text{script};
    text.replace("{page}", platform->executableName_ + ".html");
    if (file.Write(text.data(), text.size()) != text.size())
    {
        message = Format("Could not write '{}'.", path);
        return false;
    }
    return true;
}

} // namespace Urho3D

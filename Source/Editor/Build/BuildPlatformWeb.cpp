// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

// Web backend. The emsdk machinery both this platform and its terminal step lean on is shared with
// the other emscripten-backed platforms (see BuildPlatformEmscripten.cpp); what remains here are
// the genuinely web-specific answers: the artifacts to prove are html/js/wasm rather than an
// executable, the finished package is a browser payload assembled by file_packager (the terminal
// WebRuntimeStep, in Stages/), and "run it" means serving it over http.

#include "BuildPlatform.h"
#include "Stages/BuildSteps.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/Core/StringUtils.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/Log.h>

namespace Urho3D
{

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

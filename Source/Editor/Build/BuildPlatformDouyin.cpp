// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

// Douyin backend. The compile side is the shared emsdk machinery; what is specific here is the
// payload: the compile target is the vendor module rather than a complete host application, the
// game files are staged into a subpackage because the vendor caps the main package well below the
// size of the game data, and nothing on this machine can launch the result - the vendor devtool
// plays it.

#include "BuildPlatform.h"
#include "Stages/BuildSteps.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/Core/StringUtils.h>
#include <Urho3D/IO/FileSystem.h>

namespace Urho3D
{

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

ea::unique_ptr<BuildStep> DouyinBuildPlatform::MakeRuntimeStep()
{
    return ea::make_unique<DouyinRuntimeStep>(*this);
}

} // namespace Urho3D

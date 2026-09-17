// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

// Android backend. It declines the engine compile (the gradle project builds the engine from source)
// and auto-run (there is no host binary to launch here), mounts resources inside the generated
// project's assets/, and ends the plan by writing that project. The remaining differences are stated
// inline in BuildPlatform.h; this file carries the resource mount and the terminal step.

#include "BuildPlatform.h"
#include "Stages/BuildSteps.h"

namespace Urho3D
{

ea::string AndroidBuildPlatform::ResolveResourceDir(const ea::string& outputDir) const
{
    // Resources are found by name, not by position, so the only difference from a desktop package is
    // where the mounted directories live: inside the assets folder of the generated gradle project
    // here, beside the executable there.
    return outputDir + "assets/";
}

ea::unique_ptr<BuildStep> AndroidBuildPlatform::MakeRuntimeStep()
{
    return ea::make_unique<AndroidRuntimeStep>(*this);
}

} // namespace Urho3D

// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

// Desktop backend. Nothing about a desktop build diverges from the shared pipeline except the
// terminal step - copy the host binary and the engine library beside the data - and that copy is the
// base BuildPlatform behaviour everywhere it is asked about (engine artifacts, auto-run, launch). So
// this file only hands the plan its last stage.

#include "BuildPlatform.h"
#include "Stages/BuildSteps.h"

namespace Urho3D
{

ea::unique_ptr<BuildStep> DesktopBuildPlatform::MakeRuntimeStep()
{
    return ea::make_unique<DesktopRuntimeStep>(*this);
}

} // namespace Urho3D

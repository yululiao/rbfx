// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#pragma once

#include <EASTL/string.h>
#include <EASTL/vector.h>

namespace Urho3D
{

class BuildProfile;
class Context;

/// Write a self contained gradle application project into 'outputDir', the way the engine's own
/// android/ directory is one, so the generated tree can be opened by Android Studio or built by the
/// companion script without any further setup.
///
/// The resource packages are expected to be in place already; the build pipeline exports them into
/// 'resourceDir' before this runs, and the generator checks that instead of assuming, because a
/// sourceSets entry pointing at nothing produces an apk that crashes at startup rather than one
/// that fails to build.
///
/// Nothing here runs gradle. A desktop machine without an Android SDK must still be able to produce
/// the project, hand it to somebody who has the SDK, and have it work; build_android.ps1 inside the
/// generated tree is what performs the build and reports precisely which tool is missing.
///
/// Returns false and fills 'errors' with every reason when something cannot be done.
bool GenerateAndroidScaffold(Context* context, const BuildProfile& profile, const ea::string& projectPath,
    const ea::string& outputDir, const ea::string& resourceDir, ea::vector<ea::string>& errors);

} // namespace Urho3D

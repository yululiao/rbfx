// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#pragma once

#include <Urho3D/Core/Object.h>
#include <Urho3D/Core/Variant.h>

#include <EASTL/functional.h>
#include <EASTL/string.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/vector.h>

namespace Urho3D
{

class BuildPlatform;

/// Event sent when a build finishes, whether it succeeded or not. Parameters: Success (bool),
/// Platform (String), Message (String, empty on success), OutputDir (String). The Lua side reaches
/// the same four fields through Editor.subscribe("buildFinished", fn).
extern const StringHash E_BUILD_FINISHED;

/// The editor's entry point to the build pipeline. It is deliberately thin: it owns one BuildPlatform
/// and forwards to it. Its whole job is the two pieces of plumbing a plain class cannot do for itself
/// - subscribing to the frame clock and the async-process notification the state machine runs on, and
/// firing the finish event when a build ends, which needs an Object. Resolving a platform, the plan of
/// stages, staging, cooking and shelling out to tools all live behind BuildPlatform, so nothing that
/// presses a button here can reach into a half-built plan.
class BuildSystem : public Object
{
    URHO3D_OBJECT(BuildSystem, Object);

public:
    /// Called once per build with the outcome. The first argument is a human readable reason, empty
    /// on success; the second is the output directory the build was told to write into.
    using DoneHandler = ea::function<void(bool success, const ea::string& message, const ea::string& outputDir)>;

    explicit BuildSystem(Context* context);

    /// Defined out of line because platform_ is a unique_ptr to the forward-declared BuildPlatform:
    /// destroying it here, where that type is still incomplete, would not compile.
    ~BuildSystem() override;

    /// Start building a platform of the current project. Returns false when a build is already
    /// running or when the platform cannot be found, both of which are reported in the log rather than
    /// through the handler, because the caller asked synchronously and expects an answer now.
    /// outputOverride replaces the directory from the platform, which is what lets a verification
    /// run write outside the repository.
    bool BuildNow(const ea::string& platformName, const ea::string& outputOverride = EMPTY_STRING,
        DoneHandler onDone = DoneHandler());

    /// Stop at the next stage boundary. A process that is already running is allowed to finish:
    /// the API that started it hands out no handle that could kill it, and pretending otherwise
    /// would leave a half written package in the output directory.
    void Cancel();

    // --- State of the current or most recent build, forwarded to the platform that is (or was)
    // --- running it. The accessors that hand out references answer with an empty value before the
    // --- first build, when there is no platform yet.
    ///@{
    bool IsBuilding() const;
    const ea::string& GetPlatformName() const;
    /// Directory the last build was told to write into, absolute with a trailing slash. Kept after
    /// the build ends so that the tab can point at what it produced.
    const ea::string& GetOutputDir() const;
    /// Fraction of the plan already finished, in [0, 1]. It stays where a build left it, so a tab
    /// can still show 1.0 right after a success instead of dropping back to zero.
    float GetProgress() const;
    ea::string GetStageName() const;
    const ea::vector<ea::string>& GetErrors() const;
    /// Interpreter to run the emsdk python scripts with, preferring the python emsdk ships over
    /// whatever happens to be on PATH. Also what a web package is served with.
    ea::string ResolveEmsdkPython() const;
    ///@}

private:
    /// The frame clock and the async-process notification, forwarded to the running platform. Both
    /// do nothing until a build has put a platform in place.
    void HandleBeginFrame(StringHash eventType, VariantMap& eventData);
    void HandleAsyncExecFinished(StringHash eventType, VariantMap& eventData);

    /// Backend for the platform of the current build, replaced at the start of every build so a
    /// platform may target any platform. Kept after a build ends so the tab can still read the
    /// result and, for a web package, resolve the interpreter to serve it with.
    ea::unique_ptr<BuildPlatform> platform_;
};

} // namespace Urho3D

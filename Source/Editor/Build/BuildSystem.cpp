// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

// The editor-facing half of the build system: subscribe to the two engine events the state machine
// runs on, hand each to the BuildPlatform that owns the running build, and wrap the completion so the
// finish event fires from an Object. There is no pipeline logic here on purpose - the plan, the
// stages and every tool invocation live behind BuildPlatform.

#include "BuildPlatform.h"
#include "BuildSystem.h"

#include <Urho3D/Core/CoreEvents.h>
#include <Urho3D/IO/IOEvents.h>
#include <Urho3D/IO/Log.h>

namespace Urho3D
{

const StringHash E_BUILD_FINISHED("buildFinished");

namespace
{

// References the forwarding accessors hand back before a build has ever run, when there is no
// platform to read a real value from. Function-local statics so they are initialised on first use and
// never depend on link order against the EASTL allocator.
const ea::string& NoString()
{
    static const ea::string empty;
    return empty;
}

const ea::vector<ea::string>& NoErrors()
{
    static const ea::vector<ea::string> empty;
    return empty;
}

} // namespace

BuildSystem::BuildSystem(Context* context)
    : Object(context)
{
    // BeginFrame is the clock of the state machine, and the async-exec notification is how a tool that
    // finished between two frames is heard. The platform does the driving; this object only relays.
    SubscribeToEvent(E_BEGINFRAME, URHO3D_HANDLER(BuildSystem, HandleBeginFrame));
    SubscribeToEvent(E_ASYNCEXECFINISHED, URHO3D_HANDLER(BuildSystem, HandleAsyncExecFinished));
}

BuildSystem::~BuildSystem() = default;

bool BuildSystem::BuildNow(const ea::string& platformName, const ea::string& outputOverride, DoneHandler onDone)
{
    // Refuse while a build is in flight. Starting one swaps the backend, and the running build lives
    // entirely inside the current one, so replacing it would abandon a plan half walked.
    if (platform_ && platform_->IsBuilding())
    {
        URHO3D_LOGERROR("[Build] Platform '{}' is still building", platform_->GetPlatformName());
        return false;
    }

    auto backend = CreateBuildPlatform(context_, platformName);
    if (!backend)
        return false;

    // The platform reports its outcome to this wrapper. Firing the engine event is the one thing the
    // pipeline cannot do for itself - sending an event needs an Object - so it is added here rather
    // than reaching out of BuildPlatform, and only then handed to the caller's own callback.
    const ea::string platformCopy = platformName;
    DoneHandler wrapper = [this, platformCopy, onDone = ea::move(onDone)](
                              bool success, const ea::string& message, const ea::string& outputDir) mutable
    {
        VariantMap eventData;
        eventData["Success"] = success;
        eventData["Platform"] = platformCopy;
        eventData["Message"] = success ? EMPTY_STRING : message;
        eventData["OutputDir"] = outputDir;
        SendEvent(E_BUILD_FINISHED, eventData);

        if (onDone)
            onDone(success, message, outputDir);
    };

    // StartBuild resolves the platform and, on success, composes the plan and begins walking it; it
    // reports a refused start synchronously (already building, no project, unknown platform) without
    // ever invoking the wrapper. Only keep the backend for a build that actually started.
    if (!backend->StartBuild(platformName, outputOverride, ea::move(wrapper)))
        return false;

    platform_ = ea::move(backend);
    return true;
}

void BuildSystem::Cancel()
{
    if (platform_)
        platform_->Cancel();
}

bool BuildSystem::IsBuilding() const
{
    return platform_ && platform_->IsBuilding();
}

const ea::string& BuildSystem::GetPlatformName() const
{
    return platform_ ? platform_->GetPlatformName() : NoString();
}

const ea::string& BuildSystem::GetOutputDir() const
{
    return platform_ ? platform_->GetOutputDir() : NoString();
}

float BuildSystem::GetProgress() const
{
    return platform_ ? platform_->GetProgress() : 0.0f;
}

ea::string BuildSystem::GetStageName() const
{
    return platform_ ? platform_->GetStageName() : ea::string("Idle");
}

const ea::vector<ea::string>& BuildSystem::GetErrors() const
{
    return platform_ ? platform_->GetErrors() : NoErrors();
}

ea::string BuildSystem::ResolveEmsdkPython() const
{
    return platform_ ? platform_->ResolveEmsdkPython() : ea::string();
}

void BuildSystem::HandleBeginFrame(StringHash eventType, VariantMap& eventData)
{
    if (platform_)
        platform_->Tick();
}

void BuildSystem::HandleAsyncExecFinished(StringHash eventType, VariantMap& eventData)
{
    if (!platform_)
        return;
    using namespace AsyncExecFinished;
    platform_->OnProcessFinished(eventData[P_REQUESTID].GetUInt(), eventData[P_EXITCODE].GetInt());
}

} // namespace Urho3D

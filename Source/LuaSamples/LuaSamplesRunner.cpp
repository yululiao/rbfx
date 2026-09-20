//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

// Host executable for the Lua rewrite of the built-in samples. Each sample
// lives in its own directory with a main.lua entry point and runs against
// the same engine bindings the editor plugin uses.

#include <Urho3D/Core/Context.h>
#include <Urho3D/Core/CoreEvents.h>
#include <Urho3D/Core/ProcessUtils.h>
#include <Urho3D/Core/StringUtils.h>
#include <Urho3D/Engine/Application.h>
#include <Urho3D/Engine/Engine.h>
#include <Urho3D/Engine/EngineDefs.h>
#include <Urho3D/IO/Log.h>
#include <LuaScript/EngineLuaVM.h>
#include <Urho3D/Graphics/Octree.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/SystemUI/Console.h>

using namespace Urho3D;

namespace
{

ea::string GetSampleNameFromArguments()
{
    // First positional argument. Note: GetArguments() already excludes the
    // executable path, so the scan starts at index 0.
    const ea::vector<ea::string>& arguments = GetArguments();
    for (unsigned i = 0; i < arguments.size(); ++i)
    {
        const ea::string& argument = arguments[i];
        if (!argument.empty() && argument.front() != '-')
            return argument;
    }
    return {};
}

} // namespace

class LuaSamplesRunner : public Application
{
    URHO3D_OBJECT(LuaSamplesRunner, Application);

public:
    explicit LuaSamplesRunner(Context* context)
        : Application(context)
    {
    }

    void Setup() override
    {
        // The positional argument selects the sample to run. The engine's CLI
        // parser rejects unknown positional arguments, so grab the sample
        // name here (Setup runs before the parser) and re-parse the command
        // line without it. Other engine options (--log-file, --windowed, ...)
        // still pass through; --timeout and --prefs-dir are consumed by the
        // runner itself because the engine's parameter merge silently drops
        // keys the application did not pre-define (see Start).
        sampleName_ = GetSampleNameFromArguments();
        const ea::vector<ea::string>& arguments = GetArguments();
        ea::string cmdLine;
        bool sampleNameSkipped = false;
        for (unsigned i = 0; i < arguments.size(); ++i)
        {
            const ea::string& argument = arguments[i];
            if (!sampleNameSkipped && !argument.empty() && argument.front() != '-')
            {
                sampleNameSkipped = true; // Drop the sample selector only
                continue;
            }
            if (argument == "--timeout" && i + 1 < arguments.size())
            {
                // Run-time bound used by the CI smoke gate. Consumed here and
                // dropped from the re-parsed line; Start() turns it into a
                // clean self-exit so the whole log is flushed (a kill from
                // outside loses the buffered tail).
                timeoutSecs_ = ToFloat(arguments[i + 1]);
                ++i; // Skip the value
                continue;
            }
            if (argument == "--prefs-dir" && i + 1 < arguments.size())
            {
                // Hermetic smoke runs: redirect the engine's user-preferences
                // directory (EngineParameters.json, psocache.bin, shader cache,
                // default logs -- everything under conf://) into a disposable
                // directory. Without this, the clean-exit shutdown saves into
                // the real user profile, which a CI agent or sandboxed shell
                // may refuse to write, fabricating [error] lines that have
                // nothing to do with the samples.
                engineParameters_[EP_APPLICATION_PREFERENCES_DIR] = arguments[i + 1].c_str();
                ++i; // Skip the value
                continue;
            }
            cmdLine.append_sprintf("\"%s\" ", argument.c_str());
        }
        ParseArguments(cmdLine, false);

        engineParameters_[EP_WINDOW_TITLE] = "Lua Samples";
        engineParameters_[EP_APPLICATION_NAME] = "Built-in Lua Samples";
        // Per-sample log file: avoids log handle contention when several
        // runners are started in sequence by test scripts.
        engineParameters_[EP_LOG_NAME] = sampleName_.empty()
            ? "conf://LuaSamples.log"
            : Format("conf://LuaSamples_{}.log", sampleName_);
        engineParameters_[EP_BORDERLESS] = false;
        engineParameters_[EP_HEADLESS] = false;
        engineParameters_[EP_SOUND] = true;
        engineParameters_[EP_RESOURCE_PATHS] = "CoreData;Data;LuaSamples";
        engineParameters_[EP_ORIENTATIONS] = "LandscapeLeft LandscapeRight Portrait";
        engineParameters_[EP_WINDOW_RESIZABLE] = true;
        if (!engineParameters_.contains(EP_RESOURCE_PREFIX_PATHS))
            engineParameters_[EP_RESOURCE_PREFIX_PATHS] = ";..;../..";
    }

    void Start() override
    {
        // Create and register the game-logic Lua subsystem.
        const auto luaScript = MakeShared<EngineLuaVM>(context_);
        context_->RegisterSubsystem(luaScript);

        // Create a scene and expose it as a global Lua variable.
        auto scene = MakeShared<Scene>(context_);
        scene->CreateComponent<Octree>();
        luaScript->SetGlobalScene("scene", scene.Get());

        // The C++ Sample base creates the console so samples can toggle it
        // (26_ConsoleInput). Mirror that behavior for the Lua samples.
        GetSubsystem<Engine>()->CreateConsole();

        if (sampleName_.empty())
        {
            URHO3D_LOGERROR(
                "Usage: LuaSamplesRunner <sample-name>, e.g. LuaSamplesRunner 01_HelloWorld");
            GetSubsystem<Engine>()->Exit();
            return;
        }

        if (!luaScript->ExecuteFile("Framework.lua"))
        {
            URHO3D_LOGERROR("Failed to load Framework.lua");
            GetSubsystem<Engine>()->Exit();
            return;
        }

        const ea::string samplePath = Format("{}/main.lua", sampleName_);
        if (!luaScript->ExecuteFile(samplePath))
        {
            URHO3D_LOGERROR("Failed to load Lua sample '{}'", samplePath);
            GetSubsystem<Engine>()->Exit();
            return;
        }

        URHO3D_LOGINFO("Lua sample '{}' started", sampleName_);

        // Self-exit after --timeout seconds of actual run time. Subscribed
        // LAST so the Framework.lua memory probe (which ticks once a second)
        // still gets its reading on the exit frame, and so the countdown
        // covers only the time the sample runs -- engine init and script
        // loading do not count.
        if (timeoutSecs_ > 0.0f)
        {
            SubscribeToEvent(E_UPDATE, [this](StringHash /*eventType*/, VariantMap& eventData) {
                runSecs_ += eventData[Update::P_TIMESTEP].GetFloat();
                if (runSecs_ >= timeoutSecs_)
                    GetSubsystem<Engine>()->Exit();
            });
        }
    }

private:
    /// Sample directory name taken from the first positional argument.
    ea::string sampleName_;
    /// Run-time limit in seconds from --timeout (0 = unlimited). Consumed in
    /// Setup and implemented as a self-exit in Start; never forwarded to the
    /// engine, whose parameter merge drops keys the app did not pre-define.
    float timeoutSecs_ = 0.0f;
    /// Run time accumulated by the --timeout handler.
    float runSecs_ = 0.0f;
};

URHO3D_DEFINE_APPLICATION_MAIN(LuaSamplesRunner);

//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

// Host executable for the Lua rewrite of the built-in samples. Each sample
// lives in its own directory with a main.lua entry point and runs against
// the same engine bindings the editor plugin uses.

#include <Urho3D/Core/Context.h>
#include <Urho3D/Core/ProcessUtils.h>
#include <Urho3D/Engine/Application.h>
#include <Urho3D/Engine/Engine.h>
#include <Urho3D/Engine/EngineDefs.h>
#include <Urho3D/IO/Log.h>
#include <LuaScript/LuaScript.h>
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
        // line without it. Engine options like --timeout still pass through.
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
        // Create and register the LuaScript subsystem.
        const auto luaScript = MakeShared<LuaScript>(context_);
        context_->RegisterSubsystem(luaScript);
        luaScript->Initialize();

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
    }

private:
    /// Sample directory name taken from the first positional argument.
    ea::string sampleName_;
};

URHO3D_DEFINE_APPLICATION_MAIN(LuaSamplesRunner);

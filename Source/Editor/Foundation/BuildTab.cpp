// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#include "../Foundation/BuildTab.h"

#include "../Core/IniHelpers.h"
#include "../Core/WidgetHelpers.h"
#include "../Project/Build/BuildSettings.h"
#include "../Project/Build/BuildSystem.h"
#include "../Project/Project.h"

#include <Urho3D/Core/StringUtils.h>
#include <Urho3D/IO/FileSystem.h>

#include <EASTL/algorithm.h>

#include <IconFontCppHeaders/IconsFontAwesome6.h>

#include <cstdlib>

namespace Urho3D
{

namespace
{

/// ui.ini key of the profile this tab last showed.
const char* const SettingsKeyProfile = "SelectedProfile";

/// The ABIs an Android build can target. A closed list on purpose, matching the one
/// BuildSettings::Validate accepts: a free text field here can produce an abiFilters entry that
/// fails two minutes into a native gradle build, which is a worse place to learn about it.
const char* const AndroidAbis[] = { "arm64-v8a", "armeabi-v7a", "x86", "x86_64" };

/// Labels of the engine build modes, in EngineBuildMode order so the combo index is the enum value.
const char* const EngineBuildLabels[] = { "Do not compile", "Incremental build", "Rebuild from clean" };
constexpr unsigned NumEngineBuildLabels = sizeof(EngineBuildLabels) / sizeof(EngineBuildLabels[0]);

/// True when the named environment variable exists and is not empty. Only the name is ever shown:
/// the value of a content key or a keystore password does not belong on screen, in a log, or in a
/// generated file.
bool EnvironmentIsSet(const ea::string& name)
{
    if (name.empty())
        return false;
    const char* value = getenv(name.c_str());
    return value && *value;
}

void RenderEnvironmentStatus(const ea::string& name)
{
    if (name.empty())
        ui::TextWrapped("(no environment variable named, so the build falls back to whatever the "
            "engine was configured with)");
    else if (EnvironmentIsSet(name))
        ui::Text("%s: set", name.c_str());
    else
        ui::TextWrapped(ICON_FA_TRIANGLE_EXCLAMATION " %s is not set in the environment", name.c_str());
}

bool ContainsAbi(const ea::vector<ea::string>& abis, const char* abi)
{
    return ea::find_if(abis.begin(), abis.end(), [abi](const ea::string& entry) { return entry == abi; })
        != abis.end();
}

} // namespace

void Foundation_BuildTab(Context* context, Project* project)
{
    const auto tab = MakeShared<BuildTab>(context);
    project->AddTab(tab);

    // Not opened by default: a project is built a handful of times a day, and a panel that is empty
    // the rest of the time takes height away from the scene view. The menu entry has to be
    // subscribed from here rather than drawn by the tab, because a closed tab draws nothing. The
    // receiver is the tab itself, which is what drops the subscription when the project closes.
    project->OnRenderProjectMenu.Subscribe(tab.Get(), [self = tab.Get()]
    {
        if (ui::MenuItem(ICON_FA_HAMMER " Build"))
            self->Focus();
    });
}

BuildTab::BuildTab(Context* context)
    : EditorTab(context, "Build", "e4b0a7c6-9d13-4f85-b2a7-6c08fd31e549", EditorTabFlag::None,
          EditorTabPlacement::DockBottom)
{
}

void BuildTab::RenderContent()
{
    auto* project = GetProject();
    auto* settings = project ? project->GetBuildSettings() : nullptr;
    auto* build = project ? project->GetBuildSystem() : nullptr;
    if (!settings || !build)
        return;

    BuildProfile* profile = ResolveProfile(settings);
    if (!profile)
    {
        ui::TextWrapped("This project has no build profiles. Add one to '%s' and reopen this tab.",
            settings->GetFilePath().c_str());
        return;
    }

    ui::Text("Build a self contained, runnable copy of the game. Everything below is stored in '%s'.",
        settings->GetFilePath().c_str());

    if (ui::BeginCombo("Profile", profile->name_.c_str()))
    {
        for (const BuildProfile& candidate : settings->GetProfiles())
        {
            if (ui::Selectable(candidate.name_.c_str(), candidate.name_ == profile->name_))
            {
                selectedProfile_ = candidate.name_;
                validationCountdown_ = 0;
            }
        }
        ui::EndCombo();
    }

    const ea::string outputDir = profile->ResolveOutputDir(project->GetProjectPath());
    ui::Text("Platform: %s", profile->platform_.empty() ? "(not set)" : profile->platform_.c_str());
    ui::Text("Output: %s", outputDir.c_str());

    // A build in flight reads the profile from memory, and a read only project must not gain files
    // from being looked at; both lock the same widgets.
    const bool readOnly = project->GetFlags().Test(ProjectFlag::ReadOnly);
    auto* fs = GetSubsystem<FileSystem>();

    {
        const bool building = build->IsBuilding();
        const ea::string executable = outputDir + profile->executableName_ + GetExecutableSuffix();

        ui::BeginDisabled(building || readOnly);
        if (ui::Button(ICON_FA_HAMMER " Build"))
        {
            // False only when no profile of that name exists or a build is already running, neither
            // of which this button can reach; the reason is in the log if it ever happens.
            build->BuildNow(profile->name_);
        }
        ui::EndDisabled();

        ui::SameLine();
        ui::BeginDisabled(!building);
        if (ui::Button(ICON_FA_BAN " Cancel"))
            build->Cancel();
        ui::EndDisabled();
        if (ui::IsItemHovered())
            ui::SetTooltip("Stop after the stage that is running now; a tool that already started is "
                "allowed to finish so that no half written file is left behind");

        ui::SameLine();
        ui::BeginDisabled(!fs->DirExists(outputDir));
        if (ui::Button(ICON_FA_FOLDER_OPEN " Open Output"))
            fs->SystemOpen(outputDir);
        ui::EndDisabled();

        if (!profile->IsAndroid())
        {
            ui::SameLine();
            if (profile->IsWeb())
            {
                // A page is not something to launch but to serve: the script starts a local server
                // and opens the browser as soon as it is listening.
                const ea::string serve = outputDir + "serve.py";
                ui::BeginDisabled(building || !fs->FileExists(serve));
                if (ui::Button(ICON_FA_PLAY " Run"))
                    fs->SystemRunAsync(build->ResolveEmsdkPython(), { serve });
                ui::EndDisabled();
                if (ui::IsItemHovered())
                    ui::SetTooltip("Serve the package that is currently in the output directory "
                        "and open it in the browser");
            }
            else
            {
                ui::BeginDisabled(building || !fs->FileExists(executable));
                if (ui::Button(ICON_FA_PLAY " Run"))
                    fs->SystemRunAsync(executable, {});
                ui::EndDisabled();
                if (ui::IsItemHovered())
                    ui::SetTooltip("Launch the game that is currently in the output directory");
            }
        }
    }

    ui::Separator();

    ui::BeginDisabled(build->IsBuilding() || readOnly);
    RenderPackageOptions(*profile);
    RenderTextureCompressionOptions(*profile);
    if (profile->IsAndroid())
        RenderAndroidOptions(*profile);
    if (profile->IsWeb())
        RenderWebOptions(*profile);
    ui::EndDisabled();

    RenderStatus(profile, settings, project);

    if (dirty_)
    {
        // Written on every change rather than when the tab closes: `Editor --build` reads the same
        // file, and a profile that only exists in the memory of the editor is a build that quietly
        // ignores what was just configured.
        if (!settings->SaveFile(settings->GetFilePath()))
            ui::TextWrapped(ICON_FA_TRIANGLE_EXCLAMATION " Could not write '%s'; the changes above "
                "will be lost when the project closes.", settings->GetFilePath().c_str());
        dirty_ = false;
        validationCountdown_ = 0;
    }
}

BuildProfile* BuildTab::ResolveProfile(BuildSettings* settings)
{
    BuildProfile* profile = settings->FindProfileMutable(selectedProfile_);
    if (profile)
        return profile;

    // The persisted name is allowed to be stale, because profiles are renamed or trimmed in the file
    // between sessions. Falling back to the first profile beats drawing nothing; the choice is kept
    // for this session only, so a name that came back later is honoured again.
    auto& profiles = settings->GetMutableProfiles();
    if (profiles.empty())
        return nullptr;
    profile = &profiles.front();
    selectedProfile_ = profile->name_;
    return profile;
}

void BuildTab::RenderPackageOptions(BuildProfile& profile)
{
    Touch(ui::InputText("Executable name", &profile.executableName_));
    if (ui::IsItemHovered())
        ui::SetTooltip("File name of the shipped game, without the platform suffix");

    Touch(PathField("Output directory", profile.outputDir_, PathFieldKind::Directory, nullptr,
        "Absolute, or relative to the project folder. It is emptied at the start of "
            "every build, so point it at a directory the build owns"));

    Touch(ui::Checkbox("Pack resources into .pak files", &profile.packData_));
    ui::Indent();
    ui::BeginDisabled(!profile.packData_);
    Touch(ui::Checkbox("Compress packages", &profile.compressPackages_));
    ui::EndDisabled();
    ui::Unindent();

    Touch(ui::Checkbox("Encrypt Lua scripts", &profile.encryptScripts_));
    ui::Indent();
    ui::BeginDisabled(!profile.encryptScripts_);
    Touch(ui::InputText("Script key environment variable", &profile.scriptKeyEnvVar_));
    if (ui::IsItemHovered())
        ui::SetTooltip("Named, never stored: the 64 hex character key is read from the environment "
            "at build time, and the runtime reads the same variable");
    RenderEnvironmentStatus(profile.scriptKeyEnvVar_);
    ui::EndDisabled();
    ui::Unindent();

    Touch(ui::Checkbox("Include engine Data/ files", &profile.includeEngineData_));
    if (ui::IsItemHovered())
        ui::SetTooltip("Project files win on name clashes, so turning this off only shrinks the "
            "package if the project already carries everything it needs");

    Touch(ui::Checkbox("Run the game when the build finishes", &profile.autoRunAfterBuild_));

    ui::Separator();
    Touch(PathField("Engine binaries", profile.engineBin_, PathFieldKind::Directory));
    Touch(PathField("Engine data", profile.engineData_, PathFieldKind::Directory));
    ui::TextWrapped("Both point at an engine you built yourself. A missing host file is reported "
        "together with the command that produces it, or compiled by the build itself when 'Compile "
        "engine host' below is on.");

    // Android hides the selector rather than disabling it: its gradle project compiles the engine
    // on its own, so the choice would be dead weight on screen.
    if (!profile.IsAndroid())
    {
        const int current = static_cast<int>(profile.engineBuild_);
        if (ui::BeginCombo("Compile engine host", EngineBuildLabels[current]))
        {
            for (int i = 0; i < static_cast<int>(NumEngineBuildLabels); ++i)
            {
                if (ui::Selectable(EngineBuildLabels[i], i == current))
                {
                    profile.engineBuild_ = static_cast<EngineBuildMode>(i);
                    Touch(true);
                }
            }
            ui::EndCombo();
        }
        if (ui::IsItemHovered())
            ui::SetTooltip("Whether the build compiles the C++ engine host into the CMake build "
                "tree above the engine binaries. Incremental reuses whatever is still up to date; "
                "Rebuild cleans the host's previous products first");
    }
}

void BuildTab::RenderAndroidOptions(BuildProfile& profile)
{
    AndroidBuildSettings& android = profile.android_;

    if (!ui::CollapsingHeader(ICON_FA_MOBILE_SCREEN_BUTTON " Android", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    Touch(ui::InputText("Application id", &android.applicationId_));
    Touch(ui::InputInt("Version code", &android.versionCode_));
    Touch(ui::InputText("Version name", &android.versionName_));
    Touch(ui::InputInt("Minimum SDK", &android.minSdk_));
    Touch(ui::InputInt("Target SDK", &android.targetSdk_));
    Touch(ui::InputText("Screen orientation", &android.orientation_));
    Touch(PathField("Launcher icon", android.icon_, PathFieldKind::File, "png",
        "Path to a png; empty keeps the icon the engine template ships with"));

    if (ui::TreeNodeEx("ABIs", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ui::TextWrapped("An ABI the engine was not configured for fails in the native build rather "
            "than at install time.");
        for (const char* abi : AndroidAbis)
        {
            bool enabled = ContainsAbi(android.abis_, abi);
            if (!ui::Checkbox(abi, &enabled))
                continue;

            Touch(true);
            const auto iter = ea::find_if(android.abis_.begin(), android.abis_.end(),
                [abi](const ea::string& entry) { return entry == abi; });
            if (enabled && iter == android.abis_.end())
                android.abis_.push_back(abi);
            else if (!enabled && iter != android.abis_.end())
                android.abis_.erase(iter);
        }
        ui::TreePop();
    }

    ui::Separator();
    Touch(ui::InputText("Keystore path environment variable", &android.keystoreEnvVar_));
    RenderEnvironmentStatus(android.keystoreEnvVar_);
    Touch(ui::InputText("Keystore key alias environment variable", &android.keystoreAliasEnvVar_));
    ui::TextWrapped("Both passwords follow the naming rule of the generated project: the variable "
        "above with _PASSWORD appended. Nothing is written into the generated gradle files but the "
        "names.");
}

void BuildTab::RenderWebOptions(BuildProfile& profile)
{
    if (!ui::CollapsingHeader(ICON_FA_GLOBE " Web", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    Touch(PathField("Emsdk root", profile.webEmsdkRoot_, PathFieldKind::Directory, nullptr,
        "Your emsdk directory, where the build finds file_packager.py. Empty tries "
            "the emsdk environment variables first and then the CMake cache of the web engine "
            "build"));
}

void BuildTab::RenderTextureCompressionOptions(BuildProfile& profile)
{
    TextureCompressionSettings& tc = profile.textureCompression_;

    if (!ui::CollapsingHeader(ICON_FA_IMAGE " Texture compression", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    Touch(ui::Checkbox("Compress textures for the target platform", &tc.enabled_));
    if (ui::IsItemHovered())
        ui::SetTooltip("Off by default. When on, the build converts staged textures to a GPU compressed "
            "format with the offline PVRTexTool and ships those instead of the png/jpg sources");

    ui::Indent();
    ui::BeginDisabled(!tc.enabled_);

    // An empty format means "use the platform default", which is not obvious to edit. Resolving it
    // into the fields keeps the widgets honest; the build resolves the same defaults anyway, so a
    // profile that never touched these fields still compresses with the right format.
    if (tc.enabled_)
        tc = profile.GetEffectiveTextureCompression();

    Touch(ui::InputText("Color format (no alpha)", &tc.colorFormatNoAlpha_));
    if (ui::IsItemHovered())
        ui::SetTooltip("Desktop BC1, mobile and web ETC2_RGB");

    Touch(ui::InputText("Color format (alpha)", &tc.colorFormatAlpha_));
    if (ui::IsItemHovered())
        ui::SetTooltip("Desktop BC3, mobile and web ETC2_RGBA");

    Touch(ui::InputText("Normal map format", &tc.normalFormat_));
    if (ui::IsItemHovered())
        ui::SetTooltip("Desktop BC5 (two channel, linear). Diligent has no two channel mobile or "
            "web format, so normal maps fall back to ETC2_RGBA there");

    Touch(ui::InputText("Container", &tc.container_));
    if (ui::IsItemHovered())
        ui::SetTooltip("Desktop dds, mobile and web ktx");

    Touch(ui::InputText("Quality", &tc.quality_));
    if (ui::IsItemHovered())
        ui::SetTooltip("PVRTexTool -q token; empty uses the tool default");

    Touch(ui::Checkbox("Generate mipmaps", &tc.mipmaps_));

    ui::EndDisabled();
    ui::Unindent();
}

void BuildTab::RenderStatus(BuildProfile* profile, BuildSettings* settings, Project* project)
{
    auto* build = project->GetBuildSystem();
    ui::Separator();

    // Validating costs a handful of file system stats and an environment read, which is not
    // something to do per frame; eight frames is fast enough that a change shows up immediately.
    if (validationCountdown_ == 0)
    {
        validationCountdown_ = 8;
        settings->Validate(*profile, validationErrors_);
    }
    else
        --validationCountdown_;

    if (build->IsBuilding())
    {
        const ea::string overlay = Format("{}: {}", build->GetStageName(), build->GetProfileName());
        ui::ProgressBar(build->GetProgress(), ImVec2{ 200.0f, 0.0f }, overlay.c_str());
        ui::TextWrapped("The build runs between frames, so this window stays usable; every stage is "
            "written to the Console tab under the [Build] prefix.");
        return;
    }

    const auto& errors = build->GetErrors();
    if (!errors.empty())
    {
        ui::Text(ICON_FA_TRIANGLE_EXCLAMATION " The last build of '%s' failed:",
            build->GetProfileName().c_str());
        for (const ea::string& error : errors)
            ui::TextWrapped("- %s", error.c_str());
    }
    else if (!build->GetProfileName().empty())
    {
        ui::Text(ICON_FA_CIRCLE_CHECK " Last build of '%s' finished into %s",
            build->GetProfileName().c_str(), build->GetOutputDir().c_str());
    }

    if (!validationErrors_.empty())
    {
        ui::Text(ICON_FA_TRIANGLE_EXCLAMATION " '%s' cannot be built yet:", profile->name_.c_str());
        for (const ea::string& error : validationErrors_)
            ui::TextWrapped("- %s", error.c_str());
    }
}

void BuildTab::Touch(bool widgetChanged)
{
    if (widgetChanged)
        dirty_ = true;
}

void BuildTab::WriteIniSettings(ImGuiTextBuffer& output)
{
    BaseClassName::WriteIniSettings(output);
    WriteStringToIni(output, SettingsKeyProfile, selectedProfile_);
}

void BuildTab::ReadIniSettings(const char* line)
{
    BaseClassName::ReadIniSettings(line);
    if (const auto value = ReadStringFromIni(line, SettingsKeyProfile))
        selectedProfile_ = *value;
}

} // namespace Urho3D

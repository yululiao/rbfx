// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#pragma once

#include "../Tabs/EditorTab.h"

#include <EASTL/vector.h>

namespace Urho3D
{

class BuildPlatformData;
class BuildSettings;
class Project;

void Build_BuildTab(Context* context, Project* project);

/// Tab that edits the build platforms of a project and drives the build pipeline.
///
/// Nothing here decides what a build does: what a platform means lives in BuildSettings, and how a
/// platform becomes a package lives in BuildSystem. This file only draws, and the one piece of state
/// it owns is which platform is being looked at.
class BuildTab : public EditorTab
{
    URHO3D_OBJECT(BuildTab, EditorTab)

public:
    explicit BuildTab(Context* context);

    /// Implement EditorTab
    /// @{
    void RenderContent() override;
    void WriteIniSettings(ImGuiTextBuffer& output) override;
    void ReadIniSettings(const char* line) override;
    /// @}

private:
    /// The build platform the persisted platform selection points at, or the first one when it
    /// points at nothing.
    BuildPlatformData* ResolvePlatform(BuildSettings* settings);
    /// Switches, names and engine paths that describe the produced package.
    void RenderPackageOptions(BuildPlatformData& platform);
    /// Per-platform texture compression parameters: PVRTexTool formats, container and quality.
    void RenderTextureCompressionOptions(BuildPlatformData& platform);
    /// The part of a platform only the Android scaffold generator reads.
    void RenderAndroidOptions(BuildPlatformData& platform);
    /// The part of a platform only the web package assembler reads.
    void RenderWebOptions(BuildPlatformData& platform);
    /// Progress of a build in flight, or the reason the last one did not succeed.
    void RenderStatus(BuildPlatformData* platform, BuildSettings* settings, Project* project);
    /// Remember that a widget changed the platform, so Build.json is rewritten once this frame.
    void Touch(bool widgetChanged);

    /// Why the selected platform cannot be built, refreshed on a cadence in RenderStatus.
    ea::vector<ea::string> validationErrors_;
    /// Frames left until the selected platform is validated again.
    unsigned validationCountdown_{};
    /// Name of the selected platform's platform. A name, not a pointer: the platform vector is
    /// edited through this tab and reallocates.
    ea::string selectedPlatform_;
    /// Set by any widget that changed a value, cleared once the file has been written.
    bool dirty_{};
};

} // namespace Urho3D

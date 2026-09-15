// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#pragma once

#include "../Project/EditorTab.h"

#include <EASTL/vector.h>

namespace Urho3D
{

class BuildProfile;
class BuildSettings;
class Project;

void Foundation_BuildTab(Context* context, Project* project);

/// Tab that edits the build profiles of a project and drives the build pipeline.
///
/// Nothing here decides what a build does: what a profile means lives in BuildSettings, and how a
/// profile becomes a package lives in BuildSystem. This file only draws, and the one piece of state
/// it owns is which profile is being looked at.
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
    /// The profile the persisted name points at, or the first one when it points at nothing.
    BuildProfile* ResolveProfile(BuildSettings* settings);
    /// Switches, names and engine paths that describe the produced package.
    void RenderPackageOptions(BuildProfile& profile);
    /// Per-platform texture compression parameters: PVRTexTool formats, container and quality.
    void RenderTextureCompressionOptions(BuildProfile& profile);
    /// The part of a profile only the Android scaffold generator reads.
    void RenderAndroidOptions(BuildProfile& profile);
    /// The part of a profile only the web package assembler reads.
    void RenderWebOptions(BuildProfile& profile);
    /// Progress of a build in flight, or the reason the last one did not succeed.
    void RenderStatus(BuildProfile* profile, BuildSettings* settings, Project* project);
    /// Remember that a widget changed the profile, so Build.json is rewritten once this frame.
    void Touch(bool widgetChanged);

    /// Why the selected profile cannot be built, refreshed on a cadence in RenderStatus.
    ea::vector<ea::string> validationErrors_;
    /// Frames left until the selected profile is validated again.
    unsigned validationCountdown_{};
    /// Name, not pointer: the profile vector is edited through this tab and reallocates.
    ea::string selectedProfile_;
    /// Set by any widget that changed a value, cleared once the file has been written.
    bool dirty_{};
};

} // namespace Urho3D

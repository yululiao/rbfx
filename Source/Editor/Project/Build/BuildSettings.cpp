// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#include "../../Project/Build/BuildSettings.h"

#include <Urho3D/Core/ProcessUtils.h>
#include <Urho3D/Core/StringUtils.h>
#include <Urho3D/IO/ArchiveSerialization.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Resource/JSONArchive.h>
#include <Urho3D/Resource/JSONFile.h>

#include <EASTL/algorithm.h>

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace Urho3D
{

namespace
{

/// Names the editor seeds into a fresh project.
const ea::string DefaultWindowsName = "Windows";
const ea::string DefaultAndroidName = "Android";
const ea::string DefaultWebName = "Web";
const ea::string DefaultExecutableName = "Game";

/// Environment variable the runtime already consults for a content key override, so a profile that
/// leaves the field alone cannot produce scripts the local runtime then refuses to open.
const char* const DefaultScriptKeyEnvVar = "RBFX_LUA_SCRIPT_KEY";

/// Turn a folder name into something legal inside a Java package: lowercase, letters and digits
/// only, never starting with a digit. Project names like "Empty Test" are common enough that
/// silently writing an invalid application id would just move the error into gradle.
ea::string SanitizePackageSegment(const ea::string& text)
{
    ea::string result;
    for (const char ch : text)
    {
        const bool isAlnum = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
        if (isAlnum)
            result.push_back(static_cast<char>(tolower(ch)));
        // Everything else (space, dash, dot) simply disappears; segments already separate packages.
    }
    if (result.empty())
        result = "game";
    if (result[0] >= '0' && result[0] <= '9')
        result = "game" + result;
    return result;
}

/// A key is 32 bytes in hex, nothing else. Half a key is worse than no key because it encrypts
/// with something the runtime will not resolve to the same bytes.
bool IsHexKey(const ea::string& text)
{
    if (text.size() != 64)
        return false;
    for (const char ch : text)
    {
        const bool isHex = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
        if (!isHex)
            return false;
    }
    return true;
}

/// Fill every empty texture compression field with the platform default. Mobile and web have no
/// two-channel normal format in Diligent (no EAC_RG11), so normal maps fall back to the RGBA
/// color format there. Web reuses the mobile ETC2/KTX combination: ETC2 is a core WebGL2 format
/// (hardware native on mobile browsers, ANGLE-translated on desktop ones).
void ApplyTextureCompressionDefaults(TextureCompressionSettings& settings, bool isMobile, bool isWeb)
{
    if (isMobile || isWeb)
    {
        if (settings.colorFormatNoAlpha_.empty())
            settings.colorFormatNoAlpha_ = "ETC2_RGB";
        if (settings.colorFormatAlpha_.empty())
            settings.colorFormatAlpha_ = "ETC2_RGBA";
        if (settings.normalFormat_.empty())
            settings.normalFormat_ = "ETC2_RGBA";
        if (settings.container_.empty())
            settings.container_ = "ktx";
    }
    else
    {
        if (settings.colorFormatNoAlpha_.empty())
            settings.colorFormatNoAlpha_ = "BC1";
        if (settings.colorFormatAlpha_.empty())
            settings.colorFormatAlpha_ = "BC3";
        if (settings.normalFormat_.empty())
            settings.normalFormat_ = "BC5";
        if (settings.container_.empty())
            settings.container_ = "dds";
    }
}

} // namespace

ea::string GetExecutableSuffix()
{
#if defined(_WIN32)
    return ".exe";
#else
    return ea::string();
#endif
}

void AndroidBuildSettings::SerializeInBlock(Archive& archive)
{
    SerializeOptionalValue(archive, "ApplicationId", applicationId_, ea::string("com.example.game"));
    SerializeOptionalValue(archive, "VersionCode", versionCode_, 1);
    SerializeOptionalValue(archive, "VersionName", versionName_, ea::string("0.1.0"));
    SerializeOptionalValue(archive, "MinSdk", minSdk_, 24);
    SerializeOptionalValue(archive, "TargetSdk", targetSdk_, 35);
    SerializeOptionalValue(archive, "Orientation", orientation_, ea::string("sensorLandscape"));
    SerializeOptionalValue(archive, "Abis", abis_, ea::vector<ea::string>{ea::string("arm64-v8a")});
    SerializeOptionalValue(archive, "Icon", icon_, ea::string());
    SerializeOptionalValue(archive, "KeystoreEnvVar", keystoreEnvVar_, ea::string());
    SerializeOptionalValue(archive, "KeystoreAliasEnvVar", keystoreAliasEnvVar_, ea::string());
}

void TextureCompressionSettings::SerializeInBlock(Archive& archive)
{
    SerializeOptionalValue(archive, "Enabled", enabled_, false);
    SerializeOptionalValue(archive, "ColorFormatNoAlpha", colorFormatNoAlpha_, ea::string());
    SerializeOptionalValue(archive, "ColorFormatAlpha", colorFormatAlpha_, ea::string());
    SerializeOptionalValue(archive, "NormalFormat", normalFormat_, ea::string());
    SerializeOptionalValue(archive, "Container", container_, ea::string());
    SerializeOptionalValue(archive, "Quality", quality_, ea::string());
    SerializeOptionalValue(archive, "Mipmaps", mipmaps_, true);
}

void BuildProfile::SerializeInBlock(Archive& archive)
{
    SerializeOptionalValue(archive, "Name", name_);
    SerializeOptionalValue(archive, "Platform", platform_);
    SerializeOptionalValue(archive, "EngineBin", engineBin_, ea::string());
    SerializeOptionalValue(archive, "EngineData", engineData_, ea::string());
    SerializeOptionalValue(archive, "OutputDir", outputDir_);
    SerializeOptionalValue(archive, "ExecutableName", executableName_, DefaultExecutableName);
    SerializeOptionalValue(archive, "PackData", packData_, true);
    SerializeOptionalValue(archive, "CompressPackages", compressPackages_, true);
    SerializeOptionalValue(archive, "EncryptScripts", encryptScripts_, false);
    SerializeOptionalValue(archive, "IncludeEngineData", includeEngineData_, true);
    SerializeOptionalValue(archive, "AutoRunAfterBuild", autoRunAfterBuild_, false);
    SerializeOptionalValue(archive, "ScriptKeyEnvVar", scriptKeyEnvVar_, ea::string(DefaultScriptKeyEnvVar));
    SerializeOptionalValue(archive, "WebEmsdkRoot", webEmsdkRoot_, ea::string());
    // The block itself is always written while every leaf inside it decides for itself whether it
    // differs from its fallback. Writing the block unconditionally keeps the reader from having to
    // tell "section absent" apart from "section present and complete".
    SerializeOptionalValue(archive, "Android", android_, AlwaysSerialize{});
    SerializeOptionalValue(archive, "TextureCompression", textureCompression_, AlwaysSerialize{});
}

ea::string BuildProfile::ResolveOutputDir(const ea::string& projectPath) const
{
    ea::string result = outputDir_.empty() ? ea::string("Build/") + name_ : outputDir_;
    if (!IsAbsolutePath(result))
        result = AddTrailingSlash(projectPath) + result;
    // Backslashes survive in hand edited files and in the output of a Windows file dialog, and
    // every consumer below concatenates with forward slashes.
    result.replace("\\", "/");
    return AddTrailingSlash(RemoveTrailingSlash(result));
}

TextureCompressionSettings BuildProfile::GetEffectiveTextureCompression() const
{
    TextureCompressionSettings result = textureCompression_;
    ApplyTextureCompressionDefaults(result, IsAndroid(), IsWeb());
    return result;
}

BuildSettings::BuildSettings(Context* context)
    : Object(context)
{
}

void BuildSettings::SerializeInBlock(Archive& archive)
{
    SerializeOptionalValue(archive, "Profiles", profiles_);
}

bool BuildSettings::LoadFile(const ea::string& fileName)
{
    auto* fs = GetSubsystem<FileSystem>();

    filePath_ = fileName;
    profiles_.clear();

    // No file yet is the normal state of a brand new project, not a problem.
    if (!fs->FileExists(fileName))
        return true;

    JSONFile jsonFile(context_);
    if (!jsonFile.LoadFile(fileName))
    {
        URHO3D_LOGERROR("Failed to parse build settings '{}'", fileName);
        return false;
    }

    JSONInputArchive archive{&jsonFile};
    SerializeOptionalValue(archive, "Build", *this, AlwaysSerialize{});
    return true;
}

bool BuildSettings::SaveFile(const ea::string& fileName)
{
    JSONFile jsonFile(context_);
    JSONOutputArchive archive{&jsonFile};
    SerializeOptionalValue(archive, "Build", *this, AlwaysSerialize{});
    return jsonFile.SaveFile(fileName);
}

bool BuildSettings::LoadProject(const ea::string& projectPath, const ea::string& engineData, bool seedDefaults)
{
    const ea::string path = AddTrailingSlash(projectPath) + "Build.json";
    if (!LoadFile(path))
        return false;

    if (!seedDefaults)
        return true;

    // Seed only when the file held nothing at all. A profile somebody trimmed by hand stays trimmed,
    // while a project that never had a Build.json still gets something it can build - and since the
    // tab has no way to create a profile, an empty list would otherwise be a dead end.
    if (!profiles_.empty())
        return true;

    EnsureProfile(DefaultWindowsName, projectPath, engineData);
    EnsureProfile(DefaultAndroidName, projectPath, engineData);
    EnsureProfile(DefaultWebName, projectPath, engineData);

    if (!SaveFile(path))
    {
        URHO3D_LOGERROR("Failed to write the default build profiles to '{}'", path);
        return false;
    }
    return true;
}

const BuildProfile* BuildSettings::FindProfile(const ea::string& name) const
{
    const auto iter = ea::find_if(profiles_.begin(), profiles_.end(),
        [&name](const BuildProfile& profile) { return profile.name_ == name; });
    return iter != profiles_.end() ? &*iter : nullptr;
}

BuildProfile* BuildSettings::FindProfileMutable(const ea::string& name)
{
    const auto iter = ea::find_if(profiles_.begin(), profiles_.end(),
        [&name](const BuildProfile& profile) { return profile.name_ == name; });
    return iter != profiles_.end() ? &*iter : nullptr;
}

ea::vector<ea::string> BuildSettings::GetProfileNames() const
{
    ea::vector<ea::string> names;
    names.reserve(profiles_.size());
    for (const BuildProfile& profile : profiles_)
        names.push_back(profile.name_);
    return names;
}

bool BuildSettings::EnsureProfile(const ea::string& name, const ea::string& projectPath, const ea::string& engineData)
{
    if (FindProfile(name) != nullptr)
        return false;

    auto* fs = GetSubsystem<FileSystem>();
    BuildProfile profile;
    profile.name_ = name;
    if (name == DefaultAndroidName)
        profile.platform_ = "Android";
    else if (name == DefaultWebName)
        profile.platform_ = "Web";
    else
        profile.platform_ = "WindowsDesktop";
    profile.engineBin_ = RemoveTrailingSlash(fs->GetProgramDir());
    profile.engineData_ = engineData;
    profile.outputDir_ = "Build/" + name;
    profile.executableName_ = DefaultExecutableName;
    profile.packData_ = true;
    profile.compressPackages_ = true;
    profile.encryptScripts_ = false;
    profile.includeEngineData_ = true;
    profile.autoRunAfterBuild_ = false;
    profile.scriptKeyEnvVar_ = DefaultScriptKeyEnvVar;

    if (profile.IsAndroid())
    {
        const ea::string projectName = SanitizePackageSegment(GetFileNameAndExtension(RemoveTrailingSlash(projectPath)));
        profile.android_.applicationId_ = "com.example." + projectName;
    }

    // Seed concrete per-platform formats so a fresh Build.json documents what a build will do. The
    // master switch stays off: texture compression is opt-in.
    ApplyTextureCompressionDefaults(profile.textureCompression_, profile.IsAndroid(), profile.IsWeb());

    profiles_.push_back(profile);
    return true;
}

ea::string BuildSettings::FindTool(const BuildProfile& profile, const ea::string& toolName) const
{
    auto* fs = GetSubsystem<FileSystem>();
    const ea::string fileName = toolName + GetExecutableSuffix();

    ea::vector<ea::string> candidates;
    if (!profile.engineBin_.empty())
        candidates.push_back(RemoveTrailingSlash(profile.engineBin_) + "/" + fileName);
    candidates.push_back(RemoveTrailingSlash(fs->GetProgramDir()) + "/" + fileName);

    for (const ea::string& candidate : candidates)
    {
        if (fs->FileExists(candidate))
            return candidate;
    }
    return EMPTY_STRING;
}

bool BuildSettings::Validate(const BuildProfile& profile, ea::vector<ea::string>& errors) const
{
    auto* fs = GetSubsystem<FileSystem>();
    errors.clear();

    if (profile.name_.empty())
        errors.push_back("Profile has no name.");
    if (!profile.IsAndroid() && !profile.IsWindowsDesktop() && !profile.IsWeb())
    {
        errors.push_back(ToString("Profile '%s': unknown platform '%s', expected 'WindowsDesktop', 'Android' or 'Web'.",
            profile.name_.c_str(), profile.platform_.c_str()));
    }

    const ea::string exeSuffix = GetExecutableSuffix();

    // The engine binaries are only consumed by a desktop or web package. Android compiles the host
    // from source inside gradle, so pointing it at a Windows build directory would be meaningless.
    if (profile.IsWindowsDesktop() || profile.IsWeb())
    {
        if (profile.engineBin_.empty())
        {
            errors.push_back("Engine binary directory is not set.");
        }
        else if (!fs->DirExists(profile.engineBin_))
        {
            errors.push_back(ToString("Engine binary directory does not exist: '%s'.", profile.engineBin_.c_str()));
        }
        else if (profile.IsWindowsDesktop())
        {
            const ea::string host = RemoveTrailingSlash(profile.engineBin_) + "/LuaGamePlayer" + exeSuffix;
            const ea::string engineLib = RemoveTrailingSlash(profile.engineBin_) + "/Urho3D" + DYN_LIB_SUFFIX;
            const ea::string luaLib = RemoveTrailingSlash(profile.engineBin_) + "/RbfxLuaScript" + DYN_LIB_SUFFIX;
            const char* buildCommand = "cmake --build msvc --target LuaGamePlayer --config Debug";
            if (!fs->FileExists(host))
                errors.push_back(ToString("Missing '%s'. Build it first, the editor will not start a "
                    "multi-minute engine build on its own (%s).", host.c_str(), buildCommand));
            if (!fs->FileExists(engineLib))
                errors.push_back(ToString("Missing '%s'. Build it first, the editor will not start a "
                    "multi-minute engine build on its own (%s).", engineLib.c_str(), buildCommand));
            if (!fs->FileExists(luaLib))
                errors.push_back(ToString("Missing '%s'. Build it first, the editor will not start a "
                    "multi-minute engine build on its own (%s).", luaLib.c_str(), buildCommand));
        }
        else
        {
            // The web host is one html page plus its script and binary sidecars, produced by the
            // emscripten engine build into the engine binary directory.
            const ea::string bin = RemoveTrailingSlash(profile.engineBin_);
            const char* buildCommand = "cmake --build web --target LuaGamePlayer";
            const char* const webArtifacts[] = {"/LuaGamePlayer.html", "/LuaGamePlayer.js", "/LuaGamePlayer.wasm"};
            for (const char* artifact : webArtifacts)
            {
                if (!fs->FileExists(bin + artifact))
                    errors.push_back(ToString("Missing '%s'. Build the web host first, the editor will not "
                        "start a multi-minute engine build on its own (%s).", (bin + artifact).c_str(), buildCommand));
            }
        }
    }

    if (profile.engineData_.empty())
        errors.push_back("Engine data directory is not set.");
    else if (!fs->DirExists(profile.engineData_))
        errors.push_back(ToString("Engine data directory does not exist: '%s'.", profile.engineData_.c_str()));
    else if (profile.includeEngineData_ && !fs->DirExists(profile.engineData_ + "/Data"))
        errors.push_back(ToString("'%s' has no Data/ subdirectory, but the profile includes engine data.",
            profile.engineData_.c_str()));

    if (profile.executableName_.empty())
        errors.push_back("Executable name is empty.");
    else if (profile.executableName_.find_first_of("/\\:") != ea::string::npos)
        errors.push_back(ToString("Executable name '%s' must be a bare file name without a suffix.",
            profile.executableName_.c_str()));

    if (profile.packData_ && FindTool(profile, "PackageTool").empty())
        errors.push_back("PackageTool was not found next to the engine binaries or the editor. "
            "Build it with: cmake --build msvc --target PackageTool --config Debug");

    if (profile.textureCompression_.enabled_ && FindTool(profile, "PVRTexToolCLI").empty())
        errors.push_back("Texture compression is enabled but PVRTexToolCLI was not found next to the "
            "engine binaries or the editor.");

    if (profile.encryptScripts_)
    {
        if (FindTool(profile, "LuaCompiler").empty())
        {
            errors.push_back("LuaCompiler was not found next to the engine binaries or the editor. "
                "Build it with: cmake --build msvc --target LuaCompiler --config Debug");
        }

        // The variable always has to be named, otherwise the compiler and the runtime can be
        // pointed at two different ones without anybody noticing. Its value is only checked when
        // it is actually set: an unset one means "use the key the runtime library was compiled
        // with", which is a legitimate configuration for a team that configures both ends.
        if (profile.scriptKeyEnvVar_.empty())
        {
            errors.push_back("EncryptScripts is on but no script key environment variable is named.");
        }
        else if (const char* key = getenv(profile.scriptKeyEnvVar_.c_str()); key && *key && !IsHexKey(key))
        {
            errors.push_back(ToString("Script key environment variable '%s' must hold 64 hex characters, "
                "it holds %d.", profile.scriptKeyEnvVar_.c_str(), static_cast<int>(strlen(key))));
        }
    }

    if (profile.IsAndroid())
    {
        if (profile.android_.applicationId_.empty() ||
            profile.android_.applicationId_.find('.') == ea::string::npos)
        {
            errors.push_back("Android application id must be a dotted package name, for example "
                "'com.example.game'.");
        }
        if (profile.android_.abis_.empty())
            errors.push_back("Android profile lists no ABIs; at least one of arm64-v8a, armeabi-v7a, x86_64.");
        for (const ea::string& abi : profile.android_.abis_)
        {
            if (abi != "arm64-v8a" && abi != "armeabi-v7a" && abi != "x86" && abi != "x86_64")
                errors.push_back(ToString("Unrecognized Android ABI '%s'.", abi.c_str()));
        }
        if (profile.android_.minSdk_ > profile.android_.targetSdk_)
            errors.push_back("Android minSdk is greater than targetSdk.");
        // gradle rejects a version code below one, and an apk reinstall on a device compares it as
        // an unsigned integer, so zero is never a meaningful value here.
        if (profile.android_.versionCode_ < 1)
            errors.push_back("Android versionCode must be at least 1.");
    }

    return errors.empty();
}

} // namespace Urho3D

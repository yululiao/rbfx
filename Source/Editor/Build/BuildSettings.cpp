// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#include "BuildSettings.h"

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

/// Environment variable the runtime already consults for a content key override, so a platform that
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

/// Names the EngineBuildMode values wear in Build.json. One word each, so a hand-edited file
/// reads like the combo box that writes it.
const char* const EngineBuildModeNames[] = {"Never", "Incremental", "Rebuild"};

bool EngineBuildModeFromString(const ea::string& text, EngineBuildMode& mode)
{
    for (unsigned i = 0; i < sizeof(EngineBuildModeNames) / sizeof(EngineBuildModeNames[0]); ++i)
    {
        if (text.comparei(EngineBuildModeNames[i]) == 0)
        {
            mode = static_cast<EngineBuildMode>(i);
            return true;
        }
    }
    if (!text.empty())
        URHO3D_LOGWARNING("Unknown EngineBuild mode '{}' in Build.json, falling back to 'Never'", text);
    mode = EngineBuildMode::Never;
    return false;
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

void BuildPlatformData::SerializeInBlock(Archive& archive)
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
    // The mode round-trips through its name: the file stays readable and an unknown word degrades
    // to Never with a warning instead of failing the load over one bad token.
    ea::string engineBuild = EngineBuildModeNames[static_cast<unsigned>(engineBuild_)];
    SerializeOptionalValue(archive, "EngineBuild", engineBuild, ea::string(EngineBuildModeNames[0]));
    EngineBuildModeFromString(engineBuild, engineBuild_);
    // The block itself is always written while every leaf inside it decides for itself whether it
    // differs from its fallback. Writing the block unconditionally keeps the reader from having to
    // tell "section absent" apart from "section present and complete".
    SerializeOptionalValue(archive, "Android", android_, AlwaysSerialize{});
    SerializeOptionalValue(archive, "TextureCompression", textureCompression_, AlwaysSerialize{});
}

ea::string BuildPlatformData::ResolveOutputDir(const ea::string& projectPath) const
{
    ea::string result = outputDir_.empty() ? ea::string("Build/") + name_ : outputDir_;
    if (!IsAbsolutePath(result))
        result = AddTrailingSlash(projectPath) + result;
    // Backslashes survive in hand edited files and in the output of a Windows file dialog, and
    // every consumer below concatenates with forward slashes.
    result.replace("\\", "/");
    return AddTrailingSlash(RemoveTrailingSlash(result));
}

TextureCompressionSettings BuildPlatformData::GetEffectiveTextureCompression() const
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
    SerializeOptionalValue(archive, "Platforms", platforms_);
}

bool BuildSettings::LoadFile(const ea::string& fileName)
{
    auto* fs = GetSubsystem<FileSystem>();

    filePath_ = fileName;
    platforms_.clear();

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

    // Seed only when the file held nothing at all. A platform somebody trimmed by hand stays trimmed,
    // while a project that never had a Build.json still gets something it can build - and since the
    // tab has no way to create a platform, an empty list would otherwise be a dead end.
    if (!platforms_.empty())
        return true;

    EnsurePlatform(DefaultWindowsName, projectPath, engineData);
    EnsurePlatform(DefaultAndroidName, projectPath, engineData);
    EnsurePlatform(DefaultWebName, projectPath, engineData);

    if (!SaveFile(path))
    {
        URHO3D_LOGERROR("Failed to write the default build platforms to '{}'", path);
        return false;
    }
    return true;
}

const BuildPlatformData* BuildSettings::FindPlatform(const ea::string& name) const
{
    const auto iter = ea::find_if(platforms_.begin(), platforms_.end(),
        [&name](const BuildPlatformData& platform) { return platform.name_ == name; });
    return iter != platforms_.end() ? &*iter : nullptr;
}

BuildPlatformData* BuildSettings::FindPlatformMutable(const ea::string& name)
{
    const auto iter = ea::find_if(platforms_.begin(), platforms_.end(),
        [&name](const BuildPlatformData& platform) { return platform.name_ == name; });
    return iter != platforms_.end() ? &*iter : nullptr;
}

ea::vector<ea::string> BuildSettings::GetPlatformNames() const
{
    ea::vector<ea::string> names;
    names.reserve(platforms_.size());
    for (const BuildPlatformData& platform : platforms_)
        names.push_back(platform.name_);
    return names;
}

bool BuildSettings::EnsurePlatform(const ea::string& name, const ea::string& projectPath, const ea::string& engineData)
{
    if (FindPlatform(name) != nullptr)
        return false;

    auto* fs = GetSubsystem<FileSystem>();
    BuildPlatformData platform;
    platform.name_ = name;
    if (name == DefaultAndroidName)
        platform.platform_ = "Android";
    else if (name == DefaultWebName)
        platform.platform_ = "Web";
    else
        platform.platform_ = "WindowsDesktop";
    platform.engineBin_ = RemoveTrailingSlash(fs->GetProgramDir());
    platform.engineData_ = engineData;
    platform.outputDir_ = "Build/" + name;
    platform.executableName_ = DefaultExecutableName;
    platform.packData_ = true;
    platform.compressPackages_ = true;
    platform.encryptScripts_ = false;
    platform.includeEngineData_ = true;
    platform.autoRunAfterBuild_ = false;
    platform.scriptKeyEnvVar_ = DefaultScriptKeyEnvVar;

    if (platform.IsAndroid())
    {
        const ea::string projectName = SanitizePackageSegment(GetFileNameAndExtension(RemoveTrailingSlash(projectPath)));
        platform.android_.applicationId_ = "com.example." + projectName;
    }

    // Seed concrete per-platform formats so a fresh Build.json documents what a build will do. The
    // master switch stays off: texture compression is opt-in.
    ApplyTextureCompressionDefaults(platform.textureCompression_, platform.IsAndroid(), platform.IsWeb());

    platforms_.push_back(platform);
    return true;
}

ea::string BuildSettings::FindTool(const BuildPlatformData& platform, const ea::string& toolName) const
{
    auto* fs = GetSubsystem<FileSystem>();
    const ea::string fileName = toolName + GetExecutableSuffix();

    ea::vector<ea::string> candidates;
    if (!platform.engineBin_.empty())
        candidates.push_back(RemoveTrailingSlash(platform.engineBin_) + "/" + fileName);
    candidates.push_back(RemoveTrailingSlash(fs->GetProgramDir()) + "/" + fileName);

    for (const ea::string& candidate : candidates)
    {
        if (fs->FileExists(candidate))
            return candidate;
    }
    return EMPTY_STRING;
}

bool BuildSettings::Validate(const BuildPlatformData& platform, ea::vector<ea::string>& errors) const
{
    auto* fs = GetSubsystem<FileSystem>();
    errors.clear();

    if (platform.name_.empty())
        errors.push_back("Platform has no name.");
    if (!platform.IsAndroid() && !platform.IsWindowsDesktop() && !platform.IsWeb())
    {
        errors.push_back(ToString("Platform '%s': unknown platform '%s', expected 'WindowsDesktop', 'Android' or 'Web'.",
            platform.name_.c_str(), platform.platform_.c_str()));
    }

    const ea::string exeSuffix = GetExecutableSuffix();

    // The engine binaries are only consumed by a desktop or web package. Android compiles the host
    // from source inside gradle, so pointing it at a Windows build directory would be meaningless.
    if (platform.IsWindowsDesktop() || platform.IsWeb())
    {
        if (platform.engineBin_.empty())
        {
            errors.push_back("Engine binary directory is not set.");
        }
        else if (!fs->DirExists(platform.engineBin_))
        {
            errors.push_back(ToString("Engine binary directory does not exist: '%s'.", platform.engineBin_.c_str()));
        }
        else if (platform.IsWindowsDesktop())
        {
            const ea::string host = RemoveTrailingSlash(platform.engineBin_) + "/LuaGamePlayer" + exeSuffix;
            const ea::string engineLib = RemoveTrailingSlash(platform.engineBin_) + "/Urho3D" + DYN_LIB_SUFFIX;
            const char* buildCommand = "cmake --build msvc --target LuaGamePlayer --config Debug";
            if (!fs->FileExists(host))
                errors.push_back(ToString("Missing '%s'. Build it first (%s), or set 'Compile engine "
                    "host' on this platform and the build does it itself.", host.c_str(), buildCommand));
            if (!fs->FileExists(engineLib))
                errors.push_back(ToString("Missing '%s'. Build it first (%s), or set 'Compile engine "
                    "host' on this platform and the build does it itself.", engineLib.c_str(), buildCommand));
        }
        else
        {
            // The web host is one html page plus its script and binary sidecars, produced by the
            // emscripten engine build into the engine binary directory.
            const ea::string bin = RemoveTrailingSlash(platform.engineBin_);
            const char* buildCommand = "cmake --build web --target LuaGamePlayer";
            const char* const webArtifacts[] = {"/LuaGamePlayer.html", "/LuaGamePlayer.js", "/LuaGamePlayer.wasm"};
            for (const char* artifact : webArtifacts)
            {
                if (!fs->FileExists(bin + artifact))
                    errors.push_back(ToString("Missing '%s'. Build the web host first (%s), or set "
                        "'Compile engine host' on this platform and the build does it itself.",
                        (bin + artifact).c_str(), buildCommand));
            }
        }
    }

    if (platform.engineData_.empty())
        errors.push_back("Engine data directory is not set.");
    else if (!fs->DirExists(platform.engineData_))
        errors.push_back(ToString("Engine data directory does not exist: '%s'.", platform.engineData_.c_str()));
    else if (platform.includeEngineData_ && !fs->DirExists(platform.engineData_ + "/Data"))
        errors.push_back(ToString("'%s' has no Data/ subdirectory, but the platform includes engine data.",
            platform.engineData_.c_str()));

    if (platform.executableName_.empty())
        errors.push_back("Executable name is empty.");
    else if (platform.executableName_.find_first_of("/\\:") != ea::string::npos)
        errors.push_back(ToString("Executable name '%s' must be a bare file name without a suffix.",
            platform.executableName_.c_str()));

    if (platform.packData_ && FindTool(platform, "PackageTool").empty())
        errors.push_back("PackageTool was not found next to the engine binaries or the editor. "
            "Build it with: cmake --build msvc --target PackageTool --config Debug");

    if (platform.textureCompression_.enabled_ && FindTool(platform, "PVRTexToolCLI").empty())
        errors.push_back("Texture compression is enabled but PVRTexToolCLI was not found next to the "
            "engine binaries or the editor.");

    if (platform.encryptScripts_)
    {
        if (FindTool(platform, "LuaCompiler").empty())
        {
            errors.push_back("LuaCompiler was not found next to the engine binaries or the editor. "
                "Build it with: cmake --build msvc --target LuaCompiler --config Debug");
        }

        // The variable always has to be named, otherwise the compiler and the runtime can be
        // pointed at two different ones without anybody noticing. Its value is only checked when
        // it is actually set: an unset one means "use the key the runtime library was compiled
        // with", which is a legitimate configuration for a team that configures both ends.
        if (platform.scriptKeyEnvVar_.empty())
        {
            errors.push_back("EncryptScripts is on but no script key environment variable is named.");
        }
        else if (const char* key = getenv(platform.scriptKeyEnvVar_.c_str()); key && *key && !IsHexKey(key))
        {
            errors.push_back(ToString("Script key environment variable '%s' must hold 64 hex characters, "
                "it holds %d.", platform.scriptKeyEnvVar_.c_str(), static_cast<int>(strlen(key))));
        }
    }

    if (platform.IsAndroid())
    {
        if (platform.android_.applicationId_.empty() ||
            platform.android_.applicationId_.find('.') == ea::string::npos)
        {
            errors.push_back("Android application id must be a dotted package name, for example "
                "'com.example.game'.");
        }
        if (platform.android_.abis_.empty())
            errors.push_back("Android platform lists no ABIs; at least one of arm64-v8a, armeabi-v7a, x86_64.");
        for (const ea::string& abi : platform.android_.abis_)
        {
            if (abi != "arm64-v8a" && abi != "armeabi-v7a" && abi != "x86" && abi != "x86_64")
                errors.push_back(ToString("Unrecognized Android ABI '%s'.", abi.c_str()));
        }
        if (platform.android_.minSdk_ > platform.android_.targetSdk_)
            errors.push_back("Android minSdk is greater than targetSdk.");
        // gradle rejects a version code below one, and an apk reinstall on a device compares it as
        // an unsigned integer, so zero is never a meaningful value here.
        if (platform.android_.versionCode_ < 1)
            errors.push_back("Android versionCode must be at least 1.");
    }

    return errors.empty();
}

} // namespace Urho3D

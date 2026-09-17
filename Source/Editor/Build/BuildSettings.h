// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#pragma once

#include <Urho3D/Core/Object.h>

#include <EASTL/string.h>
#include <EASTL/vector.h>

namespace Urho3D
{

class Archive;

/// Whether and how a build compiles the C++ engine host itself before packaging it. Desktop and
/// web read it; Android does not, its gradle project compiles the engine from source regardless.
enum class EngineBuildMode
{
    /// Use the binaries that are already in the engine binary directory.
    Never,
    /// Compile the host into the existing build tree, reusing whatever is still up to date.
    Incremental,
    /// Compile the host after cleaning its previous products from the build tree.
    Rebuild,
};

/// Suffix every operating system puts on runnable binaries. The build steps need it to locate the
/// offline tools (PackageTool, LuaCompiler) next to the engine binaries a platform points at.
ea::string GetExecutableSuffix();

/// Android specific part of a build platform. Ignored by desktop platforms, and the only part the
/// Android scaffold generator reads.
///
/// The initializers below and the serialization fallbacks in the .cpp are the same numbers on
/// purpose: a Build.json that predates one of these fields, or that a user trimmed by hand, still
/// has to describe a buildable app.
struct AndroidBuildSettings
{
    ea::string applicationId_ = "com.example.game";
    int versionCode_ = 1;
    ea::string versionName_ = "0.1.0";
    int minSdk_ = 24;
    int targetSdk_ = 35;
    /// AndroidManifest screenOrientation, verbatim ("sensorLandscape", "portrait", ...).
    ea::string orientation_ = "sensorLandscape";
    /// ABIs to compile for. Each one becomes an abiFilters entry and an androgen arm-arch argument.
    ea::vector<ea::string> abis_ = { "arm64-v8a" };
    /// Optional path to a launcher icon; empty keeps the template icon.
    ea::string icon_;
    /// Name of the environment variable holding the keystore path. Never the value itself: a
    /// password or a keystore location must not end up committed inside the project folder.
    ea::string keystoreEnvVar_;
    /// Name of the environment variable holding the key alias inside that keystore.
    ea::string keystoreAliasEnvVar_;

    void SerializeInBlock(Archive& archive);
};

/// Per-platform texture compression parameters, applied at build time by the offline PVRTexTool.
///
/// Every string field treats "" as "use the platform default", resolved by
/// BuildPlatformData::GetEffectiveTextureCompression. That keeps a hand-trimmed Build.json buildable and
/// lets a platform written for one platform never accidentally carry the other platform's formats.
struct TextureCompressionSettings
{
    /// Master switch. Off by default: enabling compression is opt-in so existing projects are unaffected.
    bool enabled_{};
    /// Format for color textures without an alpha channel ("BC1" desktop, "ETC2_RGB" mobile).
    ea::string colorFormatNoAlpha_;
    /// Format for color textures with an alpha channel ("BC3" desktop, "ETC2_RGBA" mobile).
    ea::string colorFormatAlpha_;
    /// Format for normal maps ("BC5" desktop, "ETC2_RGBA" mobile - Diligent has no EAC_RG11).
    ea::string normalFormat_;
    /// Container and extension of the cooked files ("dds" desktop, "ktx" mobile).
    ea::string container_;
    /// PVRTexTool quality token (-q). Empty means the tool's own default.
    ea::string quality_;
    /// Generate mipmaps for cooked textures.
    bool mipmaps_{true};

    void SerializeInBlock(Archive& archive);
};

/// One named build configuration. Persisted in <project>/Build.json, which this struct family
/// owns exclusively - Project.json stays untouched so adding build settings cannot break the
/// plugin/launch schema that already lives there.
///
/// Every field is serialized with an explicit fallback (see the .cpp), which is what lets a
/// hand-edited Build.json omit anything it is happy with while every platform still round-trips
/// to itself instead of inheriting the defaults of whichever platform was written last.
struct BuildPlatformData
{
    /// Display name, unique inside the file. Doubles as the argument to `--build`. Deliberately
    /// without a fallback: a platform nobody can name is an error, not something to guess at.
    ea::string name_;
    /// "WindowsDesktop", "Android" or "Web". Kept as a string on purpose: an unknown value has to be
    /// reported as an error rather than quietly resolved to one of the known platforms, and a
    /// missing one is reported the same way instead of defaulting an Android platform to desktop.
    ea::string platform_;
    /// Absolute directory holding the already-built host binary and shared libraries.
    ea::string engineBin_;
    /// Absolute directory holding CoreData/ and Data/ of the engine working tree.
    ea::string engineData_;
    /// Output directory, absolute or relative to the project. Empty resolves to Build/<Name>,
    /// which is why there is no per-field fallback for it - the default depends on the platform.
    ea::string outputDir_;
    /// Host binary name without suffix.
    ea::string executableName_;
    /// Fold Data/ and CoreData/ into .pak files instead of shipping loose directories.
    bool packData_{};
    /// Ask PackageTool for LZ4 compression. Only meaningful with packData_.
    bool compressPackages_{};
    /// Run the project's Lua sources through LuaCompiler into encrypted .luc containers.
    bool encryptScripts_{};
    /// Copy the engine's own Data/ into the package underneath the project files.
    bool includeEngineData_{};
    /// Launch the produced executable once the build finished. A convenience for iteration.
    bool autoRunAfterBuild_{};
    /// Name of the environment variable holding the 64-hex content key handed to LuaCompiler.
    /// Defaults to RBFX_LUA_SCRIPT_KEY, which is also the variable the runtime reads, so the two
    /// ends cannot disagree without somebody going out of their way to make it happen.
    ea::string scriptKeyEnvVar_;
    /// Root of the Emscripten SDK the web host was built with (".../emsdk"). Optional: when
    /// empty the build resolves it from the EMSCRIPTEN environment variable or from the
    /// CMakeCache.txt next to the engine binaries. Only the Web platform reads it.
    ea::string webEmsdkRoot_;
    /// Whether the build compiles the C++ engine host itself first. Never by default: a compile
    /// takes minutes, so a platform opts in when it wants a one click turnaround of engine changes.
    /// The stage runs before anything else because it produces the artifacts Validate checks.
    EngineBuildMode engineBuild_{};
    AndroidBuildSettings android_;
    TextureCompressionSettings textureCompression_;

    void SerializeInBlock(Archive& archive);

    bool IsAndroid() const { return platform_ == "Android"; }
    bool IsWindowsDesktop() const { return platform_ == "WindowsDesktop"; }
    bool IsWeb() const { return platform_ == "Web"; }

    /// Texture compression settings with every empty field resolved to this platform's platform default.
    TextureCompressionSettings GetEffectiveTextureCompression() const;

    /// Output directory made absolute against the project and normalized to end with a slash.
    ea::string ResolveOutputDir(const ea::string& projectPath) const;
};

using BuildPlatformDataVector = ea::vector<BuildPlatformData>;

/// Holds the build platforms of a project and reads/writes the file they live in.
class BuildSettings : public Object
{
    URHO3D_OBJECT(BuildSettings, Object);

public:
    explicit BuildSettings(Context* context);

    void SerializeInBlock(Archive& archive) override;

    /// Load the platforms from a JSON file. Missing files are not an error, the editor creates the
    /// defaults on first use; a file that exists but does not parse is.
    bool LoadFile(const ea::string& fileName);
    bool SaveFile(const ea::string& fileName);
    const ea::string& GetFilePath() const { return filePath_; }

    /// Open the Build.json of a project. A file that does not exist yet gets the default platforms
    /// written into it, because a project without a platform has nothing to build. seedDefaults is
    /// off for a project opened read only, which must not gain files just by being looked at.
    /// projectPath resolves the relative default output directories, engineData is the directory
    /// that holds CoreData/ and Data/ of the engine working tree - only the caller can know it.
    bool LoadProject(const ea::string& projectPath, const ea::string& engineData, bool seedDefaults);

    const BuildPlatformDataVector& GetPlatforms() const { return platforms_; }
    BuildPlatformDataVector& GetMutablePlatforms() { return platforms_; }
    const BuildPlatformData* FindPlatform(const ea::string& name) const;
    BuildPlatformData* FindPlatformMutable(const ea::string& name);
    ea::vector<ea::string> GetPlatformNames() const;

    /// Seed a platform when no platform of that name exists yet, so a fresh project has something to
    /// pick. engineData is the directory that holds CoreData/ and Data/, which only the caller can
    /// know; the engine binary directory is wherever the editor itself was launched from. Returns
    /// whether a platform was added.
    bool EnsurePlatform(const ea::string& name, const ea::string& projectPath, const ea::string& engineData);

    /// Every reason the platform cannot be built right now, one string each. Collects all of them:
    /// a user who is missing three things should not have to run the check three times.
    bool Validate(const BuildPlatformData& platform, ea::vector<ea::string>& errors) const;

    /// Locate an offline build tool by name, looking in the platform's engine binary directory
    /// first and next to the editor second. Returns an empty string when it is nowhere to be found.
    ea::string FindTool(const BuildPlatformData& platform, const ea::string& toolName) const;

private:
    BuildPlatformDataVector platforms_;
    ea::string filePath_;
};

} // namespace Urho3D

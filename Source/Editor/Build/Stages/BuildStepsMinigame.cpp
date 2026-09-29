// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

// The terminal packaging step of the Douyin platform: the vendor package assembled around the
// minigame build tree. The engine module and the game data each ship as a subpackage - the vendor
// caps the subpackages, so the module goes brotli-compressed for the loader that decompresses it -
// and every manifest (the vendor's game.json and project.config.json, and the engine file layer's
// rbfx_files.json) is generated to describe exactly the subpackages this build produced, so a
// single-variant build and a two-variant build both yield a working package.

#include "../BuildPlatform.h"
#include "../BuildSettings.h"
#include "BuildSteps.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/Core/StringUtils.h>
#include <Urho3D/IO/File.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/Log.h>

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <EASTL/unordered_map.h>

namespace Urho3D
{

namespace
{

/// Vendor budgets for a minigame package: hard upload limits, not guidelines. A package over either
/// number is rejected by the upload tool, which is easier to face in a build log than in an upload
/// dialog.
constexpr unsigned long long MainPackageBudget = 4ull * 1024 * 1024;
constexpr unsigned long long TotalPackageBudget = 20ull * 1024 * 1024;

/// Brotli quality for the compressed engine module. Measured on the 34 MB module: quality 9 lands at
/// a sixth of the size in about a second and a half, quality 11 buys two points more for thirty
/// times the time - the bytes it saves do not pay for the wait, so the step stays at 9.
constexpr int WasmBrotliQuality = 9;

/// Write a generated text file, reporting the path on any failure.
bool WriteTextFile(Context* context, const ea::string& path, const ea::string& text, ea::string& message)
{
    File file(context, path, FILE_WRITE);
    if (!file.IsOpen())
    {
        message = Format("Could not write '{}'.", path);
        return false;
    }
    if (file.Write(text.data(), text.size()) != text.size())
    {
        message = Format("Could not write '{}'.", path);
        return false;
    }
    return true;
}

/// Subpackage directory names the build tree may carry, in the order the config prefers them: the
/// standard variant first, the legacy-compatible one as the fallback for devices that need it.
ea::vector<ea::string> FindWasmSubpackages(FileSystem* fs, const ea::string& bin)
{
    ea::vector<ea::string> names;
    for (const ea::string& name : {MinigameWasmSubpackageName, MinigameWasmSubpackageName + "_compatible"})
    {
        if (fs->DirExists(bin + name))
            names.push_back(name);
    }
    return names;
}

/// Extensions the vendor tool collects into its simulator file system. Measured in the tool's
/// own code, not in any document: its pipeline globs exactly this list (getWhiteList() feeding
/// ALLOWED_MICROGAME_EXTS_REG in the installed tool's dist/pages/main-helper/index.js), and a
/// file whose extension is missing here is never staged - present on disk yet unreadable at
/// run time, which surfaces as readFileSync failures for files that are right there. The list
/// spans the media types and the engine ecosystems the tool supports (Cocos .ani/.scene/.atlas,
/// LayaAir .lmat/.ls/.lh, Egret .dbbin, ...); engine-private extensions (.lua, .mdl,
/// .renderpath, ...) are not in it. Version-sensitive: re-read it from the tool whenever the
/// filter seems to change. Extensions match the file-name tail, so a file carrying the rename
/// suffix below stays collectable.
constexpr const char* const VendorSafeExtensions[] = {
    ".json", ".js", ".png", ".jpg", ".jpeg", ".gif", ".svg", ".cer", ".mp3", ".aac", ".m4a",
    ".mp4", ".wav", ".flac", ".ape", ".ogg", ".wma", ".midi", ".ogv", ".webm", ".mkv", ".ttc",
    ".ttf", ".woff", ".otf", ".obj", ".dae", ".fbx", ".mtl", ".stl", ".3ds", ".pvr", ".plist",
    ".fnt", ".gz", ".ccz", ".bmp", ".atlas", ".swf", ".ani", ".part", ".proto", ".bin", ".sk",
    ".mipmaps", ".txt", ".zip", ".tt", ".map", ".silk", ".dbbin", ".dbmv", ".etc", ".lmat",
    ".lm", ".ls", ".lh", ".lani", ".lav", ".lsani", ".ltc", ".xml", ".pkm", ".scene", ".csv",
    ".prefab", ".mesh", ".wasm", ".br", ".heic", ".astc", ".ico", ".cur", ".dat", ".dds", ".glb",
    ".gltf", ".ktx", ".lmani", ".lml", ".skel"};
/// Appended to every file whose extension is outside the list above: itself a listed extension,
/// so the renamed file keeps passing the vendor filter, and the file manifest maps it back onto
/// the engine-visible name.
constexpr const char* VendorRenameSuffix[] = {".dat"};

bool HasVendorSafeExtension(const ea::string& fileName)
{
    const ea::string extension = GetExtension(fileName);
    for (const char* const extensionSafe : VendorSafeExtensions)
    {
        if (extension == extensionSafe)
            return true;
    }
    return false;
}

} // namespace

bool DouyinRuntimeStep::Run(ea::string& message)
{
    auto* fs = owner_.context()->GetSubsystem<FileSystem>();
    const BuildPlatformData* platform = owner_.platform();
    const ea::string bin = NormalizeDir(platform->engineBin_);

    // The build staged the bootstrap beside the module; without it there is no entry document and
    // the rest of the assembly would produce a package that cannot start.
    if (!fs->DirExists(bin + "minigame"))
    {
        message = Format("'{}' has no 'minigame' directory. Build the MinigamePlayer target in the "
            "minigame build tree first: cmake --build <tree> --target MinigamePlayer.", bin);
        return false;
    }

    // Whatever the tree produced: the standard variant, the legacy-compatible one, or both side by
    // side (one per build tree). A single variant is a valid package - the regenerated config points
    // both of its engine entries at it.
    const ea::vector<ea::string> wasmSubpackages = FindWasmSubpackages(fs, bin);
    if (wasmSubpackages.empty())
    {
        message = Format("'{}' has no wasm subpackage. Build the MinigamePlayer target in the "
            "minigame build tree first: cmake --build <tree> --target MinigamePlayer.", bin);
        return false;
    }

    if (!StageBootstrap(bin, message))
        return false;
    for (const ea::string& name : wasmSubpackages)
    {
        if (!StageWasmSubpackage(bin, name, message))
            return false;
    }
    if (!WriteGameConfig(wasmSubpackages, message))
        return false;
    // Before the manifest scan: the renames are what the scan has to see, and the manifest is
    // what maps the engine-visible names back onto the renamed files.
    ea::unordered_map<ea::string, ea::string> vendorRenames;
    if (!RenameForVendorCompatibility(vendorRenames, message))
        return false;
    if (!WriteFileManifest(vendorRenames, message))
        return false;
    // After the manifest scan: the placeholder is not a game file and must not be listed.
    if (!EnsureDataSubpackageEntry(message))
        return false;
    if (!WriteGameManifest(wasmSubpackages, message))
        return false;
    if (!WriteProjectConfig(message))
        return false;

    ReportPackageSizes();
    URHO3D_LOGINFO("[Build] Douyin package is ready in '{}'; open the directory with the Douyin "
        "developer tool", owner_.outputDir());
    return true;
}

bool DouyinRuntimeStep::StageBootstrap(const ea::string& bin, ea::string& message)
{
    auto* fs = owner_.context()->GetSubsystem<FileSystem>();
    const ea::string& outputDir = owner_.outputDir();

    // The tree stages the bootstrap (game.js and the layers it requires) in one directory; the
    // vendor runtime runs the package root's game.js first and the requires between them are
    // relative, so the whole set moves together. rbfx_game_config.js comes along as the template and
    // is regenerated right after.
    if (!fs->CopyDir(bin + "minigame", outputDir))
    {
        message = Format("Could not copy the bootstrap scripts from '{}'.", bin + "minigame");
        return false;
    }
    if (!fs->FileExists(outputDir + "game.js"))
    {
        message = Format("'{}' contains no game.js; the package would have no entry point.",
            bin + "minigame");
        return false;
    }
    return true;
}

bool DouyinRuntimeStep::StageWasmSubpackage(const ea::string& bin, const ea::string& name,
    ea::string& message)
{
    auto* fs = owner_.context()->GetSubsystem<FileSystem>();
    const ea::string destination = owner_.outputDir() + name + "/";

    // What the build staged into the subpackage directory is the entry script; it requires the
    // module beside itself, and the build keeps the two at the binary directory root, so the entry
    // is copied in here (the same mapping `cmake --install` performs) and the module follows
    // compressed.
    if (!fs->CopyDir(bin + name, destination))
    {
        message = Format("Could not copy the wasm subpackage from '{}'.", bin + name);
        return false;
    }
    if (!fs->FileExists(destination + "game.js"))
    {
        message = Format("'{}' has no game.js entry script.", bin + name + "/");
        return false;
    }

    const ea::string source = bin + MinigameHostName + ".js";
    const ea::string artifact = destination + MinigameHostName + ".js";
    if (!fs->FileExists(source))
    {
        message = Format("'{}' is not there to be copied.", source);
        return false;
    }
    if (!fs->Copy(source, artifact))
    {
        message = Format("Could not copy '{}' to '{}'.", source, artifact);
        return false;
    }

    return CompressWasmModule(bin, destination, message);
}

bool DouyinRuntimeStep::CompressWasmModule(const ea::string& bin, const ea::string& destination,
    ea::string& message)
{
    auto* fs = owner_.context()->GetSubsystem<FileSystem>();
    const ea::string source = bin + MinigameHostName + ".wasm";
    if (!fs->FileExists(source))
    {
        message = Format("'{}' is not there to be compressed.", source);
        return false;
    }

    // The vendor loader decompresses a brotli module itself - a package file named *.wasm.br loads
    // from base library 3.7.0.0 on - so the package carries only the compressed form. Shipping the
    // uncompressed module beside it would pay the subpackage budget twice for the same bytes, and
    // inflating in JavaScript was rejected: the platform already decompresses the module, so a
    // script-side inflater would add code and a wait to every start for nothing. The compression
    // itself rides the node inside the emsdk that built the module - its zlib is a brotli encoder -
    // so the pipeline adds no compression dependency of its own; the script is a throwaway written
    // beside the package and removed once it has run.
    ea::string script;
    script += "// Compress the engine module the way the vendor loader reads it. The build step writes\n";
    script += "// this file, runs it once and removes it again; argv: source, target, quality.\n";
    script += "\"use strict\";\n";
    script += "const fs = require(\"fs\");\n";
    script += "const zlib = require(\"zlib\");\n";
    script += "const input = fs.readFileSync(process.argv[2]);\n";
    script += "const output = zlib.brotliCompressSync(input, {params: {\n";
    script += "    [zlib.constants.BROTLI_PARAM_QUALITY]: Number(process.argv[4]),\n";
    script += "    [zlib.constants.BROTLI_PARAM_SIZE_HINT]: input.length}});\n";
    script += "fs.writeFileSync(process.argv[3], output);\n";

    const ea::string scriptPath = owner_.outputDir() + "rbfx_compress_wasm.js";
    if (!WriteTextFile(owner_.context(), scriptPath, script, message))
        return false;

    const ea::string artifact = destination + MinigameHostName + ".wasm.br";
    ea::vector<ea::string> arguments{scriptPath, source, artifact, Format("{}", WasmBrotliQuality)};
    ea::string output;
    const int exitCode = fs->SystemRun(owner_.ResolveEmsdkNode(), arguments, output);
    fs->Delete(scriptPath);

    if (exitCode != 0)
    {
        message = Format("Compressing '{}' failed (exit code {}): {}", source, exitCode, output);
        return false;
    }
    if (!fs->FileExists(artifact))
    {
        message = Format("Compressing '{}' produced no '{}'.", source, artifact);
        return false;
    }

    File wasmHandle(owner_.context(), source, FILE_READ);
    File moduleHandle(owner_.context(), artifact, FILE_READ);
    URHO3D_LOGINFO("[Build] Compressed the engine module for the vendor loader: {} KB -> {} KB; "
        "the package needs Douyin base library 3.7.0.0 or newer",
        (wasmHandle.GetSize() + 1023) / 1024, (moduleHandle.GetSize() + 1023) / 1024);
    return true;
}

bool DouyinRuntimeStep::WriteGameConfig(const ea::vector<ea::string>& wasmSubpackages,
    ea::string& message)
{
    auto* fs = owner_.context()->GetSubsystem<FileSystem>();
    const ea::string& resourceDir = owner_.resourceDir();

    // The template the build staged names both variants, which is only true for a package assembled
    // from two trees; regenerate it around what this build actually produced. The first segment of a
    // module path is the subpackage the loading manager downloads, so the config and the payload
    // cannot drift apart. The path ends in .wasm.br: the staging compressed the module and the
    // vendor loader decompresses it in place (base library 3.7.0.0 and newer).
    const auto wasmPath = [](const ea::string& name) { return name + "/" + MinigameHostName + ".wasm.br"; };
    const auto hasSubpackage = [&wasmSubpackages](const ea::string& name)
    {
        return ea::find(wasmSubpackages.begin(), wasmSubpackages.end(), name) != wasmSubpackages.end();
    };
    const ea::string compatibleName = MinigameWasmSubpackageName + "_compatible";

    ea::string standard = hasSubpackage(MinigameWasmSubpackageName)
        ? wasmPath(MinigameWasmSubpackageName) : EMPTY_STRING;
    ea::string compatible = hasSubpackage(compatibleName) ? wasmPath(compatibleName) : EMPTY_STRING;
    // A package carrying only the legacy variant ships it to everyone: it becomes wasm_file (the
    // module every device loads) and the fallback entry stays empty, exactly like a single-standard
    // package.
    if (standard.empty())
    {
        standard = compatible;
        compatible = EMPTY_STRING;
    }

    // Where the loading manager finds the game data; "" would mean the main package, which the
    // vendor budget makes impossible for anything but the smallest project.
    const bool hasData = fs->DirExists(resourceDir + DataDirName)
        || fs->DirExists(resourceDir + CoreDataDirName);
    const ea::string dataSubpackage = hasData ? MinigameDataSubpackageName : EMPTY_STRING;

    ea::string text;
    text += "// Generated by the editor build for this package: it names the subpackages this particular\n";
    text += "// build produced. Keep it dependency-free - rbfx_env.js reads rbfx.game_config lazily, and\n";
    text += "// nothing here may touch the DOM or vendor APIs.\n\n";
    text += "rbfx.game_config = {\n";
    text += "    // Engine module location, relative to the package root. A package assembled with a\n";
    text += "    // single variant carries that variant's path in wasm_file and \"\" here.\n";
    text += "    wasm_file: \"" + standard + "\",\n";
    text += "    wasm_file_compatible: \"" + compatible + "\",\n\n";
    text += "    // Data subpackage root name, \"\" when the game data ships in the main package. The\n";
    text += "    // loading manager downloads it before the engine starts; the file manifest maps the\n";
    text += "    // engine-visible names back onto it.\n";
    text += "    data_subpackage: \"" + dataSubpackage + "\",\n\n";
    text += "    // Engine VFS mount point for the game files.\n";
    text += "    data_root: \"Data\",\n\n";
    text += "    // Canvas sizing: true sizes the canvas to the physical resolution (logical size times\n";
    text += "    // the pixel ratio); false keeps the logical resolution and lets the vendor scale it.\n";
    text += "    screen: {native_resolution: true},\n\n";
    text += "    // The WeChat debug overlay switch; Douyin has no equivalent, so it is always off.\n";
    text += "    enable_debug: false,\n\n";
    text += "    // Ask the vendor update manager to check for a new package version on every launch.\n";
    text += "    enable_check_update: true\n";
    text += "};\n";

    return WriteTextFile(owner_.context(), owner_.outputDir() + "rbfx_game_config.js", text, message);
}

bool DouyinRuntimeStep::RenameForVendorCompatibility(
    ea::unordered_map<ea::string, ea::string>& vendorRenames, ea::string& message)
{
    auto* fs = owner_.context()->GetSubsystem<FileSystem>();

    // The whole assembled package is subject to the vendor filter, not only the data subpackage:
    // an unrecognized extension would be dropped wherever it sits. The bootstrap scripts and the
    // module pair are covered by the safe list, and the generated manifests do not exist yet, so
    // what is scanned here is exactly what the renames apply to. The returned map (package-
    // relative name after -> before) is what lets the manifest below tell a renamed file from
    // one that was born with the suffix - the file name alone cannot express that.
    ea::vector<ea::string> files;
    fs->ScanDir(files, owner_.outputDir(), "*", SCAN_FILES | SCAN_RECURSIVE);

    unsigned renamed = 0;
    for (const ea::string& file : files)
    {
        if (HasVendorSafeExtension(file))
            continue;

        const ea::string from = owner_.outputDir() + file;
        const ea::string to = from + VendorRenameSuffix[0];
        if (!fs->Rename(from, to))
        {
            message = Format("Could not rename '{}': the vendor file filter would drop it as is.", from);
            return false;
        }
        vendorRenames[file + VendorRenameSuffix[0]] = file;
        ++renamed;
    }

    if (renamed)
        URHO3D_LOGINFO("[Build] Renamed {} file(s) with the '{}' suffix: the vendor tool stages "
            "only the extensions it recognizes", renamed, VendorRenameSuffix[0]);
    return true;
}

bool DouyinRuntimeStep::WriteFileManifest(
    const ea::unordered_map<ea::string, ea::string>& vendorRenames, ea::string& message)
{
    auto* fs = owner_.context()->GetSubsystem<FileSystem>();
    const ea::string& resourceDir = owner_.resourceDir();

    // Every staged game file answers to the engine by its storage name under the data root
    // ("Data/..."), while it physically lives under the data subpackage; the manifest is the map
    // between the two, read synchronously by the runtime file layer. Sorted so the same input
    // produces the same manifest.
    ea::vector<ea::string> files;
    fs->ScanDir(files, resourceDir, "*", SCAN_FILES | SCAN_RECURSIVE);
    ea::sort(files.begin(), files.end());

    const ea::string root = MinigameDataSubpackageName + "/";
    ea::string text;
    text += "{\n";
    text += "  \"files\": [\n";
    for (unsigned i = 0; i < files.size(); ++i)
    {
        // RenameForVendorCompatibility appended the vendor suffix to every file whose extension
        // the tool would drop. The engine asks for the original name, so that is the manifest
        // key; the physical name rides along for the runtime file layer to open. The rename map
        // is keyed by the on-disk (renamed) package-relative name - the subpackage root plus
        // this entry as scanned.
        const ea::string packageRelative = MinigameDataSubpackageName + "/" + files[i];
        const bool renamed = vendorRenames.contains(packageRelative);
        const ea::string logical = renamed
            ? files[i].substr(0, files[i].length() - strlen(VendorRenameSuffix[0]))
            : files[i];
        const ea::string path = resourceDir + files[i];
        File handle(owner_.context(), path, FILE_READ);
        if (!handle.IsOpen())
        {
            message = Format("Could not read '{}' while writing the file manifest.", path);
            return false;
        }
        ea::string entry = Format("    {{\"path\": \"{}\", \"size\": {}, \"root\": \"{}\"", logical,
            handle.GetSize(), root);
        if (renamed)
            entry += Format(", \"physical\": \"{}\"", files[i]);
        entry += "}";
        text += entry;
        text += i + 1 < files.size() ? ",\n" : "\n";
    }
    text += "  ],\n";
    // The remote channel stays empty: the vendor runtime downloads the package whole, nothing is
    // fetched at run time.
    text += "  \"remote\": {}\n";
    text += "}\n";

    return WriteTextFile(owner_.context(), owner_.outputDir() + "rbfx_files.json", text, message);
}

bool DouyinRuntimeStep::EnsureDataSubpackageEntry(ea::string& message)
{
    auto* fs = owner_.context()->GetSubsystem<FileSystem>();
    const ea::string& resourceDir = owner_.resourceDir();

    // Every vendor subpackage root must carry a game.js: the runtime executes it right after the
    // subpackage download completes, which is how the wasm subpackage hands the engine module
    // factory to the loading manager. The data subpackage ships files rather than code - the
    // engine reaches them through the file layer - so its entry does nothing at all, but a
    // missing one makes the developer tool reject the package (".../ should have game.js").
    if (!fs->DirExists(resourceDir + DataDirName) && !fs->DirExists(resourceDir + CoreDataDirName))
        return true;

    ea::string text;
    text += "// Every subpackage root must carry a game.js; the vendor runtime executes it once the\n";
    text += "// subpackage download finishes. This subpackage ships game data only - the engine reads\n";
    text += "// the files through the file layer - so there is nothing to run here.\n";
    return WriteTextFile(owner_.context(), resourceDir + "game.js", text, message);
}

bool DouyinRuntimeStep::WriteGameManifest(const ea::vector<ea::string>& wasmSubpackages,
    ea::string& message)
{
    auto* fs = owner_.context()->GetSubsystem<FileSystem>();
    const BuildPlatformData* platform = owner_.platform();
    const ea::string& resourceDir = owner_.resourceDir();

    // The vendor manifest: the runtime reads the orientation and the subpackage list from here.
    // Every subpackage physically present is listed - the engine ones were checked above, the data
    // one only when there is data to download.
    ea::vector<ea::string> subpackages = wasmSubpackages;
    if (fs->DirExists(resourceDir + DataDirName) || fs->DirExists(resourceDir + CoreDataDirName))
        subpackages.push_back(MinigameDataSubpackageName);

    const ea::string orientation =
        platform->douyin_.orientation_ == "landscape" ? "landscape" : "portrait";

    ea::string text;
    text += "{\n";
    text += "  \"deviceOrientation\": \"" + orientation + "\",\n";
    text += "  \"subPackages\": [\n";
    for (unsigned i = 0; i < subpackages.size(); ++i)
    {
        text += "    {\"name\": \"" + subpackages[i] + "\", \"root\": \"" + subpackages[i] + "/\"}";
        text += i + 1 < subpackages.size() ? ",\n" : "\n";
    }
    text += "  ]\n";
    text += "}\n";

    return WriteTextFile(owner_.context(), owner_.outputDir() + "game.json", text, message);
}

bool DouyinRuntimeStep::WriteProjectConfig(ea::string& message)
{
    // The developer tool reads this when the output directory is opened as a project; the runtime
    // never looks at it. The appid is the one piece the editor cannot derive, so it is whatever the
    // platform settings carry - the tool asks for it on import when it is empty.
    const BuildPlatformData* platform = owner_.platform();

    ea::string text;
    text += "{\n";
    text += "  \"appid\": \"" + platform->douyin_.appId_ + "\",\n";
    text += "  \"projectname\": \"" + platform->executableName_ + "\"\n";
    text += "}\n";

    return WriteTextFile(owner_.context(), owner_.outputDir() + "project.config.json", text, message);
}

void DouyinRuntimeStep::ReportPackageSizes() const
{
    auto* fs = owner_.context()->GetSubsystem<FileSystem>();
    const ea::string& outputDir = owner_.outputDir();

    // The main package is what the vendor runtime downloads before anything runs, so it is the one
    // with a budget worth watching; the subpackages stream on demand. Sizes are read back from the
    // output tree so the numbers describe the package rather than what the steps think they wrote.
    ea::vector<ea::string> rootFiles;
    fs->ScanDir(rootFiles, outputDir, "*", SCAN_FILES);
    unsigned long long mainPackage = 0;
    for (const ea::string& file : rootFiles)
    {
        File handle(owner_.context(), outputDir + file, FILE_READ);
        if (handle.IsOpen())
            mainPackage += handle.GetSize();
    }

    unsigned long long total = mainPackage;
    ea::vector<ea::string> subpackages;
    fs->ScanDir(subpackages, outputDir, "package_*", SCAN_DIRS);
    subpackages.erase(ea::remove_if(subpackages.begin(), subpackages.end(),
        [](const ea::string& name) { return name == "." || name == ".."; }), subpackages.end());
    ea::sort(subpackages.begin(), subpackages.end());
    for (const ea::string& name : subpackages)
    {
        const unsigned long long size = owner_.DirectorySize(outputDir + name);
        total += size;
        URHO3D_LOGINFO("[Build] Subpackage '{}': {} KB", name, (size + 1023) / 1024);
    }

    URHO3D_LOGINFO("[Build] Package sizes: main {} KB of {} KB budget, total {} KB of {} KB budget",
        (mainPackage + 1023) / 1024, MainPackageBudget / 1024,
        (total + 1023) / 1024, TotalPackageBudget / 1024);
    if (mainPackage > MainPackageBudget)
        URHO3D_LOGWARNING("[Build] The main package is over the vendor budget; move content into a "
            "subpackage or the upload will be rejected.");
    if (total > TotalPackageBudget)
        URHO3D_LOGWARNING("[Build] The whole package is over the vendor budget; the upload will be "
            "rejected until it shrinks.");
}

} // namespace Urho3D

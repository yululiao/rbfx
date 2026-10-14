// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

// The platform-agnostic data stages, each a BuildStep: validate, wait for cooking, clean and recreate
// the output, stage the project's Data/ and the engine's CoreData/ into staging, compile and prune
// scripts, pack the resources and report. They reach the resolved paths, platform, error list and
// process runner through the owning BuildPlatform. Bodies are the pipeline's own, moved verbatim
// into the step types.

#include "../../Assets/TextureImportSettings.h"
#include "../BuildInternal.h"
#include "../BuildPlatform.h"
#include "../BuildSettings.h"
#include "BuildSteps.h"
#include "../../Project/AssetManager.h"
#include "../../Project/Project.h"

#include <Urho3D/Core/StringUtils.h>
#include <Urho3D/IO/File.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/IO/PackageFile.h>

#include <EASTL/algorithm.h>

#include <cstdlib>

namespace Urho3D
{

namespace
{

/// Copy a directory tree on top of another one. FileSystem::CopyDir is the merge this needs - it
/// creates missing directories and opens destinations for writing, so a later copy replaces what an
/// earlier one put there. It reports success for a source that does not exist though, which cannot
/// be told apart from an empty directory, so the existence check happens here.
bool MergeDirectory(FileSystem* fs, const ea::string& source, const ea::string& destination, ea::string& message)
{
    if (!fs->DirExists(source))
    {
        message = Format("Nothing to copy from '{}': the directory does not exist.", source);
        return false;
    }
    if (!fs->CopyDir(source, destination))
    {
        message = Format("Failed to copy '{}' into '{}'.", source, destination);
        return false;
    }
    return true;
}

// --- Asset filter matching. Paths are staged-relative, forward slashes, compared case-insensitively.
// --- A game that loads its assets from Lua by runtime-concatenated names cannot be pruned by static
// --- reference analysis, so inclusion is a hand-authored allowlist (see AssetFilterSettings).

/// ASCII lowercase; resource paths are ASCII, other bytes pass through untouched.
inline char LowerAscii(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool IEquals(const ea::string& a, const ea::string& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i != a.size(); ++i)
        if (LowerAscii(a[i]) != LowerAscii(b[i]))
            return false;
    return true;
}

bool IStartsWith(const ea::string& a, const ea::string& prefix)
{
    if (a.size() < prefix.size())
        return false;
    for (size_t i = 0; i != prefix.size(); ++i)
        if (LowerAscii(a[i]) != LowerAscii(prefix[i]))
            return false;
    return true;
}

/// Recursive glob over a path. '*' matches within one segment (never crosses '/'), '?' matches one
/// non-'/' char, '**' matches across segments. 'si'/'pi' are cursors into subject/pattern.
bool GlobFrom(const ea::string& s, size_t si, const ea::string& p, size_t pi)
{
    while (pi < p.size())
    {
        const char pc = p[pi];
        if (pc == '*')
        {
            const bool dbl = (pi + 1 < p.size() && p[pi + 1] == '*');
            if (dbl)
            {
                pi += 2;
                if (pi < p.size() && p[pi] == '/') // "**/" also matches the empty span
                    ++pi;
                for (size_t k = si;; ++k)
                {
                    if (GlobFrom(s, k, p, pi))
                        return true;
                    if (k >= s.size())
                        return false;
                }
            }
            ++pi;
            for (size_t k = si;; ++k)
            {
                if (GlobFrom(s, k, p, pi))
                    return true;
                if (k >= s.size() || s[k] == '/') // single '*' stays inside the segment
                    return false;
            }
        }
        else if (pc == '?')
        {
            if (si >= s.size() || s[si] == '/')
                return false;
            ++si;
            ++pi;
        }
        else
        {
            if (si >= s.size() || LowerAscii(s[si]) != LowerAscii(pc))
                return false;
            ++si;
            ++pi;
        }
    }
    return si == s.size();
}

/// One rule against one relative path. A pattern with no wildcard is a directory prefix (whole
/// subtree); otherwise it is a glob. Backslashes and a trailing slash in the pattern are normalized.
bool MatchRule(const ea::string& rel, ea::string pattern)
{
    pattern.replace("\\", "/");
    while (!pattern.empty() && pattern.back() == '/')
        pattern.pop_back();
    if (pattern.empty())
        return false;
    if (pattern.find('*') == ea::string::npos && pattern.find('?') == ea::string::npos)
    {
        if (IEquals(rel, pattern))
            return true;
        return rel.size() > pattern.size() && rel[pattern.size()] == '/' && IStartsWith(rel, pattern);
    }
    return GlobFrom(rel, 0, pattern, 0);
}

/// The scaffolding that always ships regardless of the allowlist, so a filter cannot brick the game:
/// root-level *.json entry configs (Game.json) and the Scenes/, Scripts/, UI/ roots.
bool IsProtectedRoot(const ea::string& rel)
{
    if (rel.find('/') == ea::string::npos)
    {
        static const ea::string jsonExt = ".json";
        return rel.size() >= jsonExt.size() && IEquals(rel.substr(rel.size() - jsonExt.size()), jsonExt);
    }
    return IStartsWith(rel, "Scenes/") || IStartsWith(rel, "Scripts/") || IStartsWith(rel, "UI/");
}

/// Bytes of a file, or zero when it cannot be opened. Only used to size a dropped file for the report.
unsigned long long FileSizeOf(Context* context, const ea::string& path)
{
    File file(context, path, FILE_READ);
    return file.IsOpen() ? static_cast<unsigned long long>(file.GetSize()) : 0ULL;
}

/// Tally of one staging pass: what the filter dropped, and which Include entries actually matched.
struct FilterStats
{
    unsigned dropped = 0;
    unsigned long long droppedBytes = 0ULL;
    ea::vector<char> includeHit; // parallel to filter.include_
};

/// Keep-or-drop a staged relative path. 'absPath' sizes a dropped file for the report. Protected roots
/// win over everything; an empty Include means the filter is off and everything is kept.
bool ShouldKeep(const ea::string& relIn, const ea::string& absPath, Context* context,
    const AssetFilterSettings& filter, FilterStats& stats)
{
    ea::string rel = relIn;
    rel.replace("\\", "/");

    // Record the matching Include entry even when a protected root keeps the file anyway, so a
    // redundant "Scripts/**" in the allowlist does not raise a spurious zero-match warning.
    int hit = -1;
    if (filter.IsEnabled())
    {
        for (size_t i = 0; i != filter.include_.size(); ++i)
        {
            if (MatchRule(rel, filter.include_[i]))
            {
                hit = static_cast<int>(i);
                break;
            }
        }
        if (hit >= 0 && hit < static_cast<int>(stats.includeHit.size()))
            stats.includeHit[hit] = 1;
    }

    if (IsProtectedRoot(rel))
        return true;
    if (!filter.IsEnabled())
        return true;

    auto drop = [&]() {
        ++stats.dropped;
        stats.droppedBytes += FileSizeOf(context, absPath);
    };

    if (hit < 0)
    {
        drop();
        return false;
    }
    for (const ea::string& ex : filter.exclude_)
    {
        if (MatchRule(rel, ex))
        {
            drop();
            return false;
        }
    }
    return true;
}

/// The merge MergeDirectory cannot do: FileSystem::CopyDir never skips a file, and the staged Data/
/// must leave cooking-only sources behind (IsCookingSourceFile). Merge semantics are otherwise the
/// same; directories are created around each surviving file rather than copied up front, so a folder
/// that held nothing but sources does not resurface empty in the package.
bool MergeProjectData(FileSystem* fs, Context* context, const ea::string& source, const ea::string& destination,
    const AssetFilterSettings& filter, FilterStats& stats, ea::string& message)
{
    if (!fs->DirExists(source))
    {
        message = Format("Nothing to copy from '{}': the directory does not exist.", source);
        return false;
    }

    ea::vector<ea::string> files;
    fs->ScanDir(files, source, "*", SCAN_FILES | SCAN_RECURSIVE);

    unsigned skipped = 0;
    for (const ea::string& relative : files)
    {
        if (IsCookingSourceFile(relative))
        {
            ++skipped;
            continue;
        }
        // The directory allowlist carves the staged tree down to what the game actually ships.
        if (!ShouldKeep(relative, source + relative, context, filter, stats))
            continue;

        const ea::string target = destination + relative;
        if (!fs->CreateDirsRecursive(GetPath(target)))
        {
            message = Format("Could not create the staging directory for '{}'.", target);
            return false;
        }
        if (!fs->Copy(source + relative, target))
        {
            message = Format("Failed to copy '{}' into '{}'.", source + relative, destination);
            return false;
        }
    }

    if (skipped > 0)
        URHO3D_LOGINFO("[Build] Left {} cooking source file(s) out of the staged Data/", skipped);
    return true;
}

} // namespace

bool ValidateStep::Run(ea::string& message)
{
    auto* fs = owner_.context()->GetSubsystem<FileSystem>();
    auto* settings = owner_.GetSettings();
    auto* project = owner_.context()->GetSubsystem<Project>();
    const BuildPlatformData* platform = owner_.platform();
    ea::vector<ea::string>& errors = owner_.errors();

    errors.clear();
    settings->Validate(*platform, errors);

    // Two facts a platform cannot speak for, because they are about the project rather than about
    // the toolchain: that there is a data tree at all, and that it names an entry point. Without
    // Game.json the shipped game starts, prints a warning and shows an empty scene, which a user
    // will read as "the build is broken" instead of "the project is incomplete".
    const ea::string dataPath = NormalizeDir(project->GetDataPath());
    if (!fs->DirExists(dataPath))
        errors.push_back(Format("The project has no data directory: '{}'.", dataPath));
    else if (!fs->FileExists(dataPath + "Game.json"))
    {
        errors.push_back(Format("'{}' has no Game.json. The shipped game would not know which scene "
            "to load or which script to run.", dataPath));
    }

    // The output directory is emptied at the start of a build, so a mistyped path must not be
    // allowed to cost somebody a source tree or a volume.
    const ea::string& outputDir = owner_.outputDir();
    const ea::string output = RemoveTrailingSlash(outputDir);
    const ea::string parent = RemoveTrailingSlash(GetPath(output));
    if (parent.empty() || parent == output)
    {
        errors.push_back(Format("Refusing to build into '{}' because the whole directory is deleted "
            "first. Point OutputDir at a dedicated build directory.", outputDir));
    }
    else
    {
        const ea::string protectedDirs[] = {
            RemoveTrailingSlash(ForwardSlashes(project->GetProjectPath())),
            RemoveTrailingSlash(ForwardSlashes(platform->engineData_)),
            RemoveTrailingSlash(ForwardSlashes(platform->engineBin_)),
        };
        for (const ea::string& protectedDir : protectedDirs)
        {
            if (!protectedDir.empty() && output == protectedDir)
            {
                errors.push_back(Format("Refusing to empty '{}' because a build reads from it.", outputDir));
                break;
            }
        }
    }

    if (errors.empty())
        return true;

    for (const ea::string& error : errors)
        URHO3D_LOGERROR("[Build] {}", error);
    message = Format("Platform '{}' is not buildable, {} problem(s) above.", owner_.GetPlatformName(), errors.size());
    return false;
}

bool AwaitAssetsStep::Run(ea::string& message)
{
    auto* project = owner_.context()->GetSubsystem<Project>();
    auto* assetManager = project ? project->GetAssetManager() : nullptr;

    // Without an asset manager there is nothing cooking to wait for; the Cache simply contributes no
    // outputs and StageData copies only the Data/ trees.
    if (!assetManager)
        return true;

    if (assetManager->IsProcessing())
    {
        const auto progress = assetManager->GetProgress();
        URHO3D_LOGINFO("[Build] Waiting for asset cooking ({}/{})", progress.first, progress.second);
        // Hold on this step; the platform re-runs it next frame instead of advancing.
        owner_.HoldStep();
    }
    return true;
}

bool CleanOutputStep::Run(ea::string& message)
{
    auto* fs = owner_.context()->GetSubsystem<FileSystem>();
    const ea::string& outputDir = owner_.outputDir();

    if (fs->DirExists(outputDir) && !fs->RemoveDir(outputDir, true))
    {
        message = Format("Could not empty '{}'. A game launched from a previous build is the usual "
            "reason; close it and build again.", outputDir);
        return false;
    }
    if (!fs->CreateDirsRecursive(outputDir))
    {
        message = Format("Could not create '{}'.", outputDir);
        return false;
    }
    return true;
}

bool StageDataStep::Run(ea::string& message)
{
    auto* fs = owner_.context()->GetSubsystem<FileSystem>();
    auto* project = owner_.context()->GetSubsystem<Project>();

    const ea::string staged = owner_.stagingDir() + DataDirName;

    // Everything below is gated by the platform's directory allowlist. An empty Include leaves the
    // filter off, so the whole tree ships exactly as before. The tally spans both staging passes.
    const AssetFilterSettings& filter = owner_.platform()->assetFilter_;
    FilterStats stats;
    stats.includeHit.assign(filter.include_.size(), 0);

    // The project's Data/ is the only source of this tree: engine sample resources are not the
    // game's, and the engine's own contribution to a package is CoreData/, staged separately. A
    // project that needs an engine sample copies it into its own Data/. Cooking-only sources stay
    // behind: nothing resolves the .fbx after the import, only the .mdl staged from the Cache.
    if (!MergeProjectData(fs, owner_.context(), NormalizeDir(project->GetDataPath()), staged, filter, stats, message))
        return false;

    // Imported assets cook their runtime-format products (the .mdl/.ani behind a source .fbx) into the
    // Cache satellite directories, not into Data/, and scenes reference those satellite names directly.
    // AwaitAssets already guaranteed cooking finished, so every output the manifest records must exist;
    // copying it into the staged tree under the same relative name is what makes the packaged resource
    // keys resolve at runtime. Last so a cooked output wins any name clash with a plain Data/ file.
    if (auto* assetManager = project->GetAssetManager())
    {
        const ea::string cachePath = AddTrailingSlash(project->GetCachePath());
        for (const ea::string& output : assetManager->GetAllCacheOutputs())
        {
            const ea::string source = cachePath + output;
            if (!fs->FileExists(source))
            {
                message = Format("Cooked output '{}' is recorded in the manifest but missing from the "
                    "Cache. Re-import the asset before shipping.", source);
                return false;
            }
            // The same allowlist governs cooked outputs as Data/ files: they share the relative key
            // space, so an excluded model's cooked products never reach the package either.
            if (!ShouldKeep(output, source, owner_.context(), filter, stats))
                continue;
            const ea::string destination = staged + output;
            if (!fs->CreateDirsRecursive(GetPath(destination)))
            {
                message = Format("Could not create the staging directory for '{}'.", destination);
                return false;
            }
            if (!fs->Copy(source, destination))
            {
                message = Format("Could not stage cooked output '{}'.", destination);
                return false;
            }
        }
    }

    if (filter.IsEnabled())
    {
        URHO3D_LOGINFO("[Build] Asset filter active: dropped {} file(s), {} bytes not staged", stats.dropped,
            stats.droppedBytes);
        // An Include entry that matched nothing is almost always a typo or a moved folder; without
        // this it would silently ship nothing for that line and the breakage surfaces only at runtime.
        for (size_t i = 0; i != filter.include_.size(); ++i)
        {
            if (i >= stats.includeHit.size() || !stats.includeHit[i])
            {
                URHO3D_LOGWARNING("[Build] Asset filter Include '{}' matched nothing - check the path "
                    "(staged-relative, forward slashes, no typos).", filter.include_[i]);
            }
        }
    }
    return true;
}

bool CompileScriptsStep::Run(ea::string& message)
{
    auto* settings = owner_.GetSettings();
    const BuildPlatformData* platform = owner_.platform();

    const ea::string compiler = settings->FindTool(*platform, "LuaCompiler");
    if (compiler.empty())
    {
        message = "LuaCompiler disappeared between validation and this stage.";
        return false;
    }

    const ea::string staged = owner_.stagingDir() + DataDirName;

    // Input root and output root are the same directory on purpose: the tool mirrors every source
    // path onto itself with a .luc extension, so the containers land exactly where the resource
    // names in the scenes already point. Encrypting the whole staged tree rather than only the
    // project's folder is also what makes the "no plain source ships" check afterwards meaningful,
    // because engine samples carry .lua files too.
    ea::vector<ea::string> arguments;
    arguments.push_back("--recursive");
    arguments.push_back("--encrypt");
    arguments.push_back("--out=" + staged);

    if (!platform->scriptKeyEnvVar_.empty())
    {
        if (const char* key = getenv(platform->scriptKeyEnvVar_.c_str()); key && *key)
            arguments.push_back("--key=" + ea::string(key));
    }
    arguments.push_back(staged);

    return owner_.StartProcess(compiler, arguments,
        [this](ea::string& resumeMessage) { return PruneStagedSources(resumeMessage); }, message);
}

bool CompileScriptsStep::PruneStagedSources(ea::string& message)
{
    auto* fs = owner_.context()->GetSubsystem<FileSystem>();
    const ea::string staged = owner_.stagingDir() + DataDirName;

    ea::vector<ea::string> sources;
    fs->ScanDir(sources, staged, "*.lua", SCAN_FILES | SCAN_RECURSIVE);

    unsigned removed = 0;
    for (const ea::string& relative : sources)
    {
        const ea::string source = staged + relative;
        const size_t dot = source.rfind('.');
        const ea::string container = source.substr(0, dot) + ".luc";

        // A source without its compiled twin would leave the game without that script if the plain
        // file were deleted, and shipping both would defeat the encryption. Neither is acceptable,
        // so an incomplete compile fails the build instead of warning about it.
        if (!fs->FileExists(container))
        {
            message = Format("LuaCompiler produced no '{}' for '{}'.", container, source);
            return false;
        }
        if (!fs->Delete(source))
        {
            message = Format("Could not delete the plain script '{}'.", source);
            return false;
        }
        ++removed;
    }

    URHO3D_LOGINFO("[Build] Compiled scripts now ship as .luc; removed {} plain source file(s)", removed);
    return true;
}

bool StageCoreDataStep::Run(ea::string& message)
{
    auto* fs = owner_.context()->GetSubsystem<FileSystem>();
    const BuildPlatformData* platform = owner_.platform();
    const ea::string source = AddTrailingSlash(ForwardSlashes(platform->engineData_)) + CoreDataDirName;
    const ea::string destination = owner_.stagingDir() + CoreDataDirName;
    if (platform->coreDataExclude_.empty())
        return MergeDirectory(fs, source, destination, message);

    // Selective staging, same copy shape as MergeProjectData: directories are created around each
    // surviving file, so an excluded subtree does not resurface as empty folders in the package.
    if (!fs->DirExists(source))
    {
        message = Format("Nothing to copy from '{}': the directory does not exist.", source);
        return false;
    }

    ea::vector<ea::string> files;
    fs->ScanDir(files, source, "*", SCAN_FILES | SCAN_RECURSIVE);

    unsigned dropped = 0;
    unsigned long long droppedBytes = 0ULL;
    for (const ea::string& relative : files)
    {
        ea::string rel = relative;
        rel.replace("\\", "/");
        bool excluded = false;
        for (const ea::string& ex : platform->coreDataExclude_)
        {
            if (MatchRule(rel, ex))
            {
                excluded = true;
                break;
            }
        }
        if (excluded)
        {
            ++dropped;
            droppedBytes += FileSizeOf(owner_.context(), source + relative);
            continue;
        }
        const ea::string target = destination + relative;
        if (!fs->CreateDirsRecursive(GetPath(target)))
        {
            message = Format("Could not create the staging directory for '{}'.", target);
            return false;
        }
        if (!fs->Copy(source + relative, target))
        {
            message = Format("Could not copy '{}' to '{}'.", source + relative, target);
            return false;
        }
    }

    URHO3D_LOGINFO("[Build] CoreData exclude active: dropped {} file(s), {} bytes not staged",
        dropped, droppedBytes);
    return true;
}

bool ExportDataStep::Run(ea::string& message)
{
    auto* fs = owner_.context()->GetSubsystem<FileSystem>();
    auto* settings = owner_.GetSettings();
    const BuildPlatformData* platform = owner_.platform();

    const ea::string dirName = coreData_ ? CoreDataDirName : DataDirName;
    const ea::string packageName = coreData_ ? CoreDataPackageName : DataPackageName;
    const ea::string staged = owner_.stagingDir() + dirName;
    const ea::string& resourceDir = owner_.resourceDir();

    if (!platform->packData_)
    {
        if (!MergeDirectory(fs, staged, resourceDir + dirName, message))
            return false;
        return VerifyExportedResources(message);
    }

    const ea::string package = resourceDir + packageName;
    const ea::string tool = settings->FindTool(*platform, "PackageTool");
    if (tool.empty())
    {
        message = "PackageTool disappeared between validation and this stage.";
        return false;
    }

    // PackageTool leaves an existing package alone when it considers it up to date, comparing
    // timestamps against whatever is already there. A package that another platform wrote into the
    // same destination is exactly the artifact that must not survive that check, so it goes first.
    if (fs->FileExists(package) && !fs->Delete(package))
    {
        message = Format("Could not delete the stale package '{}'.", package);
        return false;
    }
    if (!fs->CreateDirsRecursive(GetPath(package)))
    {
        message = Format("Could not create '{}'.", GetPath(package));
        return false;
    }

    // No basepath argument: package entries have to be relative to the directory being packed,
    // which is what makes them match the resource names the runtime asks for.
    ea::vector<ea::string> arguments{ staged, package };
    if (platform->compressPackages_)
        arguments.push_back("-c");

    return owner_.StartProcess(tool, arguments,
        [this](ea::string& resumeMessage) { return VerifyExportedResources(resumeMessage); }, message);
}

bool ExportDataStep::VerifyExportedResources(ea::string& message)
{
    auto* fs = owner_.context()->GetSubsystem<FileSystem>();
    const BuildPlatformData* platform = owner_.platform();

    const ea::string dirName = coreData_ ? CoreDataDirName : DataDirName;
    const ea::string packageName = coreData_ ? CoreDataPackageName : DataPackageName;
    const ea::string& resourceDir = owner_.resourceDir();

    if (!platform->packData_)
    {
        // Loose directories: proving the entry configuration arrived is enough, because a copy that
        // partially failed already made CopyDir report failure.
        const ea::string entry = resourceDir + dirName + "Game.json";
        if (!coreData_ && !fs->FileExists(entry))
        {
            message = Format("'{}' is missing from the output.", entry);
            return false;
        }
        return true;
    }

    const ea::string package = resourceDir + packageName;

    // Read the result back with the same class the game will use, rather than asking the bundler to
    // describe its own output: this proves the header, the entry table and the data offsets all
    // agree, which is more than a tool printing a number it just computed.
    PackageFile reader(owner_.context());
    if (!reader.Open(package))
    {
        message = Format("'{}' was written but cannot be read back.", package);
        return false;
    }
    if (reader.GetNumFiles() == 0)
    {
        message = Format("'{}' contains no files.", package);
        return false;
    }
    URHO3D_LOGINFO("[Build] {} holds {} file(s), {} bytes, {}", packageName, reader.GetNumFiles(),
        reader.GetTotalDataSize(), reader.IsCompressed() ? "LZ4 compressed" : "uncompressed");

    if (coreData_)
        return true;

    if (!reader.Exists("Game.json"))
    {
        message = Format("'{}' does not contain Game.json, so the shipped game cannot find its entry "
            "configuration.", package);
        return false;
    }

    // Only a platform that encrypts has something to prove about script formats: for every other
    // build the plain .lua file IS the shipped artifact, and refusing it would reject the common
    // case. When encryption is on, a surviving source file is worse than a missing container - the
    // runtime prefers the .luc, so the source is dead weight that reads as an unprotected script.
    if (platform->encryptScripts_)
    {
        unsigned containers = 0;
        for (const auto& entry : reader.GetEntries())
        {
            if (ExtensionIs(entry.first, "luc"))
                ++containers;
            else if (ExtensionIs(entry.first, "lua"))
            {
                message = Format("'{}' still contains the plain script '{}'.", package, entry.first);
                return false;
            }
        }
        if (containers == 0)
        {
            message = Format("EncryptScripts is on but '{}' holds no .luc container.", package);
            return false;
        }
    }
    return true;
}

bool SummaryStep::Run(ea::string& message)
{
    auto* fs = owner_.context()->GetSubsystem<FileSystem>();
    const ea::string& outputDir = owner_.outputDir();

    ea::vector<ea::string> entries;
    fs->ScanDir(entries, outputDir, "*", SCAN_DIRS | SCAN_FILES);
    // A scan that includes directories also answers with '.' and '..', which are this very output
    // directory and the directory around it. Left in, the summary would report the package once as
    // its contents and once again as a directory containing them.
    entries.erase(ea::remove_if(entries.begin(), entries.end(),
                    [](const ea::string& entry) { return entry == "." || entry == ".."; }),
        entries.end());
    ea::sort(entries.begin(), entries.end());

    unsigned long long total = 0;
    for (const ea::string& entry : entries)
    {
        const ea::string path = outputDir + entry;
        const unsigned long long size = fs->DirExists(path) ? owner_.DirectorySize(path) : [&]()
        {
            File file(owner_.context(), path, FILE_READ);
            return file.IsOpen() ? static_cast<unsigned long long>(file.GetSize()) : 0ULL;
        }();
        total += size;
        URHO3D_LOGINFO("[Build]   {:>14} bytes  {}", size, entry);
    }

    URHO3D_LOGINFO("[Build]   {:>14} bytes  total, {} top level entr{} in {}", total, entries.size(),
        entries.size() == 1 ? "y" : "ies", outputDir);
    return true;
}

} // namespace Urho3D

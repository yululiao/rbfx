// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

// Texture compression stage as a BuildStep: the multi-frame walk that runs PVRTexTool over every
// staged texture source, backed by a persistent cook cache so a no-change rebuild reuses products
// instead of waking the tool. Unlike the other steps this one carries its own state - the queue and
// its cursor live in the step, which is the whole point of making a stage a type: nothing on the
// platform has to know that texture cooking is the only stage that spans many processes.

#include "../../Assets/TextureImportSettings.h"
#include "../BuildInternal.h"
#include "../BuildPlatform.h"
#include "../BuildSettings.h"
#include "BuildSteps.h"
#include "../../Project/Project.h"

#include <Urho3D/Math/StringHash.h>
#include <Urho3D/Core/StringUtils.h>
#include <Urho3D/IO/File.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Resource/Image.h>

namespace Urho3D
{

bool CompressTexturesStep::Run(ea::string& message)
{
    auto* fs = owner_.context()->GetSubsystem<FileSystem>();
    auto* settings = owner_.GetSettings();
    auto* project = owner_.context()->GetSubsystem<Project>();
    const BuildPlatformData* platform = owner_.platform();

    // Resolve the tool once, up front: a platform that enabled compression but cannot find the tool has
    // to fail before a single texture is decoded, not halfway through the queue.
    const ea::string tool = settings->FindTool(*platform, "PVRTexToolCLI");
    if (tool.empty())
    {
        message = "PVRTexToolCLI disappeared between validation and this stage.";
        return false;
    }
    toolPath_ = tool;

    const TextureCompressionSettings tc = platform->GetEffectiveTextureCompression();
    const ea::string container = tc.container_.empty() ? ea::string("dds") : tc.container_;
    const ea::string staged = owner_.stagingDir() + DataDirName;
    // Artifacts survives between builds and, unlike Cache, is not mounted as a resource root, so a
    // cooked texture parked here can never be mistaken for one the game loads by name.
    const ea::string cacheRoot = NormalizeDir(project->GetArtifactsPath()) + "TextureCompression/";

    ea::vector<ea::string> found;
    fs->ScanDir(found, staged, "*", SCAN_FILES | SCAN_RECURSIVE);

    queue_.clear();
    index_ = 0;

    for (const ea::string& rawRelative : found)
    {
        const ea::string relative = ForwardSlashes(rawRelative);
        if (!IsTextureSourceFile(relative))
            continue;

        const ea::string stagedSource = staged + relative;
        // The original in Data/, when there is one, supplies both the import metadata and the half of
        // the cache key that has to outlive the build. The staged copy cannot: FileSystem::Copy rewrites
        // it and stamps it with the build time, so its mtime differs on every run.
        const ea::string original = FindOriginalDataFile(relative);
        const ea::string& keySource = original.empty() ? stagedSource : original;

        TextureImporterParams params;
        LoadTextureImporterParams(owner_.context(), keySource, params);

        // Normal maps are data and always cook linear, whatever a hand-edited metadata file claims.
        const bool isLinear = params.textureType_ == TextureImportType::NormalMap
            || params.colorSpace_ == TextureImportColorSpace::Linear;
        // The per-file mipmap mode resolves against the platform here, so the fingerprint and the
        // tool call always agree on what gets baked.
        const bool bakeMipmaps = params.mipmapMode_ == TextureImportMipmapMode::Enabled
            || (params.mipmapMode_ == TextureImportMipmapMode::Inherit && tc.mipmaps_);

        unsigned sourceSize = 0;
        {
            File probe(owner_.context(), stagedSource, FILE_READ);
            if (probe.IsOpen())
                sourceSize = probe.GetSize();
        }
        const unsigned sourceTime = fs->GetLastModifiedTime(keySource);

        // Everything that decides the output except the alpha channel, which only picks between the two
        // color formats. Alpha stays out of the key on purpose: an edit that changes it changes the
        // source mtime too, which the key already carries. Leaving it out is what lets a no-change
        // rebuild skip the image decode entirely and go straight to the cached product.
        const ea::string fingerprint = Format("{}|{}|{}|{}|{}|{}|{}|{}|{}", sourceTime, sourceSize,
            params.textureType_ == TextureImportType::NormalMap ? "normal" : "color", isLinear ? "lRGB" : "sRGB",
            tc.colorFormatNoAlpha_, tc.colorFormatAlpha_, tc.normalFormat_, bakeMipmaps ? "mip" : "nomip",
            tc.quality_);

        const size_t dot = relative.rfind('.');
        const ea::string relativeNoExt = relative.substr(0, dot);

        Job job;
        job.relative_ = relative;
        job.stagedSource_ = stagedSource;
        job.stagedDest_ = staged + relativeNoExt + "." + container;
        job.legacySidecarDir_ = stagedSource + ".d";
        job.cacheProduct_ = cacheRoot + relativeNoExt + "-" +
            Format("{:08x}", StringHash(fingerprint + "|" + container, StringHash::NoReverse{}).Value()) + "." + container;
        job.isNormal_ = params.textureType_ == TextureImportType::NormalMap;
        job.params_ = params;
        job.mipmaps_ = bakeMipmaps;

        if (!fs->CreateDirsRecursive(GetPath(job.cacheProduct_)))
        {
            message = Format("Could not create the texture cache directory for '{}'.", job.relative_);
            return false;
        }
        queue_.push_back(ea::move(job));
    }

    if (queue_.empty())
    {
        URHO3D_LOGINFO("[Build] No texture sources were staged; compression did nothing");
        return true;
    }
    return RunQueue(message);
}

bool CompressTexturesStep::RunQueue(ea::string& message)
{
    auto* fs = owner_.context()->GetSubsystem<FileSystem>();
    const TextureCompressionSettings tc = owner_.platform()->GetEffectiveTextureCompression();

    while (index_ < queue_.size())
    {
        Job& job = queue_[index_];

        // A product whose name still matches the fingerprint is byte-for-byte what this source and these
        // settings produce, so it goes straight into staging and the tool is never woken.
        if (fs->FileExists(job.cacheProduct_))
        {
            if (!InstallCooked(job, message))
                return false;
            ++cached_;
            ++index_;
            continue;
        }

        // Cache miss: decode just enough to choose the format. A normal map always uses its own format
        // and stays linear; anything else is a color texture whose alpha picks between the two formats.
        bool hasAlpha = false;
        {
            File file(owner_.context(), job.stagedSource_, FILE_READ);
            if (!file.IsOpen())
            {
                message = Format("Could not open the staged texture '{}'.", job.stagedSource_);
                return false;
            }
            Image image(owner_.context());
            if (!image.BeginLoad(file))
            {
                message = Format("'{}' is not a readable image, so it cannot be compressed.", job.stagedSource_);
                return false;
            }
            hasAlpha = image.HasAlphaChannel();
        }

        const ea::string format = job.isNormal_
            ? tc.normalFormat_
            : (hasAlpha ? tc.colorFormatAlpha_ : tc.colorFormatNoAlpha_);
        if (format.empty())
        {
            message = Format("No texture format is configured for '{}'.", job.relative_);
            return false;
        }
        // Normal maps store directions, not colors; encoding them as sRGB would bend every vector.
        // Linear color textures (masks, LUTs) keep their data unconverted for the same reason.
        const char* colorSpace = job.isNormal_ || job.params_.colorSpace_ == TextureImportColorSpace::Linear
            ? "lRGB"
            : "sRGB";

        ea::vector<ea::string> arguments;
        arguments.push_back("-i");
        arguments.push_back(job.stagedSource_);
        arguments.push_back("-o");
        arguments.push_back(job.cacheProduct_);
        arguments.push_back("-f");
        arguments.push_back(Format("{},UBN,{}", format, colorSpace));
        if (job.mipmaps_)
            arguments.push_back("-m");
        if (!tc.quality_.empty())
        {
            arguments.push_back("-q");
            arguments.push_back(tc.quality_);
        }
        // The tool reports progress on stderr, which the async runner would otherwise surface as noise.
        arguments.push_back("-shh");

        const unsigned index = index_;
        return owner_.StartProcess(toolPath_, arguments,
            [this, index](ea::string& resumeMessage) { return FinalizeCooked(index, resumeMessage); },
            message);
    }

    URHO3D_LOGINFO("[Build] Textures: {} compressed, {} reused from cache", compressed_, cached_);
    return true;
}

bool CompressTexturesStep::FinalizeCooked(unsigned index, ea::string& message)
{
    auto* fs = owner_.context()->GetSubsystem<FileSystem>();
    Job& job = queue_[index];

    // A zero exit already gated getting here, but the product is the only proof that matters: the tool
    // can exit cleanly and still write nothing when it dislikes the requested format.
    if (!fs->FileExists(job.cacheProduct_))
    {
        message = Format("PVRTexToolCLI finished but produced no '{}'.", job.cacheProduct_);
        return false;
    }
    if (!InstallCooked(job, message))
        return false;
    ++compressed_;
    ++index_;
    // Keep the queue moving; the next texture either hits the cache or suspends this step again.
    return RunQueue(message);
}

bool CompressTexturesStep::InstallCooked(const Job& job, ea::string& message)
{
    auto* fs = owner_.context()->GetSubsystem<FileSystem>();

    if (!fs->CreateDirsRecursive(GetPath(job.stagedDest_)))
    {
        message = Format("Could not create the staging directory for '{}'.", job.stagedDest_);
        return false;
    }
    if (!fs->Copy(job.cacheProduct_, job.stagedDest_))
    {
        message = Format("Could not place the cooked texture '{}'.", job.stagedDest_);
        return false;
    }
    // The source is dead weight now: the runtime router redirects its name to this product, so shipping
    // both would carry the uncompressed pixels for nothing and roughly double the package.
    if (!fs->Delete(job.stagedSource_))
    {
        message = Format("Could not remove the uncompressed source '{}'.", job.stagedSource_);
        return false;
    }
    // Import metadata is editor-only in the legacy scheme; its leftover ".d" directories must not
    // ship beside the cooked texture. The current ".texmeta" file is different: the runtime reads
    // it, so it stays in staging exactly as it was copied from Data/ - nothing to generate, nothing
    // to remove.
    if (fs->DirExists(job.legacySidecarDir_) && !fs->RemoveDir(job.legacySidecarDir_, true))
    {
        message = Format("Could not remove the legacy import metadata '{}'.", job.legacySidecarDir_);
        return false;
    }
    return true;
}

ea::string CompressTexturesStep::FindOriginalDataFile(const ea::string& relative) const
{
    auto* fs = owner_.context()->GetSubsystem<FileSystem>();
    auto* project = owner_.context()->GetSubsystem<Project>();
    const BuildPlatformData* platform = owner_.platform();

    // Project Data/ is copied over engine Data/, so a file present in both came from the project.
    const ea::string projectData = NormalizeDir(project->GetDataPath());
    if (fs->FileExists(projectData + relative))
        return projectData + relative;
    if (platform->includeEngineData_)
    {
        const ea::string engineData = AddTrailingSlash(ForwardSlashes(platform->engineData_)) + DataDirName;
        if (fs->FileExists(engineData + relative))
            return engineData + relative;
    }
    return EMPTY_STRING;
}

} // namespace Urho3D

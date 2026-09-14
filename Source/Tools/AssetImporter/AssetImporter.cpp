//
// Copyright (c) 2008-2022 the Urho3D project.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include <Urho3D/Core/Context.h>
#include <Urho3D/Core/ProcessUtils.h> // PrintLine
#include <Urho3D/Core/StringUtils.h>
#include <Urho3D/Core/WorkQueue.h>
#include <Urho3D/Graphics/Graphics.h> // RegisterGraphicsLibrary
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h> // RegisterSceneLibrary
#ifdef URHO3D_PHYSICS
#include <Urho3D/Physics/PhysicsWorld.h> // RegisterPhysicsLibrary
#endif

#include "AssetImporterLibrary.h"
#include "FbxImporter.h"

#include <Urho3D/DebugNew.h>

#include <cctype>
#include <exception>

using namespace Urho3D;

namespace
{

// Acquire Context for in-process import run.
// Reuse the host process context when available (e.g. the one created by the Editor,
// which already has all the required subsystems and libraries registered).
// This is required because rbfx Context is a process-wide singleton
// and a second instance would trip an assertion in the Context constructor.
// If no host context exists (standalone library usage), create a private one on first use.
Context* AcquireImportContext()
{
    if (Context* existing = Context::GetInstance())
        return existing;

    static const SharedPtr<Context> ownedContext = []
    {
        auto context = MakeShared<Context>();
        context->RegisterSubsystem(new FileSystem(context));
        context->RegisterSubsystem(new ResourceCache(context));
        context->RegisterSubsystem(new WorkQueue(context));
        RegisterSceneLibrary(context);
        RegisterGraphicsLibrary(context);
#ifdef URHO3D_PHYSICS
        RegisterPhysicsLibrary(context);
#endif
        return context;
    }();
    return ownedContext.Get();
}

}

// Tool-wide configuration. These live in the global namespace (the file only does
// "using namespace Urho3D"). Several of them are read by FbxImporter.cpp through the
// extern declarations at the top of that file, so their names/types/defaults form an ABI
// contract between the two translation units and must be kept in sync.
Context* context_ = nullptr;
ea::string inputName_;
ea::string resourcePath_;
ea::string outPath_;
ea::string outName_;
bool useSubdirs_ = true;
bool localIDs_ = false;
bool saveBinary_ = false;
bool saveJson_ = false;
bool createZone_ = true;
bool noAnimations_ = false;
bool noHierarchy_ = false;
bool noMaterials_ = false;
bool noTextures_ = false;
bool noMaterialDiffuseColor_ = false;
bool noEmptyNodes_ = false;
bool saveMaterialList_ = false;
bool includeNonSkinningBones_ = false;
bool verboseLog_ = false;
bool emissiveAO_ = false;
bool noOverwriteMaterial_ = false;
bool noOverwriteTexture_ = false;
bool noOverwriteNewerTexture_ = false;
bool checkUniqueModel_ = true;
// Default matches Graphics::GetMaxBones() engine default, so models with up to
// 128 bones use global bone indices directly instead of per-geometry bone mappings
unsigned maxBones_ = 128;
ea::vector<ea::string> nonSkinningBoneIncludes_;
ea::vector<ea::string> nonSkinningBoneExcludes_;
// For subset animation import usage
float importStartTime_ = 0.0f;
float importEndTime_ = 0.0f;
// Bitmask of AssetImporterContentType detected by the last "import" run. Set by ImportFbx in
// FbxImporter.cpp (through the extern contract) and exposed via AssetImporterGetLastImportContent.
unsigned lastImportContentFlags_ = 0;

// The assimp-based general model importer has been removed. The tool now only converts FBX
// files, always through the ufbx backend implemented in FbxImporter.cpp, which reads the
// globals above and does all of the model/scene/animation building itself.
void Run(const ea::vector<ea::string>& arguments)
{
    // Reset per-run state of the tool globals. As a library the tool may be invoked multiple
    // times in the same process, so the globals cannot rely on process lifetime initialization.
    inputName_.clear();
    resourcePath_.clear();
    outPath_.clear();
    outName_.clear();
    useSubdirs_ = true;
    localIDs_ = false;
    saveBinary_ = false;
    saveJson_ = false;
    createZone_ = true;
    noAnimations_ = false;
    noHierarchy_ = false;
    noMaterials_ = false;
    noTextures_ = false;
    noMaterialDiffuseColor_ = false;
    noEmptyNodes_ = false;
    saveMaterialList_ = false;
    includeNonSkinningBones_ = false;
    verboseLog_ = false;
    emissiveAO_ = false;
    noOverwriteMaterial_ = false;
    noOverwriteTexture_ = false;
    noOverwriteNewerTexture_ = false;
    checkUniqueModel_ = true;
    maxBones_ = 128;
    nonSkinningBoneIncludes_.clear();
    nonSkinningBoneExcludes_.clear();
    importStartTime_ = 0.0f;
    importEndTime_ = 0.0f;
    lastImportContentFlags_ = 0;

    // Reuse the host process context (see AcquireImportContext),
    // or lazily create and initialize a private one
    context_ = AcquireImportContext();

    if (arguments.size() < 2)
    {
        ImporterErrorExit(
            "Usage: AssetImporter <command> <input file.fbx> <output file or satellite directory> [options]\n\n"
            "Commands:\n"
            "import      Auto-detect FBX content and output model and/or animation(s)\n"
            "scene       Output a scene\n"
            "node        Output a node and its children (prefab)\n"
            "dump        Dump scene node structure. No output file is generated\n"
            "\n"
            "Only FBX files are supported, imported through the ufbx backend.\n"
            "\n"
            "Options:\n"
            "-b          Save model/scene in binary format, default format is XML\n"
            "-j          Save model/scene in JSON format, default format is XML\n"
            "-i          Use local ID's for scene nodes\n"
            "-l          Output a material list file for models\n"
            "-na         Do not output animations\n"
            "-nm         Do not output materials\n"
            "-nt         Do not output material textures\n"
            "-nc         Do not use material diffuse color value, instead output white\n"
            "-nh         Do not save full node hierarchy (scene mode only)\n"
            "-ne         Do not save empty nodes (scene mode only)\n"
            "-nz         Do not create a zone and a directional light (scene mode only)\n"
            "-ns         Do not create subdirectories for resources\n"
            "-mb <x>     Maximum number of bones per submesh. Default 128\n"
            "-p <path>   Set path for scene resources. Default is output file path\n"
            "-r <name>   Use the named scene node as root node\n"
            "-s <filter> Include non-skinning bones in the model's skeleton. Can be given a\n"
            "            case-insensitive semicolon separated filter list. Bone is included\n"
            "            if its name contains any of the filters. Prefix filter with minus\n"
            "            sign to use as an exclude. For example -s \"Bip01;-Dummy;-Helper\"\n"
            "-v          Enable verbose logging\n"
            "-eao        Interpret material emissive texture as ambient occlusion\n"
            "-cm         Check and do not overwrite if material exists\n"
            "-ct         Check and do not overwrite if texture exists\n"
            "-ctn        Check and do not overwrite if texture has newer timestamp\n"
            "-am         Export all meshes even if identical (scene mode only)\n"
            "-split <start> <end> (animation model only)\n"
            "            Split animation, will only import from start frame to end frame\n"
        );
    }

    ea::string command = arguments[0].to_lower();
    ea::string rootNodeName;

    // Parse the options that configure the shared globals consumed by the ufbx importer.
    // Post-process and assimp-specific flags are gone together with the assimp backend.
    for (unsigned i = 2; i < arguments.size(); ++i)
    {
        if (arguments[i].length() > 1 && arguments[i][0] == '-')
        {
            ea::string argument = arguments[i].substr(1).to_lower();
            ea::string value = i + 1 < arguments.size() ? arguments[i + 1] : ea::string{};

            if (argument == "b")
                saveBinary_ = true;
            else if (argument == "j")
                saveJson_ = true;
            else if (argument == "i")
                localIDs_ = true;
            else if (argument == "l")
                saveMaterialList_ = true;
            else if (argument.length() == 2 && argument[0] == 'n')
            {
                switch (tolower(argument[1]))
                {
                case 'a':
                    noAnimations_ = true;
                    break;

                case 'c':
                    noMaterialDiffuseColor_ = true;
                    break;

                case 'm':
                    noMaterials_ = true;
                    break;

                case 'h':
                    noHierarchy_ = true;
                    break;

                case 'e':
                    noEmptyNodes_ = true;
                    break;

                case 's':
                    useSubdirs_ = false;
                    break;

                case 't':
                    noTextures_ = true;
                    break;

                case 'z':
                    createZone_ = false;
                    break;
                }
            }
            else if (argument == "mb" && !value.empty())
            {
                maxBones_ = ToUInt(value);
                if (maxBones_ < 1)
                    maxBones_ = 1;
                ++i;
            }
            else if (argument == "p" && !value.empty())
            {
                resourcePath_ = AddTrailingSlash(value);
                ++i;
            }
            else if (argument == "r" && !value.empty())
            {
                rootNodeName = value;
                ++i;
            }
            else if (argument == "s")
            {
                includeNonSkinningBones_ = true;
                if (value.length() && (value[0] != '-' || value.length() > 3))
                {
                    ea::vector<ea::string> filters = value.split(';');
                    for (unsigned j = 0; j < filters.size(); ++j)
                    {
                        if (filters[j][0] == '-')
                            nonSkinningBoneExcludes_.push_back(filters[j].substr(1));
                        else
                            nonSkinningBoneIncludes_.push_back(filters[j]);
                    }
                }
            }
            else if (argument == "v")
                verboseLog_ = true;
            else if (argument == "eao")
                emissiveAO_ = true;
            else if (argument == "cm")
                noOverwriteMaterial_ = true;
            else if (argument == "ct")
                noOverwriteTexture_ = true;
            else if (argument == "ctn")
                noOverwriteNewerTexture_ = true;
            else if (argument == "am")
                checkUniqueModel_ = false;
            else if (argument == "split")
            {
                ea::string value2 = i + 2 < arguments.size() ? arguments[i + 2] : ea::string{};
                if (value.length() && value2.length() && (value[0] != '-') && (value2[0] != '-'))
                {
                    importStartTime_ = ToFloat(value);
                    importEndTime_ = ToFloat(value2);
                }
            }
        }
    }

    // "import" is the primary, content-driven command: the caller only supplies an output satellite
    // directory and the ufbx importer decides what to produce (meshes -> Models/, animation stacks ->
    // Animations/). There is no explicit "model"/"anim" command anymore, so the caller never has to
    // (mis)classify the file by name.
    if (command == "import")
    {
        if (arguments.size() < 3)
            ImporterErrorExit("import requires an input FBX and an output satellite directory");

        ea::string inFile = arguments[1];
        ea::string outDir = GetInternalPath(arguments[2]);

        inputName_ = GetFileName(inFile);
        outName_ = outDir;
        outPath_ = outDir;
        // resourcePath_ anchors Materials/ etc. directly at the satellite directory.
        resourcePath_ = AddTrailingSlash(outDir);

        if (outDir.empty())
            ImporterErrorExit("No output directory defined");

        PrintLine("Reading file " + inFile);
        if (!inFile.ends_with(".fbx", false))
            ImporterErrorExit("AssetImporter only supports FBX files, got: " + inFile);

        ImportFbx(inFile, outDir, command, rootNodeName);
        return;
    }

    if (command == "scene" || command == "node" || command == "dump")
    {
        ea::string inFile = arguments[1];
        ea::string outFile;
        if (arguments.size() > 2 && arguments[2][0] != '-')
            outFile = GetInternalPath(arguments[2]);

        inputName_ = GetFileName(inFile);
        outName_ = outFile;
        outPath_ = GetPath(outFile);

        if (resourcePath_.empty())
        {
            resourcePath_ = outPath_;
            if (resourcePath_.empty())
                resourcePath_ = "./";
        }

        resourcePath_ = AddTrailingSlash(resourcePath_);

        if (command != "dump" && outFile.empty())
            ImporterErrorExit("No output file defined");

        PrintLine("Reading file " + inFile);

        // ufbx is the only backend left and it handles scene/node/dump itself.
        if (!inFile.ends_with(".fbx", false))
            ImporterErrorExit("AssetImporter only supports FBX files, got: " + inFile);

        ImportFbx(inFile, outFile, command, rootNodeName);
        return;
    }

    ImporterErrorExit("Unrecognized command " + command);
}

// ---------------------------------------------------------------------------
// In-process library API
// ---------------------------------------------------------------------------

namespace
{

// Exception used to unwind out of import routines without terminating the host process
struct ImporterExitException
{
    int exitCode_{1};
    ea::string message_;
};

ea::string lastErrorMessage;

}

[[noreturn]] void ImporterErrorExit(const ea::string& message, int exitCode)
{
    PrintLine(message);
    throw ImporterExitException{exitCode, message};
}

extern "C" ASSETIMPORTER_API int AssetImporterRun(int numArguments, const char** arguments)
{
    lastErrorMessage.clear();

    ea::vector<ea::string> args;
    args.reserve(numArguments);
    for (int i = 0; i < numArguments; ++i)
        args.emplace_back(arguments[i]);

    try
    {
        Run(args);
        return 0;
    }
    catch (const ImporterExitException& e)
    {
        lastErrorMessage = e.message_;
        return e.exitCode_ != 0 ? e.exitCode_ : 1;
    }
    catch (const std::exception& e)
    {
        lastErrorMessage = e.what();
        return 1;
    }
    catch (...)
    {
        lastErrorMessage = "Unknown error";
        return 1;
    }
}

extern "C" ASSETIMPORTER_API const char* AssetImporterGetLastError()
{
    return lastErrorMessage.empty() ? "Unknown error" : lastErrorMessage.c_str();
}

extern "C" ASSETIMPORTER_API unsigned AssetImporterGetLastImportContent()
{
    return lastImportContentFlags_;
}

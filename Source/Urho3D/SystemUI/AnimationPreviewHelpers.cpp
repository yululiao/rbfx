//
// Copyright (c) 2022-2022 the rbfx project.
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
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//

#include <Urho3D/SystemUI/AnimationPreviewHelpers.h>

#include <Urho3D/Core/Context.h>
#include <Urho3D/Graphics/Animation.h>
#include <Urho3D/Graphics/Material.h>
#include <Urho3D/Graphics/Model.h>
#include <Urho3D/Graphics/Skeleton.h>
#include <Urho3D/Graphics/StaticModel.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/ScanFlags.h>
#include <Urho3D/Resource/ResourceCache.h>

#include <EASTL/algorithm.h>
#include <EASTL/set.h>
#include <EASTL/unordered_set.h>

namespace Urho3D
{

namespace
{

/// Minimum fraction of animation tracks that must have matching bones in the model skeleton.
constexpr float MinSkeletonMatch = 0.5f;

/// Maximum number of models loaded when searching for the compatible one.
constexpr unsigned MaxModelCandidates = 64;

/// Resource directories that may contain the model matching the animation.
/// Imported animation name looks like "Models/Hero.fbx.d/Animations/Hero_Walk.ani".
ea::vector<ea::string> GetModelSearchPaths(const Animation& animation)
{
    const ea::string animationDir = GetParentPath(animation.GetName());
    const ea::string satelliteDir = GetParentPath(animationDir);
    const ea::string parentDir = GetParentPath(satelliteDir);

    return {
        animationDir,             // Model imported from the same fbx as the animation
        satelliteDir + "Models",  // Model imported into the same satellite directory
        parentDir,                // Model imported from neighboring fbx or placed nearby
    };
}

/// Number of leading characters shared by both names. Used to check most similar names first.
unsigned GetCommonPrefixLength(const ea::string& lhs, const ea::string& rhs)
{
    const unsigned maxLength = static_cast<unsigned>(ea::min(lhs.length(), rhs.length()));
    unsigned length = 0;
    while (length < maxLength && lhs[length] == rhs[length])
        ++length;
    return length;
}

} // namespace

float EvaluateSkeletonMatch(const Animation& animation, const Model& model)
{
    const auto& tracks = animation.GetTracks();
    if (tracks.empty())
        return 0.0f;

    ea::unordered_set<StringHash> boneNameHashes;
    for (const Bone& bone : model.GetSkeleton().GetBones())
        boneNameHashes.insert(bone.nameHash_);

    const unsigned numMatchedTracks = static_cast<unsigned>(ea::count_if(tracks.begin(), tracks.end(),
        [&](const auto& track) { return boneNameHashes.contains(track.first); }));
    return static_cast<float>(numMatchedTracks) / tracks.size();
}

bool IsModelCompatibleWithAnimation(const Animation& animation, const Model& model)
{
    return EvaluateSkeletonMatch(animation, model) >= MinSkeletonMatch;
}

SharedPtr<Model> FindModelForAnimation(Context* context, Animation* animation)
{
    if (!context || !animation)
        return nullptr;

    auto* cache = context->GetSubsystem<ResourceCache>();

    // Collect unique model resource names from all the search paths
    ea::vector<ea::string> modelNames;
    ea::set<ea::string> uniqueNames;
    for (const ea::string& searchPath : GetModelSearchPaths(*animation))
    {
        ea::vector<ea::string> foundNames;
        cache->Scan(foundNames, searchPath, "*.mdl", SCAN_FILES | SCAN_RECURSIVE);
        for (const ea::string& foundName : foundNames)
        {
            const ea::string modelName = searchPath.empty() ? foundName : searchPath + "/" + foundName;
            if (uniqueNames.insert(modelName).second)
                modelNames.push_back(modelName);
        }
    }

    // Check most similar names first to avoid loading every model in the search paths
    const ea::string animationBaseName = GetFileName(animation->GetName());
    ea::sort(modelNames.begin(), modelNames.end(),
        [&](const ea::string& lhs, const ea::string& rhs)
        {
            const unsigned lhsScore = GetCommonPrefixLength(animationBaseName, GetFileName(lhs));
            const unsigned rhsScore = GetCommonPrefixLength(animationBaseName, GetFileName(rhs));
            if (lhsScore != rhsScore)
                return lhsScore > rhsScore;
            return lhs.length() < rhs.length();
        });

    const unsigned numCandidates = ea::min<unsigned>(modelNames.size(), MaxModelCandidates);
    for (unsigned i = 0; i < numCandidates; ++i)
    {
        Model* model = cache->GetResource<Model>(modelNames[i]);
        if (model && IsModelCompatibleWithAnimation(*animation, *model))
            return SharedPtr<Model>{model};
    }
    return nullptr;
}

void ApplyDefaultMaterials(StaticModel* staticModel)
{
    if (!staticModel)
        return;

    staticModel->ApplyMaterialList();

    auto* cache = staticModel->GetContext()->GetSubsystem<ResourceCache>();
    Material* defaultMaterial = cache->GetResource<Material>("Materials/DefaultWhite.xml");
    if (!defaultMaterial)
        return;

    for (unsigned i = 0; i < staticModel->GetNumGeometries(); ++i)
    {
        if (!staticModel->GetMaterial(i))
            staticModel->SetMaterial(i, defaultMaterial);
    }
}

} // namespace Urho3D

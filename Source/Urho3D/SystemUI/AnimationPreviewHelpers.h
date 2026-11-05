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

#pragma once

#include "../Container/Ptr.h"

namespace Urho3D
{

class Animation;
class Context;
class Model;
class StaticModel;

/// Return fraction of animation tracks that have matching bones in the model skeleton.
URHO3D_API float EvaluateSkeletonMatch(const Animation& animation, const Model& model);

/// Return whether the model skeleton is compatible enough to preview the animation.
URHO3D_API bool IsModelCompatibleWithAnimation(const Animation& animation, const Model& model);

/// Find model resource compatible with the animation.
/// Searches in the animation directory, sibling "Models" directory and parent directory,
/// so both models imported from the same fbx and from separate fbx files are found.
URHO3D_API SharedPtr<Model> FindModelForAnimation(Context* context, Animation* animation);

/// Apply materials referenced by the model and fill missing slots with default white material.
URHO3D_API void ApplyDefaultMaterials(StaticModel* staticModel);

}

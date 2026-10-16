//
// Copyright (c) 2026 the rbfx project.
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

#pragma once

#include "../Container/Ptr.h"
#include "../Container/Str.h"
#include "../Core/Object.h"
#include "../Math/BoundingBox.h"
#include "../Math/Rect.h"
#include "../Math/Vector2.h"
#include "../Math/Vector3.h"

#include <EASTL/vector.h>

namespace Urho3D
{

class AnimatedModel;
class Animation;
class Camera;
class Context;
class Image;
class Material;
class Model;
class Node;
class Scene;
class Texture2D;
class Viewport;

/// One animation clip baked into the flipbook atlas: where its cells start and how they play.
struct BakedFlipbookClip
{
    /// Clip name (bare animation resource name it was baked from).
    ea::string name_;
    /// Global index of the first cell (frame 0, rotation 0) of this clip inside the atlas.
    unsigned startCell_{};
    /// Number of baked frames (time samples) of this clip.
    unsigned frameCount_{};
    /// Clip length in seconds.
    float duration_{};
};

/// Bake settings for a flipbook atlas.
struct FlipbookBakeParams
{
    /// Frames sampled per second of animation.
    float fps_{15.0f};
    /// Width and height in texels of one atlas cell (square).
    unsigned cellSize_{256};
    /// Number of facing directions baked around the Y axis for every frame (1 = single view).
    unsigned rotations_{1};
    /// Yaw in degrees applied to the model node around the Y axis before baking. Models whose
    /// front faces -Z (e.g. Quaternius FBX convention) need 180 so the view shows the face, not the back.
    float yaw_{180.0f};
    /// Hard upper bound on the atlas width in texels (the column count is clamped to respect it).
    unsigned maxAtlasWidth_{2048};
};

/// Offline baker that converts a skinned \p Model plus several \p Animation clips into a single
/// 2D sequence-frame ("flipbook") atlas by rendering each animation frame to an offscreen render
/// target and packing the results into one RGBA8 texture. It is the low-poly counterpart of
/// BoneMatrixBaker: instead of keeping triangles in the vertex path, the crowd is degraded to
/// camera-facing quads (BillboardSet) whose per-instance UV rectangle selects the atlas cell, so
/// the vertex throughput collapses together with the draw calls.
///
/// The bake is inherently frame-driven (each cell is one engine render of the offscreen viewport),
/// so Start() kicks off an asynchronous job that self-advances through the E_UPDATE / E_ENDRENDERING
/// events; a caller (the editor dialog) shows progress until IsRunning() goes false, at which point
/// the atlas archive (and a PNG preview) have been written to the output path.
///
/// The same object doubles as the runtime consumer: Load() rehydrates the atlas texture plus the
/// clip table, and GetClipCellUV() maps a (clip, frame, rotation) triple to the UV rectangle to
/// assign to a Billboard, keeping all UV-layout knowledge in C++.
class URHO3D_API FlipbookBaker : public Object
{
    URHO3D_OBJECT(FlipbookBaker, Object);

public:
    /// Construct.
    explicit FlipbookBaker(Context* context);
    /// Destruct.
    ~FlipbookBaker() override;
    /// Register object factory.
    /// @nobind
    static void RegisterObject(Context* context);

    // ---- Editor bake (asynchronous, self-driving across engine frames) ----

    /// Set where Start() writes the ".flipbook" archive and its PNG preview on completion.
    void SetOutputPath(const ea::string& path) { outputPath_ = path; }
    /// Optional material applied to the model during the bake (null = whatever the model carries).
    void SetBakeMaterial(Material* material) { bakeMaterial_ = material; }

    /// Validate inputs, build the temporary offscreen scene and start the frame-driven bake. Returns
    /// false and records the reason in GetLastError() on a validation failure (nothing scheduled).
    bool Start(Model* model, const ea::vector<Animation*>& animations, const FlipbookBakeParams& params);

    /// Whether a bake is currently scheduled and advancing.
    /// @property
    bool IsRunning() const { return running_; }
    /// Whether the last bake finished (successfully or not). Implies IsRunning() == false.
    /// @property
    bool IsFinished() const { return finished_; }
    /// Whether the last bake completed successfully and wrote its output.
    /// @property
    bool IsSucceeded() const { return succeeded_; }
    /// Bake completion in [0, 1].
    /// @property
    float GetProgress() const;

    // ---- Runtime (Load + query) ----

    /// Rehydrate an atlas previously written by Save(): fills the texture and the clip table so the
    /// runtime queries work without re-baking. Returns false and records the reason on failure.
    bool Load(const ea::string& path, ea::string& error);
    /// Convenience overload recording the failure reason in GetLastError().
    bool Load(const ea::string& path);

    /// Persist the atlas (RGBA8 texels + layout + per-clip metadata) to a binary ".flipbook" archive,
    /// plus a sidecar ".flipbook.png" for human inspection. Requires a completed successful bake.
    bool Save(const ea::string& path, ea::string& error) const;
    /// Convenience overload recording the failure reason in GetLastError().
    bool Save(const ea::string& path);

    /// The atlas texture (RGBA8, FILTER_NEAREST), valid after a successful bake or Load().
    /// @property
    Texture2D* GetTexture() const { return texture_; }

    /// Number of baked clips.
    /// @property
    unsigned GetNumClips() const { return static_cast<unsigned>(clips_.size()); }
    /// Baked clip name by index.
    ea::string GetClipName(unsigned clipIndex) const;
    /// Number of frames of a clip.
    unsigned GetClipFrameCount(unsigned clipIndex) const;
    /// Clip duration in seconds.
    float GetClipDuration(unsigned clipIndex) const;
    /// Frames sampled per second (shared across clips).
    /// @property
    float GetFps() const { return fps_; }
    /// Number of facing directions per frame.
    /// @property
    unsigned GetRotations() const { return rotations_; }
    /// Atlas width in texels.
    /// @property
    unsigned GetAtlasWidth() const { return atlasWidth_; }
    /// Atlas height in texels.
    /// @property
    unsigned GetAtlasHeight() const { return atlasHeight_; }

    /// UV rectangle (in 0..1 atlas space) of one cell. \p frameIndex is in [0, frameCount),
    /// \p rotIndex in [0, rotations_). Out-of-range indices are clamped. Assign the result straight
    /// to Billboard::uv_.
    Rect GetClipCellUV(unsigned clipIndex, unsigned frameIndex, unsigned rotIndex) const;

    /// Return the error message from the last Start/Save/Load call.
    const ea::string& GetLastError() const { return lastError_; }

    /// Tear down the temporary bake scene and clear all baked results.
    void Reset();

private:
    /// Configure the current cell into the offscreen scene (pose the skeleton, aim the camera, queue
    /// the render target update). Called from E_UPDATE while a bake is running.
    void PrepareCurrentCell();
    /// Read the just-rendered cell into the atlas buffer, advance the cursor, and when the last cell
    /// is done build the atlas texture + write the outputs. Called from E_ENDRENDERING.
    void ConsumeCurrentCell();
    /// Plan the clip table and cell layout from the requested fps/durations. Returns false on error.
    bool PlanAtlas(Model* model, const ea::vector<Animation*>& animations, const FlipbookBakeParams& params);
    /// Build the temporary scene (octree, transparent zone, lit model, orthographic camera, RT).
    bool BuildBakeScene(Model* model);
    /// Sweep every clip frame's posed skeleton and grow bakeCenter_/bakeRadius_ to the union of the
    /// per-bone skinned vertex boxes, so large-motion frames (death, overhead swings) fit their cell.
    void ComputeBakeFraming(Model* model);
    /// Write the clip pose at \p time into the bone nodes (bind pose for non-animated bones).
    void PoseSkeleton(unsigned clipIndex, float time);
    /// Dispose the temporary scene and render target.
    void DestroyBakeScene();
    /// Build the atlas texture from the CPU buffer, then Save() it and emit the completion log.
    void FinishBake();

    /// Resolve the (clip, frame, rotation) triple that global cell index \p cell maps to.
    void DecomposeCell(unsigned cell, unsigned& outClip, unsigned& outFrame, unsigned& outRot) const;
    /// Drop the previous bake's runtime state (scene, atlas buffers, progress) while keeping the
    /// externally configured inputs (bake material, output path) so Start() can begin from them.
    void ClearBakeState();

    // ---- Layout / metadata ----
    float fps_{15.0f};
    unsigned cellSize_{256};
    unsigned rotations_{1};
    /// Radians the model node is rotated around Y during the bake (from FlipbookBakeParams::yaw_).
    float yawRad_{0.0f};
    unsigned columns_{1};
    unsigned rows_{1};
    unsigned atlasWidth_{0};
    unsigned atlasHeight_{0};
    unsigned totalCells_{0};
    ea::vector<BakedFlipbookClip> clips_;

    // ---- CPU atlas pixels (RGBA8, atlasWidth_ * atlasHeight_ * 4), filled cell by cell during bake ----
    ea::vector<unsigned char> atlasTexels_;

    // ---- Runtime atlas texture ----
    SharedPtr<Texture2D> texture_;

    // ---- Bake job state ----
    bool running_{};
    bool finished_{};
    bool succeeded_{};
    unsigned currentCell_{};
    ea::string outputPath_;
    WeakPtr<Model> bakeModel_;
    SharedPtr<Material> bakeMaterial_;
    /// Animations kept alive for the duration of the bake (sampled per cell).
    ea::vector<SharedPtr<Animation>> animations_;

    // ---- Temporary offscreen scene ----
    SharedPtr<Scene> scene_;
    SharedPtr<Node> modelNode_;
    AnimatedModel* animatedModel_{};
    SharedPtr<Node> cameraNode_;
    Camera* camera_{};
    SharedPtr<Viewport> viewport_;
    SharedPtr<Texture2D> rtTexture_;
    SharedPtr<Texture2D> depthTexture_;
    /// Per-cell scratch image reused for the render-target read back.
    SharedPtr<Image> cellImage_;

    /// Bind-pose bounding box framing cache used to aim the orthographic camera during the bake.
    Vector3 bakeCenter_{};
    float bakeRadius_{};

    ea::string lastError_;
};

}

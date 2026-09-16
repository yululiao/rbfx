// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#pragma once

#include <Urho3D/Core/Object.h>
#include <Urho3D/Container/Ptr.h>

#include <EASTL/vector.h>

namespace Urho3D
{

class LuaGameScript;
class Scene;
class Project;
class Viewport;
class CustomBackbufferTexture;
class RenderSurface;
class Material;

/// Runs a Lua play session for the editor's current scene.
///
/// Owns the full Lua game lifecycle: entry detection (LuaGameScript component),
/// script execution, viewport collection, per-frame viewport sync to the game
/// render surface, and teardown (globals reset -> Lua state reinitialization).
/// Scene cleanup is handled by the editor via a Play-time scene snapshot.
///
/// UI tabs should only call Start/Update/Stop; all subsystem orchestration
/// (EngineLuaVM, Renderer viewports) stays inside this class.
class LuaGameRunner : public Object
{
    URHO3D_OBJECT(LuaGameRunner, Object);

public:
    explicit LuaGameRunner(Context* context);
    ~LuaGameRunner() override;

    /// Detect whether the scene has a Lua game entry (LuaGameScript component).
    /// Returns false if the EngineLuaVM subsystem is missing or scene is null.
    static bool IsLuaPlayMode(Scene* scene);

    /// Start the play session: expose the scene to Lua, execute the entry script,
    /// collect the viewport created by the script. Returns true if the session started.
    bool Start(Scene* scene, Project* project);

    /// Per-frame sync: apply the game viewport to the game render surface.
    void Update(CustomBackbufferTexture* backbuffer);

    /// Stop the session: reset Lua globals and reinitialize the Lua state.
    void Stop();

    /// Whether the session is active.
    bool IsActive() const { return !!scene_; }

private:
    /// Collect materials referenced by the scene (recursively, from all
    /// StaticModel-like components) so Stop() can reload them if the game
    /// script mutated their content (resources are not covered by the
    /// Play-time scene snapshot).
    void CollectReferencedMaterials(Scene* scene);

    /// Reload tracked materials from disk, dropping dirty runtime state.
    void ReloadTrackedMaterials();

    WeakPtr<Scene> scene_;
    SharedPtr<Viewport> viewport_;
    /// Materials referenced by the scene when Play started.
    ea::vector<SharedPtr<Material>> trackedMaterials_;
};

} // namespace Urho3D

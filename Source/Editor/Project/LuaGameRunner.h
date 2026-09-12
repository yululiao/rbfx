// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#pragma once

#include <Urho3D/Core/Object.h>
#include <Urho3D/Container/Ptr.h>

namespace Urho3D
{

class LuaGameScript;
class Scene;
class Project;
class Viewport;
class CustomBackbufferTexture;
class RenderSurface;

/// Runs a Lua play session for the editor's current scene.
///
/// Owns the full Lua game lifecycle: entry detection (LuaGameScript component),
/// script execution, viewport collection, per-frame viewport sync to the game
/// render surface, and ordered teardown (script cleanup -> globals reset ->
/// Lua state reinitialization).
///
/// UI tabs should only call Start/Update/Stop; all subsystem orchestration
/// (LuaScript, Renderer viewports) stays inside this class.
class LuaGameRunner : public Object
{
    URHO3D_OBJECT(LuaGameRunner, Object);

public:
    explicit LuaGameRunner(Context* context);
    ~LuaGameRunner() override;

    /// Detect whether the scene has a Lua game entry (LuaGameScript component).
    /// Returns false if LuaScript subsystem is missing or scene is null.
    static bool IsLuaPlayMode(Scene* scene);

    /// Start the play session: expose the scene to Lua, execute the entry script,
    /// collect the viewport created by the script. Returns true if the session started.
    bool Start(Scene* scene, Project* project);

    /// Per-frame sync: apply the game viewport to the game render surface.
    void Update(CustomBackbufferTexture* backbuffer);

    /// Stop the session: run script cleanup, reset Lua globals, reinitialize Lua state.
    void Stop();

    /// Whether the session is active.
    bool IsActive() const { return !!scene_; }

private:
    WeakPtr<Scene> scene_;
    SharedPtr<Viewport> viewport_;
};

} // namespace Urho3D

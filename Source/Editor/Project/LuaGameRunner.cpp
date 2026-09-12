// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#include "../Project/LuaGameRunner.h"

#include "../Project/LuaGameScript.h"
#include "../Project/Project.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/Graphics/Renderer.h>
#include <Urho3D/Graphics/RenderSurface.h>
#include <Urho3D/Graphics/Viewport.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/Utility/SceneRendererToTexture.h>
#include <LuaScript/LuaScript.h>

namespace Urho3D
{

LuaGameRunner::LuaGameRunner(Context* context)
    : Object(context)
{
}

LuaGameRunner::~LuaGameRunner()
{
    Stop();
}

bool LuaGameRunner::IsLuaPlayMode(Scene* scene)
{
    if (!scene)
        return false;
    auto* luaScript = scene->GetContext()->GetSubsystem<LuaScript>();
    if (!luaScript)
        return false;
    ea::vector<LuaGameScript*> components;
    scene->GetComponents<LuaGameScript>(components);
    return !components.empty();
}

bool LuaGameRunner::Start(Scene* scene, Project* project)
{
    if (!scene || !project)
        return false;

    auto* luaScript = context_->GetSubsystem<LuaScript>();
    if (!luaScript)
        return false;

    ea::vector<LuaGameScript*> components;
    scene->GetComponents<LuaGameScript>(components);
    if (components.empty())
        return false;

    // Expose the editor scene to the game script.
    luaScript->SetGlobalScene("scene", scene);

    // Execute the entry script referenced by the first LuaGameScript component.
    const ea::string scriptPath = project->GetProjectPath() + components[0]->GetScriptPath();
    auto* fs = context_->GetSubsystem<FileSystem>();
    if (fs && fs->FileExists(scriptPath))
    {
        URHO3D_LOGINFO("Executing Lua script: {}", scriptPath.c_str());
        if (!luaScript->ExecuteFileAbsolute(scriptPath))
            URHO3D_LOGERROR("Failed to execute Lua script: {}", scriptPath.c_str());
    }
    else
    {
        URHO3D_LOGWARNING("Lua script not found: {}", scriptPath.c_str());
    }

    // Collect the viewport the script registered on the main renderer.
    // Update() syncs it to the game render surface every frame.
    auto* renderer = context_->GetSubsystem<Renderer>();
    viewport_ = renderer ? renderer->GetViewport(0) : nullptr;

    scene_ = scene;
    return true;
}

void LuaGameRunner::Update(CustomBackbufferTexture* backbuffer)
{
    if (!scene_ || !viewport_ || !backbuffer)
        return;

    // Apply the game viewport to the backbuffer render surface.
    // The surface is auto-queued for rendering via SURFACE_UPDATEALWAYS
    // (set by backbuffer->SetActive(true)).
    if (RenderSurface* renderSurface = backbuffer->GetTexture()->GetRenderSurface())
        renderSurface->SetViewport(0, viewport_);
}

void LuaGameRunner::Stop()
{
    if (!scene_)
        return;

    if (auto* luaScript = context_->GetSubsystem<LuaScript>())
    {
        // Let the game script clean up its scene nodes first.
        luaScript->ExecuteString("if __cleanup then __cleanup() end", "=[cleanup]");

        // Reset globals and the Lua state for the next play session.
        luaScript->SetGlobalScene("scene", nullptr);
        luaScript->Reinitialize();
    }

    scene_ = nullptr;
    viewport_ = nullptr;
}

} // namespace Urho3D

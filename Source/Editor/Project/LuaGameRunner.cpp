// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#include "../Project/LuaGameRunner.h"

#include <LuaScript/LuaGameScript.h>
#include "../Project/Project.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/Graphics/Material.h>
#include <Urho3D/Graphics/Renderer.h>
#include <Urho3D/Graphics/RenderSurface.h>
#include <Urho3D/Graphics/StaticModel.h>
#include <Urho3D/Graphics/Viewport.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Resource/ResourceCache.h>
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

    // Track materials referenced by the scene so Stop() can reload them
    // if the game script mutates their content (resources are shared and
    // not covered by the Play-time scene snapshot).
    CollectReferencedMaterials(scene);

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
        // Reset globals and the Lua state for the next play session.
        // Scene cleanup is not needed here: the editor restores the scene
        // from its Play-time snapshot (see GameViewTab::PlayState).
        luaScript->SetGlobalScene("scene", nullptr);
        luaScript->Reinitialize();
    }

    // Reload materials touched during Play so dirty runtime state
    // (e.g. shader parameters set from Lua) does not leak into the editor.
    ReloadTrackedMaterials();

    scene_ = nullptr;
    viewport_ = nullptr;
    trackedMaterials_.clear();
}

void LuaGameRunner::CollectReferencedMaterials(Scene* scene)
{
    trackedMaterials_.clear();

    // FindComponents with the Derived flag walks the whole scene recursively
    // and matches subclasses, so AnimatedModel (derived from StaticModel) is
    // collected as well.
    ea::vector<StaticModel*> models;
    scene->FindComponents<StaticModel>(models, ComponentSearchFlag::SelfOrChildrenRecursiveDerived);
    for (StaticModel* model : models)
    {
        for (unsigned i = 0; i < model->GetNumGeometries(); ++i)
            if (Material* material = model->GetMaterial(i))
                trackedMaterials_.push_back(SharedPtr<Material>(material));
    }

    ea::sort(trackedMaterials_.begin(), trackedMaterials_.end());
    trackedMaterials_.erase(ea::unique(trackedMaterials_.begin(), trackedMaterials_.end()),
        trackedMaterials_.end());
}

void LuaGameRunner::ReloadTrackedMaterials()
{
    if (trackedMaterials_.empty())
        return;

    auto* cache = context_->GetSubsystem<ResourceCache>();
    if (!cache)
        return;

    for (const SharedPtr<Material>& material : trackedMaterials_)
    {
        if (!material || material->GetName().empty())
            continue;

        // Reload from disk: reloads in-place, replacing dirty runtime state
        // (shader parameters, cull mode, etc.) with the saved content.
        if (!cache->ReloadResource(material))
            URHO3D_LOGWARNING("Failed to reload material after Play: {}", material->GetName());
    }
}

} // namespace Urho3D

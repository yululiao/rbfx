//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#include "../Urho3D/Precompiled.h"

#include "LuaBindings.h"
#include "LuaScript.h"

#include "../Urho3D/Core/Context.h"
#include "../Urho3D/Graphics/AnimatedModel.h"
#include "../Urho3D/Graphics/Animation.h"
#include "../Urho3D/Graphics/AnimationController.h"
#include "../Urho3D/Graphics/BillboardSet.h"
#include "../Urho3D/Graphics/ParticleEffect.h"
#include "../Urho3D/Graphics/ParticleEmitter.h"
#include "../Urho3D/Graphics/Camera.h"
#include "../Urho3D/Graphics/DebugRenderer.h"
#include "../Urho3D/Graphics/DecalSet.h"
#include "../Urho3D/Graphics/Drawable.h"
#include "../Urho3D/Graphics/Graphics.h"
#include "../Urho3D/Graphics/Light.h"
#include "../Urho3D/Graphics/Material.h"
#include "../Urho3D/Graphics/Model.h"
#include "../Urho3D/Graphics/Octree.h"
#include "../Urho3D/Graphics/OctreeQuery.h"
#include "../Urho3D/Graphics/Renderer.h"
#include "../Urho3D/Graphics/RenderSurface.h"
#include "../Urho3D/Graphics/Skybox.h"
#include "../Urho3D/Graphics/StaticModel.h"
#include "../Urho3D/Graphics/StaticModelGroup.h"
#include "../Urho3D/Graphics/RibbonTrail.h"
#include "../Urho3D/Graphics/Technique.h"
#include "../Urho3D/Graphics/Terrain.h"
#include "../Urho3D/Graphics/VertexBuffer.h"
#include "../Urho3D/Graphics/IndexBuffer.h"
#include "../Urho3D/Graphics/Geometry.h"
#include "../Urho3D/UI/Text3D.h"
#include "../Urho3D/Graphics/Texture.h"
#include "../Urho3D/Graphics/Texture2D.h"
#include "../Urho3D/Graphics/Viewport.h"
#include "../Urho3D/Graphics/Zone.h"
#include "../Urho3D/Graphics/Geometry.h"
#include "../Urho3D/RenderAPI/RenderDevice.h"
#include "../Urho3D/Resource/Image.h"
#include "../Urho3D/Scene/Node.h"
#include "../Urho3D/Scene/Scene.h"
#include "../Urho3D/Scene/ValueAnimation.h"

#include <sol/sol.hpp>

namespace sol
{

template <> struct is_automagical<Urho3D::Graphics> : std::false_type {};
template <> struct is_automagical<Urho3D::Renderer> : std::false_type {};
template <> struct is_automagical<Urho3D::Camera> : std::false_type {};
template <> struct is_automagical<Urho3D::Light> : std::false_type {};
template <> struct is_automagical<Urho3D::Drawable> : std::false_type {};
template <> struct is_automagical<Urho3D::StaticModel> : std::false_type {};
template <> struct is_automagical<Urho3D::Skybox> : std::false_type {};
template <> struct is_automagical<Urho3D::Octree> : std::false_type {};
template <> struct is_automagical<Urho3D::Zone> : std::false_type {};
template <> struct is_automagical<Urho3D::Viewport> : std::false_type {};
template <> struct is_automagical<Urho3D::DebugRenderer> : std::false_type {};
template <> struct is_automagical<Urho3D::Image> : std::false_type {};
template <> struct is_automagical<Urho3D::Model> : std::false_type {};
template <> struct is_automagical<Urho3D::Material> : std::false_type {};
template <> struct is_automagical<Urho3D::Texture> : std::false_type {};
template <> struct is_automagical<Urho3D::Texture2D> : std::false_type {};
template <> struct is_automagical<Urho3D::Animation> : std::false_type {};
template <> struct is_automagical<Urho3D::AnimatedModel> : std::false_type {};
template <> struct is_automagical<Urho3D::Skeleton> : std::false_type {};
template <> struct is_automagical<Urho3D::Bone> : std::false_type {};
template <> struct is_automagical<Urho3D::AnimationController> : std::false_type {};
template <> struct is_automagical<Urho3D::BillboardSet> : std::false_type {};
template <> struct is_automagical<Urho3D::ParticleEmitter> : std::false_type {};
template <> struct is_automagical<Urho3D::ParticleEffect> : std::false_type {};
template <> struct is_automagical<Urho3D::Billboard> : std::false_type {};
template <> struct is_automagical<Urho3D::DecalSet> : std::false_type {};
template <> struct is_automagical<Urho3D::Terrain> : std::false_type {};
template <> struct is_automagical<Urho3D::StaticModelGroup> : std::false_type {};
template <> struct is_automagical<Urho3D::RibbonTrail> : std::false_type {};
template <> struct is_automagical<Urho3D::VertexBuffer> : std::false_type {};
template <> struct is_automagical<Urho3D::IndexBuffer> : std::false_type {};
template <> struct is_automagical<Urho3D::Geometry> : std::false_type {};
template <> struct is_automagical<Urho3D::Text3D> : std::false_type {};
template <> struct is_automagical<Urho3D::RenderSurface> : std::false_type {};
template <> struct is_automagical<Urho3D::Technique> : std::false_type {};
template <> struct is_automagical<Urho3D::VertexElement> : std::false_type {};
template <> struct is_automagical<Urho3D::RenderDevice> : std::false_type {};

} // namespace sol

namespace Urho3D
{

namespace
{

// WindowSettings <-> plain Lua table conversion (54_WindowSettingsDemo).
// Table layout: { mode, width, height, resizable, monitor, vSync,
// refreshRate, multiSample, sRGB }.
WindowSettings TableToWindowSettings(const sol::table& table, const WindowSettings& defaults)
{
    WindowSettings settings = defaults;
    settings.mode_ = static_cast<WindowMode>(table.get_or("mode", static_cast<int>(settings.mode_)));
    settings.size_.x_ = table.get_or("width", settings.size_.x_);
    settings.size_.y_ = table.get_or("height", settings.size_.y_);
    settings.resizable_ = table.get_or("resizable", settings.resizable_);
    settings.monitor_ = table.get_or("monitor", settings.monitor_);
    settings.vSync_ = table.get_or("vSync", settings.vSync_);
    settings.refreshRate_ = table.get_or("refreshRate", settings.refreshRate_);
    settings.multiSample_ = table.get_or("multiSample", settings.multiSample_);
    settings.sRGB_ = table.get_or("sRGB", settings.sRGB_);
    return settings;
}

sol::table WindowSettingsToTable(sol::state_view lua, const WindowSettings& settings)
{
    sol::table result = lua.create_table();
    result["mode"] = static_cast<int>(settings.mode_);
    result["width"] = settings.size_.x_;
    result["height"] = settings.size_.y_;
    result["resizable"] = settings.resizable_;
    result["monitor"] = settings.monitor_;
    result["vSync"] = settings.vSync_;
    result["refreshRate"] = settings.refreshRate_;
    result["multiSample"] = settings.multiSample_;
    result["sRGB"] = settings.sRGB_;
    return result;
}

}

void RegisterGraphicsBindings(sol::state& lua, Context* context)
{
    // Graphics subsystem: window & device queries.
    lua.new_usertype<Graphics>("Graphics",
        sol::no_constructor,
        sol::base_classes, sol::bases<Object>(),
        "GetWidth", &Graphics::GetWidth,
        "GetHeight", &Graphics::GetHeight,
        "SetWindowTitle", &Graphics::SetWindowTitle,
        "TakeScreenShot", [context](Graphics* graphics, sol::this_state s) -> sol::object {
            if (!graphics)
                return sol::lua_nil;
            SharedPtr<Image> image(new Image(context));
            if (!graphics->TakeScreenShot(*image))
                return sol::lua_nil;
            return sol::make_object(sol::state_view(s), image);
        },
        "ToggleFullscreen", &Graphics::ToggleFullscreen,
        // Multi-monitor setup (54_WindowSettingsDemo).
        "GetMonitorCount", &Graphics::GetMonitorCount,
        "SetDefaultWindowModes", [](Graphics* graphics, const sol::table& settings) -> bool {
            return graphics ? graphics->SetDefaultWindowModes(TableToWindowSettings(settings, WindowSettings{})) : false;
        },
        "windowTitle", sol::property(&Graphics::GetWindowTitle, &Graphics::SetWindowTitle)
    );
    RegisterLuaObjectWrapper<Graphics>();

    // RenderDevice: low-level window state queries (54_WindowSettingsDemo).
    lua.new_usertype<RenderDevice>("RenderDevice",
        sol::no_constructor,
        sol::base_classes, sol::bases<Object>(),
        "GetWindowSettings", [](RenderDevice* device, sol::this_state s) -> sol::table {
            return WindowSettingsToTable(sol::state_view(s),
                device ? device->GetWindowSettings() : WindowSettings{});
        },
        "GetSwapChainSize", [](RenderDevice* device) -> IntVector2 {
            return device ? device->GetSwapChainSize() : IntVector2::ZERO;
        },
        "GetDpiScale", [](RenderDevice* device) -> float {
            return device ? device->GetDpiScale() : 1.0f;
        },
        // Enumerate fullscreen resolutions of a monitor as an array of
        // tables { width, height, refreshRate }.
        "GetFullscreenModes", [](RenderDevice* device, int monitor, sol::this_state s) -> sol::table {
            sol::state_view lua(s);
            sol::table result = lua.create_table();
            unsigned index = 1;
            for (const FullscreenMode& mode : RenderDevice::GetFullscreenModes(monitor))
            {
                sol::table entry = lua.create_table();
                entry["width"] = mode.size_.x_;
                entry["height"] = mode.size_.y_;
                entry["refreshRate"] = mode.refreshRate_;
                result[index++] = entry;
            }
            return result;
        },
        // Index into the table produced by GetFullscreenModes that best
        // matches the given size and refresh rate.
        "GetClosestFullscreenModeIndex", [](RenderDevice* device, const sol::table& modes, const IntVector2& size,
            int refreshRate) -> unsigned {
            FullscreenModeVector parsed;
            for (unsigned i = 1; i <= modes.size(); ++i)
            {
                const sol::table entry = modes[i];
                FullscreenMode mode;
                mode.size_.x_ = entry.get_or("width", size.x_);
                mode.size_.y_ = entry.get_or("height", size.y_);
                mode.refreshRate_ = entry.get_or("refreshRate", refreshRate);
                parsed.push_back(mode);
            }
            return RenderDevice::GetClosestFullscreenModeIndex(parsed, FullscreenMode{size, refreshRate});
        }
    );
    RegisterLuaObjectWrapper<RenderDevice>();

    // Image resource: pixel data for screenshots and texture work.
    lua.new_usertype<Image>("Image",
        sol::no_constructor,
        // Full chain: sol3 type casts (e.g. to Object*) only check the
        // directly declared bases.
        sol::base_classes, sol::bases<Resource, Object>(),
        "GetWidth", &Image::GetWidth,
        "GetHeight", &Image::GetHeight,
        "GetComponents", &Image::GetComponents,
        "SetSize", [](Image* image, int width, int height, unsigned components) {
            return image ? image->SetSize(width, height, components) : false;
        },
        "SavePNG", [](const Image* image, const ea::string& fileName) {
            return image && image->SavePNG(fileName);
        },
        "SaveJPG", [](const Image* image, const ea::string& fileName, int quality) {
            return image && image->SaveJPG(fileName, quality);
        }
    );
    RegisterLuaObjectWrapper<Image>();

    // Renderer subsystem: viewport management.
    lua.new_usertype<Renderer>("Renderer",
        sol::no_constructor,
        sol::base_classes, sol::bases<Object>(),
        "SetViewport", &Renderer::SetViewport,
        "GetViewport", &Renderer::GetViewport,
        "GetNumViewports", &Renderer::GetNumViewports,
        "SetTextureAnisotropy", &Renderer::SetTextureAnisotropy,
        "SetTextureFilterMode", &Renderer::SetTextureFilterMode,
        "SetTextureQuality", &Renderer::SetTextureQuality,
        "DrawDebugGeometry", &Renderer::DrawDebugGeometry,
        "GetDefaultZone", &Renderer::GetDefaultZone
    );
    RegisterLuaObjectWrapper<Renderer>();

    // Camera component.
    lua.new_usertype<Camera>("Camera",
        sol::no_constructor,
        sol::base_classes, sol::bases<Component, Serializable, Object>(),
        "SetFarClip", &Camera::SetFarClip,
        "SetNearClip", &Camera::SetNearClip,
        "SetFov", &Camera::SetFov,
        "SetOrthographic", &Camera::SetOrthographic,
        "SetOrthoSize", sol::overload(
            static_cast<void (Camera::*)(float)>(&Camera::SetOrthoSize),
            static_cast<void (Camera::*)(const Vector2&)>(&Camera::SetOrthoSize)),
        "SetAspectRatio", &Camera::SetAspectRatio,
        "SetAutoAspectRatio", &Camera::SetAutoAspectRatio,
        "SetFillMode", &Camera::SetFillMode,
        "SetViewMask", &Camera::SetViewMask,
        "SetViewOverrideFlags", [](Camera* camera, unsigned flags) {
            if (camera)
                camera->SetViewOverrideFlags(ViewOverrideFlags{static_cast<ViewOverride>(flags)});
        },
        "GetFarClip", &Camera::GetFarClip,
        "GetNearClip", &Camera::GetNearClip,
        "GetFov", &Camera::GetFov,
        "IsOrthographic", &Camera::IsOrthographic,
        "GetScreenRay", &Camera::GetScreenRay,
        "GetScreenRayFromMouse", &Camera::GetScreenRayFromMouse,
        "WorldToScreenPoint", &Camera::WorldToScreenPoint,
        "ScreenToWorldPoint", &Camera::ScreenToWorldPoint,
        // Planar reflection & clipping (23_Water).
        "SetUseReflection", &Camera::SetUseReflection,
        "SetReflectionPlane", &Camera::SetReflectionPlane,
        "SetUseClipping", &Camera::SetUseClipping,
        "SetClipPlane", &Camera::SetClipPlane,
        "SetZoom", &Camera::SetZoom,
        "GetZoom", &Camera::GetZoom
    );
    RegisterLuaObjectWrapper<Camera>();

    // Drawable base: common rendering switches. Registered before the derived
    // components because sol3 requires base usertypes to exist first.
    lua.new_usertype<Drawable>("Drawable",
        sol::no_constructor,
        sol::base_classes, sol::bases<Component, Serializable, Object>(),
        "SetCastShadows", &Drawable::SetCastShadows,
        "SetOccluder", &Drawable::SetOccluder,
        "GetWorldBoundingBox", [](Drawable* drawable) { return drawable ? drawable->GetWorldBoundingBox() : BoundingBox(); },
        "SetViewMask", &Drawable::SetViewMask,
        "SetDrawDistance", &Drawable::SetDrawDistance,
        "SetShadowDistance", &Drawable::SetShadowDistance,
        "SetEnabled", &Component::SetEnabled
    );

    // Light component.
    lua.new_usertype<Light>("Light",
        sol::no_constructor,
        sol::base_classes, sol::bases<Drawable, Component, Serializable, Object>(),
        "SetLightType", &Light::SetLightType,
        "SetColor", &Light::SetColor,
        "SetBrightness", &Light::SetBrightness,
        "SetRange", &Light::SetRange,
        "SetFov", &Light::SetFov,
        "SetRadius", &Light::SetRadius,
        "SetLength", &Light::SetLength,
        "SetAspectRatio", &Light::SetAspectRatio,
        "SetSpecularIntensity", &Light::SetSpecularIntensity,
        "SetCastShadows", &Light::SetCastShadows,
        "SetShadowIntensity", &Light::SetShadowIntensity,
        "SetShadowBias", [](Light* light, float constantBias, float slopeScaledBias, sol::optional<float> normalOffset) {
            if (light)
                light->SetShadowBias(BiasParameters(constantBias, slopeScaledBias, normalOffset.value_or(0.0f)));
        },
        "SetShadowCascade", [](Light* light, float split1, float split2, float split3,
            sol::optional<float> split4, sol::optional<float> fadeStart,
            sol::optional<float> biasAutoAdjust) {
            // Mirror the C++ defaults: split4 = 0.0, fadeStart = 0.8,
            // biasAutoAdjust = 0.8. Samples pass 3 to 5 of the 6 values.
            if (light)
                light->SetShadowCascade(CascadeParameters(split1, split2, split3,
                    split4.value_or(0.0f), fadeStart.value_or(0.8f), biasAutoAdjust.value_or(0.8f)));
        },
        "SetShadowResolution", &Light::SetShadowResolution,
        "SetShadowFadeDistance", &Light::SetShadowFadeDistance,
        "SetShadowNearFarRatio", &Light::SetShadowNearFarRatio,
        "SetRampTexture", &Light::SetRampTexture,
        "GetLightType", &Light::GetLightType,
        "GetColor", &Light::GetColor,
        "GetBrightness", &Light::GetBrightness,
        "GetRange", &Light::GetRange
    );
    RegisterLuaObjectWrapper<Light>();

    // Texture resources. Sprite and material texture slots take the base
    // class; Texture2D exists for GetResource("Texture2D", ...) resolution.
    lua.new_usertype<Texture>("Texture",
        sol::no_constructor,
        sol::base_classes, sol::bases<Resource, Object>(),
        "GetWidth", &Texture::GetWidth,
        "GetHeight", &Texture::GetHeight,
        "SetFilterMode", [](Texture* texture, int mode) {
            if (texture)
                texture->SetFilterMode(static_cast<TextureFilterMode>(mode));
        },
        "GetRenderSurface", [](Texture* texture) -> RenderSurface* {
            return texture ? texture->GetRenderSurface() : nullptr;
        }
    );
    RegisterLuaObjectWrapper<Texture>();

    lua.new_usertype<Texture2D>("Texture2D",
        sol::call_constructor, sol::factories(
            [context]() { return SharedPtr<Texture2D>(new Texture2D(context)); }),
        sol::base_classes, sol::bases<Texture, Resource, Object>(),
        "SetSize", [](Texture2D* texture, int width, int height, unsigned format,
            sol::optional<unsigned> flags, sol::optional<int> multiSample) -> bool {
            if (!texture)
                return false;
            return texture->SetSize(width, height, static_cast<TextureFormat>(format),
                TextureFlags{static_cast<TextureFlag>(flags.value_or(0))}, multiSample.value_or(1));
        }
    );
    RegisterLuaObjectWrapper<Texture2D>();

    // RenderSurface: render target face of a texture; hosts RTT viewports.
    lua.new_usertype<RenderSurface>("RenderSurface",
        sol::no_constructor,
        "SetViewport", &RenderSurface::SetViewport,
        "GetViewport", &RenderSurface::GetViewport
    );

    // Technique: shader pipeline description resource.
    lua.new_usertype<Technique>("Technique",
        sol::no_constructor,
        sol::base_classes, sol::bases<Resource, Object>()
    );
    RegisterLuaObjectWrapper<Technique>();

    // StaticModel and the model/material resources.
    lua.new_usertype<Model>("Model",
        sol::call_constructor, sol::factories(
            [context]() { return SharedPtr<Model>(new Model(context)); }),
        sol::base_classes, sol::bases<Resource, Object>(),
        "SetNumGeometries", &Model::SetNumGeometries,
        "SetGeometry", &Model::SetGeometry,
        "SetBoundingBox", &Model::SetBoundingBox,
        "GetNumGeometries", &Model::GetNumGeometries,
        "GetGeometry", &Model::GetGeometry,
        "Clone", [](Model* model) -> SharedPtr<Model> {
            return model ? SharedPtr<Model>(model->Clone()) : nullptr;
        },
        // Explicit buffer registration so the model can be saved properly
        // (34_DynamicGeometry).
        "SetVertexBuffers", [](Model* model, const sol::table& buffers,
            const sol::table& morphRangeStarts, const sol::table& morphRangeCounts) {
            if (!model)
                return;
            ea::vector<SharedPtr<VertexBuffer>> vbs;
            ea::vector<unsigned> starts;
            ea::vector<unsigned> counts;
            for (int i = 1; i <= static_cast<int>(buffers.size()); ++i)
            {
                const sol::object obj = buffers[i];
                if (obj.is<VertexBuffer*>())
                    vbs.push_back(SharedPtr<VertexBuffer>(obj.as<VertexBuffer*>()));
            }
            for (int i = 1; i <= static_cast<int>(morphRangeStarts.size()); ++i)
                starts.push_back(morphRangeStarts[i].get_or<unsigned>(0));
            for (int i = 1; i <= static_cast<int>(morphRangeCounts.size()); ++i)
                counts.push_back(morphRangeCounts[i].get_or<unsigned>(0));
            model->SetVertexBuffers(vbs, starts, counts);
        },
        "SetIndexBuffers", [](Model* model, const sol::table& buffers) {
            if (!model)
                return;
            ea::vector<SharedPtr<IndexBuffer>> ibs;
            for (int i = 1; i <= static_cast<int>(buffers.size()); ++i)
            {
                const sol::object obj = buffers[i];
                if (obj.is<IndexBuffer*>())
                    ibs.push_back(SharedPtr<IndexBuffer>(obj.as<IndexBuffer*>()));
            }
            model->SetIndexBuffers(ibs);
        }
    );
    RegisterLuaObjectWrapper<Model>();

    lua.new_usertype<Material>("Material",
        sol::call_constructor, sol::factories(
            [context]() { return SharedPtr<Material>(new Material(context)); }),
        sol::base_classes, sol::bases<Resource, Object>(),
        "SetShaderParameter", [](Material* material, const char* name, sol::object value, sol::this_state s) {
            if (material)
                material->SetShaderParameter(name, LuaToVariant(sol::state_view(s), value));
        },
        "SetTechnique", [](Material* material, unsigned index, Technique* technique) {
            if (material)
                material->SetTechnique(index, technique);
        },
        "SetTexture", [](Material* material, const char* name, Texture* texture) {
            if (material)
                material->SetTexture(name, texture);
        },
        "SetDepthBias", [](Material* material, float constantBias, float slopeScaledBias, sol::optional<float> normalOffset) {
            if (material)
                material->SetDepthBias(BiasParameters(constantBias, slopeScaledBias, normalOffset.value_or(0.0f)));
        },
        "GetNumTechniques", &Material::GetNumTechniques,
        "Clone", [](Material* material) -> SharedPtr<Material> {
            return material ? SharedPtr<Material>(material->Clone()) : nullptr;
        },
        "SetCullMode", &Material::SetCullMode,
        "SetVertexShaderDefines", [](Material* material, const char* defines) {
            if (material)
                material->SetVertexShaderDefines(defines);
        },
        "SetPixelShaderDefines", [](Material* material, const char* defines) {
            if (material)
                material->SetPixelShaderDefines(defines);
        },
        "GetVertexShaderDefines", [](Material* material) -> const char* {
            static thread_local ea::string value;
            value = material ? material->GetVertexShaderDefines() : ea::string{};
            return value.c_str();
        },
        "GetPixelShaderDefines", [](Material* material) -> const char* {
            static thread_local ea::string value;
            value = material ? material->GetPixelShaderDefines() : ea::string{};
            return value.c_str();
        },
        "SetShaderParameterAnimation", [](Material* material, const char* name,
            ValueAnimation* animation, sol::optional<int> wrapMode, sol::optional<float> speed) {
            if (material)
                material->SetShaderParameterAnimation(name, animation,
                    static_cast<WrapMode>(wrapMode.value_or(WM_LOOP)), speed.value_or(1.0f));
        },
        "SetShaderParameterAnimationWrapMode", [](Material* material, const char* name, int wrapMode) {
            if (material)
                material->SetShaderParameterAnimationWrapMode(name, static_cast<WrapMode>(wrapMode));
        },
        "SetShaderParameterAnimationSpeed", &Material::SetShaderParameterAnimationSpeed,
        // Associate material with scene so shader parameter animation
        // respects scene time scale (31_MaterialAnimation).
        "SetScene", &Material::SetScene
    );
    RegisterLuaObjectWrapper<Material>();

    lua.new_usertype<StaticModel>("StaticModel",
        sol::no_constructor,
        sol::base_classes, sol::bases<Drawable, Component, Serializable, Object>(),
        "SetModel", &StaticModel::SetModel,
        "GetModel", &StaticModel::GetModel,
        "SetMaterial", sol::overload(
            static_cast<void (StaticModel::*)(Material*)>(&StaticModel::SetMaterial),
            static_cast<bool (StaticModel::*)(unsigned, Material*)>(&StaticModel::SetMaterial)),
        "GetMaterial", static_cast<Material* (StaticModel::*)() const>(&StaticModel::GetMaterial)
    );
    RegisterLuaObjectWrapper<StaticModel>();

    // StaticModelGroup: render one model many times, each instance node
    // supplying its transform (20_HugeObjectCount).
    lua.new_usertype<StaticModelGroup>("StaticModelGroup",
        sol::no_constructor,
        sol::base_classes, sol::bases<StaticModel, Drawable, Component, Serializable, Object>(),
        "AddInstanceNode", &StaticModelGroup::AddInstanceNode,
        "RemoveInstanceNode", &StaticModelGroup::RemoveInstanceNode,
        "GetNumInstanceNodes", &StaticModelGroup::GetNumInstanceNodes,
        "GetInstanceNode", &StaticModelGroup::GetInstanceNode
    );
    RegisterLuaObjectWrapper<StaticModelGroup>();

    // RibbonTrail: trail rendered behind a moving node (44_RibbonTrailDemo).
    lua.new_usertype<RibbonTrail>("RibbonTrail",
        sol::no_constructor,
        sol::base_classes, sol::bases<Drawable, Component, Serializable, Object>(),
        "SetWidth", &RibbonTrail::SetWidth,
        "SetStartColor", &RibbonTrail::SetStartColor,
        "SetEndColor", &RibbonTrail::SetEndColor,
        "SetTrailType", [](RibbonTrail* trail, int type) {
            if (trail)
                trail->SetTrailType(static_cast<TrailType>(type));
        },
        "SetLifetime", &RibbonTrail::SetLifetime,
        "SetEmitting", &RibbonTrail::SetEmitting,
        "IsEmitting", &RibbonTrail::IsEmitting,
        "SetTailColumn", &RibbonTrail::SetTailColumn,
        "SetMaterial", &RibbonTrail::SetMaterial,
        "SetUpdateInvisible", &RibbonTrail::SetUpdateInvisible,
        "GetTrailType", [](RibbonTrail* trail) {
            return trail ? static_cast<int>(trail->GetTrailType()) : 0;
        }
    );
    RegisterLuaObjectWrapper<RibbonTrail>();

    // GPU vertex/index buffers and geometry for runtime-built meshes
    // (34_DynamicGeometry).
    lua.new_usertype<VertexElement>("VertexElement",
        sol::call_constructor, sol::factories(
            []() { return VertexElement(); },
            [](int type, int semantic, sol::optional<unsigned char> index, sol::optional<unsigned> stepRate) {
                return VertexElement(static_cast<VertexElementType>(type),
                    static_cast<VertexElementSemantic>(semantic), index.value_or(0), stepRate.value_or(0));
            }),
        "type", sol::property(
            [](VertexElement* element) { return element ? static_cast<int>(element->type_) : 0; },
            [](VertexElement* element, int type) { if (element) element->type_ = static_cast<VertexElementType>(type); }),
        "semantic", sol::property(
            [](VertexElement* element) { return element ? static_cast<int>(element->semantic_) : 0; },
            [](VertexElement* element, int semantic) { if (element) element->semantic_ = static_cast<VertexElementSemantic>(semantic); }),
        "index", sol::property(
            [](VertexElement* element) { return element ? static_cast<int>(element->index_) : 0; },
            [](VertexElement* element, int index) { if (element) element->index_ = static_cast<unsigned char>(index); })
    );

    lua.new_usertype<VertexBuffer>("VertexBuffer",
        sol::call_constructor, sol::factories(
            [context]() { return SharedPtr<VertexBuffer>(new VertexBuffer(context)); }),
        "SetShadowed", &VertexBuffer::SetShadowed,
        "SetDebugName", &VertexBuffer::SetDebugName,
        "SetSize", sol::overload(
            [](VertexBuffer* buffer, unsigned vertexCount, const sol::table& elements,
                sol::optional<bool> dynamic) {
                if (!buffer)
                    return false;
                ea::vector<VertexElement> elems;
                for (int i = 1; i <= static_cast<int>(elements.size()); ++i)
                {
                    const sol::object obj = elements[i];
                    if (obj.is<VertexElement>())
                        elems.push_back(obj.as<VertexElement>());
                }
                return buffer->SetSize(vertexCount, elems, dynamic.value_or(false));
            },
            static_cast<bool (VertexBuffer::*)(unsigned, unsigned, bool)>(&VertexBuffer::SetSize)),
        "Update", [](VertexBuffer* buffer, const std::string& data) {
            if (buffer && !data.empty())
                buffer->Update(data.data(), static_cast<unsigned>(data.size()));
        },
        "GetVertexCount", &VertexBuffer::GetVertexCount,
        "GetVertexSize", static_cast<unsigned (VertexBuffer::*)() const>(&VertexBuffer::GetVertexSize),
        // Read original vertex positions from the shadowed buffer
        // (34_DynamicGeometry).
        "GetVertexPositions", [](VertexBuffer* buffer, sol::this_state s) -> sol::table {
            sol::state_view lua(s);
            sol::table result = lua.create_table();
            const auto* data = buffer ? buffer->GetShadowData() : nullptr;
            if (!data)
                return result;
            const unsigned vertexSize = buffer->GetVertexSize();
            const unsigned count = buffer->GetVertexCount();
            for (unsigned i = 0; i < count; ++i)
                result[i + 1] = *reinterpret_cast<const Vector3*>(data + i * vertexSize);
            return result;
        },
        // Rewrite vertex positions in-place, preserving other elements
        // (normals, UVs) that the Lua side never touches.
        "UpdateVertexPositions", [](VertexBuffer* buffer, const sol::table& positions) {
            if (!buffer)
                return;
            auto* data = static_cast<unsigned char*>(buffer->Map());
            if (!data)
                return;
            const unsigned vertexSize = buffer->GetVertexSize();
            const unsigned numVertices = buffer->GetVertexCount();
            const unsigned count = ea::min<unsigned>(static_cast<unsigned>(positions.size()), numVertices);
            for (unsigned i = 0; i < count; ++i)
            {
                const sol::object obj = positions[i + 1];
                if (obj.is<Vector3>())
                    *reinterpret_cast<Vector3*>(data + i * vertexSize) = obj.as<Vector3>();
            }
            buffer->Unmap();
        }
    );
    RegisterLuaObjectWrapper<VertexBuffer>();

    lua.new_usertype<IndexBuffer>("IndexBuffer",
        sol::call_constructor, sol::factories(
            [context]() { return SharedPtr<IndexBuffer>(new IndexBuffer(context)); }),
        "SetShadowed", &IndexBuffer::SetShadowed,
        "SetDebugName", &IndexBuffer::SetDebugName,
        "SetSize", &IndexBuffer::SetSize,
        "Update", [](IndexBuffer* buffer, const std::string& data) {
            if (buffer && !data.empty())
                buffer->Update(data.data(), static_cast<unsigned>(data.size()));
        },
        "GetIndexCount", &IndexBuffer::GetIndexCount,
        "GetIndexSize", &IndexBuffer::GetIndexSize
    );
    RegisterLuaObjectWrapper<IndexBuffer>();

    lua.new_usertype<Geometry>("Geometry",
        sol::call_constructor, sol::factories(
            [context]() { return SharedPtr<Geometry>(new Geometry(context)); }),
        "SetVertexBuffer", &Geometry::SetVertexBuffer,
        "SetIndexBuffer", &Geometry::SetIndexBuffer,
        "SetDrawRange", [](Geometry* geometry, int primitiveType, unsigned indexStart,
            unsigned indexCount, sol::optional<bool> getUsedVertexRange) {
            if (geometry)
                geometry->SetDrawRange(static_cast<PrimitiveType>(primitiveType), indexStart, indexCount,
                    getUsedVertexRange.value_or(true));
        },
        "SetLodDistance", &Geometry::SetLodDistance,
        "GetVertexBuffer", &Geometry::GetVertexBuffer
    );
    RegisterLuaObjectWrapper<Geometry>();

    // Text3D: text component in world space (32_Physics2DConstraints).
    lua.new_usertype<Text3D>("Text3D",
        sol::no_constructor,
        sol::base_classes, sol::bases<Drawable, Component, Serializable, Object>(),
        "SetText", [](Text3D* text, const char* value) {
            if (text)
                text->SetText(value);
        },
        "SetFont", sol::overload(
            [](Text3D* text, Font* font, float size) -> bool { return text && text->SetFont(font, size); },
            [](Text3D* text, const char* fontName, float size) -> bool { return text && text->SetFont(fontName, size); }),
        "SetAlignment", &Text3D::SetAlignment,
        "SetTextAlignment", &Text3D::SetTextAlignment,
        "SetColor", static_cast<void (Text3D::*)(const Color&)>(&Text3D::SetColor),
        "SetTextEffect", &Text3D::SetTextEffect,
        "SetEffectColor", &Text3D::SetEffectColor,
        "SetFontSize", &Text3D::SetFontSize
    );
    RegisterLuaObjectWrapper<Text3D>();

    // Skybox: default environment backdrop used by most samples.
    lua.new_usertype<Skybox>("Skybox",
        sol::no_constructor,
        sol::base_classes, sol::bases<StaticModel, Drawable, Component, Serializable, Object>()
    );
    RegisterLuaObjectWrapper<Skybox>();

    // Octree: mandatory scene component for visibility.
    lua.new_usertype<Octree>("Octree",
        sol::no_constructor,
        sol::base_classes, sol::bases<Component, Serializable, Object>(),
        // Raycast helper: triangle-accurate single query returning a table
        // { position, normal, distance, drawable } or nil on miss.
        "RaycastSingle", [](Octree* octree, const Ray& ray, float maxDistance,
            sol::optional<unsigned> drawableFlags, sol::this_state s) -> sol::object {
            if (!octree)
                return sol::lua_nil;
            ea::vector<RayQueryResult> results;
            RayOctreeQuery query(results, ray, RAY_TRIANGLE, maxDistance,
                DrawableFlags{static_cast<DrawableFlag>(drawableFlags.value_or(DRAWABLE_GEOMETRY))});
            octree->RaycastSingle(query);
            if (results.empty())
                return sol::lua_nil;
            sol::state_view lua(s);
            sol::table hit = lua.create_table();
            hit["position"] = results[0].position_;
            hit["normal"] = results[0].normal_;
            hit["distance"] = results[0].distance_;
            hit["drawable"] = WrapLuaObject(lua, results[0].drawable_);
            return hit;
        }
    );
    RegisterLuaObjectWrapper<Octree>();

    // Zone: ambient & fog settings.
    lua.new_usertype<Zone>("Zone",
        sol::no_constructor,
        sol::base_classes, sol::bases<Drawable, Component, Serializable, Object>(),
        "SetBoundingBox", &Zone::SetBoundingBox,
        "SetAmbientColor", &Zone::SetAmbientColor,
        "SetFogColor", &Zone::SetFogColor,
        "SetFogStart", &Zone::SetFogStart,
        "SetFogEnd", &Zone::SetFogEnd,
        "SetHeightFog", &Zone::SetHeightFog,
        "SetPriority", &Zone::SetPriority
    );
    RegisterLuaObjectWrapper<Zone>();

    // Animation resource: skeleton keyframe track data.
    lua.new_usertype<Animation>("Animation",
        sol::no_constructor,
        sol::base_classes, sol::bases<Resource, Object>(),
        "GetLength", &Animation::GetLength,
        "GetAnimationName", &Animation::GetAnimationName
    );
    RegisterLuaObjectWrapper<Animation>();

    // Bone: plain struct owned by a Skeleton. "animated" mirrors the public
    // Bone::animated_ flag used to disable keyframe animation per bone.
    lua.new_usertype<Bone>("Bone",
        sol::no_constructor,
        "name", [](const Bone* bone) { return bone ? bone->name_ : ea::string(); },
        "animated", &Bone::animated_,
        "node", [](Bone* bone) -> Node* { return bone ? bone->node_.Get() : nullptr; }
    );

    // Skeleton: bone collection of an AnimatedModel.
    lua.new_usertype<Skeleton>("Skeleton",
        sol::no_constructor,
        "GetNumBones", &Skeleton::GetNumBones,
        "GetBone", sol::overload(
            [](Skeleton* skeleton, unsigned index) -> Bone* {
                return skeleton ? skeleton->GetBone(index) : nullptr;
            },
            [](Skeleton* skeleton, const char* name) -> Bone* {
                return skeleton ? skeleton->GetBone(name) : nullptr;
            })
    );

    // AnimatedModel: skinned model. SetModel/SetMaterial/SetCastShadows are
    // inherited from StaticModel / Drawable.
    lua.new_usertype<AnimatedModel>("AnimatedModel",
        sol::no_constructor,
        sol::base_classes, sol::bases<StaticModel, Drawable, Component, Serializable, Object>(),
        "SetUpdateInvisible", &AnimatedModel::SetUpdateInvisible,
        "GetSkeleton", [](AnimatedModel* model) -> Skeleton* {
            return model ? &model->GetSkeleton() : nullptr;
        }
    );
    RegisterLuaObjectWrapper<AnimatedModel>();

    // AnimationController: the C++ fluent AnimationParameters API is
    // flattened into optional positional arguments for Lua.
    lua.new_usertype<AnimationController>("AnimationController",
        sol::no_constructor,
        sol::base_classes, sol::bases<Component, Serializable, Object>(),
        "PlayNew", [](AnimationController* controller, Animation* animation,
            sol::optional<bool> looped, sol::optional<float> time,
            sol::optional<float> speed, sol::optional<float> fadeInTime) {
            if (!controller)
                return 0u;
            AnimationParameters params{animation};
            if (looped.value_or(false))
                params.Looped();
            if (time)
                params.Time(*time);
            if (speed)
                params.Speed(*speed);
            return controller->PlayNew(params, fadeInTime.value_or(0.0f));
        },
        "PlayNewExclusive", [](AnimationController* controller, Animation* animation,
            sol::optional<bool> looped, sol::optional<float> time,
            sol::optional<float> speed, sol::optional<float> fadeInTime,
            sol::optional<bool> keepOnCompletion) {
            if (!controller)
                return 0u;
            AnimationParameters params{animation};
            if (looped.value_or(false))
                params.Looped();
            if (time)
                params.Time(*time);
            if (speed)
                params.Speed(*speed);
            if (keepOnCompletion.value_or(false))
                params.KeepOnCompletion();
            return controller->PlayNewExclusive(params, fadeInTime.value_or(0.0f));
        },
        // Resume or start an animation by name, fading out all others in
        // its layer. Used every physics step by 18_CharacterDemo.
        "PlayExistingExclusive", [](AnimationController* controller, Animation* animation,
            sol::optional<bool> looped, sol::optional<bool> keepOnCompletion,
            sol::optional<float> fadeInTime) {
            if (!controller)
                return 0u;
            AnimationParameters params{animation};
            if (looped.value_or(false))
                params.Looped();
            if (keepOnCompletion.value_or(false))
                params.KeepOnCompletion();
            return controller->PlayExistingExclusive(params, fadeInTime.value_or(0.0f));
        },
        // Adjust playback speed of a running animation, by name.
        "SetSpeed", [](AnimationController* controller, const char* name, float speed) {
            return controller && controller->SetSpeed(name, speed);
        },
        "Play", [](AnimationController* controller, const char* name,
            unsigned char layer, bool looped, sol::optional<float> fadeTime) {
            return controller && controller->Play(name, layer, looped, fadeTime.value_or(0.0f));
        },
        "PlayExclusive", [](AnimationController* controller, const char* name,
            unsigned char layer, bool looped, sol::optional<float> fadeTime) {
            return controller && controller->PlayExclusive(name, layer, looped, fadeTime.value_or(0.0f));
        },
        "Fade", [](AnimationController* controller, const char* name,
            float targetWeight, sol::optional<float> fadeTime) {
            return controller && controller->Fade(name, targetWeight, fadeTime.value_or(0.0f));
        },
        "Stop", [](AnimationController* controller, const char* name,
            sol::optional<float> fadeTime) {
            return controller && controller->Stop(name, fadeTime.value_or(0.0f));
        },
        "IsPlaying", sol::overload(
            [](AnimationController* controller, const char* name) {
                return controller && controller->IsPlaying(name);
            },
            [](AnimationController* controller, Animation* animation) {
                return controller && controller->IsPlaying(animation);
            }),
        // Current playback time of a named animation (44_RibbonTrailDemo
        // toggles emission at a fixed track time).
        "GetTime", [](AnimationController* controller, const char* name) -> float {
            return controller ? controller->GetTime(name) : 0.0f;
        }
    );
    RegisterLuaObjectWrapper<AnimationController>();

    // Terrain: heightmap terrain, optionally with collision through a
    // sibling CollisionShape:SetTerrain (19_VehicleDemo).
    lua.new_usertype<Terrain>("Terrain",
        sol::no_constructor,
        sol::base_classes, sol::bases<Component, Serializable, Object>(),
        "SetPatchSize", &Terrain::SetPatchSize,
        "SetSpacing", &Terrain::SetSpacing,
        "SetSmoothing", &Terrain::SetSmoothing,
        "SetHeightMap", [](Terrain* terrain, Image* image) -> bool {
            return terrain && terrain->SetHeightMap(image);
        },
        "SetMaterial", &Terrain::SetMaterial,
        "SetOccluder", &Terrain::SetOccluder,
        "GetHeight", &Terrain::GetHeight,
        "GetNormal", &Terrain::GetNormal
    );
    RegisterLuaObjectWrapper<Terrain>();

    // Billboard: plain data struct describing one quad in a BillboardSet.
    lua.new_usertype<Billboard>("Billboard",
        sol::no_constructor,
        "position", &Billboard::position_,
        "size", &Billboard::size_,
        "rotation", &Billboard::rotation_,
        "enabled", &Billboard::enabled_
    );

    // BillboardSet: particle-like quads facing the camera.
    lua.new_usertype<BillboardSet>("BillboardSet",
        sol::no_constructor,
        sol::base_classes, sol::bases<Drawable, Component, Serializable, Object>(),
        "SetNumBillboards", &BillboardSet::SetNumBillboards,
        "GetNumBillboards", &BillboardSet::GetNumBillboards,
        "SetMaterial", &BillboardSet::SetMaterial,
        "SetSorted", &BillboardSet::SetSorted,
        "GetBillboard", &BillboardSet::GetBillboard,
        "Commit", &BillboardSet::Commit
    );
    RegisterLuaObjectWrapper<BillboardSet>();

    // ParticleEffect: 3D particle configuration resource consumed by
    // ParticleEmitter:SetEffect.
    lua.new_usertype<ParticleEffect>("ParticleEffect",
        sol::no_constructor,
        sol::base_classes, sol::bases<Resource, Object>()
    );
    RegisterLuaObjectWrapper<ParticleEffect>();

    // ParticleEmitter: 3D particle effect emitter (46_RaycastVehicle dust).
    lua.new_usertype<ParticleEmitter>("ParticleEmitter",
        sol::no_constructor,
        sol::base_classes, sol::bases<BillboardSet, Drawable, Component, Serializable, Object>(),
        "SetEffect", &ParticleEmitter::SetEffect,
        "SetEmitting", &ParticleEmitter::SetEmitting,
        "IsEmitting", &ParticleEmitter::IsEmitting
    );
    RegisterLuaObjectWrapper<ParticleEmitter>();

    // DecalSet: paint decals onto drawable geometry (08_Decals).
    lua.new_usertype<DecalSet>("DecalSet",
        sol::no_constructor,
        sol::base_classes, sol::bases<Drawable, Component, Serializable, Object>(),
        "SetMaterial", &DecalSet::SetMaterial,
        "AddDecal", &DecalSet::AddDecal,
        "RemoveDecals", &DecalSet::RemoveDecals,
        "RemoveAllDecals", &DecalSet::RemoveAllDecals
    );
    RegisterLuaObjectWrapper<DecalSet>();

    // Viewport: render setup combining scene + camera.
    lua.new_usertype<Viewport>("Viewport",
        sol::no_constructor,
        sol::base_classes, sol::bases<Object>(),
        "SetScene", &Viewport::SetScene,
        "SetCamera", &Viewport::SetCamera,
        "SetRect", &Viewport::SetRect,
        "GetScene", &Viewport::GetScene,
        "GetCamera", &Viewport::GetCamera
    );

    // DebugRenderer: programmatic debug drawing.
    lua.new_usertype<DebugRenderer>("DebugRenderer",
        sol::no_constructor,
        sol::base_classes, sol::bases<Component, Serializable, Object>(),
        "AddLine", [](DebugRenderer* debug, const Vector3& start, const Vector3& end, const Color& color) {
            if (debug) debug->AddLine(start, end, color);
        },
        "AddBoundingBox", [](DebugRenderer* debug, const BoundingBox& box, const Color& color) {
            if (debug) debug->AddBoundingBox(box, color);
        },
        "AddSphere", [](DebugRenderer* debug, const Vector3& center, float radius, const Color& color) {
            if (debug) debug->AddSphere(Sphere(center, radius), color);
        },
        "AddNode", [](DebugRenderer* debug, Node* node, float scale) {
            if (debug) debug->AddNode(node, scale);
        }
    );
    RegisterLuaObjectWrapper<DebugRenderer>();

    // Global helper: create a viewport without registering it with the
    // renderer (for render-to-texture surfaces).
    lua.set_function("CreateViewport", [context](Scene* scene, Camera* camera,
        sol::optional<IntRect> rect, sol::this_state s) -> sol::object {
        if (!scene || !camera)
            return sol::lua_nil;
        SharedPtr<Viewport> viewport(rect
            ? SharedPtr<Viewport>(new Viewport(context, scene, camera, *rect))
            : SharedPtr<Viewport>(new Viewport(context, scene, camera)));
        return sol::make_object(sol::state_view(s), viewport);
    });

    // Global helper: create a viewport and register it with the renderer.
    // An optional IntRect limits the viewport to a screen sub-region.
    lua.set_function("SetViewport", [context](unsigned index, Scene* scene, Camera* camera,
        sol::optional<IntRect> rect, sol::this_state s) -> sol::object {
        auto* renderer = context->GetSubsystem<Renderer>();
        if (!renderer || !scene || !camera)
            return sol::lua_nil;
        SharedPtr<Viewport> viewport(rect
            ? SharedPtr<Viewport>(new Viewport(context, scene, camera, *rect))
            : SharedPtr<Viewport>(new Viewport(context, scene, camera)));
        renderer->SetViewport(index, viewport);
        return sol::make_object(sol::state_view(s), viewport);
    });

    // Light type constants.
    sol::table lightType = lua.create_named_table("LIGHT");
    lightType["POINT"] = LIGHT_POINT;
    lightType["SPOT"] = LIGHT_SPOT;
    lightType["DIRECTIONAL"] = LIGHT_DIRECTIONAL;

    // RibbonTrail trail types (44_RibbonTrailDemo).
    sol::table tt = lua.create_named_table("TT");
    tt["FACE_CAMERA"] = TT_FACE_CAMERA;
    tt["BONE"] = TT_BONE;

    // Window modes for Graphics:SetDefaultWindowModes
    // (54_WindowSettingsDemo).
    sol::table wmode = lua.create_named_table("WMODE");
    wmode["WINDOWED"] = static_cast<int>(WindowMode::Windowed);
    wmode["BORDERLESS"] = static_cast<int>(WindowMode::Borderless);
    wmode["FULLSCREEN"] = static_cast<int>(WindowMode::Fullscreen);

    // Vertex element datatypes and semantics for VertexElement()
    // (34_DynamicGeometry).
    sol::table vet = lua.create_named_table("VET");
    vet["INT"] = TYPE_INT;
    vet["FLOAT"] = TYPE_FLOAT;
    vet["VECTOR2"] = TYPE_VECTOR2;
    vet["VECTOR3"] = TYPE_VECTOR3;
    vet["VECTOR4"] = TYPE_VECTOR4;
    vet["UBYTE4"] = TYPE_UBYTE4;
    vet["UBYTE4_NORM"] = TYPE_UBYTE4_NORM;

    sol::table vsem = lua.create_named_table("VSEM");
    vsem["POSITION"] = SEM_POSITION;
    vsem["NORMAL"] = SEM_NORMAL;
    vsem["BINORMAL"] = SEM_BINORMAL;
    vsem["TANGENT"] = SEM_TANGENT;
    vsem["TEXCOORD"] = SEM_TEXCOORD;
    vsem["COLOR"] = SEM_COLOR;
    vsem["BLENDWEIGHTS"] = SEM_BLENDWEIGHTS;
    vsem["BLENDINDICES"] = SEM_BLENDINDICES;
    vsem["OBJECTINDEX"] = SEM_OBJECTINDEX;

    // View override flags for Camera:SetViewOverrideFlags.
    sol::table vo = lua.create_named_table("VO");
    vo["DISABLE_OCCLUSION"] = VO_DISABLE_OCCLUSION;
    vo["NO_SHADOWS"] = VO_DISABLE_SHADOWS;

    // Texture filter modes for Texture:SetFilterMode.
    sol::table filter = lua.create_named_table("FILTER");
    filter["NEAREST"] = FILTER_NEAREST;
    filter["BILINEAR"] = FILTER_BILINEAR;
    filter["TRILINEAR"] = FILTER_TRILINEAR;
    filter["ANISOTROPIC"] = FILTER_ANISOTROPIC;
    filter["DEFAULT"] = FILTER_DEFAULT;

    // Common texture formats for Texture2D:SetSize.
    sol::table texf = lua.create_named_table("TEXF");
    texf["RGBA8_UNORM"] = static_cast<int>(TextureFormat::TEX_FORMAT_RGBA8_UNORM);
    texf["RGBA8_UNORM_SRGB"] = static_cast<int>(TextureFormat::TEX_FORMAT_RGBA8_UNORM_SRGB);
    texf["BGRA8_UNORM"] = static_cast<int>(TextureFormat::TEX_FORMAT_BGRA8_UNORM);
    texf["R8_UNORM"] = static_cast<int>(TextureFormat::TEX_FORMAT_R8_UNORM);
    texf["RG16_UNORM"] = static_cast<int>(TextureFormat::TEX_FORMAT_RG16_UNORM);
    texf["RGBA16_FLOAT"] = static_cast<int>(TextureFormat::TEX_FORMAT_RGBA16_FLOAT);
    texf["R32_FLOAT"] = static_cast<int>(TextureFormat::TEX_FORMAT_R32_FLOAT);
    texf["RGBA32_FLOAT"] = static_cast<int>(TextureFormat::TEX_FORMAT_RGBA32_FLOAT);
    texf["D32"] = static_cast<int>(TextureFormat::TEX_FORMAT_D32_FLOAT);
    texf["D24S8"] = static_cast<int>(TextureFormat::TEX_FORMAT_D24_UNORM_S8_UINT);
}

} // namespace Urho3D

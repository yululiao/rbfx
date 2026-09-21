//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#include "../Urho3D/Precompiled.h"

#include "LuaBindings.h"
#include "LuaBindHelpers.h"
#include "LuaBindMacros.h"

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
    {
        using RBFX_THIS = Graphics;
        RBFX_USERTYPE(Graphics,
            sol::no_constructor
            RBFX_BASES(Object)
            RBFX_M(GetWidth)
            RBFX_M(GetHeight)
            RBFX_M(SetWindowTitle)
            RBFX_RAW(TakeScreenShot, [context](Graphics* graphics, sol::this_state s) -> sol::object {
                if (!graphics)
                    return sol::lua_nil;
                SharedPtr<Image> image(new Image(context));
                if (!graphics->TakeScreenShot(*image))
                    return sol::lua_nil;
                return sol::make_object(sol::state_view(s), image);
            })
            RBFX_M(ToggleFullscreen)
            // Multi-monitor setup (54_WindowSettingsDemo).
            RBFX_M(GetMonitorCount)
            RBFX_RAW(SetDefaultWindowModes, [](Graphics* graphics, const sol::table& settings) -> bool {
                return graphics ? graphics->SetDefaultWindowModes(TableToWindowSettings(settings, WindowSettings{})) : false;
            })
            RBFX_RAW(windowTitle, sol::property(&Graphics::GetWindowTitle, &Graphics::SetWindowTitle))
        );
    }
    RegisterLuaObjectWrapper<Graphics>();

    // RenderDevice: low-level window state queries (54_WindowSettingsDemo).
    {
        using RBFX_THIS = RenderDevice;
        RBFX_USERTYPE(RenderDevice,
            sol::no_constructor
            RBFX_BASES(Object)
            RBFX_RAW(GetWindowSettings, [](RenderDevice* device, sol::this_state s) -> sol::table {
                return WindowSettingsToTable(sol::state_view(s),
                    device ? device->GetWindowSettings() : WindowSettings{});
            })
            RBFX_RAW(GetSwapChainSize, [](RenderDevice* device) -> IntVector2 {
                return device ? device->GetSwapChainSize() : IntVector2::ZERO;
            })
            RBFX_RAW(GetDpiScale, [](RenderDevice* device) -> float {
                return device ? device->GetDpiScale() : 1.0f;
            })
            // Enumerate fullscreen resolutions of a monitor as an array of
            // tables { width, height, refreshRate }.
            RBFX_RAW(GetFullscreenModes, [](RenderDevice* device, int monitor, sol::this_state s) -> sol::table {
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
            })
            // Index into the table produced by GetFullscreenModes that best
            // matches the given size and refresh rate.
            RBFX_RAW(GetClosestFullscreenModeIndex, [](RenderDevice* device, const sol::table& modes, const IntVector2& size,
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
            })
        );
    }
    RegisterLuaObjectWrapper<RenderDevice>();

    // Image resource: pixel data for screenshots and texture work.
    {
        using RBFX_THIS = Image;
        RBFX_USERTYPE(Image,
            sol::no_constructor
            RBFX_BASES(Resource, Object)
            RBFX_M(GetWidth)
            RBFX_M(GetHeight)
            RBFX_M(GetComponents)
            RBFX_RAW(SetSize, [](Image* image, int width, int height, unsigned components) {
                return image ? image->SetSize(width, height, components) : false;
            })
            RBFX_RAW(SavePNG, [](const Image* image, const ea::string& fileName) {
                return image && image->SavePNG(fileName);
            })
            RBFX_RAW(SaveJPG, [](const Image* image, const ea::string& fileName, int quality) {
                return image && image->SaveJPG(fileName, quality);
            })
        );
    }
    RegisterLuaObjectWrapper<Image>();

    // Renderer subsystem: viewport management.
    {
        using RBFX_THIS = Renderer;
        RBFX_USERTYPE(Renderer,
            sol::no_constructor
            RBFX_BASES(Object)
            RBFX_M(SetViewport)
            RBFX_M(GetViewport)
            RBFX_M(GetNumViewports)
            RBFX_M(SetTextureAnisotropy)
            RBFX_M(SetTextureFilterMode)
            RBFX_M(SetTextureQuality)
            RBFX_M(DrawDebugGeometry)
            RBFX_M(GetDefaultZone)
        );
    }
    RegisterLuaObjectWrapper<Renderer>();

    // Camera component.
    {
        using RBFX_THIS = Camera;
        RBFX_USERTYPE(Camera,
            sol::no_constructor
            RBFX_BASES(Component, Serializable, Object)
            RBFX_M(SetFarClip)
            RBFX_M(SetNearClip)
            RBFX_M(SetFov)
            RBFX_M(SetOrthographic)
            RBFX_OVERLOAD(SetOrthoSize,
                RBFX_CAST(SetOrthoSize, void, float),
                RBFX_CAST(SetOrthoSize, void, const Vector2&))
            RBFX_M(SetAspectRatio)
            RBFX_M(SetAutoAspectRatio)
            RBFX_M(SetFillMode)
            RBFX_M(SetViewMask)
            RBFX_RAW(SetViewOverrideFlags, [](Camera* camera, unsigned flags) {
                if (camera)
                    camera->SetViewOverrideFlags(ViewOverrideFlags{static_cast<ViewOverride>(flags)});
            })
            RBFX_M(GetFarClip)
            RBFX_M(GetNearClip)
            RBFX_M(GetFov)
            RBFX_M(IsOrthographic)
            RBFX_M(GetScreenRay)
            RBFX_M(GetScreenRayFromMouse)
            RBFX_M(WorldToScreenPoint)
            RBFX_M(ScreenToWorldPoint)
            // Planar reflection & clipping (23_Water).
            RBFX_M(SetUseReflection)
            RBFX_M(SetReflectionPlane)
            RBFX_M(SetUseClipping)
            RBFX_M(SetClipPlane)
            RBFX_M(SetZoom)
            RBFX_M(GetZoom)
        );
    }
    RegisterLuaObjectWrapper<Camera>();

    // Drawable base: common rendering switches. Registered before the derived
    // components because sol3 requires base usertypes to exist first.
    {
        using RBFX_THIS = Drawable;
        RBFX_USERTYPE(Drawable,
            sol::no_constructor
            RBFX_BASES(Component, Serializable, Object)
            RBFX_M(SetCastShadows)
            RBFX_M(SetOccluder)
            RBFX_RAW(GetWorldBoundingBox, [](Drawable* drawable) { return drawable ? drawable->GetWorldBoundingBox() : BoundingBox(); })
            RBFX_M(SetViewMask)
            RBFX_M(SetDrawDistance)
            RBFX_M(SetShadowDistance)
            RBFX_RAW(SetEnabled, &Component::SetEnabled)
        );
    }

    // Light component.
    {
        using RBFX_THIS = Light;
        RBFX_USERTYPE(Light,
            sol::no_constructor
            RBFX_BASES(Drawable, Component, Serializable, Object)
            RBFX_M(SetLightType)
            RBFX_M(SetColor)
            RBFX_M(SetBrightness)
            RBFX_M(SetRange)
            RBFX_M(SetFov)
            RBFX_M(SetRadius)
            RBFX_M(SetLength)
            RBFX_M(SetAspectRatio)
            RBFX_M(SetSpecularIntensity)
            RBFX_M(SetCastShadows)
            RBFX_M(SetShadowIntensity)
            RBFX_RAW(SetShadowBias, [](Light* light, float constantBias, float slopeScaledBias, sol::optional<float> normalOffset) {
                if (light)
                    light->SetShadowBias(BiasParameters(constantBias, slopeScaledBias, normalOffset.value_or(0.0f)));
            })
            RBFX_RAW(SetShadowCascade, [](Light* light, float split1, float split2, float split3,
                sol::optional<float> split4, sol::optional<float> fadeStart,
                sol::optional<float> biasAutoAdjust) {
                // Mirror the C++ defaults: split4 = 0.0, fadeStart = 0.8,
                // biasAutoAdjust = 0.8. Samples pass 3 to 5 of the 6 values.
                if (light)
                    light->SetShadowCascade(CascadeParameters(split1, split2, split3,
                        split4.value_or(0.0f), fadeStart.value_or(0.8f), biasAutoAdjust.value_or(0.8f)));
            })
            RBFX_M(SetShadowResolution)
            RBFX_M(SetShadowFadeDistance)
            RBFX_M(SetShadowNearFarRatio)
            RBFX_M(SetRampTexture)
            RBFX_M(GetLightType)
            RBFX_M(GetColor)
            RBFX_M(GetBrightness)
            RBFX_M(GetRange)
        );
    }
    RegisterLuaObjectWrapper<Light>();

    // Texture resources. Sprite and material texture slots take the base
    // class; Texture2D exists for GetResource("Texture2D", ...) resolution.
    {
        using RBFX_THIS = Texture;
        RBFX_USERTYPE(Texture,
            sol::no_constructor
            RBFX_BASES(Resource, Object)
            RBFX_M(GetWidth)
            RBFX_M(GetHeight)
            RBFX_RAW(SetFilterMode, [](Texture* texture, int mode) {
                if (texture)
                    texture->SetFilterMode(static_cast<TextureFilterMode>(mode));
            })
            RBFX_RAW(GetRenderSurface, [](Texture* texture) -> RenderSurface* {
                return texture ? texture->GetRenderSurface() : nullptr;
            })
        );
    }
    RegisterLuaObjectWrapper<Texture>();

    {
        using RBFX_THIS = Texture2D;
        RBFX_USERTYPE(Texture2D,
            sol::call_constructor, sol::factories(
                [context]() { return SharedPtr<Texture2D>(new Texture2D(context)); })
            RBFX_BASES(Texture, Resource, Object)
            RBFX_RAW(SetSize, [](Texture2D* texture, int width, int height, unsigned format,
                sol::optional<unsigned> flags, sol::optional<int> multiSample) -> bool {
                if (!texture)
                    return false;
                return texture->SetSize(width, height, static_cast<TextureFormat>(format),
                    TextureFlags{static_cast<TextureFlag>(flags.value_or(0))}, multiSample.value_or(1));
            })
        );
    }
    RegisterLuaObjectWrapper<Texture2D>();

    // RenderSurface: render target face of a texture; hosts RTT viewports.
    {
        using RBFX_THIS = RenderSurface;
        RBFX_USERTYPE(RenderSurface,
            sol::no_constructor
            RBFX_M(SetViewport)
            RBFX_M(GetViewport)
        );
    }

    // Technique: shader pipeline description resource.
    {
        using RBFX_THIS = Technique;
        RBFX_USERTYPE(Technique,
            sol::no_constructor
            RBFX_BASES(Resource, Object)
        );
    }
    RegisterLuaObjectWrapper<Technique>();

    // StaticModel and the model/material resources.
    {
        using RBFX_THIS = Model;
        RBFX_USERTYPE(Model,
            sol::call_constructor, sol::factories(
                [context]() { return SharedPtr<Model>(new Model(context)); })
            RBFX_BASES(Resource, Object)
            RBFX_M(SetNumGeometries)
            RBFX_M(SetGeometry)
            RBFX_M(SetBoundingBox)
            RBFX_M(GetNumGeometries)
            RBFX_M(GetGeometry)
            RBFX_RAW(Clone, [](Model* model) -> SharedPtr<Model> {
                return model ? SharedPtr<Model>(model->Clone()) : nullptr;
            })
            // Explicit buffer registration so the model can be saved properly
            // (34_DynamicGeometry).
            RBFX_RAW(SetVertexBuffers, [](Model* model, const sol::table& buffers,
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
            })
            RBFX_RAW(SetIndexBuffers, [](Model* model, const sol::table& buffers) {
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
            })
        );
    }
    RegisterLuaObjectWrapper<Model>();

    {
        using RBFX_THIS = Material;
        RBFX_USERTYPE(Material,
            sol::call_constructor, sol::factories(
                [context]() { return SharedPtr<Material>(new Material(context)); })
            RBFX_BASES(Resource, Object)
            RBFX_RAW(SetShaderParameter, [](Material* material, const char* name, sol::object value, sol::this_state s) {
                if (material)
                    material->SetShaderParameter(name, LuaToVariant(sol::state_view(s), value));
            })
            RBFX_RAW(SetTechnique, [](Material* material, unsigned index, Technique* technique) {
                if (material)
                    material->SetTechnique(index, technique);
            })
            RBFX_RAW(SetTexture, [](Material* material, const char* name, Texture* texture) {
                if (material)
                    material->SetTexture(name, texture);
            })
            RBFX_RAW(SetDepthBias, [](Material* material, float constantBias, float slopeScaledBias, sol::optional<float> normalOffset) {
                if (material)
                    material->SetDepthBias(BiasParameters(constantBias, slopeScaledBias, normalOffset.value_or(0.0f)));
            })
            RBFX_M(GetNumTechniques)
            RBFX_RAW(Clone, [](Material* material) -> SharedPtr<Material> {
                return material ? SharedPtr<Material>(material->Clone()) : nullptr;
            })
            RBFX_M(SetCullMode)
            RBFX_RAW(SetVertexShaderDefines, [](Material* material, const char* defines) {
                if (material)
                    material->SetVertexShaderDefines(defines);
            })
            RBFX_RAW(SetPixelShaderDefines, [](Material* material, const char* defines) {
                if (material)
                    material->SetPixelShaderDefines(defines);
            })
            RBFX_RAW(GetVertexShaderDefines, [](Material* material) -> const char* {
                static thread_local ea::string value;
                value = material ? material->GetVertexShaderDefines() : ea::string{};
                return value.c_str();
            })
            RBFX_RAW(GetPixelShaderDefines, [](Material* material) -> const char* {
                static thread_local ea::string value;
                value = material ? material->GetPixelShaderDefines() : ea::string{};
                return value.c_str();
            })
            RBFX_RAW(SetShaderParameterAnimation, [](Material* material, const char* name,
                ValueAnimation* animation, sol::optional<int> wrapMode, sol::optional<float> speed) {
                if (material)
                    material->SetShaderParameterAnimation(name, animation,
                        static_cast<WrapMode>(wrapMode.value_or(WM_LOOP)), speed.value_or(1.0f));
            })
            RBFX_RAW(SetShaderParameterAnimationWrapMode, [](Material* material, const char* name, int wrapMode) {
                if (material)
                    material->SetShaderParameterAnimationWrapMode(name, static_cast<WrapMode>(wrapMode));
            })
            RBFX_M(SetShaderParameterAnimationSpeed)
            // Associate material with scene so shader parameter animation
            // respects scene time scale (31_MaterialAnimation).
            RBFX_M(SetScene)
        );
    }
    RegisterLuaObjectWrapper<Material>();

    {
        using RBFX_THIS = StaticModel;
        RBFX_USERTYPE(StaticModel,
            sol::no_constructor
            RBFX_BASES(Drawable, Component, Serializable, Object)
            RBFX_M(SetModel)
            RBFX_M(GetModel)
            RBFX_OVERLOAD(SetMaterial,
                RBFX_CAST(SetMaterial, void, Material*),
                RBFX_CAST(SetMaterial, bool, unsigned, Material*))
            RBFX_RAW(GetMaterial, static_cast<Material* (StaticModel::*)() const>(&StaticModel::GetMaterial))
        );
    }
    RegisterLuaObjectWrapper<StaticModel>();

    // StaticModelGroup: render one model many times, each instance node
    // supplying its transform (20_HugeObjectCount).
    {
        using RBFX_THIS = StaticModelGroup;
        RBFX_USERTYPE(StaticModelGroup,
            sol::no_constructor
            RBFX_BASES(StaticModel, Drawable, Component, Serializable, Object)
            RBFX_M(AddInstanceNode)
            RBFX_M(RemoveInstanceNode)
            RBFX_M(GetNumInstanceNodes)
            RBFX_M(GetInstanceNode)
        );
    }
    RegisterLuaObjectWrapper<StaticModelGroup>();

    // RibbonTrail: trail rendered behind a moving node (44_RibbonTrailDemo).
    {
        using RBFX_THIS = RibbonTrail;
        RBFX_USERTYPE(RibbonTrail,
            sol::no_constructor
            RBFX_BASES(Drawable, Component, Serializable, Object)
            RBFX_M(SetWidth)
            RBFX_M(SetStartColor)
            RBFX_M(SetEndColor)
            RBFX_RAW(SetTrailType, [](RibbonTrail* trail, int type) {
                if (trail)
                    trail->SetTrailType(static_cast<TrailType>(type));
            })
            RBFX_M(SetLifetime)
            RBFX_M(SetEmitting)
            RBFX_M(IsEmitting)
            RBFX_M(SetTailColumn)
            RBFX_M(SetMaterial)
            RBFX_M(SetUpdateInvisible)
            RBFX_RAW(GetTrailType, [](RibbonTrail* trail) {
                return trail ? static_cast<int>(trail->GetTrailType()) : 0;
            })
        );
    }
    RegisterLuaObjectWrapper<RibbonTrail>();

    // GPU vertex/index buffers and geometry for runtime-built meshes
    // (34_DynamicGeometry).
    {
        using RBFX_THIS = VertexElement;
        RBFX_USERTYPE(VertexElement,
            sol::call_constructor, sol::factories(
                []() { return VertexElement(); },
                [](int type, int semantic, sol::optional<unsigned char> index, sol::optional<unsigned> stepRate) {
                    return VertexElement(static_cast<VertexElementType>(type),
                        static_cast<VertexElementSemantic>(semantic), index.value_or(0), stepRate.value_or(0));
                })
            RBFX_RAW(type, sol::property(
                [](VertexElement* element) { return element ? static_cast<int>(element->type_) : 0; },
                [](VertexElement* element, int type) { if (element) element->type_ = static_cast<VertexElementType>(type); }))
            RBFX_RAW(semantic, sol::property(
                [](VertexElement* element) { return element ? static_cast<int>(element->semantic_) : 0; },
                [](VertexElement* element, int semantic) { if (element) element->semantic_ = static_cast<VertexElementSemantic>(semantic); }))
            RBFX_RAW(index, sol::property(
                [](VertexElement* element) { return element ? static_cast<int>(element->index_) : 0; },
                [](VertexElement* element, int index) { if (element) element->index_ = static_cast<unsigned char>(index); }))
        );
    }

    {
        using RBFX_THIS = VertexBuffer;
        RBFX_USERTYPE(VertexBuffer,
            sol::call_constructor, sol::factories(
                [context]() { return SharedPtr<VertexBuffer>(new VertexBuffer(context)); })
            RBFX_M(SetShadowed)
            RBFX_M(SetDebugName)
            RBFX_OVERLOAD(SetSize,
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
                RBFX_CAST(SetSize, bool, unsigned, unsigned, bool))
            RBFX_RAW(Update, [](VertexBuffer* buffer, const std::string& data) {
                if (buffer && !data.empty())
                    buffer->Update(data.data(), static_cast<unsigned>(data.size()));
            })
            RBFX_M(GetVertexCount)
            RBFX_RAW(GetVertexSize, static_cast<unsigned (VertexBuffer::*)() const>(&VertexBuffer::GetVertexSize))
            // Read original vertex positions from the shadowed buffer
            // (34_DynamicGeometry).
            RBFX_RAW(GetVertexPositions, [](VertexBuffer* buffer, sol::this_state s) -> sol::table {
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
            })
            // Rewrite vertex positions in-place, preserving other elements
            // (normals, UVs) that the Lua side never touches.
            RBFX_RAW(UpdateVertexPositions, [](VertexBuffer* buffer, const sol::table& positions) {
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
            })
        );
    }
    RegisterLuaObjectWrapper<VertexBuffer>();

    {
        using RBFX_THIS = IndexBuffer;
        RBFX_USERTYPE(IndexBuffer,
            sol::call_constructor, sol::factories(
                [context]() { return SharedPtr<IndexBuffer>(new IndexBuffer(context)); })
            RBFX_M(SetShadowed)
            RBFX_M(SetDebugName)
            RBFX_M(SetSize)
            RBFX_RAW(Update, [](IndexBuffer* buffer, const std::string& data) {
                if (buffer && !data.empty())
                    buffer->Update(data.data(), static_cast<unsigned>(data.size()));
            })
            RBFX_M(GetIndexCount)
            RBFX_M(GetIndexSize)
        );
    }
    RegisterLuaObjectWrapper<IndexBuffer>();

    {
        using RBFX_THIS = Geometry;
        RBFX_USERTYPE(Geometry,
            sol::call_constructor, sol::factories(
                [context]() { return SharedPtr<Geometry>(new Geometry(context)); })
            RBFX_M(SetVertexBuffer)
            RBFX_M(SetIndexBuffer)
            RBFX_RAW(SetDrawRange, [](Geometry* geometry, int primitiveType, unsigned indexStart,
                unsigned indexCount, sol::optional<bool> getUsedVertexRange) {
                if (geometry)
                    geometry->SetDrawRange(static_cast<PrimitiveType>(primitiveType), indexStart, indexCount,
                        getUsedVertexRange.value_or(true));
            })
            RBFX_M(SetLodDistance)
            RBFX_M(GetVertexBuffer)
        );
    }
    RegisterLuaObjectWrapper<Geometry>();

    // Text3D: text component in world space (32_Physics2DConstraints).
    {
        using RBFX_THIS = Text3D;
        RBFX_USERTYPE(Text3D,
            sol::no_constructor
            RBFX_BASES(Drawable, Component, Serializable, Object)
            RBFX_RAW(SetText, [](Text3D* text, const char* value) {
                if (text)
                    text->SetText(value);
            })
            RBFX_OVERLOAD(SetFont,
                [](Text3D* text, Font* font, float size) -> bool { return text && text->SetFont(font, size); },
                [](Text3D* text, const char* fontName, float size) -> bool { return text && text->SetFont(fontName, size); })
            RBFX_M(SetAlignment)
            RBFX_M(SetTextAlignment)
            RBFX_RAW(SetColor, static_cast<void (Text3D::*)(const Color&)>(&Text3D::SetColor))
            RBFX_M(SetTextEffect)
            RBFX_M(SetEffectColor)
            RBFX_M(SetFontSize)
        );
    }
    RegisterLuaObjectWrapper<Text3D>();

    // Skybox: default environment backdrop used by most samples.
    {
        using RBFX_THIS = Skybox;
        RBFX_USERTYPE(Skybox,
            sol::no_constructor
            RBFX_BASES(StaticModel, Drawable, Component, Serializable, Object)
        );
    }
    RegisterLuaObjectWrapper<Skybox>();

    // Octree: mandatory scene component for visibility.
    {
        using RBFX_THIS = Octree;
        RBFX_USERTYPE(Octree,
            sol::no_constructor
            RBFX_BASES(Component, Serializable, Object)
            // Raycast helper: triangle-accurate single query returning a table
            // { position, normal, distance, drawable } or nil on miss.
            RBFX_RAW(RaycastSingle, [](Octree* octree, const Ray& ray, float maxDistance,
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
            })
        );
    }
    RegisterLuaObjectWrapper<Octree>();

    // Zone: ambient & fog settings.
    {
        using RBFX_THIS = Zone;
        RBFX_USERTYPE(Zone,
            sol::no_constructor
            RBFX_BASES(Drawable, Component, Serializable, Object)
            RBFX_M(SetBoundingBox)
            RBFX_M(SetAmbientColor)
            RBFX_M(SetFogColor)
            RBFX_M(SetFogStart)
            RBFX_M(SetFogEnd)
            RBFX_M(SetHeightFog)
            RBFX_M(SetPriority)
        );
    }
    RegisterLuaObjectWrapper<Zone>();

    // Animation resource: skeleton keyframe track data.
    {
        using RBFX_THIS = Animation;
        RBFX_USERTYPE(Animation,
            sol::no_constructor
            RBFX_BASES(Resource, Object)
            RBFX_M(GetLength)
            RBFX_M(GetAnimationName)
        );
    }
    RegisterLuaObjectWrapper<Animation>();

    // Bone: plain struct owned by a Skeleton. "animated" mirrors the public
    // Bone::animated_ flag used to disable keyframe animation per bone.
    {
        using RBFX_THIS = Bone;
        RBFX_USERTYPE(Bone,
            sol::no_constructor
            RBFX_RAW(name, [](const Bone* bone) { return bone ? bone->name_ : ea::string(); })
            RBFX_RAW(animated, &Bone::animated_)
            RBFX_RAW(node, [](Bone* bone, sol::this_state s) -> sol::object {
                return bone ? WrapLuaObjectAs<Node>(sol::state_view(s), bone->node_.Get()) : sol::lua_nil;
            })
        );
    }

    // Skeleton: bone collection of an AnimatedModel.
    {
        using RBFX_THIS = Skeleton;
        RBFX_USERTYPE(Skeleton,
            sol::no_constructor
            RBFX_M(GetNumBones)
            RBFX_OVERLOAD(GetBone,
                [](Skeleton* skeleton, unsigned index) -> Bone* {
                    return skeleton ? skeleton->GetBone(index) : nullptr;
                },
                [](Skeleton* skeleton, const char* name) -> Bone* {
                    return skeleton ? skeleton->GetBone(name) : nullptr;
                })
        );
    }

    // AnimatedModel: skinned model. SetModel/SetMaterial/SetCastShadows are
    // inherited from StaticModel / Drawable.
    {
        using RBFX_THIS = AnimatedModel;
        RBFX_USERTYPE(AnimatedModel,
            sol::no_constructor
            RBFX_BASES(StaticModel, Drawable, Component, Serializable, Object)
            RBFX_M(SetUpdateInvisible)
            RBFX_RAW(GetSkeleton, [](AnimatedModel* model) -> Skeleton* {
                return model ? &model->GetSkeleton() : nullptr;
            })
        );
    }
    RegisterLuaObjectWrapper<AnimatedModel>();

    // AnimationController: the C++ fluent AnimationParameters API is
    // flattened into optional positional arguments for Lua.
    {
        using RBFX_THIS = AnimationController;
        RBFX_USERTYPE(AnimationController,
            sol::no_constructor
            RBFX_BASES(Component, Serializable, Object)
            RBFX_RAW(PlayNew, [](AnimationController* controller, Animation* animation,
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
            })
            RBFX_RAW(PlayNewExclusive, [](AnimationController* controller, Animation* animation,
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
            })
            // Resume or start an animation by name, fading out all others in
            // its layer. Used every physics step by 18_CharacterDemo.
            RBFX_RAW(PlayExistingExclusive, [](AnimationController* controller, Animation* animation,
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
            })
            // Adjust playback speed of a running animation, by name.
            RBFX_RAW(SetSpeed, [](AnimationController* controller, const char* name, float speed) {
                return controller && controller->SetSpeed(name, speed);
            })
            RBFX_RAW(Play, [](AnimationController* controller, const char* name,
                unsigned char layer, bool looped, sol::optional<float> fadeTime) {
                return controller && controller->Play(name, layer, looped, fadeTime.value_or(0.0f));
            })
            RBFX_RAW(PlayExclusive, [](AnimationController* controller, const char* name,
                unsigned char layer, bool looped, sol::optional<float> fadeTime) {
                return controller && controller->PlayExclusive(name, layer, looped, fadeTime.value_or(0.0f));
            })
            RBFX_RAW(Fade, [](AnimationController* controller, const char* name,
                float targetWeight, sol::optional<float> fadeTime) {
                return controller && controller->Fade(name, targetWeight, fadeTime.value_or(0.0f));
            })
            RBFX_RAW(Stop, [](AnimationController* controller, const char* name,
                sol::optional<float> fadeTime) {
                return controller && controller->Stop(name, fadeTime.value_or(0.0f));
            })
            RBFX_OVERLOAD(IsPlaying,
                [](AnimationController* controller, const char* name) {
                    return controller && controller->IsPlaying(name);
                },
                [](AnimationController* controller, Animation* animation) {
                    return controller && controller->IsPlaying(animation);
                })
            // Current playback time of a named animation (44_RibbonTrailDemo
            // toggles emission at a fixed track time).
            RBFX_RAW(GetTime, [](AnimationController* controller, const char* name) -> float {
                return controller ? controller->GetTime(name) : 0.0f;
            })
        );
    }
    RegisterLuaObjectWrapper<AnimationController>();

    // Terrain: heightmap terrain, optionally with collision through a
    // sibling CollisionShape:SetTerrain (19_VehicleDemo).
    {
        using RBFX_THIS = Terrain;
        RBFX_USERTYPE(Terrain,
            sol::no_constructor
            RBFX_BASES(Component, Serializable, Object)
            RBFX_M(SetPatchSize)
            RBFX_M(SetSpacing)
            RBFX_M(SetSmoothing)
            RBFX_RAW(SetHeightMap, [](Terrain* terrain, Image* image) -> bool {
                return terrain && terrain->SetHeightMap(image);
            })
            RBFX_M(SetMaterial)
            RBFX_M(SetOccluder)
            RBFX_M(GetHeight)
            RBFX_M(GetNormal)
        );
    }
    RegisterLuaObjectWrapper<Terrain>();

    // Billboard: plain data struct describing one quad in a BillboardSet.
    {
        using RBFX_THIS = Billboard;
        RBFX_USERTYPE(Billboard,
            sol::no_constructor
            RBFX_RAW(position, &Billboard::position_)
            RBFX_RAW(size, &Billboard::size_)
            RBFX_RAW(rotation, &Billboard::rotation_)
            RBFX_RAW(enabled, &Billboard::enabled_)
        );
    }

    // BillboardSet: particle-like quads facing the camera.
    {
        using RBFX_THIS = BillboardSet;
        RBFX_USERTYPE(BillboardSet,
            sol::no_constructor
            RBFX_BASES(Drawable, Component, Serializable, Object)
            RBFX_M(SetNumBillboards)
            RBFX_M(GetNumBillboards)
            RBFX_M(SetMaterial)
            RBFX_M(SetSorted)
            RBFX_M(GetBillboard)
            RBFX_M(Commit)
        );
    }
    RegisterLuaObjectWrapper<BillboardSet>();

    // ParticleEffect: 3D particle configuration resource consumed by
    // ParticleEmitter:SetEffect.
    {
        using RBFX_THIS = ParticleEffect;
        RBFX_USERTYPE(ParticleEffect,
            sol::no_constructor
            RBFX_BASES(Resource, Object)
        );
    }
    RegisterLuaObjectWrapper<ParticleEffect>();

    // ParticleEmitter: 3D particle effect emitter (46_RaycastVehicle dust).
    {
        using RBFX_THIS = ParticleEmitter;
        RBFX_USERTYPE(ParticleEmitter,
            sol::no_constructor
            RBFX_BASES(BillboardSet, Drawable, Component, Serializable, Object)
            RBFX_M(SetEffect)
            RBFX_M(SetEmitting)
            RBFX_M(IsEmitting)
        );
    }
    RegisterLuaObjectWrapper<ParticleEmitter>();

    // DecalSet: paint decals onto drawable geometry (08_Decals).
    {
        using RBFX_THIS = DecalSet;
        RBFX_USERTYPE(DecalSet,
            sol::no_constructor
            RBFX_BASES(Drawable, Component, Serializable, Object)
            RBFX_M(SetMaterial)
            RBFX_M(AddDecal)
            RBFX_M(RemoveDecals)
            RBFX_M(RemoveAllDecals)
        );
    }
    RegisterLuaObjectWrapper<DecalSet>();

    // Viewport: render setup combining scene + camera.
    {
        using RBFX_THIS = Viewport;
        RBFX_USERTYPE(Viewport,
            sol::no_constructor
            RBFX_BASES(Object)
            RBFX_M(SetScene)
            RBFX_M(SetCamera)
            RBFX_M(SetRect)
            RBFX_M(GetScene)
            RBFX_M(GetCamera)
        );
    }

    // DebugRenderer: programmatic debug drawing.
    {
        using RBFX_THIS = DebugRenderer;
        RBFX_USERTYPE(DebugRenderer,
            sol::no_constructor
            RBFX_BASES(Component, Serializable, Object)
            RBFX_RAW(AddLine, [](DebugRenderer* debug, const Vector3& start, const Vector3& end, const Color& color) {
                if (debug) debug->AddLine(start, end, color);
            })
            RBFX_RAW(AddBoundingBox, [](DebugRenderer* debug, const BoundingBox& box, const Color& color) {
                if (debug) debug->AddBoundingBox(box, color);
            })
            RBFX_RAW(AddSphere, [](DebugRenderer* debug, const Vector3& center, float radius, const Color& color) {
                if (debug) debug->AddSphere(Sphere(center, radius), color);
            })
            RBFX_RAW(AddNode, [](DebugRenderer* debug, Node* node, float scale) {
                if (debug) debug->AddNode(node, scale);
            })
        );
    }
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
    RBFX_ENUM_TABLE(LIGHT, "POINT", LIGHT_POINT, "SPOT", LIGHT_SPOT, "DIRECTIONAL",
        LIGHT_DIRECTIONAL);

    // RibbonTrail trail types (44_RibbonTrailDemo).
    RBFX_ENUM_TABLE(TT, "FACE_CAMERA", TT_FACE_CAMERA, "BONE", TT_BONE);

    // Window modes for Graphics:SetDefaultWindowModes
    // (54_WindowSettingsDemo).
    RBFX_ENUM_TABLE(WMODE, "WINDOWED", static_cast<int>(WindowMode::Windowed), "BORDERLESS",
        static_cast<int>(WindowMode::Borderless), "FULLSCREEN",
        static_cast<int>(WindowMode::Fullscreen));

    // Vertex element datatypes and semantics for VertexElement()
    // (34_DynamicGeometry).
    RBFX_ENUM_TABLE(VET, "INT", TYPE_INT, "FLOAT", TYPE_FLOAT, "VECTOR2", TYPE_VECTOR2, "VECTOR3",
        TYPE_VECTOR3, "VECTOR4", TYPE_VECTOR4, "UBYTE4", TYPE_UBYTE4, "UBYTE4_NORM",
        TYPE_UBYTE4_NORM);

    RBFX_ENUM_TABLE(VSEM, "POSITION", SEM_POSITION, "NORMAL", SEM_NORMAL, "BINORMAL", SEM_BINORMAL,
        "TANGENT", SEM_TANGENT, "TEXCOORD", SEM_TEXCOORD, "COLOR", SEM_COLOR, "BLENDWEIGHTS",
        SEM_BLENDWEIGHTS, "BLENDINDICES", SEM_BLENDINDICES, "OBJECTINDEX", SEM_OBJECTINDEX);

    // View override flags for Camera:SetViewOverrideFlags.
    RBFX_ENUM_TABLE(VO, "DISABLE_OCCLUSION", VO_DISABLE_OCCLUSION, "NO_SHADOWS",
        VO_DISABLE_SHADOWS);

    // Texture filter modes for Texture:SetFilterMode.
    RBFX_ENUM_TABLE(FILTER, "NEAREST", FILTER_NEAREST, "BILINEAR", FILTER_BILINEAR, "TRILINEAR",
        FILTER_TRILINEAR, "ANISOTROPIC", FILTER_ANISOTROPIC, "DEFAULT", FILTER_DEFAULT);

    // Cull modes for Material:SetCullMode / GetCullMode. Material binding took
    // raw ints before this table existed; scripts can now name them. Additive
    // (no prior Lua name for CULL_*), and kept as plain ints to match the
    // existing int-passing idiom every other enum table uses (sol2 marshals the
    // unregistered CullMode parameter from an integer, so this is behavior-neutral).
    RBFX_ENUM_TABLE(CULL, "NONE", CULL_NONE, "CCW", CULL_CCW, "CW", CULL_CW);

    // Common texture formats for Texture2D:SetSize.
    RBFX_ENUM_TABLE(TEXF, "RGBA8_UNORM", static_cast<int>(TextureFormat::TEX_FORMAT_RGBA8_UNORM),
        "RGBA8_UNORM_SRGB", static_cast<int>(TextureFormat::TEX_FORMAT_RGBA8_UNORM_SRGB),
        "BGRA8_UNORM", static_cast<int>(TextureFormat::TEX_FORMAT_BGRA8_UNORM), "R8_UNORM",
        static_cast<int>(TextureFormat::TEX_FORMAT_R8_UNORM), "RG16_UNORM",
        static_cast<int>(TextureFormat::TEX_FORMAT_RG16_UNORM), "RGBA16_FLOAT",
        static_cast<int>(TextureFormat::TEX_FORMAT_RGBA16_FLOAT), "R32_FLOAT",
        static_cast<int>(TextureFormat::TEX_FORMAT_R32_FLOAT), "RGBA32_FLOAT",
        static_cast<int>(TextureFormat::TEX_FORMAT_RGBA32_FLOAT), "D32",
        static_cast<int>(TextureFormat::TEX_FORMAT_D32_FLOAT), "D24S8",
        static_cast<int>(TextureFormat::TEX_FORMAT_D24_UNORM_S8_UINT));
}

} // namespace Urho3D

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
        using LUA_THIS = Graphics;
        LUA_CLASS(Graphics,
            sol::no_constructor
            LUA_BASES(Object)
            LUA_MEMBER_FUNC(GetWidth)
            LUA_MEMBER_FUNC(GetHeight)
            LUA_MEMBER_FUNC(SetWindowTitle)
            LUA_MEMBER_FUNC_RAW(TakeScreenShot, [context](Graphics* graphics, sol::this_state s) -> sol::object {
                if (!graphics)
                    return sol::lua_nil;
                SharedPtr<Image> image(new Image(context));
                if (!graphics->TakeScreenShot(*image))
                    return sol::lua_nil;
                return sol::make_object(sol::state_view(s), image);
            })
            LUA_MEMBER_FUNC(ToggleFullscreen)
            // Multi-monitor setup (54_WindowSettingsDemo).
            LUA_MEMBER_FUNC(GetMonitorCount)
            LUA_MEMBER_FUNC_RAW(SetDefaultWindowModes, [](Graphics* graphics, const sol::table& settings) -> bool {
                return graphics ? graphics->SetDefaultWindowModes(TableToWindowSettings(settings, WindowSettings{})) : false;
            })
            LUA_MEMBER_PROP_RAW(windowTitle, sol::property(&Graphics::GetWindowTitle, &Graphics::SetWindowTitle))
        );
    }
    RegisterLuaObjectWrapper<Graphics>();

    // RenderDevice: low-level window state queries (54_WindowSettingsDemo).
    {
        using LUA_THIS = RenderDevice;
        LUA_CLASS(RenderDevice,
            sol::no_constructor
            LUA_BASES(Object)
            LUA_MEMBER_FUNC_RAW(GetWindowSettings, [](RenderDevice* device, sol::this_state s) -> sol::table {
                return WindowSettingsToTable(sol::state_view(s),
                    device ? device->GetWindowSettings() : WindowSettings{});
            })
            LUA_MEMBER_FUNC_RAW(GetSwapChainSize, [](RenderDevice* device) -> IntVector2 {
                return device ? device->GetSwapChainSize() : IntVector2::ZERO;
            })
            LUA_MEMBER_FUNC_RAW(GetDpiScale, [](RenderDevice* device) -> float {
                return device ? device->GetDpiScale() : 1.0f;
            })
            // Enumerate fullscreen resolutions of a monitor as an array of
            // tables { width, height, refreshRate }.
            LUA_MEMBER_FUNC_RAW(GetFullscreenModes, [](RenderDevice* device, int monitor, sol::this_state s) -> sol::table {
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
            LUA_MEMBER_FUNC_RAW(GetClosestFullscreenModeIndex, [](RenderDevice* device, const sol::table& modes, const IntVector2& size,
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
        using LUA_THIS = Image;
        LUA_CLASS(Image,
            sol::no_constructor
            LUA_BASES(Resource, Object)
            LUA_MEMBER_FUNC(GetWidth)
            LUA_MEMBER_FUNC(GetHeight)
            LUA_MEMBER_FUNC(GetComponents)
            LUA_MEMBER_FUNC_RAW(SetSize, [](Image* image, int width, int height, unsigned components) {
                return image ? image->SetSize(width, height, components) : false;
            })
            LUA_MEMBER_FUNC_RAW(SavePNG, [](const Image* image, const ea::string& fileName) {
                return image && image->SavePNG(fileName);
            })
            LUA_MEMBER_FUNC_RAW(SaveJPG, [](const Image* image, const ea::string& fileName, int quality) {
                return image && image->SaveJPG(fileName, quality);
            })
        );
    }
    RegisterLuaObjectWrapper<Image>();

    // Renderer subsystem: viewport management.
    {
        using LUA_THIS = Renderer;
        LUA_CLASS(Renderer,
            sol::no_constructor
            LUA_BASES(Object)
            LUA_MEMBER_FUNC(SetViewport)
            LUA_MEMBER_FUNC(GetViewport)
            LUA_MEMBER_FUNC(GetNumViewports)
            LUA_MEMBER_FUNC(SetTextureAnisotropy)
            LUA_MEMBER_FUNC(SetTextureFilterMode)
            LUA_MEMBER_FUNC(SetTextureQuality)
            LUA_MEMBER_FUNC(DrawDebugGeometry)
            LUA_MEMBER_FUNC(GetDefaultZone)
        );
    }
    RegisterLuaObjectWrapper<Renderer>();

    // Camera component.
    {
        using LUA_THIS = Camera;
        LUA_CLASS(Camera,
            sol::no_constructor
            LUA_BASES(Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetFarClip)
            LUA_MEMBER_FUNC(SetNearClip)
            LUA_MEMBER_FUNC(SetFov)
            LUA_MEMBER_FUNC(SetOrthographic)
            LUA_MEMBER_FUNC_OVERLOAD(SetOrthoSize,
                LUA_CAST(SetOrthoSize, void, float),
                LUA_CAST(SetOrthoSize, void, const Vector2&))
            LUA_MEMBER_FUNC(SetAspectRatio)
            LUA_MEMBER_FUNC(SetAutoAspectRatio)
            LUA_MEMBER_FUNC(SetFillMode)
            LUA_MEMBER_FUNC(SetViewMask)
            LUA_MEMBER_FUNC_RAW(SetViewOverrideFlags, [](Camera* camera, unsigned flags) {
                if (camera)
                    camera->SetViewOverrideFlags(ViewOverrideFlags{static_cast<ViewOverride>(flags)});
            })
            LUA_MEMBER_FUNC(GetFarClip)
            LUA_MEMBER_FUNC(GetNearClip)
            LUA_MEMBER_FUNC(GetFov)
            LUA_MEMBER_FUNC(IsOrthographic)
            LUA_MEMBER_FUNC(GetScreenRay)
            LUA_MEMBER_FUNC(GetScreenRayFromMouse)
            LUA_MEMBER_FUNC(WorldToScreenPoint)
            LUA_MEMBER_FUNC(ScreenToWorldPoint)
            // Planar reflection & clipping (23_Water).
            LUA_MEMBER_FUNC(SetUseReflection)
            LUA_MEMBER_FUNC(SetReflectionPlane)
            LUA_MEMBER_FUNC(SetUseClipping)
            LUA_MEMBER_FUNC(SetClipPlane)
            LUA_MEMBER_FUNC(SetZoom)
            LUA_MEMBER_FUNC(GetZoom)
        );
    }
    RegisterLuaObjectWrapper<Camera>();

    // Drawable base: common rendering switches. Registered before the derived
    // components because sol3 requires base usertypes to exist first.
    {
        using LUA_THIS = Drawable;
        LUA_CLASS(Drawable,
            sol::no_constructor
            LUA_BASES(Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetCastShadows)
            LUA_MEMBER_FUNC(SetOccluder)
            LUA_MEMBER_FUNC_RAW(GetWorldBoundingBox, [](Drawable* drawable) { return drawable ? drawable->GetWorldBoundingBox() : BoundingBox(); })
            LUA_MEMBER_FUNC(SetViewMask)
            LUA_MEMBER_FUNC(SetDrawDistance)
            LUA_MEMBER_FUNC(SetShadowDistance)
            LUA_MEMBER_FUNC_RAW(SetEnabled, &Component::SetEnabled)
        );
    }

    // Light component.
    {
        using LUA_THIS = Light;
        LUA_CLASS(Light,
            sol::no_constructor
            LUA_BASES(Drawable, Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetLightType)
            LUA_MEMBER_FUNC(SetColor)
            LUA_MEMBER_FUNC(SetBrightness)
            LUA_MEMBER_FUNC(SetRange)
            LUA_MEMBER_FUNC(SetFov)
            LUA_MEMBER_FUNC(SetRadius)
            LUA_MEMBER_FUNC(SetLength)
            LUA_MEMBER_FUNC(SetAspectRatio)
            LUA_MEMBER_FUNC(SetSpecularIntensity)
            LUA_MEMBER_FUNC(SetCastShadows)
            LUA_MEMBER_FUNC(SetShadowIntensity)
            LUA_MEMBER_FUNC_RAW(SetShadowBias, [](Light* light, float constantBias, float slopeScaledBias, sol::optional<float> normalOffset) {
                if (light)
                    light->SetShadowBias(BiasParameters(constantBias, slopeScaledBias, normalOffset.value_or(0.0f)));
            })
            LUA_MEMBER_FUNC_RAW(SetShadowCascade, [](Light* light, float split1, float split2, float split3,
                sol::optional<float> split4, sol::optional<float> fadeStart,
                sol::optional<float> biasAutoAdjust) {
                // Mirror the C++ defaults: split4 = 0.0, fadeStart = 0.8,
                // biasAutoAdjust = 0.8. Samples pass 3 to 5 of the 6 values.
                if (light)
                    light->SetShadowCascade(CascadeParameters(split1, split2, split3,
                        split4.value_or(0.0f), fadeStart.value_or(0.8f), biasAutoAdjust.value_or(0.8f)));
            })
            LUA_MEMBER_FUNC(SetShadowResolution)
            LUA_MEMBER_FUNC(SetShadowFadeDistance)
            LUA_MEMBER_FUNC(SetShadowNearFarRatio)
            LUA_MEMBER_FUNC(SetRampTexture)
            LUA_MEMBER_FUNC(GetLightType)
            LUA_MEMBER_FUNC(GetColor)
            LUA_MEMBER_FUNC(GetBrightness)
            LUA_MEMBER_FUNC(GetRange)
        );
    }
    RegisterLuaObjectWrapper<Light>();

    // Texture resources. Sprite and material texture slots take the base
    // class; Texture2D exists for GetResource("Texture2D", ...) resolution.
    {
        using LUA_THIS = Texture;
        LUA_CLASS(Texture,
            sol::no_constructor
            LUA_BASES(Resource, Object)
            LUA_MEMBER_FUNC(GetWidth)
            LUA_MEMBER_FUNC(GetHeight)
            LUA_MEMBER_FUNC_RAW(SetFilterMode, [](Texture* texture, int mode) {
                if (texture)
                    texture->SetFilterMode(static_cast<TextureFilterMode>(mode));
            })
            LUA_MEMBER_FUNC_RAW(GetRenderSurface, [](Texture* texture) -> RenderSurface* {
                return texture ? texture->GetRenderSurface() : nullptr;
            })
        );
    }
    RegisterLuaObjectWrapper<Texture>();

    {
        using LUA_THIS = Texture2D;
        LUA_CLASS(Texture2D,
            sol::call_constructor, sol::factories(
                [context]() { return SharedPtr<Texture2D>(new Texture2D(context)); })
            LUA_BASES(Texture, Resource, Object)
            LUA_MEMBER_FUNC_RAW(SetSize, [](Texture2D* texture, int width, int height, unsigned format,
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
        using LUA_THIS = RenderSurface;
        LUA_CLASS(RenderSurface,
            sol::no_constructor
            LUA_MEMBER_FUNC(SetViewport)
            LUA_MEMBER_FUNC(GetViewport)
        );
    }

    // Technique: shader pipeline description resource.
    {
        using LUA_THIS = Technique;
        LUA_CLASS(Technique,
            sol::no_constructor
            LUA_BASES(Resource, Object)
        );
    }
    RegisterLuaObjectWrapper<Technique>();

    // StaticModel and the model/material resources.
    {
        using LUA_THIS = Model;
        LUA_CLASS(Model,
            sol::call_constructor, sol::factories(
                [context]() { return SharedPtr<Model>(new Model(context)); })
            LUA_BASES(Resource, Object)
            LUA_MEMBER_FUNC(SetNumGeometries)
            LUA_MEMBER_FUNC(SetGeometry)
            LUA_MEMBER_FUNC(SetBoundingBox)
            LUA_MEMBER_FUNC(GetNumGeometries)
            LUA_MEMBER_FUNC(GetGeometry)
            LUA_MEMBER_FUNC_RAW(Clone, [](Model* model) -> SharedPtr<Model> {
                return model ? SharedPtr<Model>(model->Clone()) : nullptr;
            })
            // Explicit buffer registration so the model can be saved properly
            // (34_DynamicGeometry).
            LUA_MEMBER_FUNC_RAW(SetVertexBuffers, [](Model* model, const sol::table& buffers,
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
            LUA_MEMBER_FUNC_RAW(SetIndexBuffers, [](Model* model, const sol::table& buffers) {
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
        using LUA_THIS = Material;
        LUA_CLASS(Material,
            sol::call_constructor, sol::factories(
                [context]() { return SharedPtr<Material>(new Material(context)); })
            LUA_BASES(Resource, Object)
            LUA_MEMBER_FUNC_RAW(SetShaderParameter, [](Material* material, const char* name, sol::object value, sol::this_state s) {
                if (material)
                    material->SetShaderParameter(name, LuaToVariant(sol::state_view(s), value));
            })
            LUA_MEMBER_FUNC_RAW(SetTechnique, [](Material* material, unsigned index, Technique* technique) {
                if (material)
                    material->SetTechnique(index, technique);
            })
            LUA_MEMBER_FUNC_RAW(SetTexture, [](Material* material, const char* name, Texture* texture) {
                if (material)
                    material->SetTexture(name, texture);
            })
            LUA_MEMBER_FUNC_RAW(SetDepthBias, [](Material* material, float constantBias, float slopeScaledBias, sol::optional<float> normalOffset) {
                if (material)
                    material->SetDepthBias(BiasParameters(constantBias, slopeScaledBias, normalOffset.value_or(0.0f)));
            })
            LUA_MEMBER_FUNC(GetNumTechniques)
            LUA_MEMBER_FUNC_RAW(Clone, [](Material* material) -> SharedPtr<Material> {
                return material ? SharedPtr<Material>(material->Clone()) : nullptr;
            })
            LUA_MEMBER_FUNC(SetCullMode)
            LUA_MEMBER_FUNC_RAW(SetVertexShaderDefines, [](Material* material, const char* defines) {
                if (material)
                    material->SetVertexShaderDefines(defines);
            })
            LUA_MEMBER_FUNC_RAW(SetPixelShaderDefines, [](Material* material, const char* defines) {
                if (material)
                    material->SetPixelShaderDefines(defines);
            })
            LUA_MEMBER_FUNC_RAW(GetVertexShaderDefines, [](Material* material) -> const char* {
                static thread_local ea::string value;
                value = material ? material->GetVertexShaderDefines() : ea::string{};
                return value.c_str();
            })
            LUA_MEMBER_FUNC_RAW(GetPixelShaderDefines, [](Material* material) -> const char* {
                static thread_local ea::string value;
                value = material ? material->GetPixelShaderDefines() : ea::string{};
                return value.c_str();
            })
            LUA_MEMBER_FUNC_RAW(SetShaderParameterAnimation, [](Material* material, const char* name,
                ValueAnimation* animation, sol::optional<int> wrapMode, sol::optional<float> speed) {
                if (material)
                    material->SetShaderParameterAnimation(name, animation,
                        static_cast<WrapMode>(wrapMode.value_or(WM_LOOP)), speed.value_or(1.0f));
            })
            LUA_MEMBER_FUNC_RAW(SetShaderParameterAnimationWrapMode, [](Material* material, const char* name, int wrapMode) {
                if (material)
                    material->SetShaderParameterAnimationWrapMode(name, static_cast<WrapMode>(wrapMode));
            })
            LUA_MEMBER_FUNC(SetShaderParameterAnimationSpeed)
            // Associate material with scene so shader parameter animation
            // respects scene time scale (31_MaterialAnimation).
            LUA_MEMBER_FUNC(SetScene)
        );
    }
    RegisterLuaObjectWrapper<Material>();

    {
        using LUA_THIS = StaticModel;
        LUA_CLASS(StaticModel,
            sol::no_constructor
            LUA_BASES(Drawable, Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetModel)
            LUA_MEMBER_FUNC(GetModel)
            LUA_MEMBER_FUNC_OVERLOAD(SetMaterial,
                LUA_CAST(SetMaterial, void, Material*),
                LUA_CAST(SetMaterial, bool, unsigned, Material*))
            LUA_MEMBER_FUNC_RAW(GetMaterial, static_cast<Material* (StaticModel::*)() const>(&StaticModel::GetMaterial))
        );
    }
    RegisterLuaObjectWrapper<StaticModel>();

    // StaticModelGroup: render one model many times, each instance node
    // supplying its transform (20_HugeObjectCount).
    {
        using LUA_THIS = StaticModelGroup;
        LUA_CLASS(StaticModelGroup,
            sol::no_constructor
            LUA_BASES(StaticModel, Drawable, Component, Serializable, Object)
            LUA_MEMBER_FUNC(AddInstanceNode)
            LUA_MEMBER_FUNC(RemoveInstanceNode)
            LUA_MEMBER_FUNC(GetNumInstanceNodes)
            LUA_MEMBER_FUNC(GetInstanceNode)
        );
    }
    RegisterLuaObjectWrapper<StaticModelGroup>();

    // RibbonTrail: trail rendered behind a moving node (44_RibbonTrailDemo).
    {
        using LUA_THIS = RibbonTrail;
        LUA_CLASS(RibbonTrail,
            sol::no_constructor
            LUA_BASES(Drawable, Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetWidth)
            LUA_MEMBER_FUNC(SetStartColor)
            LUA_MEMBER_FUNC(SetEndColor)
            LUA_MEMBER_FUNC_RAW(SetTrailType, [](RibbonTrail* trail, int type) {
                if (trail)
                    trail->SetTrailType(static_cast<TrailType>(type));
            })
            LUA_MEMBER_FUNC(SetLifetime)
            LUA_MEMBER_FUNC(SetEmitting)
            LUA_MEMBER_FUNC(IsEmitting)
            LUA_MEMBER_FUNC(SetTailColumn)
            LUA_MEMBER_FUNC(SetMaterial)
            LUA_MEMBER_FUNC(SetUpdateInvisible)
            LUA_MEMBER_FUNC_RAW(GetTrailType, [](RibbonTrail* trail) {
                return trail ? static_cast<int>(trail->GetTrailType()) : 0;
            })
        );
    }
    RegisterLuaObjectWrapper<RibbonTrail>();

    // GPU vertex/index buffers and geometry for runtime-built meshes
    // (34_DynamicGeometry).
    {
        using LUA_THIS = VertexElement;
        LUA_CLASS(VertexElement,
            sol::call_constructor, sol::factories(
                []() { return VertexElement(); },
                [](int type, int semantic, sol::optional<unsigned char> index, sol::optional<unsigned> stepRate) {
                    return VertexElement(static_cast<VertexElementType>(type),
                        static_cast<VertexElementSemantic>(semantic), index.value_or(0), stepRate.value_or(0));
                })
            LUA_MEMBER_PROP_RAW(type, sol::property(
                [](VertexElement* element) { return element ? static_cast<int>(element->type_) : 0; },
                [](VertexElement* element, int type) { if (element) element->type_ = static_cast<VertexElementType>(type); }))
            LUA_MEMBER_PROP_RAW(semantic, sol::property(
                [](VertexElement* element) { return element ? static_cast<int>(element->semantic_) : 0; },
                [](VertexElement* element, int semantic) { if (element) element->semantic_ = static_cast<VertexElementSemantic>(semantic); }))
            LUA_MEMBER_PROP_RAW(index, sol::property(
                [](VertexElement* element) { return element ? static_cast<int>(element->index_) : 0; },
                [](VertexElement* element, int index) { if (element) element->index_ = static_cast<unsigned char>(index); }))
        );
    }

    {
        using LUA_THIS = VertexBuffer;
        LUA_CLASS(VertexBuffer,
            sol::call_constructor, sol::factories(
                [context]() { return SharedPtr<VertexBuffer>(new VertexBuffer(context)); })
            LUA_MEMBER_FUNC(SetShadowed)
            LUA_MEMBER_FUNC(SetDebugName)
            LUA_MEMBER_FUNC_OVERLOAD(SetSize,
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
                LUA_CAST(SetSize, bool, unsigned, unsigned, bool))
            LUA_MEMBER_FUNC_RAW(Update, [](VertexBuffer* buffer, const std::string& data) {
                if (buffer && !data.empty())
                    buffer->Update(data.data(), static_cast<unsigned>(data.size()));
            })
            LUA_MEMBER_FUNC(GetVertexCount)
            LUA_MEMBER_FUNC_RAW(GetVertexSize, static_cast<unsigned (VertexBuffer::*)() const>(&VertexBuffer::GetVertexSize))
            // Read original vertex positions from the shadowed buffer
            // (34_DynamicGeometry).
            LUA_MEMBER_FUNC_RAW(GetVertexPositions, [](VertexBuffer* buffer, sol::this_state s) -> sol::table {
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
            LUA_MEMBER_FUNC_RAW(UpdateVertexPositions, [](VertexBuffer* buffer, const sol::table& positions) {
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
        using LUA_THIS = IndexBuffer;
        LUA_CLASS(IndexBuffer,
            sol::call_constructor, sol::factories(
                [context]() { return SharedPtr<IndexBuffer>(new IndexBuffer(context)); })
            LUA_MEMBER_FUNC(SetShadowed)
            LUA_MEMBER_FUNC(SetDebugName)
            LUA_MEMBER_FUNC(SetSize)
            LUA_MEMBER_FUNC_RAW(Update, [](IndexBuffer* buffer, const std::string& data) {
                if (buffer && !data.empty())
                    buffer->Update(data.data(), static_cast<unsigned>(data.size()));
            })
            LUA_MEMBER_FUNC(GetIndexCount)
            LUA_MEMBER_FUNC(GetIndexSize)
        );
    }
    RegisterLuaObjectWrapper<IndexBuffer>();

    {
        using LUA_THIS = Geometry;
        LUA_CLASS(Geometry,
            sol::call_constructor, sol::factories(
                [context]() { return SharedPtr<Geometry>(new Geometry(context)); })
            LUA_MEMBER_FUNC(SetVertexBuffer)
            LUA_MEMBER_FUNC(SetIndexBuffer)
            LUA_MEMBER_FUNC_RAW(SetDrawRange, [](Geometry* geometry, int primitiveType, unsigned indexStart,
                unsigned indexCount, sol::optional<bool> getUsedVertexRange) {
                if (geometry)
                    geometry->SetDrawRange(static_cast<PrimitiveType>(primitiveType), indexStart, indexCount,
                        getUsedVertexRange.value_or(true));
            })
            LUA_MEMBER_FUNC(SetLodDistance)
            LUA_MEMBER_FUNC(GetVertexBuffer)
        );
    }
    RegisterLuaObjectWrapper<Geometry>();

    // Text3D: text component in world space (32_Physics2DConstraints).
    {
        using LUA_THIS = Text3D;
        LUA_CLASS(Text3D,
            sol::no_constructor
            LUA_BASES(Drawable, Component, Serializable, Object)
            LUA_MEMBER_FUNC_RAW(SetText, [](Text3D* text, const char* value) {
                if (text)
                    text->SetText(value);
            })
            LUA_MEMBER_FUNC_OVERLOAD(SetFont,
                [](Text3D* text, Font* font, float size) -> bool { return text && text->SetFont(font, size); },
                [](Text3D* text, const char* fontName, float size) -> bool { return text && text->SetFont(fontName, size); })
            LUA_MEMBER_FUNC(SetAlignment)
            LUA_MEMBER_FUNC(SetTextAlignment)
            LUA_MEMBER_FUNC_RAW(SetColor, static_cast<void (Text3D::*)(const Color&)>(&Text3D::SetColor))
            LUA_MEMBER_FUNC(SetTextEffect)
            LUA_MEMBER_FUNC(SetEffectColor)
            LUA_MEMBER_FUNC(SetFontSize)
        );
    }
    RegisterLuaObjectWrapper<Text3D>();

    // Skybox: default environment backdrop used by most samples.
    {
        using LUA_THIS = Skybox;
        LUA_CLASS(Skybox,
            sol::no_constructor
            LUA_BASES(StaticModel, Drawable, Component, Serializable, Object)
        );
    }
    RegisterLuaObjectWrapper<Skybox>();

    // Octree: mandatory scene component for visibility.
    {
        using LUA_THIS = Octree;
        LUA_CLASS(Octree,
            sol::no_constructor
            LUA_BASES(Component, Serializable, Object)
            // Raycast helper: triangle-accurate single query returning a table
            // { position, normal, distance, drawable } or nil on miss.
            LUA_MEMBER_FUNC_RAW(RaycastSingle, [](Octree* octree, const Ray& ray, float maxDistance,
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
        using LUA_THIS = Zone;
        LUA_CLASS(Zone,
            sol::no_constructor
            LUA_BASES(Drawable, Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetBoundingBox)
            LUA_MEMBER_FUNC(SetAmbientColor)
            LUA_MEMBER_FUNC(SetFogColor)
            LUA_MEMBER_FUNC(SetFogStart)
            LUA_MEMBER_FUNC(SetFogEnd)
            LUA_MEMBER_FUNC(SetHeightFog)
            LUA_MEMBER_FUNC(SetPriority)
        );
    }
    RegisterLuaObjectWrapper<Zone>();

    // Animation resource: skeleton keyframe track data.
    {
        using LUA_THIS = Animation;
        LUA_CLASS(Animation,
            sol::no_constructor
            LUA_BASES(Resource, Object)
            LUA_MEMBER_FUNC(GetLength)
            LUA_MEMBER_FUNC(GetAnimationName)
        );
    }
    RegisterLuaObjectWrapper<Animation>();

    // Bone: plain struct owned by a Skeleton. "animated" mirrors the public
    // Bone::animated_ flag used to disable keyframe animation per bone.
    {
        using LUA_THIS = Bone;
        LUA_CLASS(Bone,
            sol::no_constructor
            LUA_MEMBER_FUNC_RAW(name, [](const Bone* bone) { return bone ? bone->name_ : ea::string(); })
            LUA_MEMBER_PROP_RAW(animated, &Bone::animated_)
            LUA_MEMBER_FUNC_RAW(node, [](Bone* bone, sol::this_state s) -> sol::object {
                return bone ? WrapLuaObjectAs<Node>(sol::state_view(s), bone->node_.Get()) : sol::lua_nil;
            })
        );
    }

    // Skeleton: bone collection of an AnimatedModel.
    {
        using LUA_THIS = Skeleton;
        LUA_CLASS(Skeleton,
            sol::no_constructor
            LUA_MEMBER_FUNC(GetNumBones)
            LUA_MEMBER_FUNC_OVERLOAD(GetBone,
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
        using LUA_THIS = AnimatedModel;
        LUA_CLASS(AnimatedModel,
            sol::no_constructor
            LUA_BASES(StaticModel, Drawable, Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetUpdateInvisible)
            LUA_MEMBER_FUNC_RAW(GetSkeleton, [](AnimatedModel* model) -> Skeleton* {
                return model ? &model->GetSkeleton() : nullptr;
            })
        );
    }
    RegisterLuaObjectWrapper<AnimatedModel>();

    // AnimationController: the C++ fluent AnimationParameters API is
    // flattened into optional positional arguments for Lua.
    {
        using LUA_THIS = AnimationController;
        LUA_CLASS(AnimationController,
            sol::no_constructor
            LUA_BASES(Component, Serializable, Object)
            LUA_MEMBER_FUNC_RAW(PlayNew, [](AnimationController* controller, Animation* animation,
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
            LUA_MEMBER_FUNC_RAW(PlayNewExclusive, [](AnimationController* controller, Animation* animation,
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
            LUA_MEMBER_FUNC_RAW(PlayExistingExclusive, [](AnimationController* controller, Animation* animation,
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
            LUA_MEMBER_FUNC_RAW(SetSpeed, [](AnimationController* controller, const char* name, float speed) {
                return controller && controller->SetSpeed(name, speed);
            })
            LUA_MEMBER_FUNC_RAW(Play, [](AnimationController* controller, const char* name,
                unsigned char layer, bool looped, sol::optional<float> fadeTime) {
                return controller && controller->Play(name, layer, looped, fadeTime.value_or(0.0f));
            })
            LUA_MEMBER_FUNC_RAW(PlayExclusive, [](AnimationController* controller, const char* name,
                unsigned char layer, bool looped, sol::optional<float> fadeTime) {
                return controller && controller->PlayExclusive(name, layer, looped, fadeTime.value_or(0.0f));
            })
            LUA_MEMBER_FUNC_RAW(Fade, [](AnimationController* controller, const char* name,
                float targetWeight, sol::optional<float> fadeTime) {
                return controller && controller->Fade(name, targetWeight, fadeTime.value_or(0.0f));
            })
            LUA_MEMBER_FUNC_RAW(Stop, [](AnimationController* controller, const char* name,
                sol::optional<float> fadeTime) {
                return controller && controller->Stop(name, fadeTime.value_or(0.0f));
            })
            LUA_MEMBER_FUNC_OVERLOAD(IsPlaying,
                [](AnimationController* controller, const char* name) {
                    return controller && controller->IsPlaying(name);
                },
                [](AnimationController* controller, Animation* animation) {
                    return controller && controller->IsPlaying(animation);
                })
            // Current playback time of a named animation (44_RibbonTrailDemo
            // toggles emission at a fixed track time).
            LUA_MEMBER_FUNC_RAW(GetTime, [](AnimationController* controller, const char* name) -> float {
                return controller ? controller->GetTime(name) : 0.0f;
            })
        );
    }
    RegisterLuaObjectWrapper<AnimationController>();

    // Terrain: heightmap terrain, optionally with collision through a
    // sibling CollisionShape:SetTerrain (19_VehicleDemo).
    {
        using LUA_THIS = Terrain;
        LUA_CLASS(Terrain,
            sol::no_constructor
            LUA_BASES(Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetPatchSize)
            LUA_MEMBER_FUNC(SetSpacing)
            LUA_MEMBER_FUNC(SetSmoothing)
            LUA_MEMBER_FUNC_RAW(SetHeightMap, [](Terrain* terrain, Image* image) -> bool {
                return terrain && terrain->SetHeightMap(image);
            })
            LUA_MEMBER_FUNC(SetMaterial)
            LUA_MEMBER_FUNC(SetOccluder)
            LUA_MEMBER_FUNC(GetHeight)
            LUA_MEMBER_FUNC(GetNormal)
        );
    }
    RegisterLuaObjectWrapper<Terrain>();

    // Billboard: plain data struct describing one quad in a BillboardSet.
    {
        using LUA_THIS = Billboard;
        LUA_CLASS(Billboard,
            sol::no_constructor
            LUA_MEMBER_PROP_RAW(position, &Billboard::position_)
            LUA_MEMBER_PROP_RAW(size, &Billboard::size_)
            LUA_MEMBER_PROP_RAW(rotation, &Billboard::rotation_)
            LUA_MEMBER_PROP_RAW(enabled, &Billboard::enabled_)
        );
    }

    // BillboardSet: particle-like quads facing the camera.
    {
        using LUA_THIS = BillboardSet;
        LUA_CLASS(BillboardSet,
            sol::no_constructor
            LUA_BASES(Drawable, Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetNumBillboards)
            LUA_MEMBER_FUNC(GetNumBillboards)
            LUA_MEMBER_FUNC(SetMaterial)
            LUA_MEMBER_FUNC(SetSorted)
            LUA_MEMBER_FUNC(GetBillboard)
            LUA_MEMBER_FUNC(Commit)
        );
    }
    RegisterLuaObjectWrapper<BillboardSet>();

    // ParticleEffect: 3D particle configuration resource consumed by
    // ParticleEmitter:SetEffect.
    {
        using LUA_THIS = ParticleEffect;
        LUA_CLASS(ParticleEffect,
            sol::no_constructor
            LUA_BASES(Resource, Object)
        );
    }
    RegisterLuaObjectWrapper<ParticleEffect>();

    // ParticleEmitter: 3D particle effect emitter (46_RaycastVehicle dust).
    {
        using LUA_THIS = ParticleEmitter;
        LUA_CLASS(ParticleEmitter,
            sol::no_constructor
            LUA_BASES(BillboardSet, Drawable, Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetEffect)
            LUA_MEMBER_FUNC(SetEmitting)
            LUA_MEMBER_FUNC(IsEmitting)
        );
    }
    RegisterLuaObjectWrapper<ParticleEmitter>();

    // DecalSet: paint decals onto drawable geometry (08_Decals).
    {
        using LUA_THIS = DecalSet;
        LUA_CLASS(DecalSet,
            sol::no_constructor
            LUA_BASES(Drawable, Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetMaterial)
            LUA_MEMBER_FUNC(AddDecal)
            LUA_MEMBER_FUNC(RemoveDecals)
            LUA_MEMBER_FUNC(RemoveAllDecals)
        );
    }
    RegisterLuaObjectWrapper<DecalSet>();

    // Viewport: render setup combining scene + camera.
    {
        using LUA_THIS = Viewport;
        LUA_CLASS(Viewport,
            sol::no_constructor
            LUA_BASES(Object)
            LUA_MEMBER_FUNC(SetScene)
            LUA_MEMBER_FUNC(SetCamera)
            LUA_MEMBER_FUNC(SetRect)
            LUA_MEMBER_FUNC(GetScene)
            LUA_MEMBER_FUNC(GetCamera)
        );
    }

    // DebugRenderer: programmatic debug drawing.
    {
        using LUA_THIS = DebugRenderer;
        LUA_CLASS(DebugRenderer,
            sol::no_constructor
            LUA_BASES(Component, Serializable, Object)
            LUA_MEMBER_FUNC_RAW(AddLine, [](DebugRenderer* debug, const Vector3& start, const Vector3& end, const Color& color) {
                if (debug) debug->AddLine(start, end, color);
            })
            LUA_MEMBER_FUNC_RAW(AddBoundingBox, [](DebugRenderer* debug, const BoundingBox& box, const Color& color) {
                if (debug) debug->AddBoundingBox(box, color);
            })
            LUA_MEMBER_FUNC_RAW(AddSphere, [](DebugRenderer* debug, const Vector3& center, float radius, const Color& color) {
                if (debug) debug->AddSphere(Sphere(center, radius), color);
            })
            LUA_MEMBER_FUNC_RAW(AddNode, [](DebugRenderer* debug, Node* node, float scale) {
                if (debug) debug->AddNode(node, scale);
            })
        );
    }
    RegisterLuaObjectWrapper<DebugRenderer>();

    // Global helper: create a viewport without registering it with the
    // renderer (for render-to-texture surfaces).
    LUA_GLOBAL_FUNC(CreateViewport, [context](Scene* scene, Camera* camera,
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
    LUA_GLOBAL_FUNC(SetViewport, [context](unsigned index, Scene* scene, Camera* camera,
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
    LUA_ENUM_TABLE(LIGHT, "POINT", LIGHT_POINT, "SPOT", LIGHT_SPOT, "DIRECTIONAL",
        LIGHT_DIRECTIONAL);

    // RibbonTrail trail types (44_RibbonTrailDemo).
    LUA_ENUM_TABLE(TT, "FACE_CAMERA", TT_FACE_CAMERA, "BONE", TT_BONE);

    // Window modes for Graphics:SetDefaultWindowModes
    // (54_WindowSettingsDemo).
    LUA_ENUM_TABLE(WMODE, "WINDOWED", static_cast<int>(WindowMode::Windowed), "BORDERLESS",
        static_cast<int>(WindowMode::Borderless), "FULLSCREEN",
        static_cast<int>(WindowMode::Fullscreen));

    // Vertex element datatypes and semantics for VertexElement()
    // (34_DynamicGeometry).
    LUA_ENUM_TABLE(VET, "INT", TYPE_INT, "FLOAT", TYPE_FLOAT, "VECTOR2", TYPE_VECTOR2, "VECTOR3",
        TYPE_VECTOR3, "VECTOR4", TYPE_VECTOR4, "UBYTE4", TYPE_UBYTE4, "UBYTE4_NORM",
        TYPE_UBYTE4_NORM);

    LUA_ENUM_TABLE(VSEM, "POSITION", SEM_POSITION, "NORMAL", SEM_NORMAL, "BINORMAL", SEM_BINORMAL,
        "TANGENT", SEM_TANGENT, "TEXCOORD", SEM_TEXCOORD, "COLOR", SEM_COLOR, "BLENDWEIGHTS",
        SEM_BLENDWEIGHTS, "BLENDINDICES", SEM_BLENDINDICES, "OBJECTINDEX", SEM_OBJECTINDEX);

    // View override flags for Camera:SetViewOverrideFlags.
    LUA_ENUM_TABLE(VO, "DISABLE_OCCLUSION", VO_DISABLE_OCCLUSION, "NO_SHADOWS",
        VO_DISABLE_SHADOWS);

    // Texture filter modes for Texture:SetFilterMode.
    LUA_ENUM_TABLE(FILTER, "NEAREST", FILTER_NEAREST, "BILINEAR", FILTER_BILINEAR, "TRILINEAR",
        FILTER_TRILINEAR, "ANISOTROPIC", FILTER_ANISOTROPIC, "DEFAULT", FILTER_DEFAULT);

    // Cull modes for Material:SetCullMode / GetCullMode. Material binding took
    // raw ints before this table existed; scripts can now name them. Additive
    // (no prior Lua name for CULL_*), and kept as plain ints to match the
    // existing int-passing idiom every other enum table uses (sol2 marshals the
    // unregistered CullMode parameter from an integer, so this is behavior-neutral).
    LUA_ENUM_TABLE(CULL, "NONE", CULL_NONE, "CCW", CULL_CCW, "CW", CULL_CW);

    // Common texture formats for Texture2D:SetSize.
    LUA_ENUM_TABLE(TEXF, "RGBA8_UNORM", static_cast<int>(TextureFormat::TEX_FORMAT_RGBA8_UNORM),
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

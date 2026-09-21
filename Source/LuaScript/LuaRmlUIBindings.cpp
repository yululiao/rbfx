//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

// RmlUI binding module. Exposes the RmlUI subsystem, the off-screen canvas
// component, and - most importantly - a small non-invasive trampoline component
// (LuaRmlUIComponent) so that the C++ idiom "subclass RmlUIComponent and bind a
// data model from OnDataModelInitialized" is reachable from Lua without the
// engine knowing anything about Lua.
//
// Why a trampoline instead of exposing RmlUIComponent directly: RmlUIComponent
// only lets you bind a data model from inside its OnDataModelInitialized()
// virtual, and the RmlUi templates drive the UI through getter/setter/event
// callbacks (eastl::function). A Lua script cannot override a C++ virtual, so we
// ship one tiny reflected subclass in this plugin that (a) overrides the virtuals
// to call back into a Lua handler table and (b) adapts the eastl::function binds to
// sol::function. The engine source is untouched.

#include "../Urho3D/Precompiled.h"

#include "LuaBindings.h"
#include "LuaBindHelpers.h"
#include "LuaBindMacros.h"

#include "../Urho3D/Core/Context.h"
#include "../Urho3D/Graphics/Texture2D.h"
#include "../Urho3D/IO/Log.h"
#include "../Urho3D/RmlUI/RmlCanvasComponent.h"
#include "../Urho3D/RmlUI/RmlUI.h"
#include "../Urho3D/RmlUI/RmlUIComponent.h"

#include <EASTL/map.h>

#include <sol/sol.hpp>

namespace Urho3D
{
class LuaRmlUIComponent;
}

namespace sol
{

template <> struct is_automagical<Urho3D::RmlUI> : std::false_type {};
template <> struct is_automagical<Urho3D::RmlUIComponent> : std::false_type {};
template <> struct is_automagical<Urho3D::RmlCanvasComponent> : std::false_type {};
template <> struct is_automagical<Urho3D::LuaRmlUIComponent> : std::false_type {};

} // namespace sol

namespace Urho3D
{

/// Lua-driven RmlUIComponent. Behaves exactly like the built-in window component
/// but lets a script supply an "onInit" handler (invoked at the precise moment the
/// RmlUi data model is being constructed, when Bind* calls are legal) and an
/// optional "update" handler (per-frame). State lives in the Lua closure; the C++
/// side only forwards the data-model callbacks.
class RBFXLUA_API LuaRmlUIComponent : public RmlUIComponent
{
    URHO3D_OBJECT(LuaRmlUIComponent, RmlUIComponent);

public:
    explicit LuaRmlUIComponent(Context* context)
        : RmlUIComponent(context)
    {
    }

    /// Register as a creatable component type. Safe to call repeatedly.
    static void RegisterObject(Context* context)
    {
        if (!context->IsReflected<LuaRmlUIComponent>())
            context->AddFactoryReflection<LuaRmlUIComponent>();
    }

    /// Attach the Lua handler table. Recognised keys:
    ///   onInit(component)          - bind the data model (called once, at bind time)
    ///   update(component, timeStep)- per-frame tick (optional)
    /// Must be called before SetResource() opens the document.
    void SetHandlers(sol::table handlers)
    {
        luaState_ = handlers.lua_state();
        handlers_ = handlers;
    }

    /// Bind a read/write model property backed by two Lua functions.
    /// getter() -> value ; setter(value). Legal only inside the onInit handler.
    /// (String parameters use const char* to match this repo's Lua convention: a
    /// Lua string does not convert to ea::string, and it must be captured as an
    /// ea::string before being handed to the long-lived eastl::function callbacks.)
    bool BindProperty(const char* name, sol::protected_function getter, sol::protected_function setter)
    {
        auto* self = this;
        const ea::string n(name);
        return BindDataModelProperty(n,
            [self, getter, n](Variant& out) mutable
            {
                sol::protected_function_result r = getter();
                if (r.valid())
                    out = LuaToVariant(sol::state_view(self->luaState_), sol::object(r));
                else
                {
                    sol::error err = r;
                    URHO3D_LOGERROR("LuaRmlUIComponent getter for '{}' failed: {}", n, err.what());
                }
            },
            [self, setter, n](const Variant& value) mutable
            {
                if (!setter.valid())
                    return; // Read-only bind: nothing to write back.
                sol::protected_function_result r = setter(VariantToLua(sol::state_view(self->luaState_), value));
                if (!r.valid())
                {
                    sol::error err = r;
                    URHO3D_LOGERROR("LuaRmlUIComponent setter for '{}' failed: {}", n, err.what());
                }
            });
    }

    /// Bind a read-only model property: getter() -> value.
    bool BindReadonlyProperty(const char* name, sol::protected_function getter)
    {
        return BindProperty(name, getter, sol::protected_function());
    }

    /// Bind a model event (e.g. a button's data-event-click). cb(argsTable).
    bool BindEvent(const char* name, sol::protected_function callback)
    {
        auto* self = this;
        const ea::string n(name);
        return BindDataModelEvent(n,
            [self, callback, n](const VariantVector& args) mutable
            {
                sol::state_view lua(self->luaState_);
                sol::protected_function_result r = callback(VariantVectorToLuaTable(lua, args));
                if (!r.valid())
                {
                    sol::error err = r;
                    URHO3D_LOGERROR("LuaRmlUIComponent event '{}' handler failed: {}", n, err.what());
                }
            });
    }

    /// Mark a bound model variable dirty so the UI refreshes it.
    void MarkDirty(const char* name)
    {
        DirtyVariable(name);
    }

    /// Bind a persistent Urho Variant / VariantVector / VariantMap slot that the
    /// script later fills with SetVariant*(...). The slot lives on this component
    /// (ea::map => stable element addresses, which RmlUi holds as a raw pointer).
    bool BindVariant(const char* name)
    {
        auto slot = variantSlots_.emplace(name, Variant()).first;
        return BindDataModelVariant(slot->first, &slot->second);
    }
    bool BindVariantVector(const char* name)
    {
        auto slot = vectorSlots_.emplace(name, VariantVector()).first;
        return BindDataModelVariantVector(slot->first, &slot->second);
    }
    bool BindVariantMap(const char* name)
    {
        auto slot = mapSlots_.emplace(name, VariantMap()).first;
        return BindDataModelVariantMap(slot->first, &slot->second);
    }

    /// Fill a slot bound above and notify the model. Safe to call any time after onInit.
    void SetVariant(const char* name, sol::object value)
    {
        auto it = variantSlots_.find(name);
        if (it == variantSlots_.end())
            return;
        it->second = LuaToVariant(sol::state_view(luaState_), value);
        DirtyVariable(it->first);
    }
    void SetVariantVector(const char* name, sol::table values)
    {
        auto it = vectorSlots_.find(name);
        if (it == vectorSlots_.end())
            return;
        VariantVector& dst = it->second;
        dst.clear();
        sol::state_view lua(luaState_);
        values.for_each([&lua, &dst](sol::object, sol::object value) { dst.push_back(LuaToVariant(lua, value)); });
        DirtyVariable(it->first);
    }
    void SetVariantMap(const char* name, sol::table values)
    {
        auto it = mapSlots_.find(name);
        if (it == mapSlots_.end())
            return;
        VariantMap& dst = it->second;
        dst.clear();
        sol::state_view lua(luaState_);
        values.for_each([&lua, &dst](sol::object key, sol::object value)
        {
            if (key.get_type() == sol::type::string)
                dst[StringHash(key.as<const char*>())] = LuaToVariant(lua, value);
        });
        DirtyVariable(it->first);
    }

protected:
    /// Invoked by the base exactly while the RmlUi data model constructor is alive.
    /// This is the only window where Bind* calls succeed, so we run the Lua onInit here.
    void OnDataModelInitialized() override
    {
        if (!handlers_ || !luaState_)
            return;
        sol::state_view lua(luaState_);
        sol::object handler = (*handlers_)["onInit"];
        if (handler.valid() && handler.get_type() == sol::type::function)
        {
            sol::protected_function fn = handler;
            sol::protected_function_result r = fn(WrapLuaObject(lua, this));
            if (!r.valid())
            {
                sol::error err = r;
                URHO3D_LOGERROR("LuaRmlUIComponent onInit failed: {}", err.what());
            }
        }
    }

    /// Forward the per-frame tick to the Lua "update" handler when present.
    void Update(float timeStep) override
    {
        RmlUIComponent::Update(timeStep);
        if (!handlers_ || !luaState_)
            return;
        sol::state_view lua(luaState_);
        sol::object handler = (*handlers_)["update"];
        if (handler.valid() && handler.get_type() == sol::type::function)
        {
            sol::protected_function fn = handler;
            sol::protected_function_result r = fn(WrapLuaObject(lua, this), timeStep);
            if (!r.valid())
            {
                sol::error err = r;
                URHO3D_LOGERROR("LuaRmlUIComponent update failed: {}", err.what());
            }
        }
    }

private:
    lua_State* luaState_ = nullptr;
    sol::optional<sol::table> handlers_;
    /// Persistent data-model storage. ea::map keeps element addresses stable so the
    /// raw pointers handed to BindDataModelVariant* stay valid for the model's lifetime.
    ea::map<ea::string, Variant> variantSlots_;
    ea::map<ea::string, VariantVector> vectorSlots_;
    ea::map<ea::string, VariantMap> mapSlots_;
};

void RegisterRmlUIBindings(sol::state& lua, Context* context)
{
    // Make the trampoline creatable via Node:CreateComponent("LuaRmlUIComponent").
    LuaRmlUIComponent::RegisterObject(context);

    // RmlUI subsystem (master instance reachable through GetSubsystem("RmlUI")).
    {
        using LUA_THIS = RmlUI;
        LUA_CLASS(RmlUI, sol::no_constructor
            LUA_BASES(Object)
            LUA_MEMBER_FUNC(SetDebuggerVisible)
            LUA_MEMBER_FUNC(IsDebuggerVisible)
            LUA_MEMBER_FUNC_RAW(LoadFont, [](RmlUI* ui, const char* font, sol::optional<bool> fallback) -> bool {
                return ui ? ui->LoadFont(font, fallback.value_or(false)) : false;
            })
            LUA_MEMBER_FUNC(ReloadFonts)
            LUA_MEMBER_FUNC(SetScale)
            LUA_MEMBER_FUNC(GetScale)
            LUA_MEMBER_FUNC(IsInputCaptured)
            LUA_MEMBER_FUNC(IsHovered)
        );
    }
    RegisterLuaObjectWrapper<RmlUI>();

    // RmlCanvasComponent: renders an off-screen RmlUI into a texture (windows on 3D objects).
    {
        using LUA_THIS = RmlCanvasComponent;
        LUA_CLASS(RmlCanvasComponent, sol::no_constructor
            LUA_BASES(Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetUISize)
            LUA_MEMBER_FUNC(SetTexture)
            LUA_MEMBER_FUNC(GetTexture)
            LUA_MEMBER_FUNC(SetRemapMousePos)
            LUA_MEMBER_FUNC(GetRemapMousePos)
            LUA_MEMBER_FUNC(SetClearColor)
            LUA_MEMBER_FUNC(GetUI)
        );
    }
    RegisterLuaObjectWrapper<RmlCanvasComponent>();

    // RmlUIComponent base: window placement / font size / document access. Registered
    // before the derived trampoline because sol3 requires base usertypes to exist first.
    {
        using LUA_THIS = RmlUIComponent;
        LUA_CLASS(RmlUIComponent, sol::no_constructor
            LUA_BASES(Component, Serializable, Object)
            LUA_MEMBER_FUNC_RAW(SetResource, [](RmlUIComponent* component, const char* path) {
                if (component)
                    component->SetResource(ea::string(path));
            })
            LUA_MEMBER_FUNC(SetUseNormalizedCoordinates)
            LUA_MEMBER_FUNC(GetUseNormalizedCoordinates)
            LUA_MEMBER_FUNC(SetPosition)
            LUA_MEMBER_FUNC(GetPosition)
            LUA_MEMBER_FUNC(SetSize)
            LUA_MEMBER_FUNC(GetSize)
            LUA_MEMBER_FUNC(SetAutoSize)
            LUA_MEMBER_FUNC(GetAutoSize)
            LUA_MEMBER_FUNC(SetEmSize)
            LUA_MEMBER_FUNC(GetEmSize)
            LUA_MEMBER_FUNC(IsModal)
            LUA_MEMBER_FUNC(SetModal)
            LUA_MEMBER_FUNC(Focus)
            LUA_MEMBER_FUNC(GetUI)
        );
    }
    RegisterLuaObjectWrapper<RmlUIComponent>();

    // The Lua-facing trampoline. Adds the handler/data-model surface on top of the base.
    {
        using LUA_THIS = LuaRmlUIComponent;
        LUA_CLASS(LuaRmlUIComponent, sol::no_constructor
            LUA_BASES(RmlUIComponent, Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetHandlers)
            LUA_MEMBER_FUNC(BindProperty)
            LUA_MEMBER_FUNC(BindReadonlyProperty)
            LUA_MEMBER_FUNC(BindEvent)
            LUA_MEMBER_FUNC(BindVariant)
            LUA_MEMBER_FUNC(BindVariantVector)
            LUA_MEMBER_FUNC(BindVariantMap)
            LUA_MEMBER_FUNC(SetVariant)
            LUA_MEMBER_FUNC(SetVariantVector)
            LUA_MEMBER_FUNC(SetVariantMap)
            LUA_MEMBER_FUNC(MarkDirty)
        );
    }
    RegisterLuaObjectWrapper<LuaRmlUIComponent>();
}

} // namespace Urho3D

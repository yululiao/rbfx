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
#include "../Urho3D/Input/Input.h"
#include "../Urho3D/Input/InputConstants.h"
#include "../Urho3D/Input/InputMap.h"
#include "../Urho3D/Input/MoveAndOrbitComponent.h"
#include "../Urho3D/Input/MoveAndOrbitController.h"

#include <sol/sol.hpp>

namespace sol
{

template <> struct is_automagical<Urho3D::Input> : std::false_type {};
template <> struct is_automagical<Urho3D::TouchState> : std::false_type {};
template <> struct is_automagical<Urho3D::MoveAndOrbitController> : std::false_type {};
template <> struct is_automagical<Urho3D::MoveAndOrbitComponent> : std::false_type {};
template <> struct is_automagical<Urho3D::InputMap> : std::false_type {};

} // namespace sol

namespace Urho3D
{

namespace
{

// Input key helpers registered as plain functions: the constants live in
// plain enums and are cheap to mirror.
struct InputConstants
{
};

}

void RegisterInputBindings(sol::state& lua, Context* context)
{
    {
        using RBFX_THIS = Input;
        RBFX_USERTYPE(Input, sol::no_constructor
            RBFX_BASES(Object)

            // Key & mouse queries
            RBFX_M(GetKeyDown)
            RBFX_M(GetKeyPress)
            // MouseButton args arrive as Lua numbers; FlagSet<MouseButton> is
            // not directly convertible so the cast happens here.
            RBFX_RAW(GetMouseButtonDown, [](Input* input, int button) {
                return input && input->GetMouseButtonDown(static_cast<MouseButton>(button));
            })
            RBFX_RAW(GetMouseButtonPress, [](Input* input, int button) {
                return input && input->GetMouseButtonPress(static_cast<MouseButton>(button));
            })
            RBFX_M(GetMousePosition)
            RBFX_M(GetMouseMove)
            RBFX_M(GetMouseMoveX)
            RBFX_M(GetMouseMoveY)
            // Wheel scrolling (49/50 zoom-to-cursor) and mouse re-centering
            // (16_Chat style mouse lock helpers).
            RBFX_M(GetMouseMoveWheel)
            RBFX_M(CenterMousePosition)
            RBFX_M(GetKeyFromName)
            RBFX_RAW(GetQualifierDown, [](Input* input, int qualifier) {
                return input && input->GetQualifierDown(static_cast<Qualifier>(qualifier));
            })

            // Mouse state management. The suppressEvent parameters are optional
            // in C++ and must be mirrored with sol::optional for Lua calls.
            RBFX_RAW(SetMouseVisible, [](Input* input, bool enable, sol::optional<bool> suppressEvent) {
                if (input)
                    input->SetMouseVisible(enable, suppressEvent.value_or(false));
            })
            RBFX_M(IsMouseVisible)
            RBFX_RAW(SetMouseMode, [](Input* input, int mode, sol::optional<bool> suppressEvent) {
                if (input)
                    input->SetMouseMode(static_cast<MouseMode>(mode), suppressEvent.value_or(false));
            })
            RBFX_M(GetMouseMode)
            RBFX_RAW(SetMouseGrabbed, [](Input* input, bool grab, sol::optional<bool> suppressEvent) {
                if (input)
                    input->SetMouseGrabbed(grab, suppressEvent.value_or(false));
            })
            RBFX_M(IsMouseLocked)
            RBFX_M(IsMouseGrabbed)

            // Misc
            RBFX_M(GetNumTouches)
            RBFX_M(GetTouch)
            RBFX_M(GetNumJoysticks)
            RBFX_M(IsMinimized)
            RBFX_M(SetToggleFullscreen)
        );
    }
    RegisterLuaObjectWrapper<Input>();

    // TouchState: plain struct describing one finger. Fetched through
    // Input:GetTouch(index), never constructed from Lua (37_UIDrag).
    {
        using RBFX_THIS = TouchState;
        RBFX_USERTYPE(TouchState, sol::no_constructor
            RBFX_RAW(touchID, &TouchState::touchID_)
            RBFX_RAW(position, &TouchState::position_)
            RBFX_RAW(lastPosition, &TouchState::lastPosition_)
            RBFX_RAW(delta, &TouchState::delta_)
            RBFX_RAW(pressure, &TouchState::pressure_)
        );
    }

    // MoveAndOrbitController: optional WASD+orbit camera helper component
    // (46_RaycastVehicle). Created through Node:CreateComponent, configured
    // by loading an input map resource.
    {
        using RBFX_THIS = MoveAndOrbitController;
        RBFX_USERTYPE(MoveAndOrbitController, sol::no_constructor
            RBFX_BASES(Component, Serializable, Object)
            RBFX_RAW(LoadInputMap, [](MoveAndOrbitController* controller, const char* name) {
                if (controller)
                    controller->LoadInputMap(name);
            })
            RBFX_M(GetInputMap)
        );
    }
    RegisterLuaObjectWrapper<MoveAndOrbitController>();

    // MoveAndOrbitComponent: movement/orbit state updated by the controller.
    // The Lua vehicle logic (46_RaycastVehicle) reads velocity/yaw/pitch from
    // it every frame.
    {
        using RBFX_THIS = MoveAndOrbitComponent;
        RBFX_USERTYPE(MoveAndOrbitComponent, sol::no_constructor
            RBFX_BASES(Component, Serializable, Object)
            RBFX_M(SetVelocity)
            RBFX_M(SetYaw)
            RBFX_M(SetPitch)
            RBFX_M(GetVelocity)
            RBFX_M(GetYaw)
            RBFX_M(GetPitch)
            RBFX_M(GetYawPitchRotation)
        );
    }
    RegisterLuaObjectWrapper<MoveAndOrbitComponent>();

    // InputMap: named action evaluation from a loaded input map resource
    // (46_RaycastVehicle braking).
    {
        using RBFX_THIS = InputMap;
        RBFX_USERTYPE(InputMap, sol::no_constructor
            RBFX_BASES(Resource, Object)
            RBFX_RAW(Evaluate, [](InputMap* inputMap, const char* name) -> float {
                return inputMap ? inputMap->Evaluate(name) : 0.0f;
            })
        );
    }
    RegisterLuaObjectWrapper<InputMap>();

    // Keyboard constants (subset used by samples; SDL keycodes are lowercase
    // ASCII which Lua can produce with string.byte for the rest).
    sol::table key = lua.create_named_table("KEY");
    key["ESC"] = KEY_ESCAPE;
    key["RETURN"] = KEY_RETURN;
    key["TAB"] = KEY_TAB;
    key["SPACE"] = KEY_SPACE;
    key["LEFT"] = KEY_LEFT;
    key["RIGHT"] = KEY_RIGHT;
    key["UP"] = KEY_UP;
    key["DOWN"] = KEY_DOWN;
    key["PAGEUP"] = KEY_PAGEUP;
    key["PAGEDOWN"] = KEY_PAGEDOWN;
    key["HOME"] = KEY_HOME;
    key["END"] = KEY_END;
    key["INSERT"] = KEY_INSERT;
    key["DELETE"] = KEY_DELETE;
    key["F1"] = KEY_F1;
    key["F2"] = KEY_F2;
    key["F3"] = KEY_F3;
    key["F4"] = KEY_F4;
    key["F5"] = KEY_F5;
    key["F6"] = KEY_F6;
    key["F7"] = KEY_F7;
    key["F8"] = KEY_F8;
    key["F9"] = KEY_F9;
    key["F10"] = KEY_F10;
    key["F11"] = KEY_F11;
    key["F12"] = KEY_F12;
    key["SHIFT"] = KEY_LSHIFT;
    key["CTRL"] = KEY_LCTRL;
    key["ALT"] = KEY_LALT;
    // Alphanumeric keys: SDL keycodes for a-z / 0-9 equal their ASCII codes.
    for (int c = 'a'; c <= 'z'; ++c)
    {
        const char name[2] = { static_cast<char>(c - 'a' + 'A'), '\0' };
        key[name] = c;
    }
    for (int c = '0'; c <= '9'; ++c)
    {
        const char name[2] = { static_cast<char>(c), '\0' };
        key[name] = c;
    }

    // Mouse buttons.
    RBFX_ENUM_TABLE(MOUSEB, "LEFT", MOUSEB_LEFT, "RIGHT", MOUSEB_RIGHT, "MIDDLE", MOUSEB_MIDDLE,
        "X1", MOUSEB_X1, "X2", MOUSEB_X2);

    // Mouse modes.
    RBFX_ENUM_TABLE(MM, "ABSOLUTE", MM_ABSOLUTE, "RELATIVE", MM_RELATIVE, "WRAP", MM_WRAP,
        "FREE", MM_FREE, "INVALID", MM_INVALID);

    // Keyboard qualifiers (InputConstants.h).
    RBFX_ENUM_TABLE(QUAL, "NONE", QUAL_NONE, "SHIFT", QUAL_SHIFT, "CTRL", QUAL_CTRL, "ALT", QUAL_ALT,
        "ANY", QUAL_ANY);
}

} // namespace Urho3D

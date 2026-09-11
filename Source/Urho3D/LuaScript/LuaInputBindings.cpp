//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#include "../Precompiled.h"

#include "../LuaScript/LuaBindings.h"

#include "../Core/Context.h"
#include "../Input/Input.h"
#include "../Input/InputConstants.h"
#include "../Input/InputMap.h"
#include "../Input/MoveAndOrbitComponent.h"
#include "../Input/MoveAndOrbitController.h"

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
    lua.new_usertype<Input>("Input",
        sol::no_constructor,
        sol::base_classes, sol::bases<Object>(),

        // Key & mouse queries
        "GetKeyDown", &Input::GetKeyDown,
        "GetKeyPress", &Input::GetKeyPress,
        // MouseButton args arrive as Lua numbers; FlagSet<MouseButton> is
        // not directly convertible so the cast happens here.
        "GetMouseButtonDown", [](Input* input, int button) {
            return input && input->GetMouseButtonDown(static_cast<MouseButton>(button));
        },
        "GetMouseButtonPress", [](Input* input, int button) {
            return input && input->GetMouseButtonPress(static_cast<MouseButton>(button));
        },
        "GetMousePosition", &Input::GetMousePosition,
        "GetMouseMove", &Input::GetMouseMove,
        "GetMouseMoveX", &Input::GetMouseMoveX,
        "GetMouseMoveY", &Input::GetMouseMoveY,
        // Wheel scrolling (49/50 zoom-to-cursor) and mouse re-centering
        // (16_Chat style mouse lock helpers).
        "GetMouseMoveWheel", &Input::GetMouseMoveWheel,
        "CenterMousePosition", &Input::CenterMousePosition,
        "GetKeyFromName", &Input::GetKeyFromName,
        "GetQualifierDown", [](Input* input, int qualifier) {
            return input && input->GetQualifierDown(static_cast<Qualifier>(qualifier));
        },

        // Mouse state management. The suppressEvent parameters are optional
        // in C++ and must be mirrored with sol::optional for Lua calls.
        "SetMouseVisible", [](Input* input, bool enable, sol::optional<bool> suppressEvent) {
            if (input)
                input->SetMouseVisible(enable, suppressEvent.value_or(false));
        },
        "IsMouseVisible", &Input::IsMouseVisible,
        "SetMouseMode", [](Input* input, int mode, sol::optional<bool> suppressEvent) {
            if (input)
                input->SetMouseMode(static_cast<MouseMode>(mode), suppressEvent.value_or(false));
        },
        "GetMouseMode", &Input::GetMouseMode,
        "SetMouseGrabbed", [](Input* input, bool grab, sol::optional<bool> suppressEvent) {
            if (input)
                input->SetMouseGrabbed(grab, suppressEvent.value_or(false));
        },
        "IsMouseLocked", &Input::IsMouseLocked,
        "IsMouseGrabbed", &Input::IsMouseGrabbed,

        // Misc
        "GetNumTouches", &Input::GetNumTouches,
        "GetTouch", &Input::GetTouch,
        "GetNumJoysticks", &Input::GetNumJoysticks,
        "IsMinimized", &Input::IsMinimized,
        "SetToggleFullscreen", &Input::SetToggleFullscreen
    );
    RegisterLuaObjectWrapper<Input>();

    // TouchState: plain struct describing one finger. Fetched through
    // Input:GetTouch(index), never constructed from Lua (37_UIDrag).
    lua.new_usertype<TouchState>("TouchState",
        sol::no_constructor,
        "touchID", &TouchState::touchID_,
        "position", &TouchState::position_,
        "lastPosition", &TouchState::lastPosition_,
        "delta", &TouchState::delta_,
        "pressure", &TouchState::pressure_
    );

    // MoveAndOrbitController: optional WASD+orbit camera helper component
    // (46_RaycastVehicle). Created through Node:CreateComponent, configured
    // by loading an input map resource.
    lua.new_usertype<MoveAndOrbitController>("MoveAndOrbitController",
        sol::no_constructor,
        sol::base_classes, sol::bases<Component, Serializable, Object>(),
        "LoadInputMap", [](MoveAndOrbitController* controller, const char* name) {
            if (controller)
                controller->LoadInputMap(name);
        },
        "GetInputMap", &MoveAndOrbitController::GetInputMap
    );
    RegisterLuaObjectWrapper<MoveAndOrbitController>();

    // MoveAndOrbitComponent: movement/orbit state updated by the controller.
    // The Lua vehicle logic (46_RaycastVehicle) reads velocity/yaw/pitch from
    // it every frame.
    lua.new_usertype<MoveAndOrbitComponent>("MoveAndOrbitComponent",
        sol::no_constructor,
        sol::base_classes, sol::bases<Component, Serializable, Object>(),
        "SetVelocity", &MoveAndOrbitComponent::SetVelocity,
        "SetYaw", &MoveAndOrbitComponent::SetYaw,
        "SetPitch", &MoveAndOrbitComponent::SetPitch,
        "GetVelocity", &MoveAndOrbitComponent::GetVelocity,
        "GetYaw", &MoveAndOrbitComponent::GetYaw,
        "GetPitch", &MoveAndOrbitComponent::GetPitch,
        "GetYawPitchRotation", &MoveAndOrbitComponent::GetYawPitchRotation
    );
    RegisterLuaObjectWrapper<MoveAndOrbitComponent>();

    // InputMap: named action evaluation from a loaded input map resource
    // (46_RaycastVehicle braking).
    lua.new_usertype<InputMap>("InputMap",
        sol::no_constructor,
        sol::base_classes, sol::bases<Resource, Object>(),
        "Evaluate", [](InputMap* inputMap, const char* name) -> float {
            return inputMap ? inputMap->Evaluate(name) : 0.0f;
        }
    );
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
    sol::table mouseb = lua.create_named_table("MOUSEB");
    mouseb["LEFT"] = MOUSEB_LEFT;
    mouseb["RIGHT"] = MOUSEB_RIGHT;
    mouseb["MIDDLE"] = MOUSEB_MIDDLE;
    mouseb["X1"] = MOUSEB_X1;
    mouseb["X2"] = MOUSEB_X2;

    // Mouse modes.
    sol::table mm = lua.create_named_table("MM");
    mm["ABSOLUTE"] = MM_ABSOLUTE;
    mm["RELATIVE"] = MM_RELATIVE;
    mm["WRAP"] = MM_WRAP;
    mm["FREE"] = MM_FREE;
    mm["INVALID"] = MM_INVALID;

    // Keyboard qualifiers (InputConstants.h).
    sol::table qual = lua.create_named_table("QUAL");
    qual["NONE"] = QUAL_NONE;
    qual["SHIFT"] = QUAL_SHIFT;
    qual["CTRL"] = QUAL_CTRL;
    qual["ALT"] = QUAL_ALT;
    qual["ANY"] = QUAL_ANY;
}

} // namespace Urho3D

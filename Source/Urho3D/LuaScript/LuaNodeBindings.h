//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#pragma once

#include "../LuaScript/LuaBindings.h"

namespace Urho3D
{

class Context;

/// Register scene graph types (Node, Scene, Component, prefabs) to Lua.
void RegisterNodeBindings(sol::state& lua, Context* context);

} // namespace Urho3D

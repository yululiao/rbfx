// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#pragma once

#include "../Urho3D/Scene/Component.h"

#include "Export.h"

namespace Urho3D
{

/// Component that holds the resource name of the game's entry Lua script.
///
/// It is serialized into scene files, so every host that loads such a scene
/// must be able to construct it: both the editor and the standalone
/// LuaGamePlayer. That is why it lives in the shared RbfxLuaScript module
/// rather than the editor-only library. When a host loads a scene containing
/// this component it reads GetScriptPath() and executes the referenced Lua
/// file as a resource, which then wires up the rest of the game logic.
///
/// The stored value is a resource name such as "Scripts/main.lua", i.e. relative
/// to whatever directory the host mounted as its data root, never an absolute
/// path - a scene travels with the project and must not name a location on one
/// developer's disk.
class RBFXLUA_API LuaGameScript : public Component
{
    URHO3D_OBJECT(LuaGameScript, Component);

public:
    explicit LuaGameScript(Context* context);
    ~LuaGameScript() override;

    static void RegisterObject(Context* context);

    void SetScriptPath(const ea::string& path);
    const ea::string& GetScriptPath() const { return scriptPath_; }

private:
    ea::string scriptPath_;
};

} // namespace Urho3D

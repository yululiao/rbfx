// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#pragma once

#include <Urho3D/Scene/Component.h>

namespace Urho3D
{

/// Component that holds a path to a Lua script file.
/// When the editor enters Play mode, GameView finds this component in the
/// scene and executes the referenced Lua script.
class LuaGameScript : public Component
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

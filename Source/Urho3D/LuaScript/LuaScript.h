//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#pragma once

#include "../Core/Object.h"

#include <EASTL/unique_ptr.h>

namespace Urho3D
{

class Node;

}

struct lua_State;

namespace sol
{

class state;

}

namespace Urho3D
{

/// Lua scripting subsystem.
class URHO3D_API LuaScript : public Object
{
    URHO3D_OBJECT(LuaScript, Object);

public:
    /// Construct.
    explicit LuaScript(Context* context);
    /// Destruct.
    ~LuaScript() override;

    /// Initialize Lua state and register engine bindings.
    bool Initialize();

    /// Execute Lua code from a string.
    bool ExecuteString(const ea::string& code, const ea::string& chunkName = EMPTY_STRING);
    /// Execute Lua code from a resource file.
    bool ExecuteFile(const ea::string& fileName);

    /// Expose a Node (or a Scene subclass) as a global Lua variable.
    void SetGlobalNode(const ea::string& name, Node* node);

    /// Return sol state.
    sol::state& GetState();
    /// Return raw Lua state.
    lua_State* GetLuaState() const;

private:
    /// Register Urho3D types available to Lua.
    void RegisterEngineBindings();

    /// Lua virtual machine state.
    ea::unique_ptr<sol::state> luaState_;
};

} // namespace Urho3D

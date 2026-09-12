// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#include "LuaGameScript.h"

#include <Urho3D/Core/ObjectCategory.h>

namespace Urho3D
{

LuaGameScript::LuaGameScript(Context* context)
    : Component(context)
{
}

LuaGameScript::~LuaGameScript() = default;

void LuaGameScript::RegisterObject(Context* context)
{
    context->AddFactoryReflection<LuaGameScript>(Category_Scene);

    URHO3D_ATTRIBUTE("Script Path", ea::string, scriptPath_, EMPTY_STRING, AM_DEFAULT);
}

void LuaGameScript::SetScriptPath(const ea::string& path)
{
    scriptPath_ = path;
}

} // namespace Urho3D

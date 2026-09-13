// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#include "../Urho3D/Precompiled.h"

#include "LuaGameScript.h"

#include "../Urho3D/Core/Context.h"
#include "../Urho3D/Core/ObjectCategory.h"

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

    // The FileFilter metadata tells the inspector this string is a lua file path, so it renders a
    // native browse button; the stored value is a resource name relative to the mounted data
    // directory (e.g. "Scripts/main.lua"), which is exactly how every host resolves it - through
    // the VFS, so the same string works in the editor, in the standalone player and in a package.
    URHO3D_ATTRIBUTE("Script Path", ea::string, scriptPath_, EMPTY_STRING, AM_DEFAULT)
        .SetMetadata(AttributeMetadata::FileFilter, "lua");
}

void LuaGameScript::SetScriptPath(const ea::string& path)
{
    scriptPath_ = path;
}

} // namespace Urho3D

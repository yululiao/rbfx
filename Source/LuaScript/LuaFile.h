//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#pragma once

#include "../Urho3D/Resource/Resource.h"

#include "Export.h"

#include <EASTL/vector.h>

#include <cstdint>

struct lua_State;

namespace Urho3D
{

/// A Lua chunk stored as an engine resource.
///
/// Making scripts resources is what lets require() and top-level execution share one path to
/// the bytes: the VFS resolves the name (directory, .pak, APK assets), ResourceCache owns the
/// decoded payload, and file watching can invalidate exactly this object. Scripts therefore
/// participate in the same reload and memory-accounting pipeline as every other asset, which
/// is also why a plain package.path based require() could never have worked in a shipping build.
///
/// Loading a .lua file never fails on content: a syntax error is only discovered when the
/// chunk is compiled by LoadChunk(), which is a per-VM decision - the same file may be loaded
/// into the editor VM and the game VM.
class RBFXLUA_API LuaFile : public Resource
{
    URHO3D_OBJECT(LuaFile, Resource);

public:
    explicit LuaFile(Context* context);
    ~LuaFile() override;

    /// Register the resource type. Safe to call repeatedly on the same context.
    static void RegisterObject(Context* context);

    /// Implement Resource. Decrypts and unpacks the container if the file is packaged.
    /// @{
    bool BeginLoad(Deserializer& source) override;
    /// @}

    /// Decode this chunk into the given Lua state without executing it. On success the Lua
    /// function is left on top of the stack and the caller owns it. On failure the stack is
    /// left untouched and \p error describes the problem.
    bool LoadChunk(lua_State* L, ea::string& error) const;

    /// Payload bytes after decryption: either Lua source text or Lua 5.4 bytecode.
    const ea::vector<uint8_t>& GetData() const { return data_; }
    /// True when the payload is bytecode rather than source text. Bytecode is only valid for
    /// the Lua version and object model that produced it.
    bool IsBytecode() const { return bytecode_; }
    /// Bumped on every successful load, including automatic reloads. The package loader uses
    /// it to tell a cached module from one whose bytes changed underneath it.
    unsigned GetLoadSerial() const { return loadSerial_; }

private:
    /// Decoded payload.
    ea::vector<uint8_t> data_;
    /// Whether the payload is precompiled bytecode.
    bool bytecode_ = false;
    /// Load counter, see GetLoadSerial.
    unsigned loadSerial_ = 0;
};

} // namespace Urho3D

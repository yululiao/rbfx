//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#pragma once

#include "Export.h"

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <cstddef>
#include <cstdint>

namespace Urho3D
{

/// Container format for shipped Lua scripts, and the single place where a script
/// resource turns into bytes that can be handed to the Lua VM.
///
/// This is the seam the whole Lua asset pipeline hangs on: LuaFile only ever calls
/// LuaScriptContainerUnpack(), the offline bundler only ever calls Pack(). Runtime and
/// tooling share these functions (the tool links this module), so the format cannot drift.
///
/// Layout, all integers little-endian:
///   offset 0   char[4]  magic 'R','L','U','C'
///   offset 4   uint8    format version (currently 1)
///   offset 5   uint8    payload kind (LuaScriptPayload)
///   offset 6   uint8    cipher kind (LuaScriptCipher)
///   offset 7   uint8    flags, reserved (0)
///   offset 8   uint32   payload size in bytes after unpacking
///   offset 12  uint8[16] CTR initial counter block
///   offset 28  payload  (AES-256-CTR ciphertext when cipher is Aes256Ctr, raw bytes otherwise)
///
/// CTR does not pad, so the stored size equals the payload size; a mismatch is reported as
/// corruption instead of being silently tolerated.
///
/// Threat model, as agreed: this raises the cost of casual repacking above "open the file in
/// a text editor". It is not DRM. The key is compiled into the binary and can be overridden
/// by the environment, which is exactly what a determined attacker defeats in minutes; what
/// it does stop is the person who just wants to read or tweak game logic in a shipped build.
enum class LuaScriptPayload : uint8_t
{
    /// Plain Lua source text. Always safe to ship to every platform.
    Source = 0,
    /// Lua 5.4 bytecode produced by LuaScriptCompileToBytecode(). Not portable across Lua
    /// versions and formally not guaranteed across architectures, so a bytecode build must
    /// be produced per target platform.
    Bytecode = 1,
};

enum class LuaScriptCipher : uint8_t
{
    /// Bytes are stored as-is. Used by development builds and by the self test.
    Plain = 0,
    /// AES-256-CTR with the key from LuaScriptContainerSetKey() / the environment.
    Aes256Ctr = 1,
};

/// Size of the container header in bytes.
static constexpr size_t LuaScriptContainerHeaderSize = 28;

/// Size of the content key in bytes. Spelled out here so that callers of SetKey() do not have
/// to include the cipher header, which is an implementation detail of this module.
static constexpr size_t LuaScriptContainerKeyBytes = 32;

/// True when the buffer starts with a container header. Plain .lua text is never packaged,
/// so callers use this to decide whether unpacking is required at all.
RBFXLUA_API bool LuaScriptContainerIsPackaged(const void* data, size_t size);

/// Decode a container into its usable bytes. \p out receives payload bytes on success.
/// On failure \p error carries a human readable reason and \p out is left empty.
RBFXLUA_API bool LuaScriptContainerUnpack(
    const void* data, size_t size, ea::vector<uint8_t>& out, ea::string& error);

/// Encode payload bytes into a container. \p ivSeed differentiates the keystream between
/// files; pass LuaScriptContainerNameSeed() of the resource name so that two files never
/// share a counter sequence.
RBFXLUA_API bool LuaScriptContainerPack(const void* payload, size_t size, LuaScriptPayload kind,
    LuaScriptCipher cipher, uint32_t ivSeed, ea::vector<uint8_t>& out, ea::string& error);

/// Stable 32 bit hash of a resource name, used as the container IV seed. Deterministic so
/// that a rebuild of the same inputs produces byte-identical output.
RBFXLUA_API uint32_t LuaScriptContainerNameSeed(const char* resourceName);

/// Override the 32 byte content key. Call before any encrypted resource is loaded; hosts
/// that need it (dev builds loading a clear-text tree, tests) are the only ones that should.
RBFXLUA_API void LuaScriptContainerSetKey(const uint8_t* key32);

/// Return the key currently in use, after the environment override has been resolved.
/// Exposed so a bundler running in-process can verify it matches what it was given.
RBFXLUA_API const uint8_t* LuaScriptContainerGetKey();

/// Where the key in use came from. A key that does not match what was packaged looks like a
/// corrupt file to everything above the codec, so the origin is reportable on its own.
enum class LuaScriptContainerKeySource
{
    /// The key compiled into the binary, -DRBFX_LUA_SCRIPT_KEY_HEX.
    Compiled,
    /// The RBFX_LUA_SCRIPT_KEY environment variable.
    Environment,
    /// RBFX_LUA_SCRIPT_KEY was present but did not parse, so the compiled key is in use. Almost
    /// certainly a typo, which is why it is not folded into Compiled.
    RejectedEnvironment,
    /// A key handed to LuaScriptContainerSetKey() in this process.
    Explicit,
};

/// Report the origin of the key in use, resolving the environment lookup if that has not
/// happened yet.
RBFXLUA_API LuaScriptContainerKeySource LuaScriptContainerGetKeySource();

/// Run the NIST SP 800-38A AES-256 known-answer tests (ECB and CTR) plus a container
/// round trip. Returns false and fills \p error when the cipher or the format is broken.
/// Called automatically before the first unpack; exposed for tests and for the bundler.
RBFXLUA_API bool LuaScriptContainerSelfTest(ea::string& error);

/// Compile Lua source to bytecode of the linked Lua version. Implemented in its own
/// translation unit because the vendored lua.h has no extern "C" block and must not be
/// mixed with sol3's includes in the same TU.
/// \p strip drops debug info; keep false so runtime errors still carry line numbers.
RBFXLUA_API bool LuaScriptCompileToBytecode(
    const void* source, size_t size, const char* chunkName, bool strip, ea::vector<uint8_t>& out, ea::string& error);

} // namespace Urho3D

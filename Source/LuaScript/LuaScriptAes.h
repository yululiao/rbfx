//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#pragma once

#include <cstddef>
#include <cstdint>

namespace Urho3D
{

/// Minimal AES-256 forward cipher with a CTR mode wrapper.
///
/// Deliberately encryption-only: Lua script payloads are protected with AES-CTR, and CTR
/// derives its keystream from the *forward* cipher alone, so no InvSubBytes / inverse key
/// schedule exists here. That keeps the reviewed surface small.
///
/// Why not OpenSSL, which the tree already vendors under Source/ThirdParty/openssl:
///   * Those crypto/ssl targets are only consumed by Civetweb today; hanging them off
///     RbfxLuaScript would spread a heavy third-party dependency into every Lua host
///     (editor, player, samples, tests) and onto platforms where the prebuilt openssl
///     for that target may not exist at all (Emscripten, Android ABIs).
///   * libdatachannel/libsrtp in the same tree explicitly aborts on "ssl conflict" when
///     more than one crypto provider is enabled, so crypto target names are sensitive here.
/// The protection goal is "casual repacking is not enough" (see LuaScriptContainer.h), which
/// AES-256-CTR satisfies regardless of provider. If a FIPS-certified or constant-time
/// implementation is ever required, replace the body of this class only - the container
/// format and every call site stay untouched.
///
/// Correctness is enforced at runtime, not assumed: LuaScriptContainer runs the NIST SP 800-38A
/// appendix F.1.5 (ECB) and F.5.5 (CTR) AES-256 known-answer vectors before the first decrypt.
class LuaScriptAes
{
public:
    static constexpr unsigned KeyBytes = 32;
    static constexpr unsigned BlockBytes = 16;

    /// Expand a 32 byte key into the 15 round keys used by AES-256.
    explicit LuaScriptAes(const uint8_t* key);

    /// Encrypt one 16 byte block in place with the loaded round keys.
    void EncryptBlock(uint8_t* block) const;

    /// XOR the buffer with the CTR keystream starting at \p iv. The same call decrypts,
    /// because CTR keystream generation never depends on the ciphertext. The counter is a
    /// 128 bit big-endian integer (SP 800-38A convention) and \p iv is left unmodified.
    void CtrXcrypt(uint8_t* data, size_t size, const uint8_t* iv) const;

private:
    static constexpr unsigned Rounds = 14;
    /// (Rounds + 1) * BlockBytes, laid out as 60 consecutive 4 byte words.
    uint8_t roundKeys_[(Rounds + 1) * BlockBytes];
};

} // namespace Urho3D

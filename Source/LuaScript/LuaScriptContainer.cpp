//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#include "../Urho3D/Precompiled.h"

#include "LuaScriptContainer.h"

#include "LuaScriptAes.h"
#include "../Urho3D/Core/Mutex.h"
#include "../Urho3D/Core/StringUtils.h"

#include <cstdlib>
#include <cstring>

namespace Urho3D
{

namespace
{

constexpr uint8_t kMagic[4] = { 'R', 'L', 'U', 'C' };
constexpr uint8_t kFormatVersion = 1;
constexpr size_t kIvOffset = 12;

/// Development default. Override for release builds either at configure time with
/// -DRBFX_LUA_SCRIPT_KEY_HEX=<64 hex digits> or at run time with the RBFX_LUA_SCRIPT_KEY
/// environment variable, which lets a dev tree and a shipping tree coexist.
#ifndef RBFX_LUA_SCRIPT_KEY_HEX
#define RBFX_LUA_SCRIPT_KEY_HEX "a3f17c92d6e04b8519a7c3e5f80d2b6471ce9a5d3b8f02e6a1c4d79b5e38f260"
#endif

/// Name of the environment variable holding a 64 hex digit key override.
const char* const kKeyEnvironmentVariable = "RBFX_LUA_SCRIPT_KEY";

bool ParseKeyHex(const char* hex, uint8_t* out)
{
    if (!hex)
        return false;

    size_t digits = 0;
    while (hex[digits] != '\0')
        ++digits;
    if (digits != LuaScriptAes::KeyBytes * 2)
        return false;

    for (size_t i = 0; i < LuaScriptAes::KeyBytes; ++i)
    {
        uint8_t byte = 0;
        for (int nibble = 0; nibble < 2; ++nibble)
        {
            const char c = hex[i * 2 + (size_t)nibble];
            uint8_t value;
            if (c >= '0' && c <= '9')
                value = (uint8_t)(c - '0');
            else if (c >= 'a' && c <= 'f')
                value = (uint8_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                value = (uint8_t)(c - 'A' + 10);
            else
                return false;
            byte = (uint8_t)(byte << 4) | value;
        }
        out[i] = byte;
    }
    return true;
}

uint8_t g_explicitKey[LuaScriptAes::KeyBytes];
bool g_hasExplicitKey = false;

/// Resolve the content key. The environment override is read exactly once: switching the key
/// mid-session would invalidate every resource already sitting in the resource cache.
struct ResolvedKey
{
    uint8_t bytes[LuaScriptAes::KeyBytes] = {};
    LuaScriptContainerKeySource source = LuaScriptContainerKeySource::Compiled;
};

const ResolvedKey& Resolved()
{
    static const ResolvedKey resolved = []
    {
        ResolvedKey out;
        const char* env = getenv(kKeyEnvironmentVariable);
        if (env && *env && ParseKeyHex(env, out.bytes))
        {
            out.source = LuaScriptContainerKeySource::Environment;
            return out;
        }

        // A malformed override must not quietly ship the development key; fall back to it only
        // when no override was given at all, and say which of the two happened. The caller logs,
        // this module does not - it also runs inside the offline bundler where no Log subsystem
        // exists.
        if (!ParseKeyHex(RBFX_LUA_SCRIPT_KEY_HEX, out.bytes))
            memset(out.bytes, 0, sizeof(out.bytes));
        out.source = env && *env ? LuaScriptContainerKeySource::RejectedEnvironment : LuaScriptContainerKeySource::Compiled;
        return out;
    }();
    return resolved;
}

const uint8_t* ActiveKey()
{
    if (g_hasExplicitKey)
        return g_explicitKey;
    return Resolved().bytes;
}

uint32_t ReadLE32(const uint8_t* data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

void WriteLE32(uint8_t* data, uint32_t value)
{
    data[0] = (uint8_t)(value & 0xff);
    data[1] = (uint8_t)((value >> 8) & 0xff);
    data[2] = (uint8_t)((value >> 16) & 0xff);
    data[3] = (uint8_t)((value >> 24) & 0xff);
}

void WriteLE64(uint8_t* data, uint64_t value)
{
    for (int i = 0; i < 8; ++i)
        data[i] = (uint8_t)((value >> (8 * i)) & 0xff);
}

bool HexToBytes(const char* hex, uint8_t* out, size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        uint8_t byte = 0;
        for (int nibble = 0; nibble < 2; ++nibble)
        {
            const char c = hex[i * 2 + (size_t)nibble];
            if (c >= '0' && c <= '9')
                byte = (uint8_t)(byte << 4) | (uint8_t)(c - '0');
            else if (c >= 'a' && c <= 'f')
                byte = (uint8_t)(byte << 4) | (uint8_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                byte = (uint8_t)(byte << 4) | (uint8_t)(c - 'A' + 10);
            else
                return false;
        }
        out[i] = byte;
    }
    return true;
}

/// Fill the counter block from a per-file seed. The IV is not secret, it only has to be
/// unique per file: two files under one key must never share a keystream. Seeding from the
/// resource name hash plus the payload size keeps builds reproducible, and the case that
/// would collide (same name, same size, different content) cannot exist in one package.
void BuildIv(uint8_t* iv, uint32_t seed, size_t size)
{
    WriteLE32(iv, seed);
    WriteLE64(iv + 4, (uint64_t)size);
    WriteLE32(iv + 12, seed ^ 0x9e3779b9u);
}

void FailWith(ea::string& error, const ea::string& message)
{
    error = message;
}

/// Decode without the self test gate. The self test itself packs and unpacks through these,
/// which is what keeps it from deadlocking on its own readiness flag.
bool UnpackImpl(const void* data, size_t size, ea::vector<uint8_t>& out, ea::string& error)
{
    out.clear();
    error.clear();

    if (!LuaScriptContainerIsPackaged(data, size))
    {
        FailWith(error, "not a Lua script container");
        return false;
    }

    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    const uint8_t kind = bytes[5];
    const uint8_t cipher = bytes[6];
    if (kind != (uint8_t)LuaScriptPayload::Source && kind != (uint8_t)LuaScriptPayload::Bytecode)
    {
        FailWith(error, ToString("unknown Lua script payload kind %u", kind));
        return false;
    }
    if (cipher != (uint8_t)LuaScriptCipher::Plain && cipher != (uint8_t)LuaScriptCipher::Aes256Ctr)
    {
        FailWith(error, ToString("unknown Lua script cipher kind %u", cipher));
        return false;
    }

    const uint32_t plainSize = ReadLE32(bytes + 8);
    const size_t storedSize = size - LuaScriptContainerHeaderSize;
    if (storedSize != plainSize)
    {
        FailWith(error, ToString("Lua script container size mismatch: header says %u bytes, %zu present",
                       plainSize, storedSize));
        return false;
    }

    out.resize(plainSize);
    if (plainSize != 0)
        memcpy(out.data(), bytes + LuaScriptContainerHeaderSize, plainSize);

    if (cipher == (uint8_t)LuaScriptCipher::Aes256Ctr)
    {
        LuaScriptAes aes(ActiveKey());
        aes.CtrXcrypt(out.data(), plainSize, bytes + kIvOffset);
    }

    return true;
}

bool PackImpl(const void* payload, size_t size, LuaScriptPayload kind, LuaScriptCipher cipher, uint32_t ivSeed,
    ea::vector<uint8_t>& out, ea::string& error)
{
    out.clear();
    error.clear();

    if (!payload && size != 0)
    {
        FailWith(error, "Lua script container pack: null payload");
        return false;
    }
    if (size > 0xffffffffu)
    {
        FailWith(error, "Lua script container pack: payload too large");
        return false;
    }

    out.resize(LuaScriptContainerHeaderSize + size);
    uint8_t* bytes = out.data();
    memcpy(bytes, kMagic, sizeof(kMagic));
    bytes[4] = kFormatVersion;
    bytes[5] = (uint8_t)kind;
    bytes[6] = (uint8_t)cipher;
    bytes[7] = 0;
    WriteLE32(bytes + 8, (uint32_t)size);

    uint8_t* iv = bytes + kIvOffset;
    BuildIv(iv, ivSeed, size);

    if (size != 0)
        memcpy(bytes + LuaScriptContainerHeaderSize, payload, size);

    if (cipher == LuaScriptCipher::Aes256Ctr)
    {
        LuaScriptAes aes(ActiveKey());
        aes.CtrXcrypt(bytes + LuaScriptContainerHeaderSize, size, iv);
    }

    return true;
}

/// Run a pack/unpack round trip with both cipher kinds and both payload kinds.
bool SelfTestContainer(ea::string& error)
{
    static const char sample[] = "local a = 1\nreturn a + 2\n";
    const size_t sampleSize = sizeof(sample) - 1;

    const LuaScriptPayload kinds[] = { LuaScriptPayload::Source, LuaScriptPayload::Bytecode };
    const LuaScriptCipher ciphers[] = { LuaScriptCipher::Plain, LuaScriptCipher::Aes256Ctr };

    for (const LuaScriptPayload kind : kinds)
    {
        for (const LuaScriptCipher cipher : ciphers)
        {
            ea::vector<uint8_t> packed;
            if (!PackImpl(sample, sampleSize, kind, cipher, 0x11223344u, packed, error))
                return false;

            // Encrypted output must not contain the plain source, that is the whole point.
            if (cipher == LuaScriptCipher::Aes256Ctr && packed.size() >= sampleSize)
            {
                for (size_t i = 0; i + sampleSize <= packed.size(); ++i)
                {
                    if (memcmp(packed.data() + i, sample, sampleSize) == 0)
                    {
                        FailWith(error, "Lua script container self test: ciphertext still contains the plaintext");
                        return false;
                    }
                }
            }

            ea::vector<uint8_t> unpacked;
            if (!UnpackImpl(packed.data(), packed.size(), unpacked, error))
                return false;
            if (unpacked.size() != sampleSize || memcmp(unpacked.data(), sample, sampleSize) != 0)
            {
                FailWith(error, "Lua script container self test: round trip mismatch");
                return false;
            }

            // A truncated container has to be rejected, not loaded as garbage.
            ea::vector<uint8_t> ignored;
            ea::string expectedFailure;
            if (UnpackImpl(packed.data(), packed.size() - 1, ignored, expectedFailure))
            {
                FailWith(error, "Lua script container self test: truncated container was accepted");
                return false;
            }
        }
    }

    return true;
}

/// Correctness gate for the cipher, using published known-answer vectors so a broken
/// implementation cannot hide behind a matching encrypt/decrypt pair of its own making.
bool SelfTestCipher(ea::string& error)
{
    // NIST SP 800-38A appendix F.1.5, ECB-AES256.Encrypt, first block: the forward cipher on its
    // own. The CTR vector below then covers the same key in mode form.
    static const char keyHex[] = "603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4";
    static const char plainHex[] = "6bc1bee22e409f96e93d7e117393172a";
    static const char expectedHex[] = "f3eed1bdb5d2a03c064b5a7e3db181f8";

    uint8_t key[32];
    uint8_t block[16];
    uint8_t expected[16];
    if (!HexToBytes(keyHex, key, 32) || !HexToBytes(plainHex, block, 16) || !HexToBytes(expectedHex, expected, 16))
    {
        FailWith(error, "Lua script AES self test: malformed test vector");
        return false;
    }

    LuaScriptAes aes(key);
    aes.EncryptBlock(block);
    if (memcmp(block, expected, 16) != 0)
    {
        FailWith(error, "Lua script AES self test: SP 800-38A AES-256 ECB known answer failed");
        return false;
    }

    // NIST SP 800-38A F.5.5, CTR-AES256 encrypt of four blocks.
    static const char counterHex[] = "f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff";
    static const char ctrPlainHex[] = "6bc1bee22e409f96e93d7e117393172a"
                                      "ae2d8a571e03ac9c9eb76fac45af8e51"
                                      "30c81c46a35ce411e5fbc1191a0a52ef"
                                      "f69f2445df4f9b17ad2b417be66c3710";
    static const char ctrCipherHex[] = "601ec313775789a5b7a7f504bbf3d228"
                                       "f443e3ca4d62b59aca84e990cacaf5c5"
                                       "2b0930daa23de94ce87017ba2d84988d"
                                       "dfc9c58db67aada613c2dd08457941a6";

    uint8_t counter[16];
    uint8_t data[64];
    uint8_t want[64];
    uint8_t restored[64];
    if (!HexToBytes(counterHex, counter, 16) || !HexToBytes(ctrPlainHex, data, 64)
        || !HexToBytes(ctrCipherHex, want, 64))
    {
        FailWith(error, "Lua script AES self test: malformed CTR test vector");
        return false;
    }

    aes.CtrXcrypt(data, 64, counter);
    if (memcmp(data, want, 64) != 0)
    {
        FailWith(error, "Lua script AES self test: SP 800-38A CTR known answer failed");
        return false;
    }

    // CTR is its own inverse; decrypting with the same IV must restore the input.
    aes.CtrXcrypt(data, 64, counter);
    HexToBytes(ctrPlainHex, restored, 64);
    if (memcmp(data, restored, 64) != 0)
    {
        FailWith(error, "Lua script AES self test: CTR is not self inverse");
        return false;
    }

    return true;
}

Mutex g_selfTestMutex;
bool g_selfTestDone = false;
ea::string g_selfTestFailure;

/// Run the one-time correctness gate. Returns false and fills \p error when the pipeline is
/// broken, in which case no encrypted payload may be handed to the VM.
bool SelfTestPassed(ea::string& error)
{
    MutexLock lock(g_selfTestMutex);
    if (!g_selfTestDone)
    {
        g_selfTestFailure.clear();
        if (!SelfTestCipher(g_selfTestFailure))
        {
            if (g_selfTestFailure.empty())
                g_selfTestFailure = "cipher self test failed without a reason";
        }
        else if (!SelfTestContainer(g_selfTestFailure))
        {
            if (g_selfTestFailure.empty())
                g_selfTestFailure = "container self test failed without a reason";
        }
        g_selfTestDone = true;
    }

    if (!g_selfTestFailure.empty())
    {
        error = g_selfTestFailure;
        return false;
    }
    return true;
}

} // namespace

bool LuaScriptContainerIsPackaged(const void* data, size_t size)
{
    if (!data || size < LuaScriptContainerHeaderSize)
        return false;
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    return memcmp(bytes, kMagic, sizeof(kMagic)) == 0 && bytes[4] == kFormatVersion;
}

bool LuaScriptContainerUnpack(const void* data, size_t size, ea::vector<uint8_t>& out, ea::string& error)
{
    out.clear();

    // A failing self test means the cipher or the format is broken, so nothing packaged can
    // be trusted - not even a plain payload, because the round trip itself is what was tested.
    if (!SelfTestPassed(error))
        return false;

    return UnpackImpl(data, size, out, error);
}

bool LuaScriptContainerPack(const void* payload, size_t size, LuaScriptPayload kind, LuaScriptCipher cipher,
    uint32_t ivSeed, ea::vector<uint8_t>& out, ea::string& error)
{
    if (!SelfTestPassed(error))
        return false;
    return PackImpl(payload, size, kind, cipher, ivSeed, out, error);
}

uint32_t LuaScriptContainerNameSeed(const char* resourceName)
{
    // FNV-1a, lower-cased input so that a case-insensitive file system cannot produce two
    // different keystreams for what is the same file to it.
    uint32_t hash = 2166136261u;
    if (resourceName)
    {
        for (const char* c = resourceName; *c; ++c)
        {
            char ch = *c;
            if (ch >= 'A' && ch <= 'Z')
                ch = (char)(ch - 'A' + 'a');
            hash ^= (uint8_t)ch;
            hash *= 16777619u;
        }
    }
    return hash;
}

void LuaScriptContainerSetKey(const uint8_t* key32)
{
    if (!key32)
        return;
    memcpy(g_explicitKey, key32, LuaScriptAes::KeyBytes);
    g_hasExplicitKey = true;
    // The previous result no longer describes this key, so the gate runs again on next use.
    MutexLock lock(g_selfTestMutex);
    g_selfTestDone = false;
    g_selfTestFailure.clear();
}

const uint8_t* LuaScriptContainerGetKey()
{
    return ActiveKey();
}

LuaScriptContainerKeySource LuaScriptContainerGetKeySource()
{
    if (g_hasExplicitKey)
        return LuaScriptContainerKeySource::Explicit;
    return Resolved().source;
}

bool LuaScriptContainerSelfTest(ea::string& error)
{
    return SelfTestPassed(error);
}

} // namespace Urho3D

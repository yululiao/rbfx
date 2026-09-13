//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#include "../Urho3D/Precompiled.h"

#include "LuaScriptAes.h"

#include <array>
#include <cstring>

namespace Urho3D
{

namespace
{

/// AES is defined over GF(2^8) with the reduction polynomial x^8 + x^4 + x^3 + x^2 + 1.
constexpr uint8_t kReductionPolyLowByte = 0x1B;

uint8_t GfMul(uint8_t a, uint8_t b)
{
    uint8_t product = 0;
    for (int i = 0; i < 8; ++i)
    {
        if (b & 1)
            product ^= a;
        const uint8_t highBit = a & 0x80;
        a = (uint8_t)(a << 1);
        if (highBit)
            a ^= kReductionPolyLowByte;
        b = (uint8_t)(b >> 1);
    }
    return product;
}

/// Multiplicative inverse through Fermat: a^254 == a^-1 for every non-zero a in GF(2^8).
uint8_t GfInv(uint8_t a)
{
    if (a == 0)
        return 0;
    uint8_t result = 1;
    uint8_t base = a;
    int exponent = 254;
    while (exponent)
    {
        if (exponent & 1)
            result = GfMul(result, base);
        base = GfMul(base, base);
        exponent >>= 1;
    }
    return result;
}

uint8_t Rotl8(uint8_t value, int count)
{
    return (uint8_t)((value << count) | (value >> (8 - count)));
}

/// The AES S-box is the GF inverse followed by this affine map.
uint8_t Affine(uint8_t value)
{
    return (uint8_t)(value ^ Rotl8(value, 1) ^ Rotl8(value, 2) ^ Rotl8(value, 3) ^ Rotl8(value, 4) ^ 0x63);
}

/// The substitution table is computed instead of embedded: a hand-typed 256 byte table is
/// the classic place for a silent one-character error, while the definition above is short
/// enough to audit and is covered by the known-answer vectors LuaScriptContainer runs.
const uint8_t* SubstitutionTable()
{
    static const std::array<uint8_t, 256> table = []
    {
        std::array<uint8_t, 256> values{};
        for (int i = 0; i < 256; ++i)
            values[(size_t)i] = Affine(GfInv((uint8_t)i));
        return values;
    }();
    return table.data();
}

uint8_t SubByte(uint8_t value)
{
    return SubstitutionTable()[value];
}

void SubWord(uint8_t* word)
{
    for (int i = 0; i < 4; ++i)
        word[i] = SubByte(word[i]);
}

void RotWord(uint8_t* word)
{
    const uint8_t first = word[0];
    word[0] = word[1];
    word[1] = word[2];
    word[2] = word[3];
    word[3] = first;
}

uint8_t RoundConstant(int index)
{
    // Rcon[i] = x^(i-1) in GF(2^8): 0x01, 0x02, 0x04 ... 0x80, 0x1B, 0x36
    uint8_t value = 1;
    for (int i = 1; i < index; ++i)
    {
        const uint8_t highBit = value & 0x80;
        value = (uint8_t)(value << 1);
        if (highBit)
            value ^= kReductionPolyLowByte;
    }
    return value;
}

} // namespace

LuaScriptAes::LuaScriptAes(const uint8_t* key)
{
    std::memcpy(roundKeys_, key, 4 * 8);

    for (int word = 8; word < (int)(Rounds + 1) * 4; ++word)
    {
        uint8_t temp[4];
        std::memcpy(temp, roundKeys_ + (word - 1) * 4, 4);

        if (word % 8 == 0)
        {
            RotWord(temp);
            SubWord(temp);
            temp[0] ^= RoundConstant(word / 8);
        }
        else if (word % 8 == 4)
        {
            SubWord(temp);
        }

        for (int i = 0; i < 4; ++i)
            roundKeys_[word * 4 + i] = (uint8_t)(roundKeys_[(word - 8) * 4 + i] ^ temp[i]);
    }
}

void LuaScriptAes::EncryptBlock(uint8_t* s) const
{
    auto addRoundKey = [this, s](unsigned round)
    {
        const uint8_t* rk = roundKeys_ + round * BlockBytes;
        for (int i = 0; i < BlockBytes; ++i)
            s[i] ^= rk[i];
    };
    auto subBytes = [s]
    {
        for (int i = 0; i < BlockBytes; ++i)
            s[i] = SubByte(s[i]);
    };
    // Column-major state: s[row + 4 * column]. Row i is rotated left by i.
    auto shiftRows = [s]
    {
        uint8_t tmp = s[1];
        s[1] = s[5];
        s[5] = s[9];
        s[9] = s[13];
        s[13] = tmp;

        for (int i = 2; i <= 6; i += 4)
        {
            const uint8_t a = s[i];
            s[i] = s[i + 8];
            s[i + 8] = a;
        }

        tmp = s[3];
        s[3] = s[15];
        s[15] = s[11];
        s[11] = s[7];
        s[7] = tmp;
    };
    // b_i = 2*a_i + 3*a_(i+1) + a_(i+2) + a_(i+3), rewritten so that each column costs
    // two GF multiplications by x instead of four general ones.
    auto mixColumns = [s]
    {
        for (int column = 0; column < 4; ++column)
        {
            uint8_t* c = s + column * 4;
            const uint8_t sum = (uint8_t)(c[0] ^ c[1] ^ c[2] ^ c[3]);
            const uint8_t b0 = (uint8_t)(c[0] ^ sum ^ GfMul((uint8_t)(c[0] ^ c[1]), 2));
            const uint8_t b1 = (uint8_t)(c[1] ^ sum ^ GfMul((uint8_t)(c[1] ^ c[2]), 2));
            const uint8_t b2 = (uint8_t)(c[2] ^ sum ^ GfMul((uint8_t)(c[2] ^ c[3]), 2));
            const uint8_t b3 = (uint8_t)(c[3] ^ sum ^ GfMul((uint8_t)(c[3] ^ c[0]), 2));
            c[0] = b0;
            c[1] = b1;
            c[2] = b2;
            c[3] = b3;
        }
    };

    addRoundKey(0);
    for (unsigned round = 1; round < Rounds; ++round)
    {
        subBytes();
        shiftRows();
        mixColumns();
        addRoundKey(round);
    }
    subBytes();
    shiftRows();
    addRoundKey(Rounds);
}

void LuaScriptAes::CtrXcrypt(uint8_t* data, size_t size, const uint8_t* iv) const
{
    uint8_t counter[BlockBytes];
    uint8_t keystream[BlockBytes];
    std::memcpy(counter, iv, BlockBytes);

    size_t offset = 0;
    while (offset < size)
    {
        std::memcpy(keystream, counter, BlockBytes);
        EncryptBlock(keystream);

        const size_t chunk = size - offset < BlockBytes ? size - offset : BlockBytes;
        for (size_t i = 0; i < chunk; ++i)
            data[offset + i] ^= keystream[i];
        offset += chunk;

        for (int i = BlockBytes - 1; i >= 0; --i)
        {
            if (++counter[i] != 0)
                break;
        }
    }
}

} // namespace Urho3D

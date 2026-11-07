// Bytes, the hashes of UE 4.26 (CityHash, name hashes, layout hash) and names, for the own map writer.
// Part of own_map_writer.h, which includes every part.
#pragma once
#include "custom_material_hash.h"
#include "material_json.h"
#include "own_game_layouts.h"
#include "own_type_layouts.h"
#include <algorithm>
#include <array>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace P4MapKit
{
namespace CustomMaterial
{
namespace OwnMap
{
using Material::Json;

struct Error : std::runtime_error
{
    explicit Error(const std::string& s) : std::runtime_error("own map: " + s) {}
};
inline void Need(bool ok, const std::string& why)
{
    if (!ok)
        throw Error(why);
}

// ----- bytes -------------------------------------------------------------------------------------------------------
inline void Put8(Bytes& o, std::uint64_t v)
{
    o.push_back(std::uint8_t(v));
}
inline void Put16(Bytes& o, std::uint64_t v)
{
    for (int i = 0; i < 2; i++)
        o.push_back(std::uint8_t(v >> (8 * i)));
}
inline void Put32(Bytes& o, std::uint64_t v)
{
    for (int i = 0; i < 4; i++)
        o.push_back(std::uint8_t(v >> (8 * i)));
}
inline void Put64(Bytes& o, std::uint64_t v)
{
    for (int i = 0; i < 8; i++)
        o.push_back(std::uint8_t(v >> (8 * i)));
}
inline void PutBytes(Bytes& o, const Bytes& b)
{
    o.insert(o.end(), b.begin(), b.end());
}
inline void At16(Bytes& o, std::size_t at, std::uint64_t v)
{
    for (int i = 0; i < 2; i++)
        o.at(at + i) = std::uint8_t(v >> (8 * i));
}
inline void At32(Bytes& o, std::size_t at, std::uint64_t v)
{
    for (int i = 0; i < 4; i++)
        o.at(at + i) = std::uint8_t(v >> (8 * i));
}
inline void At64(Bytes& o, std::size_t at, std::uint64_t v)
{
    for (int i = 0; i < 8; i++)
        o.at(at + i) = std::uint8_t(v >> (8 * i));
}
inline std::uint32_t Get32(const Bytes& o, std::size_t at)
{
    std::uint32_t v = 0;
    for (int i = 0; i < 4; i++)
        v |= std::uint32_t(o.at(at + i)) << (8 * i);
    return v;
}
inline std::size_t AlignUp(std::size_t v, std::size_t a)
{
    return (v + a - 1) / a * a;
}
inline Bytes Text(const std::string& s)
{
    return Bytes(s.begin(), s.end());
}
inline Bytes Cat(std::initializer_list<Bytes> parts)
{
    Bytes o;
    for (const auto& p : parts)
        PutBytes(o, p);
    return o;
}

// ----- hashes ------------------------------------------------------------------------------------------------------
namespace City
{ // UE 4.26 Core/Private/Hash/CityHash.cpp (CityHash64, CityHash64WithSeed), little-endian host
constexpr std::uint64_t k0 = 0xc3a5c85c97cb3127ULL, k1 = 0xb492b66fbe98f273ULL, k2 = 0x9ae16a3b2f90404fULL;
inline std::uint64_t F64(const std::uint8_t* s)
{
    std::uint64_t v;
    std::memcpy(&v, s, 8);
    return v;
}
inline std::uint64_t F32(const std::uint8_t* s)
{
    std::uint32_t v;
    std::memcpy(&v, s, 4);
    return v;
}
inline std::uint64_t Rot(std::uint64_t v, int n)
{
    return n == 0 ? v : (v >> n) | (v << (64 - n));
}
inline std::uint64_t ShiftMix(std::uint64_t v)
{
    return v ^ (v >> 47);
}
inline std::uint64_t Bswap(std::uint64_t v)
{
    std::uint64_t o = 0;
    for (int i = 0; i < 8; i++)
        o = (o << 8) | ((v >> (8 * i)) & 0xff);
    return o;
}
inline std::uint64_t Hash128to64(std::uint64_t lo, std::uint64_t hi)
{
    const std::uint64_t mul = 0x9ddfea08eb382d69ULL;
    std::uint64_t a = (lo ^ hi) * mul;
    a ^= a >> 47;
    std::uint64_t b = (hi ^ a) * mul;
    b ^= b >> 47;
    return b * mul;
}
inline std::uint64_t Len16(std::uint64_t u, std::uint64_t v, std::uint64_t mul)
{
    std::uint64_t a = (u ^ v) * mul;
    a ^= a >> 47;
    std::uint64_t b = (v ^ a) * mul;
    b ^= b >> 47;
    return b * mul;
}
inline std::uint64_t Len0to16(const std::uint8_t* s, std::uint64_t n)
{
    if (n >= 8)
    {
        const std::uint64_t mul = k2 + n * 2, a = F64(s) + k2, b = F64(s + n - 8);
        return Len16(Rot(b, 37) * mul + a, (Rot(a, 25) + b) * mul, mul);
    }
    if (n >= 4)
    {
        const std::uint64_t mul = k2 + n * 2;
        return Len16(n + (F32(s) << 3), F32(s + n - 4), mul);
    }
    if (n > 0)
    {
        const std::uint64_t a = s[0], b = s[n >> 1], c = s[n - 1], y = a + (b << 8), z = n + (c << 2);
        return ShiftMix((y * k2) ^ (z * k0)) * k2;
    }
    return k2;
}
inline std::uint64_t Len17to32(const std::uint8_t* s, std::uint64_t n)
{
    const std::uint64_t mul = k2 + n * 2, a = F64(s) * k1, b = F64(s + 8), c = F64(s + n - 8) * mul,
                        d = F64(s + n - 16) * k2;
    return Len16(Rot(a + b, 43) + Rot(c, 30) + d, a + Rot(b + k2, 18) + c, mul);
}
inline std::pair<std::uint64_t, std::uint64_t> Weak(
    std::uint64_t w, std::uint64_t x, std::uint64_t y, std::uint64_t z, std::uint64_t a, std::uint64_t b)
{
    a += w;
    b = Rot(b + a + z, 21);
    const std::uint64_t c = a;
    a += x + y;
    b += Rot(a, 44);
    return {a + z, b + c};
}
inline std::pair<std::uint64_t, std::uint64_t> WeakS(const std::uint8_t* s, std::uint64_t a, std::uint64_t b)
{
    return Weak(F64(s), F64(s + 8), F64(s + 16), F64(s + 24), a, b);
}
inline std::uint64_t Len33to64(const std::uint8_t* s, std::uint64_t n)
{
    const std::uint64_t mul = k2 + n * 2;
    std::uint64_t a = F64(s) * k2, b = F64(s + 8);
    const std::uint64_t c = F64(s + n - 24), d = F64(s + n - 32), e = F64(s + 16) * k2, f = F64(s + 24) * 9,
                        g = F64(s + n - 8), h = F64(s + n - 16) * mul;
    const std::uint64_t u = Rot(a + g, 43) + (Rot(b, 30) + c) * 9, v = ((a + g) ^ d) + f + 1,
                        w = Bswap((u + v) * mul) + h, x = Rot(e + f, 42) + c, y = (Bswap((v + w) * mul) + g) * mul,
                        z = e + f + c;
    a = Bswap((x + z) * mul + y) + b;
    b = ShiftMix((z + a) * mul + d + h) * mul;
    return b + x;
}
inline std::uint64_t Hash64(const std::uint8_t* s, std::uint64_t n)
{
    if (n <= 32)
        return n <= 16 ? Len0to16(s, n) : Len17to32(s, n);
    if (n <= 64)
        return Len33to64(s, n);
    std::uint64_t x = F64(s + n - 40), y = F64(s + n - 16) + F64(s + n - 56),
                  z = Hash128to64(F64(s + n - 48) + n, F64(s + n - 24));
    auto v = WeakS(s + n - 64, n, z);
    auto w = WeakS(s + n - 32, y + k1, x);
    x = x * k1 + F64(s);
    std::uint64_t rem = (n - 1) & ~std::uint64_t(63), p = 0;
    for (;;)
    {
        x = Rot(x + y + v.first + F64(s + p + 8), 37) * k1;
        y = Rot(y + v.second + F64(s + p + 48), 42) * k1;
        x ^= w.second;
        y += v.first + F64(s + p + 40);
        z = Rot(z + w.first, 33) * k1;
        v = WeakS(s + p, v.second * k1, x + w.first);
        w = WeakS(s + p + 32, z + w.second, y + F64(s + p + 16));
        std::swap(z, x);
        p += 64;
        rem -= 64;
        if (rem == 0)
            break;
    }
    return Hash128to64(Hash128to64(v.first, w.first) + ShiftMix(y) * k1 + z, Hash128to64(v.second, w.second) + x);
}
} // namespace City

inline void NeedAscii(const std::string& s)
{
    for (unsigned char c : s)
        Need(c < 128, "names are written for ASCII only: " + s);
}
inline std::string Upper(std::string s)
{
    for (auto& c : s)
        if (c >= 'a' && c <= 'z')
            c = char(c - 32);
    return s;
}
inline std::string Lower(std::string s)
{
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z')
            c = char(c + 32);
    return s;
}

// A UTF-8 name's code points. Parameter and preshader names may be ASCII or kana, CJK and Hangul (the game has
// Japanese parameter names): characters whose case mapping is the identity and which Python's repr leaves as they
// are, so the name hashes, the name table's sort and the id hash's text are written exactly. Others are refused.
inline std::vector<std::uint32_t> NameCodePoints(const std::string& s)
{
    std::vector<std::uint32_t> out;
    for (std::size_t i = 0; i < s.size();)
    {
        const auto c = std::uint8_t(s[i++]);
        int n = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
        Need(n >= 0 && i + std::size_t(n) <= s.size(), "a name is not UTF-8: " + s);
        std::uint32_t v = n == 0 ? c : c & (0x3F >> n);
        for (int k = 0; k < n; k++)
        {
            const auto d = std::uint8_t(s[i++]);
            Need((d & 0xC0) == 0x80, "a name is not UTF-8: " + s);
            v = (v << 6) | (d & 0x3F);
        }
        out.push_back(v);
    }
    return out;
}
inline std::vector<std::uint32_t> NeedName(const std::string& s)
{
    auto points = NameCodePoints(s);
    for (auto v : points)
        Need(v < 0x80 || (v >= 0x3001 && v <= 0x9FFF) || (v >= 0xAC00 && v <= 0xD7A3) || (v >= 0xFF01 && v <= 0xFF20) ||
                (v >= 0xFF3B && v <= 0xFF40) || (v >= 0xFF5B && v <= 0xFFEF),
            "names are written for ASCII, kana, CJK and Hangul: " + s);
    return points;
}

// FHashedName(FName(name)) for an ASCII name: CityHash64WithSeed(upper case, number 0).
inline std::uint64_t HashedName(const std::string& name)
{
    NeedAscii(name);
    const auto upper = Upper(name);
    const auto h = City::Hash64(reinterpret_cast<const std::uint8_t*>(upper.data()), upper.size());
    return City::Hash128to64(h - City::k2, 0);
}

// The two 16-bit hashes of a serialized name entry (FNameEntrySerialized): Strihash_DEPRECATED and StrCrc32.
inline std::pair<std::uint16_t, std::uint16_t> NameHashes(const std::string& text)
{
    const auto points = NeedName(text);
    const bool wide = std::any_of(points.begin(), points.end(), [](std::uint32_t v) { return v > 127; });
    static const auto tables = []
    {
        std::array<std::array<std::uint32_t, 256>, 2> t{};
        for (std::uint32_t i = 0; i < 256; i++)
        {
            std::uint32_t c = i << 24;
            for (int k = 0; k < 8; k++)
                c = (c & 0x80000000u) ? (c << 1) ^ 0x04c11db7u : c << 1;
            t[0][i] = c;
            c = i;
            for (int k = 0; k < 8; k++)
                c = (c >> 1) ^ ((c & 1) ? 0xedb88320u : 0);
            t[1][i] = c;
        }
        return t;
    }();
    std::uint32_t h = 0;
    for (auto ch : points)
    { // FCrc::Strihash_DEPRECATED of the upper case text: one table step per byte of a char
        if (ch >= 'a' && ch <= 'z')
            ch -= 32;
        h = ((h >> 8) & 0x00ffffffu) ^ tables[0][(h ^ ch) & 0xff];
        if (wide)
            h = ((h >> 8) & 0x00ffffffu) ^ tables[0][(h ^ (ch >> 8)) & 0xff];
    }
    std::uint32_t c = 0xffffffffu;
    for (auto ch : points)
    { // FCrc::StrCrc32: each character as four bytes, low first
        std::uint32_t v = ch;
        for (int k = 0; k < 4; k++)
        {
            c = (c >> 8) ^ tables[1][(c ^ v) & 0xff];
            v >>= 8;
        }
    }
    return {std::uint16_t(h & 0xffff), std::uint16_t(~c & 0xffff)};
}

// FRHIUniformBufferLayout::ComputeHash.
inline std::uint32_t LayoutHash(
    std::uint32_t size, std::uint32_t staticSlot, const std::vector<std::pair<std::uint16_t, std::uint8_t>>& resources)
{
    std::uint32_t h = (size << 16) | staticSlot;
    for (const auto& r : resources)
        h ^= r.first;
    std::size_t n = resources.size();
    while (n >= 4)
        for (int shift : {0, 8, 16, 24})
        {
            n--;
            h ^= std::uint32_t(resources[n].second) << shift;
        }
    while (n >= 2)
        for (int shift : {0, 16})
        {
            n--;
            h ^= std::uint32_t(resources[n].second) << shift;
        }
    while (n > 0)
    {
        n--;
        h ^= resources[n].second;
    }
    return h;
}

}
}
} // namespace P4MapKit::CustomMaterial::OwnMap

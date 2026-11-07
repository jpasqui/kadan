// The input varyings of a Switch 2 (SF_GLSL_SAGE) pixel program, named again.
//
// Switch 1 payloads list each varying's name and location (in_texcoord0 at 2 ...), which Kadan's binder needs to
// name interpolants. Switch 2 payloads leave the list out but keep a SHA-1 of it (OwnSage::Bindings::hash): every
// varying as its location (32-bit little-endian) then its name and a NUL, concatenated in the order the cooker's
// container held them, which is neither location nor name order. A program with no varyings has SHA-1 of nothing.
//
// The names come back by search: the locations the program header declares (one byte per generic attribute in SPH
// words 6-13, nonzero when declared), plus any lower locations the code never reads; names as an in-order subsequence
// of UE's interpolant order assigned to increasing locations; every order of the hashed list. The hash decides, so a
// result is exact or there is none. Checked on all 500 base-pass pixel programs of Cave000 in 2.0.0.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace P4MapKit
{
namespace Sage
{
using Varying = std::pair<std::string, int>;   // name, location

// SHA-1 (FIPS 180-1), small and self-contained.
inline std::array<std::uint8_t, 20> Sha1(const std::vector<std::uint8_t>& message)
{
    std::uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    std::vector<std::uint8_t> m = message;
    const std::uint64_t bits = std::uint64_t(message.size()) * 8;
    m.push_back(0x80);
    while (m.size() % 64 != 56)
        m.push_back(0);
    for (int i = 7; i >= 0; --i)
        m.push_back(std::uint8_t(bits >> (8 * i)));
    const auto rol = [](std::uint32_t v, int n) { return (v << n) | (v >> (32 - n)); };
    for (std::size_t at = 0; at < m.size(); at += 64)
    {
        std::uint32_t w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = std::uint32_t(m[at + 4 * i]) << 24 | std::uint32_t(m[at + 4 * i + 1]) << 16 |
                   std::uint32_t(m[at + 4 * i + 2]) << 8 | m[at + 4 * i + 3];
        for (int i = 16; i < 80; ++i)
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i)
        {
            std::uint32_t f, k;
            if (i < 20) f = (b & c) | (~b & d), k = 0x5A827999u;
            else if (i < 40) f = b ^ c ^ d, k = 0x6ED9EBA1u;
            else if (i < 60) f = (b & c) | (b & d) | (c & d), k = 0x8F1BBCDCu;
            else f = b ^ c ^ d, k = 0xCA62C1D6u;
            const std::uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d, d = c, c = rol(b, 30), b = a, a = t;
        }
        h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e;
    }
    std::array<std::uint8_t, 20> out{};
    for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 4; ++j)
            out[std::size_t(4 * i + j)] = std::uint8_t(h[i] >> (24 - 8 * j));
    return out;
}

// The hash a payload keeps for this list, in this order.
inline std::array<std::uint8_t, 20> VaryingHash(const std::vector<Varying>& list)
{
    std::vector<std::uint8_t> bytes;
    for (const auto& v : list)
    {
        for (int i = 0; i < 4; ++i)
            bytes.push_back(std::uint8_t(std::uint32_t(v.second) >> (8 * i)));
        bytes.insert(bytes.end(), v.first.begin(), v.first.end());
        bytes.push_back(0);
    }
    return Sha1(bytes);
}

// UE 4.26/4.27 base-pass interpolants in declaration order, as GLSL names (centroid where the cooker marks it).
inline const std::vector<std::string>& InterpolantOrder()
{
    static const std::vector<std::string> Order = {"in_texcoord10_centroid", "in_texcoord11_centroid", "in_color0",
        "in_texcoord0", "in_texcoord1", "in_texcoord2", "in_texcoord3", "in_texcoord4", "in_texcoord5", "in_texcoord6",
        "in_texcoord7", "in_texcoord8", "in_texcoord9"};
    return Order;
}

// Generic attributes the pixel program header declares: SPH words 6-13, one byte per attribute.
inline std::vector<int> DeclaredAttributes(const std::array<std::uint32_t, 32>& header)
{
    std::vector<int> out;
    for (int a = 0; a < 32; ++a)
        if ((header[std::size_t(6 + a / 4)] >> (8 * (a % 4))) & 0xffu)
            out.push_back(a);
    return out;
}

// The varyings whose hash is `hash`, by location; empty when the search finds none (or `found` false). A program
// whose hash is SHA-1 of nothing has none.
inline std::vector<Varying> RecoverVaryings(const std::array<std::uint8_t, 20>& hash, const std::vector<int>& declared,
                                            bool& found, const std::vector<std::string>& order = InterpolantOrder())
{
    found = false;
    if (VaryingHash({}) == hash)
    {
        found = true;
        return {};
    }
    const int top = declared.empty() ? 0 : *std::max_element(declared.begin(), declared.end());
    std::vector<int> free;
    for (int l = 0; l <= top; ++l)
        if (std::find(declared.begin(), declared.end(), l) == declared.end())
            free.push_back(l);
    std::vector<Varying> result;
    // Subsets of `free` by size, smallest first.
    for (std::size_t extra = 0; extra <= free.size() && !found; ++extra)
    {
        std::vector<bool> pick(free.size(), false);
        std::fill(pick.begin(), pick.begin() + std::ptrdiff_t(extra), true);
        do
        {
            std::vector<int> locations = declared;
            for (std::size_t i = 0; i < free.size(); ++i)
                if (pick[i]) locations.push_back(free[i]);
            std::sort(locations.begin(), locations.end());
            const std::size_t k = locations.size();
            if (k == 0 || k > order.size() || k > 8) continue;
            // In-order subsequences of `order` of length k.
            std::vector<bool> take(order.size(), false);
            std::fill(take.begin(), take.begin() + std::ptrdiff_t(k), true);
            do
            {
                std::vector<Varying> list;
                for (std::size_t i = 0, j = 0; i < order.size(); ++i)
                    if (take[i]) list.emplace_back(order[i], locations[j++]);
                std::vector<std::size_t> perm(k);
                for (std::size_t i = 0; i < k; ++i) perm[i] = i;
                do
                {
                    std::vector<Varying> hashed;
                    for (auto i : perm) hashed.push_back(list[i]);
                    if (VaryingHash(hashed) == hash)
                    {
                        found = true;
                        result = list;
                        break;
                    }
                } while (std::next_permutation(perm.begin(), perm.end()));
            } while (!found && std::prev_permutation(take.begin(), take.end()));
        } while (!found && std::prev_permutation(pick.begin(), pick.end()));
    }
    return result;
}
} // namespace Sage
} // namespace P4MapKit

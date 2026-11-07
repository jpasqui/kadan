// Deterministic shader identifiers and compile-recipe digests. SHA-1 is the
// engine's shader file identifier, SHA-256 binds authoring inputs independently.
#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
namespace P4MapKit
{
namespace CustomMaterial
{
using Bytes = std::vector<std::uint8_t>;
inline std::uint32_t RotL(std::uint32_t x, unsigned n)
{
    return (x << n) | (x >> (32 - n));
}
inline std::uint32_t RotR(std::uint32_t x, unsigned n)
{
    return (x >> n) | (x << (32 - n));
}
inline Bytes DigestInput(const Bytes& in)
{
    Bytes data = in;
    std::uint64_t bits = std::uint64_t(in.size()) * 8;
    data.push_back(128);
    while (data.size() % 64 != 56)
        data.push_back(0);
    for (int i = 7; i >= 0; i--)
        data.push_back(std::uint8_t(bits >> (8 * i)));
    return data;
}
inline std::uint32_t BEWord(const Bytes& b, std::size_t o)
{
    return std::uint32_t(b[o]) << 24 | std::uint32_t(b[o + 1]) << 16 | std::uint32_t(b[o + 2]) << 8 | b[o + 3];
}
inline Bytes HashWords(const std::uint32_t* p, unsigned n)
{
    Bytes out;
    for (unsigned i = 0; i < n; i++)
        for (int j = 3; j >= 0; j--)
            out.push_back(std::uint8_t(p[i] >> (8 * j)));
    return out;
}
inline Bytes Sha1(const Bytes& input)
{
    auto b = DigestInput(input);
    std::uint32_t h[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
    for (std::size_t block = 0; block < b.size(); block += 64)
    {
        std::uint32_t w[80];
        for (int i = 0; i < 16; i++)
            w[i] = BEWord(b, block + 4 * i);
        for (int i = 16; i < 80; i++)
            w[i] = RotL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        auto a = h[0], c = h[2], d = h[3], e = h[4], bb = h[1];
        for (int i = 0; i < 80; i++)
        {
            auto f = i < 20   ? (bb & c) | (~bb & d)
                     : i < 40 ? bb ^ c ^ d
                     : i < 60 ? (bb & c) | (bb & d) | (c & d)
                              : bb ^ c ^ d;
            auto k = i < 20 ? 0x5a827999u : i < 40 ? 0x6ed9eba1u : i < 60 ? 0x8f1bbcdcu : 0xca62c1d6u;
            auto t = RotL(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = RotL(bb, 30);
            bb = a;
            a = t;
        }
        h[0] += a;
        h[1] += bb;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }
    return HashWords(h, 5);
}
inline Bytes Sha256(const Bytes& input)
{
    static const std::uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    auto b = DigestInput(input);
    std::uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                          0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    for (std::size_t block = 0; block < b.size(); block += 64)
    {
        std::uint32_t w[64];
        for (int i = 0; i < 16; i++)
            w[i] = BEWord(b, block + 4 * i);
        for (int i = 16; i < 64; i++)
        {
            auto s0 = RotR(w[i - 15], 7) ^ RotR(w[i - 15], 18) ^ (w[i - 15] >> 3);
            auto s1 = RotR(w[i - 2], 17) ^ RotR(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        auto a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++)
        {
            auto s1 = RotR(e, 6) ^ RotR(e, 11) ^ RotR(e, 25), ch = (e & f) ^ (~e & g), t1 = hh + s1 + ch + k[i] + w[i];
            auto s0 = RotR(a, 2) ^ RotR(a, 13) ^ RotR(a, 22), maj = (a & bb) ^ (a & c) ^ (bb & c), t2 = s0 + maj;
            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = bb;
            bb = a;
            a = t1 + t2;
        }
        h[0] += a;
        h[1] += bb;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
    }
    return HashWords(h, 8);
}
inline Bytes ByteText(const std::string& s)
{
    return Bytes(s.begin(), s.end());
}
inline std::string Hex(const Bytes& b)
{
    std::string out;
    const char* digits = "0123456789abcdef";
    for (auto x : b)
    {
        out += digits[x >> 4];
        out += digits[x & 15];
    }
    return out;
}
inline Bytes Unhex(const std::string& s)
{
    if (s.size() % 2)
        throw std::runtime_error("odd hex byte count");
    Bytes out;
    auto nibble = [](char c) -> int
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        throw std::runtime_error("lowercase hexadecimal required");
    };
    for (std::size_t i = 0; i < s.size(); i += 2)
        out.push_back(std::uint8_t(nibble(s[i]) * 16 + nibble(s[i + 1])));
    return out;
}
}
}

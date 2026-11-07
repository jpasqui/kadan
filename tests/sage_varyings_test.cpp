#include "../core/sage_varyings.h"
#include <cstdlib>
#include <iostream>
using namespace P4MapKit::Sage;
static void Check(bool good, const char* what)
{
    if (!good) { std::cerr << "sage varyings check failed: " << what << "\n"; std::exit(1); }
}
static std::string Hex(const std::array<std::uint8_t, 20>& h)
{
    static const char* digits = "0123456789abcdef";
    std::string s;
    for (auto b : h) s += digits[b >> 4], s += digits[b & 15];
    return s;
}
int main()
{
    // SHA-1 test vectors.
    Check(Hex(Sha1({})) == "da39a3ee5e6b4b0d3255bfef95601890afd80709", "SHA-1 of nothing");
    const std::string abc = "abc";
    Check(Hex(Sha1(std::vector<std::uint8_t>(abc.begin(), abc.end()))) == "a9993e364706816aba3e25717850c26c9cd0d89d",
          "SHA-1 of abc");
    // The payload hash of one varying, as a Switch 2 payload keeps it (2.0.0 M_Black_Master base pass, in_texcoord7 at
    // location 2): only the formula and its inputs, no game data.
    Check(Hex(VaryingHash({{"in_texcoord7", 2}})) == "f80adccfe3c9fc5858bdeddbb06309bff990723b", "one varying");
    // A list hashed out of location order is found, by location.
    const std::vector<Varying> hashed = {{"in_texcoord10_centroid", 0}, {"in_texcoord11_centroid", 1}, {"in_texcoord7", 3},
                                         {"in_texcoord0", 2}};
    bool found = false;
    auto got = RecoverVaryings(VaryingHash(hashed), {1, 2, 3}, found);
    Check(found && got.size() == 4 && got[0].first == "in_texcoord10_centroid" && got[0].second == 0 &&
              got[2].first == "in_texcoord0" && got[3].first == "in_texcoord7" && got[3].second == 3,
          "unread location 0 and container order");
    // A gap below the first declared location.
    got = RecoverVaryings(VaryingHash({{"in_texcoord7", 2}}), {2}, found);
    Check(found && got.size() == 1 && got[0].second == 2, "a single varying above unused locations");
    // No varyings.
    got = RecoverVaryings(VaryingHash({}), {}, found);
    Check(found && got.empty(), "no varyings");
    // A hash no candidate gives.
    std::array<std::uint8_t, 20> other{};
    other[0] = 1;
    got = RecoverVaryings(other, {0, 1}, found);
    Check(!found && got.empty(), "unknown hash is refused");
    // Declared attributes from SPH words 6-13.
    std::array<std::uint32_t, 32> header{};
    header[6] = 0xaa0a2a00u;
    const auto declared = DeclaredAttributes(header);
    Check((declared == std::vector<int>{1, 2, 3}), "declared attributes");
    std::cout << "sage varyings: all checks passed\n";
    return 0;
}

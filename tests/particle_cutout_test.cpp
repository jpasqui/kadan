#include "core/particle_cutout.h"
#include <fstream>
#include <iostream>
#include <cstdlib>

using namespace P4MapKit::ParticleCutout;
void Require(bool Value) { if (!Value) std::abort(); }
void Put(std::vector<std::uint8_t>& Bytes, std::size_t At, std::uint32_t Word)
{ for (int I = 0; I < 4; ++I) Bytes[At + I] = std::uint8_t(Word >> (I * 8)); }
int main(int Argc, char** Argv)
{
    std::vector<std::uint8_t> Bytes(28, 0); Put(Bytes, 4, 1); Put(Bytes, 8, 2);
    Put(Bytes, 12, 0x80000000); Put(Bytes, 16, 0x3f800000);
    Put(Bytes, 20, 0x3e800000); Put(Bytes, 24, 0x3f400000);
    FRequiredTail Readback; std::string Why;
    Require(Read(Bytes, Readback, Why));
    Require((Readback.CoordinateBits == std::vector<std::uint32_t>{0x80000000, 0x3f800000, 0x3e800000, 0x3f400000}));
    const auto Unchanged = Readback.CoordinateBits;
    for (std::size_t Size = 0; Size < Bytes.size(); ++Size)
    { Require(!Read(Bytes.data(), Size, Readback, Why)); Require(Readback.CoordinateBits == Unchanged); }
    auto Bad = Bytes; Bad.push_back(0); Require(!Read(Bad, Readback, Why));
    for (const auto Change : {std::pair<std::size_t, std::uint32_t>{0, 1}, {4, 0}, {4, 2},
        {8, 0xffffffff}, {8, 1048577}, {12, 0x7fc00000}, {12, 0x7f800000}})
    { Bad = Bytes; Put(Bad, Change.first, Change.second); Require(!Read(Bad, Readback, Why)); Require(Readback.CoordinateBits == Unchanged); }
    Bad.assign(12, 0); Put(Bad, 4, 1); Require(Read(Bad, Readback, Why)); Require(Readback.CoordinateBits.empty());
    for (int I = 1; I < Argc; ++I)
    {
        std::ifstream Input(Argv[I], std::ios::binary);
        Require(bool(Input)); Bytes.assign(std::istreambuf_iterator<char>(Input), {});
        Require(Read(Bytes, Readback, Why));
        std::cout << "Retained tail " << I << ": " << Readback.CoordinateBits.size() / 2 << " vertices\n";
    }
    std::cout << "Particle cutout bounds/bit preservation: PASS\n";
}

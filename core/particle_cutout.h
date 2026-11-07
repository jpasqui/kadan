// UE4.26 Required module tail: optional UObject GUID, cooked flag, FVector2D array.
// Preserve coordinate bit patterns; admission still requires native texture regeneration proof.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <cmath>

namespace P4MapKit { namespace ParticleCutout {
struct FRequiredTail
{
    std::vector<std::uint32_t> CoordinateBits;
};
inline bool Read(const std::uint8_t* Bytes, std::size_t Size, FRequiredTail& Out, std::string& Error)
{
    const auto Reject = [&](const char* Why) { Error = Why; return false; };
    if (!Bytes || Size < 12) return Reject("particle-required-tail-truncated");
    const auto U32 = [&](std::size_t At) {
        return std::uint32_t(Bytes[At]) | std::uint32_t(Bytes[At + 1]) << 8 |
            std::uint32_t(Bytes[At + 2]) << 16 | std::uint32_t(Bytes[At + 3]) << 24;
    };
    if (U32(0) != 0) return Reject("particle-required-guid-unqualified");
    if (U32(4) != 1) return Reject("particle-required-not-cooked");
    const std::uint32_t Count = U32(8);
    if (Count > 1048576) return Reject("particle-required-geometry-count-unbounded");
    if (Size != 12 + std::size_t(Count) * 8) return Reject("particle-required-geometry-size");
    FRequiredTail Candidate;
    Candidate.CoordinateBits.reserve(std::size_t(Count) * 2);
    for (std::size_t At = 12; At < Size; At += 4)
    {
        const auto Bits = U32(At);
        float Value; std::memcpy(&Value, &Bits, 4);
        if (!std::isfinite(Value)) return Reject("particle-required-geometry-not-finite");
        Candidate.CoordinateBits.push_back(Bits);
    }
    Out = std::move(Candidate); Error.clear(); return true;
}
inline bool Read(const std::vector<std::uint8_t>& Bytes, FRequiredTail& Out, std::string& Error)
{ return Read(Bytes.data(), Bytes.size(), Out, Error); }
} }

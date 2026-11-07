#pragma once
// Bounded UE4.26 FContainerHeader inspection. Bytes come from retoc's actual header chunk.
#include <cstdint>
#include <string>
#include <vector>
#include <set>
#include <utility>

namespace P4MapKit { namespace Export {
struct ContainerHeader {
    std::uint64_t Id = 0;
    std::vector<std::uint64_t> Packages;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> Redirects;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> CultureRedirects;
};
// Retoc can emit an identity redirect for each cooked package. These records
// remain in the census, but cannot widen the explicitly selected package set.
inline bool CheckSelectedContainer(const ContainerHeader& Header,
    const std::vector<std::uint64_t>& Selected, std::string& Error) {
    const std::set<std::uint64_t> Expected(Selected.begin(), Selected.end());
    const std::set<std::uint64_t> Actual(Header.Packages.begin(), Header.Packages.end());
    if (Selected.empty() || Selected.size() > 1000000 || Expected.size() != Selected.size() ||
        Actual.size() != Header.Packages.size() || Actual != Expected) {
        Error = "Final container packages differ from the explicit selection"; return false;
    }
    if (!Header.CultureRedirects.empty()) {
        Error = "Localized package aliases require separate exact target proofs"; return false;
    }
    for (const auto& Redirect : Header.Redirects)
        if (Redirect.first != Redirect.second || Expected.count(Redirect.first) == 0) {
            Error = "Final container contains an unselected or nonidentity package redirect"; return false;
        }
    Error.clear(); return true;
}
namespace ContainerDetail {
struct Reader {
    const std::vector<std::uint8_t>& Bytes; std::size_t At = 0;
    bool Take(std::size_t N) { if (N > Bytes.size() - At) return false; At += N; return true; }
    bool U32(std::uint32_t& Out) {
        if (Bytes.size() - At < 4) return false;
        Out = 0; for (unsigned I = 0; I != 4; ++I) Out |= std::uint32_t(Bytes[At++]) << (8 * I); return true;
    }
    bool U64(std::uint64_t& Out) {
        if (Bytes.size() - At < 8) return false;
        Out = 0; for (unsigned I = 0; I != 8; ++I) Out |= std::uint64_t(Bytes[At++]) << (8 * I); return true;
    }
    bool Count(std::uint32_t& Out) { return U32(Out) && Out <= 1000000; }
    bool Blob(std::uint32_t& Size) { return U32(Size) && Take(Size); }
};
}
inline bool ReadContainerHeader(const std::vector<std::uint8_t>& Bytes, ContainerHeader& Out, std::string& Error) {
    if (Bytes.size() > 64 * 1024 * 1024) { Error = "Container header exceeds 64 MiB"; return false; }
    ContainerDetail::Reader R{Bytes}; ContainerHeader Result;
    std::uint32_t Declared = 0, Count = 0, Size = 0;
    auto Fail = [&]() { Error = "Malformed or unsupported UE4.26 container header"; return false; };
    if (!R.U64(Result.Id) || !R.Count(Declared) || !R.Blob(Size) || !R.Blob(Size) ||
        !R.Count(Count) || Count != Declared || std::size_t(Count) > (Bytes.size() - R.At) / 8) return Fail();
    std::set<std::uint64_t> Seen;
    for (std::uint32_t I = 0; I < Count; ++I) {
        std::uint64_t Id = 0; if (!R.U64(Id) || !Seen.insert(Id).second) return Fail(); Result.Packages.push_back(Id);
    }
    if (!R.Blob(Size) || std::uint64_t(Size) < std::uint64_t(Declared) * 32 || !R.Count(Count)) return Fail();
    for (std::uint32_t I = 0; I < Count; ++I) {
        std::uint32_t Length = 0, Pairs = 0;
        if (!R.U32(Length)) return Fail();
        const std::int64_t Signed = static_cast<std::int32_t>(Length);
        const std::uint64_t Width = Signed < 0 ? std::uint64_t(-Signed) * 2 : std::uint64_t(Signed);
        if (Width > Bytes.size() - R.At || !R.Take(std::size_t(Width)) || !R.Count(Pairs) ||
            std::uint64_t(Pairs) * 16 > Bytes.size() - R.At || Result.CultureRedirects.size() + Pairs > 1000000) return Fail();
        for (std::uint32_t Pair = 0; Pair < Pairs; ++Pair) {
            std::uint64_t From = 0, To = 0;
            if (!R.U64(From) || !R.U64(To) || Seen.count(To) == 0) return Fail();
            Result.CultureRedirects.emplace_back(From, To);
        }
    }
    if (!R.Count(Count) || std::uint64_t(Count) * 16 > Bytes.size() - R.At) return Fail();
    const std::set<std::uint64_t> PackageSet = Seen;
    Seen.clear();
    for (std::uint32_t I = 0; I < Count; ++I) {
        std::uint64_t From = 0, To = 0;
        if (!R.U64(From) || !R.U64(To) || !Seen.insert(From).second || PackageSet.count(To) == 0) return Fail();
        Result.Redirects.emplace_back(From, To);
    }
    // IoStore writers may align the header to a 4 KiB page. Larger padding is
    // admitted only at that exact boundary; opaque trailing data is never ignored.
    const std::size_t Padding = Bytes.size() - R.At;
    if (Padding > 15 && (Padding >= 4096 || Bytes.size() % 4096 != 0)) return Fail();
    while (R.At < Bytes.size()) if (Bytes[R.At++] != 0) return Fail();
    Out = std::move(Result); Error.clear(); return true;
}
}}

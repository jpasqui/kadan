// A cooked package's names in the order the engine's save writes them.
//
// UE 4.26's save sorts every name of a package (FObjectNameSortHelper: FName::Compare on the names without their
// numbers, so case-insensitive, then the shorter first). A cook's own translations that add a name (a game tag the
// save left out) add it after the sorted ones. Putting it in its place moves the index of every name after it, and a
// name is referred to by its index from the header's tables and from anywhere in the objects' data, also from native
// data no reader here knows. Where the objects refer to names is found by comparing the package with the same package
// saved again with one name added that comes before every other (the probe): there, every reference to a name is one
// more, and every other byte is the same or an offset into the file moved by the probe's bytes. Anything else, and the
// package is left as it is.
#pragma once
#include "package_reader.h"
#include "export_package.h"
#include <cctype>

namespace P4MapKit { namespace Export {

inline bool EngineNameLess(const std::string& A, const std::string& B)
{
    const std::size_t Shared = std::min(A.size(), B.size());
    for (std::size_t At = 0; At < Shared; ++At)
    {
        const int Left = std::tolower(static_cast<unsigned char>(A[At])), Right = std::tolower(static_cast<unsigned char>(B[At]));
        if (Left != Right) return Left < Right;
    }
    return A.size() < B.size();
}

namespace NameSortDetail
{
struct FNameTables
{
    std::size_t NameCount = 0, NamesAt = 0, ExportCount = 0, ExportsAt = 0, ImportCount = 0, ImportsAt = 0,
        SoftCount = 0, SoftAt = 0;
    std::vector<std::string> Names;
    // Each name's entry as the header has it (its string and its hashes), and where the table ends.
    std::vector<std::vector<std::uint8_t>> Entries;
    std::size_t NamesEnd = 0;
};

inline bool ReadTables(const std::vector<std::uint8_t>& Header, FNameTables& Out, std::string& Error)
{
    const auto Fail = [&](const char* Why) { Error = Why; return false; };
    FByteReader Summary(Header, 20);
    std::size_t Count = 0, Size = 0;
    std::string Folder;
    std::uint64_t Flags = 0, Value = 0;
    if (!Summary.Count(Count, 1000) || !Summary.Skip(Count * 20) || !Summary.Count(Size, Header.size()) ||
        !Summary.String(Folder) || !Summary.Unsigned(4, Flags))
        return Fail("name-sort-summary");
    const auto Number = [&](std::size_t& Into) { return Summary.Unsigned(4, Value) && (Into = std::size_t(Value), true); };
    if (!Number(Out.NameCount) || !Number(Out.NamesAt) || !Summary.Skip(8) || !Number(Out.ExportCount) ||
        !Number(Out.ExportsAt) || !Number(Out.ImportCount) || !Number(Out.ImportsAt) || !Summary.Skip(4) ||
        !Number(Out.SoftCount) || !Number(Out.SoftAt))
        return Fail("name-sort-summary-tables");
    if (Out.NameCount > 1000000 || Out.ExportsAt + Out.ExportCount * 104 > Header.size() ||
        Out.ImportsAt + Out.ImportCount * 28 > Header.size() || Out.SoftAt + Out.SoftCount * 8 > Header.size())
        return Fail("name-sort-tables-outside-header");
    std::size_t At = Out.NamesAt;
    for (std::size_t Index = 0; Index < Out.NameCount; ++Index)
    {
        std::uint64_t Length = 0;
        if (!PackageRewriteDetail::Scalar(Header, At, Length) || std::int32_t(Length) <= 0 || Length > 65536 ||
            At + 4 + Length + 4 > Header.size() || Header[At + 4 + Length - 1] != 0)
            return Fail("name-sort-name-not-plain");
        Out.Names.emplace_back(Header.begin() + At + 4, Header.begin() + At + 4 + Length - 1);
        Out.Entries.emplace_back(Header.begin() + At, Header.begin() + At + 4 + Length + 4);
        At += 4 + Length + 4;
    }
    Out.NamesEnd = At;
    return true;
}

// The places of the header that refer to a name: each import's class package, class and name, each export's name,
// and each soft package reference.
inline std::vector<std::size_t> HeaderNamePlaces(const FNameTables& Tables)
{
    std::vector<std::size_t> Places;
    for (std::size_t Index = 0; Index < Tables.ImportCount; ++Index)
        for (const std::size_t Field : {0u, 8u, 20u})
            Places.push_back(Tables.ImportsAt + Index * 28 + Field);
    for (std::size_t Index = 0; Index < Tables.ExportCount; ++Index)
        Places.push_back(Tables.ExportsAt + Index * 104 + 16);
    for (std::size_t Index = 0; Index < Tables.SoftCount; ++Index)
        Places.push_back(Tables.SoftAt + Index * 8);
    return Places;
}

inline std::int64_t Signed(const std::vector<std::uint8_t>& Bytes, std::size_t At, std::size_t Width)
{
    std::uint64_t Value = 0;
    for (std::size_t Index = 0; Index < Width; ++Index) Value |= std::uint64_t(Bytes[At + Index]) << (8 * Index);
    return Width == 4 ? std::int64_t(std::int32_t(std::uint32_t(Value))) : std::int64_t(Value);
}
} // namespace NameSortDetail

// Puts the name table where the engine's save writes it, right after the summary. A translation that adds names
// (RebuildCookedExports) writes the whole table again at the end of the header and leaves the old one where it was,
// which makes the header longer than the save's and moves every offset into the file after it.
inline bool PlaceNamesAfterSummary(const std::vector<std::uint8_t>& Header, const std::vector<std::uint8_t>& Body,
    RewrittenPackage& Out, std::string& Error)
{
    using namespace PackageRewriteDetail;
    const auto Fail = [&](const char* Why) { Error = Why; return false; };
    Out = {Header, Body, false};
    FByteReader Summary(Header, 20);
    std::size_t Count = 0, Declared = 0;
    std::string Text;
    if (!Summary.Count(Count, 1000) || !Summary.Skip(Count * 20)) return Fail("name-place-custom-versions");
    const auto HeaderSizeAt = Summary.Tell();
    if (!Summary.Count(Declared, Header.size()) || !Summary.String(Text)) return Fail("name-place-summary");
    const auto FlagsAt = Summary.Tell();
    if (!Summary.Skip(72) || !Summary.Count(Count, 100) || !Summary.Skip(Count * 8)) return Fail("name-place-generations");
    for (int Index = 0; Index < 2; ++Index)
        if (!Summary.Skip(10) || !Summary.String(Text)) return Fail("name-place-engine-version");
    if (!Summary.Skip(4) || !Summary.Count(Count, 0) || !Summary.Skip(4) || !Summary.Count(Count, 100))
        return Fail("name-place-compression");
    for (std::size_t Index = 0; Index < Count; ++Index)
        if (!Summary.String(Text)) return Fail("name-place-additional-packages");
    const auto RegistryAt = Summary.Tell(), BulkAt = RegistryAt + 4;
    if (!Summary.Skip(12)) return Fail("name-place-bulk");
    const auto WorldTileAt = Summary.Tell();
    if (!Summary.Skip(4) || !Summary.Count(Count, 100) || !Summary.Skip(Count * 4)) return Fail("name-place-chunks");
    const auto PreloadAt = Summary.Tell();
    if (!Summary.Skip(8)) return Fail("name-place-preload");
    const auto SummaryEnd = Summary.Tell();
    NameSortDetail::FNameTables Tables;
    if (!NameSortDetail::ReadTables(Header, Tables, Error))
        return false;
    if (Tables.NamesAt == SummaryEnd)
        return true;
    if (Tables.NamesEnd != Header.size() || Tables.NamesAt < SummaryEnd)
        return Fail("name-place-table-not-at-the-end");
    // The old table runs from the summary to the first other table, and is a table of names: no more of them than now
    // (a translation that renames one, as a cook's relabelling does, changes it in the new table only).
    const std::vector<std::size_t> Offsets{FlagsAt + 16, FlagsAt + 24, FlagsAt + 32, FlagsAt + 36, FlagsAt + 44,
        FlagsAt + 48, FlagsAt + 52, RegistryAt, WorldTileAt, PreloadAt + 4};
    std::size_t OldEnd = Tables.NamesAt;
    for (const std::size_t At : Offsets)
    {
        std::uint64_t Value = 0;
        if (!Scalar(Header, At, Value)) return Fail("name-place-offset");
        if (Value && Value >= SummaryEnd && Value < OldEnd) OldEnd = std::size_t(Value);
    }
    std::size_t At = SummaryEnd, Old = 0;
    while (At < OldEnd)
    {
        std::uint64_t Length = 0;
        if (++Old > Tables.Entries.size() || !Scalar(Header, At, Length) || std::int32_t(Length) <= 0 ||
            Length > 65536 || At + 4 + Length + 4 > OldEnd || Header[At + 4 + Length - 1] != 0)
            return Fail("name-place-old-table-not-names");
        At += 4 + std::size_t(Length) + 4;
    }
    std::vector<std::uint8_t> Placed(Header.begin(), Header.begin() + SummaryEnd);
    Placed.insert(Placed.end(), Header.begin() + Tables.NamesAt, Header.end());
    Placed.insert(Placed.end(), Header.begin() + OldEnd, Header.begin() + Tables.NamesAt);
    const std::int64_t Table = std::int64_t(Header.size() - Tables.NamesAt), Shift = Table - std::int64_t(OldEnd - SummaryEnd);
    const std::int64_t Shorter = std::int64_t(Placed.size()) - std::int64_t(Header.size());
    for (const std::size_t Field : Offsets)
    {
        std::uint64_t Value = 0;
        Scalar(Header, Field, Value);
        if (Value >= OldEnd) Put(Placed, Field, std::uint64_t(std::int64_t(Value) + Shift));
    }
    Put(Placed, FlagsAt + 8, SummaryEnd);
    Put(Placed, HeaderSizeAt, Placed.size());
    std::uint64_t Bulk = 0, ExportsAt = 0;
    Scalar(Header, BulkAt, Bulk, 8);
    Put(Placed, BulkAt, std::uint64_t(std::int64_t(Bulk) + Shorter), 8);
    Scalar(Placed, FlagsAt + 24, ExportsAt);
    for (std::size_t Index = 0; Index < Tables.ExportCount; ++Index)
    {
        std::uint64_t Serial = 0;
        Scalar(Placed, std::size_t(ExportsAt) + Index * 104 + 36, Serial, 8);
        Put(Placed, std::size_t(ExportsAt) + Index * 104 + 36, std::uint64_t(std::int64_t(Serial) + Shorter), 8);
    }
    const auto Before = ReadPackageHeader(Header, Body.size()), After = ReadPackageHeader(Placed, Body.size());
    if (!After.Error.Code.empty() || After.Names != Before.Names || After.Imports.size() != Before.Imports.size() ||
        After.Exports.size() != Before.Exports.size())
        return Fail("name-place-readback");
    for (std::size_t Index = 0; Index < Before.Exports.size(); ++Index)
        if (After.Exports[Index].Name != Before.Exports[Index].Name || After.Exports[Index].Size != Before.Exports[Index].Size ||
            After.Exports[Index].Offset != Before.Exports[Index].Offset)
            return Fail("name-place-readback-export");
    Out.Header = std::move(Placed);
    Out.Changed = true;
    Error.clear();
    return true;
}

inline bool CookedNamesSorted(const std::vector<std::uint8_t>& Header)
{
    NameSortDetail::FNameTables Tables;
    std::string Error;
    return NameSortDetail::ReadTables(Header, Tables, Error) &&
        std::is_sorted(Tables.Names.begin(), Tables.Names.end(), EngineNameLess);
}

// Writes Header and Body with their names in the engine's order, given the same package saved with Probe added.
// Changed is false where they were in that order already.
inline bool SortCookedNames(const std::vector<std::uint8_t>& Header, const std::vector<std::uint8_t>& Body,
    const std::vector<std::uint8_t>& ProbeHeader, const std::vector<std::uint8_t>& ProbeBody, const std::string& Probe,
    RewrittenPackage& Out, std::string& Error)
{
    using namespace NameSortDetail;
    const auto Fail = [&](const char* Why) { Error = Why; return false; };
    FNameTables Tables, Probed;
    if (!ReadTables(Header, Tables, Error) || !ReadTables(ProbeHeader, Probed, Error))
        return false;
    Out = {Header, Body, false};
    if (std::is_sorted(Tables.Names.begin(), Tables.Names.end(), EngineNameLess))
        return true;
    // Where each name is in the probe's package. Names alike but for case have no order of their own in the save, so
    // two such can change places between the saves; every name still has to be at another place there, in a way its
    // lowest byte shows.
    if (Probed.Names.size() != Tables.Names.size() + 1 || Probed.Names[0] != Probe)
        return Fail("name-sort-probe-not-first");
    std::map<std::string, std::size_t> ProbePlace;
    for (std::size_t Index = 0; Index < Probed.Names.size(); ++Index)
        ProbePlace[Probed.Names[Index]] = Index;
    std::vector<std::int64_t> There(Tables.Names.size());
    for (std::size_t Index = 0; Index < Tables.Names.size(); ++Index)
    {
        const auto Found = ProbePlace.find(Tables.Names[Index]);
        if (Found == ProbePlace.end() || Found->second == 0 || ((Found->second ^ Index) & 0xff) == 0)
            return Fail("name-sort-probe-names-differ");
        There[Index] = std::int64_t(Found->second);
    }
    if (Probed.ExportCount != Tables.ExportCount || Probed.ImportCount != Tables.ImportCount ||
        ProbeBody.size() != Body.size() || ProbeHeader.size() <= Header.size())
        return Fail("name-sort-probe-package-differs");
    for (std::size_t Index = 0; Index < Tables.ExportCount; ++Index)
        if (!std::equal(Header.begin() + Tables.ExportsAt + Index * 104 + 28,
                Header.begin() + Tables.ExportsAt + Index * 104 + 36,
                ProbeHeader.begin() + Probed.ExportsAt + Index * 104 + 28))
            return Fail("name-sort-probe-export-sizes-differ");
    // An offset into the file moves by what the probe added to the header: its entry in the name table, once for each
    // table the header held when the offset was written (a translation that adds names writes the table again), or
    // what the header grew by in all. That move has to show in its lowest byte, which every changed reference to a
    // name does too, and an offset is never a name's index.
    const std::int64_t Moved = std::int64_t(ProbeHeader.size() - Header.size()), Entry = std::int64_t(Probe.size()) + 9;
    if (!(Moved & 0xff) || !(Entry & 0xff))
        return Fail("name-sort-probe-size-ambiguous");
    const auto Offset = [&](std::int64_t Was, std::int64_t Now)
    {
        const std::int64_t By = Now - Was;
        return Was >= std::int64_t(Tables.Names.size()) &&
            (By == Moved || By == Entry || By == 2 * Entry || By == 3 * Entry || By == 4 * Entry);
    };
    std::vector<std::size_t> BodyPlaces;
    for (std::size_t At = 0; At < Body.size();)
    {
        if (Body[At] == ProbeBody[At])
        {
            ++At;
            continue;
        }
        if (At + 4 <= Body.size())
        {
            const std::int64_t Was = Signed(Body, At, 4), Now = Signed(ProbeBody, At, 4);
            if (Was >= 0 && std::size_t(Was) < Tables.Names.size() && Now == There[std::size_t(Was)])
            {
                BodyPlaces.push_back(At);
                At += 4;
                continue;
            }
            if (Offset(Was, Now))
            {
                At += 4;
                continue;
            }
        }
        if (At + 8 <= Body.size() && Offset(Signed(Body, At, 8), Signed(ProbeBody, At, 8)))
        {
            At += 8;
            continue;
        }
        Error = "name-sort-probe-difference-not-a-name at body byte " + std::to_string(At) + ": " +
            std::to_string(Signed(Body, At, std::min<std::size_t>(4, Body.size() - At) == 4 ? 4 : 1)) + " became " +
            std::to_string(Signed(ProbeBody, At, std::min<std::size_t>(4, Body.size() - At) == 4 ? 4 : 1));
        return false;
    }
    // The header's references move the same way, but for the one export that carried the probe's name.
    const auto Places = HeaderNamePlaces(Tables), ProbePlaces = HeaderNamePlaces(Probed);
    std::size_t Renamed = 0;
    for (std::size_t Index = 0; Index < Places.size(); ++Index)
    {
        const std::int64_t Was = Signed(Header, Places[Index], 4), Now = Signed(ProbeHeader, ProbePlaces[Index], 4);
        const bool Export = Index >= Tables.ImportCount * 3 && Index < Tables.ImportCount * 3 + Tables.ExportCount;
        if (Was < 0 || std::size_t(Was) >= Tables.Names.size())
            return Fail("name-sort-header-reference-outside-names");
        if (Now == There[std::size_t(Was)])
            continue;
        if (!Export || Now != 0 || ++Renamed > 1)
            return Fail("name-sort-probe-header-reference");
    }
    // The engine's order. Two names alike but for case keep the order the save gave them.
    std::vector<std::size_t> Order(Tables.Names.size());
    for (std::size_t Index = 0; Index < Order.size(); ++Index) Order[Index] = Index;
    std::stable_sort(Order.begin(), Order.end(),
        [&](std::size_t A, std::size_t B) { return EngineNameLess(Tables.Names[A], Tables.Names[B]); });
    std::vector<std::uint32_t> Moves(Order.size());
    for (std::size_t Index = 0; Index < Order.size(); ++Index) Moves[Order[Index]] = std::uint32_t(Index);
    std::size_t At = Tables.NamesAt;
    for (const std::size_t Index : Order)
    {
        std::copy(Tables.Entries[Index].begin(), Tables.Entries[Index].end(), Out.Header.begin() + At);
        At += Tables.Entries[Index].size();
    }
    if (At != Tables.NamesEnd)
        return Fail("name-sort-table-size");
    for (const std::size_t Place : Places)
        PackageRewriteDetail::Put(Out.Header, Place, Moves[std::size_t(Signed(Header, Place, 4))]);
    for (const std::size_t Place : BodyPlaces)
        PackageRewriteDetail::Put(Out.Body, Place, Moves[std::size_t(Signed(Body, Place, 4))]);
    // Read again: every import and export names what it named before.
    const auto Before = ReadPackageHeader(Header, Body.size()), After = ReadPackageHeader(Out.Header, Out.Body.size());
    if (!Before.Error.Code.empty() || !After.Error.Code.empty() || Before.Imports.size() != After.Imports.size() ||
        Before.Exports.size() != After.Exports.size())
        return Fail("name-sort-readback");
    for (std::size_t Index = 0; Index < Before.Imports.size(); ++Index)
        if (Before.Imports[Index].Name != After.Imports[Index].Name ||
            Before.Imports[Index].Class != After.Imports[Index].Class ||
            Before.Imports[Index].ClassPackage != After.Imports[Index].ClassPackage)
            return Fail("name-sort-readback-import");
    for (std::size_t Index = 0; Index < Before.Exports.size(); ++Index)
        if (Before.Exports[Index].Name != After.Exports[Index].Name)
            return Fail("name-sort-readback-export");
    if (!std::is_sorted(After.Names.begin(), After.Names.end(), EngineNameLess))
        return Fail("name-sort-readback-order");
    Out.Changed = true;
    Error.clear();
    return true;
}
} } // namespace P4MapKit::Export

// Two cooked packages of the same asset compared: the game's original and one cooked again from an import. Equal
// bytes end it. Otherwise each part of the files is compared on its own (the header's summary, names, imports and
// export table; each export's payload; the bulk file), and every difference is said where it is, so a difference
// reads as which object or table changed, not only as an offset.
#pragma once
#include "package_reader.h"
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace P4MapKit
{
struct FPackageFiles
{
    std::vector<std::uint8_t> Header, Body, Bulk;
};

struct FPackageComparison
{
    bool HeaderSame = false, BodySame = false, BulkSame = false;
    std::size_t ExportsCompared = 0, ExportsDiffering = 0;
    // One line each: where, then what differs. At most Limit lines; Truncated counts those left out.
    std::vector<std::string> Differences;
    std::size_t Truncated = 0;
    // Set when either package could not be read; the byte results above still hold.
    std::string Error;
    bool Same() const { return HeaderSame && BodySame && BulkSame; }
};

namespace PackageCompareDetail
{
inline std::size_t FirstDifference(const std::uint8_t* A, std::size_t ASize, const std::uint8_t* B, std::size_t BSize)
{
    const std::size_t Size = ASize < BSize ? ASize : BSize;
    for (std::size_t At = 0; At < Size; ++At)
        if (A[At] != B[At]) return At;
    return Size;
}

inline std::string Hex(std::uint64_t Value)
{
    static const char* Digits = "0123456789abcdef";
    std::string Text;
    do { Text.insert(Text.begin(), Digits[Value & 15]); Value >>= 4; } while (Value);
    return "0x" + Text;
}

// An import or export by its full path, with its class, so the two packages' tables compare by what they name.
inline std::string Identity(const FPackageInventory& Package, std::int32_t Index)
{
    std::string Path;
    if (!Package.Reference(Index, Path)) return "<unreadable " + std::to_string(Index) + ">";
    if (Index < 0)
    {
        const auto& Import = Package.Imports[std::size_t(-std::int64_t(Index) - 1)];
        return Path + " (" + Import.ClassPackage + "." + Import.Class + ")";
    }
    if (Index > 0)
    {
        std::string Class;
        Package.Reference(Package.Exports[std::size_t(Index - 1)].Class, Class);
        return Path + " (" + Class + ")";
    }
    return Path;
}

inline std::string Joined(const std::set<std::string>& Items, std::size_t Limit = 8)
{
    std::string Text;
    std::size_t Count = 0;
    for (const auto& Item : Items)
    {
        if (Count == Limit) { Text += ", and " + std::to_string(Items.size() - Limit) + " more"; break; }
        Text += (Count++ ? ", " : "") + Item;
    }
    return Text;
}
} // namespace PackageCompareDetail

inline FPackageComparison ComparePackages(const FPackageFiles& Game, const FPackageFiles& Ours, std::size_t Limit = 200)
{
    using namespace PackageCompareDetail;
    FPackageComparison Result;
    Result.HeaderSame = Game.Header == Ours.Header;
    Result.BodySame = Game.Body == Ours.Body;
    Result.BulkSame = Game.Bulk == Ours.Bulk;
    const auto Say = [&](const std::string& Line)
    {
        if (Result.Differences.size() < Limit) Result.Differences.push_back(Line);
        else ++Result.Truncated;
    };
    if (Result.Same()) return Result;
    const FPackageInventory A = ReadPackageHeader(Game.Header, Game.Body.size());
    const FPackageInventory B = ReadPackageHeader(Ours.Header, Ours.Body.size());
    if (!A.Error.Code.empty() || !B.Error.Code.empty())
    {
        Result.Error = !A.Error.Code.empty() ? "game package unreadable: " + A.Error.Code
                                            : "cooked package unreadable: " + B.Error.Code;
        return Result;
    }
    if (A.Package != B.Package) Say("summary: package name " + A.Package + " became " + B.Package);
    if (A.Flags != B.Flags) Say("summary: package flags " + Hex(A.Flags) + " became " + Hex(B.Flags));
    if (Game.Header.size() != Ours.Header.size())
        Say("header: " + std::to_string(Game.Header.size()) + " bytes became " + std::to_string(Ours.Header.size()));
    if (Game.Body.size() != Ours.Body.size())
        Say("body: " + std::to_string(Game.Body.size()) + " bytes became " + std::to_string(Ours.Body.size()));
    if (!Result.BulkSame)
        Say("bulk: " + std::to_string(Game.Bulk.size()) + " bytes became " + std::to_string(Ours.Bulk.size()) +
            (Game.Bulk.size() == Ours.Bulk.size()
                    ? ", first difference at " +
                        Hex(FirstDifference(Game.Bulk.data(), Game.Bulk.size(), Ours.Bulk.data(), Ours.Bulk.size()))
                    : std::string()));

    // Names: what only one side has, then whether the shared ones keep their order (an index into the table is
    // what every object's data stores, so a different order changes bytes everywhere without changing content).
    const std::set<std::string> NamesA(A.Names.begin(), A.Names.end()), NamesB(B.Names.begin(), B.Names.end());
    std::set<std::string> OnlyA, OnlyB;
    for (const auto& Name : NamesA) if (!NamesB.count(Name)) OnlyA.insert(Name);
    for (const auto& Name : NamesB) if (!NamesA.count(Name)) OnlyB.insert(Name);
    if (!OnlyA.empty()) Say("names: only the game's has " + Joined(OnlyA));
    if (!OnlyB.empty()) Say("names: only ours has " + Joined(OnlyB));
    if (OnlyA.empty() && OnlyB.empty() && A.Names != B.Names)
        for (std::size_t At = 0; At < A.Names.size(); ++At)
            if (A.Names[At] != B.Names[At])
            {
                Say("names: same names, other order from index " + std::to_string(At) + " (" + A.Names[At] +
                    " / " + B.Names[At] + ")");
                break;
            }

    // Imports by identity, then order.
    std::vector<std::string> ImportsA, ImportsB;
    for (std::size_t At = 0; At < A.Imports.size(); ++At) ImportsA.push_back(Identity(A, -1 - std::int32_t(At)));
    for (std::size_t At = 0; At < B.Imports.size(); ++At) ImportsB.push_back(Identity(B, -1 - std::int32_t(At)));
    {
        const std::set<std::string> SetA(ImportsA.begin(), ImportsA.end()), SetB(ImportsB.begin(), ImportsB.end());
        std::set<std::string> Lost, Added;
        for (const auto& Item : SetA) if (!SetB.count(Item)) Lost.insert(Item);
        for (const auto& Item : SetB) if (!SetA.count(Item)) Added.insert(Item);
        if (!Lost.empty()) Say("imports: only the game's has " + Joined(Lost));
        if (!Added.empty()) Say("imports: only ours has " + Joined(Added));
        if (Lost.empty() && Added.empty() && ImportsA != ImportsB) Say("imports: same imports, other order");
    }

    // Exports by identity: the table entry, then the payload.
    std::map<std::string, std::size_t> ExportsB;
    std::vector<std::string> OrderA, OrderB;
    for (std::size_t At = 0; At < B.Exports.size(); ++At)
    {
        const std::string Name = Identity(B, std::int32_t(At + 1));
        ExportsB.emplace(Name, At);
        OrderB.push_back(Name);
    }
    std::set<std::string> Missing, Extra;
    for (std::size_t At = 0; At < A.Exports.size(); ++At)
    {
        const std::string Name = Identity(A, std::int32_t(At + 1));
        OrderA.push_back(Name);
        const auto Found = ExportsB.find(Name);
        if (Found == ExportsB.end()) { Missing.insert(Name); continue; }
        ++Result.ExportsCompared;
        const auto& X = A.Exports[At];
        const auto& Y = B.Exports[Found->second];
        std::vector<std::string> What;
        if (X.Flags != Y.Flags) What.push_back("object flags " + Hex(X.Flags) + " became " + Hex(Y.Flags));
        if (Identity(A, X.Template) != Identity(B, Y.Template))
            What.push_back("template " + Identity(A, X.Template) + " became " + Identity(B, Y.Template));
        if (Identity(A, X.Super) != Identity(B, Y.Super))
            What.push_back("super " + Identity(A, X.Super) + " became " + Identity(B, Y.Super));
        if (X.Filters != Y.Filters) What.push_back("load filters differ");
        if (X.PackageFlags != Y.PackageFlags || X.NotAlwaysLoaded != Y.NotAlwaysLoaded || X.IsAsset != Y.IsAsset)
            What.push_back("export entry flags differ");
        if (X.DependencyCounts != Y.DependencyCounts) What.push_back("dependency counts differ");
        if (X.Size != Y.Size) What.push_back("payload " + std::to_string(X.Size) + " bytes became " + std::to_string(Y.Size));
        const std::size_t At2 = FirstDifference(Game.Body.data() + X.Offset, X.Size, Ours.Body.data() + Y.Offset, Y.Size);
        if (X.Size == Y.Size && At2 < X.Size) What.push_back("payload differs from byte " + std::to_string(At2));
        else if (X.Size != Y.Size && At2 < (X.Size < Y.Size ? X.Size : Y.Size))
            What.push_back("payload differs from byte " + std::to_string(At2));
        if (What.empty()) continue;
        ++Result.ExportsDiffering;
        std::string Line = "export " + Name + ":";
        for (std::size_t Part = 0; Part < What.size(); ++Part) Line += (Part ? "; " : " ") + What[Part];
        Say(Line);
    }
    for (const auto& Name : OrderB)
        if (std::find(OrderA.begin(), OrderA.end(), Name) == OrderA.end()) Extra.insert(Name);
    if (!Missing.empty()) Say("exports: only the game's has " + Joined(Missing));
    if (!Extra.empty()) Say("exports: only ours has " + Joined(Extra));
    if (Missing.empty() && Extra.empty() && OrderA != OrderB) Say("exports: same objects, other order");

    // What lies outside the tables and payloads: the summary, and the header's and body's other parts.
    const auto Spans = [&](const std::vector<FInventorySpan>& SpansA, const std::vector<std::uint8_t>& BytesA,
                           const std::vector<FInventorySpan>& SpansB, const std::vector<std::uint8_t>& BytesB,
                           const char* File)
    {
        std::map<std::string, std::vector<const FInventorySpan*>> ByKind;
        for (const auto& Span : SpansB) ByKind[Span.Kind].push_back(&Span);
        std::map<std::string, std::size_t> Seen;
        for (const auto& Span : SpansA)
        {
            if (Span.Kind == "export-payload" || Span.Kind == "names-and-hashes" || Span.Kind == "imports" ||
                Span.Kind == "exports")
                continue;
            const std::size_t Nth = Seen[Span.Kind]++;
            const auto& Others = ByKind[Span.Kind];
            if (Nth >= Others.size())
            {
                Say(std::string(File) + ": the game's " + Span.Kind + " part at " + Hex(Span.Offset) + " has no counterpart");
                continue;
            }
            const auto& Other = *Others[Nth];
            if (Span.Size != Other.Size ||
                FirstDifference(BytesA.data() + Span.Offset, Span.Size, BytesB.data() + Other.Offset, Other.Size) < Span.Size)
                Say(std::string(File) + ": " + Span.Kind + " part (" + std::to_string(Span.Size) + " bytes at " +
                    Hex(Span.Offset) + ") differs" +
                    (Span.Size != Other.Size ? ", ours is " + std::to_string(Other.Size) + " bytes" : std::string()));
        }
    };
    Spans(A.HeaderSpans, Game.Header, B.HeaderSpans, Ours.Header, "header");
    Spans(A.BodySpans, Game.Body, B.BodySpans, Ours.Body, "body");
    return Result;
}
} // namespace P4MapKit

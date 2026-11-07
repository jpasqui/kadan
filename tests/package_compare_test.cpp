// The package comparison says which part of two cooked packages differs: equal files, a changed payload, a lost
// name or import, other object flags, a different export and a different bulk file.
#include "core/package_compare.h"
#include <cstdlib>
#include <iostream>

static int Checks = 0;
static void Check(bool Pass)
{
    ++Checks;
    if (!Pass) { std::cerr << "package compare check failed: " << Checks << '\n'; std::exit(1); }
}
static void Put(std::vector<std::uint8_t>& Bytes, std::uint64_t Value, std::size_t Width = 4)
{
    for (std::size_t Byte = 0; Byte < Width; ++Byte) Bytes.push_back(std::uint8_t(Value >> (8 * Byte)));
}
static void Patch(std::vector<std::uint8_t>& Bytes, std::size_t At, std::uint64_t Value, std::size_t Width = 4)
{
    for (std::size_t Byte = 0; Byte < Width; ++Byte) Bytes[At + Byte] = std::uint8_t(Value >> (8 * Byte));
}
static void String(std::vector<std::uint8_t>& Bytes, const std::string& Text)
{
    Put(Bytes, Text.size() + 1);
    Bytes.insert(Bytes.end(), Text.begin(), Text.end());
    Bytes.push_back(0);
}

// A cooked package with one export, a data table named by the last name, holding Payload.
static P4MapKit::FPackageFiles Package(const std::vector<std::string>& Names, std::uint32_t ExportName,
    const std::vector<std::uint8_t>& Payload, std::uint32_t ObjectFlags = 1)
{
    P4MapKit::FPackageFiles Files;
    auto& Header = Files.Header;
    Put(Header, 0x9e2a83c1); Put(Header, UINT32_MAX - 6);
    for (int Index = 0; Index < 5; ++Index) Put(Header, 0);
    String(Header, "/Game/Original");
    const std::size_t Flags = Header.size();
    Put(Header, 0x80000000);
    for (int Index = 0; Index < 17; ++Index) Put(Header, 0);
    const std::size_t NameAt = Header.size();
    for (const auto& Name : Names) { String(Header, Name); Put(Header, 0); }
    const auto Index = [&](const std::string& Name)
    { return std::uint32_t(std::find(Names.begin(), Names.end(), Name) - Names.begin()); };
    const std::size_t ImportAt = Header.size();
    // /Script/Engine (a Package) and DataTable (a Class inside it): class package, class, outer, name.
    for (const std::uint32_t Word : {Index("/Script/Engine"), 0u, Index("Package"), 0u, 0u, Index("/Script/Engine"), 0u,
             Index("/Script/Engine"), 0u, Index("Class"), 0u, UINT32_MAX, Index("DataTable"), 0u})
        Put(Header, Word);
    const std::size_t ExportAt = Header.size();
    Put(Header, UINT32_MAX - 1); Put(Header, 0); Put(Header, 0); Put(Header, 0);
    Put(Header, ExportName); Put(Header, 0); Put(Header, ObjectFlags);
    Put(Header, Payload.size(), 8); const std::size_t PayloadOffsetAt = Header.size(); Put(Header, 0, 8);
    for (int Count = 0; Count < 3; ++Count) Put(Header, 0);
    for (int Count = 0; Count < 4; ++Count) Put(Header, 0);
    Put(Header, 0); Put(Header, 0); Put(Header, 1); Put(Header, UINT32_MAX);
    for (int Count = 0; Count < 4; ++Count) Put(Header, 0);
    Patch(Header, 24, Header.size()); Patch(Header, PayloadOffsetAt, Header.size(), 8);
    Patch(Header, Flags + 4, Names.size()); Patch(Header, Flags + 8, NameAt);
    Patch(Header, Flags + 20, 1); Patch(Header, Flags + 24, ExportAt);
    Patch(Header, Flags + 28, 2); Patch(Header, Flags + 32, ImportAt);
    Files.Body = Payload;
    Files.Body.insert(Files.Body.end(), {0xc1, 0x83, 0x2a, 0x9e});
    return Files;
}

static bool Says(const P4MapKit::FPackageComparison& Result, const std::string& Text)
{
    for (const auto& Line : Result.Differences)
        if (Line.find(Text) != std::string::npos) return true;
    return false;
}

int main()
{
    using namespace P4MapKit;
    const std::vector<std::string> Names{"/Script/Engine", "Package", "Class", "DataTable", "Asset", "None"};
    const std::vector<std::uint8_t> Payload{5, 0, 0, 0, 0, 0, 0, 0};
    const auto Game = Package(Names, 4, Payload);
    Check(ReadPackageHeader(Game.Header, Game.Body.size()).Error.Code.empty());

    // Equal files are equal, and nothing is said.
    auto Result = ComparePackages(Game, Game);
    Check(Result.Same() && Result.Differences.empty() && Result.Error.empty());

    // A changed payload byte: the export, and where in it.
    auto Changed = Payload; Changed[3] = 9;
    Result = ComparePackages(Game, Package(Names, 4, Changed));
    Check(Result.HeaderSame && !Result.BodySame && Result.ExportsCompared == 1 && Result.ExportsDiffering == 1);
    Check(Says(Result, "export Asset (/Script/Engine.DataTable): payload differs from byte 3"));

    // A longer payload: its size, and the body's.
    auto Longer = Payload; Longer.push_back(1);
    Result = ComparePackages(Game, Package(Names, 4, Longer));
    Check(Says(Result, "payload 8 bytes became 9") && Says(Result, "body: 12 bytes became 13"));

    // Other object flags.
    Result = ComparePackages(Game, Package(Names, 4, Payload, 9));
    Check(Says(Result, "object flags 0x1 became 0x9"));

    // A name only one side has, and the same names in another order.
    auto More = Names; More.push_back("Extra");
    Result = ComparePackages(Game, Package(More, 4, Payload));
    Check(Says(Result, "names: only ours has Extra") && Result.ExportsDiffering == 0);
    std::vector<std::string> Swapped{"/Script/Engine", "Package", "Class", "DataTable", "None", "Asset"};
    Result = ComparePackages(Game, Package(Swapped, 5, Payload));
    Check(Says(Result, "names: same names, other order from index 4"));

    // Another export name: the game's export is missing from ours and ours is extra.
    std::vector<std::string> Renamed{"/Script/Engine", "Package", "Class", "DataTable", "Other", "None"};
    Result = ComparePackages(Game, Package(Renamed, 4, Payload));
    Check(Says(Result, "exports: only the game's has Asset (/Script/Engine.DataTable)"));
    Check(Says(Result, "exports: only ours has Other (/Script/Engine.DataTable)"));
    Check(Says(Result, "names: only the game's has Asset"));

    // A different bulk file.
    auto Bulk = Game; Bulk.Bulk = {1, 2, 3};
    auto BulkB = Game; BulkB.Bulk = {1, 2, 4};
    Result = ComparePackages(Bulk, BulkB);
    Check(!Result.BulkSame && Result.HeaderSame && Says(Result, "bulk: 3 bytes became 3, first difference at 0x2"));

    // A package that cannot be read is said so.
    auto Broken = Game; Broken.Header.resize(10);
    Result = ComparePackages(Game, Broken);
    Check(Result.Error.rfind("cooked package unreadable", 0) == 0);

    // The limit keeps the list short and counts the rest.
    Result = ComparePackages(Game, Package(Renamed, 4, Changed, 9), 1);
    Check(Result.Differences.size() == 1 && Result.Truncated > 0);
    std::cout << Checks << " package compare checks passed\n";
    return 0;
}

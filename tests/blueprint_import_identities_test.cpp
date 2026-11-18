// The names of a converted Blueprint's unresolved imports, recovered from a small original Zen package and its
// converted legacy pair, both written here. The hash is a stand-in for the engine's: the recovery only needs the same
// function on both sides.
#include "core/blueprint_import_identities.h"
#include <cstdlib>
#include <iostream>
#include <map>
using namespace P4MapKit::Blueprint;
#define CHECK(X) do { if (!(X)) { std::cerr << "Failed at " << __LINE__ << ": " << #X << '\n'; std::exit(1); } } while (false)

namespace
{
using FBytes = std::vector<std::uint8_t>;
constexpr std::uint64_t Mask = (std::uint64_t(1) << 62) - 1;
const std::string Package = "/Game/Test/BP_Thing";
const std::string Imported = "/Game/Other/Target";

void Put(FBytes& Bytes, std::uint64_t Value, std::size_t Width = 4)
{
    for (std::size_t Index = 0; Index < Width; ++Index)
        Bytes.push_back(std::uint8_t(Value >> (Index * 8)));
}
void Patch(FBytes& Bytes, std::size_t At, std::uint64_t Value, std::size_t Width = 4)
{
    for (std::size_t Index = 0; Index < Width; ++Index)
        Bytes[At + Index] = std::uint8_t(Value >> (Index * 8));
}
void String(FBytes& Bytes, const std::string& Text)
{
    Put(Bytes, Text.size() + 1);
    Bytes.insert(Bytes.end(), Text.begin(), Text.end());
    Bytes.push_back(0);
}

// FNV-1a, with paths a case may make collide.
std::map<std::string, std::string> SameHashAs;
std::uint64_t Hash(const std::string& Normalized)
{
    const auto Same = SameHashAs.find(Normalized);
    const std::string& Text = Same == SameHashAs.end() ? Normalized : Same->second;
    std::uint64_t Value = 14695981039346656037ull;
    for (const char C : Text)
        Value = (Value ^ std::uint8_t(C)) * 1099511628211ull;
    return Value;
}
std::uint64_t PackageImport(const std::string& Path)
{
    return (std::uint64_t(2) << 62) | (Hash(ImportIdentityDetail::Normalize(Path)) & Mask);
}

struct FFixture
{
    FBytes Header, Body, Raw, Metadata;
};

// One export, Thing, of a class the converter could not name: import 1, /Engine/UnknownPackage.UnknownExport. The
// original package's import table gives import 0 and import 1 the identities ZenImports holds.
FFixture Make(const std::vector<std::string>& Names, const std::vector<std::uint64_t>& ZenImports)
{
    const auto NameOf = [&](const std::string& Name)
    { return std::uint64_t(std::find(Names.begin(), Names.end(), Name) - Names.begin()); };
    FFixture Out;
    Out.Body = {1, 2, 3, 4};
    FBytes& Header = Out.Header;
    Put(Header, 0x9e2a83c1);
    Put(Header, std::uint32_t(-7));
    for (int Index = 0; Index < 5; ++Index)
        Put(Header, 0);
    String(Header, Package);
    const std::size_t Flags = Header.size();
    Put(Header, 0x80000000);
    for (int Index = 0; Index < 17; ++Index)
        Put(Header, 0);
    const std::size_t NamesAt = Header.size();
    for (const auto& Name : Names)
    {
        String(Header, Name);
        Put(Header, 0);
    }
    const std::size_t ImportsAt = Header.size();
    // Class package, class, outer and name of /Engine/UnknownPackage and its UnknownExport.
    for (const std::uint64_t Word : {NameOf("/Script/CoreUObject"), std::uint64_t(0), NameOf("Package"),
             std::uint64_t(0), std::uint64_t(0), NameOf("/Engine/UnknownPackage"), std::uint64_t(0),
             NameOf("/Script/CoreUObject"), std::uint64_t(0), NameOf("Class"), std::uint64_t(0),
             std::uint64_t(std::uint32_t(-1)), NameOf("UnknownExport"), std::uint64_t(0)})
        Put(Header, Word);
    const std::size_t ExportsAt = Header.size();
    Put(Header, std::uint32_t(-2));
    Put(Header, 0);
    Put(Header, 0);
    Put(Header, 0);
    Put(Header, NameOf("Thing"));
    Put(Header, 0);
    Put(Header, 1);
    Put(Header, Out.Body.size(), 8);
    const std::size_t OffsetAt = Header.size();
    Put(Header, 0, 8);
    for (int Index = 0; Index < 7; ++Index)
        Put(Header, 0);
    Put(Header, 0);
    Put(Header, 0);
    Put(Header, 1);
    Put(Header, std::uint32_t(-1));
    for (int Index = 0; Index < 4; ++Index)
        Put(Header, 0);
    Patch(Header, 24, Header.size());
    Patch(Header, OffsetAt, Header.size(), 8);
    Patch(Header, Flags + 4, Names.size());
    Patch(Header, Flags + 8, NamesAt);
    Patch(Header, Flags + 20, 1);
    Patch(Header, Flags + 24, ExportsAt);
    Patch(Header, Flags + 28, 2);
    Patch(Header, Flags + 32, ImportsAt);

    // The original Zen package: its summary, name map, name hashes, imports, the export, one bundle, an empty graph
    // and the export's payload.
    FBytes& Raw = Out.Raw;
    Raw.assign(64, 0);
    FBytes NameMap;
    for (const auto& Name : Names)
    {
        NameMap.push_back(std::uint8_t(Name.size() >> 8));
        NameMap.push_back(std::uint8_t(Name.size()));
        NameMap.insert(NameMap.end(), Name.begin(), Name.end());
    }
    const std::size_t CookedHeaderSize = 4096;
    std::array<std::uint32_t, 16> Summary{};
    Summary[0] = std::uint32_t(NameOf(Package));
    Summary[5] = std::uint32_t(CookedHeaderSize);
    Summary[6] = std::uint32_t(Raw.size());
    Raw.insert(Raw.end(), NameMap.begin(), NameMap.end());
    Summary[7] = std::uint32_t(NameMap.size());
    Summary[8] = std::uint32_t(Raw.size());
    Raw.resize(Raw.size() + 8 * (Names.size() + 1), 0);
    Summary[9] = std::uint32_t(Raw.size() - Summary[8]);
    Summary[10] = std::uint32_t(Raw.size());
    for (const auto Import : ZenImports)
        Put(Raw, Import, 8);
    Summary[11] = std::uint32_t(Raw.size());
    Put(Raw, CookedHeaderSize, 8);
    Put(Raw, Out.Body.size(), 8);
    Put(Raw, NameOf("Thing"));
    Put(Raw, 0);
    Put(Raw, UINT64_MAX, 8);
    Put(Raw, ZenImports[1], 8);
    Put(Raw, UINT64_MAX, 8);
    Put(Raw, UINT64_MAX, 8);
    Put(Raw, 0, 8);
    Put(Raw, 1);
    Put(Raw, 0);
    Summary[12] = std::uint32_t(Raw.size());
    Put(Raw, 0);
    Put(Raw, 2);
    Put(Raw, 0);
    Put(Raw, 0);
    Put(Raw, 0);
    Put(Raw, 1);
    Summary[13] = std::uint32_t(Raw.size());
    Put(Raw, 0);
    Summary[14] = 4;
    Raw.insert(Raw.end(), Out.Body.begin(), Out.Body.end());
    for (std::size_t Index = 0; Index < Summary.size(); ++Index)
        Patch(Raw, 4 * Index, Summary[Index]);

    const std::string Json = "{\"toc_version\":\"DirectoryIndex\",\"container_header_version\":\"Initial\","
                             "\"store_entry\":{\"export_bundle_count\":1,\"export_count\":1,\"imported_packages\":[" +
        std::to_string(Hash(ImportIdentityDetail::Normalize(Imported))) + "]}}";
    Out.Metadata.assign(Json.begin(), Json.end());
    return Out;
}

FVerifiedImportIdentities Recover(const FFixture& Fixture, const FImportIdentityExportNames& Exports = {})
{
    return RecoverImportIdentities(Fixture.Header, Fixture.Body, Fixture.Raw, Fixture.Metadata, Package, Hash, Exports);
}

std::string Refusal(const FFixture& Fixture, const FImportIdentityExportNames& Exports = {})
{
    try
    {
        Recover(Fixture, Exports);
    }
    catch (const Error& Refused)
    {
        return Refused.what();
    }
    return {};
}

bool Starts(const std::string& Text, const std::string& Start)
{
    return Text.compare(0, Start.size(), Start) == 0;
}

const std::vector<std::string> Common{Package, Imported, "None", "/Script/CoreUObject", "Package", "Class",
    "/Engine/UnknownPackage", "UnknownExport", "Thing"};
std::vector<std::string> With(std::initializer_list<const char*> More)
{
    auto Names = Common;
    Names.insert(Names.end(), More.begin(), More.end());
    return Names;
}
} // namespace

int main()
{
    const std::uint64_t Null = UINT64_MAX;

    // A name of the package spells the class's path, and only that path has the import's hash.
    {
        const auto Fixture = Make(With({"Target_C"}), {Null, PackageImport(Imported + ".Target_C")});
        const auto Found = Recover(Fixture);
        CHECK(Found.Package() == Package);
        CHECK(Found.Imports().size() == 1);
        CHECK(Found.Imports().count(-2) && Found.Imports().at(-2) == Imported + ".Target_C");
        CHECK(Found.MatchesSource(Fixture.Header, Fixture.Body));
        CHECK(!Found.MatchesSource(Fixture.Header, FBytes{1, 2, 3, 5}));
    }

    // A nested object is looked for only under an outer that is itself one of the package's imports: a path whose hash
    // the package does not import is not kept, so nothing is looked for under it.
    {
        const auto Nested = PackageImport(Imported + ".Thing.Target_C");
        const auto Lone = Refusal(Make(With({"Target_C"}), {Null, Nested}));
        CHECK(Starts(Lone, "import-identities-candidate-ambiguity: no name of the package spells import 1"));
        const auto Found = Recover(Make(With({"Target_C"}), {PackageImport(Imported + ".Thing"), Nested}));
        CHECK(Found.Imports().size() == 1 && Found.Imports().at(-2) == Imported + ".Thing.Target_C");
    }

    // The class is named nowhere in the package: the imported package's own export names spell it.
    {
        const auto Fixture = Make(Common, {Null, PackageImport(Imported + ".Target_C")});
        CHECK(Starts(Refusal(Fixture), "import-identities-candidate-ambiguity: no name of the package spells import 1"));
        std::vector<std::string> Asked;
        const auto Found = Recover(Fixture,
            [&](const std::string& Of)
            {
                Asked.push_back(Of);
                return std::vector<std::string>{"Target_C", "Default__Target_C", "not a name"};
            });
        CHECK(Asked == std::vector<std::string>{Imported});
        CHECK(Found.Imports().at(-2) == Imported + ".Target_C");
        // Export names that do not spell it do not help.
        CHECK(!Refusal(Fixture, [](const std::string&) { return std::vector<std::string>{"Other_C"}; }).empty());
    }

    // Two different paths with the import's hash: which one is meant cannot be told, so the package is refused.
    {
        SameHashAs[ImportIdentityDetail::Normalize(Imported + ".Thing")] =
            ImportIdentityDetail::Normalize(Imported + ".Target_C");
        CHECK(Refusal(Make(With({"Target_C"}), {Null, PackageImport(Imported + ".Target_C")})) ==
            "import-identities-candidate-ambiguity");
        SameHashAs.clear();
    }

    // Spellings that differ only by case are one object, as Unreal's names are.
    {
        const auto Found = Recover(Make(With({"Target_C", "target_c"}), {Null, PackageImport(Imported + ".Target_C")}));
        CHECK(Found.Imports().size() == 1 && ImportIdentityDetail::Normalize(Found.Imports().at(-2)) ==
            ImportIdentityDetail::Normalize(Imported + ".Target_C"));
    }

    // An import whose hash no path under an imported package has is refused, not guessed.
    {
        const auto Fixture = Make(With({"Target_C"}), {Null, PackageImport("/Engine/UnknownPackage.Nothing")});
        CHECK(!Refusal(Fixture).empty());
    }

    // The original package must be the converted one: a different payload is refused.
    {
        auto Fixture = Make(With({"Target_C"}), {Null, PackageImport(Imported + ".Target_C")});
        Fixture.Raw.back() ^= 1;
        CHECK(Refusal(Fixture) == "import-identities-payload-tail" ||
            Refusal(Fixture) == "import-identities-export-payload");
    }

    // The bound on the paths tried (400 million) is not reached by any input small enough to write here.
    std::cout << "blueprint import identities: passed\n";
    return 0;
}

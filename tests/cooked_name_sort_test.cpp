#include "../core/cooked_name_sort.h"
#include "../core/cooked_export_rebuild.h"
#include <iostream>
#include <cstdlib>
static void Check(bool Good, const char* What)
{
    if (!Good) { std::cerr << "cooked name sort check failed: " << What << "\n"; std::exit(1); }
}
static void Put(std::vector<std::uint8_t>& B, std::uint64_t V, std::size_t W = 4)
{ for (std::size_t I = 0; I < W; ++I) B.push_back(std::uint8_t(V >> (I * 8))); }
static void Patch(std::vector<std::uint8_t>& B, std::size_t A, std::uint64_t V, std::size_t W = 4)
{ P4MapKit::Export::PackageRewriteDetail::Put(B, A, V, W); }
static void String(std::vector<std::uint8_t>& B, const std::string& V)
{ P4MapKit::Export::PackageRewriteDetail::String(B, V); }

// A cooked package with two imports and one export, its names in the order given; the export's data refers to the
// names given by Refs. With Probe, the probe's name comes first and is the export's name, as a save of the package
// with the export renamed to it would have it.
struct FPackage { std::vector<std::uint8_t> Header, Body; };
static FPackage Make(const std::vector<std::string>& Names, const std::vector<std::string>& Refs, const char* Probe = nullptr)
{
    std::vector<std::string> All = Names;
    if (Probe) All.insert(All.begin(), Probe);
    const auto Index = [&](const std::string& Name)
    { return std::uint32_t(std::find(All.begin(), All.end(), Name) - All.begin()); };
    FPackage P; auto& H = P.Header;
    Put(H, 0x9e2a83c1); Put(H, UINT32_MAX - 6); for (int I = 0; I < 3; ++I) Put(H, 0);
    Put(H, 0);
    const auto SizeAt = H.size(); Put(H, 0); String(H, "/Game/Creator"); const auto Flags = H.size();
    Put(H, 0x80000000); for (int I = 0; I < 17; ++I) Put(H, 0);
    Put(H, 1); Put(H, 1); Put(H, 7);
    for (int I = 0; I < 2; ++I) { Put(H, 4, 2); Put(H, 26, 2); Put(H, 0, 2); Put(H, 0); String(H, ""); }
    Put(H, 0); Put(H, 0); Put(H, 123); Put(H, 0);
    Put(H, 0); const auto BulkAt = H.size(); Put(H, 0, 8); Put(H, 0); Put(H, 0);
    const auto PreloadAt = H.size(); Put(H, 1); Put(H, 0);
    const auto NamesAt = H.size();
    for (const auto& Name : All) { String(H, Name); Put(H, 123); }
    const auto Imports = H.size();
    for (const auto V : {Index("/Script/Engine"), 0u, Index("Package"), 0u, 0u, Index("/Script/Engine"), 0u,
             Index("/Script/Engine"), 0u, Index("Class"), 0u, UINT32_MAX, Index("StaticMesh"), 0u})
        Put(H, V);
    const auto ExportAt = H.size();
    Put(H, UINT32_MAX - 1); for (int I = 0; I < 3; ++I) Put(H, 0);
    Put(H, Probe ? 0u : Index("Mesh")); Put(H, Probe ? 0u : 1u); Put(H, 1);
    Put(H, 8 * Refs.size(), 8); const auto Offset = H.size(); Put(H, 0, 8);
    for (int I = 0; I < 10; ++I) Put(H, 0); Put(H, UINT32_MAX); for (int I = 0; I < 4; ++I) Put(H, 0);
    const auto Preload = H.size(); Put(H, UINT32_MAX);
    for (const auto& Ref : Refs) { Put(P.Body, Index(Ref)); Put(P.Body, 0); }
    P.Body.insert(P.Body.end(), {0xc1, 0x83, 0x2a, 0x9e});
    Patch(H, SizeAt, H.size()); Patch(H, Offset, H.size(), 8); Patch(H, BulkAt, H.size() + P.Body.size() - 4, 8);
    Patch(H, Flags + 4, All.size()); Patch(H, Flags + 8, NamesAt); Patch(H, Flags + 20, 1);
    Patch(H, Flags + 24, ExportAt); Patch(H, Flags + 28, 2); Patch(H, Flags + 32, Imports);
    Patch(H, PreloadAt + 4, Preload);
    return P;
}

int main()
{
    using namespace P4MapKit; using namespace P4MapKit::Export;
    Check(EngineNameLess("Ak_GEN_VARIABLE", "AkComponent"), "underscore before letters");
    Check(EngineNameLess("C4TerrainTag", "C4TerrainTagContainer"), "shorter first");
    Check(EngineNameLess("bNetAddressable", "BodyInstance"), "case-insensitive");
    Check(EngineNameLess("*P4NameProbe", "/Game/A"), "probe first");

    // The save sorted its names; a translation added Zed's neighbour "Alpha" after them.
    const std::vector<std::string> Names{"/Script/Engine", "Class", "Mesh", "None", "Package", "StaticMesh", "Zed", "Alpha"};
    const std::vector<std::string> Refs{"Zed", "Alpha", "None"};
    const auto Base = Make(Names, Refs), Probed = Make(Names, Refs, "*P4NameProbe");
    Check(!CookedNamesSorted(Base.Header), "base unsorted");
    RewrittenPackage Out; std::string Error;
    Check(SortCookedNames(Base.Header, Base.Body, Probed.Header, Probed.Body, "*P4NameProbe", Out, Error), Error.c_str());
    Check(Out.Changed && CookedNamesSorted(Out.Header), "sorted");
    std::vector<std::string> Sorted = Names;
    std::sort(Sorted.begin(), Sorted.end(), EngineNameLess);
    const auto Read = ReadPackageHeader(Out.Header, Out.Body.size());
    Check(Read.Error.Code.empty() && Read.Names == Sorted, "names in the engine's order");
    Check(Read.Exports.size() == 1 && Read.Exports[0].Name == "Mesh_0", "export name kept");
    Check(Read.Imports.size() == 2 && Read.Imports[1].Name == "StaticMesh" && Read.Imports[1].Class == "Class" &&
            Read.Imports[0].Class == "Package",
        "import names kept");
    const auto Expected = Make(Sorted, Refs);
    Check(Out.Body == Expected.Body, "body references follow their names");
    Check(Out.Header.size() == Base.Header.size(), "header size kept");

    // Already sorted: nothing changes.
    Check(SortCookedNames(Expected.Header, Expected.Body, Make(Sorted, Refs, "*P4NameProbe").Header,
              Make(Sorted, Refs, "*P4NameProbe").Body, "*P4NameProbe", Out, Error) &&
            !Out.Changed && Out.Header == Expected.Header,
        "sorted package left alone");

    // A difference the probe does not explain refuses.
    auto Odd = Probed;
    Odd.Body[4] ^= 0x10;
    Check(!SortCookedNames(Base.Header, Base.Body, Odd.Header, Odd.Body, "*P4NameProbe", Out, Error) &&
            Error.rfind("name-sort-probe-difference-not-a-name", 0) == 0,
        "unexplained difference refused");
    // A probe that does not come first refuses.
    const auto Late = Make(Names, Refs, "Zz");
    Check(!SortCookedNames(Base.Header, Base.Body, Late.Header, Late.Body, "*P4NameProbe", Out, Error), "probe name");
    // A translation that adds a name writes the table again at the end of the header; placing it after the summary
    // gives the layout the save would have written with that name.
    {
        const std::vector<std::string> Saved{"/Script/Engine", "Class", "Mesh", "None", "Package", "StaticMesh", "Zed"};
        const auto Before = Make(Saved, Refs);
        auto Added = Saved;
        Added.push_back("Alpha");
        RewrittenPackage Rebuilt;
        std::string Why;
        Check(RebuildCookedExports(Before.Header, Before.Body, Added, {}, Rebuilt, Why), Why.c_str());
        Check(Rebuilt.Header.size() > Before.Header.size() + 10, "rebuild wrote the table again");
        RewrittenPackage Placed;
        Check(PlaceNamesAfterSummary(Rebuilt.Header, Rebuilt.Body, Placed, Why) && Placed.Changed, Why.c_str());
        const auto Wanted = Make(Added, Refs);
        Check(Placed.Header.size() == Wanted.Header.size(), "placed header as long as the save's");
        const auto A = ReadPackageHeader(Placed.Header, Placed.Body.size());
        const auto W = ReadPackageHeader(Wanted.Header, Wanted.Body.size());
        Check(A.Error.Code.empty() && A.Names == W.Names && A.Exports.size() == 1 &&
                A.Exports[0].Offset == W.Exports[0].Offset && A.Exports[0].Size == W.Exports[0].Size &&
                A.Imports.size() == W.Imports.size(),
            "placed tables read as the save's");
        // The rebuild writes no hash for the name it adds; the rest is the save's layout byte for byte.
        std::size_t Differ = 0;
        for (std::size_t I = 0; I < Placed.Header.size(); ++I) Differ += Placed.Header[I] != Wanted.Header[I];
        Check(Differ == 1, "only the added name's hash differs");
        RewrittenPackage Again;
        Check(PlaceNamesAfterSummary(Placed.Header, Placed.Body, Again, Why) && !Again.Changed, "placed once");
    }
    std::cout << "cooked name sort: ok\n";
    return 0;
}

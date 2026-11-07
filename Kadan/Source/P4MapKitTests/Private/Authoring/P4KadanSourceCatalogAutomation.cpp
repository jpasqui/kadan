#include "P4KadanSourceCatalog.h"
#include "P4KadanContainerIndex.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanSourceCatalogBoundaryTest, "P4MapKit.Kadan.SourceCatalogBoundaries",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4KadanSourceCatalogBoundaryTest::RunTest(const FString&)
{
    const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() /
        TEXT("P4MapKit/Tests/SourceCatalog") / FGuid::NewGuid().ToString(EGuidFormats::Digits));
    ON_SCOPE_EXIT
    {
        IFileManager::Get().DeleteDirectory(*Root, false, true);
    };
    const FString Paks = Root / TEXT("Carrot4/Content/Paks");
    if (!IFileManager::Get().MakeDirectory(*Paks, true))
        return false;
    // Tiny filename fixtures exercise normalization only; they are never
    // passed to retoc or used as an actual game directory inventory.
    for (const TCHAR* Name : {TEXT("Carrot4-Switch.pak"), TEXT("Carrot4-Switch.utoc"), TEXT("Carrot4-Switch.ucas")})
        if (!FFileHelper::SaveStringToFile(
                TEXT("fixture"), *(Paks / Name), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
            return false;
    FString Found, Error;
    TestTrue(TEXT("romfs ancestor resolves its exact Paks directory"), P4KadanNormalizeGamePaks(Root, Found, Error));
    TestTrue(TEXT("resolved path is the actual Paks child"), FPaths::IsSamePath(Found, Paks));
    TestTrue(TEXT("direct Paks selection works"), P4KadanNormalizeGamePaks(Paks, Found, Error));
    TestTrue(TEXT("Carrot4 ancestor works"), P4KadanNormalizeGamePaks(Root / TEXT("Carrot4"), Found, Error));
    FP4KadanSourceCatalog Catalog;
    TestFalse(
        TEXT("missing retoc cannot populate a catalog"), P4KadanReadSourceCatalog(Root, FString(), Catalog, Error));
    TestTrue(
        TEXT("missing tool has a field-specific reason"), Error.Contains(TEXT("retoc")) && Catalog.Rows.Num() == 0);
    TestFalse(TEXT("a missing tool path cannot populate a catalog"),
        P4KadanReadSourceCatalog(Root, Root / TEXT("AbsentRetoc.exe"), Catalog, Error));
    IFileManager::Get().Delete(*(Paks / TEXT("Carrot4-Switch.ucas")));
    TestFalse(TEXT("an incomplete IoStore pair is rejected"), P4KadanNormalizeGamePaks(Root, Found, Error));
    TestTrue(TEXT("failed normalization clears previous path"), Found.IsEmpty());
    FFileHelper::SaveStringToFile(
        TEXT("fixture"), *(Paks / TEXT("Carrot4-Switch.ucas")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    for (const TCHAR* Name : {TEXT("Carrot4-Switch.pak"), TEXT("Carrot4-Switch.utoc"), TEXT("Carrot4-Switch.ucas")})
        FFileHelper::SaveStringToFile(
            TEXT("fixture"), *(Root / Name), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    TestFalse(TEXT("multiple complete game directories require an exact choice"),
        P4KadanNormalizeGamePaks(Root, Found, Error));
    TestTrue(TEXT("ambiguous directory error is explicit"), Error.Contains(TEXT("more than one")));
    TestTrue(TEXT("catalog source directory is the owned project cache"),
        P4KadanIsCatalogGameFolder(P4KadanCatalogGameFolder()));
    TestFalse(TEXT("an external game folder cannot become the owned catalog cache"), P4KadanIsCatalogGameFolder(Root));
    TestFalse(TEXT("a traversal outside the controlled cache cannot be accepted"),
        P4KadanIsCatalogGameFolder(P4KadanCatalogGameFolder() / TEXT("../Outside")));
    FP4KadanImportSettings Settings;
    Settings.GameFolder = Root;
    Settings.GamePaks = Paks;
    TestFalse(TEXT("advanced extracted sources are never prepared/deleted as controlled caches"),
        P4KadanPrepareCatalogImport(Settings, Error));
    Settings.GameFolder = P4KadanCatalogGameFolder();
    Settings.ConvertedCache = Root;
    TestFalse(TEXT("an external converted-cache override is rejected before any tool/cache operation"),
        P4KadanPrepareCatalogImport(Settings, Error));

    FP4KadanContainerIndex Index;
    TArray<FP4KadanContainerPackage> Packages;
    const FString WorldRow = TEXT(
        "Carrot4-Switch abc ExportBundleData ../../../Carrot4/Content/Carrot4/Maps/Cave000_F00_P.umap\n");
    const FString AssetRow = TEXT(
        "Carrot4-Switch def ExportBundleData ../../../Carrot4/Content/Carrot4/Mesh/SM_Test.uasset\n");
    TestTrue(TEXT("actual retoc directory row protocol enumerates without source payloads"),
        Index.Read(WorldRow + AssetRow + WorldRow, Error) && Index.Enumerate(Packages, Error));
    TestEqual(TEXT("duplicate observed paths create one browse row"), Packages.Num(), 2);
    int32 Maps = 0;
    for (const auto& Row : Packages)
    {
        if (Row.IsMap)
            ++Maps;
        TestTrue(TEXT("browse path is a canonical game identity"), Row.Package.StartsWith(TEXT("/Game/")));
    }
    TestEqual(TEXT("only observed umap is classified as a World"), Maps, 1);
    TestTrue(TEXT("case collisions retain explicit index ambiguity"),
        Index.Read(WorldRow +
                TEXT("Carrot4-Switch ghi ExportBundleData ../../../Carrot4/Content/carrot4/maps/cave000_f00_p.umap\n"),
            Error));
    TestFalse(TEXT("ambiguous identities cannot populate the catalog"), Index.Enumerate(Packages, Error));
    TestEqual(TEXT("ambiguous enumeration leaves no stale rows"), Packages.Num(), 0);
    TestFalse(
        TEXT("a bounded index rejects an overlong row"), Index.Read(WorldRow + FString::ChrN(8193, TCHAR('x')), Error));
    TestFalse(TEXT("a failed read cannot expose its earlier partial rows"), Index.Enumerate(Packages, Error));
    TestFalse(TEXT("an index traversal is rejected"),
        Index.Read(TEXT("Carrot4-Switch abc ExportBundleData ../../../Carrot4/Content/../Injected.umap\n"), Error));
    TestEqual(TEXT("browse payload byte size remains unknown rather than guessed"),
        FP4KadanSourceCatalogRow().SourceBytes, int64(-1));
    return !HasAnyErrors();
}
#endif

#if P4_MAPKIT_TESTS
#include "P4ExportSelection.h"
#include "P4ExportContainer.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ExportRootsTest, "P4MapKit.Export.SelectionRoots",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ExportRootsTest::RunTest(const FString&)
{
    FP4ModDescriptor Mod;
    Mod.Id = TEXT("selection.test");
    Mod.Name = TEXT("Selection Test");
    Mod.Version = TEXT("1.0.0");
    const FString Directory = FPaths::Combine(
        FPaths::ProjectSavedDir(), TEXT("P4MapKit/SelectionTest"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
    struct FCleanup
    {
        FString Directory;
        ~FCleanup() { IFileManager::Get().DeleteDirectory(*Directory, false, true); }
    } Cleanup{Directory};
    FString Error;
    TArray<FP4ExportRoot> Roots;
    TestFalse(TEXT("Empty selection is refused"), P4ValidateExportRoots(Roots, Error));
    TArray<FP4ExportRoot> NewSelection;
    TestTrue(TEXT("New mod reads an empty selection"), P4ReadExportRoots(Directory, Mod, NewSelection, Error));
    TestEqual(TEXT("New mod has no roots"), NewSelection.Num(), 0);
    FP4ExportRoot Root;
    Root.Kind = EP4ExportRootKind::Folder;
    Root.Path = TEXT("/Game/CreatorFolder");
    Roots.Add(Root);
    TestTrue(TEXT("Canonical folder selection accepted"), P4ValidateExportRoots(Roots, Error));
    TestTrue(TEXT("Selection persists separately"), P4WriteExportRoots(Directory, Mod, Roots, Error));
    TArray<FP4ExportRoot> Read;
    TestTrue(TEXT("Selection reopens"), P4ReadExportRoots(Directory, Mod, Read, Error));
    TestEqual(TEXT("Selection path retained"), Read[0].Path, Root.Path);
    TestTrue(TEXT("Selection kind retained"), Read[0].Kind == EP4ExportRootKind::Folder);
    TestFalse(TEXT("Selection does not create identity metadata"),
        IFileManager::Get().DirectoryExists(*FPaths::Combine(Directory, TEXT("Config/KadanMods"))));
    Roots.Add(Root);
    TestFalse(TEXT("Duplicate selection refused"), P4WriteExportRoots(Directory, Mod, Roots, Error));
    Roots.Pop();
    Root.Path = TEXT("/Game/../Outside");
    Roots[0] = Root;
    TestFalse(TEXT("Traversal refused"), P4ValidateExportRoots(Roots, Error));
    TestTrue(TEXT("Invalid update retained saved selection"), P4ReadExportRoots(Directory, Mod, Read, Error));
    TestEqual(TEXT("Prior selection unchanged"), Read[0].Path, FString(TEXT("/Game/CreatorFolder")));
    TestTrue(TEXT("Removing last root persists empty selection"), P4WriteExportRoots(Directory, Mod, {}, Error));
    TestTrue(TEXT("Empty selection reopens"), P4ReadExportRoots(Directory, Mod, NewSelection, Error));
    TestEqual(TEXT("Deleted root does not return"), NewSelection.Num(), 0);
    FP4ExportGameInventory Unverified;
    FP4ExportSelectionPreview Preview;
    Preview.Refused.Add(TEXT("sentinel"));
    TestFalse(TEXT("Missing real game inventory refuses preview"),
        P4PreviewExportSelection(Mod, Read, Unverified, {}, Preview, Error));
    TestEqual(TEXT("Refused preview preserves result"), Preview.Refused[0], FString(TEXT("sentinel")));
    FP4ExportContainerCensus Census;
    Census.PackageIds.Add(123);
    TestFalse(TEXT("Missing real game inventory refuses container inspection"),
        P4InspectExportContainer({}, TEXT("not-a-container"), Directory, Unverified, Census, Error));
    TestEqual(TEXT("Refused inspection preserves result"), Census.PackageIds[0], uint64(123));
    return true;
}
#endif

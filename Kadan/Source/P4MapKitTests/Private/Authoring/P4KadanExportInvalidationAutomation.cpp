#include "P4KadanExportInvalidation.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKadanExportInvalidation, "P4MapKit.Kadan.ExportInvalidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKadanExportInvalidation::RunTest(const FString&)
{
    const TArray<FP4ExportRoot> Roots = {
        {EP4ExportRootKind::Folder, TEXT("/Game/Mods/Example/Folder")},
        {EP4ExportRootKind::Package, TEXT("/Game/Elsewhere/Root")}
    };
    FP4ExportSelectionRow Dependency;
    Dependency.Package = TEXT("/Game/Shared/Required");
    const TArray<FP4ExportSelectionRow> Rows = {Dependency};
    TestTrue(TEXT("A selected package invalidates review"),
        P4KadanExportPackageIsSelected(TEXT("/Game/Elsewhere/Root"), Roots, Rows));
    TestTrue(TEXT("An asset inside a selected folder invalidates review"),
        P4KadanExportPackageIsSelected(TEXT("/Game/Mods/Example/Folder/Subfolder/Asset"), Roots, Rows));
    TestTrue(TEXT("A required dependency invalidates review"),
        P4KadanExportPackageIsSelected(Dependency.Package, Roots, Rows));
    TestTrue(TEXT("Unreal package identity is case insensitive"),
        P4KadanExportPackageIsSelected(TEXT("/game/shared/required"), Roots, Rows));
    TestFalse(TEXT("An unrelated package preserves review"),
        P4KadanExportPackageIsSelected(TEXT("/Game/Unrelated/Asset"), Roots, Rows));
    TestFalse(TEXT("A folder prefix requires a path boundary"),
        P4KadanExportPackageIsSelected(TEXT("/Game/Mods/Example/FolderExtra/Asset"), Roots, Rows));
    TestFalse(TEXT("A package root does not claim a subdirectory"),
        P4KadanExportPackageIsSelected(TEXT("/Game/Elsewhere/Root/Asset"), Roots, Rows));
    return !HasAnyErrors();
}
#endif

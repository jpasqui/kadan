#include "P4ExportNavigation.h"

#include "Engine/World.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4NavigationOpenClosedParity,
    "P4MapKit.Export.NavigationOpenClosedParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FP4NavigationOpenClosedParity::RunTest(const FString&)
{
    // This retained editor fixture omits some visual foliage types. They are
    // unrelated to the map's Recast data and emit load diagnostics on reopen.
    AddExpectedError(TEXT("Failed import for FoliageType_InstancedStaticMesh"),
        EAutomationExpectedErrorFlags::Contains, 0);
    const FString Package = TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave000/Cave000_F00/Cave000_F00_P");
    const FString GameFiles = FPaths::ProjectSavedDir() /
        TEXT("Kadan/ConvertedOriginals/Carrot4/Content");
    const FString Original = GameFiles / Package.Mid(6) + TEXT(".umap");
    if (!TestTrue(TEXT("The original game files are available"),
            IFileManager::Get().FileExists(*Original)))
    {
        return false;
    }
    if (!TestNotNull(TEXT("A different editor map opens"),
            UEditorLoadingAndSavingUtils::NewBlankMap(false)))
    {
        return false;
    }
    FP4MapNavigation Closed;
    FString Error;
    if (!TestTrue(TEXT("The closed game map produces navigation"),
            P4BuildMapNavigation(Package, GameFiles, Closed, Error)))
    {
        AddError(Error);
        return false;
    }
    if (!TestNotNull(TEXT("The game map opens in the editor"),
            UEditorLoadingAndSavingUtils::LoadMap(Package)))
    {
        return false;
    }
    FP4MapNavigation Open;
    Error.Reset();
    if (!TestTrue(TEXT("The open game map produces navigation"),
            P4BuildMapNavigation(Package, GameFiles, Open, Error)))
    {
        AddError(Error);
        return false;
    }
    TestTrue(TEXT("Both paths replace the game's navigation"),
        !Closed.Kept && !Open.Kept && Closed.Bytes.Num() > 0);
    TestTrue(TEXT("Open and closed map builds emit identical navigation bytes"),
        Open.Bytes == Closed.Bytes);
    AddInfo(FString::Printf(TEXT("Compared %d navigation bytes from both editor states."),
        Open.Bytes.Num()));
    return !HasAnyErrors();
}
#endif

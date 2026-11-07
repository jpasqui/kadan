#if P4_MAPKIT_TESTS
#include "P4ExportDriverTestShared.h"
#include "PackageTools.h"
#include "Engine/StaticMeshActor.h"

namespace
{
const TCHAR* const Effects = TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave000/Cave000_F00/Sublevels/Cave000_F00_Effects");
const TCHAR* const Mine = TEXT("/Game/KadanImportKeepsUnsavedLevel/Mine");

// Takes one small sublevel of the imported map out of the project, so the next import of the map makes it again
// and then opens the map to check it.
bool RemoveEffects(FAutomationTestBase& Test)
{
    if (UPackage* Loaded = FindPackage(nullptr, Effects))
        if (!UPackageTools::UnloadPackages({Loaded}))
            return Test.TestTrue(TEXT("The sublevel unloads"), false);
    FString File;
    return Test.TestTrue(TEXT("The sublevel's file is removed"),
        FPackageName::TryConvertLongPackageNameToFilename(Effects, File, FPackageName::GetMapPackageExtension()) &&
            IFileManager::Get().Delete(*File, false, true, true));
}

FString OpenMap()
{
    const UWorld* World = GEditor->GetEditorWorldContext().World();
    return World ? World->GetOutermost()->GetName() : FString();
}
} // namespace

// An import that makes a level opens its map to check it, which replaces the editor's world. A level with unsaved
// edits that is open keeps them (nobody can be asked in a test, so the check is skipped and said), and a saved
// level that is open is open again afterwards.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanImportKeepsUnsavedLevel, "P4MapKit.Kadan.ImportKeepsUnsavedLevel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4KadanImportKeepsUnsavedLevel::RunTest(const FString&)
{
    using namespace P4ExportDriverTest;
    const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    if (Root.IsEmpty() || !FPackageName::DoesPackageExist(Cave000))
    {
        AddWarning(TEXT("P4MapKit test not run. It needs a project with Cave000 imported (kadan_fixture.py copy)."));
        return true;
    }
    FP4ExportModRequest Request;
    FP4ModNames Names;
    if (!P4ExportDriverTest::Request(*this, TEXT("test.importkeeps"), TEXT("Import keeps"), Request, Names))
        return false;
    FP4KadanImportSettings Settings;
    Settings.GameFolder = Root / TEXT("host/Saved/P4MapKit/FullNativeCave000/game");
    Settings.GamePaks = Request.GamePaksDirectory;
    Settings.Retoc = Request.Tools.Retoc;
    Settings.Repak = Request.Tools.Repak;
    // The Import panel's settings.
    Settings.AllowGameBackedLevels = true;
    ON_SCOPE_EXIT
    {
        UEditorLoadingAndSavingUtils::NewBlankMap(false);
        FString File;
        if (FPackageName::TryConvertLongPackageNameToFilename(Mine, File, FPackageName::GetMapPackageExtension()))
            IFileManager::Get().Delete(*File, false, true, true);
    };
    UWorld* Level = UEditorLoadingAndSavingUtils::NewBlankMap(false);
    if (!TestTrue(TEXT("The person's level is made and saved"),
            Level && UEditorLoadingAndSavingUtils::SaveMap(Level, Mine) && OpenMap() == Mine))
        return false;
    // An edit the person has not saved yet.
    TWeakObjectPtr<AActor> Edit = Level->SpawnActor<AStaticMeshActor>(FVector(0, 0, 100), FRotator::ZeroRotator);
    if (Edit.IsValid())
        Edit->MarkPackageDirty();
    if (!TestTrue(TEXT("The level has an unsaved edit"), Edit.IsValid() && Edit->GetOutermost()->IsDirty()))
        return false;
    if (!RemoveEffects(*this))
        return false;
    const FP4KadanImportResult First = P4KadanImportMap(Settings, Cave000, true);
    for (const FString& Problem : First.Problems)
        AddInfo(TEXT("Not imported. ") + Problem);
    TestTrue(TEXT("The import makes the sublevel again"), First.Ok && FPackageName::DoesPackageExist(Effects));
    TestTrue(TEXT("The unsaved edit is still in the open level"), Edit.IsValid() && OpenMap() == Mine);
    TestTrue(TEXT("The import says it did not open the maps"),
        First.Warnings.ContainsByPredicate([](const FString& Line) { return Line.Contains(TEXT("unsaved edits")); }));
    // Saved, the level is open again after the import opened the map.
    if (!TestTrue(TEXT("The person saves the level"),
            UEditorLoadingAndSavingUtils::SaveMap(GEditor->GetEditorWorldContext().World(), Mine)) ||
        !RemoveEffects(*this))
        return false;
    const FP4KadanImportResult Second = P4KadanImportMap(Settings, Cave000, true);
    TestTrue(TEXT("The import makes the sublevel again"), Second.Ok && FPackageName::DoesPackageExist(Effects));
    TestEqual(TEXT("The person's level is open again after the import"), OpenMap(), FString(Mine));
    return !HasAnyErrors();
}
#endif

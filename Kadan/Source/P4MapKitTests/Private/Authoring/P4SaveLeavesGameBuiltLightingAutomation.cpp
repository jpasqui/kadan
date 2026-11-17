#if P4_MAPKIT_TESTS
#include "Misc/AutomationTest.h"
#include "Containers/Ticker.h"
#include "Engine/Level.h"
#include "Engine/MapBuildDataRegistry.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/OutputDevice.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "P4KadanReadOnly.h"
#include "P4LevelChanges.h"
#include "P4NativeConstructionPreview.h"

namespace
{
// The log lines that name a package, while a save runs.
class FNamedLines : public FOutputDevice
{
public:
    explicit FNamedLines(const FString& InName) : Name(InName) {}
    virtual void Serialize(const TCHAR* Text, ELogVerbosity::Type Verbosity, const FName&) override
    {
        if (Verbosity <= ELogVerbosity::Warning && FCString::Stristr(Text, *Name))
            Lines.Add(Text);
    }
    FString Name;
    TArray<FString> Lines;
};
} // namespace

// Saving a game level the way Save Current Level does writes the level and leaves the game's built lighting it refers to
// as imported while nothing changed it: Unreal's follow-up save of the level's built lighting has nothing to write. Once
// a lighting build in the editor changed it, the same save writes it, with no refusal. Runs in a project with an
// imported map (P4_SAVE_LIGHTING_MAP, Cave000 by default); without one there is nothing to check.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SaveLeavesGameBuiltLightingTest, "P4MapKit.Kadan.SaveLeavesGameBuiltLighting",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4SaveLeavesGameBuiltLightingTest::RunTest(const FString&)
{
    FString Map = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_SAVE_LIGHTING_MAP"));
    if (Map.IsEmpty())
        Map = TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave000/Cave000_F00/Cave000_F00_P");
    if (!FPackageName::DoesPackageExist(Map))
    {
        AddInfo(Map + TEXT(" is not imported in this project, so there is nothing to save."));
        return true;
    }
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(Map);
    if (!TestNotNull(TEXT("The map opens"), World))
        return false;
    ULevel* Level = nullptr;
    for (ULevel* Each : World->GetLevels())
        if (Each && Each->MapBuildData && Each->MapBuildData->GetOutermost() != Each->GetOutermost() &&
            Each->MapBuildData->GetOutermost()->GetName().EndsWith(TEXT("_BuiltData")))
        {
            Level = Each;
            break;
        }
    if (!TestNotNull(TEXT("A level of the map refers to the game's built lighting"), Level))
        return false;
    UMapBuildDataRegistry* const Registry = Level->MapBuildData;
    UPackage* const RegistryPackage = Registry->GetOutermost();
    FString RegistryFile;
    TestTrue(TEXT("The game's built lighting has a file"),
        FPackageName::DoesPackageExist(RegistryPackage->GetName(), nullptr, &RegistryFile));
    const FDateTime Before = IFileManager::Get().GetTimeStamp(*RegistryFile);
    P4Kadan::TakeReadOnlyMessages();
    UWorld* const LevelWorld = Level->GetTypedOuter<UWorld>();

    // The saves below write the level's file and, the second time, the game's built lighting file. Both go back as
    // they were when the test ends, so the project still holds the map as imported for the tests after this one.
    struct FKeptFile
    {
        FString File, Copy;
        bool ReadOnly = false;
    };
    TArray<FKeptFile> KeptFiles;
    FString LevelFile;
    FPackageName::DoesPackageExist(Level->GetOutermost()->GetName(), nullptr, &LevelFile);
    for (const FString& File : {LevelFile, RegistryFile})
    {
        if (File.IsEmpty())
            continue;
        FKeptFile One;
        One.File = File;
        One.Copy = FPaths::CreateTempFilename(*FPaths::ProjectIntermediateDir(), TEXT("KadanSaveTest"), TEXT(".kept"));
        One.ReadOnly = IFileManager::Get().IsReadOnly(*File);
        if (TestTrue(TEXT("The test keeps a copy of ") + File,
                IFileManager::Get().Copy(*One.Copy, *File, true, true, true) == COPY_OK))
            KeptFiles.Add(One);
    }
    ON_SCOPE_EXIT
    {
        IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
        for (const FKeptFile& One : KeptFiles)
        {
            Files.SetReadOnly(*One.File, false);
            IFileManager::Get().Copy(*One.File, *One.Copy, true, true, true);
            Files.SetReadOnly(*One.File, One.ReadOnly);
            Files.SetReadOnly(*One.Copy, false);
            IFileManager::Get().Delete(*One.Copy, false, true, true);
        }
    };

    // Kadan's comparison of the level cooks it with a save of its own. The level keeps the game's built lighting
    // through it, with no editor tick, as an export that cooks the level afterwards needs.
    {
        const FP4LevelChangesResult Changes = P4AnalyzeOriginalLevelChanges(LevelWorld);
        TestTrue(TEXT("The level's changes are read: ") + Changes.ErrorDetail, Changes.IsValid());
    }
    TestTrue(TEXT("The level refers to the game's built lighting right after Kadan cooks it"),
        Level->MapBuildData == Registry);

    FNamedLines Named(FPackageName::GetShortName(RegistryPackage->GetName()));
    GLog->AddOutputDevice(&Named);
    bool Saved = false;
    {
        // As a creator's Save Current Level, without the dialogs an automated run cannot answer.
        TGuardValue<bool> Script(GIsRunningUnattendedScript, true);
        Level->GetOutermost()->MarkPackageDirty();
        Saved = FEditorFileUtils::SaveLevel(Level);
    }
    GLog->RemoveOutputDevice(&Named);
    TestTrue(TEXT("The level saves"), Saved);
    TestEqual(TEXT("Nothing about the game's built lighting is logged: ") + FString::Join(Named.Lines, TEXT(" | ")),
        Named.Lines.Num(), 0);
    TestTrue(TEXT("The game's built lighting file is not written"),
        IFileManager::Get().GetTimeStamp(*RegistryFile) == Before);
    TestFalse(TEXT("The game's built lighting is not left unsaved"), RegistryPackage->IsDirty());
    // The level gave it up for the follow-up save only. A level that holds the registry as its own value has it again
    // for a cook straight after the save, with no editor tick in between. A level that shows it as one of Kadan's
    // previews (an import of an earlier version) has no value of its own for that cook, and has the preview again on
    // the editor's next tick.
    const bool Preview =
        P4IsNativePreviewValue(Level, FindFProperty<FProperty>(ULevel::StaticClass(), TEXT("MapBuildData")));
    AddInfo(Preview ? TEXT("The level shows the game's built lighting as a preview.")
                    : TEXT("The level holds the game's built lighting as its own value."));
    {
        const FP4LevelChangesResult Changes = P4AnalyzeOriginalLevelChanges(LevelWorld);
        TestTrue(TEXT("The level's changes are read after the save: ") + Changes.ErrorDetail, Changes.IsValid());
    }
    if (!Preview)
        TestTrue(TEXT("The level refers to the game's built lighting again when Kadan cooks it after the save"),
            Level->MapBuildData == Registry);
    FTicker::GetCoreTicker().Tick(0.f);
    TestTrue(TEXT("The level refers to the game's built lighting again after the save"), Level->MapBuildData == Registry);
    // And a comparison after that leaves it there, as an export that cooks the level needs.
    {
        const FP4LevelChangesResult Changes = P4AnalyzeOriginalLevelChanges(LevelWorld);
        TestTrue(TEXT("The level's changes are read again: ") + Changes.ErrorDetail, Changes.IsValid());
    }
    TestTrue(TEXT("The level still refers to the game's built lighting after Kadan cooks it again"),
        Level->MapBuildData == Registry);

    // A lighting build in the editor changes the game's registry, and the next save writes it.
    TestFalse(TEXT("The game's built lighting can be edited"), P4Kadan::IsLockedPackage(RegistryPackage));
    RegistryPackage->MarkPackageDirty();
    TestFalse(TEXT("Its file is writable once it changed"), IFileManager::Get().IsReadOnly(*RegistryFile));
    FNamedLines Again(Named.Name);
    GLog->AddOutputDevice(&Again);
    {
        TGuardValue<bool> Script(GIsRunningUnattendedScript, true);
        Level->GetOutermost()->MarkPackageDirty();
        Saved = FEditorFileUtils::SaveLevel(Level);
    }
    GLog->RemoveOutputDevice(&Again);
    TestTrue(TEXT("The level saves with its changed lighting"), Saved);
    TestEqual(TEXT("Nothing refuses the changed lighting: ") + FString::Join(Again.Lines, TEXT(" | ")), Again.Lines.Num(), 0);
    TestFalse(TEXT("The changed lighting is saved"), RegistryPackage->IsDirty());
    TestTrue(TEXT("Its file is written"), IFileManager::Get().GetTimeStamp(*RegistryFile) != Before);
    TestTrue(TEXT("The level still refers to it, with no editor tick"), Level->MapBuildData == Registry);
    return !HasAnyErrors();
}
#endif

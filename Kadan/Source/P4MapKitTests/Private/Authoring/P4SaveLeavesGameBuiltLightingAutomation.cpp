#if P4_MAPKIT_TESTS
#include "Misc/AutomationTest.h"
#include "Containers/Ticker.h"
#include "Engine/Level.h"
#include "Engine/MapBuildDataRegistry.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "Misc/OutputDevice.h"
#include "Misc/PackageName.h"
#include "P4KadanReadOnly.h"

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
    FTicker::GetCoreTicker().Tick(0.f);
    TestTrue(TEXT("The level refers to the game's built lighting again after the save"), Level->MapBuildData == Registry);

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
    FTicker::GetCoreTicker().Tick(0.f);
    TestTrue(TEXT("The level saves with its changed lighting"), Saved);
    TestEqual(TEXT("Nothing refuses the changed lighting: ") + FString::Join(Again.Lines, TEXT(" | ")), Again.Lines.Num(), 0);
    TestFalse(TEXT("The changed lighting is saved"), RegistryPackage->IsDirty());
    TestTrue(TEXT("Its file is written"), IFileManager::Get().GetTimeStamp(*RegistryFile) != Before);
    TestTrue(TEXT("The level still refers to it"), Level->MapBuildData == Registry);
    return !HasAnyErrors();
}
#endif

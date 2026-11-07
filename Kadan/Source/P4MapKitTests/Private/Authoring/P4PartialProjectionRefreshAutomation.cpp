#if P4_MAPKIT_TESTS
#include "P4KadanImport.h"
#include "P4LevelChanges.h"
#include "P4OriginalLevelImport.h"
#include "P4PackageSave.h"
#include "P4LevelContent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "NavModifierVolume.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PartialProjectionRefreshTest, "P4MapKit.Kadan.RefreshOriginalProjection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PartialProjectionRefreshTest::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_PROJECTION_REFRESH")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. Select the owned original Area001 projection."));
        return true;
    }
    const FString Package = TEXT("/Game/Carrot4/Maps/Main/Area/Area001/Area001_P");
    FP4KadanImportSettings Settings;
    Settings.GameFolder = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REIMPORT_GAME"));
    Settings.GamePaks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    Settings.AllowGameBackedLevels = true;
    FString Private = FPaths::GetPath(Settings.GamePaks);
    Settings.Retoc = Private / TEXT("retoc/retoc.exe");
    Settings.Repak = Private / TEXT("repak/repak.exe");
    TArray<TSharedPtr<FP4KadanSourceAsset>> Sources;
    TArray<FString> Problems;
    if (!TestTrue(TEXT("Retained authoritative dependency source registry is readable"),
            P4KadanScanGame(Settings.GameFolder, Sources, Problems)))
        return false;
    // Only the root is rebuilt. Its dependencies already exist in this owned
    // project and are independently checked/lent by normal level reconstruction.
    auto Root = MakeShared<FP4KadanSourceAsset>();
    Root->Package = Package;
    Root->Kind = TEXT("World");
    Root->Header = FPaths::ConvertRelativePathToFull(
        FPaths::ProjectSavedDir() / TEXT("Kadan/ConvertedOriginals/Carrot4/Content") / Package.Mid(6) + TEXT(".umap"));
    Root->Body = FPaths::ChangeExtension(Root->Header, TEXT("uexp"));
    FP4KadanImportPlan Plan;
    Plan.Assets.Add(Root);
    Plan.Scanned = Sources;
    Plan.Scanned.Add(Root);
    Plan.RequestedPackages.Add(Package);
    Plan.Roots = 1;
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_PROJECTION_REPAIR_RULE")) == TEXT("1"))
    {
        const FString Rule = TEXT("/Game/Carrot4/Placeables/GameRule/BP_CarrotGameRuleStory");
        const FString Table = TEXT("/Game/Carrot4/Core/UI/DT_UIWidgetStory");
        auto Repair = P4KadanPlanImport(Settings, TSet<FString>{Rule, Table}, false);
        // Keep the real plan's complete source registry. The rest of this owned
        // fixture already holds the dependencies; explicitly repair these two assets.
        Repair.Assets.RemoveAll([&](const TSharedPtr<FP4KadanSourceAsset>& Asset)
            { return Asset->Package != Rule && Asset->Package != Table; });
        const auto Repaired = P4KadanRunImport(Settings, Repair, false);
        const FString Folder = FPaths::ProjectSavedDir() / TEXT("P4MapKit");
        IFileManager::Get().MakeDirectory(*Folder, true);
        FFileHelper::SaveStringToFile(Repaired.Report, *(Folder / TEXT("Area001-rule-reference-repair.json")),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        if (!TestTrue(TEXT("The original rule and UI table repair through their normal importers"),
                Repaired.Imported.Contains(Rule) && Repaired.Imported.Contains(Table)))
            return false;
    }
    UWorld* Previous = LoadObject<UWorld>(nullptr, *(Package + TEXT(".Area001_P")));
    AActor* CreatorActor = nullptr;
    if (Previous)
        for (AActor* Actor : Previous->PersistentLevel->Actors)
            if (IsValid(Actor) && Actor->GetRootComponent() && Actor->GetName().StartsWith(TEXT("NavLinkProxy")))
            {
                CreatorActor = Actor;
                break;
            }
    if (!TestNotNull(TEXT("A real original actor is available for the creator-preservation control"), CreatorActor))
        return false;
    CreatorActor->GetRootComponent()->UpdateComponentToWorld();
    const FName EditedName = CreatorActor->GetFName();
    const FVector OriginalLocation = CreatorActor->GetActorLocation(), Desired = OriginalLocation + FVector(1, 0, 0);
    ON_SCOPE_EXIT
    {
        UWorld* Restored = LoadObject<UWorld>(nullptr, *(Package + TEXT(".Area001_P")));
        AActor* Actor = Restored ? FindObject<AActor>(Restored->PersistentLevel, *EditedName.ToString()) : nullptr;
        if (Actor)
        {
            Actor->SetActorLocation(OriginalLocation);
            Restored->MarkPackageDirty();
            FString Error;
            TestTrue(TEXT("The actual creator-move control returns the owned root to its imported state"),
                P4SaveAssetPackage(
                    Restored,
                    [&]()
                    {
                        const auto C = P4AnalyzeOriginalLevelChanges(Restored);
                        return C.IsValid() && C.Unchanged;
                    },
                    Error));
        }
    };
    CreatorActor->Modify();
    CreatorActor->SetActorLocation(Desired);
    Previous->MarkPackageDirty();
    FString SaveError;
    if (!TestTrue(TEXT("The creator's real one-centimetre original-actor move is saved before reimport"),
            P4SaveAssetPackage(
                Previous,
                [&]()
                {
                    const auto C = P4AnalyzeOriginalLevelChanges(Previous);
                    return C.IsValid() && !C.Unchanged;
                },
                SaveError)))
        return false;
    const double Start = FPlatformTime::Seconds();
    const auto Result = P4KadanRunImport(Settings, Plan, false);
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("P4MapKit");
    IFileManager::Get().MakeDirectory(*Folder, true);
    FFileHelper::SaveStringToFile(Result.Report, *(Folder / TEXT("Area001-projection-refresh.json")),
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    UWorld* World = LoadObject<UWorld>(nullptr, *(Package + TEXT(".Area001_P")));
    if (!TestNotNull(TEXT("The original root remains available after explicit reimport"), World))
        return false;
    int32 Count = 0;
    for (AActor* Actor : World->PersistentLevel->Actors)
        if (IsValid(Actor) && Actor->IsA<ANavModifierVolume>())
            ++Count;
    AddInfo(FString::Printf(
        TEXT("Original Area001 root refresh %.1fs: %d editable navigation modifier volumes; accepted=%d"),
        FPlatformTime::Seconds() - Start, Count, Result.Imported.Contains(Package)));
    TestTrue(TEXT("Normal best-effort reimport accepts the improved original root"), Result.Imported.Contains(Package));
    TestEqual(TEXT("All 176 source navigation volumes are present in the actual published root"), Count, 176);
    const auto Changes = P4AnalyzeOriginalLevelChanges(World);
    AActor* Replayed = FindObject<AActor>(World->PersistentLevel, *EditedName.ToString());
    TestTrue(TEXT("The improved source projection preserves the creator's saved movement exactly"),
        Replayed && Replayed->GetActorLocation() == Desired);
    TestTrue(TEXT("Only the known creator movement remains after source improvement"),
        Changes.IsValid() && Changes.Moved.Num() == 1 && Changes.Removed.Num() == 0 && Changes.Added.Num() == 0 &&
            Changes.OtherChanged.Num() == 0);
    return !HasAnyErrors();
}
#endif

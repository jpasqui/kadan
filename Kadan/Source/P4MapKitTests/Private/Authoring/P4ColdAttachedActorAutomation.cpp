#if P4_MAPKIT_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "FileHelpers.h"
#include "P4LevelChanges.h"
#include "P4PackageSave.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ColdAttachedActorTest, "P4MapKit.Editor.ColdOriginalAttachmentCoordinates",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ColdAttachedActorTest::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_COLD_ATTACHMENT")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. Select the owned complete Cave014 source fixture."));
        return true;
    }
    const FString Layer = TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave014/Cave014_F03/Sublevels/Cave014_F03_Teki");
    UWorld* Saved = LoadObject<UWorld>(nullptr, *(Layer + TEXT(".Cave014_F03_Teki")));
    USceneComponent* Before = Saved ? FindObject<USceneComponent>(Saved->PersistentLevel, TEXT("GAmeBozu_2.Sphere"))
                                    : nullptr;
    if (!TestNotNull(TEXT("The actual saved source attachment is available before editor construction"), Before))
        return false;
    // Exact original fixture coordinates independently recorded by native source
    // comparison; the editor can already have reconstructed an asset during load.
    const FVector Original(-1015.002197265625f, -183.87579345703125f, 59.999923706054688f);
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ATTACHMENT_REPAIR_FIXTURE")) == TEXT("1"))
    {
        FString Project = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
        FPaths::NormalizeDirectoryName(Project);
        if (!TestTrue(TEXT("Fixture repair stays inside the owned round-two project"),
                Project.EndsWith(TEXT("/private/kadan-import-fix-1024/project"))))
            return false;
        const auto Delta = P4AnalyzeOriginalLevelChanges(Saved);
        const FString Prefix = TEXT("Cave014_F03_Teki.PersistentLevel.GAmeBozu_2.Sphere | relativelocation = ");
        const bool OnlyRounding = Delta.IsValid() && Delta.Removed.Num() == 0 && Delta.Added.Num() == 0 &&
            Delta.Moved.Num() == 0 && Delta.LevelChanges.Before.Num() == 0 && Delta.LevelChanges.After.Num() == 0 &&
            Delta.OtherChanged.Num() == 1 && Delta.OtherChanged[0].Lines.Before.Num() == 1 &&
            Delta.OtherChanged[0].Lines.After.Num() == 1 && Delta.OtherChanged[0].Lines.Before[0].StartsWith(Prefix) &&
            Delta.OtherChanged[0].Lines.After[0].StartsWith(Prefix);
        if (!TestTrue(TEXT("The owned fixture has no changes except the recorded engine attachment artifact"),
                Delta.Unchanged || OnlyRounding))
            return false;
        Before->SetRelativeLocation_Direct(Original);
        Before->UpdateComponentToWorld();
        FString Error;
        if (!TestTrue(
                TEXT("The exact original fixture is restored and saved without changing any other source content: ") +
                    Error,
                P4SaveAssetPackage(
                    Saved,
                    [&]()
                    {
                        const auto Check = P4AnalyzeOriginalLevelChanges(Saved);
                        return Check.IsValid() && Check.Unchanged;
                    },
                    Error)))
            return false;
    }
    AddInfo(FString::Printf(TEXT("Source sphere creation=%d root=%s parent=%s"), int32(Before->CreationMethod),
        *GetPathNameSafe(Before->GetOwner()->GetRootComponent()), *GetPathNameSafe(Before->GetAttachParent())));
    UWorld* Opened = UEditorLoadingAndSavingUtils::LoadMap(
        TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave014/Cave014_F03/Cave014_F03_P"));
    if (!TestNotNull(TEXT("The actual original persistent map opens in the editor"), Opened))
        return false;
    ON_SCOPE_EXIT
    {
        UEditorLoadingAndSavingUtils::NewBlankMap(false);
    };
    UWorld* World = LoadObject<UWorld>(nullptr, *(Layer + TEXT(".Cave014_F03_Teki")));
    USceneComponent* Sphere = World ? FindObject<USceneComponent>(World->PersistentLevel, TEXT("GAmeBozu_2.Sphere"))
                                    : nullptr;
    if (!TestNotNull(TEXT("The source sphere remains in the opened streaming layer"), Sphere))
        return false;
    AddInfo(FString::Printf(TEXT("Attachment test before %.12f %.12f after %.12f %.12f"), Original.X, Original.Y,
        Sphere->GetRelativeLocation().X, Sphere->GetRelativeLocation().Y));
    TestTrue(TEXT("Opening preserves the exact saved authored attachment coordinates"),
        Sphere->GetRelativeLocation() == Original);
    const auto Changes = P4AnalyzeOriginalLevelChanges(World);
    TestTrue(TEXT("Ordinary editor construction retains the complete original layer"),
        Changes.IsValid() && Changes.Unchanged);
    const FVector Current = Sphere->GetRelativeLocation();
    const bool Dirty = World->GetOutermost()->IsDirty();
    {
        ON_SCOPE_EXIT
        {
            Sphere->SetRelativeLocation_Direct(Current);
            Sphere->UpdateComponentToWorld();
            World->GetOutermost()->SetDirtyFlag(Dirty);
        };
        Sphere->Modify();
        Sphere->SetRelativeLocation(Current + FVector(0.01f, 0, 0));
        World->MarkPackageDirty();
        const auto Edited = P4AnalyzeOriginalLevelChanges(World);
        TestTrue(TEXT("A genuine creator change to the same attachment remains an edit"),
            Edited.IsValid() && !Edited.Unchanged);
    }
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ATTACHMENT_SAVED_CREATOR")) == TEXT("1"))
    {
        // Resolve again during cleanup: normal map loading replaces world objects.
        ON_SCOPE_EXIT
        {
            UWorld* Restore = LoadObject<UWorld>(nullptr, *(Layer + TEXT(".Cave014_F03_Teki")));
            auto* Part = Restore ? FindObject<USceneComponent>(Restore->PersistentLevel, TEXT("GAmeBozu_2.Sphere"))
                                 : nullptr;
            if (Part)
            {
                Part->SetRelativeLocation_Direct(Original);
                Part->UpdateComponentToWorld();
                Restore->MarkPackageDirty();
                FString Error;
                TestTrue(TEXT("The saved creator control restores the owned original fixture"),
                    P4SaveAssetPackage(
                        Restore,
                        [&]()
                        {
                            const auto C = P4AnalyzeOriginalLevelChanges(Restore);
                            return C.IsValid() && C.Unchanged;
                        },
                        Error));
            }
        };
        const FVector Desired = Original + FVector(0.01f, 0, 0);
        Sphere->Modify();
        Sphere->SetRelativeLocation_Direct(Desired);
        Sphere->UpdateComponentToWorld();
        World->MarkPackageDirty();
        FString Error;
        if (!TestTrue(TEXT("A real creator attachment override saves exactly"),
                P4SaveAssetPackage(World, [&]() { return Sphere->GetRelativeLocation() == Desired; }, Error)))
            return false;
        UEditorLoadingAndSavingUtils::NewBlankMap(false);
        if (!TestNotNull(TEXT("The map reopens with a saved creator override"),
                UEditorLoadingAndSavingUtils::LoadMap(
                    TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave014/Cave014_F03/Cave014_F03_P"))))
            return false;
        UWorld* Reopened = LoadObject<UWorld>(nullptr, *(Layer + TEXT(".Cave014_F03_Teki")));
        auto* Part = Reopened ? FindObject<USceneComponent>(Reopened->PersistentLevel, TEXT("GAmeBozu_2.Sphere"))
                              : nullptr;
        TestTrue(TEXT("Construction preserves the saved creator value rather than the game's original baseline"),
            Part && Part->GetRelativeLocation() == Desired);
        const auto SavedEdit = P4AnalyzeOriginalLevelChanges(Reopened);
        TestTrue(
            TEXT("The saved creator override remains visible as an edit"), SavedEdit.IsValid() && !SavedEdit.Unchanged);
    }
    return !HasAnyErrors();
}
#endif

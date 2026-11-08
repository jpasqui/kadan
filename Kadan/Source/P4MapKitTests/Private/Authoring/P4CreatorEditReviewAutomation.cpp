#if P4_MAPKIT_TESTS
#include "P4ExportContainer.h"
#include "P4ExportDriver.h"
#include "P4ExportSelection.h"
#include "P4GameLighting.h"
#include "P4KadanCreatorReview.h"
#include "P4LevelChanges.h"
#include "P4ModDescriptor.h"
#include "AssetRegistryModule.h"
#include "Misc/Paths.h"
#include "Components/LightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "Engine/PointLight.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureStreamingTypes.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"

namespace P4ExportDriverTest
{
bool Request(
    FAutomationTestBase& Test, const FString& Id, const FString& Name, FP4ExportModRequest& Out, FP4ModNames& Names);
} //namespace P4ExportDriverTest

namespace P4CreatorEditReviewTest
{
const TCHAR* const Cave000 = TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave000/Cave000_F00/Cave000_F00_P");

// Cave000_F00 open in the editor, with its game area, the game's inventory and a mod request. False with a
// warning when the project has no imported Cave000_F00.
struct FOpenCave
{
    UWorld* Map = nullptr;
    UWorld* Area = nullptr;
    FString AreaPackage;
    FP4ExportModRequest Request;
    FP4ModNames Names;
    FP4ExportGameInventory Inventory;
    FP4LevelChangesResult Before;

    bool Open(FAutomationTestBase& Test, const TCHAR* Id)
    {
        if (!FPackageName::DoesPackageExist(Cave000))
        {
            Test.AddWarning(TEXT("P4MapKit test not run. Run it in a project with an imported Cave000_F00, such as a "
                                 "copy of the cave000 fixture."));
            return false;
        }
        if (!P4ExportDriverTest::Request(Test, Id, TEXT("Creator Edit Review Test"), Request, Names))
            return false;
        // A creator's project reviews as its own mod, whose folder holds what its edits refer to.
        TArray<FP4ModDescriptor> Mods;
        FString Why;
        if (P4ListModDescriptors(FPaths::ProjectDir(), Mods, Why))
            for (const FP4ModDescriptor& Mod : Mods)
            {
                FP4ModNames Own;
                if (P4ModDerivedNames(Mod, Own, Why) &&
                    FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"))
                        .Get()
                        .HasAssets(FName(*Own.PackageRoot), true))
                {
                    Request.Mod = Mod;
                    Names = Own;
                    Test.AddInfo(TEXT("Reviewed as the project's own mod ") + Mod.Id + TEXT("."));
                    break;
                }
            }
        Map = UEditorLoadingAndSavingUtils::LoadMap(Cave000);
        if (!Test.TestNotNull(TEXT("The game map opens"), Map))
            return false;
        for (ULevelStreaming* Streaming : Map->GetStreamingLevels())
            if (Streaming && Streaming->GetWorldAssetPackageName().EndsWith(TEXT("/Cave000_F00_GameArea")) &&
                Streaming->GetLoadedLevel())
            {
                Area = CastChecked<UWorld>(Streaming->GetLoadedLevel()->GetOuter());
                AreaPackage = Streaming->GetWorldAssetPackageName();
            }
        if (!Test.TestNotNull(TEXT("The game area is loaded with the map"), Area))
            return false;
        // A creator's project may have changed the area already: the tests compare with what it has.
        Before = P4AnalyzeOriginalLevelChanges(Area);
        if (!Test.TestTrue(TEXT("The game area's changes can be read"), Before.IsValid()))
            return false;
        FString Error;
        if (!Test.TestTrue(TEXT("The game inventory is read"),
                P4ReadExportGameInventory(
                    Request.Tools, Request.GamePaksDirectory, Request.WorkDirectory, Inventory, Error)))
        {
            Test.AddError(Error);
            return false;
        }
        return true;
    }

    // The review of the game area alone, with the map open.
    FP4KadanCreatorReview Review(FAutomationTestBase& Test)
    {
        FP4ExportRoot Root;
        Root.Kind = EP4ExportRootKind::Package;
        Root.Path = AreaPackage;
        FP4ExportSelectionPreview Preview;
        const TMap<FName, FString> NoClaims;
        FString Error;
        if (!Test.TestTrue(TEXT("The game area has a shipping preview"),
                P4PreviewExportSelection(Request.Mod, {Root}, Inventory, NoClaims, Preview, Error)))
            Test.AddError(Error);
        return P4ReviewCreatorSelection(
            Request.Mod, Preview, Map, &Inventory, nullptr, Request.ExtractedGameDirectory);
    }
};

// The actors a level's changes name, whatever the change.
TSet<FString> ChangedActors(const FP4LevelChangesResult& Changes)
{
    TSet<FString> Out(Changes.Added);
    Out.Append(Changes.Removed);
    for (const FP4LevelActorMove& Move : Changes.Moved)
        Out.Add(Move.Actor);
    for (const FP4LevelActorChanges& Changed : Changes.OtherChanged)
        Out.Add(Changed.Actor);
    return Out;
}

// Undoes the test's one transaction and checks that the game area has the changes it had before the test.
void UndoTo(FAutomationTestBase& Test, UWorld* Area, const FP4LevelChangesResult& Before)
{
    GEditor->UndoTransaction();
    const FP4LevelChangesResult After = P4AnalyzeOriginalLevelChanges(Area);
    Test.TestTrue(TEXT("The test leaves the game area's changes as they were"),
        After.IsValid() && ChangedActors(After).Num() == ChangedActors(Before).Num() &&
            ChangedActors(After).Includes(ChangedActors(Before)));
    Area->GetOutermost()->SetDirtyFlag(false);
}
} //namespace P4CreatorEditReviewTest

// A creator adds a Static point light to Cave000_F00's game area and takes out an actor the game's build lit, so the
// export rebuilds the game's lighting. Review names the added light and says to make it Movable, before any build,
// and the lighting step says the same rather than asking to import the level again.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4AddedStaticLightReviewTest, "P4MapKit.Export.AddedStaticLightReview",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4AddedStaticLightReviewTest::RunTest(const FString&)
{
    using namespace P4CreatorEditReviewTest;
    FOpenCave Cave;
    if (!Cave.Open(*this, TEXT("native.addedlight.test")))
        return !HasAnyErrors();
    ON_SCOPE_EXIT
    {
        if (GEditor)
            UEditorLoadingAndSavingUtils::NewBlankMap(false);
    };
    ULevel* Area = Cave.Area->PersistentLevel;
    const TCHAR* const LitGameActor = TEXT("CMA_T02_DC_Byoutake01_1");
    GEditor->BeginTransaction(FText::FromString(TEXT("Kadan added light test")));
    FActorSpawnParameters Spawn;
    Spawn.OverrideLevel = Area;
    APointLight* Lamp = Cave.Area->SpawnActor<APointLight>(FVector(590, -837, 150), FRotator::ZeroRotator, Spawn);
    if (Lamp)
    {
        Lamp->SetActorLabel(TEXT("P4AddedLamp"));
        Lamp->GetLightComponent()->SetMobility(EComponentMobility::Static);
    }
    AActor* Removed = FindObject<AActor>(Area, LitGameActor);
    if (Removed)
        Removed->GetWorld()->EditorDestroyActor(Removed, true);
    GEditor->EndTransaction();
    ON_SCOPE_EXIT
    {
        UndoTo(*this, Cave.Area, Cave.Before);
    };
    if (!TestNotNull(TEXT("The light is added"), Lamp))
        return false;
    AddInfo(Removed ? TEXT("A lit game actor was taken out for the test.")
                    : TEXT("The lit game actor was gone already; the area's own changes rebuild its lighting."));

    const FString Issue = P4AddedBakedLightIssue(Cave.Area);
    AddInfo(TEXT("added light: ") + Issue);
    TestTrue(TEXT("The added light is named with the fix"),
        Issue.Contains(TEXT("P4AddedLamp")) && Issue.Contains(TEXT("Static")) && Issue.Contains(TEXT("Movable")));

    const FP4KadanCreatorReview Review = Cave.Review(*this);
    for (const FString& Blocker : Review.Blockers)
        AddInfo(TEXT("blocker: ") + Blocker);
    TestTrue(TEXT("Review shows the added light before export"),
        Review.Blockers.ContainsByPredicate(
            [](const FString& Blocker) { return Blocker.Contains(TEXT("P4AddedLamp")) && Blocker.Contains(TEXT("Movable")); }));

    // The lighting step refuses before it builds anything, with the same sentence.
    TArray<FP4AddedLitLevel> NoAdded;
    FP4RebuiltGameLighting Lighting;
    FString Error;
    TestFalse(TEXT("The lighting step refuses the added Static light"),
        P4BuildEditedMapLighting(Cave.Map, Cave.Inventory, true, NoAdded, Lighting, Error));
    AddInfo(TEXT("lighting step: ") + Error);
    TestTrue(TEXT("The lighting step names the added light, not a reimport"),
        Error.Contains(TEXT("P4AddedLamp")) && !Error.Contains(TEXT("Import its level again")));

    // Movable, as the game lights it, the light stops nothing.
    Lamp->GetLightComponent()->SetMobility(EComponentMobility::Movable);
    TestTrue(TEXT("A Movable added light is no issue"), P4AddedBakedLightIssue(Cave.Area).IsEmpty());
    return !HasAnyErrors();
}

// After a lighting or texture streaming build the editor has new texture streaming data on the game's mesh
// components. That is not a change of the creator's: the level stays as imported, and review ships it as the
// game's.
// A level of the open map with a change of its own that the mod leaves out is found.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4BuildDataNotAChangeTest, "P4MapKit.Export.BuildDataNotAChange",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4BuildDataNotAChangeTest::RunTest(const FString&)
{
    using namespace P4CreatorEditReviewTest;
    FOpenCave Cave;
    if (!Cave.Open(*this, TEXT("native.builddata.test")))
        return !HasAnyErrors();
    ON_SCOPE_EXIT
    {
        if (GEditor)
            UEditorLoadingAndSavingUtils::NewBlankMap(false);
    };
    // Every game static mesh component of the area gets texture streaming data other than its own, as a build
    // that worked it out again would leave.
    TArray<TPair<UStaticMeshComponent*, TArray<FStreamingTextureBuildInfo>>> Kept;
    for (AActor* Actor : Cave.Area->PersistentLevel->Actors)
    {
        TInlineComponentArray<UStaticMeshComponent*> Parts(Actor);
        for (UStaticMeshComponent* Part : Parts)
        {
            Kept.Emplace(Part, Part->StreamingTextureData);
            FStreamingTextureBuildInfo Built;
            Built.PackedRelativeBox = 0xFFFF0000;
            Built.TextureLevelIndex = 0;
            Built.TexelFactor = 123.f;
            Part->StreamingTextureData.Add(Built);
        }
    }
    ON_SCOPE_EXIT
    {
        for (auto& Each : Kept)
            Each.Key->StreamingTextureData = Each.Value;
        Cave.Area->GetOutermost()->SetDirtyFlag(false);
    };
    AddInfo(FString::Printf(TEXT("%d mesh components were given new texture streaming data."), Kept.Num()));
    if (!TestTrue(TEXT("The area has mesh components"), Kept.Num() > 0))
        return false;
    const FP4LevelChangesResult After = P4AnalyzeOriginalLevelChanges(Cave.Area);
    int32 Streaming = 0;
    for (const FP4LevelActorChanges& Changed : After.OtherChanged)
        for (const FString& Line : Changed.Lines.After)
            if (Line.Contains(TEXT("streamingtexturedata")))
            {
                if (++Streaming <= 3)
                    AddInfo(TEXT("read as a change: ") + Changed.Actor + TEXT(" ") + Line.Left(200));
            }
    AddInfo(FString::Printf(TEXT("%d texture streaming lines read as changes; %d actors changed before, %d after."),
        Streaming, ChangedActors(Cave.Before).Num(), ChangedActors(After).Num()));
    TestTrue(TEXT("New texture streaming data is not a change of the level"),
        After.IsValid() && Streaming == 0 && ChangedActors(After).Num() == ChangedActors(Cave.Before).Num());

    // A real change, outside the mod's selection, is found.
    AActor* Moved = nullptr;
    for (AActor* Actor : Cave.Area->PersistentLevel->Actors)
        if (Actor && Actor->IsA<AStaticMeshActor>())
        {
            Moved = Actor;
            break;
        }
    if (!TestNotNull(TEXT("The area has a static mesh actor to move"), Moved))
        return false;
    GEditor->BeginTransaction(FText::FromString(TEXT("Kadan changed level test")));
    Moved->Modify();
    Moved->SetActorLocation(Moved->GetActorLocation() + FVector(0, 0, 10));
    GEditor->EndTransaction();
    ON_SCOPE_EXIT
    {
        UndoTo(*this, Cave.Area, Cave.Before);
    };
    const FP4KadanCreatorReview Nothing;
    const TArray<FString> Left = P4ChangedLevelsOutsideSelection(Cave.Map, Cave.Inventory, Nothing);
    AddInfo(TEXT("left out: ") + FString::Join(Left, TEXT(", ")));
    TestTrue(TEXT("The changed game area is found outside the selection"), Left.Contains(Cave.AreaPackage));
    const FP4KadanCreatorReview Selected = Cave.Review(*this);
    TestFalse(TEXT("A selected changed level is not reported"),
        P4ChangedLevelsOutsideSelection(Cave.Map, Cave.Inventory, Selected).Contains(Cave.AreaPackage));
    return !HasAnyErrors();
}
#endif

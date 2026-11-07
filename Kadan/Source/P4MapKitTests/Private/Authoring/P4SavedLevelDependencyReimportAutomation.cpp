#if P4_MAPKIT_TESTS
#include "P4KadanImport.h"
#include "P4KadanTestSeams.h"
#include "P4BlenderExport.h"
#include "Serialization/JsonSerializer.h"
#include "P4LevelChanges.h"
#include "P4OriginalRecord.h"
#include "AssetRegistryModule.h"
#include "Misc/AutomationTest.h"
#include "HAL/PlatformMisc.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "EngineUtils.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "UObject/Package.h"
#include "Components/ArrowComponent.h"
#include "Components/StaticMeshComponent.h"

// Focused component integration: the production early scope, actual BP-only native service and
// native level reimport. Warm dependencies remain the qualified fixture's. This isolates the real
// Blueprint reinstancer and creator replay; combined normal RunImport is validated separately.
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FP4SavedLevelDependencyReimport, "P4MapKit.Kadan.SavedLevelDependencyReimport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
void FP4SavedLevelDependencyReimport::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
    for (const TCHAR* Variant : {TEXT("unchanged"), TEXT("moved"), TEXT("changed"), TEXT("added")})
    {
        Names.Add(Variant);
        Commands.Add(Variant);
    }
}
bool FP4SavedLevelDependencyReimport::RunTest(const FString& Variant)
{
    const FString Game = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REIMPORT_GAME"));
    const FString Map = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REIMPORT_MAP"));
    if (Game.IsEmpty() || !Map.EndsWith(TEXT("/Cave003_F00_P")))
    {
        AddWarning(TEXT(
            "P4MapKit test not run. Set P4_NATIVE_REIMPORT_GAME and P4_NATIVE_REIMPORT_MAP to the owned warm Cave003 fixture."));
        return true;
    }
    auto& Features = IModularFeatures::Get();
    if (!Features.IsModularFeatureAvailable(FP4KadanSavedLevelTestSeams::FeatureName()))
    {
        AddWarning(TEXT("P4MapKit test not run. This Kadan build has no saved-level component test seam."));
        return true;
    }
    const auto& Seams = Features.GetModularFeature<FP4KadanSavedLevelTestSeams>(
        FP4KadanSavedLevelTestSeams::FeatureName());
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(Map);
    if (!TestNotNull(TEXT("Actual saved map opens"), World))
        return false;
    const auto Before = P4AnalyzeOriginalLevelChanges(World);
    if (!TestTrue(TEXT("Fixture begins semantically unchanged"), Before.IsValid() && Before.Unchanged))
        return false;
    const FString ActorName = TEXT("ACarrotGameRuleStoryCaveOtakaraCollect2");
    AActor* Rule = FindObject<AActor>(World->PersistentLevel, *ActorName);
    UArrowComponent* Arrow = Rule ? Cast<UArrowComponent>(Rule->GetRootComponent()) : nullptr;
    if (!TestNotNull(TEXT("Actual source Arrow is the rule's root before dependency updates"), Arrow))
        return false;
    const FTransform OriginalTransform = Rule->GetActorTransform();
    const FColor OriginalColor = Arrow->ArrowColor, ChangedColor(7, 11, 13);
    const FVector Moved = OriginalTransform.GetLocation() + FVector(35, 0, 0);
    const FName AddedName(TEXT("KadanReimportCreatorProbe"));
    UStaticMesh* AddedMesh = nullptr;
    // Restore only this test's authored edits, using a fresh load after any package replacement.
    ON_SCOPE_EXIT
    {
        if (UWorld* Restored = UEditorLoadingAndSavingUtils::LoadMap(Map))
        {
            AActor* Actor = FindObject<AActor>(Restored->PersistentLevel, *ActorName);
            UArrowComponent* Root = Actor ? Cast<UArrowComponent>(Actor->GetRootComponent()) : nullptr;
            if (Root)
            {
                Actor->SetActorTransform(OriginalTransform);
                Root->ArrowColor = OriginalColor;
                if (AActor* Added = FindObject<AActor>(Restored->PersistentLevel, *AddedName.ToString()))
                    Restored->DestroyActor(Added, false, false);
                Restored->MarkPackageDirty();
                TestTrue(TEXT("Test cleanup saves only its restored creator edits"),
                    UEditorLoadingAndSavingUtils::SaveMap(Restored, Map));
            }
            else
                AddInfo(TEXT(
                    "Test cleanup preserves the saved input: dependency mutation removed Arrow in the loaded failed map; it is not resaved."));
        }
    };
    if (Variant == TEXT("moved"))
        Rule->SetActorLocation(Moved);
    if (Variant == TEXT("changed"))
        Arrow->ArrowColor = ChangedColor;
    if (Variant == TEXT("added"))
    {
        for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
            if (It->GetStaticMeshComponent()->GetStaticMesh())
            {
                AddedMesh = It->GetStaticMeshComponent()->GetStaticMesh();
                break;
            }
        if (!AddedMesh)
        {
            // Persistent geometry can be streamed/instanced. Load one actual retained game mesh,
            // independently requiring original package/SHA custody and unchanged diagnostic state.
            TArray<FAssetData> Meshes;
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"))
                .Get()
                .GetAssetsByClass(UStaticMesh::StaticClass()->GetFName(), Meshes);
            Meshes.Sort(
                [](const FAssetData& A, const FAssetData& B) { return A.PackageName.LexicalLess(B.PackageName); });
            int32 Checked = 0;
            for (const FAssetData& Data : Meshes)
            {
                if (!Data.PackageName.ToString().StartsWith(TEXT("/Game/Carrot4/")))
                    continue;
                if (++Checked > 32)
                    break;
                UStaticMesh* Mesh = Cast<UStaticMesh>(Data.GetAsset());
                FP4OriginalRecord Record;
                FString Why;
                if (Mesh && P4ReadOriginalRecord(Mesh, Record, Why) && Record.Package == Data.PackageName.ToString() &&
                    Record.SourceDigest.Len() == 64 &&
                    P4DiagnoseOriginalAsset(Mesh).Comparison == EP4OriginalComparison::Equal)
                {
                    AddedMesh = Mesh;
                    break;
                }
            }
        }
        if (!TestNotNull(TEXT("Added creator actor uses an actual qualified warm game mesh"), AddedMesh))
            return false;
        UE_LOG(LogTemp, Display, TEXT("Kadan saved dependency added actor uses actual unchanged game mesh %s"),
            *AddedMesh->GetPathName());
        FActorSpawnParameters Params;
        Params.OverrideLevel = World->PersistentLevel;
        Params.Name = AddedName;
        AStaticMeshActor* Added = World->SpawnActor<AStaticMeshActor>(
            AStaticMeshActor::StaticClass(), OriginalTransform, Params);
        if (!TestNotNull(TEXT("Creator static mesh actor is placed in the saved map"), Added))
            return false;
        Added->GetStaticMeshComponent()->SetStaticMesh(AddedMesh);
    }
    World->MarkPackageDirty();
    UE_LOG(LogTemp, Display, TEXT("Kadan saved dependency before SaveMap: flags=%08x"),
        World->GetOutermost()->GetPackageFlags());
    if (!TestTrue(
            TEXT("Creator's initial edit is saved before reimport"), UEditorLoadingAndSavingUtils::SaveMap(World, Map)))
        return false;
    TestTrue(TEXT("Successful editor SaveMap reproduces the post-save registry NewlyCreated flag"),
        World->GetOutermost()->HasAnyPackageFlags(PKG_NewlyCreated));
    const FString Repo = FPaths::GetPath(
        FPaths::GetPath(FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"))));
    const FString Retoc = Repo / TEXT("private/retoc/retoc.exe"), Repak = Repo / TEXT("private/repak/repak.exe");
    const FString Base = TEXT("/Game/Carrot4/Placeables/GameRule/BP_CarrotGameRuleBase");
    const FString Leaf = TEXT("/Game/Carrot4/Placeables/GameRule/BP_CarrotGameRuleStoryCaveOtakaraCollect");
    const FString Header = Game / TEXT("Carrot4/Content") / Map.Mid(6) + TEXT(".umap");
    const FString Body = FPaths::ChangeExtension(Header, TEXT("uexp"));
    for (int32 Cycle = 0; Cycle < 2; ++Cycle)
    {
        const auto TracePackage = [&](const TCHAR* Stage)
        {
            UPackage* Package = World->GetOutermost();
            UE_LOG(LogTemp, Display, TEXT("Kadan saved dependency package %s: %s flags=%08x filename=%s size=%llu"),
                Stage, *Package->GetName(), Package->GetPackageFlags(), *Package->FileName.ToString(),
                (unsigned long long)Package->FileSize);
        };
        TracePackage(TEXT("before-scope"));
        FString Error;
        void* Scope = Seams.Begin(Map, Error);
        TracePackage(TEXT("after-scope"));
        if (!TestNotNull(TEXT("Production early scope captures actual saved source and creator edits"), Scope))
        {
            AddInfo(Error);
            return false;
        }
        ON_SCOPE_EXIT
        {
            Seams.End(Scope);
        };
        UE_LOG(LogTemp, Display, TEXT("Kadan saved dependency component test %s: actual BP reload cycle %d"), *Variant,
            Cycle);
        const FString BP = UKadanBlender::ImportGamePackages(Game, Retoc, {Leaf}, true, Repak, true);
        TSharedPtr<FJsonObject> Report;
        if (!TestTrue(TEXT("Actual BP-only service succeeds"),
                FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(BP), Report) && Report.IsValid() &&
                    Report->GetBoolField(TEXT("ok"))))
        {
            AddInfo(BP);
            return false;
        }
        const auto Imported = Report->GetObjectField(TEXT("imported"));
        if (!TestTrue(TEXT("Exactly the two actual Blueprint packages are reimported"),
                Imported->Values.Num() == 2 && Imported->HasField(Base) && Imported->HasField(Leaf)))
        {
            AddInfo(BP);
            return false;
        }
        TracePackage(TEXT("after-blueprints"));
        const auto Result = P4ReimportOriginalLevelNative(World, Header, Body, true);
        // Package reload retires the old world even when a later readback check fails.
        // Reacquire by ordinary package identity; never inspect the retired raw pointer.
        World = LoadObject<UWorld>(
            nullptr, *(Map + TEXT(".") + FPackageName::GetShortName(Map)), nullptr, LOAD_NoWarn | LOAD_Quiet);
        if (World)
            TracePackage(TEXT("after-native-reimport"));
        if (!TestTrue(FString::Printf(TEXT("Native reimport saves the map after actual BP reload, cycle %d"), Cycle),
                Result.IsValid() && Result.Saved))
        {
            AddInfo(Result.ErrorDetail);
            return false;
        }
        World = UEditorLoadingAndSavingUtils::LoadMap(Map);
        Rule = World ? FindObject<AActor>(World->PersistentLevel, *ActorName) : nullptr;
        Arrow = Rule ? Cast<UArrowComponent>(Rule->GetRootComponent()) : nullptr;
        if (!TestNotNull(TEXT("Source Arrow survives dependency replacement and saved cold readback"), Arrow))
            return false;
        if (Variant == TEXT("moved"))
            TestTrue(TEXT("Initial saved creator move survives"), Rule->GetActorLocation().Equals(Moved, .001f));
        if (Variant == TEXT("changed"))
            TestTrue(TEXT("Initial saved Arrow property survives its old instance being destroyed"),
                Arrow->ArrowColor == ChangedColor);
        if (Variant == TEXT("added"))
        {
            AStaticMeshActor* Added = FindObject<AStaticMeshActor>(World->PersistentLevel, *AddedName.ToString());
            TestTrue(TEXT("Initial saved creator addition and mesh survive"),
                Added && Added->GetStaticMeshComponent()->GetStaticMesh() == AddedMesh);
        }
        const auto Actual = P4AnalyzeOriginalLevelChanges(World);
        TestTrue(TEXT("New baseline and creator delta remain readable"), Actual.IsValid());
        if (Variant == TEXT("unchanged"))
            TestTrue(TEXT("Unchanged input remains semantically unchanged"), Actual.Unchanged);
        if (HasAnyErrors())
            return false;
    }
    return !HasAnyErrors();
}
#endif

#if P4_MAPKIT_TESTS
#include "P4LevelChanges.h"
#include "P4KadanImport.h"
#include "P4KadanImportSources.h"
#include "P4KadanTestSeams.h"
#include "P4BlenderExport.h"
#include "Serialization/JsonSerializer.h"
#include "P4OriginalLevelImport.h"
#include "P4LevelContent.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/Blueprint.h"
#include "Engine/LevelStreaming.h"
#include "FileHelpers.h"
#include "GameFramework/Actor.h"
#include "Misc/PackageName.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4DiagnosticWorldCookTest, "P4MapKit.Levels.DiagnosticCookPreservesDirtyState",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4DiagnosticWorldCookTest::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_DIAGNOSTIC_WORLD")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. Select the owned reimported Cave027 sublevels."));
        return true;
    }
    TArray<TSharedPtr<FP4KadanSourceAsset>> Sources;
    TArray<FString> Problems;
    const FString Game = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REIMPORT_GAME"));
    const bool Scanned = !Game.IsEmpty() && P4KadanScanGame(Game, Sources, Problems);
    AddInfo(FString::Printf(TEXT("Source archive %s: %d packages; %s"), *Game, Sources.Num(),
        *FString::Join(Problems, TEXT(" | ")).Left(1500)));
    if (!TestTrue(TEXT("The actual source pod Blueprint is available alongside converted worlds"),
            Scanned &&
                Sources.ContainsByPredicate([](const TSharedPtr<FP4KadanSourceAsset>& Row)
                    { return Row.IsValid() && Row->Package == TEXT("/Game/Carrot4/Placeables/Pod/GPod"); })))
        return false;
    FP4KadanImportSourceScope OriginalSources(Sources);
    for (const TCHAR* Suffix : {TEXT("GameArea"), TEXT("Objects")})
    {
        const FString Leaf = FString(TEXT("Cave027_F00_")) + Suffix;
        const FString Package = TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave027/Cave027_F00/Sublevels/") + Leaf;
        UWorld* World = LoadObject<UWorld>(nullptr, *(Package + TEXT(".") + Leaf));
        if (!TestNotNull(TEXT("The actual saved source sublevel opens"), World))
            return false;
        UPackage* Outer = World->GetOutermost();
        const bool Dirty = Outer->IsDirty();
        ON_SCOPE_EXIT
        {
            Outer->SetDirtyFlag(Dirty);
        };
        TestFalse(TEXT("The saved source fixture begins clean"), Dirty);
        if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REIMPORT_WORLD")) == TEXT("1"))
        {
            // The normal batch has already loaded its Blueprint assets, including editor
            // SCS templates. A class-only cold load does not expose those template exports.
            TSet<FString> Loaded;
            for (AActor* Actor : World->PersistentLevel->Actors)
                if (IsValid(Actor))
                    for (UClass* Class = Actor->GetClass(); Class && !Class->HasAnyClassFlags(CLASS_Native);
                        Class = Class->GetSuperClass())
                    {
                        const FString Owner = Class->GetOutermost()->GetName();
                        if (!Loaded.Contains(Owner))
                        {
                            Loaded.Add(Owner);
                            LoadObject<UBlueprint>(nullptr, *(Owner + TEXT(".") + FPackageName::GetShortName(Owner)));
                        }
                    }
            const FString Header = FPaths::ProjectSavedDir() / TEXT("Kadan/ConvertedOriginals/Carrot4/Content") /
                    Package.Mid(6) +
                TEXT(".umap");
            const auto Reimport = P4ReimportOriginalLevelNative(
                World, Header, FPaths::ChangeExtension(Header, TEXT("uexp")), true);
            World = LoadObject<UWorld>(nullptr, *(Package + TEXT(".") + Leaf));
            if (!TestTrue(TEXT("Actual standalone source sublevel reimport is saved: ") + Reimport.ErrorDetail,
                    Reimport.IsValid() && Reimport.Saved && World))
                return false;
            Outer = World->GetOutermost();
            TestFalse(TEXT("Successful source reimport returns a clean published sublevel: ") + Leaf, Outer->IsDirty());
        }
        for (const bool Pending : {false, true})
        {
            Outer->SetDirtyFlag(Pending);
            const auto Before = P4AnalyzeOriginalLevelChanges(World);
            TestTrue(TEXT("The reconstructed source layer remains qualified and unchanged: ") + Before.ErrorDetail,
                Before.IsValid() && Before.Unchanged);
            TestEqual(TEXT("A diagnostic cook and its component teardown preserve the caller's dirty state: ") + Leaf,
                Outer->IsDirty(), Pending);
            const auto After = P4AnalyzeOriginalLevelChanges(World);
            TestTrue(TEXT("A second cook retains the complete source content"),
                After.IsValid() && After.State == Before.State);
            TestEqual(
                TEXT("Repeated diagnostics never create or clear pending work: ") + Leaf, Outer->IsDirty(), Pending);
        }
    }
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SavedWorldGraphPublicationTest, "P4MapKit.Kadan.SavedWorldGraphPublication",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4SavedWorldGraphPublicationTest::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REIMPORT_GRAPH")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. Select the owned Cave027 saved-world publication fixture."));
        return true;
    }
    auto& Features = IModularFeatures::Get();
    if (!TestTrue(TEXT("The actual early snapshot scope is available"),
            Features.IsModularFeatureAvailable(FP4KadanSavedLevelTestSeams::FeatureName())))
        return false;
    const auto& Seams = Features.GetModularFeature<FP4KadanSavedLevelTestSeams>(
        FP4KadanSavedLevelTestSeams::FeatureName());
    const FString Map = TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave027/Cave027_F00/Cave027_F00_P");
    TArray<TSharedPtr<FP4KadanSourceAsset>> Sources;
    TArray<FString> Problems;
    if (!P4KadanScanGame(FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REIMPORT_GAME")), Sources, Problems))
        return false;
    FP4KadanImportSourceScope SourceScope(Sources);
    UWorld* Root = LoadObject<UWorld>(nullptr, *(Map + TEXT(".Cave027_F00_P")));
    if (!TestNotNull(TEXT("The saved root loads as an asset before editor opening"), Root))
        return false;
    FString Error;
    void* Scope = Seams.Begin(Map, Error);
    if (!TestNotNull(TEXT("The original saved streaming graph is captured before publication"), Scope))
    {
        AddError(Error);
        return false;
    }
    ON_SCOPE_EXIT
    {
        if (Scope)
            Seams.End(Scope);
    };
    FString Containers = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    FPaths::NormalizeDirectoryName(Containers);
    const FString Private = FPaths::GetPath(Containers);
    const FString BP = UKadanBlender::ImportGamePackages(
        FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REIMPORT_GAME")), Private / TEXT("retoc/retoc.exe"),
        {TEXT("/Game/Carrot4/Placeables/Pod/GPod")}, true, Private / TEXT("repak/repak.exe"), true);
    TSharedPtr<FJsonObject> BPReport;
    if (!TestTrue(TEXT("The actual source pod Blueprint is reimported after early graph capture"),
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(BP), BPReport) && BPReport.IsValid() &&
                BPReport->GetBoolField(TEXT("ok"))))
    {
        AddInfo(BP.Left(3000));
        return false;
    }
    const FString MapFolder = FPackageName::GetLongPackagePath(Map);
    for (const FString& Name : TArray<FString>{MapFolder / TEXT("Sublevels/Cave027_F00_GameArea"),
             MapFolder / TEXT("Sublevels/Cave027_F00_Objects"), Map})
    {
        UWorld* World = LoadObject<UWorld>(nullptr, *(Name + TEXT(".") + FPackageName::GetShortName(Name)));
        if (!TestNotNull(TEXT("The actual source world is available"), World))
            return false;
        for (AActor* Actor : World->PersistentLevel->Actors)
            if (IsValid(Actor))
                for (UClass* Class = Actor->GetClass(); Class && !Class->HasAnyClassFlags(CLASS_Native);
                    Class = Class->GetSuperClass())
                {
                    const FString Owner = Class->GetOutermost()->GetName();
                    LoadObject<UBlueprint>(nullptr, *(Owner + TEXT(".") + FPackageName::GetShortName(Owner)));
                }
        const FString Header =
            FPaths::ProjectSavedDir() / TEXT("Kadan/ConvertedOriginals/Carrot4/Content") / Name.Mid(6) + TEXT(".umap");
        const auto Result = P4ReimportOriginalLevelNative(
            World, Header, FPaths::ChangeExtension(Header, TEXT("uexp")), true);
        if (!TestTrue(TEXT("The source world is successfully published: ") + Name + TEXT(" ") + Result.ErrorDetail,
                Result.IsValid() && Result.Saved))
            return false;
        Root = LoadObject<UWorld>(nullptr, *(Map + TEXT(".Cave027_F00_P")));
        if (!TestNotNull(TEXT("The published root identity remains readable"), Root))
            return false;
        TArray<FString> Names{Map};
        for (ULevelStreaming* Stream : Root->GetStreamingLevels())
            if (Stream)
                Names.Add(Stream->GetWorldAssetPackageName());
        for (const FString& Part : Names)
            if (UPackage* Loaded = FindPackage(nullptr, *Part))
            {
                AddInfo(FString::Printf(TEXT("After publishing %s: %s dirty=%d flags=%08x"), *Name, *Part,
                    Loaded->IsDirty(), Loaded->GetPackageFlags()));
                if (Name == Map)
                    TestFalse(TEXT("Every saved source world remains unloadable after graph publication: ") + Part,
                        Loaded->IsDirty());
                if (Name == Map && Loaded->IsDirty())
                    if (UWorld* Changed = UWorld::FindWorldInPackage(Loaded))
                    {
                        const FString Delta = P4LevelChangesJson(P4AnalyzeOriginalLevelChanges(Changed));
                        FFileHelper::SaveStringToFile(Delta,
                            *(FPaths::ProjectSavedDir() / TEXT("P4MapKit") /
                                (Changed->GetName() + TEXT("-publication-delta.json"))),
                            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
                        AddInfo(TEXT("Dirty published world actual source delta: ") + Delta.Left(14000));
                    }
            }
    }
    if (!HasAnyErrors())
    {
        const FString Package = MapFolder / TEXT("Sublevels/Cave027_F00_GameArea");
        UWorld* World = LoadObject<UWorld>(nullptr, *(Package + TEXT(".Cave027_F00_GameArea")));
        AActor* Edited = nullptr;
        if (World)
            for (AActor* Actor : World->PersistentLevel->Actors)
                if (IsValid(Actor) && Actor->GetRootComponent() && !Actor->IsChildActor() &&
                    Actor->GetClass()->GetName().StartsWith(TEXT("CMA_")))
                {
                    Edited = Actor;
                    break;
                }
        if (!TestNotNull(TEXT("An actual original room actor is available for the creator-edit control"), Edited))
            return false;
        const FVector Before = Edited->GetActorLocation(), Changed = Before + FVector(1, 0, 0);
        const bool Dirty = World->GetOutermost()->IsDirty();
        ON_SCOPE_EXIT
        {
            Edited->SetActorLocation(Before);
            World->GetOutermost()->SetDirtyFlag(Dirty);
        };
        Edited->SetActorLocation(Changed);
        World->MarkPackageDirty();
        Seams.End(Scope);
        Scope = nullptr;
        TestTrue(
            TEXT("A creator edit after publication retains its pending dirty flag"), World->GetOutermost()->IsDirty());
        TestTrue(TEXT("Publication cleanup never restores over the creator's changed actor"),
            Edited->GetActorLocation().Equals(Changed));
    }
    return !HasAnyErrors();
}
#endif

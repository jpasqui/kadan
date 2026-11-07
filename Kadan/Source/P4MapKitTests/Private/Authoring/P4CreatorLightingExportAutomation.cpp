#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "P4ExportDriver.h"
#include "P4ModDescriptor.h"
#include "P4KadanCreatorReview.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/MapBuildDataRegistry.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/Material.h"
#include "HAL/PlatformMisc.h"
#include "Misc/PackageName.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4CreatorLightingExportTest, "P4MapKit.Quality.CreatorLightingExport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4CreatorLightingExportTest::RunTest(const FString&)
{
    FP4ModDescriptor Mod;
    Mod.Id = TEXT("test.creatorlighting");
    Mod.Name = TEXT("Baked light");
    Mod.Version = TEXT("1.0.0");
    FP4ModNames Names;
    FString Error;
    if (!P4ModDerivedNames(Mod, Names, Error))
    {
        AddError(Error);
        return false;
    }
    const FString LevelName = TEXT("Baked_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UPackage* Package = CreatePackage(*(Names.PackageRoot / LevelName));
    UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false, FName(*LevelName), Package);
    if (!TestNotNull(TEXT("Authored fixture world"), World))
        return false;
    TStrongObjectPtr<UWorld> HeldWorld(World);
    UPackage* LightingPackage = CreatePackage(*(Package->GetName() + TEXT("_BuiltData")));
    TStrongObjectPtr<UMapBuildDataRegistry> Registry(NewObject<UMapBuildDataRegistry>(
        LightingPackage, FName(*(LevelName + TEXT("_BuiltData"))), RF_Public | RF_Standalone));
    ON_SCOPE_EXIT
    {
        World->PersistentLevel->MapBuildData = nullptr;
        World->DestroyWorld(false);
        for (UPackage* Each : {Package, LightingPackage})
            Each->SetDirtyFlag(false);
        Registry->ClearFlags(RF_Public | RF_Standalone);
    };
    const FGuid LightId = FGuid::NewGuid();
    Registry->FindOrAllocateLightBuildData(LightId, true).ShadowMapChannel = 2;
    World->PersistentLevel->MapBuildData = Registry.Get();
    TestTrue(TEXT("The authored level's real Lightmass registry has a cook route"),
        P4ExportAssetAvailability(Registry.Get(), Mod).Route == EP4ExportAssetRoute::CreatorLighting);
    FP4ExportSelectionPreview Selection;
    FP4ExportSelectionRow Row;
    Row.Package = LightingPackage->GetName();
    Row.AssetClass = TEXT("MapBuildDataRegistry");
    Row.Disposition = EP4ExportDisposition::CreatorPackage;
    Selection.Rows.Add(Row);
    Selection.CookCandidates.Add(Row.Package);
    const FP4KadanCreatorReview Reviewed = P4ReviewCreatorSelection(Mod, Selection, nullptr);
    TestTrue(TEXT("The actual export review admits the baked registry"),
        Reviewed.Blockers.Num() == 0 && Reviewed.Ships == 1);
    TestEqual(TEXT("Qualification preserves the baked stationary shadow channel"),
        Registry->GetLightBuildData(LightId)->ShadowMapChannel, 2);
    World->PersistentLevel->MapBuildData = nullptr;
    const FP4ExportAvailability Orphan = P4ExportAssetAvailability(Registry.Get(), Mod);
    TestFalse(TEXT("A registry with only a matching filename is refused"), Orphan.IsAvailable());
    TestTrue(
        TEXT("The refusal explains the missing authored-level link"), Orphan.Reason.Contains(TEXT("actually used")));
    TestEqual(
        TEXT("Refusal also preserves light build data"), Registry->GetLightBuildData(LightId)->ShadowMapChannel, 2);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4BakedStationaryLightFunctionDataTest,
    "P4MapKit.Quality.BakedStationaryLightFunctionData",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4BakedStationaryLightFunctionDataTest::RunTest(const FString&)
{
    const FString Package = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_LIGHT_FUNCTION_BAKED_LEVEL"));
    if (Package.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. Supply a real Lightmass-baked stationary light-function fixture."));
        return true;
    }
    UWorld* World = LoadObject<UWorld>(
        nullptr, *(Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Package)));
    if (!TestTrue(TEXT("Baked fixture has its native registry"),
            World && World->PersistentLevel && World->PersistentLevel->MapBuildData))
        return false;
    int32 Count = 0, BakedReceivers = 0;
    for (AActor* Actor : World->PersistentLevel->Actors)
    {
        if (!IsValid(Actor))
            continue;
        TInlineComponentArray<UStaticMeshComponent*> Meshes(Actor);
        for (UStaticMeshComponent* Mesh : Meshes)
        {
            if (Mesh->Mobility != EComponentMobility::Static || !Mesh->LODData.Num())
                continue;
            const FGuid Id = Mesh->LODData[0].MapBuildDataId;
            const auto* Data = World->PersistentLevel->MapBuildData->GetMeshBuildData(Id);
            if (!Data || !Data->LightMap.IsValid() || !Data->ShadowMap.IsValid())
                continue;
            ++BakedReceivers;
            AddInfo(FString::Printf(
                TEXT("Baked receiver %s GUID=%s lightmap=1 shadowmap=1"), *Mesh->GetPathName(), *Id.ToString()));
        }
        TInlineComponentArray<USpotLightComponent*> Lights(Actor);
        for (USpotLightComponent* Light : Lights)
        {
            if (Light->Mobility != EComponentMobility::Stationary || !Light->LightFunctionMaterial)
                continue;
            ++Count;
            TestTrue(TEXT("Baked light functions retain both required shadow settings"),
                Light->CastShadows && Light->CastDynamicShadows);
            TestTrue(TEXT("Saved light-function domain"),
                Light->LightFunctionMaterial->GetMaterial()->MaterialDomain == MD_LightFunction);
            const auto* Data = World->PersistentLevel->MapBuildData->GetLightBuildData(Light->LightGuid);
            if (!TestNotNull(TEXT("Light GUID joins its baked build-data entry"), Data))
                continue;
            TestTrue(TEXT("Baked stationary shadow channel is usable"),
                Data->ShadowMapChannel >= 0 && Data->ShadowMapChannel < 4);
            AddInfo(FString::Printf(TEXT("Baked light %s GUID=%s shadow channel=%d"), *Light->GetPathName(),
                *Light->LightGuid.ToString(), Data->ShadowMapChannel));
        }
    }
    TestTrue(TEXT("The real fixture contains a stationary spotlight with a light function"), Count > 0);
    TestTrue(TEXT("The real fixture contains a static receiver with baked light and shadow data"), BakedReceivers > 0);
    return !HasAnyErrors();
}
#endif

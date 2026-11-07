#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "Engine/MapBuildDataRegistry.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/StaticMesh.h"
#include "Components/LightComponent.h"
#include "Components/PostProcessComponent.h"
#include "HAL/IConsoleManager.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/ReflectionCaptureComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "P4OriginalLevelImport.h"
#include "P4OriginalRecord.h"

// Where a saved map's lighting is in the editor: its levels and their build data, its lights, sky
// lights, post-process volumes and how many static meshes have a lightmap. Nothing is imported or saved.
// A diagnostic for people to read, not a check: run it by name with P4_NATIVE_SCREEN_MAP set. The
// report goes next to the map pictures (Saved/P4MapKit/MapScreens/<map>/lighting.json).
namespace P4MapLightingReport
{
FString Mobility(const USceneComponent* Component)
{
    if (!Component)
        return TEXT("none");
    return Component->Mobility == EComponentMobility::Static    ? TEXT("static")
        : Component->Mobility == EComponentMobility::Stationary ? TEXT("stationary")
                                                                : TEXT("movable");
}
FString Record(UObject* Asset)
{
    FP4OriginalRecord Read;
    FString Error;
    if (!Asset)
        return TEXT("none");
    if (!P4ReadOriginalRecord(Asset, Read, Error))
        return TEXT("no record (") + Error + TEXT(")");
    return FString::Printf(TEXT("fidelity %s, reference only %d, read only %d, kind %s"), *Read.Fidelity,
        Read.ReferenceOnly, Read.ReadOnly, *Read.Kind);
}
} //namespace P4MapLightingReport

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MapLightingReport, "P4MapKit.Editor.MapLightingReport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MapLightingReport::RunTest(const FString&)
{
    using namespace P4MapLightingReport;
    const FString Map = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SCREEN_MAP"));
    if (Map.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. Set P4_NATIVE_SCREEN_MAP to a saved map for its lighting report"));
        return true;
    }
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(Map);
    if (!TestNotNull(TEXT("the saved map opens"), World))
        return false;
    World->FlushLevelStreaming(EFlushLevelStreamingType::Full);
    auto Report = MakeShared<FJsonObject>();
    Report->SetStringField(TEXT("map"), Map);
    Report->SetNumberField(TEXT("lighting_unbuilt_objects"), World->NumLightingUnbuiltObjects);
    Report->SetNumberField(TEXT("unbuilt_reflection_captures"), World->NumUnbuiltReflectionCaptures);
    TArray<TSharedPtr<FJsonValue>> Streams;
    for (ULevelStreaming* Stream : World->GetStreamingLevels())
    {
        if (!Stream)
            continue;
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("package"), Stream->GetWorldAssetPackageName());
        Row->SetStringField(TEXT("class"), Stream->GetClass()->GetName());
        Row->SetBoolField(TEXT("loaded"), Stream->GetLoadedLevel() != nullptr);
        Row->SetBoolField(TEXT("visible"), Stream->GetLoadedLevel() && Stream->GetLoadedLevel()->bIsVisible);
        Row->SetBoolField(TEXT("visible_in_editor"), Stream->GetShouldBeVisibleInEditor());
        Row->SetBoolField(TEXT("package_exists"), FPackageName::DoesPackageExist(Stream->GetWorldAssetPackageName()));
        Streams.Add(MakeShared<FJsonValueObject>(Row));
    }
    Report->SetArrayField(TEXT("streaming_levels"), Streams);
    Report->SetStringField(TEXT("persistent_level_record"), Record(World));
    Report->SetBoolField(TEXT("force_no_precomputed_lighting"), World->GetWorldSettings()->bForceNoPrecomputedLighting);
    auto Rendering = MakeShared<FJsonObject>();
    for (const TCHAR* Name :
        {TEXT("r.AllowStaticLighting"), TEXT("r.SupportStationarySkylight"), TEXT("r.DefaultFeature.AutoExposure"),
            TEXT("r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange"), TEXT("r.EyeAdaptationQuality"),
            TEXT("r.ShadowQuality"), TEXT("r.MaterialQualityLevel"), TEXT("r.FeatureLevelPreview")})
        if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
            Rendering->SetStringField(Name, Variable->GetString());
    Report->SetObjectField(TEXT("rendering_cvars"), Rendering);
    TArray<TSharedPtr<FJsonValue>> Levels, Lights, Skies, Volumes, Components;
    for (ULevel* Level : World->GetLevels())
    {
        if (!Level)
            continue;
        UWorld* LevelWorld = Cast<UWorld>(Level->GetOuter());
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("package"), Level->GetOutermost()->GetName());
        Row->SetBoolField(TEXT("visible"), Level->bIsVisible);
        Row->SetBoolField(TEXT("lighting_scenario"), Level->bIsLightingScenario);
        UMapBuildDataRegistry* Data = Level->MapBuildData;
        Row->SetStringField(TEXT("build_data"), Data ? Data->GetPathName() : FString(TEXT("none")));
        Row->SetStringField(TEXT("build_data_record"), Record(Data));
        Row->SetStringField(TEXT("level_record"), Record(LevelWorld));
        TSet<FName> Omitted;
        FString Source, Error;
        const bool GameBacked = LevelWorld && P4ReadGameBackedLevelCustody(LevelWorld, Omitted, Source, Error);
        Row->SetBoolField(TEXT("game_backed"), GameBacked);
        TArray<TSharedPtr<FJsonValue>> OmittedNames;
        for (const FName& Name : Omitted)
            OmittedNames.Add(MakeShared<FJsonValueString>(Name.ToString()));
        Row->SetArrayField(TEXT("omitted_actors"), OmittedNames);
        int32 StaticMeshes = 0, WithLightmap = 0, WithBuildData = 0, Movable = 0;
        for (AActor* Actor : Level->Actors)
        {
            if (!IsValid(Actor))
                continue;
            TInlineComponentArray<UActorComponent*> Parts(Actor);
            for (UActorComponent* Part : Parts)
            {
                if (UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Part))
                {
                    if (!Mesh->GetStaticMesh() || !Mesh->IsVisible())
                        continue;
                    ++StaticMeshes;
                    if (Mesh->Mobility == EComponentMobility::Movable)
                        ++Movable;
                    if (Mesh->LODData.Num() && Data && Data->GetMeshBuildData(Mesh->LODData[0].MapBuildDataId))
                        ++WithBuildData;
                    if (Mesh->LODData.Num() && Mesh->GetMeshMapBuildData(Mesh->LODData[0]) &&
                        Mesh->GetMeshMapBuildData(Mesh->LODData[0])->LightMap.IsValid())
                        ++WithLightmap;
                }
                else if (UPostProcessComponent* Post = Cast<UPostProcessComponent>(Part))
                {
                    const FPostProcessSettings& S = Post->Settings;
                    auto One = MakeShared<FJsonObject>();
                    One->SetStringField(TEXT("component"), Post->GetPathName());
                    One->SetStringField(TEXT("level"), Level->GetOutermost()->GetName());
                    One->SetBoolField(TEXT("enabled"), Post->bEnabled);
                    One->SetBoolField(TEXT("unbound"), Post->bUnbound);
                    One->SetNumberField(TEXT("blend_weight"), Post->BlendWeight);
                    One->SetNumberField(TEXT("exposure_method"), int32(S.AutoExposureMethod));
                    One->SetNumberField(TEXT("exposure_bias"), S.AutoExposureBias);
                    One->SetBoolField(TEXT("apply_physical_camera"), S.AutoExposureApplyPhysicalCameraExposure);
                    One->SetNumberField(TEXT("iso"), S.CameraISO);
                    One->SetNumberField(TEXT("shutter"), S.CameraShutterSpeed);
                    One->SetNumberField(TEXT("fstop"), S.DepthOfFieldFstop);
                    Components.Add(MakeShared<FJsonValueObject>(One));
                }
                else if (USkyLightComponent* Sky = Cast<USkyLightComponent>(Part))
                {
                    auto One = MakeShared<FJsonObject>();
                    One->SetStringField(TEXT("actor"), Actor->GetName());
                    One->SetStringField(TEXT("level"), Level->GetOutermost()->GetName());
                    One->SetStringField(TEXT("mobility"), Mobility(Sky));
                    One->SetNumberField(TEXT("intensity"), Sky->Intensity);
                    One->SetBoolField(TEXT("visible"), Sky->IsVisible());
                    One->SetBoolField(TEXT("affects_world"), Sky->bAffectsWorld);
                    One->SetNumberField(TEXT("source_type"), int32(Sky->SourceType));
                    One->SetStringField(
                        TEXT("cubemap"), Sky->Cubemap ? Sky->Cubemap->GetPathName() : FString(TEXT("none")));
                    One->SetStringField(TEXT("color"), Sky->GetLightColor().ToString());
                    Skies.Add(MakeShared<FJsonValueObject>(One));
                }
                else if (ULightComponent* Light = Cast<ULightComponent>(Part))
                {
                    auto One = MakeShared<FJsonObject>();
                    One->SetStringField(TEXT("actor"), Actor->GetName());
                    One->SetStringField(TEXT("class"), Light->GetClass()->GetName());
                    One->SetStringField(TEXT("level"), Level->GetOutermost()->GetName());
                    One->SetStringField(TEXT("mobility"), Mobility(Light));
                    One->SetNumberField(TEXT("intensity"), Light->Intensity);
                    One->SetStringField(TEXT("color"), Light->GetLightColor().ToString());
                    One->SetBoolField(TEXT("visible"), Light->IsVisible());
                    One->SetBoolField(TEXT("affects_world"), Light->bAffectsWorld);
                    One->SetBoolField(TEXT("registered"), Light->IsRegistered());
                    One->SetBoolField(TEXT("render_state_created"), Light->IsRenderStateCreated());
                    One->SetNumberField(TEXT("lighting_channel_mask"),
                        int32(Light->LightingChannels.bChannel0) | (int32(Light->LightingChannels.bChannel1) << 1) |
                            (int32(Light->LightingChannels.bChannel2) << 2));
                    One->SetBoolField(TEXT("build_data"), Data && Data->GetLightBuildData(Light->LightGuid) != nullptr);
                    One->SetBoolField(TEXT("hidden_in_editor"), Actor->IsHiddenEd());
                    Lights.Add(MakeShared<FJsonValueObject>(One));
                }
            }
            if (APostProcessVolume* Volume = Cast<APostProcessVolume>(Actor))
            {
                const FPostProcessSettings& S = Volume->Settings;
                auto One = MakeShared<FJsonObject>();
                One->SetStringField(TEXT("actor"), Actor->GetName());
                One->SetStringField(TEXT("level"), Level->GetOutermost()->GetName());
                One->SetBoolField(TEXT("enabled"), Volume->bEnabled);
                One->SetBoolField(TEXT("unbound"), Volume->bUnbound);
                One->SetNumberField(TEXT("blend_weight"), Volume->BlendWeight);
                One->SetBoolField(TEXT("override_exposure_method"), S.bOverride_AutoExposureMethod);
                One->SetNumberField(TEXT("exposure_method"), int32(S.AutoExposureMethod));
                One->SetBoolField(TEXT("override_exposure_bias"), S.bOverride_AutoExposureBias);
                One->SetNumberField(TEXT("exposure_bias"), S.AutoExposureBias);
                One->SetBoolField(TEXT("override_min_brightness"), S.bOverride_AutoExposureMinBrightness);
                One->SetNumberField(TEXT("min_brightness"), S.AutoExposureMinBrightness);
                One->SetBoolField(TEXT("override_max_brightness"), S.bOverride_AutoExposureMaxBrightness);
                One->SetNumberField(TEXT("max_brightness"), S.AutoExposureMaxBrightness);
                One->SetBoolField(TEXT("override_iso"), S.bOverride_CameraISO);
                One->SetNumberField(TEXT("iso"), S.CameraISO);
                One->SetBoolField(TEXT("override_shutter"), S.bOverride_CameraShutterSpeed);
                One->SetNumberField(TEXT("shutter"), S.CameraShutterSpeed);
                One->SetBoolField(TEXT("override_fstop"), S.bOverride_DepthOfFieldFstop);
                One->SetNumberField(TEXT("fstop"), S.DepthOfFieldFstop);
                One->SetBoolField(
                    TEXT("override_apply_physical_camera"), S.bOverride_AutoExposureApplyPhysicalCameraExposure);
                One->SetNumberField(TEXT("apply_physical_camera"), S.AutoExposureApplyPhysicalCameraExposure);
                One->SetBoolField(TEXT("override_indirect_intensity"), S.bOverride_IndirectLightingIntensity);
                One->SetNumberField(TEXT("indirect_intensity"), S.IndirectLightingIntensity);
                Volumes.Add(MakeShared<FJsonValueObject>(One));
            }
        }
        Row->SetNumberField(TEXT("static_meshes"), StaticMeshes);
        Row->SetNumberField(TEXT("movable_meshes"), Movable);
        Row->SetNumberField(TEXT("meshes_with_build_data"), WithBuildData);
        Row->SetNumberField(TEXT("meshes_with_lightmap"), WithLightmap);
        Levels.Add(MakeShared<FJsonValueObject>(Row));
    }
    Report->SetArrayField(TEXT("levels"), Levels);
    Report->SetArrayField(TEXT("lights"), Lights);
    Report->SetArrayField(TEXT("sky_lights"), Skies);
    Report->SetArrayField(TEXT("post_process_volumes"), Volumes);
    Report->SetArrayField(TEXT("post_process_components"), Components);
    const FString Out = FPaths::ProjectSavedDir() / TEXT("P4MapKit/MapScreens") / FPackageName::GetShortName(Map);
    IFileManager::Get().MakeDirectory(*Out, true);
    FString Json;
    FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
    TestTrue(TEXT("the lighting report is written"),
        FFileHelper::SaveStringToFile(
            Json, *(Out / TEXT("lighting.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    AddInfo(FString::Printf(
        TEXT("%d levels, %d lights, %d sky lights, %d post-process volumes, %d objects with unbuilt lighting"),
        Levels.Num(), Lights.Num(), Skies.Num(), Volumes.Num(), World->NumLightingUnbuiltObjects));
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/DirectionalLight.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ReflectionCaptureComponent.h"
#include "Components/SkyLightComponent.h"
#include "LandscapeComponent.h"
#include "LandscapeProxy.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/PlayerStart.h"
#include "Materials/Material.h"
#include "MaterialShared.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "ContentStreaming.h"
#include "ShaderCompiler.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"

// Render targets are independent of editor viewport focus/redraw. These are diagnostic
// previews with declared lighting, never game appearance or completeness evidence.
namespace P4SweepCapture
{
void RecordComponentAudit(UWorld* World, const TSharedRef<FJsonObject>& Report);
FBox ExpandFloorFootprint(
    const FBox&, const TArray<FVector>&, const TArray<UPrimitiveComponent*>&, const TSharedRef<FJsonObject>&);
bool Capture(UWorld* World, const FString& Out, const TSharedRef<FJsonObject>& Report)
{
    const double Began = FPlatformTime::Seconds();
    Report->SetBoolField(TEXT("capture_verified"), false);
    const auto Fail = [&](const FString& Error)
    {
        Report->SetBoolField(TEXT("capture_verified"), false);
        Report->SetStringField(TEXT("capture_error"), Error);
        return false;
    };
    if (!World || GUsingNullRHI)
        return Fail(TEXT("SceneCapture2D requires a loaded editor world and a renderer"));
    if (!IFileManager::Get().MakeDirectory(*Out, true))
        return Fail(TEXT("Cannot create capture folder"));
    World->FlushLevelStreaming(EFlushLevelStreamingType::Full);
    RecordComponentAudit(World, Report);
    TArray<UPrimitiveComponent*> Parts;
    TArray<FVector> Terrain, Rooms, LandscapePoints;
    FBox Bounds(ForceInit), RoomBounds(ForceInit), LandscapeBounds(ForceInit), TerrainGeometryBounds(ForceInit);
    TArray<TSharedPtr<FJsonValue>> RoomExamples;
    AActor* Start = nullptr;
    for (ULevel* Level : World->GetLevels())
        if (Level && Level->bIsVisible)
            for (AActor* Actor : Level->Actors)
                if (IsValid(Actor))
                {
                    if (Actor->IsA<APlayerStart>() || Actor->GetClass()->GetName().Contains(TEXT("PlayerStart")) ||
                        Actor->GetName().Contains(TEXT("PlayerStart")))
                        if (!Start || Actor->GetPathName() < Start->GetPathName())
                            Start = Actor;
                    if (Actor->IsHiddenEd())
                        continue;
                    TInlineComponentArray<UPrimitiveComponent*> Components(Actor);
                    for (UPrimitiveComponent* Part : Components)
                    {
                        if ((!Part->IsA<UStaticMeshComponent>() && !Part->IsA<ULandscapeComponent>()) ||
                            !Part->IsRegistered() || !Part->IsVisible() || Part->Bounds.Origin.ContainsNaN() ||
                            !FMath::IsFinite(Part->Bounds.SphereRadius) || Part->Bounds.SphereRadius <= 1.f)
                            continue;
                        const UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Part);
                        if (Mesh &&
                            (!Mesh->GetStaticMesh() || Mesh->GetStaticMesh()->GetPathName().Contains(TEXT("Sky"))))
                            continue;
                        Bounds += Part->Bounds.GetBox();
                        Parts.Add(Part);
                        if (Part->IsA<ULandscapeComponent>())
                        {
                            LandscapeBounds += Part->Bounds.GetBox();
                            LandscapePoints.Add(Part->Bounds.Origin);
                        }
                        const FString MeshPath = Mesh ? Mesh->GetStaticMesh()->GetPathName() : FString();
                        if (MeshPath.Contains(TEXT("/Terrain/Cave/")) &&
                            (MeshPath.Contains(TEXT("_RM_")) || MeshPath.Contains(TEXT("_CF_")) ||
                                MeshPath.Contains(TEXT("_WL_"))) &&
                            !Mesh->GetStaticMesh()->GetName().StartsWith(TEXT("COL_")))
                            RoomBounds += Part->Bounds.GetBox(); // Include connecting floors/walls in the full layout.
                        if (MeshPath.Contains(TEXT("/Terrain/Cave/")) && MeshPath.Contains(TEXT("_RM_")) &&
                            !Mesh->GetStaticMesh()->GetName().StartsWith(TEXT("COL_")))
                        {
                            // Room meshes can extend far below their authored floor pivot.
                            // Their bounds centre puts an eye inside the support structure.
                            Rooms.Add(
                                FVector(Part->Bounds.Origin.X, Part->Bounds.Origin.Y, Part->GetComponentLocation().Z));
                            if (RoomExamples.Num() < 8)
                            {
                                auto Example = MakeShared<FJsonObject>();
                                Example->SetStringField(TEXT("component"), Part->GetPathName());
                                Example->SetStringField(TEXT("pivot"), Part->GetComponentLocation().ToString());
                                Example->SetStringField(TEXT("bounds_center"), Part->Bounds.Origin.ToString());
                                RoomExamples.Add(MakeShared<FJsonValueObject>(Example));
                            }
                        }
                        if (Part->IsA<ULandscapeComponent>() ||
                            (MeshPath.Contains(TEXT("/Terrain/")) && !MeshPath.Contains(TEXT("/Kakiwari/")) &&
                                !MeshPath.Contains(TEXT("FarGround"))))
                        {
                            Terrain.Add(Part->Bounds.Origin);
                            TerrainGeometryBounds += Part->Bounds.GetBox();
                        }
                    }
                }
    // Empty levels still get four pictures, with an explicit empty-scene receipt.
    if (!Bounds.IsValid)
        Bounds = FBox(FVector(-1000, -1000, -100), FVector(1000, 1000, 100));
    Report->SetStringField(TEXT("capture_bounds_min"), Bounds.Min.ToString());
    Report->SetStringField(TEXT("capture_bounds_max"), Bounds.Max.ToString());
    Report->SetNumberField(TEXT("visible_geometry_components"), Parts.Num());
    Report->SetNumberField(TEXT("terrain_components"), Terrain.Num());
    TArray<TSharedPtr<FJsonValue>> LevelRows;
    for (ULevel* Level : World->GetLevels())
        if (Level)
        {
            auto Item = MakeShared<FJsonObject>();
            Item->SetStringField(TEXT("package"), Level->GetOutermost()->GetName());
            Item->SetBoolField(TEXT("visible"), Level->bIsVisible);
            Item->SetNumberField(TEXT("actors"), Level->Actors.Num());
            LevelRows.Add(MakeShared<FJsonValueObject>(Item));
        }
    Report->SetArrayField(TEXT("capture_levels"), LevelRows);
    Report->SetBoolField(TEXT("empty_scene"), Parts.Num() == 0);
    Report->SetStringField(TEXT("camera_policy"),
        TEXT(
            "v5: room bounds expanded with nearby floor-height geometry, landscape, then non-backdrop terrain footprint for overview, with full scene bounds retained in the receipt; player start +150cm, yaw retained, pitch -15. Room geometry centres horizontally and component floor-pivot heights vertically anchor cave cells; otherwise landscape components anchor terrain cells when present. Cave radius at most 2200cm. Missing start uses the first selected terrain cell."));
    Report->SetNumberField(TEXT("camera_room_components"), Rooms.Num());
    Report->SetArrayField(TEXT("camera_room_examples"), RoomExamples);
    if (Rooms.Num())
        Terrain = Rooms;
    else if (LandscapePoints.Num())
        Terrain = LandscapePoints;
    if (Terrain.Num() == 0)
        for (UPrimitiveComponent* Part : Parts)
            Terrain.Add(Part->Bounds.Origin);
    const FVector Center = Bounds.GetCenter(), Span = Bounds.GetSize();
    const FBox FloorBounds = ExpandFloorFootprint(RoomBounds, Rooms, Parts, Report);
    const FBox OverviewBounds = FloorBounds.IsValid ? FloorBounds
        : LandscapeBounds.IsValid                   ? LandscapeBounds
        : TerrainGeometryBounds.IsValid             ? TerrainGeometryBounds
                                                    : Bounds;
    const FVector OverviewCenter = OverviewBounds.GetCenter(), OverviewSpan = OverviewBounds.GetSize();
    Report->SetStringField(TEXT("overview_bounds_min"), OverviewBounds.Min.ToString());
    Report->SetStringField(TEXT("overview_bounds_max"), OverviewBounds.Max.ToString());
    FBox TerrainBounds(ForceInit);
    for (const FVector& Point : Terrain)
        TerrainBounds += Point;
    if (!TerrainBounds.IsValid)
        TerrainBounds = Bounds;
    const FVector TerrainSpan = TerrainBounds.GetSize(), TerrainCenter = TerrainBounds.GetCenter();
    Report->SetStringField(TEXT("terrain_density_bounds_min"), TerrainBounds.Min.ToString());
    Report->SetStringField(TEXT("terrain_density_bounds_max"), TerrainBounds.Max.ToString());
    TMap<UPackage*, bool> DirtyBefore;
    for (ULevel* Level : World->GetLevels())
        if (Level)
            DirtyBefore.Add(Level->GetOutermost(), Level->GetOutermost()->IsDirty());
    TArray<AActor*> Preview;
    FActorSpawnParameters Spawn;
    Spawn.ObjectFlags |= RF_Transient;
    const auto Track = [&](AActor* Actor)
    {
        if (Actor)
            Preview.Add(Actor);
        return Actor;
    };
    ON_SCOPE_EXIT
    {
        for (AActor* Actor : Preview)
            if (IsValid(Actor))
                World->DestroyActor(Actor);
        FlushRenderingCommands();
        for (const auto& Package : DirtyBefore)
            Package.Key->SetDirtyFlag(Package.Value);
    };
    ASceneCapture2D* Camera = Cast<ASceneCapture2D>(Track(World->SpawnActor<ASceneCapture2D>(Spawn)));
    if (!Camera)
        return Fail(TEXT("Cannot create SceneCapture2D"));
    USceneCaptureComponent2D* Capture = Camera->GetCaptureComponent2D();
    UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(Camera);
    Target->ClearColor = FLinearColor::Black;
    Target->InitCustomFormat(1280, 720, PF_B8G8R8A8, false);
    Target->UpdateResourceImmediate(true);
    Capture->TextureTarget = Target;
    Capture->bCaptureEveryFrame = false;
    Capture->bCaptureOnMovement = false;
    Capture->CaptureSource = SCS_FinalColorLDR;
    Capture->FOVAngle = 70.f;
    // Match the editor's temporary visibility, including its source-qualified collision
    // helpers. Scene captures otherwise use game show flags and can draw hidden helpers.
    for (ULevel* Level : World->GetLevels())
        if (Level)
            for (AActor* Actor : Level->Actors)
                if (IsValid(Actor) && Actor->IsHiddenEd())
                    Capture->HiddenActors.Add(Actor);
    // Without eye adaptation UE 4.26 locks exposure to 1 and ignores every exposure setting (GetEyeAdaptationParameters),
    // as the game does with r.EyeAdaptationQuality=0.
    Capture->ShowFlags.SetEyeAdaptation(false);
    Capture->ShowFlags.SetMotionBlur(false);
    Capture->PostProcessBlendWeight = 1.f;
    const auto Pixels = [&](TArray<FColor>& OutPixels)
    {
        World->SendAllEndOfFrameUpdates();
        Capture->CaptureScene();
        FlushRenderingCommands();
        return Target->GameThread_GetRenderTargetResource()->ReadPixels(OutPixels) && OutPixels.Num() == 1280 * 720;
    };
    // Compile all pending map shaders, then prove that this capture actually draws a
    // newly compiled material rather than UE's grey default. No viewport screenshots.
    AStaticMeshActor* Probe = Cast<AStaticMeshActor>(Track(World->SpawnActor<AStaticMeshActor>(Spawn)));
    if (!Probe)
        return Fail(TEXT("Cannot create shader probe"));
    UMaterial* Red = NewObject<UMaterial>(GetTransientPackage());
    Red->SetShadingModel(MSM_Unlit);
    UMaterialExpressionConstant3Vector* Color = NewObject<UMaterialExpressionConstant3Vector>(Red);
    Color->Constant = FLinearColor(1, 0, 0);
    Red->Expressions.Add(Color);
    Red->EmissiveColor.Expression = Color;
    Red->PostEditChange();
    Probe->SetActorLocation(FVector(Center.X, Center.Y, Bounds.Min.Z - 10000.f));
    Probe->GetStaticMeshComponent()->SetStaticMesh(
        LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
    Probe->GetStaticMeshComponent()->SetMaterial(0, Red);
    Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
    Capture->ShowOnlyActorComponents(Probe);
    // Probe the linear scene before the film tonemapper can desaturate bright red.
    Capture->CaptureSource = SCS_SceneColorHDR;
    Camera->SetActorLocation(Probe->GetActorLocation() + FVector(-200, 0, 0));
    Camera->SetActorRotation(FRotator::ZeroRotator);
    if (GShaderCompilingManager)
        GShaderCompilingManager->FinishAllCompilation();
    FlushRenderingCommands();
    TArray<FColor> Read;
    bool Ready = false;
    for (int32 Attempt = 0; Attempt < 12 && !Ready; ++Attempt)
    {
        if (!Pixels(Read))
            return Fail(TEXT("Shader probe render-target readback failed"));
        const FColor Pixel = Read[360 * 1280 + 640];
        Report->SetStringField(TEXT("shader_probe_pixel"), Pixel.ToString());
        Ready = Pixel.R > 96 && Pixel.G < 40 && Pixel.B < 40;
        if (!Ready && GShaderCompilingManager)
            GShaderCompilingManager->ProcessAsyncResults(true, false);
    }
    if (!Ready)
        return Fail(
            TEXT("Red unlit shader probe never rendered red: ") + Report->GetStringField(TEXT("shader_probe_pixel")));
    Report->SetBoolField(TEXT("shader_probe_ready"), true);
    Report->SetNumberField(TEXT("capture_shader_ready_seconds"), FPlatformTime::Seconds() - Began);
    TSet<UMaterialInterface*> Materials;
    int32 MissingSlots = 0;
    TArray<TSharedPtr<FJsonValue>> MissingExamples;
    for (UPrimitiveComponent* Part : Parts)
    {
        TArray<UMaterialInterface*> Used;
        if (const auto* Landscape = Cast<ULandscapeComponent>(Part))
        {
            // GetUsedMaterials also appends seven optional editor debug globals,
            // even when bGetDebugMaterials is false. Audit the scene proxy's LOD
            // inputs instead; an unassigned required material still counts.
            TArray<UMaterialInterface*> Available;
            if (World->FeatureLevel < ERHIFeatureLevel::SM5)
                Available.Append(Landscape->MobileMaterialInterfaces);
            else if (Landscape->GetLandscapeProxy() && Landscape->GetLandscapeProxy()->bUseDynamicMaterialInstance)
                for (auto* Material : Landscape->MaterialInstancesDynamic)
                    Available.Add(Material);
            else
                for (auto* Material : Landscape->MaterialInstances)
                    Available.Add(Material);
            TSet<int32> Indices;
            for (int8 Index : Landscape->LODIndexToMaterialIndex)
                Indices.Add(Index);
            if (!Indices.Num())
                Used.Add(nullptr);
            for (int32 Index : Indices)
                Used.Add(Available.IsValidIndex(Index) ? Available[Index] : nullptr);
        }
        else
            Part->GetUsedMaterials(Used);
        for (UMaterialInterface* Material : Used)
            if (Material)
                Materials.Add(Material);
            else
            {
                ++MissingSlots;
                if (MissingExamples.Num() < 12)
                {
                    const UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Part);
                    MissingExamples.Add(MakeShared<FJsonValueString>(Part->GetPathName() + TEXT(" mesh=") +
                        (Mesh && Mesh->GetStaticMesh() ? Mesh->GetStaticMesh()->GetPathName() : FString())));
                }
            }
    }
    // Resource queries can expose a late static permutation after the probe's
    // compilation barrier. Finish that pending work before the scene audit.
    for (UMaterialInterface* Material : Materials)
        Material->GetMaterialResource(GMaxRHIFeatureLevel);
    if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling())
    {
        GShaderCompilingManager->FinishAllCompilation();
        FlushRenderingCommands();
    }
    TArray<TSharedPtr<FJsonValue>> ShaderProblems, ShaderDetails;
    for (UMaterialInterface* Material : Materials)
    {
        FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIFeatureLevel);
        if (!Resource || !Resource->HasValidGameThreadShaderMap() || Resource->GetCompileErrors().Num())
        {
            ShaderProblems.Add(MakeShared<FJsonValueString>(Material->GetPathName()));
            if (ShaderDetails.Num() < 16)
            {
                auto Detail = MakeShared<FJsonObject>();
                Detail->SetStringField(TEXT("material"), Material->GetPathName());
                Detail->SetBoolField(TEXT("resource_available"), Resource != nullptr);
                Detail->SetBoolField(TEXT("valid_shader_map"), Resource && Resource->HasValidGameThreadShaderMap());
                TArray<TSharedPtr<FJsonValue>> Errors;
                if (Resource)
                    for (const FString& Error : Resource->GetCompileErrors())
                        if (Errors.Num() < 4)
                            Errors.Add(MakeShared<FJsonValueString>(Error.Left(2048)));
                Detail->SetArrayField(TEXT("compile_errors"), Errors);
                ShaderDetails.Add(MakeShared<FJsonValueObject>(Detail));
            }
        }
    }
    Report->SetNumberField(TEXT("capture_missing_material_slots"), MissingSlots);
    Report->SetArrayField(TEXT("capture_missing_material_slot_examples"), MissingExamples);
    Report->SetArrayField(TEXT("capture_materials_without_shaders"), ShaderProblems);
    Report->SetArrayField(TEXT("capture_material_shader_details"), ShaderDetails);
    Capture->ShowOnlyActors.Empty();
    Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
    Capture->CaptureSource = SCS_FinalColorLDR;
    World->DestroyActor(Probe);
    Preview.Remove(Probe);
    // Fixed, shadowless preview lights compensate for captures without adaptation. Per-pixel translucent surfaces
    // (TLM_SurfacePerPixelLighting) get one directional light only, the last one the renderer sorts
    // (LightGridInjection.cpp), so the map's own directional lights are out of the captures and the key light comes
    // last. A map's environment sun otherwise lit its water alone, with the value the game replaces at runtime.
    TArray<UDirectionalLightComponent*> MapLights;
    // Diagnostic: the map's own lights alone, without preview lights.
    const bool bMapLights = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_MAP_LIGHTS")) == TEXT("1");
    for (ULevel* Level : bMapLights ? TArray<ULevel*>() : World->GetLevels())
        if (Level)
            for (AActor* Actor : Level->Actors)
                if (IsValid(Actor))
                {
                    TInlineComponentArray<UDirectionalLightComponent*> Lights(Actor);
                    for (UDirectionalLightComponent* Light : Lights)
                        if (Light->IsVisible())
                        {
                            Light->SetVisibility(false);
                            MapLights.Add(Light);
                        }
                }
    ON_SCOPE_EXIT
    {
        for (UDirectionalLightComponent* Light : MapLights)
            if (IsValid(Light))
                Light->SetVisibility(true);
        FlushRenderingCommands();
    };
    for (int32 Index = 0; Index < (bMapLights ? 0 : 2); ++Index)
    {
        ADirectionalLight* Light = Cast<ADirectionalLight>(Track(World->SpawnActor<ADirectionalLight>(Spawn)));
        if (!Light)
            return Fail(TEXT("Cannot create preview light"));
        Light->SetActorRotation(FRotator(-55.f, Index ? -35.f : 145.f, 0));
        UDirectionalLightComponent* Component = CastChecked<UDirectionalLightComponent>(Light->GetLightComponent());
        Component->SetMobility(EComponentMobility::Movable);
        Component->SetIntensity(Index ? 8.f : 2.f);
        Component->SetCastShadows(false);
    }
    // The map's sky lights and reflection captures, captured once its shaders and lights are in: before this the water
    // and every other reflective surface reflected an empty capture.
    USkyLightComponent::UpdateSkyCaptureContents(World);
    UReflectionCaptureComponent::UpdateReflectionCaptureContents(World);
    FlushRenderingCommands();
    Report->SetNumberField(TEXT("preview_hidden_map_directional_lights"), MapLights.Num());
    Report->SetStringField(TEXT("preview_lighting"), bMapLights
            ? TEXT("the map's own lights, exposure 1 (eye adaptation off); diagnostic editor preview")
            : TEXT("shadowless directional 8+2 (map directional lights hidden), map reflections captured, exposure 1 (eye adaptation off); diagnostic editor preview"));
    IStreamingManager::Get().StreamAllResources(10.f);
    // A background SceneCapture does not imply that the editor viewport asked
    // for the same texture mips. Keep residency evidence beside visual results.
    TSet<UTexture2D*> Textures;
    for (UMaterialInterface* Material : Materials)
    {
        TArray<UTexture*> Used;
        Material->GetUsedTextures(Used, EMaterialQualityLevel::Num, true, GMaxRHIFeatureLevel, true);
        for (UTexture* Texture : Used)
            if (auto* Image = Cast<UTexture2D>(Texture))
                Textures.Add(Image);
    }
    int32 UnderResident = 0;
    TArray<TSharedPtr<FJsonValue>> TextureExamples;
    for (UTexture2D* Texture : Textures)
        if (Texture->GetNumResidentMips() < Texture->GetNumMips())
        {
            ++UnderResident;
            if (TextureExamples.Num() < 12)
            {
                auto Item = MakeShared<FJsonObject>();
                Item->SetStringField(TEXT("texture"), Texture->GetPathName());
                Item->SetNumberField(TEXT("platform_mips"), Texture->GetNumMips());
                Item->SetNumberField(TEXT("resident_mips"), Texture->GetNumResidentMips());
                Item->SetNumberField(TEXT("source_width"), Texture->Source.GetSizeX());
                Item->SetNumberField(TEXT("platform_width"), Texture->GetSizeX());
                Item->SetNumberField(TEXT("lod_bias"), Texture->LODBias);
                TextureExamples.Add(MakeShared<FJsonValueObject>(Item));
            }
        }
    Report->SetNumberField(TEXT("capture_texture_count"), Textures.Num());
    Report->SetNumberField(TEXT("capture_textures_below_full_residency"), UnderResident);
    Report->SetArrayField(TEXT("capture_texture_residency_examples"), TextureExamples);
    struct FView
    {
        FString Name;
        FVector Eye, Aim;
        bool Ortho = false;
        float Fov = 70.f;
        bool ExactRotation = false;
        FRotator Rotation = FRotator::ZeroRotator;
    };
    TArray<FView> Views;
    // Orthographic width does not require a distant camera. Keeping the eye
    // near this footprint also avoids ordinary game distance/LOD culling.
    const float OverviewHeight = OverviewBounds.Max.Z +
        FMath::Max(1000.f, FMath::Max(OverviewSpan.X, OverviewSpan.Y) * .2f);
    Views.Add({TEXT("overview"), FVector(OverviewCenter.X, OverviewCenter.Y, OverviewHeight), OverviewCenter, true});
    const FVector Eye = Start ? Start->GetActorLocation() + FVector(0, 0, 150) : TerrainCenter + FVector(0, 0, 300);
    Views.Add({TEXT("player-start"), Eye,
        Eye + FRotator(-15.f, Start ? Start->GetActorRotation().Yaw : 45.f, 0).Vector() * 1000.f});
    Report->SetStringField(
        TEXT("player_start"), Start ? Start->GetPathName() : TEXT("unavailable; terrain-center fallback"));
    struct FCell
    {
        int32 Count = 0;
        FVector Sum = FVector::ZeroVector;
    };
    FCell Cells[64];
    for (const FVector& Point : Terrain)
    {
        const int32 X = FMath::Clamp(int32((Point.X - TerrainBounds.Min.X) / FMath::Max(TerrainSpan.X, 1.f) * 8), 0, 7);
        const int32 Y = FMath::Clamp(int32((Point.Y - TerrainBounds.Min.Y) / FMath::Max(TerrainSpan.Y, 1.f) * 8), 0, 7);
        ++Cells[Y * 8 + X].Count;
        Cells[Y * 8 + X].Sum += Point;
    }
    int32 First = -1;
    for (int32 View = 0; View < 2; ++View)
    {
        int32 Best = -1;
        for (int32 Cell = 0; Cell < 64; ++Cell)
        {
            if (View && First >= 0 && FMath::Abs(Cell % 8 - First % 8) + FMath::Abs(Cell / 8 - First / 8) < 3)
                continue;
            if (Best < 0 || Cells[Cell].Count > Cells[Best].Count)
                Best = Cell;
        }
        if (!View)
            First = Best;
        const FVector Aim = Best >= 0 && Cells[Best].Count ? Cells[Best].Sum / Cells[Best].Count : TerrainCenter;
        if (!View && !Start)
        {
            Views[1].Eye = Aim + FVector(0, 0, 150);
            Views[1].Aim = Views[1].Eye + FRotator(-15.f, 45.f, 0).Vector() * 1000.f;
            Report->SetStringField(TEXT("player_start"), TEXT("unavailable; selected terrain-cell fallback"));
        }
        const float Radius = FMath::Clamp(
            FMath::Max(TerrainSpan.X, TerrainSpan.Y) / 8.f * 1.8f, 800.f, Rooms.Num() ? 2200.f : 12000.f);
        Views.Add({FString::Printf(TEXT("terrain-%d"), View + 1),
            Aim + FVector(View ? .75f : -.75f, -.75f, .7f) * Radius, Aim});
    }
    // A matching game screenshot supplies an observed camera, not a guessed
    // orbit around the editor's terrain center. Keep the same four-view contract.
    const FString CameraFile = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_GAME_CAMERAS"));
    if (!CameraFile.IsEmpty())
    {
        FString Text;
        TSharedPtr<FJsonObject> Manifest;
        const TSharedPtr<FJsonObject>* Maps = nullptr;
        FString Format;
        if (IFileManager::Get().FileSize(*CameraFile) > 65536 || !FFileHelper::LoadFileToString(Text, *CameraFile) ||
            !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Manifest) || !Manifest.IsValid() ||
            !Manifest->TryGetStringField(TEXT("format"), Format) || Format != TEXT("kadan-measured-game-cameras-v1") ||
            !Manifest->TryGetObjectField(TEXT("maps"), Maps))
            return Fail(TEXT("Measured game camera manifest is unreadable or malformed"));
        const FString Map = World->GetOutermost()->GetName();
        const TArray<TSharedPtr<FJsonValue>>* Matches = nullptr;
        if ((*Maps)->HasField(Map) && !(*Maps)->TryGetArrayField(Map, Matches))
            return Fail(TEXT("Measured game camera map entry must be an array"));
        if (Matches)
        {
            if (Matches->Num() > 3)
                return Fail(TEXT("At most three measured game cameras may replace perspective views"));
            const auto Vector = [&](const TSharedPtr<FJsonObject>& Item, const TCHAR* Key, FVector& OutVector)
            {
                const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
                double Numbers[3];
                if (!Item->TryGetArrayField(Key, Values) || Values->Num() != 3)
                    return false;
                for (int32 N = 0; N < 3; ++N)
                    if (!(*Values)[N]->TryGetNumber(Numbers[N]) || !FMath::IsFinite(Numbers[N]) ||
                        FMath::Abs(Numbers[N]) > 1.e8)
                        return false;
                OutVector = FVector(Numbers[0], Numbers[1], Numbers[2]);
                return true;
            };
            TSet<FString> Used;
            for (const auto& Match : *Matches)
            {
                const auto Item = Match->Type == EJson::Object ? Match->AsObject() : nullptr;
                FString Name, Source;
                FVector EyePoint, Angles;
                double Fov = 0;
                if (!Item.IsValid() || !Item->TryGetStringField(TEXT("view"), Name) || Name == TEXT("overview") ||
                    Used.Contains(Name) || !Item->TryGetStringField(TEXT("source"), Source) || Source.IsEmpty() ||
                    !Vector(Item, TEXT("eye"), EyePoint) || !Vector(Item, TEXT("rotation"), Angles) ||
                    !Item->TryGetNumberField(TEXT("fov"), Fov) || !FMath::IsFinite(Fov) || Fov <= 1 || Fov >= 170)
                    return Fail(TEXT(
                        "Measured game camera requires a unique perspective view, source, finite pose and field of view"));
                FView* View = Views.FindByPredicate([&](const FView& Candidate) { return Candidate.Name == Name; });
                if (!View)
                    return Fail(TEXT("Measured game camera names an unknown view"));
                Used.Add(Name);
                View->Eye = EyePoint;
                View->Rotation = FRotator(Angles.X, Angles.Y, Angles.Z);
                View->ExactRotation = true;
                View->Aim = EyePoint + View->Rotation.Vector() * 1000.f;
                View->Fov = float(Fov);
                if (Name == TEXT("player-start"))
                    Report->SetStringField(TEXT("player_start"), TEXT("Measured game entry camera: ") + Source);
            }
            Report->SetArrayField(TEXT("measured_game_cameras"), *Matches);
            Report->SetStringField(TEXT("camera_policy"),
                Report->GetStringField(TEXT("camera_policy")) +
                    TEXT(
                        " Perspective overrides use measured game camera poses and FOV; sources are recorded in measured_game_cameras."));
        }
    }
    TArray<TSharedPtr<FJsonValue>> Images;
    IImageWrapperModule& Wrapper = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    for (const FView& View : Views)
    {
        Camera->SetActorLocation(View.Eye);
        Camera->SetActorRotation(View.ExactRotation ? View.Rotation
                : View.Ortho                        ? FRotator(-90, -90, 0)
                                                    : (View.Aim - View.Eye).Rotation());
        Capture->FOVAngle = View.Fov;
        Capture->ProjectionType = View.Ortho ? ECameraProjectionMode::Orthographic : ECameraProjectionMode::Perspective;
        Capture->OrthoWidth = FMath::Max(1000.f, FMath::Max(OverviewSpan.X, OverviewSpan.Y * 1280.f / 720.f) * 1.08f);
        // SceneCapture does not publish the background editor's camera to
        // texture streaming. Request the mips for this actual view, with normal
        // distance/LOD decisions, before reading its render target.
        const double StreamingBegan = FPlatformTime::Seconds();
        const float StreamingScreen = 1280.f / FMath::Tan(FMath::DegreesToRadians(View.Fov * .5f));
        IStreamingManager::Get().AddViewInformation(View.Eye, 1280.f, StreamingScreen, 1.f, true);
        IStreamingManager::Get().Tick(0.f, true);
        const int32 PendingStreaming = IStreamingManager::Get().StreamAllResources(10.f);
        int32 ResidentMips = 0;
        for (UTexture2D* Texture : Textures)
            ResidentMips += Texture->GetNumResidentMips();
        if (!Pixels(Read))
            return Fail(TEXT("SceneCapture2D readback failed"));
        int32 Lit = 0;
        for (FColor& Pixel : Read)
        {
            Lit += FMath::Max3(Pixel.R, Pixel.G, Pixel.B) > 24;
            Pixel.A = 255;
        }
        TSharedPtr<IImageWrapper> JPEG = Wrapper.CreateImageWrapper(EImageFormat::JPEG);
        if (!JPEG->SetRaw(Read.GetData(), Read.Num() * sizeof(FColor), 1280, 720, ERGBFormat::BGRA, 8) ||
            !FFileHelper::SaveArrayToFile(JPEG->GetCompressed(78), *(Out / (View.Name + TEXT(".jpg")))))
            return Fail(TEXT("Cannot save capture JPEG"));
        auto Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("image"), View.Name + TEXT(".jpg"));
        Item->SetStringField(TEXT("camera"), View.Eye.ToString());
        Item->SetStringField(TEXT("aim"), View.Aim.ToString());
        Item->SetStringField(TEXT("rotation"), Camera->GetActorRotation().ToString());
        Item->SetNumberField(TEXT("fov"), Capture->FOVAngle);
        Item->SetNumberField(TEXT("texture_streaming_seconds"), FPlatformTime::Seconds() - StreamingBegan);
        Item->SetNumberField(TEXT("texture_streaming_pending"), PendingStreaming);
        Item->SetNumberField(TEXT("texture_resident_mips"), ResidentMips);
        if (View.Ortho)
            Item->SetNumberField(TEXT("ortho_width"), Capture->OrthoWidth);
        Item->SetNumberField(TEXT("width"), 1280);
        Item->SetNumberField(TEXT("height"), 720);
        Item->SetNumberField(TEXT("lit_pixels"), Lit);
        Images.Add(MakeShared<FJsonValueObject>(Item));
    }
    Report->SetArrayField(TEXT("captures"), Images);
    Report->SetBoolField(TEXT("capture_verified"), Images.Num() == 4);
    Report->SetNumberField(TEXT("capture_seconds"), FPlatformTime::Seconds() - Began);
    FString Text;
    FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Text));
    if (!FFileHelper::SaveStringToFile(
            Text, *(Out / TEXT("capture.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        return Fail(TEXT("Cannot save capture receipt"));
    return true;
}
} //namespace P4SweepCapture

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SweepCaptureControl, "P4MapKit.Editor.SweepRenderTargetCapture",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4SweepCaptureControl::RunTest(const FString&)
{
    UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
    AStaticMeshActor* Floor = World->SpawnActor<AStaticMeshActor>();
    Floor->GetStaticMeshComponent()->SetStaticMesh(
        LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
    Floor->SetActorScale3D(FVector(30, 30, 1));
    APlayerStart* Start = World->SpawnActor<APlayerStart>(FVector(0, 0, 150), FRotator::ZeroRotator);
    Start->SetIsTemporarilyHiddenInEditor(true);
    World->GetOutermost()->SetDirtyFlag(false);
    auto Report = MakeShared<FJsonObject>();
    const bool Ok = P4SweepCapture::Capture(World, FPaths::ProjectSavedDir() / TEXT("P4MapKit/CaptureControl"), Report);
    TestTrue(TEXT("Four JPEG render-target views with a red shader-ready probe"),
        Ok && Report->GetBoolField(TEXT("capture_verified")));
    if (Ok)
    {
        const auto& Images = Report->GetArrayField(TEXT("captures"));
        TestEqual(TEXT("Exactly four prescribed views"), Images.Num(), 4);
        TestEqual(TEXT("Hidden player-start markers still set the player view"),
            Report->GetStringField(TEXT("player_start")), Start->GetPathName());
        TestTrue(TEXT("Overview visibly draws the control floor"),
            Images[0]->AsObject()->GetNumberField(TEXT("lit_pixels")) > 1280 * 720 / 4);
    }
    if (!Ok)
        AddError(Report->GetStringField(TEXT("capture_error")));
    if (Ok)
    {
        const FString Previous = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_GAME_CAMERAS"));
        const FString File = FPaths::ProjectSavedDir() / TEXT("P4MapKit/CaptureControl/measured-cameras.json");
        ON_SCOPE_EXIT
        {
            FPlatformMisc::SetEnvironmentVar(TEXT("P4_NATIVE_SWEEP_GAME_CAMERAS"), *Previous);
            IFileManager::Get().Delete(*File);
        };
        auto Manifest = MakeShared<FJsonObject>();
        auto Maps = MakeShared<FJsonObject>();
        auto View = MakeShared<FJsonObject>();
        Manifest->SetStringField(TEXT("format"), TEXT("kadan-measured-game-cameras-v1"));
        Manifest->SetObjectField(TEXT("maps"), Maps);
        View->SetStringField(TEXT("view"), TEXT("terrain-1"));
        View->SetStringField(TEXT("source"), TEXT("synthetic measured-camera control"));
        View->SetArrayField(TEXT("eye"),
            {MakeShared<FJsonValueNumber>(650), MakeShared<FJsonValueNumber>(200), MakeShared<FJsonValueNumber>(400)});
        View->SetArrayField(TEXT("rotation"),
            {MakeShared<FJsonValueNumber>(-25), MakeShared<FJsonValueNumber>(150), MakeShared<FJsonValueNumber>(3)});
        View->SetNumberField(TEXT("fov"), 61.25);
        Maps->SetArrayField(World->GetOutermost()->GetName(), {MakeShared<FJsonValueObject>(View)});
        FString Json;
        FJsonSerializer::Serialize(Manifest, TJsonWriterFactory<>::Create(&Json));
        TestTrue(TEXT("Measured camera control manifest writes"),
            FFileHelper::SaveStringToFile(Json, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
        FPlatformMisc::SetEnvironmentVar(TEXT("P4_NATIVE_SWEEP_GAME_CAMERAS"), *File);
        auto Matched = MakeShared<FJsonObject>();
        const bool Captured = P4SweepCapture::Capture(
            World, FPaths::ProjectSavedDir() / TEXT("P4MapKit/CaptureControl"), Matched);
        TestTrue(TEXT("Observed camera keeps all four render-target captures"),
            Captured && Matched->GetArrayField(TEXT("captures")).Num() == 4);
        if (Captured)
        {
            const auto Third = Matched->GetArrayField(TEXT("captures"))[2]->AsObject();
            TestEqual(TEXT("Observed eye is used exactly"), Third->GetStringField(TEXT("camera")),
                FVector(650, 200, 400).ToString());
            FRotator Observed;
            const bool Read = Observed.InitFromString(Third->GetStringField(TEXT("rotation")));
            TestTrue(TEXT("Observed rotation includes roll within native quaternion precision"),
                Read && Observed.Equals(FRotator(-25, 150, 3), .0001f));
            TestEqual(TEXT("Observed horizontal FOV is used exactly"), Third->GetNumberField(TEXT("fov")), 61.25);
        }
        else
            AddError(Matched->GetStringField(TEXT("capture_error")));
        View->SetNumberField(TEXT("fov"), 0);
        Json.Empty();
        FJsonSerializer::Serialize(Manifest, TJsonWriterFactory<>::Create(&Json));
        FFileHelper::SaveStringToFile(Json, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        auto Invalid = MakeShared<FJsonObject>();
        TestFalse(TEXT("Invalid measured camera refuses without claiming a picture"),
            P4SweepCapture::Capture(World, FPaths::ProjectSavedDir() / TEXT("P4MapKit/CaptureControl"), Invalid));
        TestFalse(TEXT("Invalid measured camera is not verified"), Invalid->GetBoolField(TEXT("capture_verified")));
    }
    TestTrue(TEXT("Capture leaves original geometry alive"),
        IsValid(Floor) && Floor->GetStaticMeshComponent()->GetStaticMesh() != nullptr);
    TestFalse(TEXT("Preview actors do not dirty the retained map"), World->GetOutermost()->IsDirty());
    UEditorLoadingAndSavingUtils::NewBlankMap(false);
    return Ok;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4CaveRoomCaptureControl, "P4MapKit.Editor.CaveRoomCapture",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4CaveRoomCaptureControl::RunTest(const FString&)
{
    const FString Map = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_CAPTURE_CAVE"));
    if (!Map.EndsWith(TEXT("/Cave006_F00_P")))
    {
        AddWarning(TEXT("P4MapKit test not run. Select the owned Cave006 room-capture fixture."));
        return true;
    }
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(Map);
    if (!TestNotNull(TEXT("The actual iron cave opens"), World))
        return false;
    ON_SCOPE_EXIT
    {
        UEditorLoadingAndSavingUtils::NewBlankMap(false);
    };
    auto Report = MakeShared<FJsonObject>();
    if (!TestTrue(TEXT("The actual cave produces all four render-target pictures"),
            P4SweepCapture::Capture(World, FPaths::ProjectSavedDir() / TEXT("P4MapKit/CaveCaptureControl"), Report)))
        return false;
    const auto Overview = Report->GetArrayField(TEXT("captures"))[0]->AsObject();
    TestTrue(TEXT("The iron cave overview visibly frames the playable floor rather than the far scaffold"),
        Overview->GetNumberField(TEXT("lit_pixels")) > 1280 * 720 / 8);
    TestTrue(TEXT("The room overview uses a readable footprint instead of the entire workshop backdrop"),
        Overview->GetNumberField(TEXT("ortho_width")) < 20000);
    int32 NearViews = 0;
    for (const auto& Entry : Report->GetArrayField(TEXT("captures")))
    {
        const auto Item = Entry->AsObject();
        if (!Item->GetStringField(TEXT("image")).StartsWith(TEXT("terrain-")))
            continue;
        FVector Eye, Aim;
        if (!TestTrue(TEXT("The measured cave camera receipt is parseable"),
                Eye.InitFromString(Item->GetStringField(TEXT("camera"))) &&
                    Aim.InitFromString(Item->GetStringField(TEXT("aim")))))
            return false;
        TestTrue(TEXT("A cave terrain view stays near the playable room instead of distant girders"),
            FVector::Distance(Eye, Aim) < 3000.f);
        ++NearViews;
    }
    TestEqual(TEXT("Both cave terrain perspectives are checked"), NearViews, 2);
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4LandscapeMaterialCaptureControl, "P4MapKit.Editor.LandscapeCaptureMaterialAudit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4LandscapeMaterialCaptureControl::RunTest(const FString&)
{
    const FString Map = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_CAPTURE_LANDSCAPE"));
    if (!Map.EndsWith(TEXT("/Cave011_F00_P")))
    {
        AddWarning(TEXT("P4MapKit test not run. Select the owned actual Cave011 landscape fixture."));
        return true;
    }
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(Map);
    if (!TestNotNull(TEXT("The actual landscape map opens"), World))
        return false;
    ON_SCOPE_EXIT
    {
        UEditorLoadingAndSavingUtils::NewBlankMap(false);
    };
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("P4MapKit/LandscapeCaptureAudit");
    auto Report = MakeShared<FJsonObject>();
    if (!TestTrue(TEXT("The actual terrain and its objects render"), P4SweepCapture::Capture(World, Folder, Report)))
        return false;
    TestEqual(TEXT("Optional null landscape editor debug materials are not missing scene materials"),
        Report->GetNumberField(TEXT("capture_missing_material_slots")), 0.0);
    AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>();
    if (!TestNotNull(TEXT("The required-material negative control is placed"), Actor))
        return false;
    ON_SCOPE_EXIT
    {
        if (IsValid(Actor))
            World->DestroyActor(Actor);
    };
    UStaticMesh* Source = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    UStaticMesh* Mesh = Source ? DuplicateObject<UStaticMesh>(Source, GetTransientPackage()) : nullptr;
    if (!TestTrue(TEXT("A transient real cube has a required material slot"), Mesh && Mesh->StaticMaterials.Num() > 0))
        return false;
    Mesh->StaticMaterials[0].MaterialInterface = nullptr;
    Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
    Actor->SetActorLocation(FVector(0, 0, 100));
    auto Missing = MakeShared<FJsonObject>();
    if (!TestTrue(TEXT("The required-material control is captured"), P4SweepCapture::Capture(World, Folder, Missing)))
        return false;
    TestTrue(TEXT("A real unassigned mesh material still fails the scene audit"),
        Missing->GetNumberField(TEXT("capture_missing_material_slots")) > 0);
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4NormalSurfaceOpenControl, "P4MapKit.Editor.NormalSurfaceMapOpen",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4NormalSurfaceOpenControl::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SURFACE_OPEN")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. Select the owned original Area002 map."));
        return true;
    }
    const FString Map = TEXT("/Game/Carrot4/Maps/Main/Area/Area002/Area002_P");
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(Map);
    if (!TestNotNull(TEXT("The ordinary editor loader opens the actual surface map"), World))
        return false;
    ON_SCOPE_EXIT
    {
        UEditorLoadingAndSavingUtils::NewBlankMap(false);
    };
    // No explicit presentation helper here: the normal OnMapOpened path must
    // already expose terrain, plants, foliage and objects for a user opening it.
    World->FlushLevelStreaming(EFlushLevelStreamingType::Full);
    TSet<FName> Visible;
    int32 Terrain = 0, PlantMeshes = 0, Instances = 0;
    for (ULevel* Level : World->GetLevels())
        if (Level && Level->bIsVisible)
        {
            const FString Package = Level->GetOutermost()->GetName();
            Visible.Add(FName(*Package));
            for (AActor* Actor : Level->Actors)
                if (IsValid(Actor))
                {
                    TInlineComponentArray<UPrimitiveComponent*> Components;
                    Actor->GetComponents(Components);
                    for (UPrimitiveComponent* Part : Components)
                        if (Part->IsRegistered() && Part->IsVisible())
                        {
                            Terrain += Part->IsA<ULandscapeComponent>();
                            // Area002's playable terrain is authored as static meshes, not
                            // UE Landscape components. Count the actual original terrain
                            // assets while excluding the distant backdrop and collision.
                            if (const auto* Mesh = Cast<UStaticMeshComponent>(Part))
                                if (Mesh->GetStaticMesh())
                                {
                                    const FString Path = Mesh->GetStaticMesh()->GetPathName();
                                    if (Path.Contains(TEXT("/Terrain/")) && !Path.Contains(TEXT("/Kakiwari/")) &&
                                        !Path.Contains(TEXT("FarGround")) &&
                                        !Mesh->GetStaticMesh()->GetName().StartsWith(TEXT("COL_")))
                                        ++Terrain;
                                }
                            if (Package.EndsWith(TEXT("_Plants")) && Part->IsA<UStaticMeshComponent>())
                                ++PlantMeshes;
                            if (auto* Mesh = Cast<UInstancedStaticMeshComponent>(Part))
                                Instances += Mesh->GetInstanceCount();
                        }
                }
        }
    const FString Prefix = TEXT("/Game/Carrot4/Maps/Main/Area/Area002/Sublevels/Area002_");
    for (const TCHAR* Layer : {TEXT("Foliage"), TEXT("Plants"), TEXT("Objects"), TEXT("Objects_Day")})
        TestTrue(TEXT("Normal map opening already shows the original layer: ") + FString(Layer),
            Visible.Contains(FName(*(Prefix + Layer))));
    TestTrue(TEXT("Actual terrain components are registered and visible after normal opening"), Terrain > 0);
    TestTrue(TEXT("Actual plant meshes are registered and visible after normal opening"), PlantMeshes > 0);
    auto Report = MakeShared<FJsonObject>();
    Report->SetStringField(TEXT("map"), Map);
    Report->SetStringField(TEXT("variant"), TEXT("day"));
    Report->SetNumberField(TEXT("normal_open_terrain_components"), Terrain);
    Report->SetNumberField(TEXT("normal_open_plant_meshes"), PlantMeshes);
    Report->SetNumberField(TEXT("normal_open_instanced_mesh_instances"), Instances);
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("P4MapKit/NormalSurfaceOpen");
    TestTrue(TEXT("Normal map opening produces real render-target views without test-only layer activation"),
        P4SweepCapture::Capture(World, Folder, Report));
    const TArray<TSharedPtr<FJsonValue>>* Images = nullptr;
    if (TestTrue(TEXT("The ordinary surface capture records its actual views"),
            Report->TryGetArrayField(TEXT("captures"), Images) && Images->Num() == 4))
        TestTrue(TEXT("The surface overview visibly frames the playable landscape rather than distant backdrop strips"),
            (*Images)[0]->AsObject()->GetNumberField(TEXT("lit_pixels")) > 1280 * 720 / 8);
    FString Json;
    FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
    FFileHelper::SaveStringToFile(
        Json, *(Folder / TEXT("normal-open.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    return !HasAnyErrors();
}
#endif

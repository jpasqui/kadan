#if WITH_DEV_AUTOMATION_TESTS
#include "P4OriginalRecord.h"
#include "P4MaterialGraph.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Editor.h"
#include "LevelEditorViewport.h"
#include "FileHelpers.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Components/MeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/LightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "MaterialShared.h"
#include "SceneViewExtension.h"
#include "SceneView.h"
#include "Engine/Texture2D.h"
#include "ContentStreaming.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "UObject/UnrealType.h"
#include "ShaderCompiler.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "ImageUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"

namespace P4CaveMaterialVisual
{
// Diagnostic-only final-view override. The imported PP/component is never written.
class FExposureDiagnostic final : public FSceneViewExtensionBase
{
public:
    FViewport* Viewport = nullptr;
    bool DisablePhysical = false;
    int32 Applied = 0;
    explicit FExposureDiagnostic(const FAutoRegister& Registration) : FSceneViewExtensionBase(Registration) {}
    void SetupViewFamily(FSceneViewFamily&) override {}
    void SetupView(FSceneViewFamily& Family, FSceneView& View) override
    {
        if (DisablePhysical && Family.RenderTarget == Viewport && Family.EngineShowFlags.Editor)
        {
            View.FinalPostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
            ++Applied;
        }
    }
    void BeginRenderViewFamily(FSceneViewFamily&) override {}
    void PreRenderViewFamily_RenderThread(FRHICommandListImmediate&, FSceneViewFamily&) override {}
    void PreRenderView_RenderThread(FRHICommandListImmediate&, FSceneView&) override {}
};
class FCapture final : public IAutomationLatentCommand
{
    struct FSource
    {
        TWeakObjectPtr<UWorld> World;
        FString Source, Baseline;
        bool Dirty;
    };
    struct FSourcePost
    {
        TWeakObjectPtr<UPostProcessComponent> Component;
        FString Settings;
    };
    struct FView
    {
        FString Name, Target;
        FVector Location;
        FRotator Rotation;
        EViewModeIndex Mode;
        bool NoPost = false;
        bool FixedExposure = false;
        bool NoPhysical = false;
        float FixedEV = 1.f;
        bool FullMips = false;
    };
    FAutomationTestBase& Test;
    FLevelEditorViewportClient* Client = nullptr;
    FVector OldLocation;
    FRotator OldRotation;
    EViewModeIndex OldMode = VMI_Lit;
    FEngineShowFlags OldFlags = FEngineShowFlags(ESFIM_Editor);
    FExposureSettings OldExposure;
    TSharedPtr<FExposureDiagnostic, ESPMode::ThreadSafe> Exposure;
    bool OldAxes = false, OldRealtime = false;
    const bool ExposureOnly;
    TArray<FSource> Sources;
    TArray<FSourcePost> SourcePost;
    TArray<FView> Views;
    TSet<UMaterialInterface*> Materials;
    TArray<UTexture2D*> GroundTextures;
    TArray<UMaterialInterface*> GroundMaterials;
    TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Images;
    FString Out;
    int32 Phase = 0;
    int32 SettlingDraws = 0;
    double Started = 0, Ready = 0;
    bool Live() const { return GEditor && Client && GEditor->GetLevelViewportClients().Contains(Client); }
    void Restore()
    {
        Exposure.Reset();
        if (!Live())
            return;
        Client->SetRealtime(OldRealtime);
        Client->SetViewMode(OldMode);
        Client->EngineShowFlags = OldFlags;
        Client->ExposureSettings = OldExposure;
        Client->bDrawAxes = OldAxes;
        Client->SetViewLocation(OldLocation);
        Client->SetViewRotation(OldRotation);
        Client->Invalidate();
        Client = nullptr;
    }
    bool Save(const FString& Error)
    {
        Report->SetStringField(TEXT("error"), Error);
        Report->SetArrayField(TEXT("images"), Images);
        FString Json;
        FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
        return FFileHelper::SaveStringToFile(
            Json, *(Out / TEXT("cave-native-visual.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }
    bool Fail(const FString& Error)
    {
        Test.AddError(Error);
        Save(Error);
        Restore();
        return true;
    }
    void Pose()
    {
        const FView& View = Views[Phase];
        Client->SetViewLocation(View.Location);
        Client->SetViewRotation(View.Rotation);
        Exposure->DisablePhysical = View.NoPhysical;
        Exposure->Applied = 0;
        Client->EngineShowFlags = OldFlags;
        Client->ExposureSettings = OldExposure;
        Client->SetViewMode(View.Mode);
        Client->EngineShowFlags.SetGrid(false);
        Client->EngineShowFlags.SetSelectionOutline(false);
        Client->EngineShowFlags.SetModeWidgets(false);
        Client->bDrawAxes = false;
        Client->Invalidate();
        if (View.NoPost)
            Client->EngineShowFlags.SetPostProcessing(false);
        if (View.FixedExposure)
        {
            Client->ExposureSettings.bFixed = true;
            Client->ExposureSettings.FixedEV100 = View.FixedEV;
        }
        // A non-realtime editor viewport does not render while the latent test
        // waits. Give each camera/exposure change real frames to settle temporal
        // history and pre-exposure before reading its pixels.
        if (View.FullMips)
        {
            for (UTexture2D* Texture : GroundTextures)
                Texture->SetForceMipLevelsToBeResident(60.f);
            IStreamingManager::Get().StreamAllResources(5.f);
        }
        SettlingDraws = 8;
    }

public:
    explicit FCapture(FAutomationTestBase& InTest, bool InExposureOnly = false)
        : Test(InTest), ExposureOnly(InExposureOnly)
    {}
    ~FCapture() override { Restore(); }
    bool Setup(const FString& Map)
    {
        if (!GEditor || GUsingNullRHI)
        {
            Test.AddError(TEXT("Actual Cave material capture requires D3D and the real level viewport"));
            return false;
        }
        UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(Map);
        if (!World)
        {
            Test.AddError(TEXT("Retained owned Cave map cannot load"));
            return false;
        }
        World->FlushLevelStreaming(EFlushLevelStreamingType::Full);
        for (FLevelEditorViewportClient* View : GEditor->GetLevelViewportClients())
            if (View && View->GetWorld() == World && View->IsPerspective() && View->Viewport &&
                View->Viewport->GetSizeXY().X >= 320)
            {
                Client = View;
                break;
            }
        if (!Client)
        {
            Test.AddError(TEXT("No actual perspective editor viewport is available"));
            return false;
        }
        OldLocation = Client->GetViewLocation();
        OldRotation = Client->GetViewRotation();
        OldMode = Client->GetViewMode();
        OldFlags = Client->EngineShowFlags;
        OldExposure = Client->ExposureSettings;
        OldAxes = Client->bDrawAxes;
        OldRealtime = Client->IsRealtime();
        Client->SetRealtime(true);
        Exposure = FSceneViewExtensions::NewExtension<FExposureDiagnostic>();
        Exposure->Viewport = Client->Viewport;
        TArray<TSharedPtr<FJsonValue>> Lights, PostProcesses;
        UStaticMeshComponent* Room = nullptr;
        UStaticMeshComponent* Mushroom = nullptr;
        for (ULevel* Level : World->GetLevels())
            if (Level && Level->bIsVisible)
            {
                UWorld* Layer = Cast<UWorld>(Level->GetOuter());
                FP4OriginalRecord Record;
                FString Error;
                if (!P4ReadOriginalRecord(Layer, Record, Error) || Record.Kind != TEXT("level"))
                    continue;
                Sources.Add({Layer, Record.SourceDigest, Record.ComparisonDigest, Layer->GetOutermost()->IsDirty()});
                for (AActor* Actor : Level->Actors)
                    if (IsValid(Actor) && !Actor->IsHiddenEd())
                    {
                        TInlineComponentArray<ULightComponent*> ActorLights(Actor);
                        for (ULightComponent* Light : ActorLights)
                            if (Light)
                            {
                                auto Entry = MakeShared<FJsonObject>();
                                Entry->SetStringField(TEXT("component"), Light->GetPathName());
                                Entry->SetNumberField(TEXT("intensity"), Light->Intensity);
                                Entry->SetStringField(TEXT("color"), Light->LightColor.ToString());
                                Entry->SetBoolField(TEXT("use_temperature"), Light->bUseTemperature);
                                Entry->SetNumberField(TEXT("temperature"), Light->Temperature);
                                Entry->SetBoolField(TEXT("registered"), Light->IsRegistered());
                                Entry->SetBoolField(TEXT("render_state_created"), Light->IsRenderStateCreated());
                                Entry->SetBoolField(TEXT("visible"), Light->IsVisible());
                                Entry->SetBoolField(TEXT("affects_world"), Light->bAffectsWorld);
                                Entry->SetStringField(TEXT("world_rotation"), Light->GetComponentRotation().ToString());
                                Entry->SetStringField(TEXT("forward"), Light->GetForwardVector().ToString());
                                Lights.Add(MakeShared<FJsonValueObject>(Entry));
                            }
                        TInlineComponentArray<UPostProcessComponent*> ActorPost(Actor);
                        for (UPostProcessComponent* Post : ActorPost)
                            if (Post)
                            {
                                FString Settings;
                                if (const FProperty* Property = FindFProperty<FProperty>(
                                        Post->GetClass(), TEXT("Settings")))
                                    Property->ExportTextItem(Settings, Property->ContainerPtrToValuePtr<void>(Post),
                                        nullptr, Post, PPF_None);
                                if (Settings.IsEmpty())
                                {
                                    Restore();
                                    Test.AddError(TEXT("Source PP settings could not be snapshotted"));
                                    return false;
                                }
                                SourcePost.Add({Post, Settings});
                                auto Entry = MakeShared<FJsonObject>();
                                Entry->SetStringField(TEXT("component"), Post->GetPathName());
                                Entry->SetBoolField(TEXT("enabled"), Post->bEnabled);
                                Entry->SetBoolField(TEXT("unbound"), Post->bUnbound);
                                Entry->SetNumberField(TEXT("blend_weight"), Post->BlendWeight);
                                Entry->SetNumberField(
                                    TEXT("exposure_method"), int32(Post->Settings.AutoExposureMethod));
                                Entry->SetNumberField(TEXT("exposure_bias"), Post->Settings.AutoExposureBias);
                                Entry->SetNumberField(
                                    TEXT("exposure_bias_backup"), Post->Settings.AutoExposureBiasBackup);
                                Entry->SetBoolField(TEXT("apply_physical_camera_exposure"),
                                    Post->Settings.AutoExposureApplyPhysicalCameraExposure);
                                Entry->SetNumberField(TEXT("camera_iso"), Post->Settings.CameraISO);
                                Entry->SetNumberField(TEXT("camera_shutter_speed"), Post->Settings.CameraShutterSpeed);
                                Entry->SetNumberField(TEXT("camera_fstop"), Post->Settings.DepthOfFieldFstop);
                                Entry->SetNumberField(
                                    TEXT("dof_focal_distance"), Post->Settings.DepthOfFieldFocalDistance);
                                Entry->SetNumberField(TEXT("dof_focal_region"), Post->Settings.DepthOfFieldFocalRegion);
                                Entry->SetNumberField(
                                    TEXT("dof_depth_blur_amount"), Post->Settings.DepthOfFieldDepthBlurAmount);
                                Entry->SetNumberField(
                                    TEXT("dof_depth_blur_radius"), Post->Settings.DepthOfFieldDepthBlurRadius);
                                PostProcesses.Add(MakeShared<FJsonValueObject>(Entry));
                            }
                        TInlineComponentArray<UMeshComponent*> Components(Actor);
                        for (UMeshComponent* Component : Components)
                            if (Component && Component->IsRegistered() && Component->IsVisible())
                            {
                                for (int32 Slot = 0; Slot < Component->GetNumMaterials(); ++Slot)
                                {
                                    UMaterialInterface* Material = Component->GetMaterial(Slot);
                                    TSet<UMaterialInterface*> Seen;
                                    for (int32 Depth = 0; Material && Depth < 32 && !Seen.Contains(Material); ++Depth)
                                    {
                                        Seen.Add(Material);
                                        Materials.Add(Material);
                                        UMaterialInstance* Instance = Cast<UMaterialInstance>(Material);
                                        Material = Instance ? Instance->Parent : nullptr;
                                    }
                                }
                                UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Component);
                                if (!Mesh || !Mesh->GetStaticMesh() || !FMath::IsFinite(Mesh->Bounds.SphereRadius) ||
                                    Mesh->Bounds.SphereRadius <= 1.f)
                                    continue;
                                if (!Room && Mesh->GetStaticMesh()->GetName() == TEXT("SM_CMA_T02_RM_2x2_001"))
                                    Room = Mesh;
                                if (!Mushroom && Mesh->GetStaticMesh()->GetName() == TEXT("SM_CMA_T02_DC_Byoutake01"))
                                    Mushroom = Mesh;
                            }
                    }
            }
        if (!Sources.Num() || !Room || !Mushroom)
        {
            Test.AddError(TEXT("Actual source room/mushroom render components are required"));
            Restore();
            return false;
        }
        Out = FPaths::ProjectSavedDir() / TEXT("P4MapKit/MaterialVisual");
        IFileManager::Get().MakeDirectory(*Out, true);
        Report->SetBoolField(TEXT("exposure_validation_only"), ExposureOnly);
        Report->SetBoolField(TEXT("initial_realtime"), OldRealtime);
        Report->SetBoolField(TEXT("capture_realtime"), true);
        Report->SetStringField(TEXT("map"), Map);
        Report->SetBoolField(TEXT("actual_editor_viewport"), true);
        Report->SetNumberField(TEXT("initial_view_mode"), int32(OldMode));
        Report->SetStringField(TEXT("initial_exposure"), OldExposure.ToString());
        Report->SetBoolField(TEXT("initial_depth_of_field_flag"), OldFlags.DepthOfField);
        Report->SetBoolField(TEXT("initial_lighting_flag"), OldFlags.Lighting);
        Report->SetBoolField(TEXT("initial_direct_lighting_flag"), OldFlags.DirectLighting);
        Report->SetArrayField(TEXT("lights"), Lights);
        Report->SetArrayField(TEXT("post_process_components"), PostProcesses);
        Report->SetNumberField(TEXT("original_layers"), Sources.Num());
        Report->SetNumberField(TEXT("material_interfaces"), Materials.Num());
        Views.Add({TEXT("wide-lit"), TEXT("actual Cave scene"), FVector(-2000.f, -2200.f, 1800.f),
            (FVector(100.f, -100.f, -100.f) - FVector(-2000.f, -2200.f, 1800.f)).Rotation(), VMI_Lit});
        const auto Add = [&](UStaticMeshComponent* Mesh, const TCHAR* Name, EViewModeIndex Mode, const FVector& Offset)
        {
            const FVector Target = Mesh->Bounds.Origin, Camera = Target + Offset * Mesh->Bounds.SphereRadius;
            Views.Add({Name, Mesh->GetPathName(), Camera, (Target - Camera).Rotation(), Mode});
        };
        Add(Room, TEXT("room-lit"), VMI_Lit, FVector(-1.05f, -1.05f, 1.4f));
        GroundMaterials = Room->GetMaterials();
        for (UMaterialInterface* Material : GroundMaterials)
            if (Material)
            {
                TArray<UTexture*> Used;
                Material->GetUsedTextures(Used, EMaterialQualityLevel::Num, true, GMaxRHIFeatureLevel, true);
                for (UTexture* Texture : Used)
                    if (UTexture2D* Flat = Cast<UTexture2D>(Texture))
                        GroundTextures.AddUnique(Flat);
            }
        Add(Room, TEXT("room-lit-all-mips"), VMI_Lit, FVector(-1.05f, -1.05f, 1.4f));
        Views.Last().FullMips = true;
        if (!ExposureOnly)
        {
            Add(Room, TEXT("room-lit-no-pp"), VMI_Lit, FVector(-1.05f, -1.05f, 1.4f));
            Views.Last().NoPost = true;
            Add(Room, TEXT("room-lit-fixed-ev1"), VMI_Lit, FVector(-1.05f, -1.05f, 1.4f));
            Views.Last().FixedExposure = true;
            Add(Room, TEXT("room-lit-fixed-ev-minus2"), VMI_Lit, FVector(-1.05f, -1.05f, 1.4f));
            Views.Last().FixedExposure = true;
            Views.Last().FixedEV = -2.f;
            Add(Room, TEXT("room-lit-no-physical-camera"), VMI_Lit, FVector(-1.05f, -1.05f, 1.4f));
            Views.Last().NoPhysical = true;
            Add(Room, TEXT("room-lighting-only-no-pp"), VMI_LightingOnly, FVector(-1.05f, -1.05f, 1.4f));
            Views.Last().NoPost = true;
            Add(Room, TEXT("room-unlit"), VMI_Unlit, FVector(-1.05f, -1.05f, 1.4f));
            Add(Mushroom, TEXT("mushroom-lit"), VMI_Lit, FVector(-1.6f, -1.6f, 1.f));
            Add(Mushroom, TEXT("mushroom-lit-fixed-ev1"), VMI_Lit, FVector(-1.6f, -1.6f, 1.f));
            Views.Last().FixedExposure = true;
            Add(Mushroom, TEXT("mushroom-lit-no-physical-camera"), VMI_Lit, FVector(-1.6f, -1.6f, 1.f));
            Views.Last().NoPhysical = true;
        }
        Started = FPlatformTime::Seconds();
        Ready = Started + 2.;
        Pose();
        return true;
    }
    bool Update() override
    {
        if (!Live())
            return Fail(TEXT("Actual viewport retired during Cave material capture"));
        const double Now = FPlatformTime::Seconds();
        if (Now - Started > 240.)
            return Fail(TEXT("Bounded actual material capture timed out"));
        if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling())
        {
            GShaderCompilingManager->ProcessAsyncResults(false, false);
            return false;
        }
        if (Now < Ready)
            return false;
        Client->Viewport->Draw(false);
        FlushRenderingCommands();
        if (SettlingDraws > 0)
        {
            --SettlingDraws;
            Ready = Now + 0.05;
            return false;
        }
        TArray<FColor> Pixels;
        const FIntPoint Size = Client->Viewport->GetSizeXY();
        if (!Client->Viewport->ReadPixels(Pixels) || Pixels.Num() != Size.X * Size.Y)
            return Fail(TEXT("Actual editor viewport readback failed"));
        int32 Transparent = 0, Visible = 0;
        uint8 Min = 255, Max = 0;
        TSet<uint32> Colors;
        for (FColor& Pixel : Pixels)
        {
            Transparent += Pixel.A == 0 ? 1 : 0;
            Min = FMath::Min(Min, FMath::Min3(Pixel.R, Pixel.G, Pixel.B));
            Max = FMath::Max(Max, FMath::Max3(Pixel.R, Pixel.G, Pixel.B));
            Visible += FMath::Max3(Pixel.R, Pixel.G, Pixel.B) > 24 ? 1 : 0;
            Pixel.A = 255;
            if (Colors.Num() < 65536)
                Colors.Add(Pixel.DWColor());
        }
        TArray<uint8> PNG;
        FImageUtils::CompressImageArray(Size.X, Size.Y, Pixels, PNG);
        const FView& View = Views[Phase];
        const FString Image = Out / (TEXT("native-cave-") + View.Name + TEXT(".png"));
        if (!FFileHelper::SaveArrayToFile(PNG, *Image))
            return Fail(TEXT("Cannot write owned opaque viewport PNG"));
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("image"), Image);
        Row->SetStringField(TEXT("target"), View.Target);
        Row->SetStringField(TEXT("camera"), View.Location.ToString());
        Row->SetStringField(TEXT("rotation"), View.Rotation.ToString());
        Row->SetNumberField(TEXT("mode"), int32(View.Mode));
        Row->SetBoolField(TEXT("postprocessing_disabled"), View.NoPost);
        Row->SetBoolField(TEXT("fixed_ev1"), View.FixedExposure);
        Row->SetNumberField(TEXT("fixed_ev100"), View.FixedEV);
        Row->SetBoolField(TEXT("physical_camera_disabled_in_final_view"), View.NoPhysical);
        Row->SetNumberField(TEXT("physical_override_draws"), Exposure->Applied);
        Row->SetNumberField(TEXT("rendered_settling_frames"), 8);
        if (View.NoPhysical && Exposure->Applied == 0)
            return Fail(TEXT("The narrow final-view exposure diagnostic never ran"));
        Row->SetNumberField(TEXT("width"), Size.X);
        Row->SetNumberField(TEXT("height"), Size.Y);
        Row->SetNumberField(TEXT("raw_alpha_zero_pixels"), Transparent);
        Row->SetNumberField(TEXT("opaque_png_alpha"), 255);
        Row->SetNumberField(TEXT("visible_rgb_pixels"), Visible);
        Row->SetNumberField(TEXT("minimum_channel"), Min);
        Row->SetNumberField(TEXT("maximum_channel"), Max);
        Row->SetNumberField(TEXT("distinct_rgb_up_to_65536"), Colors.Num());
        TArray<TSharedPtr<FJsonValue>> TextureRows;
        for (UTexture2D* Texture : GroundTextures)
        {
            auto Item = MakeShared<FJsonObject>();
            Item->SetStringField(TEXT("texture"), Texture->GetPathName());
            Item->SetNumberField(TEXT("source_width"), Texture->Source.GetSizeX());
            Item->SetNumberField(TEXT("source_height"), Texture->Source.GetSizeY());
            Item->SetNumberField(TEXT("source_mips"), Texture->Source.GetNumMips());
            Item->SetNumberField(TEXT("platform_width"), Texture->GetSizeX());
            Item->SetNumberField(TEXT("platform_height"), Texture->GetSizeY());
            Item->SetNumberField(TEXT("platform_mips"), Texture->GetNumMips());
            Item->SetNumberField(TEXT("resident_mips"), Texture->GetNumResidentMips());
            Item->SetNumberField(TEXT("lod_bias"), Texture->LODBias);
            Item->SetNumberField(TEXT("texture_group"), int32(Texture->LODGroup));
            Item->SetNumberField(TEXT("filter"), int32(Texture->Filter));
            TextureRows.Add(MakeShared<FJsonValueObject>(Item));
        }
        Row->SetArrayField(TEXT("ground_textures"), TextureRows);
        Row->SetBoolField(TEXT("highest_mips_forced_for_comparison"), View.FullMips);
        Images.Add(MakeShared<FJsonValueObject>(Row));
        Save(FString());
        if (View.Name == TEXT("room-lit") && Visible < Pixels.Num() / 10)
            Test.AddError(TEXT("Default Lit room remains almost black"));
        Test.AddInfo(TEXT("Actual opaque Cave material viewport: ") + Image);
        if (++Phase < Views.Num())
        {
            Pose();
            Ready = Now + 2.;
            return false;
        }
        auto MaterialRows = MakeShared<FJsonObject>();
        int32 CompileProblems = 0;
        for (UMaterialInterface* Interface : Materials)
        {
            auto Entry = MakeShared<FJsonObject>();
            UMaterial* Material = Cast<UMaterial>(Interface);
            Entry->SetNumberField(TEXT("master_expression_count"), Material ? Material->Expressions.Num() : -1);
            if (UMaterialInstance* Instance = Cast<UMaterialInstance>(Interface))
                Entry->SetStringField(TEXT("parent"), Instance->Parent ? Instance->Parent->GetPathName() : FString());
            FMaterialResource* Resource = Interface->GetMaterialResource(GMaxRHIFeatureLevel);
            TArray<TSharedPtr<FJsonValue>> Errors;
            if (Resource)
                for (const FString& Error : Resource->GetCompileErrors())
                    Errors.Add(MakeShared<FJsonValueString>(Error));
            const bool Compiled = Resource && Resource->HasValidGameThreadShaderMap() && Errors.Num() == 0;
            Entry->SetBoolField(TEXT("renderer_compiled"), Compiled);
            Entry->SetArrayField(TEXT("compile_errors"), Errors);
            CompileProblems += Compiled ? 0 : 1;
            MaterialRows->SetObjectField(Interface->GetPathName(), Entry);
        }
        TArray<TSharedPtr<FJsonValue>> GroundRows;
        for (UMaterialInterface* Interface : GroundMaterials)
            if (Interface)
            {
                UMaterial* Master = Interface->GetMaterial();
                auto Item = MakeShared<FJsonObject>();
                Item->SetStringField(TEXT("material"), Interface->GetPathName());
                Item->SetStringField(TEXT("master"), Master ? Master->GetPathName() : FString());
                if (Master)
                {
                    Item->SetBoolField(TEXT("normal_connected"), Master->Normal.Expression != nullptr);
                    Item->SetBoolField(TEXT("roughness_connected"), Master->Roughness.Expression != nullptr);
                    Item->SetBoolField(TEXT("base_color_connected"), Master->BaseColor.Expression != nullptr);
                    FString GraphError;
                    const FString Graph = P4MaterialGraphJson(Master, GraphError);
                    const FString GraphFile = Out / (TEXT("ground-") + Master->GetName() + TEXT(".json"));
                    if (!Graph.IsEmpty() &&
                        FFileHelper::SaveStringToFile(
                            Graph, *GraphFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
                        Item->SetStringField(TEXT("graph_file"), GraphFile);
                    else
                        Item->SetStringField(TEXT("graph_read_error"), GraphError);
                    TArray<TSharedPtr<FJsonValue>> UVs;
                    for (UMaterialExpression* Expression : Master->Expressions)
                        if (auto* UV = Cast<UMaterialExpressionTextureCoordinate>(Expression))
                        {
                            auto UVRow = MakeShared<FJsonObject>();
                            UVRow->SetNumberField(TEXT("coordinate_index"), UV->CoordinateIndex);
                            UVRow->SetNumberField(TEXT("u_tiling"), UV->UTiling);
                            UVRow->SetNumberField(TEXT("v_tiling"), UV->VTiling);
                            UVs.Add(MakeShared<FJsonValueObject>(UVRow));
                        }
                    Item->SetArrayField(TEXT("texture_coordinates"), UVs);
                }
                if (UMaterialInstance* Instance = Cast<UMaterialInstance>(Interface))
                {
                    auto Scalars = MakeShared<FJsonObject>();
                    for (const auto& Parameter : Instance->ScalarParameterValues)
                        Scalars->SetNumberField(Parameter.ParameterInfo.Name.ToString(), Parameter.ParameterValue);
                    Item->SetObjectField(TEXT("scalar_overrides"), Scalars);
                }
                GroundRows.Add(MakeShared<FJsonValueObject>(Item));
            }
        Report->SetArrayField(TEXT("ground_materials"), GroundRows);
        auto Quality = MakeShared<FJsonObject>();
        for (const TCHAR* Name : {TEXT("sg.TextureQuality"), TEXT("sg.ShadingQuality"), TEXT("sg.PostProcessQuality"),
                 TEXT("r.DepthOfFieldQuality"), TEXT("r.PostProcessAAQuality"), TEXT("r.Tonemapper.Sharpen"),
                 TEXT("r.MaterialQualityLevel"), TEXT("r.Streaming.MipBias"), TEXT("r.Streaming.PoolSize"),
                 TEXT("r.MaxAnisotropy"), TEXT("r.ScreenPercentage")})
            if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
                Quality->SetStringField(Name, Variable->GetString());
        Report->SetObjectField(TEXT("editor_quality"), Quality);
        Report->SetObjectField(TEXT("materials"), MaterialRows);
        Report->SetNumberField(TEXT("compile_problems"), CompileProblems);
        bool Unchanged = true, RecordsUnchanged = true;
        TArray<TSharedPtr<FJsonValue>> SourceChecks;
        for (const FSource& Before : Sources)
        {
            UWorld* World = Before.World.Get();
            FP4OriginalRecord Record;
            FString Error;
            const bool Read = World && P4ReadOriginalRecord(World, Record, Error);
            auto Check = MakeShared<FJsonObject>();
            Check->SetStringField(TEXT("world"), World ? World->GetPathName() : TEXT("gone"));
            Check->SetBoolField(TEXT("record_read"), Read);
            Check->SetBoolField(TEXT("source_equal"), Read && Record.SourceDigest == Before.Source);
            Check->SetBoolField(TEXT("baseline_equal"), Read && Record.ComparisonDigest == Before.Baseline);
            Check->SetBoolField(TEXT("dirty_before"), Before.Dirty);
            Check->SetBoolField(TEXT("dirty_after"), World && World->GetOutermost()->IsDirty());
            SourceChecks.Add(MakeShared<FJsonValueObject>(Check));
            RecordsUnchanged &= Read && Record.SourceDigest == Before.Source &&
                Record.ComparisonDigest == Before.Baseline;
            Unchanged &= Read && Record.SourceDigest == Before.Source && Record.ComparisonDigest == Before.Baseline &&
                World->GetOutermost()->IsDirty() == Before.Dirty;
        }
        Report->SetArrayField(TEXT("source_checks"), SourceChecks);
        Report->SetBoolField(TEXT("source_records_and_world_dirty_unchanged"), Unchanged);
        Report->SetBoolField(TEXT("source_records_unchanged"), RecordsUnchanged);
        bool PostUnchanged = SourcePost.Num() > 0;
        for (const FSourcePost& Before : SourcePost)
        {
            UPostProcessComponent* Post = Before.Component.Get();
            FString Current;
            if (Post)
                if (const FProperty* Property = FindFProperty<FProperty>(Post->GetClass(), TEXT("Settings")))
                    Property->ExportTextItem(
                        Current, Property->ContainerPtrToValuePtr<void>(Post), nullptr, Post, PPF_None);
            PostUnchanged &= Current == Before.Settings;
        }
        Report->SetBoolField(TEXT("source_postprocess_settings_exactly_unchanged"), PostUnchanged);
        Save(FString());
        Restore();
        if (!RecordsUnchanged)
            Test.AddError(TEXT("Viewport diagnostics changed original level metadata"));
        // Moving the real editor camera can dirty its persisted editor-view state.
        // The focused exposure test checks the original records and every source PP
        // property separately; the general material diagnostic keeps its stricter gate.
        if (!Unchanged && !ExposureOnly)
            Test.AddError(TEXT("Viewport diagnostics changed world dirty state"));
        if (!PostUnchanged)
            Test.AddError(TEXT("Viewport exposure diagnostic changed imported source PP settings"));
        if (CompileProblems && !ExposureOnly)
            Test.AddError(
                FString::Printf(TEXT("%d actual material interfaces lack valid D3D shader maps; see exact diagnostics"),
                    CompileProblems));
        return true;
    }
};
} //namespace P4CaveMaterialVisual
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4CaveMaterialVisual, "P4MapKit.Editor.CaveMaterialActualViewport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4CaveMaterialVisual::RunTest(const FString&)
{
    const FString Map = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MATERIAL_VISUAL_MAP"));
    if (Map.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. ")
                TEXT("Opt-in retained Cave actual material viewport: set P4_NATIVE_MATERIAL_VISUAL_MAP"));
        return true;
    }
    const auto Capture = MakeShared<P4CaveMaterialVisual::FCapture>(*this);
    if (!Capture->Setup(Map))
        return false;
    FAutomationTestFramework::Get().EnqueueLatentCommand(Capture);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ImportedExposureVisual, "P4MapKit.Editor.ImportedExposureActualViewport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ImportedExposureVisual::RunTest(const FString&)
{
    const FString Map = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MATERIAL_VISUAL_MAP"));
    if (Map.IsEmpty())
    {
        AddWarning(
            TEXT("P4MapKit test not run. Set P4_NATIVE_MATERIAL_VISUAL_MAP for the opt-in exposure viewport check"));
        return true;
    }
    const auto Capture = MakeShared<P4CaveMaterialVisual::FCapture>(*this, true);
    if (!Capture->Setup(Map))
        return false;
    FAutomationTestFramework::Get().EnqueueLatentCommand(Capture);
    return true;
}
#endif

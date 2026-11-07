#if WITH_DEV_AUTOMATION_TESTS
#include "P4RetainedMaterialFixture.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "ImageUtils.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PreviewScene.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "Serialization/JsonSerializer.h"
#include "ShaderCompiler.h"
#include "StaticMeshResources.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace P4RetainedShadowLook
{
namespace Proof = P4RetainedMaterialCandidateProof;
constexpr int32 Width = 512, Height = 512, SettlingFrames = 8;
const TCHAR* Package = TEXT("/Game/Carrot4/MasterMaterial/chr/M_chr_Shadow_Master");
const TCHAR* Object = TEXT("/Game/Carrot4/MasterMaterial/chr/M_chr_Shadow_Master.M_chr_Shadow_Master");

FString JsonText(const TSharedPtr<FJsonObject>& Value)
{
    FString Out;
    if (Value.IsValid())
        FJsonSerializer::Serialize(Value.ToSharedRef(), TJsonWriterFactory<>::Create(&Out));
    return Out;
}

FString NativeDigest(const TMap<FString, TArray<uint8>>& Values)
{
    TArray<FString> Keys, Lines;
    Values.GetKeys(Keys);
    Keys.Sort();
    for (const FString& Key : Keys)
        Lines.Add(Key + TEXT("|") + Proof::Hash(Values.FindChecked(Key)));
    const FTCHARToUTF8 Text(*FString::Join(Lines, TEXT("\n")));
    TArray<uint8> Bytes;
    Bytes.Append(reinterpret_cast<const uint8*>(Text.Get()), Text.Length());
    return Proof::Hash(Bytes);
}

// Only already-existing contexts, never LoadMap or traverse/import a source world.
TMap<FString, FString> ExistingWorlds()
{
    TMap<FString, FString> Out;
    if (GEngine)
        for (const FWorldContext& Context : GEngine->GetWorldContexts())
            if (UWorld* World = Context.World())
                Out.Add(World->GetPathName(),
                    FString::Printf(TEXT("%p|%u|%08x|%u"), World, uint32(World->WorldType), uint32(World->GetFlags()),
                        World->GetOutermost()->IsDirty() ? 1u : 0u));
    return Out;
}

bool ShaderReady(UMaterialInterface* Material)
{
    FMaterialResource* Resource = Material ? Material->GetMaterialResource(ERHIFeatureLevel::SM5) : nullptr;
    return Resource && Resource->HasValidGameThreadShaderMap() && Resource->GetCompileErrors().Num() == 0;
}

class FLook : public IAutomationLatentCommand
{
    FAutomationTestBase& Test;
    TStrongObjectPtr<UMaterial> Shadow;
    TStrongObjectPtr<UMaterialInstanceDynamic> Instance;
    TStrongObjectPtr<UMaterialInterface> FloorSurface;
    TStrongObjectPtr<UTextureRenderTarget2D> Target;
    TUniquePtr<FPreviewScene> Scene;
    UStaticMeshComponent* Consumer = nullptr;
    UStaticMeshComponent* Floor = nullptr;
    USceneCaptureComponent2D* Capture = nullptr;
    TMap<FString, TArray<uint8>> NativeBefore;
    TMap<FName, FString> MetadataBefore;
    TMap<FString, FString> FilesBefore, WorldsBefore;
    TArray<FColor> ControlPixels;
    TSharedPtr<FJsonObject> Ledger, Report = MakeShared<FJsonObject>();
    FString LedgerBefore, Out;
    bool HaveSnapshot = false, Finished = false;
    int32 Phase = 0, Draws = 0, FloorShaderWaits = 0;
    double Began = 0, NextDraw = 0;
    const FVector Camera = FVector(260.f, -320.f, 190.f), Aim = FVector(0.f, 0.f, 35.f);

    void RetirePreview()
    {
        if (Capture)
            Capture->TextureTarget = nullptr;
        // FPreviewScene owns registration and its FGCObject references. Its
        // destructor unregisters components, cleans its world and removes only
        // that new world context. No original actor/component is registered here.
        Scene.Reset();
        Capture = nullptr;
        Consumer = nullptr;
        Floor = nullptr;
        FlushRenderingCommands();
        Instance.Reset();
        Target.Reset();
        FloorSurface.Reset();
    }

    bool Finish(const FString& Error)
    {
        if (Finished)
            return true;
        Finished = true;
        if (!Error.IsEmpty())
            Test.AddError(Error);
        RetirePreview();
        bool Preserved = false;
        if (HaveSnapshot && Shadow.IsValid())
        {
            UMaterial* After = nullptr;
            TSharedPtr<FJsonObject> AfterLedger;
            FString Why;
            TMap<FString, FString> FilesAfter;
            const bool Qualified = Proof::QualifiedShadowForLook(After, AfterLedger, Why);
            const auto NativeAfter = Proof::Native(Shadow.Get());
            const bool NativeSame = NativeAfter.OrderIndependentCompareEqual(NativeBefore);
            const bool MetadataSame = Proof::Metadata(Shadow.Get()).OrderIndependentCompareEqual(MetadataBefore);
            const bool FileSame = Proof::Files(Package, FilesAfter) &&
                FilesAfter.OrderIndependentCompareEqual(FilesBefore);
            const bool ContextsSame = ExistingWorlds().OrderIndependentCompareEqual(WorldsBefore);
            const bool LedgerSame = Qualified && After == Shadow.Get() && JsonText(AfterLedger) == LedgerBefore;
            Preserved = NativeSame && MetadataSame && FileSame && ContextsSame && LedgerSame;
            Report->SetStringField(TEXT("native_before_sha256"), NativeDigest(NativeBefore));
            Report->SetStringField(TEXT("native_after_sha256"), NativeDigest(NativeAfter));
            Report->SetBoolField(TEXT("whole_native_and_object_flags_preserved"), NativeSame);
            Report->SetBoolField(TEXT("whole_metadata_and_original_record_preserved"), MetadataSame);
            Report->SetBoolField(TEXT("saved_bundle_hash_mtime_attributes_preserved"), FileSame);
            Report->SetBoolField(TEXT("existing_world_contexts_preserved"), ContextsSame);
            Report->SetBoolField(TEXT("reentrant_source_13world_bundle_ledger_admission_preserved"), LedgerSame);
            if (!Why.IsEmpty())
                Report->SetStringField(TEXT("post_preview_admission_error"), Why);
            if (!Preserved)
                Test.AddError(TEXT("Shadow preview changed a protected native/record/file/world/source identity"));
        }
        Report->SetBoolField(TEXT("preservation_proved"), Preserved);
        Report->SetNumberField(TEXT("floor_shader_wait_ticks"), FloorShaderWaits);
        Report->SetBoolField(TEXT("passed"), Error.IsEmpty() && Preserved && !Test.HasAnyErrors());
        Report->SetStringField(TEXT("error"), Error);
        Report->SetNumberField(TEXT("elapsed_seconds"), FPlatformTime::Seconds() - Began);
        if (!Out.IsEmpty())
        {
            const FString Text = JsonText(Report);
            if (!FFileHelper::SaveStringToFile(
                    Text, *(Out / TEXT("shadow-preview.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
                Test.AddError(TEXT("Cannot write the bounded owned Shadow preview receipt"));
        }
        return true;
    }

    bool RenderIdentity()
    {
        if (!Instance.IsValid() || !Shadow.IsValid() || !Consumer || Consumer->GetMaterial(0) != Instance.Get() ||
            Instance->Parent != Shadow.Get() || !ShaderReady(Shadow.Get()) || !ShaderReady(Instance.Get()))
            return false;
        FMaterialRenderProxy* Proxy = Instance->GetRenderProxy();
        UMaterial* Expected = Shadow.Get();
        UMaterialInstanceDynamic* ExpectedInstance = Instance.Get();
        bool NoFallback = false, ExactProxy = false, ExactMaterial = false, ValidMap = false;
        ENQUEUE_RENDER_COMMAND(P4RetainedShadowReadOnlyProxy)(
            [Proxy, Expected, ExpectedInstance, &NoFallback, &ExactProxy, &ExactMaterial, &ValidMap](
                FRHICommandListImmediate&)
            {
                if (!Proxy)
                    return;
                const FMaterialRenderProxy* Fallback = nullptr;
                const FMaterial& Actual = Proxy->GetMaterialWithFallback(ERHIFeatureLevel::SM5, Fallback);
                const FMaterialShaderMap* Map = Actual.GetRenderingThreadShaderMap();
                NoFallback = !Fallback && !Actual.IsDefaultMaterial() &&
                    Proxy->GetMaterialNoFallback(ERHIFeatureLevel::SM5) == &Actual;
                ExactProxy = Proxy->GetMaterialInterface() == ExpectedInstance;
                ExactMaterial = Actual.GetMaterialInterface() == Expected;
                ValidMap = Map && Map->IsValidForRendering();
            });
        FlushRenderingCommands(); // all referenced objects stay strongly held through this command
        Report->SetBoolField(TEXT("render_thread_no_fallback"), NoFallback);
        Report->SetBoolField(TEXT("render_thread_exact_mid_proxy"), ExactProxy);
        Report->SetBoolField(TEXT("render_thread_exact_saved_shadow_resource"), ExactMaterial);
        Report->SetBoolField(TEXT("render_thread_valid_sm5_shader_map"), ValidMap);
        return NoFallback && ExactProxy && ExactMaterial && ValidMap;
    }

    bool Image(TArray<FColor>& Pixels, const TCHAR* Name)
    {
        FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
        if (!Resource || !Resource->ReadPixels(Pixels) || Pixels.Num() != Width * Height)
            return false;
        // SceneCapture FinalColorLDR alpha is not an opacity proof. Export opaque
        // RGB exactly as in the existing native106 viewport evidence helper.
        for (FColor& Pixel : Pixels)
            Pixel.A = 255;
        TArray<uint8> PNG;
        FImageUtils::CompressImageArray(Width, Height, Pixels, PNG);
        const FString Path = Out / Name;
        if (!PNG.Num() || !FFileHelper::SaveArrayToFile(PNG, *Path))
            return false;
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("path"), Path);
        Row->SetStringField(TEXT("sha256"), Proof::Hash(PNG));
        Row->SetNumberField(TEXT("bytes"), PNG.Num());
        Report->SetObjectField(Phase == 0 ? TEXT("hidden_control_png") : TEXT("visible_shadow_png"), Row);
        return true;
    }

public:
    explicit FLook(FAutomationTestBase& InTest) : Test(InTest) {}
    ~FLook() override
    {
        if (!Finished)
            Finish(TEXT("Shadow preview retired before terminal verification"));
    }

    bool Setup()
    {
        Began = FPlatformTime::Seconds();
        if (!GEditor || GUsingNullRHI || GMaxRHIFeatureLevel != ERHIFeatureLevel::SM5)
        {
            Test.AddError(TEXT("Shadow LOOK requires the real owned D3D SM5 editor"));
            return false;
        }
        UMaterial* Material = nullptr;
        FString Why;
        if (!Proof::QualifiedShadowForLook(Material, Ledger, Why))
        {
            Test.AddError(TEXT("Saved/cold Shadow admission refused: ") + Why);
            return false;
        }
        if (!Material || Material->GetPathName() != Object || !ShaderReady(Material))
        {
            Test.AddError(TEXT("Exact saved Shadow has no qualified existing SM5 shader map"));
            return false;
        }
        Shadow.Reset(Material);
        LedgerBefore = JsonText(Ledger);
        NativeBefore = Proof::Native(Material);
        MetadataBefore = Proof::Metadata(Material);
        WorldsBefore = ExistingWorlds();
        if (!Proof::Files(Package, FilesBefore))
        {
            Test.AddError(TEXT("Cannot snapshot the exact saved Shadow bundle"));
            return false;
        }
        HaveSnapshot = true;
        Out = FPaths::ProjectSavedDir() / TEXT("P4MapKit/MaterialRecoveryCandidates/ShadowLook");
        IFileManager::Get().MakeDirectory(*Out, true);
        Report->SetStringField(TEXT("scope"),
            TEXT(
                "Actual isolated FPreviewScene/SceneCapture render-target pixels; no editor/user viewport, original Cave or game-pixel-fidelity claim"));
        Report->SetStringField(TEXT("material"), Material->GetPathName());
        Report->SetObjectField(TEXT("qualified_cold_ledger"), Ledger);
        Report->SetBoolField(TEXT("source_asset_or_world_save_requested"), false);
        Report->SetBoolField(TEXT("thumbnail_info_accessed"), false);
        Report->SetBoolField(TEXT("force_mips_resident"), false);
        Report->SetStringField(TEXT("source_blend_mode"), TEXT("BLEND_Opaque"));
        Report->SetStringField(TEXT("interpretation"),
            TEXT(
                "The actual opaque black Shadow surface on declared fixture geometry; no masked/dissolve rendering claim"));
        FPreviewScene::ConstructionValues Values;
        Values.AllowAudioPlayback(false)
            .SetCreatePhysicsScene(false)
            .ShouldSimulatePhysics(false)
            .SetTransactional(false)
            .SetForceMipsResident(false)
            .SetSkyBrightness(0.f)
            .SetLightBrightness(PI)
            .SetLightRotation(FRotator(-40.f, -67.5f, 0.f));
        Scene = MakeUnique<FPreviewScene>(Values);
        if (!Scene->GetWorld() || Scene->GetWorld()->FeatureLevel != ERHIFeatureLevel::SM5)
            return !Finish(TEXT("Preview scene feature level is not SM5"));
        UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
        UStaticMesh* FloorMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
        UMaterialInterface* FloorMaterial = LoadObject<UMaterialInterface>(
            nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
        if (!SphereMesh || !FloorMesh || !FloorMaterial || !SphereMesh->RenderData ||
            !SphereMesh->RenderData->LODResources.Num() ||
            SphereMesh->RenderData->LODResources[0].GetNumTriangles() <= 0)
            return !Finish(TEXT("Exact Engine fixture meshes and floor material are required"));
        FloorSurface.Reset(FloorMaterial);
        Instance.Reset(UMaterialInstanceDynamic::Create(Material, GetTransientPackage()));
        if (!Instance.IsValid())
            return !Finish(TEXT("Cannot create the transient actual Shadow consumer MID"));
        float Dissolve = 0.f;
        if (!Material->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Dissolve")), Dissolve) ||
            Dissolve != 0.f)
            return !Finish(TEXT("The evidenced source Dissolve default is not zero"));
        Instance->SetScalarParameterValue(TEXT("Dissolve"), Dissolve);
        Consumer = NewObject<UStaticMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
        Consumer->SetMobility(EComponentMobility::Movable);
        Consumer->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Consumer->SetStaticMesh(SphereMesh);
        Consumer->SetMaterial(0, Instance.Get());
        Consumer->SetVisibility(false);
        Scene->AddComponent(Consumer, FTransform(FRotator::ZeroRotator, FVector(0.f, 0.f, 50.f)));
        Floor = NewObject<UStaticMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
        Floor->SetMobility(EComponentMobility::Movable);
        Floor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Floor->SetStaticMesh(FloorMesh);
        Floor->SetMaterial(0, FloorMaterial);
        Scene->AddComponent(Floor, FTransform(FRotator::ZeroRotator, FVector(0.f, 0.f, -5.f), FVector(6.f, 6.f, .1f)));
        Target.Reset(NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient));
        Target->ClearColor = FLinearColor(.08f, .1f, .14f, 1.f);
        Target->TargetGamma = 2.2f;
        Target->InitCustomFormat(Width, Height, PF_B8G8R8A8, false);
        Target->UpdateResourceImmediate(true);
        Capture = NewObject<USceneCaptureComponent2D>(GetTransientPackage(), NAME_None, RF_Transient);
        Capture->TextureTarget = Target.Get();
        Capture->CaptureSource = SCS_FinalColorLDR;
        Capture->bCaptureEveryFrame = false;
        Capture->bCaptureOnMovement = false;
        Capture->FOVAngle = 35.f;
        Capture->ShowFlags.SetTemporalAA(false);
        Capture->ShowFlags.SetMotionBlur(false);
        Capture->PostProcessSettings.bOverride_AutoExposureMethod = true;
        Capture->PostProcessSettings.AutoExposureMethod = AEM_Manual;
        Capture->PostProcessSettings.bOverride_AutoExposureBias = true;
        Capture->PostProcessSettings.AutoExposureBias = 0.f;
        Capture->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
        Capture->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
        Capture->PostProcessSettings.bOverride_BloomIntensity = true;
        Capture->PostProcessSettings.BloomIntensity = 0.f;
        Scene->AddComponent(Capture, FTransform((Aim - Camera).Rotation(), Camera));
        FlushRenderingCommands();
        Report->SetStringField(TEXT("isolated_preview_world"), Scene->GetWorld()->GetPathName());
        Report->SetStringField(TEXT("transient_render_target"), Target->GetPathName());
        Report->SetStringField(TEXT("capture_source"), TEXT("SCS_FinalColorLDR"));
        Report->SetStringField(TEXT("consumer_component"), Consumer->GetPathName());
        Report->SetStringField(TEXT("consumer_mid"), Instance->GetPathName());
        Report->SetStringField(TEXT("mid_parent"), Instance->Parent->GetPathName());
        Report->SetStringField(TEXT("mesh"), SphereMesh->GetPathName());
        Report->SetStringField(TEXT("consumer_transform"), Consumer->GetComponentTransform().ToString());
        Report->SetNumberField(TEXT("mesh_lod0_triangles"), SphereMesh->RenderData->LODResources[0].GetNumTriangles());
        Report->SetStringField(TEXT("floor_mesh"), FloorMesh->GetPathName());
        Report->SetStringField(TEXT("floor_transform"), Floor->GetComponentTransform().ToString());
        Report->SetStringField(TEXT("floor_material"), FloorMaterial->GetPathName());
        Report->SetStringField(TEXT("camera"), Camera.ToString());
        Report->SetStringField(TEXT("rotation"), (Aim - Camera).Rotation().ToString());
        Report->SetNumberField(TEXT("fov_degrees"), 35.);
        Report->SetNumberField(TEXT("manual_exposure_bias"), 0.);
        Report->SetBoolField(TEXT("physical_camera_exposure"), false);
        Report->SetNumberField(TEXT("mid_Dissolve"), Dissolve);
        Report->SetNumberField(TEXT("light_intensity"), PI);
        Report->SetStringField(TEXT("light_rotation"), TEXT("P=-40 Y=-67.5 R=0"));
        Report->SetNumberField(TEXT("width"), Width);
        Report->SetNumberField(TEXT("height"), Height);
        Report->SetNumberField(TEXT("settling_captures_per_case"), SettlingFrames);
        if (!RenderIdentity())
            return !Finish(TEXT("Actual Shadow render proxy uses fallback or a different native material"));
        NextDraw = FPlatformTime::Seconds() + .05;
        return true;
    }

    bool Update() override
    {
        if (Finished)
            return true;
        const double Now = FPlatformTime::Seconds();
        if (Now - Began > 60.)
            return Finish(TEXT("Bounded isolated Shadow capture exceeded60seconds"));
        if (!Scene || !Capture || !Target.IsValid() || !Consumer || !Consumer->IsRegistered())
            return Finish(TEXT("Owned preview consumer retired unexpectedly"));
        if (Now < NextDraw)
            return false;
        // No ForceRecompile/FinishAllCompilation or diagnostic cook. An existing
        // shader failure is fatal; do not use the default material as image proof.
        if (!ShaderReady(Shadow.Get()) || !ShaderReady(Instance.Get()))
            return Finish(TEXT("Qualified Shadow shader ceased to be ready"));
        FMaterialResource* FloorResource = FloorSurface.IsValid()
            ? FloorSurface->GetMaterialResource(ERHIFeatureLevel::SM5)
            : nullptr;
        if (FloorResource && FloorResource->GetCompileErrors().Num())
            return Finish(TEXT("The Engine floor material has actual shader errors"));
        if (!ShaderReady(FloorSurface.Get()))
        {
            // Loading the stock floor can queue its first shader map. Drain only
            // results already pending under the same60second latent budget;
            // never request a new compile or capture its fallback appearance.
            if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling())
            {
                GShaderCompilingManager->ProcessAsyncResults(true, false);
                ++FloorShaderWaits;
                NextDraw = Now + .05;
                return false;
            }
            return Finish(TEXT("Engine floor has no valid SM5 shader map and no queued compilation to settle"));
        }
        Report->SetBoolField(TEXT("floor_sm5_ready_without_compile_errors"), true);
        Scene->GetWorld()->SendAllEndOfFrameUpdates();
        Capture->CaptureScene();
        FlushRenderingCommands();
        if (++Draws < SettlingFrames)
        {
            NextDraw = Now + .05;
            return false;
        }
        if (!RenderIdentity())
            return Finish(TEXT("Actual Shadow render identity changed before pixel readback"));
        if (Phase == 0)
        {
            if (!Image(ControlPixels, TEXT("shadow-consumer-hidden.png")))
                return Finish(TEXT("Cannot read/write actual hidden-consumer control pixels"));
            Consumer->SetVisibility(true);
            Phase = 1;
            Draws = 0;
            NextDraw = Now + .05;
            return false;
        }
        TArray<FColor> Pixels;
        if (!Image(Pixels, TEXT("shadow-consumer-visible.png")))
            return Finish(TEXT("Cannot read/write actual visible Shadow pixels"));
        int32 Changed = 0, CenterChanged = 0, Darkened = 0;
        TSet<uint32> Colors;
        for (int32 I = 0; I < Pixels.Num(); ++I)
        {
            const FColor& A = ControlPixels[I];
            const FColor& B = Pixels[I];
            const int32 Delta = FMath::Abs(int32(A.R) - B.R) + FMath::Abs(int32(A.G) - B.G) +
                FMath::Abs(int32(A.B) - B.B);
            const int32 X = I % Width, Y = I / Width;
            if (Delta > 16)
            {
                ++Changed;
                if (X >= Width / 4 && X < 3 * Width / 4 && Y >= Height / 4 && Y < 3 * Height / 4)
                    ++CenterChanged;
                if (int32(A.R) + A.G + A.B > int32(B.R) + B.G + B.B + 16)
                    ++Darkened;
            }
            if (Colors.Num() < 65536)
                Colors.Add(B.DWColor());
        }
        Report->SetNumberField(TEXT("changed_rgb_pixels_from_hidden_control"), Changed);
        Report->SetNumberField(TEXT("central_changed_rgb_pixels"), CenterChanged);
        Report->SetNumberField(TEXT("darkened_rgb_pixels"), Darkened);
        Report->SetNumberField(TEXT("distinct_rgb_up_to65536"), Colors.Num());
        Report->SetNumberField(TEXT("opaque_png_alpha"), 255);
        const bool Visible = Changed >= 1024 && CenterChanged >= 512 && Darkened >= 512 && Colors.Num() >= 8;
        Report->SetBoolField(TEXT("actual_consumer_contributes_pixels"), Visible);
        return Finish(
            Visible ? FString() : TEXT("No sufficient central Shadow contribution against the actual hidden control"));
    }
};
} //namespace P4RetainedShadowLook

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4RetainedShadowLook, "P4MapKit.Kadan.RetainedShadowActualPreview",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4RetainedShadowLook::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETAINED_SHADOW_LOOK")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. ")
                TEXT("Opt-in saved/cold Shadow FPreviewScene/SceneCapture LOOK; no original asset/world writes."));
        return true;
    }
    const auto Look = MakeShared<P4RetainedShadowLook::FLook>(*this);
    if (!Look->Setup())
        return false;
    FAutomationTestFramework::Get().EnqueueLatentCommand(Look);
    return true;
}
#endif

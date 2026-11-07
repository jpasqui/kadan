#if P4_MAPKIT_TESTS
#include "P4MaterialGameSettings.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Level.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Modules/ModuleManager.h"
#include "RenderingThread.h"
#include "Serialization/JsonSerializer.h"
#include "StaticMeshResources.h"
#include "ShaderCompiler.h"
#include "P4OriginalLevelImport.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MapVisualDiagnostics, "P4MapKit.Editor.MapVisualDiagnostics",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MapVisualDiagnostics::RunTest(const FString&)
{
    const FString Selection = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_VISUAL_DIAGNOSTICS"));
    if (Selection.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. Supply explicit saved map/camera diagnostic selections."));
        return true;
    }
    FString Text;
    TArray<TSharedPtr<FJsonValue>> Requests;
    if (!TestTrue(TEXT("The diagnostic selection is readable"),
            FFileHelper::LoadFileToString(Text, *Selection) &&
                FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Requests) && Requests.Num() <= 16))
        return false;
    for (const auto& Request : Requests)
    {
        auto Input = Request->AsObject();
        const FString Package = Input->GetStringField(TEXT("map"));
        UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(Package);
        if (!TestNotNull(TEXT("The selected saved map opens"), World))
            return false;
        World->FlushLevelStreaming(EFlushLevelStreamingType::Full);
        FString Variant;
        if (Input->TryGetStringField(TEXT("original_variant"), Variant))
        {
            TSet<FName> Expected;
            FString Error;
            if (!TestTrue(TEXT("The diagnostic activates the requested original streaming graph: ") + Error,
                    P4ActivateOriginalMapEditorLayers(World, Variant, Expected, Error)))
                return false;
        }
        auto Report = MakeShared<FJsonObject>();
        Report->SetStringField(TEXT("map"), Package);
        TArray<TSharedPtr<FJsonValue>> Parts;
        TArray<UPrimitiveComponent*> GroundSoftParts;
        const FString MeshFilter = Input->GetStringField(TEXT("mesh_filter"));
        for (ULevel* Level : World->GetLevels())
            if (Level)
                for (AActor* Actor : Level->Actors)
                {
                    if (!IsValid(Actor))
                        continue;
                    TInlineComponentArray<UStaticMeshComponent*> Components(Actor);
                    for (auto* Component : Components)
                    {
                        UStaticMesh* Mesh = Component->GetStaticMesh();
                        if (!Mesh)
                            continue;
                        for (int32 Slot = 0; Slot < Component->GetNumMaterials(); ++Slot)
                            if (UMaterialInterface* Interface = Component->GetMaterial(Slot))
                                if (const auto* Settings = UP4MaterialGameSettings::Find(Interface->GetMaterial()))
                                    if (Settings->C4MaterialDrawOrder == C4MDO_GroundSoft)
                                        GroundSoftParts.AddUnique(Component);
                        if (!Mesh->GetPathName().Contains(MeshFilter))
                            continue;
                        auto Row = MakeShared<FJsonObject>();
                        Row->SetStringField(TEXT("component"), Component->GetPathName());
                        Row->SetStringField(TEXT("mesh"), Mesh->GetPathName());
                        Row->SetStringField(TEXT("location"), Component->GetComponentLocation().ToString());
                        Row->SetStringField(TEXT("extent"), Component->Bounds.BoxExtent.ToString());
                        Row->SetBoolField(TEXT("registered"), Component->IsRegistered());
                        Row->SetBoolField(TEXT("visible"), Component->IsVisible());
                        Row->SetBoolField(TEXT("editor_hidden"), Actor->IsHiddenEd());
                        Row->SetBoolField(TEXT("level_visible"), Level->bIsVisible);
                        const FStaticMeshRenderData* Data = Mesh->RenderData.Get();
                        Row->SetNumberField(TEXT("triangles"),
                            Data && Data->LODResources.Num() ? Data->LODResources[0].GetNumTriangles() : 0);
                        TArray<TSharedPtr<FJsonValue>> Materials;
                        for (int32 Slot = 0; Slot < Component->GetNumMaterials(); ++Slot)
                        {
                            UMaterialInterface* Interface = Component->GetMaterial(Slot);
                            UMaterial* Material = Interface ? Interface->GetMaterial() : nullptr;
                            auto Facts = MakeShared<FJsonObject>();
                            Facts->SetStringField(TEXT("path"), GetPathNameSafe(Interface));
                            Facts->SetStringField(TEXT("root"), GetPathNameSafe(Material));
                            if (Material)
                            {
                                Facts->SetNumberField(TEXT("blend_mode"), int32(Interface->GetBlendMode()));
                                if (const auto* Settings = UP4MaterialGameSettings::Find(Material))
                                {
                                    Facts->SetNumberField(TEXT("c4_draw_order"), int32(Settings->C4MaterialDrawOrder));
                                }
                                TSet<FString> Types;
                                for (UMaterialExpression* Expression : Material->Expressions)
                                    if (IsValid(Expression))
                                        Types.Add(Expression->GetClass()->GetName());
                                TArray<TSharedPtr<FJsonValue>> Names;
                                for (const FString& Type : Types)
                                    Names.Add(MakeShared<FJsonValueString>(Type));
                                Facts->SetArrayField(TEXT("expression_types"), Names);
                            }
                            Materials.Add(MakeShared<FJsonValueObject>(Facts));
                        }
                        Row->SetArrayField(TEXT("materials"), Materials);
                        Parts.Add(MakeShared<FJsonValueObject>(Row));
                    }
                }
        Report->SetArrayField(TEXT("mesh_components"), Parts);
        Report->SetNumberField(TEXT("ground_soft_components"), GroundSoftParts.Num());
        FVector Eye;
        FRotator Rotation;
        if (!TestTrue(TEXT("The selected camera is finite and explicit"),
                Eye.InitFromString(Input->GetStringField(TEXT("eye"))) &&
                    Rotation.InitFromString(Input->GetStringField(TEXT("rotation"))) && !Eye.ContainsNaN() &&
                    !Rotation.ContainsNaN()))
            return false;
        const bool WasDirty = World->GetOutermost()->IsDirty();
        TArray<AActor*> TemporaryActors;
        ON_SCOPE_EXIT
        {
            for (AActor* Actor : TemporaryActors)
                if (IsValid(Actor))
                    World->DestroyActor(Actor);
            World->GetOutermost()->SetDirtyFlag(WasDirty);
        };
        ASceneCapture2D* Camera = World->SpawnActor<ASceneCapture2D>();
        TemporaryActors.Add(Camera);
        USceneCaptureComponent2D* Capture = Camera->GetCaptureComponent2D();
        UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>();
        Target->RenderTargetFormat = RTF_RGBA8;
        Target->InitAutoFormat(1280, 720);
        Target->UpdateResourceImmediate(true);
        Capture->TextureTarget = Target;
        Capture->bCaptureEveryFrame = false;
        Capture->bCaptureOnMovement = false;
        Capture->CaptureSource = SCS_FinalColorLDR;
        Capture->FOVAngle = float(Input->GetNumberField(TEXT("fov")));
        Capture->ShowFlags.SetEyeAdaptation(false);
        Capture->PostProcessBlendWeight = 1;
        Capture->PostProcessSettings.bOverride_AutoExposureMinBrightness = true;
        Capture->PostProcessSettings.AutoExposureMinBrightness = 1;
        Capture->PostProcessSettings.bOverride_AutoExposureMaxBrightness = true;
        Capture->PostProcessSettings.AutoExposureMaxBrightness = 1;
        Capture->PostProcessSettings.bOverride_AutoExposureBias = true;
        Capture->PostProcessSettings.AutoExposureBias = 1;
        for (int32 Index = 0; Index < 2; ++Index)
        {
            auto* Light = World->SpawnActor<ADirectionalLight>();
            TemporaryActors.Add(Light);
            Light->SetActorRotation(FRotator(-35, Index ? 210 : 30, 0));
            auto* Component = CastChecked<UDirectionalLightComponent>(Light->GetLightComponent());
            Component->SetMobility(EComponentMobility::Movable);
            Component->SetIntensity(Index ? 2 : 8);
            Component->SetCastShadows(false);
        }
        Camera->SetActorLocation(Eye);
        Camera->SetActorRotation(Rotation);
        const FString Output = Input->GetStringField(TEXT("output"));
        IFileManager::Get().MakeDirectory(*Output, true);
        IImageWrapperModule& Images = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
        if (GShaderCompilingManager)
            GShaderCompilingManager->FinishAllCompilation();
        for (const TCHAR* Mode : {TEXT("wireframe"), TEXT("base-color"), TEXT("lit"), TEXT("ground-soft-hidden")})
        {
            Capture->HiddenComponents.Reset();
            if (FString(Mode) == TEXT("ground-soft-hidden"))
                for (UPrimitiveComponent* Part : GroundSoftParts)
                    Capture->HiddenComponents.Add(Part);
            Capture->ShowFlags.SetWireframe(FString(Mode) == TEXT("wireframe"));
            Capture->CaptureSource = FString(Mode) == TEXT("base-color") ? SCS_BaseColor : SCS_FinalColorLDR;
            World->SendAllEndOfFrameUpdates();
            Capture->CaptureScene();
            FlushRenderingCommands();
            TArray<FColor> Pixels;
            if (!TestTrue(TEXT("The diagnostic render target is readable"),
                    Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels)))
                return false;
            auto JPEG = Images.CreateImageWrapper(EImageFormat::JPEG);
            if (!TestTrue(TEXT("The diagnostic image is retained"),
                    JPEG->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), 1280, 720, ERGBFormat::BGRA, 8) &&
                        FFileHelper::SaveArrayToFile(
                            JPEG->GetCompressed(82), *(Output / (FString(Mode) + TEXT(".jpg"))))))
                return false;
        }
        Capture->TextureTarget = nullptr;
        Target->MarkPendingKill();
        Text.Reset();
        FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Text));
        TestTrue(TEXT("The component facts are retained"),
            FFileHelper::SaveStringToFile(
                Text, *(Output / TEXT("components.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    }
    return !HasAnyErrors();
}
#endif

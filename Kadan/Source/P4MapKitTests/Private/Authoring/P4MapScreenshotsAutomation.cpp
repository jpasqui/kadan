#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Editor.h"
#include "LevelEditorViewport.h"
#include "FileHelpers.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DecalComponent.h"
#include "Materials/MaterialInterface.h"
#include "ContentStreaming.h"
#include "ShaderCompiler.h"
#include "RenderingThread.h"
#include "ImageUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

// Lit pictures of a map that is already in the project, from the real level viewport. The map is
// opened as it is saved: nothing is imported, nothing is saved, and the camera is put back after.
// A diagnostic for people to look at, not a check: run it by name with P4_NATIVE_SCREEN_MAP set.
namespace P4MapScreenshots
{
class FCapture final : public IAutomationLatentCommand
{
    struct FView
    {
        FString Name;
        FVector Location;
        FRotator Rotation;
        EViewModeIndex Mode = VMI_Lit;
        bool NoPost = false;
        bool FixedExposure = false;
    };
    FAutomationTestBase& Test;
    FLevelEditorViewportClient* Client = nullptr;
    FVector OldLocation;
    FRotator OldRotation;
    EViewModeIndex OldMode = VMI_Lit;
    bool OldRealtime = false, OldAxes = false;
    FEngineShowFlags OldFlags = FEngineShowFlags(ESFIM_Editor);
    FExposureSettings OldExposure;
    TArray<FView> Views;
    TArray<TSharedPtr<FJsonValue>> Images;
    FString Out, Map;
    int32 Phase = 0, SettlingDraws = 0;
    double Started = 0, Ready = 0;
    bool Live() const { return GEditor && Client && GEditor->GetLevelViewportClients().Contains(Client); }
    void Restore()
    {
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
    void Save(const FString& Error)
    {
        auto Report = MakeShared<FJsonObject>();
        Report->SetStringField(TEXT("map"), Map);
        Report->SetStringField(TEXT("error"), Error);
        Report->SetArrayField(TEXT("images"), Images);
        FString Json;
        FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
        FFileHelper::SaveStringToFile(
            Json, *(Out / TEXT("screens.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
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
        Client->EngineShowFlags = OldFlags;
        Client->ExposureSettings = OldExposure;
        Client->SetViewMode(View.Mode);
        if (View.NoPost)
            Client->EngineShowFlags.SetPostProcessing(false);
        if (View.FixedExposure)
        {
            Client->ExposureSettings.bFixed = true;
            Client->ExposureSettings.FixedEV100 = 1.f;
        }
        Client->EngineShowFlags.SetGrid(false);
        Client->EngineShowFlags.SetSelectionOutline(false);
        Client->EngineShowFlags.SetModeWidgets(false);
        Client->bDrawAxes = false;
        Client->Invalidate();
        IStreamingManager::Get().StreamAllResources(5.f);
        SettlingDraws = 30;
    }

public:
    explicit FCapture(FAutomationTestBase& InTest) : Test(InTest) {}
    ~FCapture() override { Restore(); }
    bool Setup(const FString& InMap)
    {
        Map = InMap;
        if (!GEditor || GUsingNullRHI)
        {
            Test.AddError(TEXT("Map pictures need D3D and the real level viewport"));
            return false;
        }
        UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(Map);
        if (!World)
        {
            Test.AddError(TEXT("The saved map cannot be opened: ") + Map);
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
            Test.AddError(TEXT("No perspective editor viewport is available"));
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
        // What the map shows: visible static meshes of its visible levels, without sky-sized ones.
        TArray<UStaticMeshComponent*> Meshes;
        TArray<FVector> Origins;
        TArray<AActor*> Objects;
        TArray<FBox> Boxes; // every shown actor, for a level of landscapes only
        for (ULevel* Level : World->GetLevels())
            if (Level && Level->bIsVisible)
                for (AActor* Actor : Level->Actors)
                    if (IsValid(Actor) && !Actor->IsHiddenEd())
                    {
                        FVector BoxOrigin, BoxExtent;
                        Actor->GetActorBounds(true, BoxOrigin, BoxExtent);
                        if (BoxExtent.Size() > 50.f && BoxExtent.Size() < 60000.f)
                            Boxes.Add(FBox(BoxOrigin - BoxExtent, BoxOrigin + BoxExtent));
                        if (Actor->GetClass()->GetPathName().StartsWith(TEXT("/Game/Carrot4/Placeables/")))
                            Objects.Add(Actor);
                        TInlineComponentArray<UStaticMeshComponent*> Parts(Actor);
                        for (UStaticMeshComponent* Mesh : Parts)
                            if (Mesh && Mesh->IsRegistered() && Mesh->IsVisible() && Mesh->GetStaticMesh() &&
                                FMath::IsFinite(Mesh->Bounds.SphereRadius) && Mesh->Bounds.SphereRadius > 50.f &&
                                Mesh->Bounds.SphereRadius < 30000.f)
                            {
                                Meshes.Add(Mesh);
                                Origins.Add(Mesh->Bounds.Origin);
                            }
                    }
        if (Meshes.Num() < 3)
            for (const FBox& Box : Boxes)
                Origins.Add(Box.GetCenter());
        if (Origins.Num() < 1)
        {
            Test.AddError(TEXT("The map shows nothing to take a picture of"));
            Restore();
            return false;
        }
        // The middle 90% of what the map shows, so a far-away piece does not set the overview.
        const auto Middle = [&](int32 Axis, float Fraction)
        {
            TArray<float> Values;
            for (const FVector& Origin : Origins)
                Values.Add(Origin[Axis]);
            Values.Sort();
            return Values[FMath::Clamp(int32(Fraction * (Values.Num() - 1)), 0, Values.Num() - 1)];
        };
        const FVector Low(Middle(0, .05f), Middle(1, .05f), Middle(2, .05f)),
            High(Middle(0, .95f), Middle(1, .95f), Middle(2, .95f));
        const FVector Center = (Low + High) * .5f;
        const float Size = FMath::Max(2000.f, (High - Low).Size());
        const FVector Wide = Center + FVector(-.55f, -.55f, .6f) * Size;
        Views.Add({TEXT("1-overview"), Wide, (Center - Wide).Rotation()});
        // Ground: the large mesh nearest the middle. Objects: the placed game object nearest the middle.
        UStaticMeshComponent* Ground = nullptr;
        float Best = TNumericLimits<float>::Max();
        for (UStaticMeshComponent* Mesh : Meshes)
            if (Mesh->Bounds.SphereRadius > 300.f && Mesh->Bounds.SphereRadius < 4000.f)
            {
                const float Distance = FVector::Dist(Mesh->Bounds.Origin, Center);
                if (Distance < Best)
                {
                    Best = Distance;
                    Ground = Mesh;
                }
            }
        if (!Ground && Boxes.Num())
        {
            const FBox* Largest = &Boxes[0];
            for (const FBox& Box : Boxes)
                if (Box.GetExtent().Size() > Largest->GetExtent().Size())
                    Largest = &Box;
            const FVector Target = Largest->GetCenter(),
                          Camera = Target +
                FVector(-.35f, -.35f, .3f) * FMath::Min(Largest->GetExtent().Size(), 6000.f);
            Views.Add({TEXT("2-ground"), Camera, (Target - Camera).Rotation()});
        }
        if (Ground)
        {
            const FVector Target = Ground->Bounds.Origin,
                          Camera = Target + FVector(-1.1f, -1.1f, 1.f) * Ground->Bounds.SphereRadius;
            Views.Add({TEXT("2-ground"), Camera, (Target - Camera).Rotation()});
        }
        AActor* Object = nullptr;
        Best = TNumericLimits<float>::Max();
        for (AActor* Actor : Objects)
        {
            const float Distance = FVector::Dist(Actor->GetActorLocation(), Center);
            if (Distance < Best)
            {
                Best = Distance;
                Object = Actor;
            }
        }
        if (Object)
        {
            FVector Origin, Extent;
            Object->GetActorBounds(true, Origin, Extent);
            const float Radius = FMath::Clamp(Extent.Size(), 150.f, 1500.f);
            const FVector Camera = Origin + FVector(-1.6f, -1.6f, 1.1f) * Radius;
            Views.Add({TEXT("3-objects"), Camera, (Origin - Camera).Rotation()});
        }
        // P4_NATIVE_SCREEN_FOCUS: comma-separated material names. A view of the first shown actor that
        // draws with each one (a decal's material or a mesh's), to look at a material where the map uses it.
        TArray<FString> Focus;
        FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SCREEN_FOCUS")).ParseIntoArray(Focus, TEXT(","), true);
        for (const FString& Name : Focus)
        {
            AActor* Found = nullptr;
            for (ULevel* Level : World->GetLevels())
                if (Level && Level->bIsVisible && !Found)
                    for (AActor* Actor : Level->Actors)
                        if (IsValid(Actor) && !Actor->IsHiddenEd() && !Found)
                        {
                            TInlineComponentArray<UActorComponent*> Parts(Actor);
                            for (UActorComponent* Part : Parts)
                            {
                                TArray<UMaterialInterface*> Used;
                                if (const UDecalComponent* Decal = Cast<UDecalComponent>(Part))
                                    Used.Add(Decal->GetDecalMaterial());
                                else if (const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Part))
                                    Primitive->GetUsedMaterials(Used);
                                for (const UMaterialInterface* Material : Used)
                                    if (Material && Material->GetName().Contains(Name))
                                    {
                                        Found = Actor;
                                        break;
                                    }
                                if (Found)
                                    break;
                            }
                        }
            if (!Found)
            {
                Test.AddWarning(TEXT("No shown actor draws with ") + Name);
                continue;
            }
            FVector Origin, Extent;
            Found->GetActorBounds(false, Origin, Extent);
            const float Radius = FMath::Clamp(Extent.Size(), 200.f, 3000.f);
            const FVector Camera = Origin + FVector(-1.2f, -1.2f, 1.3f) * Radius;
            Views.Add({TEXT("focus-") + Name, Camera, (Origin - Camera).Rotation()});
            Test.AddInfo(FString::Printf(TEXT("Focus %s: %s at %s"), *Name, *Found->GetName(), *Origin.ToString()));
        }
        if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SCREEN_CONTROLS")) == TEXT("1"))
        {
            const FView Matched = Views[Views.Num() > 1 ? 1 : 0];
            Views.Add({TEXT("4-control-unlit"), Matched.Location, Matched.Rotation, VMI_Unlit});
            Views.Add({TEXT("5-control-lit-no-post"), Matched.Location, Matched.Rotation, VMI_Lit, true});
            Views.Add({TEXT("6-control-lit-fixed-ev1"), Matched.Location, Matched.Rotation, VMI_Lit, false, true});
        }
        Out = FPaths::ProjectSavedDir() / TEXT("P4MapKit/MapScreens") / FPackageName::GetShortName(Map);
        IFileManager::Get().MakeDirectory(*Out, true);
        Started = FPlatformTime::Seconds();
        Ready = Started + 3.;
        Pose();
        return true;
    }
    bool Update() override
    {
        if (!Live())
            return Fail(TEXT("The editor viewport went away during the pictures"));
        const double Now = FPlatformTime::Seconds();
        if (Now - Started > 600.)
            return Fail(TEXT("The pictures took longer than 10 minutes"));
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
            Ready = Now + 0.1;
            return false;
        }
        TArray<FColor> Pixels;
        const FIntPoint Size = Client->Viewport->GetSizeXY();
        if (!Client->Viewport->ReadPixels(Pixels) || Pixels.Num() != Size.X * Size.Y)
            return Fail(TEXT("Editor viewport readback failed"));
        int32 Visible = 0;
        for (FColor& Pixel : Pixels)
        {
            Visible += FMath::Max3(Pixel.R, Pixel.G, Pixel.B) > 24 ? 1 : 0;
            Pixel.A = 255;
        }
        TArray<uint8> PNG;
        FImageUtils::CompressImageArray(Size.X, Size.Y, Pixels, PNG);
        const FView& View = Views[Phase];
        const FString Image = Out / (View.Name + TEXT(".png"));
        if (!FFileHelper::SaveArrayToFile(PNG, *Image))
            return Fail(TEXT("Cannot write the picture"));
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("image"), Image);
        Row->SetStringField(TEXT("camera"), View.Location.ToString());
        Row->SetStringField(TEXT("rotation"), View.Rotation.ToString());
        Row->SetNumberField(TEXT("width"), Size.X);
        Row->SetNumberField(TEXT("height"), Size.Y);
        Row->SetNumberField(TEXT("visible_rgb_pixels"), Visible);
        Row->SetStringField(TEXT("view_mode"), View.Mode == VMI_Unlit ? TEXT("unlit") : TEXT("lit"));
        Row->SetBoolField(TEXT("postprocessing"), Client->EngineShowFlags.PostProcessing);
        Row->SetBoolField(TEXT("lighting"), Client->EngineShowFlags.Lighting);
        Row->SetBoolField(TEXT("direct_lighting"), Client->EngineShowFlags.DirectLighting);
        Row->SetBoolField(TEXT("sky_lighting"), Client->EngineShowFlags.SkyLighting);
        Row->SetStringField(TEXT("exposure"), Client->ExposureSettings.ToString());
        Images.Add(MakeShared<FJsonValueObject>(Row));
        Save(FString());
        Test.AddInfo(TEXT("Map picture: ") + Image);
        if (++Phase < Views.Num())
        {
            Pose();
            Ready = Now + 2.;
            return false;
        }
        Save(FString());
        Restore();
        return true;
    }
};
} //namespace P4MapScreenshots
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MapScreenshots, "P4MapKit.Editor.MapScreenshots",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MapScreenshots::RunTest(const FString&)
{
    const FString Map = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SCREEN_MAP"));
    if (Map.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. Set P4_NATIVE_SCREEN_MAP to a saved map for its pictures"));
        return true;
    }
    const auto Capture = MakeShared<P4MapScreenshots::FCapture>(*this);
    if (!Capture->Setup(Map))
        return false;
    FAutomationTestFramework::Get().EnqueueLatentCommand(Capture);
    return true;
}
#endif

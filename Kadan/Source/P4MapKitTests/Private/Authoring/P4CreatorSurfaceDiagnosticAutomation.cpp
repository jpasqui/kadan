#if P4_MAPKIT_TESTS
#include "P4ExportNavigation.h"
#include "P4MapKitCore/navigation_map_export.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NavMesh/RecastNavMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "Serialization/JsonSerializer.h"

namespace P4ExportNavigationTest
{
bool Meshes(const TArray<uint8>& File, FString& Package, TMap<FString, P4MapKit::NavigationExport::BuiltMesh>& Out);
}

// Opt-in evidence against an actual fresh import. The native export bake and
// independent payload decoder keep display occlusion distinct from missing polygons.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4CreatorSurfaceDiagnostic, "P4MapKit.Quality.CreatorSurfaceDiagnostic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4CreatorSurfaceDiagnostic::RunTest(const FString&)
{
    const FString Map = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_CREATOR_REPORT_MAP"));
    const FString Game = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_CREATOR_REPORT_GAME"));
    if (Map.IsEmpty() || Game.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. No creator surface fixture was requested."));
        return true;
    }
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(Map);
    if (!TestNotNull(TEXT("The fresh imported map opens"), World))
        return false;
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("P4MapKit/CreatorSurfaceDiagnostic");
    IFileManager::Get().MakeDirectory(*Folder, true);
    auto Root = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Components, Settings;
    for (TActorIterator<ARecastNavMesh> It(World); It; ++It)
    {
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("actor"), It->GetPathName());
        Row->SetNumberField(TEXT("draw_offset"), It->DrawOffset);
        Row->SetNumberField(TEXT("cell_size"), It->CellSize);
        Row->SetNumberField(TEXT("cell_height"), It->CellHeight);
        Row->SetNumberField(TEXT("step"), It->AgentMaxStepHeight);
        Row->SetNumberField(TEXT("height"), It->AgentHeight);
        Row->SetNumberField(TEXT("radius"), It->AgentRadius);
        Row->SetNumberField(TEXT("tile_size"), It->TileSizeUU);
        Settings.Add(MakeShared<FJsonValueObject>(Row));
    }
    for (TActorIterator<AActor> It(World); It; ++It)
        for (UStaticMeshComponent* Part : TInlineComponentArray<UStaticMeshComponent*>(*It))
        {
            auto Row = MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("actor"), It->GetName());
            Row->SetStringField(TEXT("class"), It->GetClass()->GetPathName());
            Row->SetStringField(TEXT("component"), Part->GetName());
            Row->SetStringField(TEXT("component_class"), Part->GetClass()->GetPathName());
            UStaticMesh* Mesh = Part->GetStaticMesh();
            Row->SetStringField(TEXT("mesh"), Mesh ? Mesh->GetPathName() : FString());
            Row->SetStringField(TEXT("transform"), Part->GetComponentTransform().ToString());
            Row->SetBoolField(TEXT("navigation_relevant"), Part->IsNavigationRelevant());
            Row->SetBoolField(TEXT("can_affect_navigation"), Part->CanEverAffectNavigation());
            Row->SetNumberField(TEXT("collision_enabled"), int32(Part->GetCollisionEnabled()));
            Row->SetStringField(TEXT("profile"), Part->GetCollisionProfileName().ToString());
            if (Mesh && Mesh->BodySetup)
                Row->SetNumberField(TEXT("trace_flag"), int32(Mesh->BodySetup->CollisionTraceFlag));
            Components.Add(MakeShared<FJsonValueObject>(Row));
        }
    Root->SetArrayField(TEXT("components"), Components);
    Root->SetArrayField(TEXT("editor_navigation"), Settings);
    FP4MapNavigation Made;
    FString Error;
    const FString Content = IFileManager::Get().DirectoryExists(*(Game / TEXT("Carrot4/Content/Carrot4")))
        ? Game / TEXT("Carrot4/Content")
        : Game;
    const bool Built = P4BuildMapNavigation(Map, Content, Made, Error);
    Root->SetBoolField(TEXT("built"), Built);
    Root->SetStringField(TEXT("error"), Error);
    if (Built)
    {
        TestTrue(TEXT("The exact exported navigation bytes are retained"),
            FFileHelper::SaveArrayToFile(Made.Bytes, *(Folder / TEXT("generated.nav"))));
        FString WrittenMap;
        TMap<FString, P4MapKit::NavigationExport::BuiltMesh> Meshes;
        if (TestTrue(TEXT("The actual generated payload independently decodes"),
                P4ExportNavigationTest::Meshes(Made.Bytes, WrittenMap, Meshes)))
        {
            TArray<TSharedPtr<FJsonValue>> Rows;
            for (const auto& Pair : Meshes)
            {
                auto Row = MakeShared<FJsonObject>();
                Row->SetStringField(TEXT("name"), Pair.Key);
                TArray<TSharedPtr<FJsonValue>> Polys;
                for (const auto& Tile : Pair.Value.Tiles)
                    for (size_t P = 0; P < size_t(Tile.Header.OffMeshBase); ++P)
                    {
                        TArray<TSharedPtr<FJsonValue>> Vertices;
                        const auto& Poly = Tile.Polys[P];
                        for (int32 V = 0; V < Poly.VertCount; ++V)
                        {
                            TArray<TSharedPtr<FJsonValue>> XYZ;
                            for (float Value : Tile.Verts[Poly.Verts[V]])
                                XYZ.Add(MakeShared<FJsonValueNumber>(Value));
                            Vertices.Add(MakeShared<FJsonValueArray>(XYZ));
                        }
                        Polys.Add(MakeShared<FJsonValueArray>(Vertices));
                    }
                Row->SetArrayField(TEXT("polygons"), Polys);
                Rows.Add(MakeShared<FJsonValueObject>(Row));
            }
            Root->SetArrayField(TEXT("generated"), Rows);
        }
    }
    FString Text;
    FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Text));
    TestTrue(TEXT("The actual collision and navigation inventory is retained"),
        FFileHelper::SaveStringToFile(
            Text, *(Folder / TEXT("scene.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    TestTrue(TEXT("The normal navigation build completes"), Built);
    if (!Error.IsEmpty())
        AddError(Error);
    return !HasAnyErrors();
}
#endif

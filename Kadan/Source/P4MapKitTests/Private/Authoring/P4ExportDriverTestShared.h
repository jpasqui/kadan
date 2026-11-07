#pragma once
// Shared by the export driver tests (P4Export*Automation.cpp).
#include "P4ExportDriver.h"
#include "P4ExportNavigation.h"
#include "P4GameLighting.h"
#include "P4AuthoringRows.h"
#include "P4ReceiptPanelExport.h"
#include "P4ExportProcess.h"
#include "P4ExportReadbackContext.h"
#include "P4KadanImport.h"
#include "P4CreatorSource.h"
#include "P4KadanCreatorReview.h"
#include "P4KadanSaveMod.h"
#include "P4MaterialInstance.h"
#include "P4NativeCustomCompilerBridge.h"
#include "P4SoundExport.h"
#include "P4MapKitCore/wwise_inventory.h"
#include "Sound/SoundWave.h"
#include "AssetImportTask.h"
#include "Factories/FbxFactory.h"
#include "Factories/FbxImportUI.h"
#include "Factories/FbxStaticMeshImportData.h"
#include "Factories/TextureFactory.h"
#include "PhysicsEngine/BodySetup.h"
#include "P4LevelChanges.h"
#include "P4OriginalLevelImport.h"
#include "P4CollisionHelperDisplay.h"
#include "P4OriginalRecord.h"
#include "P4LevelContent.h"
#include "Engine/PointLight.h"
#include "P4MapKitCore/package_reader.h"
#include "P4MapKitCore/placement_params.h"
#include "P4PlacementExport.h"
#include "P4AddedContent.h"
#include "P4TableExport.h"
#include "P4KadanCreatorReview.h"
#include "P4ExportContainer.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Components/StaticMeshComponent.h"
#include "Components/ChildActorComponent.h"
#include "Curves/CurveFloat.h"
#include "Editor.h"
#include "Engine/LevelStreaming.h"
#include "Engine/LevelStreamingAlwaysLoaded.h"
#include "EditorLevelUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "UObject/UObjectIterator.h"
#include "Engine/Level.h"
#include "Engine/LevelScriptActor.h"
#include "Engine/LevelScriptBlueprint.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_Event.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Self.h"
#include "GameFramework/WorldSettings.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

namespace P4FixtureRetirement
{
    TSet<FName> LivePackages();
    bool Roots(const TArray<UObject*>& Owned, const TArray<UObject*>& Direct);
    bool Package(UPackage* Made);
}

namespace P4SweepCapture
{
    bool Capture(UWorld* World, const FString& Out, const TSharedRef<FJsonObject>& Report);
    void AddSavedDayLevels(UWorld* World,const FString& Map,const FP4KadanImportResult& Imported,
        const TSharedRef<FJsonObject>& Report);
}

namespace P4ExportNavigationTest
{
    bool Meshes(const TArray<uint8>& File, FString& Package, TMap<FString, P4MapKit::NavigationExport::BuiltMesh>& Out);
}

namespace P4ExportDriverTest
{
    // Opt-in lifetime evidence for a kept-map sweep. This does not retain worlds,
    // change their types/flags, or bypass the editor's normal cleanup checks.
    void TraceWorldsBeforeMapCleanup();
    bool QualifiedOriginalFamilyActor(AActor* Actor, const TSet<FName>& Qualified, const TSet<FName>& Omitted);
    // A game material instance of Cave000_F00, when the project has the map imported.
    const TCHAR* const GameParent = TEXT("/Game/Carrot4/Terrain/Cave/Type/Soil/Meshes/CMA_T02_DC_DirtPile01/MI_stg_Soil_Cmn_DirtPile01");
    const TCHAR* const Cave000 = TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave000/Cave000_F00/Cave000_F00_P");

    FString ObjectPathOf(const FString& Package);

    bool KeepImport(FAutomationTestBase& Test, const FString& Name, const FP4KadanImportResult& Import);

    // Each test reuses its own output so an earlier export cannot leave another mod's files behind.
    bool Request(FAutomationTestBase& Test, const FString& Id, const FString& Name, FP4ExportModRequest& Out, FP4ModNames& Names);

    // Actual external inputs pass through Unreal's factory callbacks, as a creator's import does.
    UObject* ImportOwn(FAutomationTestBase& Test, const FString& Root, const FString& Name, bool Mesh);

    // The creator's own content: a texture, a material instance of the game's (when the project has
    // the parent) using it, and a static mesh with collision using the instance. Removed at the end.
    struct FOwn
    {
        TStrongObjectPtr<UTexture2D> Texture;
        TStrongObjectPtr<UMaterialInstanceConstant> Instance;
        TStrongObjectPtr<UStaticMesh> Mesh;
        TArray<UObject*> Made;
        ~FOwn()
        {
            for (UObject* Object : Made)
            {
                if (!IsValid(Object)) continue;
                FAssetRegistryModule::AssetDeleted(Object);
                Object->ClearFlags(RF_Public | RF_Standalone);
                Object->GetOutermost()->SetDirtyFlag(false);
            }
        }
    };
    bool MakeOwn(FAutomationTestBase& Test, const FString& Root, FOwn& Own);

    // A 64x64 TGA imported through Unreal's texture factory, as a creator's import does.
    UTexture2D* ImportTga(FAutomationTestBase& Test, const FString& Root, const FString& Name,
        TFunctionRef<FColor(int32 X, int32 Y)> Pixel, bool Normal);
    // P4ExportPartialChecks.cpp: the cases the coverage tracker lists as partly working, beside the material row
    // (P4_NATIVE_SWEEP_PARTS: lights, textures, skinned, foliage, enemy, comma separated).
    bool AddPartialChecks(FAutomationTestBase& Test, UWorld* World, const FString& Root, const FVector& At,
        const FString& Parts, FP4ExportModRequest& Request, TArray<UObject*>& Made, TArray<FString>& Names,
        TArray<AActor*>& Actors);
    // P4ExportMaterialRow.cpp: a row of creator cubes, one per everyday material made from scratch, placed from At.
    bool AddMaterialRow(FAutomationTestBase& Test, UWorld* World, const FString& Root, const FVector& At,
        TArray<UObject*>& Made, TArray<FString>& Names, TArray<AActor*>& Actors);

    bool CookedNames(const FString& Header, std::vector<std::string>& Names);
    bool Has(const std::vector<std::string>& Names, const char* Name);

    // What an import made, taken out of the editor and off the disk again when the test ends. The project's
    // files before the import are kept in a file while it runs, so that a run cut off is cleaned up by the next.
    TArray<FString> ContentFiles();
    bool RemoveFiles(const TArray<FString>& Files, const TSet<FName>& Preserve = {});
    struct FImported
    {
        FString List;
        TSet<FString> Before;
        TSet<FName> LiveBefore;
        bool Ready = false;
        TArray<FString> NewSince(const TSet<FString>& Files) const
        {
            TArray<FString> Out;
            for (const FString& File : ContentFiles()) if (!Files.Contains(File)) Out.Add(File);
            return Out;
        }
        explicit FImported(const FString& Name) : List(FPaths::ProjectSavedDir() / TEXT("P4MapKit/Tests") / Name / TEXT("before.txt"))
        {
            if (IFileManager::Get().FileExists(*List))
            {
                TArray<FString> Lines;
                if (!FFileHelper::LoadFileToStringArray(Lines, *List)) return;
                if (!RemoveFiles(NewSince(TSet<FString>(Lines)), P4FixtureRetirement::LivePackages())) return;
            }
            LiveBefore = P4FixtureRetirement::LivePackages();
            Before = TSet<FString>(ContentFiles());
            Ready = IFileManager::Get().MakeDirectory(*FPaths::GetPath(List), true) &&
                FFileHelper::SaveStringArrayToFile(Before.Array(), *List, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        }
        ~FImported()
        {
            if (!Ready) return;
            if (GEditor) UEditorLoadingAndSavingUtils::NewBlankMap(false);
            if (RemoveFiles(NewSince(Before), LiveBefore)) IFileManager::Get().Delete(*List, false, true, true);
        }
    };
    TSharedPtr<FJsonObject> ReadJson(const FString& File);
}

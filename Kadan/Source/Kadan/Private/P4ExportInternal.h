// Internal to the mod export (P4ExportMod): the steps of one export run and the helpers its files share.
#pragma once

#include "P4ExportDriver.h"
#include "P4Common.h"
#include "P4GamePaths.h"
#include "P4GameNames.h"
#include "P4ExportProgress.h"
#include "P4ExportPublication.h"
#include "P4ExportWork.h"
#include "P4SwitchShaderMapFreeze.h"
#include "P4StockEngineFieldContent.h"
#include "P4GameMaterialModeCook.h"
#include "P4GamePreviewWorld.h"
#include "P4StockTags.h"
#include "P4GameNativeTails.h"
#include "P4KadanSaveMod.h"
#include "P4KadanGameContent.h"
#include "P4MaterialInstanceCopyContent.h"
#include "P4MaterialInstancePermutation.h"
#include "P4GameReplacement.h"
#include "P4GameMaterialInstanceExport.h"
#include "P4ExportMaps.h"
#include "P4AuthoringRows.h"
#include "P4CollisionSettings.h"
#include "P4DiagnosticCookPackageGuard.h"
#include "P4ExportNavigation.h"
#include "P4ExportProcess.h"
#include "P4ExportReadbackContext.h"
#include "P4MapKitCore/export_contract.h"
#include "P4MapKitCore/export_package.h"
#include "P4NativeHash.h"
#include "P4OriginalRecord.h"
#include "P4CreatorSource.h"
#include "P4CreatorReferencePreflight.h"
#include "P4LevelChanges.h"
#include "P4LevelExport.h"
#include "P4PlacementExport.h"
#include "P4GameLighting.h"
#include "P4LightCookDefaults.h"
#include "P4TableExport.h"
#include "P4AddedContent.h"
#include "P4TextureCookProof.h"
#include "P4CreatorSource.h"
#include "P4NativeCustomCompilerBridge.h"
#include "P4TranslucentMaterialStaging.h"
#include "P4MapKitCore/light_function_export.h"
#include "P4ActorAssetValuePlanning.h"
#include "P4MaterialReferenceContext.h"
#include "P4SoundExport.h"
#include "P4FoliageContent.h"
#include "P4RecoveredParticleSystem.h"
#include "P4TableExport.h"
#include "Sound/SoundWave.h"
#include "AssetRegistryModule.h"
#include "Editor.h"
#include "Components/MeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Curves/CurveLinearColor.h"
#include "Engine/StaticMesh.h"
#include "Engine/DataTable.h"
#include "Engine/Blueprint.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Engine/MapBuildDataRegistry.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimSequence.h"
#include "Engine/Level.h"
#include "Engine/LevelScriptBlueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/Material.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "NavCollision.h"
#include "PhysicsEngine/BodySetup.h"
#include "Serialization/JsonSerializer.h"
#include "StaticMeshResources.h"
#include "StaticParameterSet.h"
#include "UObject/EnumProperty.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace P4Export
{
// A changed value of one of the game's actors, as the framework writes it before the actor begins play:
// a path from the actor, the way the framework's reflection takes one, and a plain value.
using FValues = FP4ActorValues;
using FAssetValueReference = FP4ActorAssetReference;

// What an edit of one game level amounts to, read before anything is written. Nothing of the game's
// level is copied: removals, moves and changed values go into the changes file the framework applies,
// and only added actors go into a level of the mod's own.
struct FLevelEdit
{
    UWorld* World = nullptr;
    FString Package, Level;
    int32 MapIndex = 0;
    // Added: the added actors that go into a level of the mod's own. Placed: the gameplay objects among
    // the added actors, which go into the map's placement list instead.
    TArray<FString> Added, References;
    TArray<AActor*> Placed;
    bool RebuildLighting = false;
    TArray<FAssetValueReference> AssetValues;
    // NavigationOnly: the added links, modifier and bounds volumes, which the map's navigation file
    // holds instead of a level. Navigation: the edit changes where Pikmin can walk, for the reason
    // NavigationWhy names.
    TArray<FString> NavigationOnly;
    bool Navigation = false;
    FString NavigationWhy;
    // The game's gameplay objects moved or removed, which go into changes of the game's placement lists.
    TArray<FP4GameRowEdit> RowEdits;
};
// When ReadRows is set, the moves and removals of the game's gameplay objects that come from rows of the
// game's placement lists (GameRows, null when they were not found) become changes of those rows
// (P4ExportLevelEdits.cpp).
bool LevelChanges(UWorld* World, const FP4LevelChangesResult& Changes, P4MapKit::Export::MapEdit& Map, FLevelEdit& Out,
    FString& Error, bool ReadRows = false, const TArray<FP4GamePlacementRow>* GameRows = nullptr);

// Cooking (P4ExportCook.cpp).
FString CookedBase(const FString& Stage, const FString& Package);
bool NoLinks(const FString& Path, FString& Error);
bool SafeIdLeaf(const FString& Id);
ITargetPlatform* CookTarget();
bool CookTexture(UTexture2D* Texture, const FString& Stage, FString& Error);
bool CookObject(UObject* Asset, const FString& Stage, FString& Error);
// The packages a cooked package names, from its own header.
bool PackagesNamed(const FString& Header, TArray<FString>& Out, FString& Error);
FString MeshIssue(UStaticMesh* Mesh);

// Whether and how an asset can be exported (P4ExportAvailability.cpp).
FP4ExportAvailability AssetAvailability(
    UObject* Asset, const FP4ModDescriptor& Mod, bool CustomMaterials, bool CompareWithGame = true);
} // namespace P4Export

// One mod export (P4ExportMod). Each step is a method; a step that refuses returns false through Fail.
class FP4ExportRun
{
public:
    FP4ExportRun(const FP4ExportModRequest& InRequest, FP4ExportModResult& InOut, FString& InError);
    FP4ExportRun(const FP4ExportRun&) = delete;
    FP4ExportRun& operator=(const FP4ExportRun&) = delete;
    bool Execute();

private:
    struct FReleaseLists
    {
        TArray<TUniquePtr<FP4PlacementList>>& Lists;
        ~FReleaseLists()
        {
            for (auto& List : Lists)
                P4ReleasePlacementList(*List);
        }
    };
    // A lock (the output's or the work folder's), deleted when the run ends.
    struct FRelease
    {
        FRelease(TUniquePtr<FArchive>& InGuard, const FString& InPath) : Guard(InGuard), Path(InPath) {}
        ~FRelease()
        {
            Guard.Reset();
            IFileManager::Get().Delete(*Path);
        }
        TUniquePtr<FArchive>& Guard;
        FString Path;
    };
    // An edited map whose walkable ground changed, and its navigation built again.
    struct FMapNavigationEdit
    {
        bool Built = false;
        FP4MapNavigation Navigation;
        FString Why, Level;
    };
    struct FDiscardLit
    {
        TArray<FP4AddedLitLevel>& Levels;
        ~FDiscardLit()
        {
            for (FP4AddedLitLevel& Level : Levels)
                P4DiscardAddedLitLevel(Level);
        }
    };

    bool Fail(const FString& Problem);
    // The steps, in order (P4ExportDriver.cpp).
    bool Review();
    bool ReadDeclarations();
    bool ReadLevelEdits();
    bool BuildPlacementLists();
    bool PreflightCreatorReferences();
    bool PrepareWorkFolders();
    bool StageMaterials();
    bool BuildNavigation();
    bool ChooseLightingMap();
    bool TellGameMaterialUsage();
    bool ExportAddedLevels();
    bool BuildLighting();
    bool CookPlacementLists();
    bool CookOwnPackages();
    // The creator's cooked levels that none of the others streams: the ones the manifest names for the map.
    bool CreatorRootLevels(const TArray<FString>& Levels, TArray<FString>& Roots);
    // Follows the packages a cooked package names into Own, refusing the ones it may not use.
    bool Follow(const FString& From, const FString& Header);
    // The manifest, the container and publication (P4ExportPackaging.cpp).
    bool WriteManifest();
    bool PackContainer();
    bool WritePreparedFiles();
    bool Publish();

    const FP4ExportModRequest& Request;
    FP4ExportModResult& Out;
    FString& Error;
    FP4ExportModResult Result;
    FP4ModNames Names;
    FP4ExportGameInventory Inventory;
    FP4ExportSelectionPreview Preview;
    TSharedRef<FJsonObject> Tables = MakeShared<FJsonObject>();
    TSharedRef<FJsonObject> Content = MakeShared<FJsonObject>();
    TSet<FString> ContentClasses;
    TArray<FString> ContentDefinitions;
    TArray<FP4ExportMapGroup> Groups;
    std::vector<P4MapKit::Export::MapEdit> Maps;
    TArray<P4Export::FLevelEdit> Edits;
    // Each map has its own saved placement list. All maps share the one mod container.
    TArray<TUniquePtr<FP4PlacementList>> PlacementLists;
    FReleaseLists ReleaseLists{PlacementLists};
    std::map<std::string, std::string> Neutral;
    std::string Why;
    P4MapKit::Export::Descriptor Mod;
    static constexpr bool CustomMaterials = true;
    TArray<FString> Own;
    // The preflight read the game's digests; the export collects no garbage.
    TOptional<FP4GameContentGatherHold> GatherHold;
    FString Target;
    FString Working;
    TUniquePtr<FArchive> Guard;
    TOptional<FRelease> Release;
    // The work folder's lock, held as long as the output's.
    TUniquePtr<FArchive> WorkGuard;
    TOptional<FRelease> WorkRelease;
    FString Prepared;
    FString Stage;
    // The game levels shipped whole (FP4ExportModRequest::WholeLevels).
    TArray<FString> WholeLevels;
    bool CookWholeLevels();
    // Without Tsuchi, an edited game table ships whole at its game path instead of as the manifest's changes.
    bool CookNativeTables();
    FString GameFiles;
    TOptional<FP4SwitchShadersInEditorProcess> SwitchInProcess;
    TOptional<FP4TranslucentMaterialStaging> Materials;
    TSet<FString> SoundSources;
    TSet<FString> Known;
    TArray<FMapNavigationEdit> Navigations;
    bool NeedsLighting = false;
    int32 LightingMap = INDEX_NONE;
    TArray<FP4AddedLitLevel> LitLevels;
    TArray<int32> LitMaps;
    FDiscardLit DiscardLit{LitLevels};
    TArray<UTexture2D*> Textures;
    TArray<USoundWave*> Sounds;
    TArray<uint8> ShaderLibrary;
    FString Metadata;
};

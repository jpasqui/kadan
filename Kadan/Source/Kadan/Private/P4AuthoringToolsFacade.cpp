// The authoring tools' entry points that forward to the importers, builders and readers.

#include "P4AuthoringRows.h"
#include "P4Common.h"
#include "P4GamePaths.h"
#include "P4GameNames.h"
#include "P4OriginalImportSave.h"
#include "DataTableUtils.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"
#include "UObject/TextProperty.h"
#include "P4PlaceActors.h"
#include "P4MapKitCore/quality.h"
#include "ScopedTransaction.h"
#include "P4TextValues.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "UObject/SoftObjectPath.h"
#include "Misc/PackageName.h"
#include "P4NativeHash.h"
#include "Engine/DataAsset.h"
#include "UObject/StrongObjectPtr.h"
#include "P4ImportReferences.h"
#include "UObject/MetaData.h"
#include "Misc/Paths.h"
#include "P4OriginalDecode.h"
#include "P4OriginalMeshStaging.h"
#include "P4StaticMeshBuild.h"
#include "P4TextureBuild.h"
#include "P4TextureContent.h"
#include "P4MaterialGraph.h"
#include "P4MaterialInstance.h"
#include "P4StaticMeshContent.h"
#include "P4LevelContent.h"
#include "P4LightingBake.h"
#include "P4KadanImport.h"
#include "P4NativeValueStaging.h"

FString UP4AuthoringTools::NativeStageOriginalMesh(
    const FString& HeaderFile, const FString& BodyFile, const FString& ExportObject)
{
    return P4StageOriginalMesh(HeaderFile, BodyFile, ExportObject);
}

FString UP4AuthoringTools::NativeBuildOriginalMesh(
    const FString& HeaderFile, const FString& BodyFile, const FString& ExportObject)
{
    return P4BuildOriginalMeshReport(HeaderFile, BodyFile, ExportObject);
}

FString UP4AuthoringTools::NativeBuildOriginalTexture(const FString& HeaderFile, const FString& BodyFile)
{
    return P4BuildOriginalTextureReport(HeaderFile, BodyFile);
}

FString UP4AuthoringTools::NativeImportOriginalTexture(
    const FString& HeaderFile, const FString& BodyFile, bool SaveToDisk)
{
    P4ImportPublication::FImportScope Import;
    return P4ImportOriginalTexture(HeaderFile, BodyFile, SaveToDisk);
}

FString UP4AuthoringTools::NativeOriginalTextureStatus(UTexture* Texture)
{
    return P4OriginalTextureStatus(Texture);
}

FString UP4AuthoringTools::NativeBuildMaterialGraph(const FString& GraphJson, const FString& Package, bool SaveToDisk)
{
    P4ImportPublication::FImportScope Import;
    return P4BuildMaterialGraphReport(GraphJson, Package, SaveToDisk);
}

FString UP4AuthoringTools::NativeMaterialGraphJson(UMaterial* Material)
{
    FString Unsaid;
    return P4MaterialGraphJson(Material, Unsaid);
}

FString UP4AuthoringTools::NativeBuildMaterialInstance(
    const FString& InstanceJson, const FString& Package, bool SaveToDisk)
{
    // The existing game-path adapter reconstructs game instances. A supported instance
    // constructed in the creator's folder must retain its actual creator origin.
    if (Package.StartsWith(TEXT(P4_GAME_ASSET_ROOT "/"), ESearchCase::CaseSensitive))
    {
        P4ImportPublication::FImportScope Import;
        return P4BuildMaterialInstanceReport(InstanceJson, Package, SaveToDisk);
    }
    return P4BuildMaterialInstanceReport(InstanceJson, Package, SaveToDisk);
}

FString UP4AuthoringTools::NativeMaterialInstanceJson(UMaterialInstanceConstant* Instance)
{
    FString Unsaid;
    return P4MaterialInstanceJson(Instance, Unsaid);
}

FString UP4AuthoringTools::NativeImportOriginalLevel(
    const FString& HeaderFile, const FString& BodyFile, bool SaveToDisk)
{
    P4ImportPublication::FImportScope Import;
    return P4ImportOriginalLevel(HeaderFile, BodyFile, SaveToDisk);
}

FString UP4AuthoringTools::NativeOriginalLevelStatus(UWorld* World)
{
    return P4OriginalLevelStatus(World);
}

FString UP4AuthoringTools::NativeAddFoliage(
    UWorld* World, UInstancedStaticMeshComponent* Kind, FVector Location, FRotator Rotation, FVector Scale)
{
    return P4AddFoliage(World, Kind, Location, Rotation, Scale);
}

FString UP4AuthoringTools::NativeEraseFoliage(UWorld* World, FVector Center, float Radius)
{
    return P4EraseFoliage(World, Center, Radius);
}

FString UP4AuthoringTools::NativeWriteActorNavigationCollision(AActor* Actor, const FString& File)
{
    return P4WriteActorNavigationCollision(Actor, File);
}

FString UP4AuthoringTools::NativeOriginalLevelChanges(UWorld* World)
{
    return P4OriginalLevelChanges(World);
}

FString UP4AuthoringTools::NativeCompareOriginalClass(const FString& HeaderFile, const FString& BodyFile)
{
    return P4CompareOriginalClass(HeaderFile, BodyFile);
}

FString UP4AuthoringTools::NativeKadanImportMap(const FString& GameFolder, const FString& MapPackage)
{
    return P4KadanImportMap(GameFolder, MapPackage, true).Report;
}

FString UP4AuthoringTools::NativeKadanImportMapFromContainers(const FString& GameFolder, const FString& MapPackage,
    const FString& GamePaks, const FString& Retoc, const FString& Repak)
{
    FP4KadanImportSettings Settings;
    Settings.GameFolder = GameFolder;
    Settings.GamePaks = GamePaks;
    Settings.Retoc = Retoc;
    Settings.Repak = Repak;
    Settings.AllowGameBackedLevels = true;
    return P4KadanImportMap(Settings, MapPackage, true).Report;
}

FString UP4AuthoringTools::NativeLightingBakeSetup(UWorld* World, const FString& Setup)
{
    return P4LightingBakeSetup(World, Setup);
}
FString UP4AuthoringTools::NativeLightingBakeClear(UWorld* World)
{
    return P4LightingBakeClear(World);
}

FString UP4AuthoringTools::NativeLevelContent(UWorld* World)
{
    return P4LevelContentText(World);
}

FString UP4AuthoringTools::NativeImportOriginalAsset(
    const FString& HeaderFile, const FString& BodyFile, bool SaveToDisk)
{
    P4ImportPublication::FImportScope Import;
    return P4ImportOriginalObjectAsset(HeaderFile, BodyFile, SaveToDisk);
}

FString UP4AuthoringTools::NativeOriginalAssetStatus(UObject* Asset)
{
    return P4OriginalObjectAssetStatus(Asset);
}

FString UP4AuthoringTools::NativeImportOriginalMesh(const FString& HeaderFile, const FString& BodyFile, bool SaveToDisk)
{
    P4ImportPublication::FImportScope Import;
    return P4ImportOriginalMesh(HeaderFile, BodyFile, SaveToDisk);
}

FString UP4AuthoringTools::NativeOriginalMeshStatus(UStaticMesh* Mesh)
{
    return P4OriginalMeshStatus(Mesh);
}

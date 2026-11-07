#pragma once
#include "CoreMinimal.h"
#include "P4MapKitCore/navigation_map_export.h"

class AActor;
class ULevel;
class UWorld;
struct FP4LevelChangesResult;
struct FP4KadanImportSettings;

// An actor whose only part in the map is what it does to the navigation: a link, a modifier volume or
// the bounds the navigation is built in. Export builds what it does into the map's navigation file
// and does not put the actor into a level of the mod's own.
KADAN_API bool P4IsNavigationOnlyActor(const AActor* Actor);

// Whether an edit of one of the game's levels changes where Pikmin can walk: an actor of the game's
// taken out, or an actor added, moved or changed that the navigation build takes into account.
// Why names the first such actor, for the message export gives.
KADAN_API bool P4EditChangesNavigation(const FP4LevelChangesResult& Changes, FString& Why);

struct FP4MapNavigation
{
    // The mod's .nav file for the map (P4MNAVM1).
    TArray<uint8> Bytes;
    // What was done, in sentences for the creator: which of the game's navigation meshes were
    // built again and from which of the editor's.
    TArray<FString> Told;
    // The project cannot build the game's navigation (it has none of the game's agents), so no file was
    // made and the game's navigation stays. Told says so.
    bool Kept = false;
};

// Loads the selected game map for navigation without replacing the creator's open map, with every sublevel the game
// loads for it shown and its Olimar-mode sublevels only in the Olimar-mode build, and converts the
// result into the game's navigation file for that map. The map's own navigation is read from the
// game's cooked map in GameFiles (the extracted Carrot4/Content folder), or converted from the containers
// Containers names when GameFiles does not hold it. The levels shown and hidden for the build are put back as they were.
KADAN_API bool P4BuildMapNavigation(const FString& MapPackage, const FString& GameFiles, FP4MapNavigation& Out, FString& Error,
    const FP4KadanImportSettings* Containers = nullptr);

// The same for any world and the game's navigation given as Stock (ReadStockMap). The world's package
// is left as dirty or clean as it was.
KADAN_API bool P4BakeMapNavigation(UWorld* World, const FString& MapPackage, const std::vector<P4MapKit::NavigationExport::StockActor>& Stock,
    FP4MapNavigation& Out, FString& Error, const TMap<ULevel*, bool>* LevelVariants = nullptr);

// Builds a world's navigation as it is and collects its navigation meshes as the exporter reads them:
// the Olimar-mode ones (Hero in the name) or the others.
KADAN_API bool P4BakeWorldNavigation(UWorld* World, bool Hero, std::vector<P4MapKit::NavigationExport::BakedActor>& Out, FString& Error);

// Whether export can build the navigation of the map open in World, checked without building. Why says
// what the creator does first. The Kadan review shows it before export.
KADAN_API bool P4MapNavigationReady(UWorld* World, const FString& ExtractedGameDirectory, FString& Why);

// Checks the selected map rather than whichever map happens to be open.
KADAN_API bool P4MapNavigationPackageReady(const FString& Package, const FString& ExtractedGameDirectory, FString& Why);

// The path the manifest (mod.json text) names in the "navigation" key of the map's entry, relative to the
// mod folder: maps/<map>.nav for a mod of one map, maps/<N>_<map>.nav for the Nth of several.
KADAN_API bool P4MapNavigationPath(const FString& Manifest, const FString& MapPackage, FString& Relative);

// Writes the navigation file beside a mod's manifest, at the path the manifest names (P4MapNavigationPath).
KADAN_API bool P4WriteMapNavigation(const FString& ModFolder, const FString& Relative, const TArray<uint8>& Bytes, FString& Error);

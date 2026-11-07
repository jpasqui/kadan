#pragma once
#include "CoreMinimal.h"
#include "P4MapKitCore/package_reader.h"
#include <string>
#include <vector>

class UObject;
class UPackage;

// The game's own values for what the game's engine adds to the engine's own classes and structs: members
// it adds (a notify's middle timing, a skeletal mesh level's C4Distance, a particle emitter's flags, a
// Lightmass setting) and choices it adds to an engine enum (a particle module's C4 interpolation). This
// engine has no place for them, so the editor keeps the game's tags as editor-only package metadata on
// the object that has them, at their path ("Notifies[3].MiddleLink", "LODInfo[0].C4Distance",
// "LightmassSettings.bUseStaticDirectionalLightDirectLighting", "bUseOneEmitter"). The cook writes them
// back where the game has them, in the game's order, with references named in the cooked package (an
// import is added for one it lacks), and reads the output again, tag and decoded value, before it counts.

enum class EP4GameStockTag : uint8 { None, Member, Value };

// What a value the import could not hold is: a member the game's engine adds (Member), a choice of an
// engine enum this engine lacks (Value), or neither (None: a reference to an asset not in the project, a
// value this engine has a place for). Decided from this engine's own reflection of the live object.
KADAN_API EP4GameStockTag P4GameStockTagKind(const UObject* Object, const FString& Path, const FString& Json);

struct FP4GameStockTagWanted
{
    FString Path;          // below the object: "Notifies[3].MiddleTriggerTimeOffset"
    FString Json;          // the game's value as the import decoded it
    EP4GameStockTag Kind = EP4GameStockTag::None;
};

// Keeps the game's tags at the wanted paths of the game's export (its tagged list from Start, before End
// of Body) on Object. Owner is the export's class name (for the decoder's archived float rules). Kept lists
// the paths kept. A path whose tag cannot be carried exactly, or whose decoded value is not the one the
// import was told, is left out and told in Problems; nothing is kept for it.
KADAN_API bool P4StoreGameStockTags(UObject* Object, const std::vector<uint8>& Body, const P4MapKit::FPackageInventory& Package,
    std::size_t Start, std::size_t End, const FString& Owner, const TArray<FP4GameStockTagWanted>& Wanted, TArray<FString>& Kept,
    TArray<FString>& Problems);
// After the import's comparison: records what each kept value's place holds now (the array element it is
// in, the engine's own value of an enum choice), so that the cook can tell a creator's edit from the
// import's state. A creator who changes that element or that choice decides it: the game's value is left
// out where the creator changed what it belongs to.
KADAN_API void P4SealGameStockTags(UPackage* Package);
// Removes every kept tag of the package (an import that falls back to keeping the asset for reference).
KADAN_API void P4ClearGameStockTags(UPackage* Package);
KADAN_API int32 P4CountGameStockTags(const UObject* Object);
KADAN_API bool P4ObjectHasGameStockTags(const UObject* Object);
KADAN_API bool P4PackageHasGameStockTags(UPackage* Package);
// Writes every kept tag into the cooked package where its path says, and reads it back.
KADAN_API bool P4TranslateGameStockTagCook(UPackage* Package, std::vector<uint8>& Header, std::vector<uint8>& Body, FString& Error);
// The tags the last translation left out because a creator changed what they belong to, as "Object Path".
KADAN_API TArray<FString> P4GameStockTagsLeftOut();

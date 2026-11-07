#pragma once
#include "CoreMinimal.h"
#include <vector>

class UObject;
class UPackage;

// The native data the game's own classes and some engine classes write after their properties, read
// exactly and written back exactly.
//
// FoliageGridDataAsset (the game's per-level foliage sound grid, UFoliageGridDataAsset::Serialize at
// 01d491d0): after UObject's data, a version byte (the game writes 2) and, unless it is 0, a cell count
// (int32) and each cell as its type byte, its density byte and its height (int16). The project's class of
// it has no place for the cells, so the editor keeps them as editor-only package metadata on the asset
// and the cook writes them back after the asset's properties, read back before they count.
//
// VectorFieldStatic: after UObject's data, its source as bulk data stored inline (UVectorFieldStatic::
// Serialize). The engine's own class holds it: the import fills it from the game's bytes.

struct FP4FoliageGridTail
{
    uint8 Version = 0;
    // Count * 4 bytes: type, density, height (int16, little-endian) of each cell, as the file has them.
    TArray<uint8> Cells;
};
// Reads the native data that follows a FoliageGridDataAsset's properties (from UObject's id flag on).
KADAN_API bool P4ReadFoliageGridTail(const uint8* Data, int64 Size, FP4FoliageGridTail& Out, FString& Error);
KADAN_API void P4WriteFoliageGridTail(const FP4FoliageGridTail& Tail, TArray<uint8>& Out);
KADAN_API FString P4FoliageGridTailLine(const FP4FoliageGridTail& Tail);

struct FP4VectorFieldTail
{
    uint32 Flags = 0;
    int64 Elements = 0;
    TArray<uint8> Payload;
};
// Reads the native data that follows a VectorFieldStatic's properties: UObject's id flag (none) and its
// source bulk data, which has to be stored inline, uncompressed, with nothing after it.
KADAN_API bool P4ReadVectorFieldTail(const uint8* Data, int64 Size, FP4VectorFieldTail& Out, FString& Error);
KADAN_API FString P4VectorFieldTailLine(const FP4VectorFieldTail& Tail);
// Gives a VectorFieldStatic the game's source data.
KADAN_API bool P4FillVectorFieldSource(UObject* Object, const FP4VectorFieldTail& Tail, FString& Error);

// The readers of native data, one for each class whose own Serialize writes data after its properties. An
// object is read by the reader of its exact class, as a subclass's Serialize may write more.
struct FP4NativeTailReader
{
    // The class whose Serialize the reader follows.
    const TCHAR* Class;
    // Whether the editor's class has no place for the data, so it is kept with the asset and written back by
    // the cook.
    bool Kept;
    // Reads the data exactly and describes it in one line.
    bool (*Describe)(const uint8* Data, int64 Size, FString& Line, FString& Error);
    // Gives an imported object the data.
    bool (*Fill)(UObject* Object, const uint8* Data, int64 Size, FString& Error);
    // When the data names another object of the package: where it says which, and the property of the
    // class that holds it. The line of the data then says the object's path.
    int32 (*TailObject)(const uint8* Data, int64 Size) = nullptr;
    const TCHAR* TailObjectProperty = nullptr;
};
KADAN_API TArrayView<const FP4NativeTailReader> P4NativeTailReaders();
// The reader of a class's native data (its path), or null when the class writes none Kadan reads.
KADAN_API const FP4NativeTailReader* P4FindNativeTailReader(const FString& Class);

// The editor-only record of a FoliageGridDataAsset's cells.
KADAN_API bool P4StoreFoliageGridTail(UObject* Object, const FP4FoliageGridTail& Tail, FString& Error);
KADAN_API bool P4StoredFoliageGridTail(const UObject* Object, FP4FoliageGridTail& Out);
KADAN_API bool P4PackageHasGameNativeTails(UPackage* Package);
// Writes every kept native tail into the cooked package after its object's properties, and reads it back.
KADAN_API bool P4TranslateGameNativeTailCook(UPackage* Package, std::vector<uint8>& Header, std::vector<uint8>& Body, FString& Error);

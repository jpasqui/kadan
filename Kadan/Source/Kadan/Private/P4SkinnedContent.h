#pragma once
#include "CoreMinimal.h"
#include "P4MapKitCore/package_reader.h"
#include "P4SkinnedReports.h"

class FJsonObject;

// Skeletons, skinned meshes and animation sequences hold native data after their properties. The one
// importer (P4ImportOriginalObjectAsset) makes the object and gives it the game's properties. What is
// here reads the native data, says it as lines for the comparison of a cook's save with the game's
// file, and gives the object made what the native data holds, as an editable asset:
//
// - a skeleton gets its bones, its id, its retarget sources and its curve names;
// - a skinned mesh gets its material slots, its own bones, its bounds and, for each level of detail,
//   the editor's source model filled from the game's vertex and index buffers. The engine then makes
//   its render data from that model, without its mesh builder, vertex for vertex in the game's order;
// - an animation sequence gets raw tracks that hold, at every frame, the pose the engine's own decoder
//   gives for the game's compressed keys. An editor-only source record restores the exact original
//   compressed stream only while the native source key matches; edits use normal Unreal compression.

// Whether a class's native data is read here.
bool P4SkinnedClass(const FString& ClassPath);

// Re-seed retained source derived data after a cache clear. Only the original native source key
// can use it; edited tracks, skeletons or settings continue through Unreal's normal compressor.
KADAN_API bool P4RestoreSkinnedSource(UObject* Object);

// The native editor archive does not retain skeletal derived render buffers. A verified empty
// placeholder with an existing empty renderer can be restored without changing renderer presence.
// Nullable or populated renderer baselines are refused until a lossless native snapshot is supported.
bool P4CanRestoreSkinnedPlaceholder(UObject* Object);
void P4RestoreSkinnedPlaceholderRender(UObject* Object);

// The native data of one object of a file, a line for each thing it says, with names as text and
// buffers as their size and digest. Made takes platform/compressor data; animation exact-source
// comparison additionally verifies compressed buffers and codec. VertexColors is a mesh's bHasVertexColors. Reference says what an
// object index of the file is. False with Error when the data is not read to its last byte.
bool P4SkinnedNativeLines(const P4MapKit::FPackageInventory& Package, const std::vector<std::uint8_t>& Body, std::size_t Start, std::size_t End,
    const FString& ClassPath, bool VertexColors, const TFunction<FString(int32)>& Reference, TArray<FString>& Lines, TArray<FString>& Made, FString& Error);

// Gives an object that has the file's properties already what the file's native data holds. Report
// takes what was counted and measured.
bool P4FillSkinnedNative(UObject* Object, const P4MapKit::FPackageInventory& Package, const std::vector<std::uint8_t>& Body, std::size_t Start, std::size_t End,
    const TFunction<FString(int32)>& Reference, FJsonObject& Report, FString& Error);

KADAN_API bool P4FillSkinnedNative(UObject* Object, const P4MapKit::FPackageInventory& Package, const std::vector<std::uint8_t>& Body, std::size_t Start, std::size_t End,
    const TFunction<FString(int32)>& Reference, FP4SkinnedFillReport& Report, FString& Error);

// What the line comparison leaves to a measurement: for an animation sequence, the pose of every track
// at every frame as the engine's decoder gives it for the game's keys and for the keys of a cook's save
// of the asset made. The largest differences go to Report. False when one is larger than the kit accepts.
bool P4CompareSkinnedCooked(UObject* Object, const P4MapKit::FPackageInventory& Game, const std::vector<std::uint8_t>& GameBody, std::size_t GameStart, std::size_t GameEnd,
    const P4MapKit::FPackageInventory& Mine, const std::vector<std::uint8_t>& MineBody, std::size_t MineStart, std::size_t MineEnd, FJsonObject& Report, FString& Error);

bool P4CompareSkinnedCooked(UObject* Object, const P4MapKit::FPackageInventory& Game, const std::vector<std::uint8_t>& GameBody, std::size_t GameStart, std::size_t GameEnd,
    const P4MapKit::FPackageInventory& Mine, const std::vector<std::uint8_t>& MineBody, std::size_t MineStart, std::size_t MineEnd, FP4SkinnedCompareReport& Report, FString& Error);

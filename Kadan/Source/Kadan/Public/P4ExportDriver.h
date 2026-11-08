#pragma once
#include "P4ExportSelection.h"
#include "P4ExportContainer.h"

enum class EP4ExportAssetRoute : uint8
{
    Unavailable, CreatorTexture, CreatorStaticMesh, CreatorMaterialInstance, CreatorMaterial, OriginalLevelChanges, OriginalTableFields, CreatorBlueprint, CreatorContent, CreatorLevel, CreatorSound, CreatorSkinned, PreviewObject, CreatorLighting, GameActorBlueprint, CreatorData
};
struct FP4ExportAvailability
{
    EP4ExportAssetRoute Route = EP4ExportAssetRoute::Unavailable;
    FString Reason;
    bool IsAvailable() const { return Route != EP4ExportAssetRoute::Unavailable; }
};
// Cheap UI capability check: no cook, digest, game-container process or diagnostic.
// Export still independently validates the exact selection, edits and final bytes.
KADAN_API FP4ExportAvailability P4ExportAssetAvailability(UObject* Asset,const FP4ModDescriptor& Mod);
// The same without comparing the asset's content with the game's assets, for the panel's live status.
// The first comparison reads every game asset of that kind once, so only Review and export make it.
KADAN_API FP4ExportAvailability P4ExportAssetAvailabilityAtAGlance(UObject* Asset,const FP4ModDescriptor& Mod);
// Why an edit of one of the game's levels cannot be exported, as a sentence naming the actor and the value.
// Empty when it can: removals, moves, added actors and changed values of plain types.
struct FP4LevelChangesResult;
KADAN_API FString P4ExportLevelChangeIssue(UWorld* World, const FP4LevelChangesResult& Changes);
// One of the game's assets cooked whole at its game path under Stage, with the cook the export uses for its kind: a
// level keeping its reference to the game's lighting data, a texture, or any other asset. Base is the cooked file's
// path without its ending (.umap or .uasset, .uexp, and .ubulk when there is bulk data).
KADAN_API bool P4CookGameAssetWhole(UObject* Asset, const FString& Stage, FString& Base, FString& Error);

struct FP4ExportModRequest
{
    FP4ModDescriptor Mod;
    TArray<FP4ExportRoot> Roots;
    FP4ExportContainerTools Tools;
    FString ProjectDirectory, GamePaksDirectory, OutputDirectory, WorkDirectory;
    // A game's edited level routes through actor deltas. The open persistent map is an optional hint.
    // Streaming references resolve each edited level to its map, including several maps in one mod.
    // ExtractedGameDirectory holds the game's extracted packages (Carrot4/Content), read when a class of
    // the project does not say what the game's class says.
    FString MapPackage, ExtractedGameDirectory;
    // Ship each edited game level whole, cooked at its game path, instead of as a list of its changes. Any edit
    // ships, because nothing is translated. A mod that edits the same level replaces this one.
    bool WholeLevels = false;
    // Cook for the game without Tsuchi: everything ships at its game path in one container for the game's own Paks
    // folder (romfs/Carrot4/Content/Paks), which the game mounts by itself. Edited levels and tables ship whole, and
    // there is no manifest. What only Tsuchi does (new kinds, sound banks, navigation swaps) is refused at Review.
    bool Native = false;
    // Optional source archive location. By default this is read beside the
    // extracted game content; only newly compiled creator programs are shipped.
    FString ShaderArchiveFile;
    TMap<FName, FString> OtherPackageClaims;
    // Private connector preparation only. A generated preview world is cooked
    // as a new package; scene registration belongs to the runtime preview owner.
    bool PreviewOnly = false;
    FString PreviewWorldObjectPath;
    // Internal preview bootstrap/nav buffers join the native cook before retoc.
    // Final package identity and game-claim admission still run on the container.
    TFunction<bool(const FString&, TArray<FString>&, FString&)> PreparePreviewPackages;
};
// One row of a placement list Kadan made: the actor it came from, the class and placement number it names,
// and the blobs it carries by name ("AI.Static", ...). A blob the creator left as the class has it is not there.
struct FP4PlacementRow
{
    FString Actor, Class;
    uint64 Id = 0;
    FTransform Transform;
    TMap<FString, TArray<uint8>> Blobs;
};
// A gameplay object of the game's that a level edit moves, changes or removes and that the game builds from a
// row of its placement lists: the actor, the list's package (an AP_ asset of the game's) and the row's number.
// A level change does nothing to such an object; export writes the move or the removal as a change to that row.
struct FP4GameRowEdit
{
    FString Actor, List;
    uint64 Id = 0;
    bool Removed = false;
};
struct FP4ExportModResult
{
    bool Published = false;
    FString Directory;
    // Packages: every package in the container. Levels: the mod's own levels the manifest lists.
    // Placements: the placement lists it lists, and PlacementRows their rows after the mark row.
    TArray<FString> Packages, Levels, Placements, RebuiltLighting, Files, Refused, Told;
    // BakedLighting: the lighting data packages of the mod's lit levels, among Packages. BakedActors: the added
    // actors those levels hold, lit by the game's own lights. UnbakedActors: the added actors exported without
    // baked lighting, each named in Told with the reason.
    TArray<FString> BakedLighting, BakedActors, UnbakedActors;
    // How many lit levels had lighting for the level as a whole made by their build, which was taken out before the cook.
    int32 BakedVolumesTakenOut = 0;
    // The packages at game asset paths that hold the creator's file in place of the game's asset (K9), and the
    // name of the file each was made from, in the same order. They are among Packages.
    TArray<FString> Replacements, ReplacementFiles;
    TArray<FString> ReplacementKinds, ReplacementClasses, ReplacementGeneratedClasses, ReplacementNativeParents;
    uint64 PlacementMark = 0;
    TArray<FP4PlacementRow> PlacementRows;
    // The game's own gameplay objects moved or removed, written as changes to the game's placement lists.
    TArray<FP4GameRowEdit> GameRowEdits;
    // Authored object -> actual cooked object, including generated material MICs.
    TMap<FString, FString> RuntimePaths;
};
// Explicit editor action. Exports the actors added to game levels into the mod's own levels, the gameplay
// objects among them into a placement list of the mod's own, cooks
// those levels and the creator's own packages they and the selection name, packs them and checks the
// final container before publishing. An error never approves a preview or removes the previous output.
KADAN_API bool P4ExportMod(const FP4ExportModRequest& Request, FP4ExportModResult& Out, FString& Error);
// What export makes of a level edit's moves and removals of the game's gameplay objects: the rows of the game's
// placement lists it changes (GameFolder is the extracted game, as ExtractedGameDirectory), and whether the
// level's lighting is rebuilt for what is left. Nothing is written.
KADAN_API bool P4ExportLevelRowEdits(UWorld* World, const FP4LevelChangesResult& Changes, const FString& GameFolder,
    TArray<FP4GameRowEdit>& Out, bool& RebuildLighting, FString& Error);

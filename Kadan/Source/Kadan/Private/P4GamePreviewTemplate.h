#pragma once
#include "CoreMinimal.h"

struct FP4KadanImportSettings;

// The game content a new level's Play is built from. The [PreviewTemplate] section of the plugin's
// Config/DefaultKadan.ini names two of the game's scenes, the two streams kept from the first scene's
// world and the classes of the base it copies. The rest comes from the game's own data: a scene's world,
// parent scene, placement counts and preloaded demo list, the world's placement list and the list's
// assets.
struct FP4GamePreviewTemplate
{
    // The scene whose world gives the settings, navigation, captain and placement counts.
    FString WorldScene;
    FString SceneClass;
    FString World;
    FString ParentScene;
    FString Counts;
    FString CountsClass;
    uint64 CountsVersion = 0;
    FString GeometryStream;
    FString PlayerStream;
    // The scene whose first base and arrival demo the preview copies.
    FString ArrivalScene;
    FString BaseList;
    FString Base;
    FString DemoList;
    FString Demo;
    TArray<FString> BaseClasses;
    // The part a scene's world and scene names share: Area001 of Area001_P and SD_Area001.
    FString WorldToken;
    FString ArrivalToken;
};

// Reads the record and follows the game's data. Every name it returns is a package path, except the
// parent scene and the base classes, which are object paths.
KADAN_API bool P4ReadGamePreviewTemplate(
    const FP4KadanImportSettings& Settings, FP4GamePreviewTemplate& Out, FString& Error);

// What one of the game's scenes links: its world, parent scene (an object path), placement counts and
// preloaded assets.
struct FP4GamePreviewScene
{
    FString Class;
    FString World;
    FString Parent;
    FString Counts;
    TArray<FString> Preloads;
};
KADAN_API bool P4ReadGamePreviewScene(
    const FP4KadanImportSettings& Settings, const FString& Package, FP4GamePreviewScene& Out, FString& Error);

// The class each generator row of a placement asset places, in row order. Empty when the asset has no
// rows or a row can't be read.
KADAN_API bool P4ReadGamePreviewGeneratorClasses(
    const FP4KadanImportSettings& Settings, const FString& Package, TArray<FString>& Out, FString& Error);

// The longest text a world's and its scene's names share, which the game's name-based lookups use.
KADAN_API FString P4GamePreviewNameToken(const FString& WorldName, const FString& SceneName);

// The template's name with its token replaced by the preview's.
KADAN_API FString P4GamePreviewRename(const FString& Name, const FString& From, const FString& To);

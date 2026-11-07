#pragma once
#include "CoreMinimal.h"
#include "P4MapKitCore/game_builds.h"

// The game build a Paks folder holds (core/game_builds.h): the name of the game's own container in it, the engine
// version its containers convert with, and, when its global container is a known build's byte for byte, that build.
struct FP4GameBuildInfo
{
    const P4MapKit::FGameBuild* Build = nullptr;
    const P4MapKit::FGameFamily* Family = nullptr;
    FString Container;
    FString Engine;
    // The game's shader library file name in Carrot4/Content.
    FString ShaderArchive;
};

// Reads what the folder holds. A folder with no known container (empty, missing, a test fixture of the old form)
// gives the Switch family and Carrot4-Switch, which is what Kadan used before it knew other builds; Build is null
// then. Error is set only when the folder holds containers of two families.
KADAN_API bool P4ResolveGameBuild(const FString& Paks, FP4GameBuildInfo& Out, FString& Error);
// The game's own container stem in Paks ("Carrot4-Switch", "pakchunk0-Sage", ...).
KADAN_API FString P4GameContainerIn(const FString& Paks);
// retoc's --version for the containers in Paks ("UE4_26", "UE4_27").
KADAN_API FString P4GameEngineFor(const FString& Paks);
// Every known build's global container, for a check that a folder holds one of them exactly.
KADAN_API bool P4IsKnownGlobalContainer(int64 TocSize, const FString& TocSha256, int64 CasSize, const FString& CasSha256);
// The game's shader library file name for the build in Paks.
KADAN_API FString P4GameShaderArchiveFor(const FString& Paks);
// Whether Folder holds all three files of a known build's own container.
KADAN_API bool P4HasKnownGameContainer(const FString& Folder);
// Why Kadan cannot import from or export for the game build in Paks, or empty when it can. The Switch 2 family is
// refused: its levels' lighting data and its shaders are UE 4.27 and native code that this version does not read.
KADAN_API FString P4GameBuildUnsupported(const FString& Paks);

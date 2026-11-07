#pragma once
#include "CoreMinimal.h"

// Whether a creator's texture, static mesh or sound holds exactly the content of one of the game's assets in the
// project: the same source pixels, the same first level of detail, the same PCM data. GamePackage names that game
// asset. The game's digests are kept in Saved/Kadan/GameContentDigests.json and computed again for a package
// whose files changed, so only the first Review after an import loads the game's assets of that kind.
// A replacement of a game asset at its own path (P4GameReplacement.h) matches when it holds that game asset's
// content again, and every other creator asset matches the content a replacement took the place of.
KADAN_API bool P4FindGameContentMatch(UObject* Asset, FString& GamePackage);
// The digest of the content that is compared: empty for an asset of another kind or without that content.
KADAN_API FString P4GameContentDigest(UObject* Asset);
// Reads the game's digests for every texture, static mesh and sound in Assets, collecting garbage between reads to
// bound memory. Call it where everything in use is held by strong pointers; the caller holds Assets. Later
// comparisons of the same kinds find the digests ready and read nothing.
KADAN_API void P4PrepareGameContentMatches(const TArray<UObject*>& Assets);
// While one exists, comparing content with the game never collects garbage. A comparison of a kind not prepared
// still reads the game's assets, and says so in the log.
struct KADAN_API FP4GameContentGatherHold
{
    FP4GameContentGatherHold();
    ~FP4GameContentGatherHold();
    FP4GameContentGatherHold(const FP4GameContentGatherHold&) = delete;
    FP4GameContentGatherHold& operator=(const FP4GameContentGatherHold&) = delete;
};

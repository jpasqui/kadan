#pragma once
#include "CoreMinimal.h"
// The native menu/dock panel. Registration is idempotent and omitted in commandlets.
KADAN_API void P4RegisterKadanEditor();
// Opens the Kadan panel on a page: 0 Import, 1 Mods.
KADAN_API void P4KadanShowPanel(int32 Page);
void P4UnregisterKadanEditor();

// Resolve only the mod selected by the creator in the Kadan panel.
KADAN_API bool P4KadanActiveCreatorRoot(FString& Root, FString& Error);

struct FP4KadanImportResult;
// UI outcome wording follows the importer's result; kept game references are not export blockers.
// The package an import note names, from a package or an object path.
KADAN_API FString P4KadanNotePackage(const FString& Asset);
KADAN_API FString P4KadanImportCompletionMessage(const FP4KadanImportResult& Result);
// Why a package stayed in the game, following "Not imported because X did not import" to the package
// refused for a reason of its own. Empty when the result records no reason for it.
KADAN_API FString P4KadanStayedInGameReason(const FP4KadanImportResult& Result, const FString& Package);

// Display-only browser data. A container row has no reconstructed header and
// must go through settings-aware acquisition before the importer can use it.
struct FP4KadanBrowserRow
{
    FString Package, Kind;
    int64 Bytes = -1;
};

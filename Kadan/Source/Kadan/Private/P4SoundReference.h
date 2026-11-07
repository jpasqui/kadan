#pragma once
#include "P4OriginalAssetImport.h"

// An unsupported Wwise event can retain its game identity for editor references. This representation
// contains no recovered audio or event payload and never claims equality with the game's data.
// Only a native refusal that names the missing AkAudioEventData object admits this fallback.
KADAN_API FP4OriginalAssetImportResult P4KeepUnsupportedSoundReference(const FP4OriginalAssetImportResult& Refused, bool SaveToDisk);

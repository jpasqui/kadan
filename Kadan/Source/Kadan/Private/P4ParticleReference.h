#pragma once
#include "P4OriginalAssetImport.h"

// Only the named missing game-engine module admits this protected identity reference.
// It resolves dependent assets without claiming an editable or recovered particle effect.
KADAN_API FP4OriginalAssetImportResult P4KeepUnsupportedParticleReference(const FP4OriginalAssetImportResult&, bool SaveToDisk);

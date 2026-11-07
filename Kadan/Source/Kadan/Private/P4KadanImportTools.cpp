#include "P4KadanImportTools.h"
#include "P4OriginalLevelImport.h"
#include "P4OriginalImportSave.h"

FString UP4KadanImportTools::ImportLevelWithGameFallback(const FString& HeaderFile, const FString& BodyFile, bool SaveToDisk)
{
    P4ImportPublication::FImportScope Import;
    FP4OriginalLevelImportResult Result = P4ImportOriginalLevelNative(HeaderFile, BodyFile, SaveToDisk);
    if (!Result.IsValid() && (Result.Error == EP4OriginalLevelImportError::UnsupportedContent ||
        Result.Error == EP4OriginalLevelImportError::BuildFailed || Result.Error == EP4OriginalLevelImportError::CookDiffers))
        Result = P4ImportGameBackedLevelNative(HeaderFile, BodyFile, SaveToDisk, {}, Result.ErrorDetail);
    return P4OriginalLevelImportJson(Result);
}

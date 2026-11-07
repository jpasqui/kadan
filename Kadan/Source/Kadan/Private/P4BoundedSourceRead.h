#pragma once
#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "Serialization/Archive.h"

// Measure and read through the same open file. A changed path cannot enlarge
// the allocation between an earlier path size check and LoadFileToArray.
inline bool P4ReadBoundedSourceFile(const FString& Path, int64 Maximum, TArray<uint8>& Out, FString& Error)
{
    if (Maximum < 0 || Maximum > MAX_int32)
    {
        Error = TEXT("Invalid native source read bound");
        return false;
    }
    TUniquePtr<FArchive> File(IFileManager::Get().CreateFileReader(*Path));
    if (!File || File->IsError())
    {
        Error = TEXT("Cannot open actual native source bytes");
        return false;
    }
    const int64 Size = File->TotalSize();
    if (Size < 0 || Size > Maximum)
    {
        Error = TEXT("Actual native source exceeds its bounded reader");
        return false;
    }
    TArray<uint8> Bytes;
    Bytes.SetNumUninitialized(int32(Size));
    if (Size)
        File->Serialize(Bytes.GetData(), Size);
    if (File->IsError() || File->Tell() != Size || File->TotalSize() != Size || !File->Close())
    {
        Error = TEXT("Actual native source changed or failed during read");
        return false;
    }
    Out = MoveTemp(Bytes);
    return true;
}

#pragma once
#include "CoreMinimal.h"
#include "HAL/FileManager.h"

// What a source file's directory entry says about it: its size and times. A write moves the write time, and a
// replaced file has its own creation time, so a file whose stamp is the one it had when it was read whole still
// holds the bytes read then. The stamp comes from one directory query, without opening the file.
struct FP4KadanSourceStamp
{
    int64 Size = -1;
    FDateTime Created, Modified;

    bool operator==(const FP4KadanSourceStamp& Other) const
    {
        return Size == Other.Size && Created == Other.Created && Modified == Other.Modified;
    }
};

inline bool P4KadanSourceStampOf(const FString& File, FP4KadanSourceStamp& Out)
{
    const FFileStatData Data = IFileManager::Get().GetStatData(*File);
    if (!Data.bIsValid || Data.bIsDirectory || Data.FileSize < 0)
        return false;
    Out.Size = Data.FileSize;
    Out.Created = Data.CreationTime;
    Out.Modified = Data.ModificationTime;
    return true;
}

#pragma once
#include "CoreMinimal.h"

// Identity is persistent project metadata. Selection and export evidence live separately.
struct FP4ModDescriptor
{
    int32 Format = 1;
    FString Id;
    FString Name;
    FString Version;
};

struct FP4ModNames
{
    FString PackageRoot;
    FString Container;
};

KADAN_API bool P4ValidateModDescriptor(const FP4ModDescriptor& Descriptor, FString& Error);
KADAN_API bool P4ModDerivedNames(const FP4ModDescriptor& Descriptor, FP4ModNames& Out, FString& Error);
// A path the mod's own content can have: anywhere under /Game except other mods' folders. Whether the
// asset there is the game's is told by its game tag and the game's package list, never by its path.
KADAN_API bool P4IsModContentPath(const FString& PackageRoot, const FString& Package);
// ProjectDirectory is the directory containing the .uproject. Reads never create directories.
KADAN_API bool P4ReadModDescriptor(const FString& ProjectDirectory, const FString& Id,
    FP4ModDescriptor& Out, FString& Error);
KADAN_API bool P4ListModDescriptors(const FString& ProjectDirectory,
    TArray<FP4ModDescriptor>& Out, FString& Error);
// Expected=nullptr creates only; otherwise the stored descriptor must equal Expected.
// The identity cannot change, and a failed write preserves the previous descriptor.
KADAN_API bool P4WriteModDescriptor(const FString& ProjectDirectory,
    const FP4ModDescriptor& Descriptor, const FP4ModDescriptor* Expected, FString& Error);

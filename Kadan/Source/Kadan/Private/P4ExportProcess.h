#pragma once
#include "CoreMinimal.h"
// First-party native tools only; argv is quoted without launching a command shell.
KADAN_API bool P4RunExportTool(const FString& Executable, const TArray<FString>& Args, double Timeout,
    TArray<uint8>& Out, FString& Error);
// Same bounded, shell-free runner with binary output consumed incrementally.
KADAN_API bool P4RunExportToolStream(const FString& Executable, const TArray<FString>& Args, double Timeout,
    const TFunction<bool(const uint8*,int32,FString&)>& Sink, int64 ByteLimit, FString& Error);
// Stream binary stdout to a new private file. Never truncate an existing file or hard link.
KADAN_API bool P4RunExportToolToFile(const FString& Executable, const TArray<FString>& Args, double Timeout,
    const FString& File, int64 MaximumBytes, FString& Error);
KADAN_API uint64 P4ExportPackageId(const FString& Package);
// Narrow texture exports have no proved legacy fallback files in their companion pak.
bool P4VerifyEmptyExportPak(const FString& Repak,const FString& Utoc,double Timeout,FString& Error);

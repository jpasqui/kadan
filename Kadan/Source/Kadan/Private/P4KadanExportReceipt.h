#pragma once

#include "CoreMinimal.h"
#include "P4ModDescriptor.h"

struct FP4ExportModResult;
class SWidget;

struct KADAN_API FP4KadanExportReceipt
{
    FP4ModDescriptor Mod;
    FString Directory;
    FDateTime PublishedUtc;
    FDateTime TestedUtc;
    TArray<FString> Files;
    TArray<FString> Notes;
    int32 UnbakedActors = 0;
    bool GameTestRecorded = false;
};

KADAN_API bool P4KadanReadExportReceipt(const FString& ProjectDirectory, const FString& Id,
    FP4KadanExportReceipt& Out, FString& Error);
KADAN_API bool P4KadanWriteExportReceipt(const FString& ProjectDirectory,
    const FP4KadanExportReceipt& Receipt, FString& Error);
KADAN_API bool P4KadanPublishedExportReceipt(const FString& ProjectDirectory, const FP4ModDescriptor& Mod,
    const FP4ExportModResult& Result, FP4KadanExportReceipt& Out, FString& Error);

struct FP4KadanReceiptState
{
    FString ProjectDirectory;
    FString SelectedId;
    FString Error;
    FP4KadanExportReceipt Receipt;
    bool HasReceipt = false;
    FSimpleMulticastDelegate Changed;

    void Select(const FString& Id);
    void Published(const FP4ModDescriptor& Mod, const FP4ExportModResult& Result);
};

TSharedRef<SWidget> P4KadanExportReceiptWidget(TSharedRef<FP4KadanReceiptState> State);

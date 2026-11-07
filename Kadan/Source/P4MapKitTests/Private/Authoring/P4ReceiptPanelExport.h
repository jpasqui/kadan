#pragma once
#if P4_MAPKIT_TESTS
#include "P4AuthoringRows.h"
#include "P4ExportDriver.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

// Opt-in real-editor receipts use the same persisted identity, review and export service as
// the panel. Ordinary automation does not write this output or change the current project.
inline bool P4ReceiptPanelExport(FAutomationTestBase& Test, const FString& Kind, const FP4ExportModRequest& Request,
    const FString& Target, const FString& Mesh = FString())
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_KADAN_RECEIPT_KIND")) != Kind)
        return true;
    TSharedPtr<FJsonObject> Identity;
    const FString Saved = UP4AuthoringTools::NativeSaveMod(Request.Mod.Id, Request.Mod.Name, Request.Mod.Version, true);
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Saved), Identity) ||
        !Identity->GetBoolField(TEXT("valid")))
    {
        Test.AddError(Saved);
        return false;
    }
    TArray<FString> Roots;
    for (const FP4ExportRoot& Root : Request.Roots)
        Roots.Add(Root.Path);
    const FString Folder = FPaths::ProjectDir() / TEXT("KadanReceipts");
    const FString Reply = UP4AuthoringTools::NativeExportMod(Request.Mod.Id, FString::Join(Roots, TEXT(",")),
        TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave000/Cave000_F00/Cave000_F00_P"), Request.GamePaksDirectory,
        Folder / TEXT("out"), Folder / TEXT("work"), Request.Tools.Retoc, Request.Tools.Repak,
        Request.ExtractedGameDirectory);
    TSharedPtr<FJsonObject> Export;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Reply), Export))
    {
        Test.AddError(Reply);
        return false;
    }
    const bool Valid = Export->GetBoolField(TEXT("valid")) && Export->GetBoolField(TEXT("published"));
    auto Receipt = MakeShared<FJsonObject>();
    Receipt->SetBoolField(TEXT("valid"), Valid);
    Receipt->SetStringField(TEXT("mod_id"), Request.Mod.Id);
    Receipt->SetStringField(TEXT("producer"), TEXT("UP4AuthoringTools::NativeExportMod"));
    Receipt->SetStringField(TEXT("target"), Target);
    Receipt->SetStringField(TEXT("mesh"), Mesh);
    Receipt->SetObjectField(TEXT("export"), Export);
    FString Text;
    FJsonSerializer::Serialize(Receipt, TJsonWriterFactory<>::Create(&Text));
    if (!IFileManager::Get().MakeDirectory(*Folder, true) ||
        !FFileHelper::SaveStringToFile(
            Text, *(Folder / (Kind + TEXT(".json"))), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        Test.AddError(TEXT("Cannot retain the panel receipt"));
        return false;
    }
    if (!Valid)
        Test.AddError(Reply);
    return Valid;
}
#endif

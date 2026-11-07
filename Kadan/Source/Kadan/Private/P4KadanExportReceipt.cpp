#include "P4KadanExportReceipt.h"
#include "P4Common.h"
#include "P4ExportDriver.h"
#include "P4MapKitCore/export_contract.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
FString ReceiptFile(const FString& ProjectDirectory, const FString& Id)
{
    if (ProjectDirectory.IsEmpty() || !P4MapKit::Export::ValidId(TCHAR_TO_UTF8(*Id)))
    {
        return FString();
    }
    return FPaths::ConvertRelativePathToFull(ProjectDirectory / TEXT("Saved/Kadan/ExportReceipts") / (Id + TEXT(".json")));
}

bool Strings(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, TArray<FString>& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!Object->TryGetArrayField(Field, Values))
    {
        return false;
    }
    for (const auto& Value : *Values)
    {
        FString Text;
        if (!Value || !Value->TryGetString(Text))
        {
            return false;
        }
        Out.Add(MoveTemp(Text));
    }
    return true;
}

TArray<TSharedPtr<FJsonValue>> Values(const TArray<FString>& Strings)
{
    TArray<TSharedPtr<FJsonValue>> Result;
    for (const FString& Text : Strings)
    {
        Result.Add(MakeShared<FJsonValueString>(Text));
    }
    return Result;
}
}

bool P4KadanReadExportReceipt(const FString& ProjectDirectory, const FString& Id,
    FP4KadanExportReceipt& Out, FString& Error)
{
    Error.Empty();
    const FString File = ReceiptFile(ProjectDirectory, Id);
    if (File.IsEmpty())
    {
        Error = TEXT("The export receipt has no valid project or mod identity.");
        return false;
    }
    if (!IFileManager::Get().FileExists(*File))
    {
        return false;
    }
    FString Text;
    TSharedPtr<FJsonObject> Object;
    FP4KadanExportReceipt Receipt;
    int32 Format = 0;
    FString Published;
    FString Tested;
    if (!FFileHelper::LoadFileToString(Text, *File) ||
        !P4ParseJson(Text, Object) || !Object ||
        !Object->TryGetNumberField(TEXT("format"), Format) || Format != 1 ||
        !Object->TryGetStringField(TEXT("id"), Receipt.Mod.Id) || Receipt.Mod.Id != Id ||
        !Object->TryGetStringField(TEXT("name"), Receipt.Mod.Name) ||
        !Object->TryGetStringField(TEXT("version"), Receipt.Mod.Version) ||
        !Object->TryGetStringField(TEXT("directory"), Receipt.Directory) ||
        Receipt.Directory.IsEmpty() || FPaths::IsRelative(Receipt.Directory) ||
        !Object->TryGetStringField(TEXT("published_utc"), Published) ||
        !FDateTime::ParseIso8601(*Published, Receipt.PublishedUtc) ||
        !Object->TryGetBoolField(TEXT("game_test_recorded"), Receipt.GameTestRecorded) ||
        !Object->TryGetNumberField(TEXT("unbaked_actors"), Receipt.UnbakedActors) || Receipt.UnbakedActors < 0 ||
        !Strings(Object, TEXT("files"), Receipt.Files) || !Strings(Object, TEXT("notes"), Receipt.Notes) ||
        !P4ValidateModDescriptor(Receipt.Mod, Error))
    {
        Error = TEXT("The saved export receipt could not be read. Its publication has not been verified by this view.");
        return false;
    }
    if (Receipt.GameTestRecorded && (!Object->TryGetStringField(TEXT("tested_utc"), Tested) ||
        !FDateTime::ParseIso8601(*Tested, Receipt.TestedUtc)))
    {
        Error = TEXT("The saved game test record could not be read. Check the export in the game again.");
        return false;
    }
    Out = MoveTemp(Receipt);
    return true;
}

bool P4KadanWriteExportReceipt(const FString& ProjectDirectory,
    const FP4KadanExportReceipt& Receipt, FString& Error)
{
    const FString File = ReceiptFile(ProjectDirectory, Receipt.Mod.Id);
    if (File.IsEmpty() || Receipt.Directory.IsEmpty() || FPaths::IsRelative(Receipt.Directory) ||
        Receipt.PublishedUtc == FDateTime() || !P4ValidateModDescriptor(Receipt.Mod, Error))
    {
        Error = TEXT("The export receipt has no valid publication or mod identity.");
        return false;
    }
    auto Object = MakeShared<FJsonObject>();
    Object->SetNumberField(TEXT("format"), 1);
    Object->SetStringField(TEXT("id"), Receipt.Mod.Id);
    Object->SetStringField(TEXT("name"), Receipt.Mod.Name);
    Object->SetStringField(TEXT("version"), Receipt.Mod.Version);
    Object->SetStringField(TEXT("directory"), Receipt.Directory);
    Object->SetStringField(TEXT("published_utc"), Receipt.PublishedUtc.ToIso8601());
    Object->SetBoolField(TEXT("game_test_recorded"), Receipt.GameTestRecorded);
    Object->SetStringField(TEXT("tested_utc"), Receipt.TestedUtc.ToIso8601());
    Object->SetNumberField(TEXT("unbaked_actors"), Receipt.UnbakedActors);
    Object->SetArrayField(TEXT("files"), Values(Receipt.Files));
    Object->SetArrayField(TEXT("notes"), Values(Receipt.Notes));
    FString Text;
    const FString Temporary = File + TEXT(".tmp");
    if (!FJsonSerializer::Serialize(Object, TJsonWriterFactory<>::Create(&Text)) ||
        !IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true) ||
        !FFileHelper::SaveStringToFile(Text, *Temporary, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) ||
        !IFileManager::Get().Move(*File, *Temporary, true, true, false, false))
    {
        Error = TEXT("The export receipt could not be saved. Check write access to the project's Saved folder.");
        return false;
    }
    Error.Empty();
    return true;
}

bool P4KadanPublishedExportReceipt(const FString& ProjectDirectory, const FP4ModDescriptor& Mod,
    const FP4ExportModResult& Result, FP4KadanExportReceipt& Out, FString& Error)
{
    if (!Result.Published || Result.Directory.IsEmpty() || Result.Files.Num() == 0)
    {
        Error = TEXT("The export has not been published. The previous receipt is unchanged.");
        return false;
    }
    FP4KadanExportReceipt Receipt;
    Receipt.Mod = Mod;
    Receipt.Directory = FPaths::ConvertRelativePathToFull(Result.Directory);
    Receipt.PublishedUtc = FDateTime::UtcNow();
    Receipt.Files = Result.Files;
    Receipt.Notes = Result.Told;
    Receipt.UnbakedActors = Result.UnbakedActors.Num();
    if (!P4KadanWriteExportReceipt(ProjectDirectory, Receipt, Error))
    {
        return false;
    }
    Out = MoveTemp(Receipt);
    return true;
}

void FP4KadanReceiptState::Select(const FString& Id)
{
    SelectedId = Id;
    Error.Empty();
    HasReceipt = false;
    if (!Id.IsEmpty())
    {
        HasReceipt = P4KadanReadExportReceipt(ProjectDirectory, Id, Receipt, Error);
    }
    Changed.Broadcast();
}

void FP4KadanReceiptState::Published(const FP4ModDescriptor& Mod, const FP4ExportModResult& Result)
{
    FP4KadanExportReceipt Saved;
    if (P4KadanPublishedExportReceipt(ProjectDirectory, Mod, Result, Saved, Error))
    {
        Receipt = MoveTemp(Saved);
        HasReceipt = true;
        SelectedId = Mod.Id;
    }
    Changed.Broadcast();
}

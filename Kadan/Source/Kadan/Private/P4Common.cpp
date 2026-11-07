#include "P4Common.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

bool P4SameText(const FString& A, const FString& B)
{
    return A.Equals(B, ESearchCase::CaseSensitive);
}

bool P4IsSha256Hex(const FString& Text)
{
    if (Text.Len() != 64)
        return false;
    for (const TCHAR C : Text)
        if (!((C >= TEXT('0') && C <= TEXT('9')) || (C >= TEXT('a') && C <= TEXT('f'))))
            return false;
    return true;
}

bool P4LoadFileBytes(const FString& File, std::vector<std::uint8_t>& Out)
{
    TArray<uint8> Bytes;
    if (IFileManager::Get().FileSize(*File) > P4MaxFileBytes ||
        !FFileHelper::LoadFileToArray(Bytes, *File, FILEREAD_Silent))
        return false;
    Out.assign(Bytes.GetData(), Bytes.GetData() + Bytes.Num());
    return true;
}

bool P4ParseJson(const FString& Text, TSharedPtr<FJsonObject>& Out)
{
    return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Out);
}

TSharedPtr<FJsonObject> P4ParseJsonObject(const FString& Text)
{
    TSharedPtr<FJsonObject> Object;
    P4ParseJson(Text, Object);
    return Object;
}

FString P4CondensedJson(const TSharedRef<FJsonObject>& Object)
{
    FString Text;
    FJsonSerializer::Serialize(Object, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
    return Text;
}

FString P4CondensedJson(const TArray<TSharedPtr<FJsonValue>>& Values)
{
    FString Text;
    FJsonSerializer::Serialize(Values, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
    return Text;
}

FString P4CondensedJson(const TSharedPtr<FJsonValue>& Value)
{
    FString Text;
    FJsonSerializer::Serialize(
        Value, FString(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
    return Text;
}

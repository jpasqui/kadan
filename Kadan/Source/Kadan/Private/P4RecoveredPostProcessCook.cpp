#include "P4RecoveredPostProcessCook.h"
#include "P4Common.h"
#include "P4MapKitCore/postprocess_package_cook.h"
#include "Dom/JsonObject.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

bool P4RecoveredPostProcessCookHasFork(
    const std::vector<uint8>& Header, const std::vector<uint8>& Body, bool& Needed, FString& Error)
{
    std::string Why;
    const bool Ok = P4MapKit::PostProcessCook::HasFork(Header, Body, Needed, Why);
    Error = UTF8_TO_TCHAR(Why.c_str());
    return Ok;
}
bool P4TranslateRecoveredPostProcessCook(std::vector<uint8>& Header, std::vector<uint8>& Body,
    const std::vector<uint8>* SourceHeader, const std::vector<uint8>* SourceBody, bool RelocationSafe, FString& Error)
{
    std::string Why;
    const bool Ok = P4MapKit::PostProcessCook::TranslatePackage(
        Header, Body, SourceHeader, SourceBody, RelocationSafe, Why);
    Error = UTF8_TO_TCHAR(Why.c_str());
    return Ok;
}

namespace
{
using P4MapKit::PostProcessCook::FSourceTag;
using P4MapKit::PostProcessCook::FSourceComponent;
using P4MapKit::PostProcessCook::FSourceComponents;
const TCHAR* const OrderFormat = TEXT("p4-postprocess-source-order-v1");
FString Utf8(const std::string& Value)
{
    return UTF8_TO_TCHAR(Value.c_str());
}
std::string Narrow(const FString& Value)
{
    return std::string(TCHAR_TO_UTF8(*Value));
}
TSharedRef<FJsonObject> TagJson(const FSourceTag& Tag)
{
    auto Out = MakeShared<FJsonObject>();
    Out->SetStringField(TEXT("name"), Utf8(Tag.Name));
    Out->SetStringField(TEXT("type"), Utf8(Tag.Type));
    TArray<TSharedPtr<FJsonValue>> Detail;
    for (const auto& Item : Tag.Detail)
        Detail.Add(MakeShared<FJsonValueString>(Utf8(Item)));
    Out->SetArrayField(TEXT("detail"), Detail);
    Out->SetNumberField(TEXT("index"), double(Tag.ArrayIndex));
    Out->SetBoolField(TEXT("guid"), Tag.HasGuid);
    for (const auto& Pair :
        {TPair<const TCHAR*, const std::array<std::uint32_t, 4>*>(TEXT("struct_guid"), &Tag.StructGuid),
            TPair<const TCHAR*, const std::array<std::uint32_t, 4>*>(TEXT("property_guid"), &Tag.PropertyGuid)})
    {
        TArray<TSharedPtr<FJsonValue>> Words;
        for (const auto Word : *Pair.Value)
            Words.Add(MakeShared<FJsonValueNumber>(double(Word)));
        Out->SetArrayField(Pair.Key, Words);
    }
    return Out;
}
bool Word(const TSharedPtr<FJsonValue>& Value, double Limit, double& Out)
{
    if (!Value.IsValid() || !Value->TryGetNumber(Out))
        return false;
    return Out >= 0 && Out <= Limit && Out == FMath::FloorToDouble(Out);
}
bool TagOf(const TSharedPtr<FJsonObject>& In, FSourceTag& Out)
{
    FString Name, Type;
    bool Guid = false;
    const TArray<TSharedPtr<FJsonValue>>* Detail = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Words[2] = {};
    if (!In.IsValid() || !In->TryGetStringField(TEXT("name"), Name) || !In->TryGetStringField(TEXT("type"), Type) ||
        Name.IsEmpty() || Type.IsEmpty() || !In->TryGetArrayField(TEXT("detail"), Detail) ||
        !In->TryGetBoolField(TEXT("guid"), Guid) || !In->TryGetArrayField(TEXT("struct_guid"), Words[0]) ||
        !In->TryGetArrayField(TEXT("property_guid"), Words[1]) || Words[0]->Num() != 4 || Words[1]->Num() != 4)
        return false;
    double Index = 0;
    if (!Word(In->TryGetField(TEXT("index")), 1000000, Index))
        return false;
    Out = FSourceTag();
    Out.Name = Narrow(Name);
    Out.Type = Narrow(Type);
    Out.ArrayIndex = std::size_t(Index);
    Out.HasGuid = Guid;
    for (const auto& Item : *Detail)
    {
        FString Text;
        if (!Item.IsValid() || !Item->TryGetString(Text))
            return false;
        Out.Detail.push_back(Narrow(Text));
    }
    for (int32 Slot = 0; Slot < 4; ++Slot)
    {
        double A = 0, B = 0;
        if (!Word((*Words[0])[Slot], 4294967295.0, A) || !Word((*Words[1])[Slot], 4294967295.0, B))
            return false;
        Out.StructGuid[Slot] = std::uint32_t(A);
        Out.PropertyGuid[Slot] = std::uint32_t(B);
    }
    return true;
}
} //namespace

bool P4RecoveredPostProcessSourceOrder(const std::vector<uint8>& SourceHeader, const std::vector<uint8>& SourceBody,
    const FString& SourceDigest, FString& Json, FString& Error)
{
    Json.Reset();
    std::string Why;
    FSourceComponents Components;
    if (!P4MapKit::PostProcessCook::ReadSourceComponents(SourceHeader, SourceBody, Components, Why))
    {
        Error = Utf8(Why);
        return false;
    }
    Error.Reset();
    if (Components.empty())
        return true;
    if (SourceDigest.Len() != 64)
    {
        Error = TEXT("postprocess-cook-kept-order-source-digest-invalid");
        return false;
    }
    auto Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("format"), OrderFormat);
    Root->SetStringField(TEXT("source"), SourceDigest);
    TArray<TSharedPtr<FJsonValue>> List;
    for (const auto& Pair : Components)
    {
        auto Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("path"), Utf8(Pair.first));
        TArray<TSharedPtr<FJsonValue>> Tags, Members;
        for (const auto& Tag : Pair.second.Tags)
            Tags.Add(MakeShared<FJsonValueObject>(TagJson(Tag)));
        for (const auto& Tag : Pair.second.Members)
            Members.Add(MakeShared<FJsonValueObject>(TagJson(Tag)));
        Item->SetArrayField(TEXT("tags"), Tags);
        Item->SetArrayField(TEXT("members"), Members);
        if (Pair.second.HasSettings)
            Item->SetObjectField(TEXT("settings"), TagJson(Pair.second.Settings));
        List.Add(MakeShared<FJsonValueObject>(Item));
    }
    Root->SetArrayField(TEXT("components"), List);
    if (!FJsonSerializer::Serialize(Root, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json)))
    {
        Json.Reset();
        Error = TEXT("postprocess-cook-kept-order-not-written");
        return false;
    }
    return true;
}

bool P4TranslateRecoveredPostProcessCookWithOrder(std::vector<uint8>& Header, std::vector<uint8>& Body,
    const FString& Json, const FString& SourceDigest, bool RelocationSafe, FString& Error)
{
    TSharedPtr<FJsonObject> Root;
    FString Format, Source;
    const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
    if (!P4ParseJson(Json, Root) || !Root->TryGetStringField(TEXT("format"), Format) || Format != OrderFormat ||
        !Root->TryGetStringField(TEXT("source"), Source) || !Root->TryGetArrayField(TEXT("components"), List))
    {
        Error = TEXT("postprocess-cook-kept-order-unreadable");
        return false;
    }
    if (SourceDigest.Len() != 64 || !Source.Equals(SourceDigest, ESearchCase::CaseSensitive))
    {
        Error = TEXT("postprocess-cook-kept-order-source-differs");
        return false;
    }
    FSourceComponents Components;
    for (const auto& Value : *List)
    {
        const TSharedPtr<FJsonObject> Item = Value.IsValid() ? Value->AsObject() : nullptr;
        FString Path;
        const TArray<TSharedPtr<FJsonValue>>* Tags = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Members = nullptr;
        if (!Item.IsValid() || !Item->TryGetStringField(TEXT("path"), Path) || Path.IsEmpty() ||
            !Item->TryGetArrayField(TEXT("tags"), Tags) || !Item->TryGetArrayField(TEXT("members"), Members))
        {
            Error = TEXT("postprocess-cook-kept-order-unreadable");
            return false;
        }
        FSourceComponent Component;
        for (const auto& Tag : *Tags)
        {
            FSourceTag One;
            if (!TagOf(Tag.IsValid() ? Tag->AsObject() : nullptr, One))
            {
                Error = TEXT("postprocess-cook-kept-order-unreadable");
                return false;
            }
            Component.Tags.push_back(One);
        }
        for (const auto& Tag : *Members)
        {
            FSourceTag One;
            if (!TagOf(Tag.IsValid() ? Tag->AsObject() : nullptr, One))
            {
                Error = TEXT("postprocess-cook-kept-order-unreadable");
                return false;
            }
            Component.Members.push_back(One);
        }
        const TSharedPtr<FJsonObject>* Settings = nullptr;
        if (Item->TryGetObjectField(TEXT("settings"), Settings))
        {
            if (!TagOf(*Settings, Component.Settings))
            {
                Error = TEXT("postprocess-cook-kept-order-unreadable");
                return false;
            }
            Component.HasSettings = true;
        }
        else if (Component.Members.size())
        {
            Error = TEXT("postprocess-cook-kept-order-unreadable");
            return false;
        }
        if (!Components.emplace(Narrow(Path), std::move(Component)).second)
        {
            Error = TEXT("postprocess-cook-kept-order-unreadable");
            return false;
        }
    }
    std::string Why;
    const bool Ok = P4MapKit::PostProcessCook::TranslatePackage(Header, Body, &Components, RelocationSafe, Why);
    Error = Utf8(Why);
    return Ok;
}

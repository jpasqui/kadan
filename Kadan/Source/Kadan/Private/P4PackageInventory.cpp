#include "P4AuthoringRows.h"
#include "P4MapKitCore/package_reader.h"
#include "P4MapKitCore/property_decoder.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"
#include "P4NativeHash.h"
#include "P4OriginalDecode.h"
#include "P4TextureInventory.h"

namespace
{
TArray<TSharedPtr<FJsonValue>> P4InventorySpans(const std::vector<P4MapKit::FInventorySpan>& Spans)
{
    TArray<TSharedPtr<FJsonValue>> Result;
    for (const auto& Span : Spans)
    {
        auto Item = MakeShared<FJsonObject>();
        Item->SetNumberField(TEXT("offset"), double(Span.Offset));
        Item->SetNumberField(TEXT("size"), double(Span.Size));
        Item->SetStringField(TEXT("kind"), UTF8_TO_TCHAR(Span.Kind.c_str()));
        Item->SetBoolField(TEXT("decoded"), Span.Decoded);
        Result.Add(MakeShared<FJsonValueObject>(Item));
    }
    return Result;
}
bool P4ReadInventoryFile(const FString& Path, std::vector<std::uint8_t>& Out)
{
    const int64 Size = IFileManager::Get().FileSize(*Path);
    if (Size < 0 || Size > 64 * 1024 * 1024)
        return false;
    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes, *Path) || Bytes.Num() != Size)
        return false;
    Out.resize(Bytes.Num());
    if (Bytes.Num())
        FMemory::Memcpy(Out.data(), Bytes.GetData(), Bytes.Num());
    return true;
}
bool P4CollectStructCodecs(UStruct* Struct, TMap<FName, UStruct*>& Seen, std::vector<P4MapKit::FMapStructCodec>& Codecs)
{
    if (!Struct)
        return true;
    if (UStruct** Existing = Seen.Find(Struct->GetFName()))
        return *Existing == Struct;
    if (Seen.Num() >= 4096)
        return false;
    Seen.Add(Struct->GetFName(), Struct);
    TArray<FProperty*> Pending;
    for (TFieldIterator<FProperty> Field(Struct); Field; ++Field)
    {
        if (Field->HasAnyPropertyFlags(CPF_Transient))
            continue;
        Pending.Add(*Field);
        if (auto Map = CastField<FMapProperty>(*Field))
        {
            auto Key = CastField<FStructProperty>(Map->KeyProp);
            auto Value = CastField<FStructProperty>(Map->ValueProp);
            if (Key || Value)
                Codecs.push_back({TCHAR_TO_UTF8(*Struct->GetName()), TCHAR_TO_UTF8(*Field->GetName()),
                    Key ? TCHAR_TO_UTF8(*Key->Struct->GetName()) : "",
                    Value ? TCHAR_TO_UTF8(*Value->Struct->GetName()) : "",
                    Key && !!(Key->Struct->StructFlags & STRUCT_SerializeNative),
                    Value && !!(Value->Struct->StructFlags & STRUCT_SerializeNative)});
        }
    }
    for (int32 Index = 0; Index < Pending.Num(); ++Index)
    {
        if (Pending.Num() > 100000)
            return false;
        FProperty* Property = Pending[Index];
        if (auto Child = CastField<FStructProperty>(Property))
        {
            if (!P4CollectStructCodecs(Child->Struct, Seen, Codecs))
                return false;
        }
        else if (auto Array = CastField<FArrayProperty>(Property))
            Pending.Add(Array->Inner);
        else if (auto Set = CastField<FSetProperty>(Property))
            Pending.Add(Set->ElementProp);
        else if (auto Map = CastField<FMapProperty>(Property))
        {
            Pending.Add(Map->KeyProp);
            Pending.Add(Map->ValueProp);
        }
    }
    return true;
}
FString P4OriginalDocumentTypePath(const P4MapKit::FPackageInventory& Inventory, const std::vector<std::uint8_t>& Body)
{
    if (!Inventory.Error.Code.empty() || Inventory.Exports.size() != 1)
        return FString();
    const auto& Export = Inventory.Exports[0];
    std::string Class;
    if (!Inventory.Reference(Export.Class, Class))
        return FString();
    if (Class != "/Script/Engine.DataTable")
        return Class.compare(0, 8, "/Script/") == 0 ? FString(UTF8_TO_TCHAR(Class.c_str())) : FString();
    const auto Tags = P4MapKit::ReadPropertyEnvelopes(
        Body, Inventory.Names, Export.Offset, Export.Offset + Export.Size);
    if (!Tags.Error.Code.empty())
        return FString();
    for (const auto& Tag : Tags.Properties)
        if (Tag.Name == "RowStruct" && Tag.Type == "ObjectProperty" && Tag.Size == 4)
        {
            P4MapKit::FByteReader Reader(Body, Tag.PayloadOffset, Tag.PayloadOffset + Tag.Size);
            int32 Reference = 0;
            std::string Path;
            if (Reader.I32(Reference) && Inventory.Reference(Reference, Path) && Path.compare(0, 8, "/Script/") == 0)
                return FString(UTF8_TO_TCHAR(Path.c_str()));
        }
    return FString();
}
P4MapKit::FDecodedValue P4CodecValues(const std::vector<P4MapKit::FMapStructCodec>& Codecs)
{
    using FValue = P4MapKit::FDecodedValue;
    FValue Values;
    Values.Kind = FValue::EKind::Array;
    for (const auto& Codec : Codecs)
    {
        FValue Item;
        Item.Fields.emplace_back("owner", FValue::Atom(FValue::EKind::String, Codec.Owner));
        Item.Fields.emplace_back("property", FValue::Atom(FValue::EKind::String, Codec.Property));
        Item.Fields.emplace_back("key", FValue::Atom(FValue::EKind::String, Codec.Key));
        Item.Fields.emplace_back("value", FValue::Atom(FValue::EKind::String, Codec.Value));
        Item.Fields.emplace_back(
            "key_native", FValue::Atom(FValue::EKind::Boolean, Codec.KeyNative ? "true" : "false"));
        Item.Fields.emplace_back(
            "value_native", FValue::Atom(FValue::EKind::Boolean, Codec.ValueNative ? "true" : "false"));
        Values.Items.push_back(std::move(Item));
    }
    return Values;
}
} //namespace

void P4ReadOriginalDecode(const FString& HeaderFile, const FString& BodyFile, FP4OriginalDecode& State)
{
    State = FP4OriginalDecode();
    const FString Extension = FPaths::GetExtension(HeaderFile);
    if ((Extension.Equals(TEXT("uasset"), ESearchCase::IgnoreCase) ||
            Extension.Equals(TEXT("umap"), ESearchCase::IgnoreCase)) &&
        P4ReadInventoryFile(HeaderFile, State.Header) &&
        P4ReadInventoryFile(
            BodyFile.IsEmpty() ? FPaths::ChangeExtension(HeaderFile, TEXT("uexp")) : BodyFile, State.Body))
    {
        State.Inventory = P4MapKit::ReadPackage(State.Header, State.Body);
        State.Error = State.Inventory.Error;
        State.SchemaPath = P4OriginalDocumentTypePath(State.Inventory, State.Body);
        if (!P4HashSourceBuffers(State.Header.data(), int64(State.Header.size()), State.Body.data(),
                int64(State.Body.size()), State.Digest))
            State.Error = {0, "source-snapshot-hash-unavailable"};
    }
}

bool P4MapStructCodecs(UStruct* Type, std::vector<P4MapKit::FMapStructCodec>& Codecs)
{
    TMap<FName, UStruct*> Seen;
    Codecs.clear();
    return P4CollectStructCodecs(Type, Seen, Codecs);
}

bool P4CopyOriginalCodecs(FP4OriginalDecode& State)
{
    if (!IsInGameThread())
    {
        State.Error = {0, "schema-discovery-requires-game-thread"};
        return false;
    }
    if (!State.Error.Code.empty())
        return false;
    TMap<FName, UStruct*> Seen;
    State.Codecs.clear();
    UStruct* Type = State.SchemaPath.IsEmpty() ? nullptr : FindObject<UStruct>(nullptr, *State.SchemaPath);
    if (!P4CollectStructCodecs(Type, Seen, State.Codecs))
        State.Error = {0, "ambiguous-or-oversized-struct-codecs"};
    State.CodecsCopied = State.Error.Code.empty();
    return State.Error.Code.empty();
}

FString P4DecodeOriginalValues(FP4OriginalDecode& State)
{
    P4MapKit::FDecodedValue Result, Document;
    auto Error = State.Error;
    if (Error.Code.empty() && !State.CodecsCopied)
        Error = {0, "schema-codecs-not-copied"};
    if (Error.Code.empty())
    {
        P4MapKit::FPropertyDecoder Decoder(State.Inventory, State.Body, 1000000, 64 * 1024 * 1024, State.Codecs);
        Decoder.Document(Document);
        Error = Decoder.Error;
    }
    using FValue = P4MapKit::FDecodedValue;
    Result.Fields.emplace_back("format", FValue::Atom(FValue::EKind::Number, "1"));
    Result.Fields.emplace_back("valid", FValue::Atom(FValue::EKind::Boolean, Error.Code.empty() ? "true" : "false"));
    Result.Fields.emplace_back("editable_import_qualified", FValue::Atom(FValue::EKind::Boolean, "false"));
    Result.Fields.emplace_back("error", FValue::Atom(FValue::EKind::String, Error.Code));
    Result.Fields.emplace_back("error_offset", FValue::Atom(FValue::EKind::Number, std::to_string(Error.Offset)));
    Result.Fields.emplace_back("source_sha256", FValue::Atom(FValue::EKind::String, TCHAR_TO_UTF8(*State.Digest)));
    Result.Fields.emplace_back("map_struct_codecs", P4CodecValues(State.Codecs));
    if (Error.Code.empty())
        Result.Fields.emplace_back("document", std::move(Document));
    // Serialize native numeric tokens directly; FJsonValueNumber uses double.
    const auto Json = P4MapKit::DecodedJson(Result);
    return UTF8_TO_TCHAR(Json.c_str());
}

FString UP4AuthoringTools::NativeDecodeOriginalPackage(const FString& HeaderFile, const FString& BodyFile)
{
    FP4OriginalDecode State;
    P4ReadOriginalDecode(HeaderFile, BodyFile, State);
    P4CopyOriginalCodecs(State);
    return P4DecodeOriginalValues(State);
}

FString UP4AuthoringTools::NativePackageInventory(
    const FString& HeaderFile, const FString& BodyFile, bool DecodeTexturePixels)
{
    auto Result = MakeShared<FJsonObject>();
    Result->SetNumberField(TEXT("format"), 1);
    Result->SetBoolField(TEXT("editable_import_qualified"), false);
    std::vector<std::uint8_t> Header, Body;
    P4MapKit::FPackageInventory Inventory;
    const FString Extension = FPaths::GetExtension(HeaderFile);
    if ((!Extension.Equals(TEXT("uasset"), ESearchCase::IgnoreCase) &&
            !Extension.Equals(TEXT("umap"), ESearchCase::IgnoreCase)) ||
        !P4ReadInventoryFile(HeaderFile, Header) ||
        !P4ReadInventoryFile(BodyFile.IsEmpty() ? FPaths::ChangeExtension(HeaderFile, TEXT("uexp")) : BodyFile, Body))
        Inventory.Error = {0, "missing-oversized-or-unreadable-source"};
    else
        Inventory = P4MapKit::ReadPackage(Header, Body);
    Result->SetBoolField(TEXT("valid"), Inventory.Error.Code.empty());
    Result->SetStringField(TEXT("error"), UTF8_TO_TCHAR(Inventory.Error.Code.c_str()));
    Result->SetNumberField(TEXT("error_offset"), double(Inventory.Error.Offset));
    if (Inventory.Error.Code.empty())
    {
        Result->SetStringField(TEXT("package"), UTF8_TO_TCHAR(Inventory.Package.c_str()));
        Result->SetNumberField(TEXT("flags"), Inventory.Flags);
        Result->SetNumberField(TEXT("header_size"), double(Header.size()));
        Result->SetNumberField(TEXT("body_size"), double(Body.size()));
        Result->SetArrayField(TEXT("header_spans"), P4InventorySpans(Inventory.HeaderSpans));
        Result->SetArrayField(TEXT("body_spans"), P4InventorySpans(Inventory.BodySpans));
        TArray<TSharedPtr<FJsonValue>> Names, Imports, Exports, Sidecars;
        for (const auto& Name : Inventory.Names)
            Names.Add(MakeShared<FJsonValueString>(UTF8_TO_TCHAR(Name.c_str())));
        for (const auto& Import : Inventory.Imports)
        {
            auto Item = MakeShared<FJsonObject>();
            Item->SetStringField(TEXT("class_package"), UTF8_TO_TCHAR(Import.ClassPackage.c_str()));
            Item->SetStringField(TEXT("class"), UTF8_TO_TCHAR(Import.Class.c_str()));
            Item->SetStringField(TEXT("name"), UTF8_TO_TCHAR(Import.Name.c_str()));
            Item->SetNumberField(TEXT("outer"), Import.Outer);
            Imports.Add(MakeShared<FJsonValueObject>(Item));
        }
        for (const auto& Export : Inventory.Exports)
        {
            auto Item = MakeShared<FJsonObject>();
            Item->SetStringField(TEXT("name"), UTF8_TO_TCHAR(Export.Name.c_str()));
            Item->SetNumberField(TEXT("class_index"), Export.Class);
            Item->SetNumberField(TEXT("super_index"), Export.Super);
            Item->SetNumberField(TEXT("template_index"), Export.Template);
            Item->SetNumberField(TEXT("outer"), Export.Outer);
            Item->SetNumberField(TEXT("flags"), Export.Flags);
            Item->SetNumberField(TEXT("package_flags"), Export.PackageFlags);
            Item->SetNumberField(TEXT("start"), double(Export.Offset));
            Item->SetNumberField(TEXT("end"), double(Export.Offset + Export.Size));
            Item->SetNumberField(TEXT("first_dependency"), Export.FirstDependency);
            Item->SetNumberField(TEXT("is_asset"), Export.IsAsset);
            Item->SetNumberField(TEXT("not_always_loaded"), Export.NotAlwaysLoaded);
            TArray<TSharedPtr<FJsonValue>> Counts, Filters, Guid;
            for (const auto Count : Export.DependencyCounts)
                Counts.Add(MakeShared<FJsonValueNumber>(double(Count)));
            for (const auto Filter : Export.Filters)
                Filters.Add(MakeShared<FJsonValueNumber>(Filter));
            for (const auto Word : Export.Guid)
                Guid.Add(MakeShared<FJsonValueNumber>(Word));
            Item->SetArrayField(TEXT("dependency_counts"), Counts);
            Item->SetArrayField(TEXT("filters"), Filters);
            Item->SetArrayField(TEXT("guid_words"), Guid);
            std::string Path;
            Inventory.Reference(Export.Class, Path);
            Item->SetStringField(TEXT("class_path"), UTF8_TO_TCHAR(Path.c_str()));
            const auto Properties = P4MapKit::ReadPropertyEnvelopes(
                Body, Inventory.Names, Export.Offset, Export.Offset + Export.Size);
            Item->SetBoolField(TEXT("tag_envelopes_valid"), Properties.Error.Code.empty());
            Item->SetStringField(TEXT("tag_error"), UTF8_TO_TCHAR(Properties.Error.Code.c_str()));
            Item->SetNumberField(TEXT("tags_end"), double(Properties.End));
            TArray<TSharedPtr<FJsonValue>> Tags;
            for (const auto& Property : Properties.Properties)
            {
                auto Tag = MakeShared<FJsonObject>();
                Tag->SetStringField(TEXT("name"), UTF8_TO_TCHAR(Property.Name.c_str()));
                Tag->SetStringField(TEXT("type"), UTF8_TO_TCHAR(Property.Type.c_str()));
                Tag->SetNumberField(TEXT("tag_offset"), double(Property.TagOffset));
                Tag->SetNumberField(TEXT("offset"), double(Property.PayloadOffset));
                Tag->SetNumberField(TEXT("size"), double(Property.Size));
                Tag->SetNumberField(TEXT("array_index"), double(Property.ArrayIndex));
                Tag->SetBoolField(TEXT("payload_decoded"), false);
                TArray<TSharedPtr<FJsonValue>> Detail, StructGuid, PropertyGuid;
                for (const auto& Name : Property.Detail)
                    Detail.Add(MakeShared<FJsonValueString>(UTF8_TO_TCHAR(Name.c_str())));
                for (const auto Word : Property.StructGuid)
                    StructGuid.Add(MakeShared<FJsonValueNumber>(Word));
                for (const auto Word : Property.PropertyGuid)
                    PropertyGuid.Add(MakeShared<FJsonValueNumber>(Word));
                Tag->SetArrayField(TEXT("detail"), Detail);
                Tag->SetBoolField(TEXT("bool_value"), Property.BoolValue);
                Tag->SetBoolField(TEXT("has_guid"), Property.HasGuid);
                Tag->SetArrayField(TEXT("struct_guid_words"), StructGuid);
                Tag->SetArrayField(TEXT("property_guid_words"), PropertyGuid);
                Tags.Add(MakeShared<FJsonValueObject>(Tag));
            }
            Item->SetArrayField(TEXT("tags"), Tags);
            const auto Texture = P4TexturePayloadInventory(
                Inventory, Export, Properties, Body, HeaderFile, DecodeTexturePixels);
            if (Texture.IsValid())
                Item->SetObjectField(TEXT("texture_payload"), Texture);
            Exports.Add(MakeShared<FJsonValueObject>(Item));
        }
        for (const TCHAR* Suffix : {TEXT("ubulk"), TEXT("uptnl"), TEXT("m.ubulk")})
        {
            const int64 Size = IFileManager::Get().FileSize(*FPaths::ChangeExtension(HeaderFile, Suffix));
            if (Size < 0)
                continue;
            auto Item = MakeShared<FJsonObject>();
            Item->SetStringField(TEXT("kind"), Suffix);
            // String token retains exact lengths without the JSON-double boundary.
            Item->SetStringField(TEXT("size"), FString::Printf(TEXT("%lld"), Size));
            Item->SetBoolField(TEXT("decoded"), false);
            Sidecars.Add(MakeShared<FJsonValueObject>(Item));
        }
        Result->SetArrayField(TEXT("names"), Names);
        Result->SetArrayField(TEXT("imports"), Imports);
        Result->SetArrayField(TEXT("exports"), Exports);
        Result->SetArrayField(TEXT("sidecars"), Sidecars);
    }
    FString Json;
    FJsonSerializer::Serialize(Result, TJsonWriterFactory<>::Create(&Json));
    return Json;
}

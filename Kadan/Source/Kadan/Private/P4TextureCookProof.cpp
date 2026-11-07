#include "P4TextureCookProof.h"
#include "P4Common.h"
#include "P4TextureInventory.h"
#include "P4TextureProperties.h"
#include "P4NativeHash.h"
#include "P4MapKitCore/export_contract.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Policies/CondensedJsonPrintPolicy.h"

namespace
{
std::string Typed(const P4MapKit::FDecodedValue& V)
{
    const auto Q = P4MapKit::Export::Quote;
    std::string Result = std::to_string(int(V.Kind)) + ":" + Q(V.Text) + Q(V.Type) + Q(V.EnumType) + Q(V.NameBase) +
        ":" + std::to_string(V.NameNumber) + Q(V.AssetPath) + Q(V.SubPath) + (V.EnumNumeric ? "1" : "0");
    std::map<std::string, const P4MapKit::FDecodedValue*> Fields;
    for (const auto& Field : V.Fields)
        Fields.emplace(Field.first, &Field.second);
    Result += "{";
    for (const auto& Field : Fields)
        Result += Q(Field.first) + ":" + Typed(*Field.second) + ",";
    Result += "}[";
    for (const auto& Item : V.Items)
        Result += Typed(Item) + ",";
    return Result + "]";
}
FString Canonical(const TSharedPtr<FJsonValue>& Value)
{
    if (!Value.IsValid())
        return FString();
    if (Value->Type == EJson::Object)
    {
        const auto Object = Value->AsObject();
        TArray<FString> Keys;
        Object->Values.GetKeys(Keys);
        Keys.Sort();
        FString Result = TEXT("{");
        bool First = true;
        for (const FString& Key : Keys)
        {
            auto Wrapper = MakeShared<FJsonObject>();
            Wrapper->SetStringField(TEXT("key"), Key);
            FString Encoded = P4CondensedJson(Wrapper);
            const FString Quoted = Encoded.Mid(7, Encoded.Len() - 8);
            if (!First)
                Result += TEXT(",");
            First = false;
            Result += Quoted + TEXT(":") + Canonical(Object->Values[Key]);
        }
        return Result + TEXT("}");
    }
    if (Value->Type == EJson::Array)
    {
        FString Result = TEXT("[");
        bool First = true;
        for (const auto& Item : Value->AsArray())
        {
            if (!First)
                Result += TEXT(",");
            First = false;
            Result += Canonical(Item);
        }
        return Result + TEXT("]");
    }
    auto Wrapper = MakeShared<FJsonObject>();
    Wrapper->SetField(TEXT("v"), Value);
    FString Encoded = P4CondensedJson(Wrapper);
    return Encoded.Mid(5, Encoded.Len() - 6);
}
bool Read(const FString& File, std::string& Properties, TSharedPtr<FJsonValue>& Payload, FString& Error)
{
    TArray<uint8> Header, Body;
    const FString BodyFile = FPaths::ChangeExtension(File, TEXT("uexp"));
    for (const FString& Part : {File, BodyFile})
    {
        const int64 Size = IFileManager::Get().FileSize(*Part);
        if (Size < 0 || Size > 64 * 1024 * 1024)
        {
            Error = TEXT("Texture proof input is missing or exceeds 64 MiB");
            return false;
        }
    }
    if (!FFileHelper::LoadFileToArray(Header, *File) || !FFileHelper::LoadFileToArray(Body, *BodyFile))
    {
        Error = TEXT("Texture proof bytes are unreadable");
        return false;
    }
    const std::vector<std::uint8_t> H(Header.GetData(), Header.GetData() + Header.Num()),
        B(Body.GetData(), Body.GetData() + Body.Num());
    const auto Package = P4MapKit::ReadPackage(H, B);
    if (!Package.Error.Code.empty() || Package.Exports.size() != 1)
    {
        Error = TEXT("Texture proof expects exactly one well-formed export");
        return false;
    }
    const auto& Export = Package.Exports[0];
    std::string Class;
    if (!Package.Reference(Export.Class, Class) || Class != "/Script/Engine.Texture2D")
    {
        Error = TEXT("Texture proof received another asset kind");
        return false;
    }
    const auto Tags = P4MapKit::ReadPropertyEnvelopes(B, Package.Names, Export.Offset, Export.Offset + Export.Size);
    FP4TexturePropertyDocument Document;
    if (!P4InspectTextureProperties(Package, Export, Tags, B, UTexture2D::StaticClass(), Document, Error) ||
        !Document.TaggedPropertiesQualified)
        return false;
    P4MapKit::FDecodedValue Decoded;
    P4MapKit::FPropertyDecoder Decoder(Package, B);
    auto Position = Export.Offset;
    if (!Decoder.TypedProperties(Position, Export.Offset + Export.Size, Decoded) || Position != Tags.End)
    {
        Error = TEXT("Actual texture properties did not decode with exact typed identities");
        return false;
    }
    Properties = Typed(Decoded);
    auto Native = P4TexturePayloadInventory(Package, Export, Tags, B, File, false);
    bool Valid = false;
    if (!Native.IsValid() || !Native->TryGetBoolField(TEXT("metadata_valid"), Valid) || !Valid)
    {
        FString Code = TEXT("inventory-unavailable");
        double Offset = double(Tags.End);
        if (Native.IsValid())
        {
            Native->TryGetStringField(TEXT("error"), Code);
            Native->TryGetNumberField(TEXT("error_offset"), Offset);
        }
        // Report only the first serialized format marker. This probe does
        // not replace any of the complete payload validation above.
        std::string Format;
        P4MapKit::FByteReader Probe(B, Tags.End, Export.Offset + Export.Size);
        int32 HasGuid = 0, Cooked = 0;
        uint64 Word = 0;
        bool Prefix = Probe.I32(HasGuid) && (HasGuid == 0 || HasGuid == 1);
        if (Prefix && HasGuid)
            for (int32 Index = 0; Index < 4; ++Index)
                Prefix = Prefix && Probe.Unsigned(4, Word);
        Prefix = Prefix && Probe.Unsigned(4, Word) && Probe.I32(Cooked) && Cooked == 1 &&
            Probe.Name(Package.Names, Format);
        const FString FirstFormat = Prefix ? FString(UTF8_TO_TCHAR(Format.c_str())) : TEXT("unreadable");
        const FString PackageName = UTF8_TO_TCHAR(Package.Package.c_str());
        Error = FString::Printf(
            TEXT(
                "Actual cooked texture payload did not decode completely: %s; format=%s; %s at body offset %.0f; header=%d, body=%d, ubulk=%lld, uptnl=%lld, mapped=%lld bytes"),
            *PackageName.Left(512), *FirstFormat.Left(64), *Code.Left(128), Offset, Header.Num(), Body.Num(),
            IFileManager::Get().FileSize(*FPaths::ChangeExtension(File, TEXT("ubulk"))),
            IFileManager::Get().FileSize(*FPaths::ChangeExtension(File, TEXT("uptnl"))),
            IFileManager::Get().FileSize(*FPaths::ChangeExtension(File, TEXT("m.ubulk"))));
        return false;
    }
    // Packing moves byte ranges and changes their storage flags. Retain all content fields.
    for (const TCHAR* Key :
        {TEXT("start"), TEXT("end"), TEXT("error_offset"), TEXT("property_inventory"), TEXT("property_error")})
        Native->RemoveField(Key);
    const TArray<TSharedPtr<FJsonValue>>* Platforms = nullptr;
    if (!Native->TryGetArrayField(TEXT("platforms"), Platforms) || !Platforms->Num())
    {
        Error = TEXT("Texture has no verified mip payload");
        return false;
    }
    for (const auto& PlatformValue : *Platforms)
    {
        auto Platform = PlatformValue->AsObject();
        for (const TCHAR* Key : {TEXT("start"), TEXT("end"), TEXT("serialized_skip_offset")})
            Platform->RemoveField(Key);
        const TArray<TSharedPtr<FJsonValue>>* Mips = nullptr;
        if (!Platform->TryGetArrayField(TEXT("mips"), Mips) || !Mips->Num())
        {
            Error = TEXT("Texture platform has no mips");
            return false;
        }
        for (const auto& MipValue : *Mips)
        {
            auto Mip = MipValue->AsObject();
            FString Digest;
            if (!Mip->TryGetStringField(TEXT("sha256"), Digest) || Digest.Len() != 64)
            {
                Error = TEXT("Texture mip bytes were not verified");
                return false;
            }
            for (const TCHAR* Key : {TEXT("bulk_flags"), TEXT("serialized_offset"), TEXT("storage"), TEXT("offset")})
                Mip->RemoveField(Key);
        }
    }
    Payload = MakeShared<FJsonValueObject>(Native);
    return true;
}
} //namespace
bool P4VerifyCookedTextureContent(const FString& Expected, const FString& Actual, FString& Error)
{
    std::string AProperties, BProperties;
    TSharedPtr<FJsonValue> APayload, BPayload;
    if (!Read(Expected, AProperties, APayload, Error))
    {
        UE_LOG(LogTemp, Warning, TEXT("Kadan source texture proof failed: %s; %s"), *Expected, *Error);
        Error = TEXT("Cooked source texture: ") + Error;
        return false;
    }
    if (!Read(Actual, BProperties, BPayload, Error))
    {
        UE_LOG(LogTemp, Warning, TEXT("Kadan packed texture proof failed: %s; %s"), *Actual, *Error);
        Error = TEXT("Packed texture readback: ") + Error;
        return false;
    }
    if (AProperties != BProperties || !P4ExactNativeString(Canonical(APayload), Canonical(BPayload)))
    {
        Error = TEXT("Packed texture properties or actual compressed mip bytes differ from the verified cook");
        return false;
    }
    Error.Reset();
    return true;
}

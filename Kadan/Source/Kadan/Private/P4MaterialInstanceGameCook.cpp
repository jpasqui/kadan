#include "P4MaterialInstanceGameCook.h"
#include "P4MaterialInstanceGameSettings.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Dom/JsonObject.h"
#include "P4MapKitCore/postprocess_cook.h"
#include "P4MapKitCore/cooked_export_rebuild.h"
#include "UObject/UObjectHash.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace
{
TMap<FString, TSharedPtr<FJsonObject>> Records(UPackage* Package)
{
    TMap<FString, TSharedPtr<FJsonObject>> Out;
    TArray<UObject*> Objects;
    GetObjectsWithOuter(Package, Objects, false);
    for (UObject* Object : Objects)
    {
        if (const auto* Settings = UP4MaterialInstanceGameSettings::Find(Cast<UMaterialInstanceConstant>(Object)))
        {
            const auto Local = Settings->Records();
            if (Local->Values.Num())
            {
                Out.Add(Object->GetPathName(Package), Local);
            }
        }
    }
    return Out;
}
} // namespace
bool P4PackageHasMaterialInstanceC4(UPackage* Package)
{
    return Package && Records(Package).Num() > 0;
}

bool P4TranslateMaterialInstanceC4Cook(
    UPackage* Package, std::vector<uint8>& Header, std::vector<uint8>& Body, FString& Error)
{
    using namespace P4MapKit::PostProcessCook::Detail;
    using Bytes = std::vector<uint8>;
    const auto Wanted = Records(Package);
    if (!Wanted.Num())
    {
        Error.Reset();
        return true;
    }
    const auto Native = P4MapKit::ReadPackage(Header, Body);
    if (!Native.Error.Code.empty())
    {
        Error = UTF8_TO_TCHAR(Native.Error.Code.c_str());
        return false;
    }
    auto Names = Native.Names;
    std::map<std::size_t, Bytes> Replacements;
    for (std::size_t Index = 0; Index < Native.Exports.size(); ++Index)
    {
        std::string Path;
        if (!Native.Reference(int32(Index + 1), Path))
        {
            continue;
        }
        const auto* Local = Wanted.Find(UTF8_TO_TCHAR(Path.c_str()));
        if (!Local)
        {
            continue;
        }
        const auto& Export = Native.Exports[Index];
        const auto Tags = P4MapKit::ReadPropertyEnvelopes(
            Body, Native.Names, Export.Offset, Export.Offset + Export.Size);
        if (!Tags.Error.Code.empty())
        {
            Error = TEXT("C4-instance-cook-properties-unreadable");
            return false;
        }
        // Serialize all current C4 local flags and values, including disabled flags,
        // false values and explicit cooked omissions. No source property bytes are used.
        Bytes Extra;
        TArray<FString> Keys;
        (*Local)->Values.GetKeys(Keys);
        Keys.Sort();
        for (const FString& Key : Keys)
        {
            const auto Record = (*Local)->GetObjectField(Key);
            P4MapKit::PostProcessCook::FSourceTag Flag;
            Flag.Name = TCHAR_TO_UTF8(*(TEXT("bOverride_") + Key));
            Flag.Type = "BoolProperty";
            P4MapKit::FPropertyEnvelope Actual;
            Actual.BoolValue = Record->GetBoolField(TEXT("enabled"));
            if (!Tag(Extra, Names, Flag, Actual, {}))
            {
                Error = TEXT("C4-instance-cook-flag-invalid");
                return false;
            }
            if (!Record->GetBoolField(TEXT("value_present")))
            {
                continue;
            }
            const auto Value = Record->GetObjectField(TEXT("value"));
            const FString Type = Value->GetStringField(TEXT("type"));
            P4MapKit::PostProcessCook::FSourceTag Metadata;
            const FString Alias = Record->GetStringField(TEXT("value_property"));
            Metadata.Name = TCHAR_TO_UTF8(*Alias);
            Bytes Payload;
            Actual.BoolValue = false;
            if (Type == TEXT("bool"))
            {
                Metadata.Type = "BoolProperty";
                Actual.BoolValue = Value->GetBoolField(TEXT("value"));
            }
            else if (Type == TEXT("float32"))
            {
                Metadata.Type = "FloatProperty";
                const FString Bits = Value->GetStringField(TEXT("bits"));
                if (Bits.Len() != 8)
                {
                    Error = TEXT("C4-instance-cook-float-invalid");
                    return false;
                }
                for (int32 Byte = 0; Byte < 4; ++Byte)
                {
                    Payload.push_back(uint8(FParse::HexNumber(*Bits.Mid(Byte * 2, 2))));
                }
            }
            else if (Type == TEXT("name"))
            {
                Metadata.Type = "ByteProperty";
                const auto* Property = FindFProperty<FByteProperty>(UP4MaterialGameSettings::StaticClass(), *Alias);
                if (!Property || !Property->Enum)
                {
                    Error = TEXT("C4-instance-cook-enum-unknown");
                    return false;
                }
                Metadata.Detail = {TCHAR_TO_UTF8(*Property->Enum->GetName())};
                if (!Name(Payload, Names, TCHAR_TO_UTF8(*Value->GetStringField(TEXT("value")))))
                {
                    return false;
                }
            }
            else if (Type == TEXT("integer"))
            {
                Metadata.Type = "IntProperty";
                U(Payload, uint32(int32(Value->GetNumberField(TEXT("value")))));
            }
            else
            {
                Error = TEXT("C4-instance-cook-value-type-unknown");
                return false;
            }
            if (!Tag(Extra, Names, Metadata, Actual, Payload))
            {
                Error = TEXT("C4-instance-cook-value-invalid");
                return false;
            }
        }
        Bytes Rebuilt;
        bool Found = false;
        for (const auto& TagValue : Tags.Properties)
        {
            if (TagValue.Name == "BasePropertyOverrides")
            {
                if (Found || TagValue.Type != "StructProperty")
                {
                    Error = TEXT("C4-instance-base-override-frame-invalid");
                    return false;
                }
                Found = true;
                const auto Base = P4MapKit::ReadPropertyEnvelopes(
                    Body, Native.Names, TagValue.PayloadOffset, TagValue.PayloadOffset + TagValue.Size);
                if (!Base.Error.Code.empty() || Base.End != TagValue.PayloadOffset + TagValue.Size)
                {
                    Error = TEXT("C4-instance-base-override-payload-invalid");
                    return false;
                }
                Bytes Payload(Body.begin() + TagValue.PayloadOffset, Body.begin() + Base.End - 8);
                Payload.insert(Payload.end(), Extra.begin(), Extra.end());
                if (!Name(Payload, Names, "None"))
                {
                    return false;
                }
                Rebuilt.insert(Rebuilt.end(), Body.begin() + Export.Offset, Body.begin() + TagValue.TagOffset);
                if (!Tag(Rebuilt, Names, P4MapKit::PostProcessCook::FSourceTag(TagValue), TagValue, Payload))
                {
                    return false;
                }
                Rebuilt.insert(Rebuilt.end(), Body.begin() + TagValue.PayloadOffset + TagValue.Size,
                    Body.begin() + Export.Offset + Export.Size);
            }
        }
        if (!Found)
        {
            // Stock cook omits an entirely default struct. The game's C4 locals still
            // require their own native struct tag.
            Bytes Payload = Extra;
            if (!Name(Payload, Names, "None"))
            {
                return false;
            }
            Rebuilt.insert(Rebuilt.end(), Body.begin() + Export.Offset, Body.begin() + Tags.End - 8);
            P4MapKit::PostProcessCook::FSourceTag Metadata;
            Metadata.Name = "BasePropertyOverrides";
            Metadata.Type = "StructProperty";
            Metadata.Detail = {"MaterialInstanceBasePropertyOverrides"};
            if (!Tag(Rebuilt, Names, Metadata, {}, Payload) || !Name(Rebuilt, Names, "None"))
            {
                return false;
            }
            Rebuilt.insert(Rebuilt.end(), Body.begin() + Tags.End, Body.begin() + Export.Offset + Export.Size);
        }
        Replacements.emplace(Index, std::move(Rebuilt));
    }
    if (int32(Replacements.size()) != Wanted.Num())
    {
        Error = TEXT("C4-instance-cook-export-missing");
        return false;
    }
    P4MapKit::Export::RewrittenPackage Written;
    std::string Why;
    if (!P4MapKit::Export::RebuildCookedExports(Header, Body, Names, Replacements, Written, Why))
    {
        Error = UTF8_TO_TCHAR(Why.c_str());
        return false;
    }
    Header = std::move(Written.Header);
    Body = std::move(Written.Body);
    Error.Reset();
    return true;
}

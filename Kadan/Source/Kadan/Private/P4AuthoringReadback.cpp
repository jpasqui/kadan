// Readback: the values an asset or a table holds after a write, compared with what was written.

#include "P4AuthoringRowsInternal.h"
#include "P4AuthoringRows.h"
#include "P4Common.h"
#include "P4GamePaths.h"
#include "P4GameNames.h"
#include "P4OriginalImportSave.h"
#include "DataTableUtils.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"
#include "UObject/TextProperty.h"
#include "P4PlaceActors.h"
#include "P4MapKitCore/quality.h"
#include "ScopedTransaction.h"
#include "P4TextValues.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "UObject/SoftObjectPath.h"
#include "Misc/PackageName.h"
#include "P4NativeHash.h"
#include "Engine/DataAsset.h"
#include "UObject/StrongObjectPtr.h"
#include "P4ImportReferences.h"
#include "UObject/MetaData.h"
#include "Misc/Paths.h"
#include "P4OriginalDecode.h"
#include "P4OriginalMeshStaging.h"
#include "P4StaticMeshBuild.h"
#include "P4TextureBuild.h"
#include "P4TextureContent.h"
#include "P4MaterialGraph.h"
#include "P4MaterialInstance.h"
#include "P4StaticMeshContent.h"
#include "P4LevelContent.h"
#include "P4LightingBake.h"
#include "P4KadanImport.h"
#include "P4NativeValueStaging.h"

namespace P4AuthoringRowsDetail
{
bool P4ScalarProjection(FProperty* Property, const TSharedPtr<FJsonValue>& Value, TSharedPtr<FJsonValue>& Projected)
{
    if (!Property || !Value.IsValid())
        return false;
    UEnum* Enum = nullptr;
    if (auto EnumProperty = CastField<FEnumProperty>(Property))
        Enum = EnumProperty->GetEnum();
    auto Number = CastField<FNumericProperty>(Property);
    if (Number && Number->GetIntPropertyEnum())
        Enum = Number->GetIntPropertyEnum();
    if (Enum)
    {
        FString Name;
        if (Value->Type != EJson::String || !Value->TryGetString(Name) || Name.Len() >= NAME_SIZE || P4HasNul(Name))
            return false;
        const int64 Index = Enum->GetValueByNameString(Name);
        if (Index == INDEX_NONE)
            return false;
        Projected = MakeShared<FJsonValueString>(Enum->GetNameByValue(Index).ToString().ToLower());
        return true;
    }
    if (Number)
    {
        if (Value->Type != EJson::Number)
            return false;
        FString Token;
        if (Number->IsFloatingPoint())
        {
            double Parsed;
            if (!Value->TryGetNumber(Parsed) || !FMath::IsFinite(Parsed))
                return false;
            Token = FString::Printf(TEXT("%.17g"), Parsed);
            // Keep floating type and signed zero when the projection travels
            // through a transitional JSON bridge. Never cast integer values.
            if (!Token.Contains(TEXT(".")) && !Token.Contains(TEXT("e")) && !Token.Contains(TEXT("E")))
                Token += TEXT(".0");
        }
        else
        {
            FString Raw;
            std::uint64_t Bits = 0;
            const bool Unsigned = CastField<FByteProperty>(Property) || CastField<FUInt16Property>(Property) ||
                CastField<FUInt32Property>(Property) || CastField<FUInt64Property>(Property);
            if (!Value->TryGetString(Raw) || !P4MapKit::ParseInteger(TCHAR_TO_UTF8(*Raw), Unsigned, Bits))
                return false;
            if (Unsigned)
                Token = FString::Printf(TEXT("%llu"), Bits);
            else
                Token = FString::Printf(
                    TEXT("%lld"), Bits <= uint64(MAX_int64) ? int64(Bits) : -1 - int64(MAX_uint64 - Bits));
        }
        Projected = MakeShared<FJsonValueNumberString>(Token);
        return true;
    }
    if (CastField<FBoolProperty>(Property))
    {
        if (Value->Type != EJson::Boolean)
            return false;
        Projected = MakeShared<FJsonValueBoolean>(Value->AsBool());
        return true;
    }
    if (CastField<FNameProperty>(Property) || CastField<FStrProperty>(Property) ||
        CastField<FObjectPropertyBase>(Property))
    {
        FString Text;
        if (Value->Type != EJson::String || !Value->TryGetString(Text) || P4HasNul(Text))
            return false;
        if (CastField<FNameProperty>(Property))
        {
            if (Text.Len() >= NAME_SIZE)
                return false;
            Text = FName(*Text).ToString().ToLower();
        }
        else if (CastField<FObjectPropertyBase>(Property) && !CastField<FSoftObjectProperty>(Property))
            Text = Text.ToLower();
        Projected = MakeShared<FJsonValueString>(Text);
        return true;
    }
    if (CastField<FTextProperty>(Property))
    {
        // Both sides independently retain flags/history/namespace/key/source,
        // not display text. Admission has qualified only supported histories.
        FString Literal;
        if (!P4TextJsonToString(Value, Literal))
            return false;
        Projected = Value;
        return true;
    }
    return false;
}

bool P4MapEntries(FMapProperty* Property, const TSharedPtr<FJsonValue>& Value,
    TMap<FString, TSharedPtr<FJsonValue>>& Entries, const FString& Label, FString& Error)
{
    if (!Value.IsValid() || Value->Type != EJson::Array)
        return P4AdmissionFail(Error, Label, TEXT("native map readback pairs required"));
    for (const auto& Pair : Value->AsArray())
    {
        TSharedPtr<FJsonValue> Key;
        FString Identity;
        if (!Pair.IsValid() || Pair->Type != EJson::Array || Pair->AsArray().Num() != 2 ||
            !P4ScalarProjection(Property->KeyProp, Pair->AsArray()[0], Key) || !P4ExactJson(Key, Identity))
            return P4AdmissionFail(Error, Label, TEXT("unsupported or malformed native map key"));
        if (auto Number = CastField<FNumericProperty>(Property->KeyProp))
        {
            // Native floating-key equality treats positive/negative zero as
            // the same key; value projection elsewhere retains the sign.
            double Parsed;
            if (Number->IsFloatingPoint() && Key->TryGetNumber(Parsed) && Parsed == 0)
                Identity = TEXT("0.0");
        }
        if (Entries.Contains(Identity))
            return P4AdmissionFail(Error, Label, TEXT("duplicate typed native map key"));
        Entries.Add(Identity, Pair);
    }
    return true;
}

bool P4ReadbackValue(FProperty* Property, const TSharedPtr<FJsonValue>& Source, const TSharedPtr<FJsonValue>& Actual,
    TSharedPtr<FJsonValue>& SourceProjection, TSharedPtr<FJsonValue>& ActualProjection, const FString& Label,
    FString& Error, int32 Depth, bool Single)
{
    if (!Property || !Source.IsValid() || !Actual.IsValid() || Depth > 16)
        return P4AdmissionFail(Error, Label, TEXT("missing native field readback or excessive nesting"));
    const bool Fixed = !Single && Property->ArrayDim > 1;
    auto Array = Fixed ? nullptr : CastField<FArrayProperty>(Property);
    if (Fixed || Array)
    {
        if (Source->Type != EJson::Array || Actual->Type != EJson::Array ||
            Source->AsArray().Num() != Actual->AsArray().Num())
            return P4AdmissionFail(Error, Label, TEXT("native import changed array length"));
        if (!Array && Source->AsArray().Num() != Property->ArrayDim)
            return P4AdmissionFail(Error, Label, TEXT("native fixed array readback is incomplete"));
        TArray<TSharedPtr<FJsonValue>> Expected, Observed;
        for (int32 Index = 0; Index < Source->AsArray().Num(); ++Index)
        {
            TSharedPtr<FJsonValue> A, B;
            if (!P4ReadbackValue(Array ? Array->Inner : Property, Source->AsArray()[Index], Actual->AsArray()[Index], A,
                    B, Label + FString::Printf(TEXT("[%d]"), Index), Error, Depth + 1, !Array))
                return false;
            Expected.Add(A);
            Observed.Add(B);
        }
        SourceProjection = MakeShared<FJsonValueArray>(Expected);
        ActualProjection = MakeShared<FJsonValueArray>(Observed);
        return true;
    }
    if (auto Struct = CastField<FStructProperty>(Property))
    {
        if (Source->Type != EJson::Object || Actual->Type != EJson::Object)
            return P4AdmissionFail(Error, Label, TEXT("native struct readback fields required"));
        auto Expected = MakeShared<FJsonObject>();
        auto Observed = MakeShared<FJsonObject>();
        for (const auto& Field : Source->AsObject()->Values)
        {
            FProperty* Child = Struct->Struct->FindPropertyByName(FName(*Field.Key));
            TSharedPtr<FJsonValue> A, B;
            if (!P4ReadbackValue(Child, Field.Value, Actual->AsObject()->TryGetField(Field.Key), A, B,
                    Label + TEXT(".") + Field.Key, Error, Depth + 1))
                return false;
            Expected->SetField(Field.Key, A);
            Observed->SetField(Field.Key, B);
        }
        SourceProjection = MakeShared<FJsonValueObject>(Expected);
        ActualProjection = MakeShared<FJsonValueObject>(Observed);
        return true;
    }
    if (auto Map = CastField<FMapProperty>(Property))
    {
        TMap<FString, TSharedPtr<FJsonValue>> Expected, Observed;
        if (!P4MapEntries(Map, Source, Expected, Label, Error) || !P4MapEntries(Map, Actual, Observed, Label, Error))
            return false;
        if (Expected.Num() != Observed.Num())
            return P4AdmissionFail(Error, Label, TEXT("native import changed map keys"));
        TArray<FString> Identities;
        Expected.GetKeys(Identities);
        Identities.Sort();
        TArray<TSharedPtr<FJsonValue>> SourcePairs, ActualPairs;
        for (const auto& Identity : Identities)
        {
            const auto* Other = Observed.Find(Identity);
            if (!Other)
                return P4AdmissionFail(Error, Label, TEXT("native import changed map key identity"));
            const auto& Pair = Expected.FindChecked(Identity)->AsArray();
            const auto& ActualPair = (*Other)->AsArray();
            TSharedPtr<FJsonValue> A, B, KeyA, KeyB;
            if (!P4ReadbackValue(
                    Map->KeyProp, Pair[0], ActualPair[0], KeyA, KeyB, Label + TEXT(".key"), Error, Depth + 1) ||
                !P4ReadbackValue(Map->ValueProp, Pair[1], ActualPair[1], A, B,
                    Label + TEXT(".value[") + Identity + TEXT("]"), Error, Depth + 1))
                return false;
            SourcePairs.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{KeyA, A}));
            ActualPairs.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{KeyB, B}));
        }
        SourceProjection = MakeShared<FJsonValueArray>(SourcePairs);
        ActualProjection = MakeShared<FJsonValueArray>(ActualPairs);
        return true;
    }
    FString A, B;
    if (!P4ScalarProjection(Property, Source, SourceProjection) ||
        !P4ScalarProjection(Property, Actual, ActualProjection) || !P4ExactJson(SourceProjection, A) ||
        !P4ExactJson(ActualProjection, B))
        return P4AdmissionFail(Error, Label, TEXT("unsupported native scalar readback"));
    return P4ExactNativeString(A, B) || P4AdmissionFail(Error, Label, TEXT("native import changed field value"));
}

} // namespace P4AuthoringRowsDetail

using namespace P4AuthoringRowsDetail;

FString UP4AuthoringTools::NativeAssetReadback(UObject* Asset, const FString& SourceJson)
{
    FString Error, Canonical, SourceText, ActualText;
    TSharedPtr<FJsonObject> Fields, Actual;
    bool Valid = P4CanonicalAsset(Asset, SourceJson, Fields, Canonical, Error, true);
    auto Expected = MakeShared<FJsonObject>();
    auto Observed = MakeShared<FJsonObject>();
    if (Valid)
    {
        ActualText = NativeAssetValuesJson(Asset);
        Valid = !ActualText.IsEmpty() && int64(ActualText.Len()) * sizeof(TCHAR) <= 64 * 1024 * 1024 &&
            FJsonSerializer::Deserialize(
                TJsonReaderFactory<>::Create(ActualText), Actual, FJsonSerializer::EFlags::StoreNumbersAsStrings);
        if (!Valid)
            Error = TEXT("exact native asset readback unavailable or over budget");
    }
    if (Valid)
        for (const auto& Field : Fields->Values)
        {
            TSharedPtr<FJsonValue> A, B;
            if (!P4ReadbackValue(Asset->GetClass()->FindPropertyByName(FName(*Field.Key)), Field.Value,
                    Actual->TryGetField(Field.Key), A, B, Field.Key, Error))
            {
                Valid = false;
                break;
            }
            Expected->SetField(Field.Key, A);
            Observed->SetField(Field.Key, B);
        }
    if (Valid &&
        (!P4ExactJson(MakeShared<FJsonValueObject>(Expected), SourceText) ||
            !P4ExactJson(MakeShared<FJsonValueObject>(Observed), ActualText)))
    {
        Valid = false;
        Error = TEXT("exact native asset projections unavailable");
    }
    auto Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("valid"), Valid);
    Result->SetStringField(TEXT("error"), Error);
    FString Out = P4CondensedJson(Result);
    if (Valid)
    {
        Out.RemoveFromEnd(TEXT("}"));
        Out += TEXT(",\"source\":") + SourceText + TEXT(",\"readback\":") + ActualText + TEXT("}");
    }
    return Out;
}

FString UP4AuthoringTools::NativeTableReadback(UDataTable* Table, const FString& SourceJson)
{
    FString Error, SourceText, ActualText;
    TSharedPtr<FJsonObject> Admission;
    TArray<TSharedPtr<FJsonValue>> Source, Actual;
    auto Expected = MakeShared<FJsonObject>();
    auto Observed = MakeShared<FJsonObject>();
    bool Valid = FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(NativeCanonicalTableRows(Table, SourceJson)),
                     Admission, FJsonSerializer::EFlags::StoreNumbersAsStrings) &&
        Admission.IsValid() && Admission->GetBoolField(TEXT("valid"));
    if (!Valid)
        Error = Admission.IsValid() ? Admission->GetStringField(TEXT("error"))
                                    : TEXT("native source admission unavailable");
    if (Valid)
    {
        Source = Admission->GetArrayField(TEXT("rows"));
        ActualText = NativeTableRowsJson(Table);
        Valid = !ActualText.IsEmpty() && int64(ActualText.Len()) * sizeof(TCHAR) <= 64 * 1024 * 1024 &&
            FJsonSerializer::Deserialize(
                TJsonReaderFactory<>::Create(ActualText), Actual, FJsonSerializer::EFlags::StoreNumbersAsStrings);
        if (!Valid)
            Error = TEXT("exact native table readback unavailable or over budget");
    }
    TMap<FName, TSharedPtr<FJsonObject>> ReadRows;
    if (Valid)
    {
        for (const auto& Row : Actual)
        {
            if (!Row.IsValid() || Row->Type != EJson::Object)
            {
                Valid = false;
                Error = TEXT("invalid native readback row");
                break;
            }
            auto Object = Row->AsObject();
            FString Name;
            if (!Object->TryGetStringField(TEXT("Name"), Name) || Name.Len() >= NAME_SIZE || FName(*Name).IsNone() ||
                ReadRows.Contains(FName(*Name)))
            {
                Valid = false;
                Error = TEXT("invalid or duplicate native readback row identity");
                break;
            }
            ReadRows.Add(FName(*Name), Object);
        }
        if (Valid && ReadRows.Num() != Source.Num())
        {
            Valid = false;
            Error = TEXT("native import changed row count");
        }
        // Row order is data: the table is saved, and read by the game, in this order.
        for (int32 Index = 0; Valid && Index < Source.Num(); ++Index)
        {
            const FString Wanted = Source[Index]->AsObject()->GetStringField(TEXT("Name"));
            if (FName(*Wanted) != FName(*Actual[Index]->AsObject()->GetStringField(TEXT("Name"))))
            {
                Valid = false;
                Error = Wanted + TEXT(": native import changed row order");
            }
        }
    }
    if (Valid)
        for (const auto& Row : Source)
        {
            const auto Object = Row->AsObject();
            const FString Name = Object->GetStringField(TEXT("Name"));
            const auto* Current = ReadRows.Find(FName(*Name));
            if (!Current)
            {
                Valid = false;
                Error = Name + TEXT(": native import changed row identity");
                break;
            }
            auto SourceFields = MakeShared<FJsonObject>();
            auto ActualFields = MakeShared<FJsonObject>();
            for (const auto& Field : Object->Values)
            {
                if (Field.Key == TEXT("Name"))
                    continue;
                TSharedPtr<FJsonValue> A, B;
                if (!P4ReadbackValue(Table->RowStruct->FindPropertyByName(FName(*Field.Key)), Field.Value,
                        (*Current)->TryGetField(Field.Key), A, B, Name + TEXT(".") + Field.Key, Error))
                {
                    Valid = false;
                    break;
                }
                SourceFields->SetField(Field.Key, A);
                ActualFields->SetField(Field.Key, B);
            }
            if (!Valid)
                break;
            const FString Identity = FName(*Name).ToString().ToLower();
            Expected->SetObjectField(Identity, SourceFields);
            Observed->SetObjectField(Identity, ActualFields);
        }
    if (Valid &&
        (!P4ExactJson(MakeShared<FJsonValueObject>(Expected), SourceText) ||
            !P4ExactJson(MakeShared<FJsonValueObject>(Observed), ActualText)))
    {
        Valid = false;
        Error = TEXT("exact native projections unavailable");
    }
    auto Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("valid"), Valid);
    Result->SetStringField(TEXT("error"), Error);
    FString Json = P4CondensedJson(Result);
    if (Valid)
    {
        Json.RemoveFromEnd(TEXT("}"));
        Json += TEXT(",\"source\":") + SourceText + TEXT(",\"readback\":") + ActualText + TEXT("}");
    }
    return Json;
}

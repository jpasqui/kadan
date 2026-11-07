// Canonical values of a table's rows and an asset's fields, the references they name, and writing them.

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
FProperty* P4FieldProperty(UStruct* Struct, const FString& Key, int32& Slot)
{
    Slot = INDEX_NONE;
    FProperty* Property = Struct->FindPropertyByName(FName(*Key));
    if (!Property && Key.EndsWith(TEXT("]")))
    {
        int32 Open;
        if (Key.FindLastChar(TEXT('['), Open))
        {
            const FString Token = Key.Mid(Open + 1, Key.Len() - Open - 2);
            std::uint64_t Parsed = 0;
            if (!Token.IsEmpty() && P4MapKit::ParseInteger(TCHAR_TO_UTF8(*Token), true, Parsed) && Parsed <= MAX_int32)
            {
                Slot = int32(Parsed);
                Property = Struct->FindPropertyByName(FName(*Key.Left(Open)));
            }
        }
    }
    return Property;
}

bool P4CanonicalFields(UStruct* Struct, TSharedPtr<FJsonObject>& Object, const FString& Label, FString& Error,
    int32 Depth, bool Row, bool CheckReferences)
{
    if (!Struct || !Object.IsValid() || Depth > 16)
        return P4AdmissionFail(Error, Label, TEXT("missing schema or excessive property nesting"));
    auto Result = MakeShared<FJsonObject>();
    TMap<FProperty*, TMap<int32, TSharedPtr<FJsonValue>>> Slots;
    TSet<FProperty*> Complete;
    for (const auto& Field : Object->Values)
    {
        if (Row && Field.Key == TEXT("Name"))
        {
            Result->SetField(Field.Key, Field.Value);
            continue;
        }
        if (Field.Key.Len() >= NAME_SIZE || P4HasNul(Field.Key))
            return P4AdmissionFail(Error, Label, TEXT("native field name exceeds engine limits"));
        int32 Slot;
        FProperty* Property = P4FieldProperty(Struct, Field.Key, Slot);
        if (!Property || Property->HasAnyPropertyFlags(CPF_Transient) ||
            (Row && Property->GetFName() == FName(TEXT("Name"))))
            return P4AdmissionFail(Error, Label + TEXT(".") + Field.Key, TEXT("unknown or nonserialized native field"));
        if (Property->ArrayDim > 1 && (Slot != INDEX_NONE || Field.Value->Type != EJson::Array))
        {
            if (Slot == INDEX_NONE)
                Slot = 0;
            auto& Group = Slots.FindOrAdd(Property);
            if (Slot < 0 || Slot >= Property->ArrayDim || Complete.Contains(Property) || Group.Contains(Slot))
                return P4AdmissionFail(
                    Error, Label + TEXT(".") + Field.Key, TEXT("duplicate, mixed or out-of-range fixed slot"));
            Group.Add(Slot, Field.Value);
        }
        else
        {
            if (Slot != INDEX_NONE || Complete.Contains(Property) || Slots.Contains(Property))
                return P4AdmissionFail(
                    Error, Label + TEXT(".") + Field.Key, TEXT("duplicate native field identity or fixed slot"));
            Complete.Add(Property);
            auto Value = Field.Value;
            if (!P4CanonicalValue(
                    Property, Value, Label + TEXT(".") + Property->GetName(), Error, Depth + 1, false, CheckReferences))
                return false;
            Result->SetField(Property->GetName(), Value);
        }
    }
    for (auto& Group : Slots)
    {
        if (Group.Value.Num() != Group.Key->ArrayDim)
            return P4AdmissionFail(Error, Label + TEXT(".") + Group.Key->GetName(),
                TEXT("complete fixed array required; sparse slots need an adapter"));
        TArray<TSharedPtr<FJsonValue>> Values;
        for (int32 Index = 0; Index < Group.Key->ArrayDim; ++Index)
            Values.Add(Group.Value.FindChecked(Index));
        TSharedPtr<FJsonValue> Value = MakeShared<FJsonValueArray>(Values);
        if (!P4CanonicalValue(
                Group.Key, Value, Label + TEXT(".") + Group.Key->GetName(), Error, Depth + 1, false, CheckReferences))
            return false;
        Result->SetField(Group.Key->GetName(), Value);
    }
    Object = Result;
    return true;
}

bool P4CanonicalValue(FProperty* Property, TSharedPtr<FJsonValue>& Value, const FString& Label, FString& Error,
    int32 Depth, bool Single, bool CheckReferences)
{
    if (!Property || !Value.IsValid() || Depth > 16)
        return P4AdmissionFail(Error, Label, TEXT("missing value or excessive property nesting"));
    if (!Single && Property->ArrayDim > 1)
    {
        if (Value->Type != EJson::Array || Value->AsArray().Num() != Property->ArrayDim)
            return P4AdmissionFail(Error, Label, TEXT("complete fixed array required"));
        TArray<TSharedPtr<FJsonValue>> Values = Value->AsArray();
        for (int32 Index = 0; Index < Values.Num(); ++Index)
            if (!P4CanonicalValue(Property, Values[Index], Label + FString::Printf(TEXT("[%d]"), Index), Error,
                    Depth + 1, true, CheckReferences))
                return false;
        Value = MakeShared<FJsonValueArray>(Values);
        return true;
    }
    if (auto Object = CastField<FObjectPropertyBase>(Property))
    {
        FString Path;
        const bool Soft = CastField<FSoftObjectProperty>(Property) != nullptr;
        const auto ClassProperty = CastField<FClassProperty>(Property);
        if (!Object->PropertyClass || (ClassProperty && !ClassProperty->MetaClass))
            return P4AdmissionFail(Error, Label, TEXT("native reference type is incomplete"));
        if (!Soft && Value->Type == EJson::Null)
            Path.Empty();
        else if (Value->Type != EJson::String || !Value->TryGetString(Path) || P4HasNul(Path) ||
            Path.Len() >= NAME_SIZE)
            return P4AdmissionFail(Error, Label, TEXT("native object path required"));
        if (!Soft)
        {
            if (Path == TEXT("None"))
                Path.Empty();
            if (!Path.IsEmpty())
            {
                if (!(P4IsContentPackageIgnoringCase(Path) || Path.StartsWith(TEXT("/Engine/")) ||
                        Path.StartsWith(TEXT("/Script/"))) ||
                    !FPackageName::IsValidObjectPath(Path))
                    return P4AdmissionFail(Error, Label, TEXT("invalid hard reference path"));
                if (!CheckReferences)
                {
                    Value = MakeShared<FJsonValueString>(Path);
                    return true;
                }
                UObject* Target = P4ImportFindObject(Path);
                if (!Target)
                    return P4AdmissionFail(Error, Label + TEXT(" ") + Path, TEXT("missing recovered hard reference"));
                if (auto Class = CastField<FClassProperty>(Property))
                {
                    UClass* TargetClass = Cast<UClass>(Target);
                    if (!TargetClass || !Class->MetaClass || !TargetClass->IsChildOf(Class->MetaClass))
                        return P4AdmissionFail(
                            Error, Label + TEXT(" ") + Path, TEXT("reference violates native MetaClass"));
                }
                else if (!Object->PropertyClass || !Target->IsA(Object->PropertyClass))
                    return P4AdmissionFail(
                        Error, Label + TEXT(" ") + Path, TEXT("reference violates native PropertyClass"));
            }
            Value = MakeShared<FJsonValueString>(Path);
        }
        return true;
    }
    if (auto Struct = CastField<FStructProperty>(Property))
    {
        if (Value->Type == EJson::String &&
            (Struct->Struct->GetPathName() == TEXT("/Script/CoreUObject.SoftObjectPath") ||
                Struct->Struct->GetPathName() == TEXT("/Script/CoreUObject.SoftClassPath")))
        {
            auto Fields = MakeShared<FJsonObject>();
            const FString Path = Value->AsString();
            Fields->SetStringField(TEXT("AssetPathName"), Path.IsEmpty() ? TEXT("None") : Path);
            Fields->SetStringField(TEXT("SubPathString"), TEXT(""));
            Value = MakeShared<FJsonValueObject>(Fields);
        }
        if (Value->Type != EJson::Object)
            return P4AdmissionFail(Error, Label, TEXT("native struct fields required"));
        auto Fields = Value->AsObject();
        if (!P4CanonicalFields(Struct->Struct, Fields, Label, Error, Depth, false, CheckReferences))
            return false;
        Value = MakeShared<FJsonValueObject>(Fields);
        return true;
    }
    if (auto Array = CastField<FArrayProperty>(Property))
    {
        if (Value->Type != EJson::Array)
            return P4AdmissionFail(Error, Label, TEXT("native array required"));
        TArray<TSharedPtr<FJsonValue>> Values = Value->AsArray();
        for (int32 Index = 0; Index < Values.Num(); ++Index)
            if (!P4CanonicalValue(Array->Inner, Values[Index], Label + FString::Printf(TEXT("[%d]"), Index), Error,
                    Depth + 1, false, CheckReferences))
                return false;
        Value = MakeShared<FJsonValueArray>(Values);
        return true;
    }
    if (auto Map = CastField<FMapProperty>(Property))
    {
        if (Value->Type == EJson::Object && Value->AsObject()->Values.Num() == 1)
        {
            const auto* Pairs = Value->AsObject()->Values.Find(TEXT("__native_map_pairs"));
            if (Pairs)
                Value = *Pairs;
        }
        if (Value->Type != EJson::Array)
            return P4AdmissionFail(Error, Label, TEXT("typed native map pairs required"));
        TArray<TSharedPtr<FJsonValue>> Values;
        for (int32 Index = 0; Index < Value->AsArray().Num(); ++Index)
        {
            const auto Pair = Value->AsArray()[Index];
            const FString Entry = Label + FString::Printf(TEXT("[%d]"), Index);
            if (!Pair.IsValid() || Pair->Type != EJson::Array || Pair->AsArray().Num() != 2)
                return P4AdmissionFail(Error, Entry, TEXT("exactly two map pair values required"));
            TArray<TSharedPtr<FJsonValue>> Children = Pair->AsArray();
            if (!P4CanonicalValue(
                    Map->KeyProp, Children[0], Entry + TEXT(".key"), Error, Depth + 1, false, CheckReferences) ||
                !P4CanonicalValue(
                    Map->ValueProp, Children[1], Entry + TEXT(".value"), Error, Depth + 1, false, CheckReferences))
                return false;
            Values.Add(MakeShared<FJsonValueArray>(Children));
        }
        Value = MakeShared<FJsonValueArray>(Values);
        return true;
    }
    if ((CastField<FNameProperty>(Property) || CastField<FStrProperty>(Property)) && Value->Type == EJson::String &&
        P4HasNul(Value->AsString()))
        return P4AdmissionFail(Error, Label, TEXT("embedded NUL in native string"));
    const auto Number = CastField<FNumericProperty>(Property);
    if ((CastField<FNameProperty>(Property) || CastField<FEnumProperty>(Property) ||
            (Number && Number->GetIntPropertyEnum())) &&
        Value->Type == EJson::String && Value->AsString().Len() >= NAME_SIZE)
        return P4AdmissionFail(Error, Label, TEXT("native name exceeds engine limits"));
    // ExactIntegerJson checks primitive types, widths, enums, text metadata
    // and supported map keys; normalization never rounds numeric tokens.
    return true;
}

bool P4CanonicalRows(
    UDataTable* Table, const FString& Json, TArray<TSharedPtr<FJsonValue>>& Rows, FString& Error, bool CheckReferences)
{
    if (!IsInGameThread() || !IsValid(Table) || !Table->RowStruct ||
        !Table->RowStruct->IsChildOf(FTableRowBase::StaticStruct()))
        return P4AdmissionFail(Error, TEXT("Rows"), TEXT("editor thread and native TableRowBase schema required"));
    if (Table->RowStruct->FindPropertyByName(TEXT("Name")))
        return P4AdmissionFail(Error, TEXT("Rows"), TEXT("native Name field conflicts with row identity transport"));
    if (!P4TableJsonTokens(Json, Error) ||
        !FJsonSerializer::Deserialize(
            TJsonReaderFactory<>::Create(Json), Rows, FJsonSerializer::EFlags::StoreNumbersAsStrings))
        return P4AdmissionFail(Error, TEXT("Rows"), TEXT("invalid, duplicate or over-budget JSON"));
    TSet<FName> Names;
    for (auto& Row : Rows)
    {
        if (!Row.IsValid() || Row->Type != EJson::Object)
            return P4AdmissionFail(Error, TEXT("Rows"), TEXT("row object required"));
        auto Object = Row->AsObject();
        FString Name;
        const auto* NameValue = Object->Values.Find(TEXT("Name"));
        if (!NameValue || (*NameValue)->Type != EJson::String || !(*NameValue)->TryGetString(Name) || Name.IsEmpty() ||
            Name.Len() >= NAME_SIZE || P4HasNul(Name))
            return P4AdmissionFail(Error, TEXT("Rows"), TEXT("bounded nonempty native row name required"));
        const FName Identity(*Name);
        if (Identity.IsNone() || Names.Contains(Identity))
            return P4AdmissionFail(Error, Name, TEXT("duplicate or None row identity"));
        Names.Add(Identity);
        if (!P4CanonicalFields(Table->RowStruct, Object, Name, Error, 0, true, CheckReferences))
            return false;
        Row = MakeShared<FJsonValueObject>(Object);
    }
    return true;
}

bool P4CanonicalAsset(UObject* Asset, const FString& Json, TSharedPtr<FJsonObject>& Fields, FString& Canonical,
    FString& Error, bool CheckReferences)
{
    if (!IsInGameThread() || !IsValid(Asset) || Asset->HasAnyFlags(RF_ClassDefaultObject) ||
        (!Asset->IsA<UDataAsset>() &&
            Asset->GetClass()->GetPathName() != TEXT(P4_GAME_SCRIPT_PACKAGE ".ActorPlacedNumAsset") &&
            Asset->GetClass()->GetPathName() != TEXT(P4_GAME_SCRIPT_PACKAGE ".ActorPlacementInfoAsset")))
        return P4AdmissionFail(Error, TEXT("Asset"), TEXT("editor thread and supported native asset required"));
    if (!P4TableJsonTokens(Json, Error) ||
        !FJsonSerializer::Deserialize(
            TJsonReaderFactory<>::Create(Json), Fields, FJsonSerializer::EFlags::StoreNumbersAsStrings) ||
        !Fields.IsValid())
        return P4AdmissionFail(Error, TEXT("Asset"), TEXT("native field object required"));
    if (!P4CanonicalFields(Asset->GetClass(), Fields, TEXT("Asset"), Error, 0, false, CheckReferences) ||
        !P4ExactJson(MakeShared<FJsonValueObject>(Fields), Canonical))
        return false;
    // Validate importer types on a separate DOM; conversion must not replace
    // the source uint64 tokens, FText metadata or map pairs.
    TSharedPtr<FJsonObject> Checked;
    if (!FJsonSerializer::Deserialize(
            TJsonReaderFactory<>::Create(Canonical), Checked, FJsonSerializer::EFlags::StoreNumbersAsStrings))
        return false;
    for (auto& Field : Checked->Values)
        if (!ExactIntegerJson(Asset->GetClass()->FindPropertyByName(FName(*Field.Key)), Field.Value))
            return P4AdmissionFail(Error, Field.Key, TEXT("unsupported or invalid exact native value"));
    for (const auto& Field : Fields->Values)
    {
        TSharedPtr<FJsonValue> A, B;
        if (!P4ReadbackValue(Asset->GetClass()->FindPropertyByName(FName(*Field.Key)), Field.Value, Field.Value, A, B,
                Field.Key, Error))
            return false;
    }
    return true;
}

void P4CollectAssetReferences(
    FProperty* Property, const TSharedPtr<FJsonValue>& Value, TArray<TSharedPtr<FJsonValue>>& References, bool Single)
{
    if (!Single && Property->ArrayDim > 1)
    {
        for (const auto& Item : Value->AsArray())
            P4CollectAssetReferences(Property, Item, References, true);
    }
    else if (auto Object = CastField<FObjectPropertyBase>(Property))
    {
        if (CastField<FSoftObjectProperty>(Property) || Value->AsString().IsEmpty())
            return;
        UClass* Class = Object->PropertyClass;
        const auto ClassProperty = CastField<FClassProperty>(Property);
        if (ClassProperty)
            Class = ClassProperty->MetaClass;
        TArray<TSharedPtr<FJsonValue>> Item;
        Item.Add(MakeShared<FJsonValueString>(Value->AsString()));
        Item.Add(MakeShared<FJsonValueString>(Class ? Class->GetPathName() : TEXT("")));
        Item.Add(MakeShared<FJsonValueString>(ClassProperty ? TEXT("ClassProperty") : TEXT("ObjectProperty")));
        References.Add(MakeShared<FJsonValueArray>(Item));
    }
    else if (auto Struct = CastField<FStructProperty>(Property))
    {
        for (const auto& Field : Value->AsObject()->Values)
            P4CollectAssetReferences(Struct->Struct->FindPropertyByName(FName(*Field.Key)), Field.Value, References);
    }
    else if (auto Array = CastField<FArrayProperty>(Property))
    {
        for (const auto& Item : Value->AsArray())
            P4CollectAssetReferences(Array->Inner, Item, References);
    }
    else if (auto Map = CastField<FMapProperty>(Property))
        for (const auto& Pair : Value->AsArray())
        {
            P4CollectAssetReferences(Map->KeyProp, Pair->AsArray()[0], References);
            P4CollectAssetReferences(Map->ValueProp, Pair->AsArray()[1], References);
        }
}

bool P4WriteAssetValue(
    FProperty* Property, void* Destination, const TSharedPtr<FJsonValue>& Value, UObject* Owner, bool Single)
{
    if (!Single && Property->ArrayDim > 1)
    {
        for (int32 Index = 0; Index < Property->ArrayDim; ++Index)
            if (!P4WriteAssetValue(Property, static_cast<uint8*>(Destination) + Index * Property->ElementSize,
                    Value->AsArray()[Index], Owner, true))
                return false;
        return true;
    }
    if (auto Enum = CastField<FEnumProperty>(Property))
    {
        Enum->GetUnderlyingProperty()->SetIntPropertyValue(
            Destination, uint64(Enum->GetEnum()->GetValueByNameString(Value->AsString())));
        return true;
    }
    if (auto Number = CastField<FNumericProperty>(Property))
    {
        if (Number->GetIntPropertyEnum())
            Number->SetIntPropertyValue(
                Destination, uint64(Number->GetIntPropertyEnum()->GetValueByNameString(Value->AsString())));
        else if (Number->IsFloatingPoint())
            Number->SetFloatingPointPropertyValue(Destination, Value->AsNumber());
        else
        {
            FString Token;
            std::uint64_t Bits = 0;
            const bool Unsigned = CastField<FByteProperty>(Property) || CastField<FUInt16Property>(Property) ||
                CastField<FUInt32Property>(Property) || CastField<FUInt64Property>(Property);
            if (!Value->TryGetString(Token) || !P4MapKit::ParseInteger(TCHAR_TO_UTF8(*Token), Unsigned, Bits))
                return false;
            Number->SetIntPropertyValue(Destination, Bits);
        }
        return true;
    }
    if (auto Bool = CastField<FBoolProperty>(Property))
    {
        Bool->SetPropertyValue(Destination, Value->AsBool());
        return true;
    }
    if (auto Name = CastField<FNameProperty>(Property))
    {
        Name->SetPropertyValue(Destination, FName(*Value->AsString()));
        return true;
    }
    if (auto String = CastField<FStrProperty>(Property))
    {
        String->SetPropertyValue(Destination, Value->AsString());
        return true;
    }
    if (auto Text = CastField<FTextProperty>(Property))
    {
        FString Literal;
        if (!P4TextJsonToString(Value, Literal))
            return false;
        const TCHAR* End = Text->ImportText(*Literal, Destination, PPF_None, Owner);
        return End && !*End;
    }
    if (auto Soft = CastField<FSoftObjectProperty>(Property))
    {
        Soft->SetPropertyValue(Destination, FSoftObjectPtr(FSoftObjectPath(Value->AsString())));
        return true;
    }
    if (auto Object = CastField<FObjectPropertyBase>(Property))
    {
        UObject* Target = Value->AsString().IsEmpty() ? nullptr : P4ImportFindObject(Value->AsString());
        if (!Target && !Value->AsString().IsEmpty())
            return false;
        Object->SetObjectPropertyValue(Destination, Target);
        return true;
    }
    if (auto Struct = CastField<FStructProperty>(Property))
    {
        for (const auto& Field : Value->AsObject()->Values)
        {
            auto Child = Struct->Struct->FindPropertyByName(FName(*Field.Key));
            if (!P4WriteAssetValue(Child, Child->ContainerPtrToValuePtr<void>(Destination), Field.Value, Owner))
                return false;
        }
        return true;
    }
    if (auto Array = CastField<FArrayProperty>(Property))
    {
        // The rows given replace the array: a row starts from its defaults, not from the row that was there.
        FScriptArrayHelper Helper(Array, Destination);
        Helper.EmptyAndAddValues(Value->AsArray().Num());
        for (int32 Index = 0; Index < Value->AsArray().Num(); ++Index)
            if (!P4WriteAssetValue(Array->Inner, Helper.GetRawPtr(Index), Value->AsArray()[Index], Owner))
                return false;
        return true;
    }
    if (auto Map = CastField<FMapProperty>(Property))
    {
        FScriptMapHelper Helper(Map, Destination);
        Helper.EmptyValues();
        for (const auto& Pair : Value->AsArray())
        {
            const int32 Index = Helper.AddDefaultValue_Invalid_NeedsRehash();
            if (!P4WriteAssetValue(Map->KeyProp, Helper.GetKeyPtr(Index), Pair->AsArray()[0], Owner) ||
                !P4WriteAssetValue(Map->ValueProp, Helper.GetValuePtr(Index), Pair->AsArray()[1], Owner))
                return false;
        }
        Helper.Rehash();
        return true;
    }
    return false;
}

} // namespace P4AuthoringRowsDetail

#include "P4TableExport.h"
#include "P4AuthoringRows.h"
#include "P4KadanReadOnly.h"
#include "P4MapKitCore/export_contract.h"
#include "Engine/DataTable.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/EnumProperty.h"
#include "UObject/UnrealType.h"

namespace
{
FString ExactJson(const TSharedPtr<FJsonValue>& Value)
{
    if (!Value)
        return FString();
    auto Quote = [](const FString& Text)
    { return FString(UTF8_TO_TCHAR(P4MapKit::Export::Quote(TCHAR_TO_UTF8(*Text)).c_str())); };
    switch (Value->Type)
    {
        case EJson::Null:
            return TEXT("null");
        case EJson::Boolean:
            return Value->AsBool() ? TEXT("true") : TEXT("false");
        case EJson::Number:
        {
            FString Number;
            return Value->TryGetString(Number) ? Number : FString();
        }
        case EJson::String:
            return Quote(Value->AsString());
        case EJson::Array:
        {
            TArray<FString> Items;
            for (const auto& Item : Value->AsArray())
            {
                FString Text = ExactJson(Item);
                if (Text.IsEmpty())
                    return FString();
                Items.Add(Text);
            }
            return TEXT("[") + FString::Join(Items, TEXT(",")) + TEXT("]");
        }
        case EJson::Object:
        {
            TArray<FString> Fields;
            for (const auto& Field : Value->AsObject()->Values)
            {
                FString Text = ExactJson(Field.Value);
                if (Text.IsEmpty())
                    return FString();
                Fields.Add(Quote(Field.Key) + TEXT(":") + Text);
            }
            return TEXT("{") + FString::Join(Fields, TEXT(",")) + TEXT("}");
        }
        default:
            return FString();
    }
}
bool RowName(const FString& ModId, const FString& Name)
{
    if (!Name.StartsWith(ModId + TEXT(":"), ESearchCase::CaseSensitive))
        return false;
    const FString Short = Name.Mid(ModId.Len() + 1);
    auto Alnum = [](TCHAR C) { return (C >= TEXT('a') && C <= TEXT('z')) || (C >= TEXT('0') && C <= TEXT('9')); };
    if (Short.IsEmpty() || Short.Len() > 47 || !Alnum(Short[0]) || !Alnum(Short[Short.Len() - 1]))
        return false;
    for (TCHAR C : Short)
        if (!Alnum(C) && C != TEXT('.') && C != TEXT('_') && C != TEXT('-'))
            return false;
    return true;
}
bool Same(const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B)
{
    if (!A || !B || A->Type != B->Type)
        return false;
    switch (A->Type)
    {
        case EJson::Null:
            return true;
        case EJson::Boolean:
            return A->AsBool() == B->AsBool();
        case EJson::Number:
        {
            // StoreNumbersAsStrings keeps integer lexemes exact, including values above 2^53.
            FString Left, Right;
            return A->TryGetString(Left) && B->TryGetString(Right) && Left == Right;
        }
        case EJson::String:
            return A->AsString() == B->AsString();
        case EJson::Array:
            if (A->AsArray().Num() != B->AsArray().Num())
                return false;
            for (int32 I = 0; I < A->AsArray().Num(); ++I)
                if (!Same(A->AsArray()[I], B->AsArray()[I]))
                    return false;
            return true;
        case EJson::Object:
            if (A->AsObject()->Values.Num() != B->AsObject()->Values.Num())
                return false;
            for (const auto& Field : A->AsObject()->Values)
            {
                const auto* Other = B->AsObject()->Values.Find(Field.Key);
                if (!Other || !Same(Field.Value, *Other))
                    return false;
            }
            return true;
        default:
            return false;
    }
}
bool Difference(const FString& Path, const TSharedPtr<FJsonValue>& Before, const TSharedPtr<FJsonValue>& Now,
    FJsonObject& Changes, FString& Error)
{
    if (Before && Same(Before, Now))
        return true;
    if (!Now)
    {
        Error = TEXT("A table value is unavailable.");
        return false;
    }
    switch (Now->Type)
    {
        case EJson::Boolean:
        case EJson::Number:
        case EJson::String:
            Changes.SetField(Path, Now);
            return true;
        case EJson::Array:
        {
            const auto& Items = Now->AsArray();
            const TArray<TSharedPtr<FJsonValue>>* Previous = Before && Before->Type == EJson::Array ? &Before->AsArray()
                                                                                                    : nullptr;
            if (!Previous || Previous->Num() != Items.Num())
            {
                auto Length = MakeShared<FJsonObject>();
                Length->SetNumberField(TEXT("length"), Items.Num());
                Changes.SetObjectField(Path, Length);
            }
            for (int32 I = 0; I < Items.Num(); ++I)
                if (!Difference(FString::Printf(TEXT("%s[%d]"), *Path, I),
                        Previous && I < Previous->Num() ? (*Previous)[I] : nullptr, Items[I], Changes, Error))
                    return false;
            return true;
        }
        case EJson::Object:
            // Exact native transports of text histories and object references are not reflected structs.
            // Refuse changed values that Tsuchi cannot apply rather than shipping the editor transport.
            if (Now->AsObject()->HasField(TEXT("__native_text")))
            {
                Error = TEXT("This table text must be changed through Tsuchi text declarations.");
                return false;
            }
            for (const auto& Field : Now->AsObject()->Values)
            {
                if (Field.Key.StartsWith(TEXT("$")))
                {
                    Error = TEXT("This table value cannot be applied by Tsuchi.");
                    return false;
                }
                const auto* Was = Before && Before->Type == EJson::Object ? Before->AsObject()->Values.Find(Field.Key)
                                                                          : nullptr;
                if (!Difference(Path + TEXT(".") + Field.Key, Was ? *Was : nullptr, Field.Value, Changes, Error))
                    return false;
            }
            return true;
        default:
            Error = TEXT("This table value cannot be applied by Tsuchi.");
            return false;
    }
}
bool Rows(const FString& Text, TArray<TSharedPtr<FJsonValue>>& Out, FString& Error)
{
    if (!FJsonSerializer::Deserialize(
            TJsonReaderFactory<>::Create(Text), Out, FJsonSerializer::EFlags::StoreNumbersAsStrings))
    {
        Error = TEXT("The table's imported values could not be read. Import it again before editing.");
        return false;
    }
    TSet<FName> Names;
    for (const auto& Row : Out)
    {
        FString Name;
        if (!Row || Row->Type != EJson::Object || !Row->AsObject()->TryGetStringField(TEXT("Name"), Name) ||
            Name.IsEmpty() || Names.Contains(FName(*Name)))
        {
            Error = TEXT("The table has missing or repeated row names.");
            return false;
        }
        Names.Add(FName(*Name));
    }
    return true;
}
bool RuntimeFields(const UScriptStruct* RowType, FJsonObject& Rows, FString& Error)
{
    for (const auto& Row : Rows.Values)
    {
        auto Changes = Row.Value->AsObject();
        for (auto& Change : Changes->Values)
        {
            if (Change.Key == TEXT("from"))
                continue;
            TArray<FString> Parts;
            Change.Key.ParseIntoArray(Parts, TEXT("."), false);
            const UStruct* Type = RowType;
            const FProperty* Field = nullptr;
            for (int32 I = 0; I < Parts.Num(); ++I)
            {
                int32 Bracket = INDEX_NONE;
                FString Name = Parts[I];
                Name.FindChar(TEXT('['), Bracket);
                if (Bracket != INDEX_NONE)
                    Name.LeftInline(Bracket);
                Field = Type ? Type->FindPropertyByName(FName(*Name)) : nullptr;
                if (Bracket != INDEX_NONE && Field)
                {
                    if (const FArrayProperty* Array = CastField<FArrayProperty>(Field))
                        Field = Array->Inner;
                    else if (Field->ArrayDim <= 1)
                        Field = nullptr;
                }
                if (I + 1 < Parts.Num())
                {
                    const FStructProperty* Struct = CastField<FStructProperty>(Field);
                    Type = Struct ? Struct->Struct : nullptr;
                }
            }
            bool Ok = Field != nullptr;
            if (Ok && Change.Value->Type == EJson::Object)
            {
                const FArrayProperty* Array = CastField<FArrayProperty>(Field);
                // Runtime resizing can initialize scalars, strings, references, soft paths and
                // reflected structs. Maps, sets and nested arrays have no runtime initializer.
                Ok = Array && !CastField<FMapProperty>(Array->Inner) && !CastField<FSetProperty>(Array->Inner) &&
                    !CastField<FArrayProperty>(Array->Inner);
            }
            else if (Ok && Change.Value->Type == EJson::String)
            {
                UEnum* Enum = nullptr;
                if (const FEnumProperty* Typed = CastField<FEnumProperty>(Field))
                    Enum = Typed->GetEnum();
                else if (const FByteProperty* Byte = CastField<FByteProperty>(Field))
                    Enum = Byte->Enum;
                if (Enum)
                {
                    const int64 Value = Enum->GetValueByNameString(Change.Value->AsString());
                    Ok = Value != INDEX_NONE;
                    if (Ok)
                        Change.Value = MakeShared<FJsonValueNumberString>(FString::Printf(TEXT("%lld"), Value));
                }
                else
                    Ok = CastField<FNameProperty>(Field) || CastField<FStrProperty>(Field) ||
                        CastField<FSoftObjectProperty>(Field);
                if (Ok && CastField<FSoftObjectProperty>(Field))
                    Ok = !Change.Value->AsString().Contains(TEXT(":"));
            }
            else if (Ok)
                Ok = CastField<FNumericProperty>(Field) || CastField<FBoolProperty>(Field) ||
                    CastField<FEnumProperty>(Field);
            if (!Ok)
            {
                Error = FString::Printf(
                    TEXT("Tsuchi cannot apply the edited field %s in row %s."), *Change.Key, *Row.Key);
                return false;
            }
        }
    }
    return true;
}
} //namespace

bool P4TableChanges(
    const FString& ModId, const FString& Baseline, const FString& Current, TSharedPtr<FJsonObject>& Out, FString& Error)
{
    TArray<TSharedPtr<FJsonValue>> Source, Edited;
    if (!Rows(Baseline, Source, Error) || !Rows(Current, Edited, Error))
        return false;
    if (Edited.Num() < Source.Num())
    {
        Error = TEXT("Keep every game row with its original name and order.");
        return false;
    }
    auto Result = MakeShared<FJsonObject>();
    for (int32 I = 0; I < Edited.Num(); ++I)
    {
        const FString Name = Edited[I]->AsObject()->GetStringField(TEXT("Name"));
        const bool Added = I >= Source.Num();
        if (Added ? !RowName(ModId, Name) : FName(*Name) != FName(*Source[I]->AsObject()->GetStringField(TEXT("Name"))))
        {
            Error = Added ? TEXT("Name a new row with this mod's ID followed by a colon and a name.")
                          : TEXT("Keep every game row with its original name and order.");
            return false;
        }
        if (Added && !Source.Num())
        {
            Error = TEXT("A new row needs a game row to copy.");
            return false;
        }
        // Choose the stock row requiring the fewest changed leaf values. It is only named in the
        // manifest, so no unchanged stock values are copied into the mod.
        TSharedPtr<FJsonObject> Best;
        int32 Donor = Added ? 0 : I;
        for (int32 Candidate = Added ? 0 : I; Candidate < (Added ? Source.Num() : I + 1); ++Candidate)
        {
            auto Changes = MakeShared<FJsonObject>();
            if (!Difference(FString(), Source[Candidate], Edited[I], *Changes, Error))
            {
                if (Added)
                    continue;
                return false;
            }
            Changes->RemoveField(TEXT(".Name"));
            // Root struct paths do not have a leading dot.
            TMap<FString, TSharedPtr<FJsonValue>> Fields;
            for (const auto& Field : Changes->Values)
                Fields.Add(Field.Key.StartsWith(TEXT(".")) ? Field.Key.Mid(1) : Field.Key, Field.Value);
            Changes->Values = MoveTemp(Fields);
            if (!Best || Changes->Values.Num() < Best->Values.Num())
            {
                Best = Changes;
                Donor = Candidate;
            }
        }
        if (!Best)
        {
            Error = TEXT("No game row can supply the unchanged values of this new row.");
            return false;
        }
        if (Added)
            Best->SetStringField(TEXT("from"), Source[Donor]->AsObject()->GetStringField(TEXT("Name")));
        if (Best->Values.Num())
            Result->SetObjectField(Name, Best);
    }
    Out = Result;
    Error.Reset();
    return true;
}

FString P4TableManifestJson(const TSharedPtr<FJsonObject>& Manifest)
{
    return Manifest ? ExactJson(MakeShared<FJsonValueObject>(Manifest)) : FString();
}

bool P4ExportTableChanges(UDataTable* Table, const FString& ModId, TSharedPtr<FJsonObject>& Out, FString& Error)
{
    if (!IsInGameThread() || !IsValid(Table))
    {
        Error = TEXT("Choose an imported game table.");
        return false;
    }
    const FString Baseline = Table->GetOutermost()->GetMetaData()->GetValue(Table, TEXT("P4ImportedTableValues"));
    const FString Current = UP4AuthoringTools::NativeTableRowsJson(Table);
    if (P4Kadan::IsKindTable(Table))
    {
        TArray<TSharedPtr<FJsonValue>> Source, Edited;
        if (!Rows(Baseline, Source, Error) || !Rows(Current, Edited, Error))
            return false;
        if (Edited.Num() > Source.Num())
        {
            Error = TEXT("New game kinds must be declared as content.");
            return false;
        }
    }
    return P4TableChanges(ModId, Baseline, Current, Out, Error) && RuntimeFields(Table->GetRowStruct(), *Out, Error);
}

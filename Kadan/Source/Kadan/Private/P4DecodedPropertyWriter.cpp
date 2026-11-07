#include "P4DecodedPropertyWriter.h"
#include "P4PersistentTextFlags.h"
#include "P4LocalizedTextImport.h"
// Preserve the original importer's native conversion and leniency rules.
#include "UObject/UnrealType.h"
#include "UObject/EnumProperty.h"
#include "UObject/TextProperty.h"
#include "UObject/SoftObjectPtr.h"
#include "Internationalization/Text.h"

using P4MapKit::FDecodedValue;
FString FP4DecodedPropertyWriter::Text(const std::string& In)
{
    return FString(UTF8_TO_TCHAR(In.c_str()));
}

bool FP4DecodedPropertyWriter::Object(
    const FObjectPropertyBase* Property, void* Data, const FDecodedValue& Said, const FString& Where)
{
    if (Said.Kind == FDecodedValue::EKind::Null)
    {
        Property->SetObjectPropertyValue(Data, nullptr);
        return true;
    }
    if (Said.Kind != FDecodedValue::EKind::String)
        return Fail(TEXT("level-import-value-is-not-a-reference"), Where);
    const FString Path = Text(Said.Text);
    if (Said.Text == "/Engine/UnknownPackage.UnknownExport")
    {
        Property->SetObjectPropertyValue(Data, nullptr);
        NotShipped.AddUnique(Where);
        return true;
    }
    UObject* Target = nullptr;
    if (Path.StartsWith(TEXT("/"), ESearchCase::CaseSensitive))
    {
        // An asset or a class, by its whole path. It has to be in the project.
        Target = StaticLoadObject(UObject::StaticClass(), nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
        if (!Target && MissingAsNotHeld)
        {
            Property->SetObjectPropertyValue(Data, nullptr);
            NotHeld.Add(Where + TEXT(" = ") + Path + TEXT(" (not in the project)"));
            return true;
        }
        if (!Target)
            return Fail(TEXT("level-import-asset-not-in-the-project: ") + Path, Where);
    }
    else
    {
        Target = Own(Path);
        if (!Target && !Final)
        {
            Later = true;
            return true;
        }
        if (!Target)
            return Fail(TEXT("level-import-reference-to-an-object-not-made: ") + Path, Where);
    }
    const FClassProperty* Kind = CastField<FClassProperty>(Property);
    if (!Target->IsA(Property->PropertyClass) ||
        (Kind && !(Cast<UClass>(Target) && Cast<UClass>(Target)->IsChildOf(Kind->MetaClass))))
        return Fail(TEXT("level-import-reference-of-another-class: ") + Path, Where);
    Property->SetObjectPropertyValue(Data, Target);
    return true;
}
bool FP4DecodedPropertyWriter::Value(
    const FProperty* Property, void* Data, const FDecodedValue& Said, const FString& Where, int32 Depth)
{
    using V = FDecodedValue;
    if (Depth > 32)
        return Fail(TEXT("level-import-value-too-deep"), Where);
    const FString Token = Text(Said.Text);
    if (const FBoolProperty* Bool = CastField<FBoolProperty>(Property))
    {
        if (Said.Kind != V::EKind::Boolean)
            return Fail(TEXT("level-import-value-is-not-a-flag"), Where);
        Bool->SetPropertyValue(Data, Said.Text == "true");
        return true;
    }
    if (const FEnumProperty* Enum = CastField<FEnumProperty>(Property))
    {
        const int64 Entry = Said.Kind == V::EKind::String ? Enum->GetEnum()->GetValueByNameString(Token) : INDEX_NONE;
        // A choice the game's engine has and this engine does not is a value this engine has no place
        // for, as a member it does not have is: what is there stays, and the value is told.
        if (Entry == INDEX_NONE && Lenient && Said.Kind == V::EKind::String)
        {
            NotHeld.Add(Where + TEXT(" = ") + Text(P4MapKit::DecodedJson(Said)));
            return true;
        }
        if (Entry == INDEX_NONE)
            return Fail(TEXT("level-import-enum-value-not-in-this-engine: ") + Token, Where);
        Enum->GetUnderlyingProperty()->SetIntPropertyValue(Data, Entry);
        return true;
    }
    if (const FNumericProperty* Number = CastField<FNumericProperty>(Property))
    {
        const V* Bits = Said.Kind == V::EKind::Object && Said.Fields.size() == 1 ? Said.Field("__native_float32_bits")
                                                                                 : nullptr;
        if (Bits)
        {
            // Only positive infinity is written, where the game has it: the derived foliage sentinel and the
            // longest duration of a sound that loops. Other archived NaNs remain recorded unsupported source values.
            if (!CastField<FFloatProperty>(Property) || Bits->Kind != V::EKind::String || Bits->Text != "2139095040" ||
                !(Where.EndsWith(TEXT(" LowBoundOriginRadius.Z"), ESearchCase::CaseSensitive) ||
                    Where.EndsWith(TEXT("MaximumDuration"), ESearchCase::CaseSensitive)))
                return Fail(TEXT("level-import-archived-float-not-qualified"), Where);
            const uint32 Word = 0x7f800000u;
            FMemory::Memcpy(Data, &Word, sizeof(Word));
            return true;
        }
        const FByteProperty* Byte = CastField<FByteProperty>(Property);
        if (Byte && Byte->Enum && Said.Kind == V::EKind::String)
        {
            const int64 Entry = Byte->Enum->GetValueByNameString(Token);
            if (Entry == INDEX_NONE && Lenient)
            {
                NotHeld.Add(Where + TEXT(" = ") + Text(P4MapKit::DecodedJson(Said)));
                return true;
            }
            if (Entry == INDEX_NONE)
                return Fail(TEXT("level-import-enum-value-not-in-this-engine: ") + Token, Where);
            Number->SetIntPropertyValue(Data, Entry);
            return true;
        }
        if (Said.Kind != V::EKind::Number || Token.IsEmpty())
            return Fail(TEXT("level-import-value-is-not-a-number"), Where);
        const TCHAR First = Token[0];
        if (!(FChar::IsDigit(First) || First == TEXT('-')))
            return Fail(TEXT("level-import-number-not-brought-over-yet: ") + Token, Where);
        if (Number->IsFloatingPoint())
            Number->SetFloatingPointPropertyValue(Data, FCString::Atod(*Token));
        else if (First == TEXT('-'))
            Number->SetIntPropertyValue(Data, FCString::Atoi64(*Token));
        else
            Number->SetIntPropertyValue(Data, FCString::Strtoui64(*Token, nullptr, 10));
        return true;
    }
    if (const FNameProperty* Name = CastField<FNameProperty>(Property))
    {
        if (Said.Kind != V::EKind::String)
            return Fail(TEXT("level-import-value-is-not-a-name"), Where);
        Name->SetPropertyValue(Data, FName(*Token));
        return true;
    }
    if (const FStrProperty* String = CastField<FStrProperty>(Property))
    {
        if (Said.Kind != V::EKind::String)
            return Fail(TEXT("level-import-value-is-not-text"), Where);
        String->SetPropertyValue(Data, Token);
        return true;
    }
    if (const FTextProperty* Words = CastField<FTextProperty>(Property))
    {
        // Text is said as what the game finds it by: a namespace, a key and the source it was
        // written in. Or as text that is the same in every language, or as none.
        V Persistent = Said;
        P4PersistentTextFlags(Persistent);
        const V* Told = Persistent.Kind == V::EKind::Object && Persistent.Fields.size() == 1
            ? Persistent.Field("__native_text")
            : nullptr;
        const V* Flags = Told ? Told->Field("flags") : nullptr;
        const V* History = Told ? Told->Field("history") : nullptr;
        if (!Flags || !History)
            return Fail(TEXT("level-import-value-is-not-localized-text"), Where);
        const auto Word = [&](const char* Name, FString& Out)
        {
            const V* Value = Told->Field(Name);
            if (!Value || Value->Kind != V::EKind::String)
                return false;
            Out = Text(Value->Text);
            return true;
        };
        FString Namespace, Key, Source;
        const V* Has = Told->Field("has_source");
        const bool Invariant = Flags->Text == "2";
        const bool Immutable = Flags->Text == std::to_string(uint32(ETextFlag::Immutable));
        if (History->Text == "0" && (Flags->Text == "0" || Immutable) && Word("namespace", Namespace) &&
            Word("key", Key) && Word("source", Source))
        {
            FText Localized;
            if (!P4MakePersistentLocalizedText(Immutable ? ETextFlag::Immutable : 0, Namespace, Key, Source, Localized))
                return Fail(TEXT("level-import-localized-text-native-archive-differs"), Where);
            Words->SetPropertyValue(Data, Localized);
            return true;
        }
        if (History->Text == "-1" && Has && Has->Kind == V::EKind::Boolean && Has->Text == "true" && Invariant &&
            Word("source", Source))
        {
            Words->SetPropertyValue(Data, FText::AsCultureInvariant(Source));
            return true;
        }
        if (History->Text == "-1" && Has && Has->Kind == V::EKind::Boolean && Has->Text == "false" &&
            (Invariant || Flags->Text == "0"))
        {
            Words->SetPropertyValue(Data, Invariant ? FText::AsCultureInvariant(FString()) : FText());
            return true;
        }
        return Fail(TEXT("level-import-text-of-a-kind-not-brought-over-yet: flags ") + Text(Flags->Text) +
                TEXT(", history ") + Text(History->Text),
            Where);
    }
    if (const FSoftObjectProperty* Soft = CastField<FSoftObjectProperty>(Property))
    {
        if (Said.Kind != V::EKind::String && Said.Kind != V::EKind::Null)
            return Fail(TEXT("level-import-value-is-not-a-soft-reference"), Where);
        Soft->SetPropertyValue(Data, FSoftObjectPtr(FSoftObjectPath(Token)));
        return true;
    }
    if (const FObjectPropertyBase* Reference = CastField<FObjectPropertyBase>(Property))
    {
        // A lazy reference is the id of the object it names, which the object carries (the import gives
        // each object of a level the id the game's file says it carries).
        if (CastField<FLazyObjectProperty>(Property))
        {
            FLazyObjectPtr& Lazy = *static_cast<FLazyObjectPtr*>(Data);
            if (Said.Kind == V::EKind::Null)
            {
                Lazy.Reset();
                return true;
            }
            const FString Digits = Said.Kind == V::EKind::String ? Text(Said.Text) : FString();
            if (Digits.Len() != 32)
                return Fail(TEXT("level-import-value-is-not-a-lazy-reference"), Where);
            uint32 Words[4];
            for (int32 Word = 0; Word < 4; ++Word)
            {
                const FString Part = Digits.Mid(Word * 8, 8);
                for (const TCHAR Digit : Part)
                    if (!FChar::IsHexDigit(Digit))
                        return Fail(TEXT("level-import-value-is-not-a-lazy-reference"), Where);
                Words[Word] = uint32(FCString::Strtoui64(*Part, nullptr, 16));
            }
            Lazy = FUniqueObjectGuid(FGuid(Words[0], Words[1], Words[2], Words[3]));
            return true;
        }
        return Object(Reference, Data, Said, Where);
    }
    if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
    {
        const FString Kind = Struct->Struct->GetPathName();
        if (Said.Kind == V::EKind::String &&
            (Kind == TEXT("/Script/CoreUObject.SoftObjectPath") || Kind == TEXT("/Script/CoreUObject.SoftClassPath")))
        {
            static_cast<FSoftObjectPath*>(Data)->SetPath(Token);
            return true;
        }
        // A gameplay tag is read as its name.
        const FNameProperty* TagName = FindFProperty<FNameProperty>(Struct->Struct, TEXT("TagName"));
        if (Kind == TEXT("/Script/GameplayTags.GameplayTag") && TagName && Said.Kind == V::EKind::Object &&
            Said.Fields.size() == 1 && Said.Fields[0].first == "tag" && Said.Fields[0].second.Kind == V::EKind::String)
        {
            TagName->SetPropertyValue_InContainer(Data, FName(*Text(Said.Fields[0].second.Text)));
            return true;
        }
        // Which navigation agents something is for is one word of bits, which the engine saves as it is.
        if (Kind.EndsWith(TEXT(".NavAgentSelector"), ESearchCase::CaseSensitive) && Said.Kind == V::EKind::Object &&
            Said.Fields.size() == 1 && Said.Fields[0].first == "PackedBits" &&
            Said.Fields[0].second.Kind == V::EKind::Number && Struct->Struct->GetStructureSize() == 4)
        {
            const uint32 Bits = uint32(FCString::Strtoui64(*Text(Said.Fields[0].second.Text), nullptr, 10));
            FMemory::Memcpy(Data, &Bits, 4);
            return true;
        }
        if (Said.Kind != V::EKind::Object)
            return Fail(TEXT("level-import-struct-value-not-brought-over-yet: ") + Kind, Where);
        for (const auto& Child : Said.Fields)
        {
            // A member that is a fixed list says its later places in its name: Name[1], Name[2].
            const FString Member = Text(Child.first);
            FString Name = Member;
            int32 Place = 0, Bracket = INDEX_NONE;
            if (Name.FindChar(TEXT('['), Bracket))
            {
                Place = FCString::Atoi(*Name.Mid(Bracket + 1));
                Name.LeftInline(Bracket);
            }
            const FProperty* Inner = Struct->Struct->FindPropertyByName(FName(*Name));
            if (Inner && (Place < 0 || Place >= Inner->ArrayDim))
                Inner = nullptr;
            if (!Inner && Lenient)
            {
                NotHeld.Add(Where + TEXT(".") + Member + TEXT(" = ") + Text(P4MapKit::DecodedJson(Child.second)));
                continue;
            }
            if (!Inner)
                return Fail(TEXT("level-import-struct-member-not-in-this-engine: ") + Kind + TEXT(" ") + Member, Where);
            if (!Value(Inner, Inner->ContainerPtrToValuePtr<void>(Data, Place), Child.second,
                    Where + TEXT(".") + Member, Depth + 1))
                return false;
        }
        return true;
    }
    if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
    {
        if (Said.Kind != V::EKind::Array)
            return Fail(TEXT("level-import-value-is-not-a-list"), Where);
        // Every element starts as its type has it, as it does when the engine loads the list.
        FScriptArrayHelper Rows(Array, Data);
        Rows.EmptyAndAddValues(int32(Said.Items.size()));
        for (int32 Row = 0; Row < Rows.Num(); ++Row)
            if (!Value(Array->Inner, Rows.GetRawPtr(Row), Said.Items[Row], Where + FString::Printf(TEXT("[%d]"), Row),
                    Depth + 1))
                return false;
        return true;
    }
    if (const FMapProperty* Map = CastField<FMapProperty>(Property))
    {
        const V* Pairs = Said.Kind == V::EKind::Object ? Said.Field("__native_map_pairs") : nullptr;
        if (!Pairs || Pairs->Kind != V::EKind::Array)
            return Fail(TEXT("level-import-value-is-not-a-map"), Where);
        FScriptMapHelper Rows(Map, Data);
        Rows.EmptyValues(int32(Pairs->Items.size()));
        for (int32 Row = 0; Row < int32(Pairs->Items.size()); ++Row)
        {
            const V& Pair = Pairs->Items[Row];
            if (Pair.Kind != V::EKind::Array || Pair.Items.size() != 2)
                return Fail(TEXT("level-import-value-is-not-a-map"), Where);
            const int32 At = Rows.AddDefaultValue_Invalid_NeedsRehash();
            if (!Value(Map->KeyProp, Rows.GetKeyPtr(At), Pair.Items[0], Where + FString::Printf(TEXT("[%d].Key"), Row),
                    Depth + 1) ||
                !Value(Map->ValueProp, Rows.GetValuePtr(At), Pair.Items[1],
                    Where + FString::Printf(TEXT("[%d].Value"), Row), Depth + 1))
                return false;
        }
        Rows.Rehash();
        return true;
    }
    if (const FSetProperty* Set = CastField<FSetProperty>(Property))
    {
        if (Said.Kind != V::EKind::Array)
            return Fail(TEXT("level-import-value-is-not-a-set"), Where);
        FScriptSetHelper Rows(Set, Data);
        Rows.EmptyElements(int32(Said.Items.size()));
        for (int32 Row = 0; Row < int32(Said.Items.size()); ++Row)
        {
            const int32 At = Rows.AddDefaultValue_Invalid_NeedsRehash();
            if (!Value(Set->ElementProp, Rows.GetElementPtr(At), Said.Items[Row],
                    Where + FString::Printf(TEXT("[%d]"), Row), Depth + 1))
                return false;
        }
        Rows.Rehash();
        return true;
    }
    return Fail(TEXT("level-import-value-kind-not-brought-over-yet: ") + Property->GetClass()->GetName(), Where);
}

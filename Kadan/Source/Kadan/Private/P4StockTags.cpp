#include "P4StockTags.h"
#include "Animation/AnimSequenceBase.h"
#include "P4StockTagTree.h"
#include "P4StockEngineFieldContent.h"
#include "P4MapKitCore/cooked_export_rebuild.h"
#include "P4MapKitCore/property_decoder.h"
#include "P4TextureBuild.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "UObject/EnumProperty.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectHash.h"
#include <map>

namespace
{
using P4StockTags::FNode;
using P4StockTags::FList;
using P4StockTags::FStep;
using P4StockTags::Bytes;
const TCHAR* const RecordKey = TEXT("P4GameStockTags");
TArray<FString> LastLeftOut;

bool HasRecord(const UObject* Object)
{
    return IsValid(Object) && Object->GetOutermost() &&
        Object->GetOutermost()->GetMetaData()->HasValue(Object, RecordKey);
}

// One step of a kept value's path that is not its last: the struct the game's file has there, so that
// the cook can write the struct when this engine leaves it out (all its other members at their defaults).
struct FStepInfo
{
    FString Struct, Guid;
    TArray<FString> Before;
};
struct FRecord
{
    FString Path, Kind, Json, Owner;
    std::vector<std::string> Names;
    Bytes Tag;
    // The names of the tags before it in the game's list, which it is written after.
    TArray<FString> Before;
    TArray<FStepInfo> Steps;
    // Sealed after the import's comparison: the text of each array element on the way, by step, and the
    // engine's own value of an enum choice.
    TMap<int32, FString> Elements;
    bool Sealed = false;
    FString Engine;
};

FString Utf(const std::string& Text)
{
    return UTF8_TO_TCHAR(Text.c_str());
}
std::string Narrow(const FString& Text)
{
    return TCHAR_TO_UTF8(*Text);
}

TArray<TSharedPtr<FJsonValue>> Strings(const TArray<FString>& Items)
{
    TArray<TSharedPtr<FJsonValue>> Out;
    for (const FString& Item : Items)
        Out.Add(MakeShared<FJsonValueString>(Item));
    return Out;
}
bool Strings(const TSharedPtr<FJsonObject>& Json, const TCHAR* Key, TArray<FString>& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
    Out.Reset();
    if (!Json->TryGetArrayField(Key, Items))
        return false;
    for (const auto& Item : *Items)
    {
        FString Text;
        if (!Item->TryGetString(Text))
            return false;
        Out.Add(Text);
    }
    return true;
}

bool Read(const UObject* Object, TArray<FRecord>& Out)
{
    Out.Reset();
    if (!IsValid(Object) || !Object->GetOutermost())
        return false;
    UMetaData* Data = Object->GetOutermost()->GetMetaData();
    if (!Data || !Data->HasValue(Object, RecordKey))
        return false;
    TArray<TSharedPtr<FJsonValue>> Rows;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Data->GetValue(Object, RecordKey)), Rows))
        return false;
    for (const auto& Row : Rows)
    {
        const TSharedPtr<FJsonObject>* One = nullptr;
        FRecord Record;
        FString Hex;
        TArray<FString> Names;
        const TArray<TSharedPtr<FJsonValue>>* Steps = nullptr;
        const TSharedPtr<FJsonObject>* Elements = nullptr;
        if (!Row->TryGetObject(One) || !(*One)->TryGetStringField(TEXT("path"), Record.Path) ||
            !(*One)->TryGetStringField(TEXT("kind"), Record.Kind) ||
            !(*One)->TryGetStringField(TEXT("json"), Record.Json) ||
            !(*One)->TryGetStringField(TEXT("owner"), Record.Owner) || !(*One)->TryGetStringField(TEXT("tag"), Hex) ||
            !Strings(*One, TEXT("names"), Names) || !Strings(*One, TEXT("before"), Record.Before) ||
            !(*One)->TryGetArrayField(TEXT("steps"), Steps) || Hex.Len() % 2 ||
            (Record.Kind != TEXT("member") && Record.Kind != TEXT("value")))
            return false;
        for (const FString& Name : Names)
            Record.Names.push_back(Narrow(Name));
        for (const auto& Step : *Steps)
        {
            const TSharedPtr<FJsonObject>* Fields = nullptr;
            FStepInfo Info;
            if (!Step->TryGetObject(Fields) || !(*Fields)->TryGetStringField(TEXT("struct"), Info.Struct) ||
                !(*Fields)->TryGetStringField(TEXT("guid"), Info.Guid) ||
                !Strings(*Fields, TEXT("before"), Info.Before))
                return false;
            Record.Steps.Add(MoveTemp(Info));
        }
        if ((*One)->TryGetObjectField(TEXT("elements"), Elements))
            for (const auto& Pair : (*Elements)->Values)
            {
                FString Text;
                int32 Step = 0;
                if (!Pair.Value->TryGetString(Text) || !LexTryParseString(Step, *Pair.Key))
                    return false;
                Record.Elements.Add(Step, Text);
            }
        Record.Sealed = (*One)->TryGetStringField(TEXT("engine"), Record.Engine) || (*One)->HasField(TEXT("elements"));
        Record.Tag.resize(Hex.Len() / 2);
        if (HexToBytes(Hex, Record.Tag.data()) != int32(Record.Tag.size()))
            return false;
        Out.Add(MoveTemp(Record));
    }
    return Out.Num() > 0;
}

bool Write(UObject* Object, const TArray<FRecord>& Records)
{
    UMetaData* Data = Object->GetOutermost()->GetMetaData();
    if (!Records.Num())
    {
        Data->RemoveValue(Object, RecordKey);
        return true;
    }
    TArray<TSharedPtr<FJsonValue>> Rows;
    for (const FRecord& Record : Records)
    {
        auto One = MakeShared<FJsonObject>();
        One->SetStringField(TEXT("path"), Record.Path);
        One->SetStringField(TEXT("kind"), Record.Kind);
        One->SetStringField(TEXT("json"), Record.Json);
        One->SetStringField(TEXT("owner"), Record.Owner);
        TArray<FString> Names;
        for (const auto& Name : Record.Names)
            Names.Add(Utf(Name));
        One->SetArrayField(TEXT("names"), Strings(Names));
        One->SetArrayField(TEXT("before"), Strings(Record.Before));
        TArray<TSharedPtr<FJsonValue>> Steps;
        for (const FStepInfo& Info : Record.Steps)
        {
            auto Step = MakeShared<FJsonObject>();
            Step->SetStringField(TEXT("struct"), Info.Struct);
            Step->SetStringField(TEXT("guid"), Info.Guid);
            Step->SetArrayField(TEXT("before"), Strings(Info.Before));
            Steps.Add(MakeShared<FJsonValueObject>(Step));
        }
        One->SetArrayField(TEXT("steps"), Steps);
        One->SetStringField(TEXT("tag"), BytesToHex(Record.Tag.data(), int32(Record.Tag.size())));
        if (Record.Sealed)
        {
            auto Elements = MakeShared<FJsonObject>();
            for (const auto& Pair : Record.Elements)
                Elements->SetStringField(FString::FromInt(Pair.Key), Pair.Value);
            One->SetObjectField(TEXT("elements"), Elements);
            if (Record.Kind == TEXT("value"))
                One->SetStringField(TEXT("engine"), Record.Engine);
        }
        Rows.Add(MakeShared<FJsonValueObject>(One));
    }
    FString Text;
    auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
    if (!FJsonSerializer::Serialize(Rows, Writer))
        return false;
    Data->SetValue(Object, RecordKey, *Text);
    return true;
}

// The same tag in names of its own: two tags are the same when these bytes and names are.
bool Canonical(const FNode& Node, Bytes& Out, std::vector<std::string>& Names, std::string& Error)
{
    Out.clear();
    Names.clear();
    P4StockTags::FWriter Writer{Names};
    Writer.LocalReferences = true;
    if (!Writer.One(Out, Node))
    {
        Error = Writer.Error;
        return false;
    }
    return true;
}
bool Stored(const FRecord& Record, FNode& Node, std::string& Error)
{
    P4StockTags::FReader Reader{Record.Tag, Record.Names, false};
    Reader.LocalReferences = true;
    std::size_t At = 0;
    if (!Reader.One(At, Record.Tag.size(), Node, 0) || At != Record.Tag.size())
    {
        Error = Reader.Error.empty() ? "stock-tag-record-invalid" : Reader.Error;
        return false;
    }
    return true;
}
// A tag found in a package's own tree, read again strictly: what was kept as bytes is understood
// with that package's names, so it can go anywhere.
bool Strictly(const FNode& Found, const P4MapKit::FPackageInventory& Package, FNode& Out, std::string& Error)
{
    std::vector<std::string> Names = Package.Names;
    Bytes Raw;
    P4StockTags::FWriter Writer{Names};
    if (!Writer.One(Raw, Found))
    {
        Error = Writer.Error;
        return false;
    }
    P4StockTags::FReader Reader{Raw, Names, false};
    std::size_t At = 0;
    Reader.ReferenceToPath = [&](std::int32_t Index, std::string& Path) { return Package.Reference(Index, Path); };
    if (!Reader.One(At, Raw.size(), Out, 0) || At != Raw.size())
    {
        Error = Reader.Error.empty() ? "stock-tag-not-carried" : Reader.Error;
        return false;
    }
    return true;
}
// The decoded value of the tag that starts at At, as the import's decoder says it (with the same rules for
// the game's archived floats).
bool Decoded(const P4MapKit::FPackageInventory& Package, const std::vector<uint8>& Body, std::size_t At,
    const FString& Owner, FString& Json)
{
    static const std::vector<P4MapKit::FArchivedFloatCodec> Floats{
        {"AnimNotifyEvent", "MiddleDuration", true, false, false},
        {"FoliageType_InstancedStaticMesh", "LowBoundOriginRadius", false, true, false},
        {"P4RecoveredFoliageType", "LowBoundOriginRadius", false, true, false},
        {"AkAudioEvent", "MaximumDuration", false, true, false},
        {"AkAudioEventData", "MaximumDuration", false, true, false}};
    P4MapKit::FByteReader Reader(Body, At, Body.size());
    P4MapKit::FPropertyEnvelope Envelope;
    if (!P4MapKit::ReadPropertyEnvelope(Reader, Package.Names, Body.size(), Envelope) || Envelope.Name == "None")
        return false;
    P4MapKit::FPropertyDecoder Decoder(Package, Body, 1000000, 64 * 1024 * 1024, {}, Floats);
    P4MapKit::FDecodedValue Value;
    if (!Decoder.Property(Envelope, Value, 0, Narrow(Owner)))
        return false;
    Json = Utf(P4MapKit::DecodedJson(Value));
    return true;
}
FString ExportPath(UObject* Object, UPackage* Package)
{
    FString Path = Object->GetPathName(Package);
    Path.ReplaceInline(TEXT(":"), TEXT("."));
    return Path;
}
bool ParsePath(const FString& Path, std::vector<FStep>& Steps)
{
    if (!P4StockTags::ParsePath(Narrow(Path), Steps))
        return false;
    for (std::size_t At = 0; At < Steps.size(); ++At)
        if (Steps[At].Index >= 0 && At + 1 == Steps.size())
            return false;
    return true;
}

// The live object along a path: what struct the step Count is in, and its memory.
struct FLive
{
    const UStruct* Owner = nullptr;
    void* Memory = nullptr;
};
const FProperty* Member(const UStruct* Owner, const std::string& Name)
{
    const FProperty* Property = Owner ? Owner->FindPropertyByName(FName(*Utf(Name))) : nullptr;
    return Property && Property->ArrayDim == 1 ? Property : nullptr;
}
bool WalkLive(
    UObject* Object, const std::vector<FStep>& Steps, std::size_t Count, const std::vector<int>& Indices, FLive& Out)
{
    Out.Owner = Object->GetClass();
    Out.Memory = Object;
    for (std::size_t At = 0; At < Count; ++At)
    {
        const FProperty* Property = Member(Out.Owner, Steps[At].Name);
        if (!Property)
            return false;
        if (Steps[At].Index >= 0)
        {
            const FArrayProperty* Array = CastField<FArrayProperty>(Property);
            const FStructProperty* Inner = Array ? CastField<FStructProperty>(Array->Inner) : nullptr;
            if (!Inner)
                return false;
            FScriptArrayHelper Helper(Array, Array->ContainerPtrToValuePtr<void>(Out.Memory));
            if (Indices[At] < 0 || Indices[At] >= Helper.Num())
                return false;
            Out.Memory = Helper.GetRawPtr(Indices[At]);
            Out.Owner = Inner->Struct;
        }
        else
        {
            const FStructProperty* Struct = CastField<FStructProperty>(Property);
            if (!Struct)
                return false;
            Out.Memory = Struct->ContainerPtrToValuePtr<void>(Out.Memory);
            Out.Owner = Struct->Struct;
        }
    }
    return true;
}
FString ValueText(const FProperty* Property, const void* Data)
{
    FString Text;
    Property->ExportTextItem(Text, Data, nullptr, nullptr, PPF_None);
    return Text;
}
// The texts of every element of the array at step Step, the steps before it resolved by Indices.
bool ElementTexts(UObject* Object, const std::vector<FStep>& Steps, std::size_t Step, const std::vector<int>& Indices,
    TArray<FString>& Out)
{
    FLive Live;
    Out.Reset();
    if (!WalkLive(Object, Steps, Step, Indices, Live))
        return false;
    const FArrayProperty* Array = CastField<FArrayProperty>(Member(Live.Owner, Steps[Step].Name));
    if (!Array || !CastField<FStructProperty>(Array->Inner))
        return false;
    FScriptArrayHelper Helper(Array, Array->ContainerPtrToValuePtr<void>(Live.Memory));
    for (int32 At = 0; At < Helper.Num(); ++At)
        Out.Add(ValueText(Array->Inner, Helper.GetRawPtr(At)));
    return true;
}
bool EngineText(UObject* Object, const std::vector<FStep>& Steps, const std::vector<int>& Indices, FString& Out)
{
    FLive Live;
    if (!WalkLive(Object, Steps, Steps.size() - 1, Indices, Live))
        return false;
    const FProperty* Property = Member(Live.Owner, Steps.back().Name);
    if (!Property)
        return false;
    Out = ValueText(Property, Property->ContainerPtrToValuePtr<void>(Live.Memory));
    return true;
}
std::vector<int> RecordedIndices(const std::vector<FStep>& Steps)
{
    std::vector<int> Out;
    for (const FStep& Step : Steps)
        Out.push_back(Step.Index);
    return Out;
}
// Where the record's array elements are now: the recorded place while that element still holds what it
// held when the import was compared, else the one element that does. False when the creator changed it.
bool Resolve(UObject* Object, const FRecord& Record, const std::vector<FStep>& Steps, std::vector<int>& Indices)
{
    Indices = RecordedIndices(Steps);
    for (std::size_t Step = 0; Step + 1 < Steps.size(); ++Step)
    {
        if (Steps[Step].Index < 0)
            continue;
        TArray<FString> Texts;
        if (!ElementTexts(Object, Steps, Step, Indices, Texts))
            return false;
        const FString* Sealed = Record.Elements.Find(int32(Step));
        if (!Sealed)
        {
            if (Indices[Step] >= Texts.Num())
                return false;
            continue;
        }
        if (Indices[Step] < Texts.Num() && Texts[Indices[Step]] == *Sealed)
            continue;
        int32 Found = INDEX_NONE, Matches = 0;
        for (int32 At = 0; At < Texts.Num(); ++At)
            if (Texts[At] == *Sealed)
            {
                Found = At;
                ++Matches;
            }
        if (Matches != 1)
            return false;
        Indices[Step] = Found;
    }
    return true;
}

TArray<FString> NamesBefore(const FList& List, int Position)
{
    TArray<FString> Out;
    for (int At = 0; At < Position; ++At)
        Out.AddUnique(Utf(List[At].Name.Base));
    return Out;
}
// Where a tag goes in a list: after the last tag the game's file has before it.
std::size_t InsertAt(const FList& List, const TArray<FString>& Before)
{
    std::size_t Place = 0;
    for (std::size_t At = 0; At < List.size(); ++At)
        if (Before.ContainsByPredicate(
                [&](const FString& Name) { return Name.Equals(Utf(List[At].Name.Base), ESearchCase::IgnoreCase); }))
            Place = At + 1;
    return Place;
}
FString GuidText(const std::array<std::uint32_t, 4>& Guid)
{
    return FString::Printf(TEXT("%08x%08x%08x%08x"), Guid[0], Guid[1], Guid[2], Guid[3]);
}
bool GuidWords(const FString& Text, std::array<std::uint32_t, 4>& Guid)
{
    if (Text.Len() != 32)
        return false;
    for (int32 Word = 0; Word < 4; ++Word)
    {
        const FString Part = Text.Mid(Word * 8, 8);
        for (const TCHAR Digit : Part)
            if (!FChar::IsHexDigit(Digit))
                return false;
        Guid[Word] = uint32(FCString::Strtoui64(*Part, nullptr, 16));
    }
    return true;
}
} //namespace

EP4GameStockTag P4GameStockTagKind(const UObject* Object, const FString& Path, const FString& Json)
{
    std::vector<FStep> Steps;
    FLive Live;
    if (!IsValid(Object) || !ParsePath(Path, Steps) ||
        !WalkLive(const_cast<UObject*>(Object), Steps, Steps.size() - 1, RecordedIndices(Steps), Live))
        return EP4GameStockTag::None;
    const FProperty* Property = Live.Owner->FindPropertyByName(FName(*Utf(Steps.back().Name)));
    if (!Property)
        return EP4GameStockTag::Member;
    P4MapKit::FDecodedValue Said;
    if (Property->ArrayDim != 1 || !P4MapKit::ReadDecodedJson(Narrow(Json), Said) ||
        Said.Kind != P4MapKit::FDecodedValue::EKind::String || Said.Text.empty())
        return EP4GameStockTag::None;
    const UEnum* Enum = nullptr;
    if (const FEnumProperty* Choice = CastField<FEnumProperty>(Property))
        Enum = Choice->GetEnum();
    else if (const FByteProperty* Byte = CastField<FByteProperty>(Property))
        Enum = Byte->Enum;
    return Enum && Enum->GetValueByNameString(Utf(Said.Text)) == INDEX_NONE ? EP4GameStockTag::Value
                                                                            : EP4GameStockTag::None;
}

bool P4StoreGameStockTags(UObject* Object, const std::vector<uint8>& Body, const P4MapKit::FPackageInventory& Package,
    std::size_t Start, std::size_t End, const FString& Owner, const TArray<FP4GameStockTagWanted>& Wanted,
    TArray<FString>& Kept, TArray<FString>& Problems)
{
    Kept.Reset();
    if (!IsInGameThread() || !IsValid(Object) || End > Body.size() || Start > End)
    {
        Problems.Add(TEXT("stock-tag-store-invalid-input"));
        return false;
    }
    P4StockTags::FReader Reader{Body, Package.Names, true};
    FList Root;
    std::size_t At = Start;
    if (!Reader.List(At, End, Root, 0))
    {
        Problems.Add(TEXT("stock-tag-game-list-unreadable: ") + Utf(Reader.Error));
        return false;
    }
    TArray<FRecord> Records;
    if (HasRecord(Object) && !Read(Object, Records))
    {
        Problems.Add(TEXT("stock-tag-record-invalid: ") + Object->GetPathName());
        return false;
    }
    for (const FP4GameStockTagWanted& One : Wanted)
    {
        std::vector<FStep> Steps;
        const auto Refuse = [&](const FString& Why)
        { Problems.Add(Why + TEXT(": ") + Object->GetName() + TEXT(" ") + One.Path); };
        if (One.Kind == EP4GameStockTag::None || !ParsePath(One.Path, Steps))
        {
            Refuse(TEXT("stock-tag-path-not-kept"));
            continue;
        }
        FRecord Record;
        Record.Path = One.Path;
        Record.Kind = One.Kind == EP4GameStockTag::Member ? TEXT("member") : TEXT("value");
        Record.Owner = Owner;
        FList* List = &Root;
        bool Found = true;
        for (std::size_t Step = 0; Found && Step + 1 < Steps.size(); ++Step)
        {
            const int Place = P4StockTags::FindAt(*List, Steps[Step].Name);
            FNode* Node = Place >= 0 ? &(*List)[std::size_t(Place)] : nullptr;
            FStepInfo Info;
            if (Node && Steps[Step].Index >= 0 && Node->Form == FNode::EForm::Array && Node->StructArray &&
                Steps[Step].Index < int(Node->Elements.size()))
            {
                Record.Owner = Utf(Node->InnerStruct.Base);
                List = &Node->Elements[std::size_t(Steps[Step].Index)];
            }
            else if (Node && Steps[Step].Index < 0 && Node->Form == FNode::EForm::Tagged && Node->Detail.size() == 1)
            {
                Info.Struct = Utf(Node->Detail[0].Base);
                Info.Guid = GuidText(Node->StructGuid);
                Info.Before = NamesBefore(*List, Place);
                Record.Owner = Info.Struct;
                List = &Node->Children;
            }
            else
                Found = false;
            Record.Steps.Add(MoveTemp(Info));
        }
        const int Place = Found ? P4StockTags::FindAt(*List, Steps.back().Name) : -1;
        if (Place < 0)
        {
            Refuse(TEXT("stock-tag-path-not-in-the-game-file"));
            continue;
        }
        const FNode& Node = (*List)[std::size_t(Place)];
        Record.Before = NamesBefore(*List, Place);
        // The tag goes into another package: it must carry exactly in strict reading, and the value it
        // decodes to must be the one the import was told.
        std::string Why;
        FNode Strict, Again;
        FString Json;
        if (!Strictly(Node, Package, Strict, Why) || !Canonical(Strict, Record.Tag, Record.Names, Why) ||
            !Stored(Record, Again, Why))
        {
            Refuse(TEXT("stock-tag-not-carried (") + Utf(Why) + TEXT(")"));
            continue;
        }
        if (!Decoded(Package, Body, Node.At, Record.Owner, Json) || Json != One.Json)
        {
            Refuse(TEXT("stock-tag-decoded-value-differs"));
            continue;
        }
        Record.Json = Json;
        Records.RemoveAll([&](const FRecord& Old) { return Old.Path == One.Path; });
        Records.Add(MoveTemp(Record));
        Kept.Add(One.Path);
    }
    if (Kept.Num() && !Write(Object, Records))
    {
        Problems.Add(TEXT("stock-tag-record-not-written: ") + Object->GetName());
        Kept.Reset();
        return false;
    }
    return true;
}

namespace
{
void ForEachRecorded(UPackage* Package, TFunctionRef<void(UObject*, TArray<FRecord>&)> Visit)
{
    TArray<UObject*> Objects;
    GetObjectsWithOuter(Package, Objects, true);
    for (UObject* Object : Objects)
    {
        TArray<FRecord> Records;
        if (IsValid(Object) && Read(Object, Records))
            Visit(Object, Records);
    }
}
} //namespace

void P4SealGameStockTags(UPackage* Package)
{
    if (!IsValid(Package))
        return;
    ForEachRecorded(Package,
        [](UObject* Object, TArray<FRecord>& Records)
        {
            // FAnimNotifyEvent::PostSerialize gives an invalid editor GUID a new identity
            // on load. Cooked game data has no editor GUID, so initialize it before the
            // element is sealed and saved. Cold reload then retains the exact same element
            // fingerprint, while any actual creator edit still leaves its kept tags out.
            if (UAnimSequenceBase* Sequence = Cast<UAnimSequenceBase>(Object))
                for (FAnimNotifyEvent& Notify : Sequence->Notifies)
                    if (!Notify.Guid.IsValid())
                        Notify.Guid = FGuid::NewGuid();
            for (FRecord& Record : Records)
            {
                std::vector<FStep> Steps;
                if (!ParsePath(Record.Path, Steps))
                    continue;
                const std::vector<int> Indices = RecordedIndices(Steps);
                Record.Elements.Reset();
                for (std::size_t Step = 0; Step + 1 < Steps.size(); ++Step)
                {
                    TArray<FString> Texts;
                    if (Steps[Step].Index >= 0 && ElementTexts(Object, Steps, Step, Indices, Texts) &&
                        Steps[Step].Index < Texts.Num())
                        Record.Elements.Add(int32(Step), Texts[Steps[Step].Index]);
                }
                Record.Engine.Reset();
                if (Record.Kind == TEXT("value"))
                    EngineText(Object, Steps, Indices, Record.Engine);
                Record.Sealed = true;
            }
            Write(Object, Records);
        });
}

void P4ClearGameStockTags(UPackage* Package)
{
    if (!IsValid(Package))
        return;
    TArray<UObject*> Objects;
    GetObjectsWithOuter(Package, Objects, true);
    for (UObject* Object : Objects)
        if (HasRecord(Object))
            Write(Object, {});
}

int32 P4CountGameStockTags(const UObject* Object)
{
    TArray<FRecord> Records;
    return Read(Object, Records) ? Records.Num() : 0;
}

bool P4ObjectHasGameStockTags(const UObject* Object)
{
    // Presence is the routing predicate. Malformed custody must still reach the
    // translator, which refuses it without touching the cooked buffers.
    return IsValid(Object) && !Object->IsEditorOnly() && !Object->HasAnyFlags(RF_Transient) && HasRecord(Object);
}

bool P4PackageHasGameStockTags(UPackage* Package)
{
    TArray<UObject*> Objects;
    GetObjectsWithOuter(Package, Objects, true);
    for (UObject* Object : Objects)
        if (P4ObjectHasGameStockTags(Object))
            return true;
    return false;
}

TArray<FString> P4GameStockTagsLeftOut()
{
    return LastLeftOut;
}

bool P4TranslateGameStockTagCook(
    UPackage* Package, std::vector<uint8>& Header, std::vector<uint8>& Body, FString& Error)
{
    LastLeftOut.Reset();
    Error.Reset();
    struct FWanted
    {
        UObject* Object = nullptr;
        TArray<FRecord> Records;
    };
    TMap<FString, FWanted> Wanted;
    {
        TArray<UObject*> Objects;
        GetObjectsWithOuter(Package, Objects, true);
        for (UObject* Object : Objects)
        {
            FWanted One;
            One.Object = Object;
            if (!P4ObjectHasGameStockTags(Object))
                continue;
            if (!Read(Object, One.Records))
            {
                Error = TEXT("stock-tag-record-invalid: ") + Object->GetPathName();
                return false;
            }
            Wanted.Add(ExportPath(Object, Package), MoveTemp(One));
        }
    }
    if (!Wanted.Num())
        return true;
    const auto Native = P4MapKit::ReadPackage(Header, Body);
    if (!Native.Error.Code.empty())
    {
        Error = Utf(Native.Error.Code);
        return false;
    }
    auto Names = Native.Names;
    auto Imports = Native.Imports;
    const auto AddName = [&](const std::string& Name)
    {
        if (std::find(Names.begin(), Names.end(), Name) == Names.end())
            Names.push_back(Name);
    };
    // A reference by path becomes this package's own object, one it imports, or a new import of an asset
    // the project has (appending keeps every existing import and export index).
    const auto PathToReference = [&](const std::string& Wanted_, std::int32_t& Index) -> bool
    {
        const FString Path = Utf(Wanted_);
        std::string Known;
        for (std::size_t N = 0; N < Native.Exports.size(); ++N)
            if (Native.Reference(int32(N + 1), Known) && Path.Equals(Utf(Known), ESearchCase::IgnoreCase))
            {
                Index = int32(N + 1);
                return true;
            }
        P4MapKit::FPackageInventory Current = Native;
        Current.Imports = Imports;
        for (std::size_t N = 0; N < Imports.size(); ++N)
            if (Current.Reference(-int32(N + 1), Known) && Path.Equals(Utf(Known), ESearchCase::IgnoreCase))
            {
                Index = -int32(N + 1);
                return true;
            }
        int32 Dot = INDEX_NONE;
        if (!Path.StartsWith(TEXT("/")) || !Path.FindChar(TEXT('.'), Dot) || Path.Mid(Dot + 1).Contains(TEXT(".")) ||
            Path.Contains(TEXT(":")))
            return false;
        UObject* Target = LoadObject<UObject>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
        if (!Target)
            return false;
        const std::string Outer = Narrow(Path.Left(Dot)), Name = Narrow(Path.Mid(Dot + 1));
        int32 PackageIndex = 0;
        for (std::size_t N = 0; N < Imports.size(); ++N)
            if (!Imports[N].Outer && Imports[N].Class == "Package" &&
                FString(Utf(Imports[N].Name)).Equals(Utf(Outer), ESearchCase::IgnoreCase))
                PackageIndex = -int32(N + 1);
        if (!PackageIndex)
        {
            Imports.push_back({"/Script/CoreUObject", "Package", Outer, 0});
            PackageIndex = -int32(Imports.size());
        }
        Imports.push_back({Narrow(Target->GetClass()->GetOutermost()->GetName()), Narrow(Target->GetClass()->GetName()),
            Name, PackageIndex});
        Index = -int32(Imports.size());
        return true;
    };
    struct FApplied
    {
        std::size_t Export = 0;
        const FRecord* Record = nullptr;
        std::vector<FStep> Steps;
        std::vector<int> Indices;
    };
    std::vector<FApplied> Applied;
    std::map<std::size_t, Bytes> Replacements;
    TSet<FString> Seen;
    for (std::size_t At = 0; At < Native.Exports.size(); ++At)
    {
        std::string Path;
        if (!Native.Reference(int32(At + 1), Path))
        {
            Error = TEXT("stock-tag-cook-export-unnamed");
            return false;
        }
        FWanted* One = Wanted.Find(Utf(Path));
        if (!One)
            continue;
        Seen.Add(Utf(Path));
        const auto& Export = Native.Exports[At];
        P4StockTags::FReader Reader{Body, Native.Names, true};
        FList Root;
        std::size_t Position = Export.Offset;
        if (!Reader.List(Position, Export.Offset + Export.Size, Root, 0))
        {
            Error = TEXT("stock-tag-cook-unreadable: ") + Utf(Reader.Error);
            return false;
        }
        bool Changed = false;
        for (const FRecord& Record : One->Records)
        {
            std::vector<FStep> Steps;
            std::vector<int> Indices;
            FNode Node;
            std::string Why;
            if (!ParsePath(Record.Path, Steps) || !Stored(Record, Node, Why) ||
                Record.Steps.Num() != int32(Steps.size()) - 1)
            {
                Error = TEXT("stock-tag-record-invalid: ") + Utf(Path) + TEXT(" ") + Record.Path;
                return false;
            }
            const FString Told = Utf(Path) + TEXT(" ") + Record.Path;
            // A creator who removed or changed the element the value belongs to decides it.
            if (!Resolve(One->Object, Record, Steps, Indices))
            {
                LastLeftOut.Add(Told);
                continue;
            }
            FList* List = &Root;
            for (std::size_t Step = 0; Step + 1 < Steps.size(); ++Step)
            {
                const int Place = P4StockTags::FindAt(*List, Steps[Step].Name);
                if (Steps[Step].Index >= 0)
                {
                    FNode* Array = Place >= 0 ? &(*List)[std::size_t(Place)] : nullptr;
                    if (!Array || Array->Form != FNode::EForm::Array || !Array->StructArray ||
                        Indices[Step] >= int(Array->Elements.size()))
                    {
                        Error = TEXT("stock-tag-cook-has-no-place-for: ") + Told;
                        return false;
                    }
                    List = &Array->Elements[std::size_t(Indices[Step])];
                    continue;
                }
                if (Place >= 0)
                {
                    FNode& Struct = (*List)[std::size_t(Place)];
                    if (Struct.Form != FNode::EForm::Tagged)
                    {
                        Error = TEXT("stock-tag-cook-has-no-place-for: ") + Told;
                        return false;
                    }
                    List = &Struct.Children;
                    continue;
                }
                // This engine left the struct out: all of what it has there is its default. The game's file
                // has the struct, so it is written with the kept member alone.
                const FStepInfo& Info = Record.Steps[int32(Step)];
                FNode Struct;
                Struct.Name = {Steps[Step].Name, 0};
                Struct.Type = {"StructProperty", 0};
                Struct.Detail.push_back({Narrow(Info.Struct), 0});
                Struct.Form = FNode::EForm::Tagged;
                if (Info.Struct.IsEmpty() || !GuidWords(Info.Guid, Struct.StructGuid))
                {
                    Error = TEXT("stock-tag-record-invalid: ") + Told;
                    return false;
                }
                const std::size_t Insert = InsertAt(*List, Info.Before);
                List->insert(List->begin() + Insert, std::move(Struct));
                List = &(*List)[Insert].Children;
            }
            const int Existing = P4StockTags::FindAt(*List, Steps.back().Name);
            if (Record.Kind == TEXT("member"))
            {
                if (Existing >= 0)
                {
                    Error = TEXT("stock-tag-cook-would-shadow-native-field: ") + Told;
                    return false;
                }
                List->insert(List->begin() + InsertAt(*List, Record.Before), std::move(Node));
            }
            else
            {
                // The engine's own value of a choice the game added: the game's choice stands while the
                // creator left the engine's value as the import made it.
                FString Engine;
                if (Record.Sealed && (!EngineText(One->Object, Steps, Indices, Engine) || Engine != Record.Engine))
                {
                    LastLeftOut.Add(Told);
                    continue;
                }
                // Where the save already wrote the game's tag, nothing is changed.
                Bytes Ours, Game;
                std::vector<std::string> OurNames, GameNames;
                std::string Unused;
                if (Existing >= 0 && Canonical((*List)[std::size_t(Existing)], Ours, OurNames, Unused) &&
                    Canonical(Node, Game, GameNames, Unused) && Ours == Game && OurNames == GameNames)
                    continue;
                if (Existing >= 0)
                    (*List)[std::size_t(Existing)] = std::move(Node);
                else
                    List->insert(List->begin() + InsertAt(*List, Record.Before), std::move(Node));
            }
            Applied.push_back({At, &Record, Steps, Indices});
            Changed = true;
        }
        if (!Changed)
            continue;
        P4StockTags::FWriter Writer{Names};
        Bytes Rebuilt;
        Writer.PathToReference = PathToReference;
        if (!Writer.List(Rebuilt, Root))
        {
            Error = TEXT("stock-tag-cook-not-written: ") + Utf(Writer.Error);
            return false;
        }
        Rebuilt.insert(Rebuilt.end(), Body.begin() + Position, Body.begin() + Export.Offset + Export.Size);
        Replacements.emplace(At, std::move(Rebuilt));
    }
    if (Seen.Num() != Wanted.Num())
    {
        Error = TEXT("stock-tag-cook-object-not-serialized");
        return false;
    }
    if (Replacements.empty())
        return true;
    // Exports move only when something is written.
    if (!P4ValidateStockEngineCookRelocation(Header, Body, Error))
        return false;
    // The names of the imports added here. The package's own imports have theirs already (as a name and a number,
    // which the reader joins).
    for (std::size_t N = Native.Imports.size(); N < Imports.size(); ++N)
    {
        AddName(Imports[N].Name);
        AddName(Imports[N].Class);
        AddName(Imports[N].ClassPackage);
    }
    P4MapKit::Export::RewrittenPackage Candidate;
    std::string Why;
    if (!P4MapKit::Export::RebuildCookedExports(Header, Body, Names, Replacements, Candidate, Why))
    {
        Error = Utf(Why);
        return false;
    }
    if (Imports.size() != Native.Imports.size() &&
        !P4AppendCookedImports(Candidate.Header, Candidate.Body, Imports, Error))
    {
        if (Error.IsEmpty())
            Error = TEXT("stock-tag-cook-import-table-invalid");
        return false;
    }
    if (!P4RelocatePackageTextureImages(Header, Body, Candidate.Header, Candidate.Body, Error))
        return false;
    // Read the actual output again: a kept tag counts only when the written one carries the same tag and
    // decodes to the game's value.
    const auto Checked = P4MapKit::ReadPackage(Candidate.Header, Candidate.Body);
    if (!Checked.Error.Code.empty())
    {
        Error = Utf(Checked.Error.Code);
        return false;
    }
    for (const FApplied& One : Applied)
    {
        const auto& Export = Checked.Exports[One.Export];
        const FRecord& Record = *One.Record;
        P4StockTags::FReader Reader{Candidate.Body, Checked.Names, true};
        FList Root;
        std::size_t Position = Export.Offset;
        const FString Told = Utf(Export.Name) + TEXT(" ") + Record.Path;
        if (!Reader.List(Position, Export.Offset + Export.Size, Root, 0))
        {
            Error = TEXT("stock-tag-cook-readback-failed: ") + Told;
            return false;
        }
        const FList* List = &Root;
        const FNode* Found = nullptr;
        for (std::size_t Step = 0; List && Step < One.Steps.size(); ++Step)
        {
            const int Place = P4StockTags::FindAt(*List, One.Steps[Step].Name);
            const FNode* Node = Place >= 0 ? &(*List)[std::size_t(Place)] : nullptr;
            if (Step + 1 == One.Steps.size())
            {
                Found = Node;
                break;
            }
            if (!Node)
                List = nullptr;
            else if (One.Steps[Step].Index >= 0)
                List = Node->Form == FNode::EForm::Array && One.Indices[Step] < int(Node->Elements.size())
                    ? &Node->Elements[std::size_t(One.Indices[Step])]
                    : nullptr;
            else
                List = Node->Form == FNode::EForm::Tagged ? &Node->Children : nullptr;
        }
        Bytes A, B;
        std::vector<std::string> NamesA, NamesB;
        FNode Expected, Written;
        std::string Problem;
        FString Json;
        if (!Found || !Stored(Record, Expected, Problem) || !Strictly(*Found, Checked, Written, Problem) ||
            !Canonical(Expected, A, NamesA, Problem) || !Canonical(Written, B, NamesB, Problem) || A != B ||
            NamesA != NamesB)
        {
            Error = TEXT("stock-tag-cook-value-differs: ") + Told;
            return false;
        }
        if (!Decoded(Checked, Candidate.Body, Found->At, Record.Owner, Json) || Json != Record.Json)
        {
            Error = TEXT("stock-tag-cook-decoded-value-differs: ") + Told;
            return false;
        }
    }
    Header = std::move(Candidate.Header);
    Body = std::move(Candidate.Body);
    Error.Reset();
    return true;
}

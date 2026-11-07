// Private native regression draft, bound to the full 110/27 measured slice.
// Not compiled/executed by this worker. No feature or qualification claim.
#include "Policies/CondensedJsonPrintPolicy.h"
#include "P4AuthoringRows.h"
#include "P4NativeValueStaging.h"
#include "P4NativeHash.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Curves/CurveVector.h"
#include "Animation/AnimClassData.h"
#include "Serialization/ObjectWriter.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/StructuredArchive.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/StructOnScope.h"
#include "UObject/MetaData.h"
#include "UObject/UnrealType.h"
#include "UObject/PropertyPortFlags.h"

namespace P4RowsCoverage110
{
FString N110Quote(const FString& Value)
{
    FString Out;
    auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
    Writer->WriteArrayStart();
    Writer->WriteValue(Value);
    Writer->WriteArrayEnd();
    Writer->Close();
    return Out.Mid(1, Out.Len() - 2);
}
TSharedPtr<FJsonObject> N110Parse(const FString& Json)
{
    TSharedPtr<FJsonObject> Out;
    FJsonSerializer::Deserialize(
        TJsonReaderFactory<>::Create(Json), Out, FJsonSerializer::EFlags::StoreNumbersAsStrings);
    return Out;
}
FString N110Exact(const TSharedPtr<FJsonValue>& Value)
{
    if (!Value.IsValid())
        return FString();
    if (Value->Type == EJson::String)
        return N110Quote(Value->AsString());
    if (Value->Type == EJson::Number)
    {
        FString Token;
        return Value->TryGetString(Token) ? Token : FString();
    }
    if (Value->Type == EJson::Null)
        return TEXT("null");
    if (Value->Type == EJson::Boolean)
        return Value->AsBool() ? TEXT("true") : TEXT("false");
    TArray<FString> Parts;
    if (Value->Type == EJson::Array)
    {
        for (const auto& Child : Value->AsArray())
        {
            const FString Text = N110Exact(Child);
            if (Text.IsEmpty())
                return FString();
            Parts.Add(Text);
        }
        return TEXT("[") + FString::Join(Parts, TEXT(",")) + TEXT("]");
    }
    if (Value->Type == EJson::Object)
    {
        for (const auto& Field : Value->AsObject()->Values)
        {
            const FString Text = N110Exact(Field.Value);
            if (Text.IsEmpty())
                return FString();
            Parts.Add(N110Quote(Field.Key) + TEXT(":") + Text);
        }
        Parts.Sort();
        return TEXT("{") + FString::Join(Parts, TEXT(",")) + TEXT("}");
    }
    return FString();
}
TArray<uint8> N110Bytes(UObject* Object)
{
    TArray<uint8> Bytes;
    FObjectWriter Writer(Object, Bytes, false, false, false, PPF_DuplicateVerbatim);
    return Bytes;
}
struct FN110Suite
{
    FAutomationTestBase& Test;
    FString Scope, PackageRoot;
    bool Good = true;
    int32 Cases = 0;
    FN110Suite(FAutomationTestBase& InTest, const FString& Name) : Test(InTest)
    {
        const FString Token = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Scope = FPaths::ConvertRelativePathToFull(
            FPaths::ProjectSavedDir() / TEXT("P4NativeQuality/RowsCoverage110") / (Name + TEXT("_") + Token));
        PackageRoot = TEXT("/Game/NativeQuality/RowsCoverage110_") + Token;
        IFileManager::Get().MakeDirectory(*Scope, true);
    }
    bool Check(const FString& Label, bool Value)
    {
        Good = Test.TestTrue(Label, Value) && Good;
        return Value;
    }
    void Trace(const FString& Label, const FString& Input, const FString& Actual)
    {
        ++Cases;
        const FString Json = TEXT("{\"case\":") + N110Quote(Label) + TEXT(",\"input\":") + N110Quote(Input.Left(4096)) +
            TEXT(",\"actual\":") + N110Quote(Actual.Left(8192)) + TEXT("}");
        Check(Label + TEXT(": actual trace retained"),
            FFileHelper::SaveStringToFile(Json, *(Scope / FString::Printf(TEXT("case-%03d.json"), Cases)),
                FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    }
    bool Verdict(const FString& Label, const FString& Input, const FString& Actual, bool Expected)
    {
        Trace(Label, Input, Actual);
        auto Parsed = N110Parse(Actual);
        bool Valid = !Expected;
        FString Error;
        const bool Explicit = Parsed.IsValid() && Parsed->TryGetBoolField(TEXT("valid"), Valid);
        Check(Label + TEXT(": explicit native verdict"), Explicit);
        Check(Label, Explicit && Valid == Expected);
        if (!Expected)
            Check(Label + TEXT(": diagnostic retained"),
                Parsed.IsValid() && Parsed->TryGetStringField(TEXT("error"), Error) && !Error.IsEmpty());
        return Explicit && Valid == Expected;
    }
    void NoDisk()
    {
        TArray<FString> Files;
        IFileManager::Get().FindFilesRecursive(
            Files, *FPackageName::LongPackageNameToFilename(PackageRoot), TEXT("*"), true, false);
        Check(TEXT("No destination package or partial sidecar was saved"), Files.Num() == 0);
    }
};
// FKeyHandleMap is intentionally noncopyable and only serializes its handle
// array/index map to transaction archives. Preserve that actual engine cache in
// the copied curve; ordinary CopyCompleteValue alone leaves it empty. This is a
// test-only snapshot using the reflected native serializer, not production IO.
bool N110HandleCacheBytes(FStructProperty* Handles, void* CurveStorage, TArray<uint8>& Bytes)
{
    Bytes.Reset();
    FMemoryWriter Writer(Bytes);
    Writer.SetIsTransacting(true);
    Handles->SerializeItem(
        FStructuredArchiveFromArchive(Writer).GetSlot(), Handles->ContainerPtrToValuePtr<void>(CurveStorage), nullptr);
    return !Writer.IsError() && Writer.Tell() == Bytes.Num() && Bytes.Num() > 0;
}
struct FN110PropertyCopy
{
    FProperty* Property;
    FDefaultConstructedPropertyElement Value;
    FStructProperty* Handles = nullptr;
    TArray<TArray<uint8>> HandleCaches;
    bool Complete = true;
    FN110PropertyCopy(FProperty* In, UObject* Owner) : Property(In), Value(In)
    {
        Property->CopyCompleteValue(Value.GetObjAddress(), Property->ContainerPtrToValuePtr<void>(Owner));
        auto Curve = CastField<FStructProperty>(Property);
        if (Curve && Curve->Struct == FRichCurve::StaticStruct())
        {
            Handles = CastField<FStructProperty>(Curve->Struct->FindPropertyByName(TEXT("KeyHandlesToIndices")));
            Complete = Handles && Handles->Struct->GetPathName() == TEXT("/Script/Engine.KeyHandleMap") &&
                Handles->ArrayDim == 1;
            if (!Complete)
                return;
            for (int32 Slot = 0; Slot < Property->ArrayDim; ++Slot)
            {
                auto& Cache = HandleCaches.AddDefaulted_GetRef();
                if (!N110HandleCacheBytes(Handles, Property->ContainerPtrToValuePtr<void>(Owner, Slot), Cache))
                {
                    Complete = false;
                    return;
                }
                FMemoryReader Reader(Cache);
                Reader.SetIsTransacting(true);
                void* CopiedCurve = static_cast<uint8*>(Value.GetObjAddress()) + Slot * Property->ElementSize;
                Handles->SerializeItem(FStructuredArchiveFromArchive(Reader).GetSlot(),
                    Handles->ContainerPtrToValuePtr<void>(CopiedCurve), nullptr);
                if (Reader.IsError() || Reader.Tell() != Cache.Num())
                {
                    Complete = false;
                    return;
                }
            }
        }
    }
    bool Same(UObject* Owner, int32 Slot) const
    {
        if (!Complete)
            return false;
        if (Handles)
        {
            TArray<uint8> Actual;
            if (!HandleCaches.IsValidIndex(Slot) ||
                !N110HandleCacheBytes(Handles, Property->ContainerPtrToValuePtr<void>(Owner, Slot), Actual) ||
                Actual != HandleCaches[Slot])
                return false;
        }
        return Property->Identical(Property->ContainerPtrToValuePtr<void>(Owner, Slot),
            static_cast<const uint8*>(Value.GetObjAddress()) + Slot * Property->ElementSize, PPF_DeepComparison);
    }
};
struct FN110State
{
    UObject* Object;
    FString Path, Metadata;
    TArray<uint8> Bytes;
    EObjectFlags Flags;
    bool Dirty, HadMetadata;
    TArray<TUniquePtr<FN110PropertyCopy>> PropertyCopies;
    TStrongObjectPtr<UObject> Typed;
    int32 Pre = 0, Post = 0;
    FDelegateHandle Before, After;
    UScriptStruct* RowSchema = nullptr;
    TMap<FName, uint8*> RowPointers;
    TMap<FName, TArray<uint8>> RawRows;
    TMap<FName, TSharedPtr<FStructOnScope>> TypedRows;
    FN110State(UObject* In, bool ExactPropertyCopies = false)
        : Object(In), Path(In->GetPathName()),
          Metadata(In->GetOutermost()->GetMetaData()->GetValue(In, TEXT("P4Rows110Sentinel"))), Bytes(N110Bytes(In)),
          Flags(In->GetFlags()), Dirty(In->GetOutermost()->IsDirty()),
          HadMetadata(In->GetOutermost()->GetMetaData()->HasValue(In, TEXT("P4Rows110Sentinel"))),
          Typed(ExactPropertyCopies ? nullptr : DuplicateObject(In, GetTransientPackage()))
    {
        if (ExactPropertyCopies)
            for (TFieldIterator<FProperty> It(In->GetClass()); It; ++It)
                PropertyCopies.Add(MakeUnique<FN110PropertyCopy>(*It, In));
        if (auto Table = Cast<UDataTable>(Object))
        {
            RowSchema = Table->RowStruct;
            for (const auto& Row : Table->GetRowMap())
            {
                RowPointers.Add(Row.Key, Row.Value);
                RawRows.Add(Row.Key).Append(Row.Value, RowSchema->GetStructureSize());
                auto Copy = MakeShared<FStructOnScope>(RowSchema);
                RowSchema->CopyScriptStruct(Copy->GetStructMemory(), Row.Value);
                TypedRows.Add(Row.Key, Copy);
            }
        }
        Before = FCoreUObjectDelegates::OnPreObjectPropertyChanged.AddLambda(
            [this](UObject* Target, const FEditPropertyChain&)
            {
                if (Target == Object)
                    ++Pre;
            });
        After = FCoreUObjectDelegates::OnObjectPropertyChanged.AddLambda(
            [this](UObject* Target, FPropertyChangedEvent&)
            {
                if (Target == Object)
                    ++Post;
            });
    }
    ~FN110State()
    {
        FCoreUObjectDelegates::OnPreObjectPropertyChanged.Remove(Before);
        FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(After);
    }
    bool Primed(FN110Suite& F) const
    {
        bool Good = F.Check(
            TEXT("Native property-copy snapshot covers every reflected property"), PropertyCopies.Num() > 0);
        int32 Count = 0;
        for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
            ++Count;
        Good = F.Check(TEXT("No native property omitted from direct snapshot"), Count == PropertyCopies.Num()) && Good;
        for (const auto& Copy : PropertyCopies)
            for (int32 Slot = 0; Slot < Copy->Property->ArrayDim; ++Slot)
                Good = F.Check(FString::Printf(TEXT("Native snapshot prerequisite %s[%d] is immediately identical"),
                                   *Copy->Property->GetName(), Slot),
                           Copy->Same(Object, Slot)) &&
                    Good;
        return F.Check(TEXT("Native snapshot capture itself preserves full serialized bytes"),
                   N110Bytes(Object) == Bytes) &&
            Good;
    }
    void Same(FN110Suite& F) const
    {
        F.Check(TEXT("Exact native object pointer/path retained"), FindObject<UObject>(nullptr, *Path) == Object);
        F.Check(TEXT("Complete independent native serialized bytes retained"), N110Bytes(Object) == Bytes);
        if (PropertyCopies.Num())
        {
            for (const auto& Copy : PropertyCopies)
                for (int32 Slot = 0; Slot < Copy->Property->ArrayDim; ++Slot)
                    F.Check(FString::Printf(
                                TEXT("Exact copied native property %s[%d] retained"), *Copy->Property->GetName(), Slot),
                        Copy->Same(Object, Slot));
        }
        else
        {
            for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
                for (int32 Slot = 0; Slot < It->ArrayDim; ++Slot)
                    F.Check(TEXT("Every native property/fixed slot retained"),
                        It->Identical_InContainer(Object, Typed.Get(), Slot, PPF_DeepComparison));
        }
        if (auto Table = Cast<UDataTable>(Object))
        {
            F.Check(TEXT("Exact native table schema and row count retained"),
                Table->RowStruct == RowSchema && Table->GetRowMap().Num() == RowPointers.Num());
            for (const auto& Row : RowPointers)
            {
                const auto Current = Table->GetRowMap().Find(Row.Key);
                if (!F.Check(TEXT("Exact native row key/storage pointer retained"), Current && *Current == Row.Value))
                    continue;
                F.Check(TEXT("Full native row byte snapshot retained"),
                    FMemory::Memcmp(*Current, RawRows[Row.Key].GetData(), RawRows[Row.Key].Num()) == 0);
                for (TFieldIterator<FProperty> It(RowSchema); It; ++It)
                    for (int32 Slot = 0; Slot < It->ArrayDim; ++Slot)
                        F.Check(TEXT("Every typed row field and fixed slot retained"),
                            It->Identical_InContainer(
                                *Current, TypedRows[Row.Key]->GetStructMemory(), Slot, PPF_DeepComparison));
            }
        }
        F.Check(TEXT("Exact native flags and package dirty state retained"),
            Object->GetFlags() == Flags && Object->GetOutermost()->IsDirty() == Dirty);
        auto Tags = Object->GetOutermost()->GetMetaData();
        F.Check(TEXT("Exact existing metadata presence/value retained"),
            Tags->HasValue(Object, TEXT("P4Rows110Sentinel")) == HadMetadata &&
                Tags->GetValue(Object, TEXT("P4Rows110Sentinel")) == Metadata);
        F.Check(TEXT("No destination edit callbacks on refusal/read-only validation"), Pre == 0 && Post == 0);
    }
};
void N110OwnerSentinel(UObject* Object)
{
    Object->GetOutermost()->GetMetaData()->SetValue(
        Object, TEXT("P4Rows110Sentinel"), TEXT("retain-this-exact-owner-state"));
    Object->GetOutermost()->SetDirtyFlag(false);
}
FString N110MapInput(const FString& Key, const FString& Pairs)
{
    return TEXT("{") + N110Quote(Key) + TEXT(":") + Pairs + TEXT("}");
}
FString N110CountInput(const FString& Pairs)
{
    return TEXT("{\"ActorPlacedNumInfo\":{\"ExploreRateActorNum\":") + Pairs + TEXT("}}");
}
bool N110PrivateRefuses(FN110Suite& F, const FString& Label, UObject* Object, const FString& Json)
{
    FString Canonical(TEXT("stale-transport")), Error;
    const bool Actual = P4PrepareExactPrivateProperties(Object, Json, Canonical, Error);
    F.Trace(Label, Json,
        FString(Actual ? TEXT("true") : TEXT("false")) + TEXT("; canonical=") + Canonical + TEXT("; error=") + Error);
    return F.Check(Label, !Actual && Canonical.IsEmpty() && !Error.IsEmpty());
}
} //namespace P4RowsCoverage110

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4Rows110EnumReferenceMapMatrix, "P4MapKit.Quality.Rows110EnumReferenceMapMatrix",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4Rows110EnumReferenceMapMatrix::RunTest(const FString&)
{
    using namespace P4RowsCoverage110;
    FN110Suite F(*this, TEXT("EnumReferenceMapMatrix"));
    auto Class = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.VsPokerParameter"));
    auto Map = Class ? CastField<FMapProperty>(Class->FindPropertyByName(TEXT("PorkerHandTableMap"))) : nullptr;
    auto Key = Map ? CastField<FEnumProperty>(Map->KeyProp) : nullptr;
    auto Value = Map ? CastField<FObjectProperty>(Map->ValueProp) : nullptr;
    auto Sibling = Class ? CastField<FEnumProperty>(Class->FindPropertyByName(TEXT("GameMode"))) : nullptr;
    if (!F.Check(TEXT("Real game enum/hard table map native schema exists without modified flags"),
            Class && Map && Key && Value && Sibling && Value->PropertyClass == UDataTable::StaticClass()))
        return false;
    const FString AName(TEXT("EVsPokerGameMode::OneCard")), BName(TEXT("EVsPokerGameMode::TowCard"));
    const int64 AKey = Key->GetEnum()->GetValueByNameString(AName), BKey = Key->GetEnum()->GetValueByNameString(BName);
    if (!F.Check(TEXT("Original native enum spellings resolve independently"), AKey == 1 && BKey == 2))
        return false;
    UPackage* Package = CreatePackage(*(F.PackageRoot / TEXT("Poker")));
    TStrongObjectPtr<UObject> Asset(NewObject<UObject>(Package, Class, TEXT("Poker"), RF_Transactional));
    TStrongObjectPtr<UDataTable> A(NewObject<UDataTable>(Package, TEXT("FirstTable"), RF_Transient));
    A->RowStruct = FTableRowBase::StaticStruct();
    TStrongObjectPtr<UDataTable> B(NewObject<UDataTable>(Package, TEXT("SecondTable"), RF_Transient));
    B->RowStruct = FTableRowBase::StaticStruct();
    FScriptMapHelper Native(Map, Map->ContainerPtrToValuePtr<void>(Asset.Get()));
    Native.EmptyValues();
    const int32 Entry = Native.AddDefaultValue_Invalid_NeedsRehash();
    Key->GetUnderlyingProperty()->SetIntPropertyValue(Native.GetKeyPtr(Entry), uint64(AKey));
    Value->SetObjectPropertyValue(Native.GetValuePtr(Entry), A.Get());
    Native.Rehash();
    Sibling->GetUnderlyingProperty()->SetIntPropertyValue(
        Sibling->ContainerPtrToValuePtr<void>(Asset.Get()), uint64(AKey));
    const FString Source = N110MapInput(
        Map->GetName(), TEXT("[[") + N110Quote(AName) + TEXT(",") + N110Quote(A->GetPathName()) + TEXT("]]"));
    if (!F.Verdict(TEXT("Actual enum/reference map readback control"), Source,
            UP4AuthoringTools::NativeAssetReadback(Asset.Get(), Source), true))
        return false;
    N110OwnerSentinel(Asset.Get());
    {
        FN110State Held(Asset.Get());
        TArray<FString> Keys = {TEXT("null"), TEXT("false"), TEXT("1"),
            N110Quote(TEXT("EVsPokerGameMode::NoSuchActualMember")), N110Quote(FString::ChrN(NAME_SIZE, TEXT('e')))};
        for (const auto& Token : Keys)
        {
            const FString Input = N110MapInput(
                Map->GetName(), TEXT("[[") + Token + TEXT(",") + N110Quote(A->GetPathName()) + TEXT("]]"));
            F.Verdict(TEXT("Native enum map key refusal"), Input,
                UP4AuthoringTools::NativeCanonicalAssetValues(Asset.Get(), Input), false);
            Held.Same(F);
        }
        const TArray<FString> Values = {TEXT("false"), TEXT("17"), TEXT("{}"), N110Quote(TEXT("relative.path")),
            N110Quote(F.PackageRoot + TEXT("/Missing.Missing")), N110Quote(Asset->GetPathName())};
        for (const auto& Token : Values)
        {
            const FString Input = N110MapInput(
                Map->GetName(), TEXT("[[") + N110Quote(AName) + TEXT(",") + Token + TEXT("]]"));
            F.Verdict(TEXT("Real hard table reference map refusal"), Input,
                UP4AuthoringTools::NativeCanonicalAssetValues(Asset.Get(), Input), false);
            Held.Same(F);
        }
        const FString Duplicate = N110MapInput(Map->GetName(),
            TEXT("[[") + N110Quote(AName) + TEXT(",") + N110Quote(A->GetPathName()) + TEXT("],[") + N110Quote(AName) +
                TEXT(",") + N110Quote(B->GetPathName()) + TEXT("]]"));
        F.Verdict(TEXT("Duplicate genuine native enum identity refuses"), Duplicate,
            UP4AuthoringTools::NativeCanonicalAssetValues(Asset.Get(), Duplicate), false);
        Held.Same(F);
        for (const TCHAR* NullValue : {TEXT("null"), TEXT("\"None\""), TEXT("\"\"")})
        {
            const FString Input = N110MapInput(
                Map->GetName(), TEXT("[[") + N110Quote(AName) + TEXT(",") + NullValue + TEXT("]]"));
            F.Verdict(TEXT("Explicit absent hard reference canonical control"), Input,
                UP4AuthoringTools::NativeCanonicalAssetValues(Asset.Get(), Input), true);
            const FString Report = UP4AuthoringTools::NativeAssetReferences(Asset.Get(), Input);
            if (F.Verdict(TEXT("Explicit null map has no dependency"), Input, Report, true))
                F.Check(TEXT("Native null dependency inventory is exactly empty"),
                    N110Parse(Report)->GetArrayField(TEXT("references")).Num() == 0);
            Held.Same(F);
        }
        const FString Two = N110MapInput(Map->GetName(),
            TEXT("[[") + N110Quote(BName) + TEXT(",") + N110Quote(B->GetPathName()) + TEXT("],[") + N110Quote(AName) +
                TEXT(",") + N110Quote(A->GetPathName()) + TEXT("]]"));
        const FString Report = UP4AuthoringTools::NativeAssetReferences(Asset.Get(), Two);
        if (F.Verdict(TEXT("Actual two-target map dependency inventory"), Two, Report, true))
        {
            const auto Refs = N110Parse(Report)->GetArrayField(TEXT("references"));
            TSet<FString> Paths;
            for (const auto& Ref : Refs)
            {
                if (!F.Check(TEXT("Independent dependency record has path/class/property-kind triple"),
                        Ref->Type == EJson::Array && Ref->AsArray().Num() == 3))
                    continue;
                const auto Triple = Ref->AsArray();
                Paths.Add(Triple[0]->AsString());
                F.Check(TEXT("Actual map dependency native class/kind retained"),
                    Triple[1]->AsString() == UDataTable::StaticClass()->GetPathName() &&
                        Triple[2]->AsString() == TEXT("ObjectProperty"));
            }
            F.Check(TEXT("Exactly both real native table dependencies retained"),
                Refs.Num() == 2 && Paths.Num() == 2 && Paths.Contains(A->GetPathName()) &&
                    Paths.Contains(B->GetPathName()));
        }
        Held.Same(F);
    }
    Value->SetObjectPropertyValue(Native.GetValuePtr(Entry), B.Get());
    N110OwnerSentinel(Asset.Get());
    {
        FN110State Changed(Asset.Get());
        F.Verdict(TEXT("Actual native reference substitution refuses unchanged source"), Source,
            UP4AuthoringTools::NativeAssetReadback(Asset.Get(), Source), false);
        Changed.Same(F);
    }
    Value->SetObjectPropertyValue(Native.GetValuePtr(Entry), A.Get());
    Key->GetUnderlyingProperty()->SetIntPropertyValue(Native.GetKeyPtr(Entry), uint64(BKey));
    Native.Rehash();
    N110OwnerSentinel(Asset.Get());
    {
        FN110State Changed(Asset.Get());
        F.Verdict(TEXT("Actual native enum key substitution refuses unchanged source"), Source,
            UP4AuthoringTools::NativeAssetReadback(Asset.Get(), Source), false);
        Changed.Same(F);
    }
    F.NoDisk();
    return F.Good && !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4Rows110NativeFixedSlotMatrix, "P4MapKit.Quality.Rows110NativeFixedSlotMatrix",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4Rows110NativeFixedSlotMatrix::RunTest(const FString&)
{
    using namespace P4RowsCoverage110;
    FN110Suite F(*this, TEXT("NativeFixedSlotMatrix"));
    TStrongObjectPtr<UCurveVector> Curve(NewObject<UCurveVector>(GetTransientPackage(), NAME_None, RF_Transient));
    auto Fixed = CastField<FStructProperty>(UCurveVector::StaticClass()->FindPropertyByName(TEXT("FloatCurves")));
    if (!F.Check(
            TEXT("Genuine engine fixed curve schema has exactly three native slots"), Fixed && Fixed->ArrayDim == 3))
        return false;
    for (int32 Index = 0; Index < 3; ++Index)
        Curve->FloatCurves[Index].AddKey(float(Index + 1), float(Index + 17));
    const FString Whole = TEXT(
        "{\"FloatCurves\":[{\"DefaultValue\":1.25},{\"DefaultValue\":-0.0},{\"DefaultValue\":3.5}]}");
    const FString Split = TEXT(
        "{\"floatcurves[2]\":{\"DefaultValue\":3.5},\"FloatCurves\":{\"DefaultValue\":1.25},\"FLOATCURVES[1]\":{\"DefaultValue\":-0.0}}");
    for (const auto& Input : {Whole, Split})
    {
        FString Canonical, Error;
        const bool Actual = P4PrepareExactPrivateProperties(Curve.Get(), Input, Canonical, Error);
        F.Trace(TEXT("Actual complete whole/split fixed slot preparation"), Input,
            FString(Actual ? TEXT("true") : TEXT("false")) + TEXT("; canonical=") + Canonical + TEXT("; error=") +
                Error);
        if (!F.Check(TEXT("Complete native fixed slots canonicalize and prepare"), Actual && !Canonical.IsEmpty()))
            return false;
        const auto Parsed = N110Parse(Canonical);
        const auto Slots = Parsed.IsValid() ? Parsed->TryGetField(TEXT("FloatCurves")) : nullptr;
        F.Check(TEXT("All split aliases canonicalize to one exact fixed-array field"),
            Parsed.IsValid() && Parsed->Values.Num() == 1 && Slots.IsValid() && Slots->Type == EJson::Array &&
                Slots->AsArray().Num() == 3);
        uint32 ZeroBits = 0;
        const float Zero = Curve->FloatCurves[1].DefaultValue;
        FMemory::Memcpy(&ZeroBits, &Zero, sizeof(ZeroBits));
        F.Check(TEXT("Actual native default values and signed-zero bits retained"),
            Curve->FloatCurves[0].DefaultValue == 1.25f && ZeroBits == 0x80000000u &&
                Curve->FloatCurves[2].DefaultValue == 3.5f);
        for (int32 Index = 0; Index < 3; ++Index)
        {
            const auto& Keys = Curve->FloatCurves[Index].GetConstRefOfKeys();
            F.Check(TEXT("Unspecified actual curve keys survive partial slot preparation"),
                Keys.Num() == 1 && Keys[0].Time == float(Index + 1) && Keys[0].Value == float(Index + 17));
        }
    }
    TArray<uint8> HeldKeys[3];
    uint32 HeldDefaultBits[3];
    TEnumAsByte<ERichCurveExtrapolation> HeldPre[3], HeldPost[3];
    FKeyHandle HeldHandles[3];
    for (int32 Index = 0; Index < 3; ++Index)
    {
        HeldHandles[Index] = Curve->FloatCurves[Index].GetFirstKeyHandle();
        const auto& Keys = Curve->FloatCurves[Index].GetConstRefOfKeys();
        HeldKeys[Index].Append(
            reinterpret_cast<const uint8*>(Keys.GetData()), Keys.Num() * int32(sizeof(FRichCurveKey)));
        FMemory::Memcpy(&HeldDefaultBits[Index], &Curve->FloatCurves[Index].DefaultValue, sizeof(uint32));
        HeldPre[Index] = Curve->FloatCurves[Index].PreInfinityExtrap;
        HeldPost[Index] = Curve->FloatCurves[Index].PostInfinityExtrap;
    }
    FN110State Held(Curve.Get(), true);
    if (!Held.Primed(F))
        return false; // A failed oracle cannot diagnose a production refusal.
    TArray<FString> Invalid = {TEXT("{\"FloatCurves\":[]}"), TEXT("{\"FloatCurves\":[{},{}]}"),
        TEXT("{\"FloatCurves\":[{},{},{},{}]}"), TEXT("{\"FloatCurves[0]\":{},\"FloatCurves[1]\":{}}"),
        TEXT("{\"FloatCurves[3]\":{}}"), TEXT("{\"FloatCurves[-1]\":{}}"), TEXT("{\"FloatCurves[2147483648]\":{}}"),
        TEXT("{\"FloatCurves[0]\":{},\"floatcurves[0]\":{},\"FloatCurves[1]\":{},\"FloatCurves[2]\":{}}"),
        TEXT("{\"FloatCurves\":[{},{},{}],\"FloatCurves[0]\":{}}"),
        TEXT("{\"FloatCurves\":[{\"DefaultValue\":true},{},{}]}"),
        TEXT("{\"FloatCurves\":[{},{},{\"UnknownNativeCurveField\":1}]}"),
        TEXT("{\"FloatCurves\":[{},{},{\"PreInfinityExtrap\":\"NotAnActualCurveEnum\"}]}"),
        TEXT("{\"FloatCurves\":[{},{},{\"Keys\":false}]}"), TEXT("{\"FloatCurves\":[{},{},{\"Keys\":[null]}]}"),
        TEXT("{\"FloatCurves\":[{},{},{\"Keys\":[{\"Time\":\"1\",\"Value\":2}]}]}"),
        TEXT("{\"FloatCurves\":[{},{},{\"DefaultValue\":3.5e38}]}"),
        TEXT("{\"FloatCurves\":[{},{},{\"DefaultValue\":-3.5e38}]}")};
    Invalid.Add(TEXT("{\"FloatCurves\":[{},{},{\"PreInfinityExtrap\":") +
        N110Quote(FString::ChrN(NAME_SIZE, TEXT('e'))) + TEXT("}]}"));
    for (const auto& Input : Invalid)
    {
        N110PrivateRefuses(F, TEXT("Real native fixed slot/schema/value refusal"), Curve.Get(), Input);
        Held.Same(F);
        for (int32 Index = 0; Index < 3; ++Index)
        {
            const auto& Native = Curve->FloatCurves[Index];
            const auto& Keys = Native.GetConstRefOfKeys();
            uint32 DefaultBits = 0;
            FMemory::Memcpy(&DefaultBits, &Native.DefaultValue, sizeof(uint32));
            F.Check(TEXT("Independent full native key bytes and all float members retained"),
                Keys.Num() == 1 && HeldKeys[Index].Num() == int32(sizeof(FRichCurveKey)) &&
                    FMemory::Memcmp(Keys.GetData(), HeldKeys[Index].GetData(), HeldKeys[Index].Num()) == 0);
            F.Check(TEXT("Independent curve default bits and both extrapolation modes retained"),
                DefaultBits == HeldDefaultBits[Index] && Native.PreInfinityExtrap == HeldPre[Index] &&
                    Native.PostInfinityExtrap == HeldPost[Index]);
            F.Check(TEXT("Existing actual key handle and index mapping retained"),
                Native.GetFirstKeyHandle() == HeldHandles[Index] && Native.GetIndexSafe(HeldHandles[Index]) == 0);
        }
    }
    F.NoDisk();
    return F.Good && !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4Rows110NativeNameMapMatrix, "P4MapKit.Quality.Rows110NativeNameMapMatrix",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4Rows110NativeNameMapMatrix::RunTest(const FString&)
{
    using namespace P4RowsCoverage110;
    FN110Suite F(*this, TEXT("NativeNameMapMatrix"));
    TStrongObjectPtr<UAnimClassData> Object(NewObject<UAnimClassData>(GetTransientPackage(), NAME_None, RF_Transient));
    auto Property = CastField<FMapProperty>(Object->GetClass()->FindPropertyByName(TEXT("GraphBlendOptions")));
    auto Key = Property ? CastField<FNameProperty>(Property->KeyProp) : nullptr;
    auto Value = Property ? CastField<FStructProperty>(Property->ValueProp) : nullptr;
    if (!F.Check(TEXT("Installed engine has genuine name-to-blend-struct map"),
            Property && Key && Value && Value->Struct->FindPropertyByName(TEXT("BlendInTime")) &&
                Value->Struct->FindPropertyByName(TEXT("BlendOutTime"))))
        return false;
    const FString Input =
        TEXT("{\"GraphBlendOptions\":[[\"Menu, (left)\",{\"BlendInTime\":1.25,\"BlendOutTime\":2.5}],[") +
        N110Quote(TEXT("Menu \"right\" \\path")) + TEXT(",{\"BlendInTime\":3.5,\"BlendOutTime\":4.75}]]}");
    FString Canonical, Error;
    const bool Actual = P4PrepareExactPrivateProperties(Object.Get(), Input, Canonical, Error);
    F.Trace(TEXT("Actual native name-map preparation control"), Input,
        FString(Actual ? TEXT("true") : TEXT("false")) + TEXT("; canonical=") + Canonical + TEXT("; error=") + Error);
    if (!F.Check(TEXT("Quoted actual native map names and values prepare exactly"), Actual && !Canonical.IsEmpty()))
        return false;
    const auto& Map = Object->GetGraphBlendOptions();
    const auto First = Map.Find(FName(TEXT("Menu, (left)"))), Second = Map.Find(FName(TEXT("Menu \"right\" \\path")));
    if (!F.Check(TEXT("Independent native map preserves both escaped identities and float members"),
            Map.Num() == 2 && First && Second && First->BlendInTime == 1.25f && First->BlendOutTime == 2.5f &&
                Second->BlendInTime == 3.5f && Second->BlendOutTime == 4.75f))
        return false;
    FN110State Held(Object.Get());
    TArray<FString> Invalid = {TEXT("null"), TEXT("{}"), TEXT("[null]"), TEXT("[[\"native\"]]"),
        TEXT("[[\"native\",{},17]]"), TEXT("[[null,{}]]"), TEXT("[[false,{}]]"), TEXT("[[1,{}]]"),
        TEXT("[[\"native\",false]]"), TEXT("[[\"native\",[]]]"), TEXT("[[\"Native\",{}],[\"NATIVE\",{}]]"),
        TEXT("[[\"native\",{\"BlendInTime\":false}]]"),
        TEXT("[[\"native\",{\"BlendInTime\":1.25,\"UnknownNativeMember\":2.5}]]"),
        TEXT("[[\"native\",{\"BlendOutTime\":3.5e38}]]"), TEXT("[[\"native\\u0000suffix\",{}]]")};
    Invalid.Add(TEXT("[[") + N110Quote(FString::ChrN(NAME_SIZE, TEXT('n'))) + TEXT(",{}]]"));
    for (const auto& Pairs : Invalid)
    {
        N110PrivateRefuses(F, TEXT("Actual FName map schema/key/value refusal"), Object.Get(),
            N110MapInput(Property->GetName(), Pairs));
        Held.Same(F);
    }
    F.NoDisk();
    return F.Good && !HasAnyErrors();
}
#endif

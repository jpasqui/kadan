// The helpers that P4AuthoringRowsInspectionCoverageAutomation.cpp and its companion test files share.
#pragma once
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
#include "Serialization/JsonSerializer.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/StructOnScope.h"
#include "UObject/MetaData.h"
#include "UObject/UnrealType.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/SoftObjectPtr.h"
#include "Async/Async.h"

namespace P4RowsCoverage138
{
inline FString N138Quote(const FString& Value)
{
    FString Out;
    auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
    Writer->WriteArrayStart();
    Writer->WriteValue(Value);
    Writer->WriteArrayEnd();
    Writer->Close();
    return Out.Mid(1, Out.Len() - 2);
}
inline TSharedPtr<FJsonObject> N138Parse(const FString& Json)
{
    TSharedPtr<FJsonObject> Out;
    FJsonSerializer::Deserialize(
        TJsonReaderFactory<>::Create(Json), Out, FJsonSerializer::EFlags::StoreNumbersAsStrings);
    return Out;
}
inline FString N138Exact(const TSharedPtr<FJsonValue>& Value)
{
    if (!Value.IsValid())
        return FString();
    if (Value->Type == EJson::String)
        return N138Quote(Value->AsString());
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
            const FString Text = N138Exact(Child);
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
            const FString Text = N138Exact(Field.Value);
            if (Text.IsEmpty())
                return FString();
            Parts.Add(N138Quote(Field.Key) + TEXT(":") + Text);
        }
        Parts.Sort();
        return TEXT("{") + FString::Join(Parts, TEXT(",")) + TEXT("}");
    }
    return FString();
}
inline TArray<uint8> N138Bytes(UObject* Object)
{
    TArray<uint8> Bytes;
    FObjectWriter Writer(Object, Bytes, false, false, false, PPF_DuplicateVerbatim);
    return Bytes;
}
struct FN138Suite
{
    FAutomationTestBase& Test;
    FString Scope, PackageRoot;
    bool Good = true;
    int32 Cases = 0;
    FN138Suite(FAutomationTestBase& InTest, const FString& Name) : Test(InTest)
    {
        const FString Token = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Scope = FPaths::ConvertRelativePathToFull(
            FPaths::ProjectSavedDir() / TEXT("P4NativeQuality/RowsCoverage138") / (Name + TEXT("_") + Token));
        PackageRoot = TEXT("/Game/NativeQuality/RowsCoverage138_") + Token;
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
        const FString Json = TEXT("{\"case\":") + N138Quote(Label) + TEXT(",\"input\":") + N138Quote(Input.Left(4096)) +
            TEXT(",\"actual\":") + N138Quote(Actual.Left(8192)) + TEXT("}");
        Check(Label + TEXT(": actual trace retained"),
            FFileHelper::SaveStringToFile(Json, *(Scope / FString::Printf(TEXT("case-%03d.json"), Cases)),
                FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    }
    bool Verdict(const FString& Label, const FString& Input, const FString& Actual, bool Expected)
    {
        Trace(Label, Input, Actual);
        auto Parsed = N138Parse(Actual);
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
// Property-only snapshots avoid DuplicateObject's ConditionalPostLoad. In
// UE4.26 that hook recomputes a UPrimaryAssetLabel's AssetBundleData, so a
// duplicate is already a different native value before the operation under test.
// Each reflected property is initialized/copied/destroyed through its real native
// FProperty. All fixed slots must compare equal immediately and after refusal.
struct FN201PropertySnapshot
{
    FProperty* Property;
    FDefaultConstructedPropertyElement Value;
    FN201PropertySnapshot(FProperty* In, UObject* Owner) : Property(In), Value(In)
    {
        Property->CopyCompleteValue(Value.GetObjAddress(), Property->ContainerPtrToValuePtr<void>(Owner));
    }
    bool Same(UObject* Owner, int32 Slot) const
    {
        return Property->Identical(Property->ContainerPtrToValuePtr<void>(Owner, Slot),
            static_cast<const uint8*>(Value.GetObjAddress()) + Slot * Property->ElementSize, PPF_DeepComparison);
    }
};
struct FN138State
{
    UObject* Object;
    FString Path, Metadata;
    TArray<uint8> Bytes;
    EObjectFlags Flags;
    bool Dirty, HadMetadata;
    TArray<TUniquePtr<FN201PropertySnapshot>> PropertyCopies;
    TStrongObjectPtr<UObject> Typed;
    int32 Pre = 0, Post = 0;
    FDelegateHandle Before, After;
    UScriptStruct* RowSchema = nullptr;
    TMap<FName, uint8*> RowPointers;
    TMap<FName, TArray<uint8>> RawRows;
    TMap<FName, TSharedPtr<FStructOnScope>> TypedRows;
    FN138State(UObject* In, bool ExactPropertyCopies = false)
        : Object(In), Path(In->GetPathName()),
          Metadata(In->GetOutermost()->GetMetaData()->GetValue(In, TEXT("P4Rows138Sentinel"))), Bytes(N138Bytes(In)),
          Flags(In->GetFlags()), Dirty(In->GetOutermost()->IsDirty()),
          HadMetadata(In->GetOutermost()->GetMetaData()->HasValue(In, TEXT("P4Rows138Sentinel"))),
          Typed(ExactPropertyCopies ? nullptr : DuplicateObject(In, GetTransientPackage()))
    {
        if (ExactPropertyCopies)
            for (TFieldIterator<FProperty> It(In->GetClass()); It; ++It)
                PropertyCopies.Add(MakeUnique<FN201PropertySnapshot>(*It, In));
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
    ~FN138State()
    {
        FCoreUObjectDelegates::OnPreObjectPropertyChanged.Remove(Before);
        FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(After);
    }
    void Same(FN138Suite& F) const
    {
        F.Check(TEXT("Exact native object pointer/path retained"), FindObject<UObject>(nullptr, *Path) == Object);
        F.Check(TEXT("Complete independent native serialized bytes retained"), N138Bytes(Object) == Bytes);
        if (PropertyCopies.Num())
        {
            int32 PropertyCount = 0;
            for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
                ++PropertyCount;
            F.Check(TEXT("Complete reflected property snapshot count retained"), PropertyCount == PropertyCopies.Num());
            for (const auto& Copy : PropertyCopies)
                for (int32 Slot = 0; Slot < Copy->Property->ArrayDim; ++Slot)
                    F.Check(FString::Printf(TEXT("Every native property/fixed slot retained: %s[%d]"),
                                *Copy->Property->GetName(), Slot),
                        Copy->Same(Object, Slot));
        }
        else
            for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
                for (int32 Slot = 0; Slot < It->ArrayDim; ++Slot)
                    F.Check(FString::Printf(
                                TEXT("Every native property/fixed slot retained: %s[%d]"), *It->GetName(), Slot),
                        It->Identical_InContainer(Object, Typed.Get(), Slot, PPF_DeepComparison));
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
            Tags->HasValue(Object, TEXT("P4Rows138Sentinel")) == HadMetadata &&
                Tags->GetValue(Object, TEXT("P4Rows138Sentinel")) == Metadata);
        F.Check(TEXT("No destination edit callbacks on refusal/read-only validation"), Pre == 0 && Post == 0);
    }
};
inline FString N138BaselineWith(const FString& Base, const FString& Key, const FString& Literal)
{
    // Preserve the raw replacement token. UE's FString JSON writer elides
    // embedded NUL, so parsing then quoting a deliberate \u0000 corruption
    // would silently replace the test input with an ordinary valid string.
    const auto Object = N138Parse(Base);
    if (!Object.IsValid())
        return FString();
    TArray<FString> Fields;
    for (const auto& Field : Object->Values)
    {
        if (Field.Key == Key)
            continue;
        const FString Existing = N138Exact(Field.Value);
        if (Existing.IsEmpty())
            return FString();
        Fields.Add(N138Quote(Field.Key) + TEXT(":") + Existing);
    }
    if (!Literal.IsEmpty())
        Fields.Add(N138Quote(Key) + TEXT(":") + Literal);
    Fields.Sort();
    return TEXT("{") + FString::Join(Fields, TEXT(",")) + TEXT("}");
}
} //namespace P4RowsCoverage138
#endif

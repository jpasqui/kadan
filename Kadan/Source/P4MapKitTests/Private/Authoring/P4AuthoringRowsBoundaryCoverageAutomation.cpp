// Private regression draft against the full 89-test/27-workflow measurement.
// Not compiled or executed by this worker. No qualification or coverage claim.
#include "Policies/CondensedJsonPrintPolicy.h"
#include "P4AuthoringRows.h"
#include "P4NativeValueStaging.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "Async/Async.h"
#include "Curves/CurveVector.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/MetaData.h"
#include "UObject/UnrealType.h"
#include "UObject/PropertyPortFlags.h"

namespace P4RowsCoverage89
{
FString R89Quote(const FString& Value)
{
    FString Out;
    auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
    Writer->WriteArrayStart();
    Writer->WriteValue(Value);
    Writer->WriteArrayEnd();
    Writer->Close();
    return Out.Mid(1, Out.Len() - 2);
}
TSharedPtr<FJsonObject> R89Parse(const FString& Json)
{
    TSharedPtr<FJsonObject> Out;
    FJsonSerializer::Deserialize(
        TJsonReaderFactory<>::Create(Json), Out, FJsonSerializer::EFlags::StoreNumbersAsStrings);
    return Out;
}
TArray<uint8> R89Bytes(UObject* Object)
{
    TArray<uint8> Out;
    FObjectWriter Writer(Object, Out, false, false, false, PPF_DuplicateVerbatim);
    return Out;
}
uint32 R89FloatBits(const void* Storage)
{
    uint32 Out = 0;
    FMemory::Memcpy(&Out, Storage, sizeof(Out));
    return Out;
}
struct FR89Suite
{
    FAutomationTestBase& Test;
    FString Scope, PackageRoot;
    int32 Cases = 0;
    bool Good = true;
    FR89Suite(FAutomationTestBase& InTest, const FString& Name) : Test(InTest)
    {
        const FString Token = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Scope = FPaths::ConvertRelativePathToFull(
            FPaths::ProjectSavedDir() / TEXT("P4NativeQuality/RowsCoverage89") / (Name + TEXT("_") + Token));
        PackageRoot = TEXT("/Game/NativeQuality/RowsCoverage89_") + Token;
        IFileManager::Get().MakeDirectory(*Scope, true);
    }
    bool Check(const FString& Name, bool Value)
    {
        Good = Test.TestTrue(Name, Value) && Good;
        return Value;
    }
    void Trace(const FString& Name, const FString& Input, const FString& Actual)
    {
        ++Cases;
        const FString Json = TEXT("{\"case\":") + R89Quote(Name) + TEXT(",\"input\":") + R89Quote(Input.Left(4096)) +
            TEXT(",\"actual\":") + R89Quote(Actual.Left(8192)) + TEXT("}");
        Check(Name + TEXT(": actual result retained"),
            FFileHelper::SaveStringToFile(Json, *(Scope / FString::Printf(TEXT("case-%03d.json"), Cases)),
                FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    }
    bool Verdict(const FString& Name, const FString& Input, const FString& Actual, bool Expected)
    {
        Trace(Name, Input, Actual);
        auto Parsed = R89Parse(Actual);
        bool Valid = !Expected;
        FString Error;
        const bool Explicit = Parsed.IsValid() && Parsed->TryGetBoolField(TEXT("valid"), Valid);
        Check(Name + TEXT(": explicit native verdict"), Explicit);
        Check(Name, Explicit && Valid == Expected);
        if (!Expected)
            Check(Name + TEXT(": native refusal diagnostic"),
                Parsed.IsValid() && Parsed->TryGetStringField(TEXT("error"), Error) && !Error.IsEmpty());
        return Explicit && Valid == Expected;
    }
    void NoDisk()
    {
        TArray<FString> Paths;
        IFileManager::Get().FindFilesRecursive(
            Paths, *FPackageName::LongPackageNameToFilename(PackageRoot), TEXT("*"), true, false);
        Check(TEXT("No destination file or partial sidecar published"), Paths.Num() == 0);
    }
};
struct FR89State
{
    UObject* Object;
    FString Identity, Metadata;
    TArray<uint8> Bytes;
    EObjectFlags Flags;
    bool Dirty, HadMetadata;
    int32 Pre = 0, Post = 0;
    FDelegateHandle Before, After;
    FR89State(UObject* In)
        : Object(In), Identity(In->GetPathName()),
          Metadata(In->GetOutermost()->GetMetaData()->GetValue(In, TEXT("P4RowsCoverage89Sentinel"))),
          Bytes(R89Bytes(In)), Flags(In->GetFlags()), Dirty(In->GetOutermost()->IsDirty()),
          HadMetadata(In->GetOutermost()->GetMetaData()->HasValue(In, TEXT("P4RowsCoverage89Sentinel")))
    {
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
    ~FR89State()
    {
        FCoreUObjectDelegates::OnPreObjectPropertyChanged.Remove(Before);
        FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(After);
    }
    void Same(FR89Suite& F) const
    {
        F.Check(TEXT("Actual native object pointer/path retained"), FindObject<UObject>(nullptr, *Identity) == Object);
        // Byte serialization deliberately preserves retained NaN payloads; native
        // float equality cannot prove that an unchanged NaN stayed unchanged.
        F.Check(TEXT("Complete independent native serialization retained"), R89Bytes(Object) == Bytes);
        F.Check(TEXT("Native object flags and package dirty state retained"),
            Object->GetFlags() == Flags && Object->GetOutermost()->IsDirty() == Dirty);
        const auto Tags = Object->GetOutermost()->GetMetaData();
        F.Check(TEXT("Exact pre-existing metadata retained"),
            Tags->HasValue(Object, TEXT("P4RowsCoverage89Sentinel")) == HadMetadata &&
                Tags->GetValue(Object, TEXT("P4RowsCoverage89Sentinel")) == Metadata);
        F.Check(TEXT("No visible target edit notification"), Pre == 0 && Post == 0);
    }
};
struct FR89Color
{
    UClass* Class = nullptr;
    TStrongObjectPtr<UObject> Object;
    FStructProperty* Red = nullptr;
    FFloatProperty* R = nullptr;
    FFloatProperty* G = nullptr;
    bool Open(FR89Suite& F, const FString& Leaf, bool Private = false)
    {
        Class = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.OnyonCombineEftColorData"));
        if (!F.Check(TEXT("Actual recovered game color asset class exists"), Class != nullptr))
            return false;
        Object.Reset(Private
                ? UP4AuthoringTools::NewNativeAsset(Class)
                : NewObject<UObject>(CreatePackage(*(F.PackageRoot / Leaf)), Class, FName(*Leaf), RF_Transactional));
        if (!F.Check(TEXT("Actual game color fixture exists"), Object.IsValid()))
            return false;
        Red = CastField<FStructProperty>(Class->FindPropertyByName(TEXT("Red")));
        R = Red ? CastField<FFloatProperty>(Red->Struct->FindPropertyByName(TEXT("R"))) : nullptr;
        G = Red ? CastField<FFloatProperty>(Red->Struct->FindPropertyByName(TEXT("G"))) : nullptr;
        if (!F.Check(TEXT("Fixture has genuine native float members"),
                Red && R && G && Red->Struct->GetPathName() == TEXT("/Script/CoreUObject.LinearColor")))
            return false;
        R->SetPropertyValue(RStorage(), 0.25f);
        G->SetPropertyValue(GStorage(), 0.75f);
        Object->GetOutermost()->GetMetaData()->SetValue(
            Object.Get(), TEXT("P4RowsCoverage89Sentinel"), TEXT("exact-owner-state"));
        if (!Private)
            Object->GetOutermost()->SetDirtyFlag(false);
        return true;
    }
    void* RStorage() const { return R->ContainerPtrToValuePtr<void>(Red->ContainerPtrToValuePtr<void>(Object.Get())); }
    void* GStorage() const { return G->ContainerPtrToValuePtr<void>(Red->ContainerPtrToValuePtr<void>(Object.Get())); }
};
bool R89PrivateRefuses(FR89Suite& F, const FString& Label, UObject* Object, const FString& Input)
{
    FString Canonical(TEXT("stale-canonical-output")), Error;
    const bool Result = P4PrepareExactPrivateProperties(Object, Input, Canonical, Error);
    F.Trace(Label, Input,
        FString(Result ? TEXT("true") : TEXT("false")) + TEXT("; canonical=") + Canonical + TEXT("; error=") + Error);
    F.Check(Label, !Result);
    F.Check(Label + TEXT(": stale output cleared and diagnostic retained"), Canonical.IsEmpty() && !Error.IsEmpty());
    return !Result;
}
} //namespace P4RowsCoverage89

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4Rows89SynchronousPrivateOwnership,
    "P4MapKit.Quality.Rows89SynchronousPrivateOwnership",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4Rows89SynchronousPrivateOwnership::RunTest(const FString&)
{
    using namespace P4RowsCoverage89;
    FR89Suite F(*this, TEXT("SynchronousPrivateOwnership"));
    TStrongObjectPtr<UCurveVector> Scratch(NewObject<UCurveVector>(GetTransientPackage(), NAME_None, RF_Transient));
    Scratch->FloatCurves[0].AddKey(2.f, 7.f);
    Scratch->FloatCurves[1].AddKey(3.f, 11.f);
    FString Canonical, Error;
    {
        FR89State State(Scratch.Get());
        F.Check(TEXT("Actual synchronous empty private preparation positive control"),
            P4PrepareExactPrivateProperties(Scratch.Get(), TEXT("{}"), Canonical, Error) && Canonical == TEXT("{}"));
        R89PrivateRefuses(F, TEXT("synchronous-null-owner"), nullptr, TEXT("{}"));
        FString ThreadCanonical(TEXT("stale-canonical-output")), ThreadError;
        const bool Worker = Async(EAsyncExecution::Thread,
            [&]()
            {
                return P4PrepareExactPrivateProperties(Scratch.Get(), TEXT("{}"), ThreadCanonical, ThreadError);
            }).Get();
        F.Trace(TEXT("synchronous-worker-owner"), TEXT("{}"),
            FString(Worker ? TEXT("true") : TEXT("false")) + TEXT("; ") + ThreadError);
        F.Check(TEXT("Worker private preparation refuses and clears output"),
            !Worker && ThreadCanonical.IsEmpty() && !ThreadError.IsEmpty());
        for (const TCHAR* Input : {TEXT("null"), TEXT("[]"), TEXT("true"), TEXT("17"), TEXT("{\"FloatCurves\":}"),
                 TEXT("{\"FloatCurves\":null}")})
            R89PrivateRefuses(F, TEXT("synchronous-field-object-refusal ") + FString(Input), Scratch.Get(), Input);
        State.Same(F);
    }
    TStrongObjectPtr<UCurveVector> NonTransient(NewObject<UCurveVector>(GetTransientPackage(), NAME_None, RF_NoFlags));
    TStrongObjectPtr<UCurveVector> Archetype(
        NewObject<UCurveVector>(GetTransientPackage(), NAME_None, RF_Transient | RF_ArchetypeObject));
    TStrongObjectPtr<UCurveVector> Authored(NewObject<UCurveVector>(
        CreatePackage(*(F.PackageRoot / TEXT("Authored"))), TEXT("Authored"), RF_Transient | RF_Transactional));
    if (!F.Check(TEXT("Real ownership fixture flags are independently established"),
            !NonTransient->HasAnyFlags(RF_Transient) && Archetype->HasAllFlags(RF_Transient | RF_ArchetypeObject) &&
                Authored->HasAnyFlags(RF_Transient) && Authored->GetOuter() != GetTransientPackage()))
        return false;
    for (UObject* Object : {static_cast<UObject*>(NonTransient.Get()), static_cast<UObject*>(Archetype.Get()),
             static_cast<UObject*>(Authored.Get())})
    {
        FR89State State(Object);
        R89PrivateRefuses(F, TEXT("synchronous-real-owner-refusal ") + Object->GetPathName(), Object, TEXT("{}"));
        State.Same(F);
    }
    F.NoDisk();
    return F.Good && !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4Rows89AssetReadbackSourceRefusals,
    "P4MapKit.Quality.Rows89AssetReadbackSourceRefusals",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4Rows89AssetReadbackSourceRefusals::RunTest(const FString&)
{
    using namespace P4RowsCoverage89;
    FR89Suite F(*this, TEXT("AssetReadbackSources"));
    FR89Color Color;
    if (!Color.Open(F, TEXT("Color")))
        return false;
    FR89State State(Color.Object.Get());
    const FString Good(TEXT("{\"Red\":{\"R\":0.25,\"G\":0.75}}"));
    F.Verdict(TEXT("real-native-color-readback-control"), Good,
        UP4AuthoringTools::NativeAssetReadback(Color.Object.Get(), Good), true);
    for (const TCHAR* Input : {TEXT("{"), TEXT("[]"), TEXT("null"), TEXT("true"), TEXT("17"), TEXT("{\"Red\":null}"),
             TEXT("{\"Red\":[]}"), TEXT("{\"Red\":{\"R\":null}}"), TEXT("{\"Red\":{\"R\":\"0.25\"}}"),
             TEXT("{\"Red\":{\"R\":0.25,\"R\":0.5}}"), TEXT("{\"Red\":{\"Unknown\":1}}")})
    {
        F.Verdict(TEXT("direct-native-asset-readback-source-refusal ") + FString(Input), Input,
            UP4AuthoringTools::NativeAssetReadback(Color.Object.Get(), Input), false);
        State.Same(F);
    }
    F.Verdict(TEXT("native-readback-null-asset"), Good, UP4AuthoringTools::NativeAssetReadback(nullptr, Good), false);
    TStrongObjectPtr<UCurveVector> Unsupported(NewObject<UCurveVector>(GetTransientPackage(), NAME_None, RF_Transient));
    {
        FR89State Held(Unsupported.Get());
        F.Verdict(TEXT("actual-engine-nonasset-class-refuses-asset-readback"), TEXT("{}"),
            UP4AuthoringTools::NativeAssetReadback(Unsupported.Get(), TEXT("{}")), false);
        Held.Same(F);
    }
    const FString Worker = Async(EAsyncExecution::Thread,
        [&]()
        {
            return UP4AuthoringTools::NativeAssetReadback(Color.Object.Get(), Good);
        }).Get();
    F.Verdict(TEXT("direct-native-asset-readback-worker-refusal"), Good, Worker, false);
    State.Same(F);
    F.NoDisk();
    return F.Good && !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4Rows89UnavailableNativeAssetReadback,
    "P4MapKit.Quality.Rows89UnavailableNativeAssetReadback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4Rows89UnavailableNativeAssetReadback::RunTest(const FString&)
{
    using namespace P4RowsCoverage89;
    FR89Suite F(*this, TEXT("UnavailableNativeReadback"));
    int32 Case = 0;
    for (const uint32 Payload : {0x7fc01234u, 0x7f800000u, 0xff800000u})
    {
        FR89Color Color;
        if (!Color.Open(F, FString::Printf(TEXT("Nonfinite%d"), ++Case)))
            return false;
        FMemory::Memcpy(Color.RStorage(), &Payload, sizeof(Payload));
        const uint32 Sibling = R89FloatBits(Color.GStorage());
        const FString Source(TEXT("{\"Red\":{\"G\":0.75}}"));
        F.Verdict(TEXT("finite-selected-field-source-admission-control"), Source,
            UP4AuthoringTools::NativeCanonicalAssetValues(Color.Object.Get(), Source), true);
        FR89State State(Color.Object.Get());
        const FString Actual = UP4AuthoringTools::NativeAssetReadback(Color.Object.Get(), Source);
        F.Verdict(TEXT("complete-native-readback-refuses-retained-nonfinite-sibling"), Source, Actual, false);
        const auto Readback = R89Parse(Actual);
        FString Reason;
        F.Check(TEXT("Actual failure is unavailable full native readback"),
            Readback.IsValid() && Readback->TryGetStringField(TEXT("error"), Reason) &&
                Reason == TEXT("exact native asset readback unavailable or over budget"));
        const bool Applied = UP4AuthoringTools::NativeApplyAssetValues(
            Color.Object.Get(), TEXT("{\"Red\":{\"G\":0.5}}"));
        F.Trace(TEXT("native-asset-copy-refuses-unavailable-full-source"), TEXT("{\"Red\":{\"G\":0.5}}"),
            Applied ? TEXT("true") : TEXT("false"));
        F.Check(TEXT("Finite sibling edit cannot publish while full source is unavailable"), !Applied);
        F.Check(TEXT("Independent retained NaN/infinity payload and finite sibling bits unchanged"),
            R89FloatBits(Color.RStorage()) == Payload && R89FloatBits(Color.GStorage()) == Sibling);
        State.Same(F);
    }
    F.NoDisk();
    return F.Good && !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4Rows89NativeFloatPrecisionRefusals,
    "P4MapKit.Quality.Rows89NativeFloatPrecisionRefusals",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4Rows89NativeFloatPrecisionRefusals::RunTest(const FString&)
{
    using namespace P4RowsCoverage89;
    FR89Suite F(*this, TEXT("NativeFloatPrecision"));
    int32 Case = 0;
    for (const TCHAR* Token : {TEXT("0.1"), TEXT("1.0000000000000002"), TEXT("3.141592653589793")})
    {
        FR89Color Color;
        if (!Color.Open(F, FString::Printf(TEXT("Precision%d"), ++Case)))
            return false;
        const FString Source = TEXT("{\"Red\":{\"R\":") + FString(Token) + TEXT("}}");
        F.Verdict(TEXT("finite-double-source-admission-control"), Source,
            UP4AuthoringTools::NativeCanonicalAssetValues(Color.Object.Get(), Source), true);
        const uint32 RBefore = R89FloatBits(Color.RStorage()), GBefore = R89FloatBits(Color.GStorage());
        FR89State State(Color.Object.Get());
        const bool Applied = UP4AuthoringTools::NativeApplyAssetValues(Color.Object.Get(), Source);
        F.Trace(TEXT("public-native-float-loss-refuses-publication"), Source, Applied ? TEXT("true") : TEXT("false"));
        F.Check(TEXT("Source number cannot silently round on native publication"), !Applied);
        F.Check(TEXT("Independent original float and sibling bits retained"),
            R89FloatBits(Color.RStorage()) == RBefore && R89FloatBits(Color.GStorage()) == GBefore);
        State.Same(F);
        FR89Color Private;
        if (!Private.Open(F, FString::Printf(TEXT("Private%d"), Case), true))
            return false;
        R89PrivateRefuses(F, TEXT("synchronous-private-float-loss-refusal"), Private.Object.Get(), Source);
        // Private write precedes the mandatory independent readback gate. This
        // disposable scratch is quarantined; no published target is overwritten.
        F.Check(TEXT("Private precision refusal leaves exact finite sibling intact"),
            R89FloatBits(Private.GStorage()) == GBefore);
        F.Verdict(TEXT("actual-private-rounded-storage-refuses-original-source"), Source,
            UP4AuthoringTools::NativeAssetReadback(Private.Object.Get(), Source), false);
    }
    // Genuine UE fixed native slots also exercise complete typed writes without
    // synthesizing properties, codecs, or source-qualification metadata.
    TStrongObjectPtr<UCurveVector> Curve(NewObject<UCurveVector>(GetTransientPackage(), NAME_None, RF_Transient));
    Curve->FloatCurves[0].AddKey(2.f, 7.f);
    Curve->FloatCurves[1].AddKey(3.f, 11.f);
    const FString Slots(
        TEXT("{\"FloatCurves\":[{\"DefaultValue\":1.25},{\"DefaultValue\":-0.0},{\"DefaultValue\":3.5}]}"));
    FString Canonical, Error;
    const bool Prepared = P4PrepareExactPrivateProperties(Curve.Get(), Slots, Canonical, Error);
    F.Trace(TEXT("synchronous-actual-UE-fixed-slots-control"), Slots,
        FString(Prepared ? TEXT("true") : TEXT("false")) + TEXT("; ") + Error);
    F.Check(TEXT("Actual complete UE fixed slots prepare exactly"), Prepared && !Canonical.IsEmpty());
    F.Check(TEXT("Native fixed slot values and signed-zero bits independently match"),
        Curve->FloatCurves[0].DefaultValue == 1.25f &&
            R89FloatBits(&Curve->FloatCurves[1].DefaultValue) == 0x80000000u &&
            Curve->FloatCurves[2].DefaultValue == 3.5f);
    F.Check(TEXT("Partial native structs retain independently authored curve keys"),
        Curve->FloatCurves[0].GetConstRefOfKeys().Num() == 1 &&
            Curve->FloatCurves[0].GetConstRefOfKeys()[0].Value == 7.f &&
            Curve->FloatCurves[1].GetConstRefOfKeys().Num() == 1 &&
            Curve->FloatCurves[1].GetConstRefOfKeys()[0].Value == 11.f);
    F.NoDisk();
    return F.Good && !HasAnyErrors();
}
#endif

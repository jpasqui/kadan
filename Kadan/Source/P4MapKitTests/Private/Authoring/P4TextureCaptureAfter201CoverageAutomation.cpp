// PRIVATE native163 gap draft; root selects against measured201 and owns UE.
#include "Policies/CondensedJsonPrintPolicy.h"
#include "P4TextureProfileCapture.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "P4NativeHash.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/GarbageCollection.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

namespace P4TextureCaptureAfter201
{
bool Same(const FString& A, const FString& B)
{
    return A.Len() == B.Len() && !FMemory::Memcmp(*A, *B, SIZE_T(A.Len()) * sizeof(TCHAR));
}
bool SameBudget(const FP4TextureProfileCaptureBudget& A, const FP4TextureProfileCaptureBudget& B)
{
    return A.MaxCharacters == B.MaxCharacters && A.MaxNotations == B.MaxNotations && A.MaxDepth == B.MaxDepth &&
        A.MaxObjectKeys == B.MaxObjectKeys && A.MaxStringCharacters == B.MaxStringCharacters &&
        A.MaxEnumEntries == B.MaxEnumEntries;
}
bool Empty(const FP4TextureProfileCapture& Out)
{
    return Out.Entries.Num() == 0 && Out.DeclaredSourceSha256.IsEmpty() && Out.DeclaredCapturedAt.IsEmpty() &&
        Out.DeclaredScope.IsEmpty() && !Out.RuntimeProvenanceAuthenticated && !Out.DefaultsProvenanceAuthenticated &&
        !Out.CaptureAtomic;
}
struct FJsonShape
{
    uint32 QuotedTokens = 0, MaxContainerDepth = 0, MaxNodeDepth = 0;
    void Visit(const TSharedPtr<FJsonValue>& Value, uint32 Depth)
    {
        MaxNodeDepth = FMath::Max(MaxNodeDepth, Depth);
        if (Value->Type == EJson::Object)
        {
            MaxContainerDepth = FMath::Max(MaxContainerDepth, Depth);
            for (const auto& Field : Value->AsObject()->Values)
            {
                ++QuotedTokens;
                Visit(Field.Value, Depth + 1);
            }
        }
        else if (Value->Type == EJson::Array)
        {
            MaxContainerDepth = FMath::Max(MaxContainerDepth, Depth);
            for (const auto& Item : Value->AsArray())
                Visit(Item, Depth + 1);
        }
        else if (Value->Type == EJson::String)
            ++QuotedTokens;
    }
};
struct FOriginalFixture
{
    FString File, Snapshot, Complete;
    TSharedPtr<FJsonObject> Independent;
    bool Unchanged(FAutomationTestBase& Test) const
    {
        int64 Bytes = 0;
        FString Hash;
        return Test.TestTrue(TEXT("Bound original projection retains exact whole-file bytes"),
            P4HashFile(File, 8192, Bytes, Hash) && Bytes == 3522 &&
                Same(Hash, TEXT("a21e53f5dd5caa8d899eb395eb6b122e9faa58c1b48e630d0f75ae4088f4f65c")));
    }
    bool Load(FAutomationTestBase& Test)
    {
        const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
        if (!Test.TestFalse(TEXT("Native evidence fixture root is supplied"), Root.IsEmpty()))
            return false;
        File = FPaths::Combine(Root, TEXT("quality/texture-capture-original-projection.json"));
        if (!Unchanged(Test) ||
            !Test.TestTrue(TEXT("Bound original capture loads"), FFileHelper::LoadFileToString(Snapshot, *File)))
            return false;
        if (!Test.TestTrue(TEXT("Independent public engine JSON parser reads original evidence"),
                FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Snapshot), Independent) &&
                    Independent.IsValid()))
            return false;
        const auto& Records = Independent->GetArrayField(TEXT("enums"));
        if (!Test.TestTrue(TEXT("Actual original target carries all 65 ordered entries"),
                Records.Num() == 1 &&
                    Same(Records[0]->AsObject()->GetStringField(TEXT("path")), TEXT("/Script/Engine.TextureGroup")) &&
                    Records[0]->AsObject()->GetArrayField(TEXT("values")).Num() == 65))
            return false;
        return FJsonSerializer::Serialize(
            Independent.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Complete));
    }
    FString Tail(const FString& Value) const
    {
        return Complete.LeftChop(1) + TEXT(",\"after201_transport\":") + Value + TEXT("}");
    }
    bool Read(FAutomationTestBase& Test, const TCHAR* Label, const FString& Input, bool Expected,
        const TCHAR* Reason = nullptr, const FP4TextureProfileCaptureBudget& Budget = {},
        const TFunction<bool()>& Cancel = {}) const
    {
        const FString Before = Input;
        const auto BeforeBudget = Budget;
        FP4TextureProfileCapture Out;
        FP4CapturedTextureGroup Previous;
        Previous.Name = TEXT("Stale selected identity");
        Previous.Value = MIN_int64;
        Out.Entries.Add(Previous);
        Out.DeclaredSourceSha256 = Out.DeclaredCapturedAt = Out.DeclaredScope = TEXT("Stale declaration");
        Out.RuntimeProvenanceAuthenticated = Out.DefaultsProvenanceAuthenticated = Out.CaptureAtomic = true;
        FString Error(TEXT("Stale diagnostic"));
        bool NoPartialPublication = true;
        const TFunction<bool()> Guard = [&]()
        {
            NoPartialPublication = NoPartialPublication && Empty(Out);
            return Cancel && Cancel();
        };
        const bool Accepted = P4ReadTextureProfileCapture(Input, Budget, Guard, Out, Error);
        Test.TestTrue(FString(Label) + TEXT(" preserves exact caller text and every budget field"),
            Same(Input, Before) && SameBudget(Budget, BeforeBudget));
        Test.TestTrue(FString(Label) + TEXT(" publishes nothing at any cooperative callback"), NoPartialPublication);
        if (!Test.TestEqual(FString(Label) + TEXT(" acceptance: ") + Error, Accepted, Expected))
            return false;
        if (!Expected)
        {
            Test.TestTrue(
                FString(Label) + TEXT(" clears every stale entry/declaration/authentication flag"), Empty(Out));
            Test.TestTrue(FString(Label) + TEXT(" supplies fresh exact refusal: ") + Error,
                !Error.IsEmpty() && Error != TEXT("Stale diagnostic") && (!Reason || Same(Error, Reason)));
        }
        else
        {
            const auto& Values = Independent->GetArrayField(TEXT("enums"))[0]->AsObject()->GetArrayField(
                TEXT("values"));
            bool Exact = Out.Entries.Num() == Values.Num() &&
                Same(Out.DeclaredSourceSha256, Independent->GetStringField(TEXT("source_sha256"))) &&
                Same(Out.DeclaredCapturedAt, Independent->GetStringField(TEXT("captured_at"))) &&
                Same(Out.DeclaredScope, Independent->GetStringField(TEXT("scope")));
            if (Out.Entries.Num() == Values.Num())
                for (int32 Index = 0; Index < Values.Num(); ++Index)
                    Exact = Exact &&
                        Same(Out.Entries[Index].Name, Values[Index]->AsObject()->GetStringField(TEXT("name"))) &&
                        Out.Entries[Index].Value == int64(Values[Index]->AsObject()->GetNumberField(TEXT("value")));
            Test.TestTrue(
                FString(Label) + TEXT(" retains all independent original values, order and declared metadata"), Exact);
            Test.TestTrue(FString(Label) + TEXT(" clears error and cannot infer runtime/default/atomic provenance"),
                Error.IsEmpty() && !Out.RuntimeProvenanceAuthenticated && !Out.DefaultsProvenanceAuthenticated &&
                    !Out.CaptureAtomic);
        }
        return true;
    }
    bool PollPlan(FAutomationTestBase& Test, const FString& Input, const TCHAR* Marker, uint32& LexicalPolls,
        uint32& TargetNotation) const
    {
        TSharedPtr<FJsonObject> Object;
        if (!Test.TestTrue(TEXT("Independent full-source JSON reader accepts the semantic-poll fixture"),
                FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Input), Object) && Object.IsValid()))
            return false;
        // Below one 4096-character polling block, lexical polls occur once
        // at physical offset zero and once per actual quoted key/value.
        // Count those strings through independent native DOM topology.
        if (!Test.TestTrue(TEXT("Poll fixture starts with root object and fits one lexical polling block"),
                Input.StartsWith(TEXT("{")) && Input.Len() < 4096))
            return false;
        FJsonShape Shape;
        Shape.Visit(MakeShared<FJsonValueObject>(Object), 1);
        LexicalPolls = 1 + Shape.QuotedTokens;
        TargetNotation = 0;
        uint32 Index = 0;
        EJsonNotation N;
        const auto Reader = TJsonReaderFactory<>::Create(Input);
        while (Reader->ReadNext(N))
        {
            ++Index;
            if (N == EJsonNotation::Error)
                return Test.TestTrue(TEXT("Independent notation stream has no syntax errors"), false);
            if (!Marker && Index == 1)
                TargetNotation = Index;
            if (Marker && N == EJsonNotation::String && Same(Reader->GetValueAsString(), Marker))
            {
                if (!Test.TestEqual(
                        TEXT("Semantic marker is unique in actual native notation stream"), TargetNotation, uint32(0)))
                    return false;
                TargetNotation = Index;
            }
        }
        return Test.TestTrue(TEXT("Independent public notation stream reaches intended semantic boundary"),
            Reader->GetErrorMessage().IsEmpty() && TargetNotation > 0);
    }
};
} //namespace P4TextureCaptureAfter201

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4CapturePhysicalEndAfter201Draft, "P4MapKit.Quality.CapturePhysicalEndAfter201Draft",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4CapturePhysicalEndAfter201Draft::RunTest(const FString&)
{
    using namespace P4TextureCaptureAfter201;
    FOriginalFixture Fixture;
    if (!Fixture.Load(*this))
        return false;
    // New physical-end branches: wrapped late-token fixtures have following
    // braces and cannot reach these end-of-buffer scanner conditions.
    for (const TCHAR* Scalar : {TEXT("123"), TEXT("1.25"), TEXT("1e2"), TEXT("null"), TEXT("\"root text\"")})
        Fixture.Read(*this,
            TEXT("Installed engine reader refuses a complete scalar root after strict lexical validation"), Scalar,
            false, TEXT("capture-json-syntax"));
    Fixture.Read(*this, TEXT("Exponent terminates before first digit at physical end"), TEXT("1e"), false,
        TEXT("capture-invalid-number"));
    Fixture.Read(
        *this, TEXT("Short lowercase literal at physical end"), TEXT("t"), false, TEXT("capture-invalid-literal"));
    Fixture.Read(
        *this, TEXT("Nonempty whitespace has no capture root"), TEXT(" \t\r\n"), false, TEXT("capture-truncated-json"));
    Fixture.Read(*this, TEXT("Late object comma before closing brace refuses publication"),
        Fixture.Tail(TEXT("{\"v\":0,}")), false, TEXT("capture-trailing-comma"));
    Fixture.Read(*this, TEXT("Original control after physical-end refusals"), Fixture.Complete, true);
    Fixture.Unchanged(*this);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4CaptureSurrogateAfter201Draft, "P4MapKit.Quality.CaptureSurrogateAfter201Draft",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4CaptureSurrogateAfter201Draft::RunTest(const FString&)
{
    using namespace P4TextureCaptureAfter201;
    FOriginalFixture Fixture;
    if (!Fixture.Load(*this))
        return false;
    const FString EscapedPath = Fixture.Complete.Replace(
        TEXT("/Script/Engine.TextureGroup"), TEXT("\\/Script\\/Engine.TextureGroup"));
    Fixture.Read(*this, TEXT("Escaped solidus retains exact original selected identity"), EscapedPath, true);
    Fixture.Read(*this, TEXT("High surrogate followed by ordinary characters lacks low escape"),
        Fixture.Tail(TEXT("\"\\uD800ABCDEF\"")), false, TEXT("capture-unpaired-surrogate"));
    Fixture.Read(*this, TEXT("High surrogate followed by different escape marker lacks low unicode"),
        Fixture.Tail(TEXT("\"\\uD800\\xDC00\"")), false, TEXT("capture-unpaired-surrogate"));
    Fixture.Read(*this, TEXT("Decoded second surrogate exceeds low-surrogate range"),
        Fixture.Tail(TEXT("\"\\uD800\\uF000\"")), false, TEXT("capture-unpaired-surrogate"));
    Fixture.Read(*this, TEXT("High surrogate has no low escape at physical end"),
        Fixture.Complete.LeftChop(1) + TEXT(",\"after201_transport\":\"\\uD800"), false,
        TEXT("capture-unpaired-surrogate"));
    Fixture.Read(*this, TEXT("Original control after surrogate refusals"), Fixture.Complete, true);
    Fixture.Unchanged(*this);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4CaptureSemanticCancellationAfter201Draft,
    "P4MapKit.Quality.CaptureSemanticCancellationAfter201Draft",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4CaptureSemanticCancellationAfter201Draft::RunTest(const FString&)
{
    using namespace P4TextureCaptureAfter201;
    FOriginalFixture Fixture;
    if (!Fixture.Load(*this))
        return false;
    const FString Input = Fixture.Tail(TEXT("[[\"after201-semantic-marker\"]]"));
    if (!Fixture.Read(*this, TEXT("Full late-array control retains original evidence"), Input, true))
        return false;
    for (const TCHAR* Marker : {static_cast<const TCHAR*>(nullptr), TEXT("after201-semantic-marker")})
    {
        uint32 Lexical = 0, Notation = 0;
        if (!Fixture.PollPlan(*this, Input, Marker, Lexical, Notation))
            return false;
        uint32 Polls = 0;
        const uint32 CancelAt = Lexical + Notation;
        Fixture.Read(*this,
            Marker ? TEXT("Nested unselected array cancels after original target selection")
                   : TEXT("First semantic notation cancels before root traversal"),
            Input, false, TEXT("capture-cancelled"), {}, [&]() { return ++Polls == CancelAt; });
        TestTrue(TEXT("Semantic cancellation reaches independent native notation after complete lexical validation"),
            Polls == CancelAt && Polls > Lexical);
    }
    Fixture.Read(*this, TEXT("Original control after semantic cancellations"), Fixture.Complete, true);
    Fixture.Unchanged(*this);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4CaptureNativeDepthAfter201Draft, "P4MapKit.Quality.CaptureNativeDepthAfter201Draft",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4CaptureNativeDepthAfter201Draft::RunTest(const FString&)
{
    using namespace P4TextureCaptureAfter201;
    FOriginalFixture Fixture;
    if (!Fixture.Load(*this))
        return false;
    FJsonShape Shape;
    Shape.Visit(MakeShared<FJsonValueObject>(Fixture.Independent), 1);
    if (!TestTrue(TEXT("Actual native capture contains primitive fields one level below deepest containers"),
            Shape.MaxNodeDepth == Shape.MaxContainerDepth + 1))
        return false;
    uint32 Lexical = 0, First = 0;
    if (!Fixture.PollPlan(*this, Fixture.Complete, nullptr, Lexical, First))
        return false;
    FP4TextureProfileCaptureBudget Budget;
    Budget.MaxDepth = Shape.MaxContainerDepth;
    uint32 Polls = 0;
    Fixture.Read(*this, TEXT("Lexically complete capture cannot exceed semantic leaf depth budget"), Fixture.Complete,
        false, TEXT("capture-depth-budget"), Budget,
        [&]()
        {
            ++Polls;
            return false;
        });
    TestTrue(TEXT("Container-only limit reached real semantic traversal before refusing"), Polls > Lexical);
    Budget.MaxDepth = Shape.MaxNodeDepth;
    Fixture.Read(*this, TEXT("Exact independently measured native leaf-depth budget admits all evidence"),
        Fixture.Complete, true, nullptr, Budget);
    Fixture.Unchanged(*this);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4CaptureFractionTimezoneAfter201Draft,
    "P4MapKit.Quality.CaptureFractionTimezoneAfter201Draft",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4CaptureFractionTimezoneAfter201Draft::RunTest(const FString&)
{
    using namespace P4TextureCaptureAfter201;
    FOriginalFixture Fixture;
    if (!Fixture.Load(*this))
        return false;
    TSharedPtr<FJsonObject> Copy;
    if (!TestTrue(TEXT("Independent public reader clones actual capture"),
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Fixture.Complete), Copy)))
        return false;
    Copy->SetStringField(TEXT("captured_at"), TEXT("2026-01-02T03:04:05.123456"));
    FString Input;
    if (!TestTrue(TEXT("Independent native writer retains complete missing-zone JSON"),
            FJsonSerializer::Serialize(
                Copy.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Input))))
        return false;
    Fixture.Read(*this, TEXT("Fraction reaches timestamp end without required zone"), Input, false,
        TEXT("capture-time-unqualified"));
    Fixture.Read(*this, TEXT("Original declared timestamp control after refusal"), Fixture.Complete, true);
    Fixture.Unchanged(*this);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4CaptureSemanticGcAfter201Draft, "P4MapKit.Quality.CaptureSemanticGcAfter201Draft",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4CaptureSemanticGcAfter201Draft::RunTest(const FString&)
{
    using namespace P4TextureCaptureAfter201;
    FOriginalFixture Fixture;
    if (!Fixture.Load(*this))
        return false;
    TStrongObjectPtr<UEnum> NativeEnum(FindObject<UEnum>(nullptr, TEXT("/Script/Engine.TextureGroup")));
    if (!TestTrue(TEXT("Genuine loaded native texture-group enum has exact object path and class"),
            NativeEnum.IsValid() && Same(NativeEnum->GetPathName(), TEXT("/Script/Engine.TextureGroup")) &&
                NativeEnum->GetClass() == UEnum::StaticClass()))
        return false;
    TArray<TPair<FName, int64>> Entries;
    for (int32 Index = 0; Index < NativeEnum->NumEnums(); ++Index)
        Entries.Emplace(NativeEnum->GetNameByIndex(Index), NativeEnum->GetValueByIndex(Index));
    const FString Path = NativeEnum->GetPathName();
    const EObjectFlags Flags = NativeEnum->GetFlags();
    TArray<uint8> Before;
    {
        FObjectWriter Writer(NativeEnum.Get(), Before, false, false, false, PPF_DuplicateVerbatim);
    }
    uint32 Lexical = 0, Notation = 0;
    if (!Fixture.PollPlan(*this, Fixture.Complete, nullptr, Lexical, Notation))
        return false;
    uint32 Polls = 0;
    bool Collected = false;
    Fixture.Read(*this, TEXT("GC during first semantic yield preserves plain source-derived records"), Fixture.Complete,
        true, nullptr, {},
        [&]()
        {
            if (++Polls == Lexical + Notation)
            {
                CollectGarbage(RF_NoFlags);
                Collected = true;
            }
            return false;
        });
    TestTrue(TEXT("Collection occurred after lexical validation and before full record traversal"),
        Collected && Polls > Lexical + Notation);
    bool SameEntries = NativeEnum->NumEnums() == Entries.Num();
    if (SameEntries)
        for (int32 Index = 0; Index < Entries.Num(); ++Index)
            SameEntries = SameEntries && NativeEnum->GetNameByIndex(Index) == Entries[Index].Key &&
                NativeEnum->GetValueByIndex(Index) == Entries[Index].Value;
    TArray<uint8> After;
    {
        FObjectWriter Writer(NativeEnum.Get(), After, false, false, false, PPF_DuplicateVerbatim);
    }
    TestTrue(TEXT("Installed enum class identity, all native members, serialization and flags remain exact"),
        NativeEnum->GetClass() == UEnum::StaticClass() && Same(NativeEnum->GetPathName(), Path) &&
            NativeEnum->GetFlags() == Flags && SameEntries && Before == After);
    Fixture.Unchanged(*this);
    return !HasAnyErrors();
}
#endif

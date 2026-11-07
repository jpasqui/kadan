#include "Policies/CondensedJsonPrintPolicy.h"
#include "P4TextureProfileCapture.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "P4NativeHash.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace P4TextureCaptureBoundaryTests
{
bool SameText(const FString& A, const FString& B)
{
    return A.Len() == B.Len() && !FMemory::Memcmp(*A, *B, SIZE_T(A.Len()) * sizeof(TCHAR));
}

struct FFixture
{
    FString File, Snapshot, Complete;
    TSharedPtr<FJsonObject> Independent;

    bool Unchanged(FAutomationTestBase& Test) const
    {
        int64 Bytes = 0;
        FString Hash;
        return Test.TestTrue(TEXT("Original captured projection retains its bound whole bytes"),
            P4HashFile(File, 8192, Bytes, Hash) && Bytes == 3522 &&
                SameText(Hash, TEXT("a21e53f5dd5caa8d899eb395eb6b122e9faa58c1b48e630d0f75ae4088f4f65c")));
    }

    bool Load(FAutomationTestBase& Test)
    {
        const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
        if (!Test.TestFalse(TEXT("Native fixture root is supplied"), Root.IsEmpty()))
            return false;
        File = FPaths::Combine(Root, TEXT("quality/texture-capture-original-projection.json"));
        if (!Unchanged(Test) ||
            !Test.TestTrue(TEXT("Original native projection loads"), FFileHelper::LoadFileToString(Snapshot, *File)))
            return false;
        if (!Test.TestTrue(TEXT("Independent engine JSON parser reads the projection"),
                FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Snapshot), Independent) &&
                    Independent.IsValid()))
            return false;
        const auto& Enums = Independent->GetArrayField(TEXT("enums"));
        if (!Test.TestTrue(TEXT("Fixture has one original TextureGroup record with all 65 entries"),
                Enums.Num() == 1 &&
                    SameText(Enums[0]->AsObject()->GetStringField(TEXT("path")), TEXT("/Script/Engine.TextureGroup")) &&
                    Enums[0]->AsObject()->GetArrayField(TEXT("values")).Num() == 65))
            return false;
        FJsonSerializer::Serialize(
            Independent.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Complete));
        return !Complete.IsEmpty();
    }

    bool Read(FAutomationTestBase& Test, const FString& Label, const FString& Input, bool Expected,
        const TCHAR* ExpectedError = nullptr, const FP4TextureProfileCaptureBudget& Budget = {},
        const TFunction<bool()>& Cancel = {}) const
    {
        const FString Before = Input;
        FP4TextureProfileCapture Out;
        FP4CapturedTextureGroup Stale;
        Stale.Name = TEXT("Previously qualified enum");
        Stale.Value = MIN_int64;
        Out.Entries.Add(Stale);
        Out.DeclaredSourceSha256 = Out.DeclaredCapturedAt = Out.DeclaredScope = TEXT("Stale selected declaration");
        Out.RuntimeProvenanceAuthenticated = Out.DefaultsProvenanceAuthenticated = Out.CaptureAtomic = true;
        FString Error(TEXT("Stale error"));
        const bool Accepted = P4ReadTextureProfileCapture(Input, Budget, Cancel, Out, Error);
        Test.TestTrue(Label + TEXT(" preserves every caller UTF16 code unit"), SameText(Input, Before));
        if (!Test.TestEqual(Label + TEXT(" acceptance"), Accepted, Expected))
        {
            Test.AddError(Label + TEXT(" actual diagnostic: ") + Error);
            return false;
        }
        Test.TestTrue(Label + TEXT(" never invents provenance authentication"),
            !Out.RuntimeProvenanceAuthenticated && !Out.DefaultsProvenanceAuthenticated && !Out.CaptureAtomic);
        if (!Expected)
        {
            Test.TestTrue(Label + TEXT(" clears previously selected entries and every declaration"),
                Out.Entries.Num() == 0 && Out.DeclaredSourceSha256.IsEmpty() && Out.DeclaredCapturedAt.IsEmpty() &&
                    Out.DeclaredScope.IsEmpty());
            Test.TestTrue(Label + TEXT(" supplies a fresh refusal"), !Error.IsEmpty() && Error != TEXT("Stale error"));
            if (ExpectedError)
                Test.TestTrue(
                    Label + TEXT(" reaches the intended refusal boundary: ") + Error, SameText(Error, ExpectedError));
        }
        else
        {
            const auto& Values = Independent->GetArrayField(TEXT("enums"))[0]->AsObject()->GetArrayField(
                TEXT("values"));
            bool Equal = Out.Entries.Num() == Values.Num() &&
                SameText(Out.DeclaredSourceSha256, Independent->GetStringField(TEXT("source_sha256"))) &&
                SameText(Out.DeclaredCapturedAt, Independent->GetStringField(TEXT("captured_at"))) &&
                SameText(Out.DeclaredScope, Independent->GetStringField(TEXT("scope")));
            if (Out.Entries.Num() == Values.Num())
                for (int32 Index = 0; Index < Values.Num(); ++Index)
                    Equal = Equal &&
                        SameText(Out.Entries[Index].Name, Values[Index]->AsObject()->GetStringField(TEXT("name"))) &&
                        Out.Entries[Index].Value == int64(Values[Index]->AsObject()->GetNumberField(TEXT("value")));
            Test.TestTrue(
                Label + TEXT(" retains all 65 independent original identities, ordinals, ordering and declarations"),
                Equal);
            Test.TestTrue(Label + TEXT(" clears stale diagnostic"), Error.IsEmpty());
        }
        return true;
    }

    FString WithTail(const FString& Payload) const
    {
        return Complete.LeftChop(1) + TEXT(",\"unselected_transport\":") + Payload + TEXT("}");
    }
};
} //namespace P4TextureCaptureBoundaryTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TextureCaptureGrammarBoundaryAutomation,
    "P4MapKit.Quality.TextureCaptureGrammarBoundaries",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TextureCaptureGrammarBoundaryAutomation::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4TextureCaptureBoundaryTests;
    FFixture Fixture;
    if (!Fixture.Load(*this) || !Fixture.Read(*this, TEXT("Original control"), Fixture.Complete, true))
        return false;
    Fixture.Read(*this, TEXT("Unknown nested containers remain transport metadata"),
        Fixture.WithTail(TEXT("{\"empty_object\":{},\"empty_array\":[],\"mixed\":[null,false,true,{\"v\":[]}]}")),
        true);
    const TCHAR* BrokenContainers[] = {TEXT("{\"key\" 1}"), TEXT("{1:2}"), TEXT("{\"key\":}"), TEXT("[1:2]"),
        TEXT("[1,,2]"), TEXT("{\"key\":1:2}"), TEXT("{\"key\":1]"), TEXT("[1}"), TEXT("{:1}"), TEXT("[,1]"),
        TEXT("{\"key\":,1}")};
    for (const TCHAR* Payload : BrokenContainers)
    {
        // A colon inside a scanned numeric token fails lexical admission before
        // the structural container parser can classify it.
        const bool InvalidNumber = FCString::Strcmp(Payload, TEXT("[1:2]")) == 0 ||
            FCString::Strcmp(Payload, TEXT("{\"key\":1:2}")) == 0;
        Fixture.Read(*this, FString(TEXT("Malformed late container ")) + Payload, Fixture.WithTail(Payload), false,
            InvalidNumber ? TEXT("capture-invalid-number") : TEXT("capture-json-grammar"));
    }
    Fixture.Read(*this, TEXT("Root colon after complete selected evidence"), Fixture.Complete + TEXT(":0"), false,
        TEXT("capture-json-grammar"));
    Fixture.Read(*this, TEXT("Root comma after complete selected evidence"), Fixture.Complete + TEXT(",0"), false,
        TEXT("capture-json-grammar"));
    Fixture.Read(
        *this, TEXT("Unowned closing container"), Fixture.Complete + TEXT("]"), false, TEXT("capture-json-grammar"));
    Fixture.Read(*this, TEXT("Late object trailing comma followed only by whitespace"),
        Fixture.Complete.LeftChop(1) + TEXT(",\"unselected_transport\":{\"x\":0, \t\r\n"), false,
        TEXT("capture-trailing-comma"));
    Fixture.Read(*this, TEXT("Truncated escape after complete original target"),
        Fixture.Complete.LeftChop(1) + TEXT(",\"unselected_transport\":\"ends_in_slash\\"), false,
        TEXT("capture-truncated-escape"));
    Fixture.Read(*this, TEXT("Truncated unicode at physical end"),
        Fixture.Complete.LeftChop(1) + TEXT(",\"unselected_transport\":\"\\u12"), false,
        TEXT("capture-truncated-unicode"));
    Fixture.Read(*this, TEXT("Unterminated unselected string"),
        Fixture.Complete.LeftChop(1) + TEXT(",\"unselected_transport\":\"open"), false,
        TEXT("capture-truncated-string"));
    Fixture.Unchanged(*this);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TextureCaptureNumberBoundaryAutomation,
    "P4MapKit.Quality.TextureCaptureNumberBoundaries",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TextureCaptureNumberBoundaryAutomation::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4TextureCaptureBoundaryTests;
    FFixture Fixture;
    if (!Fixture.Load(*this))
        return false;
    Fixture.Read(*this, TEXT("JSON fraction/exponent/sign forms do not replace selected integer ordinals"),
        Fixture.WithTail(TEXT("[0,-0,1.25,-1.25,1e2,1E+2,1e-2,-1.25E-2]")), true);
    const TCHAR* BrokenNumbers[] = {
        TEXT("-"), TEXT("-.1"), TEXT("1."), TEXT("1e"), TEXT("1E+"), TEXT("1e-"), TEXT("0x0"), TEXT("1.0x")};
    for (const TCHAR* Payload : BrokenNumbers)
        Fixture.Read(*this, FString(TEXT("Malformed late number ")) + Payload, Fixture.WithTail(Payload), false,
            TEXT("capture-invalid-number"));
    Fixture.Read(*this, TEXT("Minus at physical end cannot form a number"),
        Fixture.Complete.LeftChop(1) + TEXT(",\"unselected_transport\":-"), false, TEXT("capture-invalid-number"));
    const TCHAR* BrokenLiterals[] = {TEXT("tru"), TEXT("falsex"), TEXT("nullx"), TEXT("nulk")};
    for (const TCHAR* Payload : BrokenLiterals)
        Fixture.Read(*this, FString(TEXT("Malformed late literal ")) + Payload, Fixture.WithTail(Payload), false,
            TEXT("capture-invalid-literal"));
    Fixture.Read(*this, TEXT("Unknown token after selected target"), Fixture.WithTail(TEXT("@")), false,
        TEXT("capture-invalid-token"));
    // Both spellings are JSON numbers, but the source's selected ordinals must
    // remain integers exactly; native reader double conversion is no oracle.
    Fixture.Read(*this, TEXT("Schema version exponent cannot substitute exact schema token"),
        Fixture.Complete.Replace(TEXT("\"schema_version\":2"), TEXT("\"schema_version\":2e0")), false,
        TEXT("capture-schema-unqualified"));
    Fixture.Read(*this, TEXT("Negative selected signed64 overflow"),
        Fixture.Complete.Replace(TEXT("\"value\":0"), TEXT("\"value\":-9223372036854775809")), false,
        TEXT("capture-target-unqualified"));
    FP4TextureProfileCaptureBudget Budget;
    // The entire real source is an independent upper bound for every existing
    // token. Only the newly appended numeric token exceeds this limit.
    Budget.MaxStringCharacters = uint32(Fixture.Complete.Len());
    const FString ExcessDigits = FString::ChrN(int32(Budget.MaxStringCharacters) + 1, TEXT('1'));
    Fixture.Read(*this, TEXT("Fraction scanning observes numeric length bound"),
        Fixture.WithTail(TEXT("0.") + ExcessDigits), false, TEXT("capture-number-budget"), Budget);
    Fixture.Read(*this, TEXT("Exponent scanning observes numeric length bound"),
        Fixture.WithTail(TEXT("1e+") + ExcessDigits), false, TEXT("capture-number-budget"), Budget);
    // First field has one short key. Lexical polls occur at input offset zero,
    // that key's string start, then absolute numeric offset 4096. Refusal at the
    // third poll exercises cancellation inside the digit scanner, before any
    // semantic traversal of the real capture or qualification can occur.
    const FString LongNumeric = TEXT("{\"numeric_poll\":1") + FString::ChrN(8192, TEXT('0')) + TEXT(",") +
        Fixture.Complete.Mid(1);
    uint32 Polls = 0;
    Fixture.Read(*this, TEXT("Long numeric scanning cooperatively cancels"), LongNumeric, false,
        TEXT("capture-cancelled"), {}, [&]() { return ++Polls == 3; });
    TestEqual(TEXT("Digit scanner stops at the requested poll without later parser work"), Polls, uint32(3));
    Fixture.Read(*this, TEXT("Original control after numeric refusals"), Fixture.Complete, true);
    Fixture.Unchanged(*this);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TextureCaptureLateBudgetBoundaryAutomation,
    "P4MapKit.Quality.TextureCaptureLateBudgetBoundaries",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TextureCaptureLateBudgetBoundaryAutomation::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4TextureCaptureBoundaryTests;
    FFixture Fixture;
    if (!Fixture.Load(*this))
        return false;
    // Count the actual native reader's notations independently, including every
    // container end; use neither production's poll count nor a fabricated enum.
    uint64 Count = 0;
    EJsonNotation Notation;
    const auto Reader = TJsonReaderFactory<>::Create(Fixture.Complete);
    while (Reader->ReadNext(Notation))
    {
        if (!TestTrue(
                TEXT("Independent notation inventory has no engine syntax error"), Notation != EJsonNotation::Error))
            return false;
        ++Count;
    }
    if (!TestTrue(
            TEXT("Independent native notation inventory completed"), Reader->GetErrorMessage().IsEmpty() && Count > 1))
        return false;
    FP4TextureProfileCaptureBudget Budget;
    Budget.MaxNotations = Count;
    Fixture.Read(*this, TEXT("Exact independently counted notation budget admits original capture"), Fixture.Complete,
        true, nullptr, Budget);
    Budget.MaxNotations = Count - 1;
    Fixture.Read(*this, TEXT("Final root notation cannot publish partial qualified target"), Fixture.Complete, false,
        TEXT("capture-notation-budget"), Budget);
    const int32 RootKeys = Fixture.Independent->Values.Num();
    if (!TestTrue(TEXT("Original source has required declarations and no tail test key"),
            RootKeys >= 5 && !Fixture.Independent->HasField(TEXT("unselected_transport"))))
        return false;
    Budget = {};
    Budget.MaxObjectKeys = uint32(RootKeys);
    Fixture.Read(*this, TEXT("Exact source root key budget control"), Fixture.Complete, true, nullptr, Budget);
    Fixture.Read(*this, TEXT("One extra root key refuses after original target was read"),
        Fixture.WithTail(TEXT("null")), false, TEXT("capture-key-budget"), Budget);
    Budget = {};
    Budget.MaxObjectKeys = uint32(RootKeys) + 1;
    FString ExcessObject = TEXT("{");
    for (int32 Index = 0; Index < RootKeys + 2; ++Index)
    {
        if (Index)
            ExcessObject += TEXT(",");
        ExcessObject += FString::Printf(TEXT("\"unknown_key_%d\":0"), Index);
    }
    ExcessObject += TEXT("}");
    Fixture.Read(*this, TEXT("Unselected nested object obeys its own key budget"), Fixture.WithTail(ExcessObject),
        false, TEXT("capture-key-budget"), Budget);
    Budget = {};
    Budget.MaxDepth = 8;
    Fixture.Read(*this, TEXT("Valid capture with bounded unrelated nesting"), Fixture.WithTail(TEXT("[[[[]]]]")), true,
        nullptr, Budget);
    Fixture.Read(*this, TEXT("Deep unselected tail cannot publish a previously qualified target"),
        Fixture.WithTail(TEXT("[[[[[[[[[]]]]]]]]]")), false, TEXT("capture-depth-budget"), Budget);
    Fixture.Read(*this, TEXT("Original control after late resource refusals"), Fixture.Complete, true);
    Fixture.Unchanged(*this);
    return !HasAnyErrors();
}
#endif

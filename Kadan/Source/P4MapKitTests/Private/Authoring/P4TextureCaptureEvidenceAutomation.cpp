#include "Policies/CondensedJsonPrintPolicy.h"
#include "P4TextureProfileCapture.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "P4NativeHash.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

namespace P4TextureCaptureEvidenceTests
{
const TCHAR* const P4CaptureProjectionHash = TEXT("a21e53f5dd5caa8d899eb395eb6b122e9faa58c1b48e630d0f75ae4088f4f65c");

bool P4CaptureSameText(const FString& A, const FString& B)
{
    return A.Len() == B.Len() && !FMemory::Memcmp(*A, *B, SIZE_T(A.Len()) * sizeof(TCHAR));
}

bool P4CaptureSameBudget(const FP4TextureProfileCaptureBudget& A, const FP4TextureProfileCaptureBudget& B)
{
    return A.MaxCharacters == B.MaxCharacters && A.MaxNotations == B.MaxNotations && A.MaxDepth == B.MaxDepth &&
        A.MaxObjectKeys == B.MaxObjectKeys && A.MaxStringCharacters == B.MaxStringCharacters &&
        A.MaxEnumEntries == B.MaxEnumEntries;
}

TSharedPtr<FJsonObject> P4CaptureDocument(const FString& Text)
{
    TSharedPtr<FJsonObject> Object;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object);
    return Object;
}

FString P4CaptureRender(const TSharedPtr<FJsonObject>& Object)
{
    FString Text;
    FJsonSerializer::Serialize(
        Object.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
    return Text;
}

TSharedPtr<FJsonObject> P4CaptureTarget(const TSharedPtr<FJsonObject>& Object)
{
    return Object->GetArrayField(TEXT("enums"))[0]->AsObject();
}

TSharedPtr<FJsonObject> P4CaptureEntry(const TSharedPtr<FJsonObject>& Object)
{
    return P4CaptureTarget(Object)->GetArrayField(TEXT("values"))[0]->AsObject();
}

struct FP4CaptureEvidenceFixture
{
    FString File, Snapshot;
    TSharedPtr<FJsonObject> Independent;

    bool Load(FAutomationTestBase& Test)
    {
        const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
        if (!Test.TestFalse(TEXT("Native fixture root is supplied"), Root.IsEmpty()))
            return false;
        File = FPaths::Combine(Root, TEXT("quality/texture-capture-original-projection.json"));
        if (!Unchanged(Test))
            return false;
        if (!Test.TestTrue(
                TEXT("Bound compact original projection loads"), FFileHelper::LoadFileToString(Snapshot, *File)))
            return false;
        Independent = P4CaptureDocument(Snapshot);
        if (!Test.TestTrue(TEXT("Independent engine JSON reader opens original projection"), Independent.IsValid()))
            return false;
        const auto Target = P4CaptureTarget(Independent);
        return Test.TestTrue(TEXT("Projection retains actual original target and 65 entries"),
            P4CaptureSameText(Target->GetStringField(TEXT("path")), TEXT("/Script/Engine.TextureGroup")) &&
                Target->GetArrayField(TEXT("values")).Num() == 65);
    }

    bool Unchanged(FAutomationTestBase& Test) const
    {
        int64 Bytes = 0;
        FString Hash;
        return Test.TestTrue(TEXT("Exact original projection bytes are unchanged"),
            P4HashFile(File, 8192, Bytes, Hash) && Bytes == 3522 && P4CaptureSameText(Hash, P4CaptureProjectionHash));
    }

    bool Matches(FAutomationTestBase& Test, const FP4TextureProfileCapture& Result,
        const TSharedPtr<FJsonObject>& Expected) const
    {
        const auto& Values = P4CaptureTarget(Expected)->GetArrayField(TEXT("values"));
        bool Equal = Result.Entries.Num() == Values.Num() &&
            P4CaptureSameText(Result.DeclaredSourceSha256, Expected->GetStringField(TEXT("source_sha256"))) &&
            P4CaptureSameText(Result.DeclaredCapturedAt, Expected->GetStringField(TEXT("captured_at"))) &&
            P4CaptureSameText(Result.DeclaredScope, Expected->GetStringField(TEXT("scope"))) &&
            !Result.RuntimeProvenanceAuthenticated && !Result.DefaultsProvenanceAuthenticated && !Result.CaptureAtomic;
        if (Result.Entries.Num() == Values.Num())
            for (int32 Index = 0; Index < Values.Num(); ++Index)
            {
                const auto Entry = Values[Index]->AsObject();
                Equal = Equal && P4CaptureSameText(Result.Entries[Index].Name, Entry->GetStringField(TEXT("name"))) &&
                    Result.Entries[Index].Value == int64(Entry->GetNumberField(TEXT("value")));
            }
        return Test.TestTrue(
            TEXT("All native entries/declarations match independent source projection; no authentication inferred"),
            Equal);
    }

    bool Accepts(FAutomationTestBase& Test, const FString& Input, const TSharedPtr<FJsonObject>& Expected,
        const FP4TextureProfileCaptureBudget& Budget = {}) const
    {
        FP4TextureProfileCapture Result;
        FString Error(TEXT("stale diagnostic"));
        FP4CapturedTextureGroup Previous;
        Previous.Name = TEXT("Stale success entry");
        Previous.Value = MIN_int64;
        Result.Entries.Add(Previous);
        Result.DeclaredSourceSha256 = Result.DeclaredCapturedAt = Result.DeclaredScope = TEXT("stale declaration");
        Result.RuntimeProvenanceAuthenticated = Result.DefaultsProvenanceAuthenticated = Result.CaptureAtomic = true;
        const FString Before = Input;
        const auto BeforeBudget = Budget;
        const bool Accepted = P4ReadTextureProfileCapture(Input, Budget, {}, Result, Error);
        Test.TestTrue(TEXT("Accepted capture preserves exact UTF16 input"), P4CaptureSameText(Input, Before));
        Test.TestTrue(TEXT("Accepted capture preserves caller budget"), P4CaptureSameBudget(Budget, BeforeBudget));
        if (!Test.TestTrue(TEXT("Valid independent capture contract accepts"), Accepted))
            return false;
        Test.TestTrue(TEXT("Success clears stale diagnostic"), Error.IsEmpty());
        return Matches(Test, Result, Expected);
    }

    bool Refuses(FAutomationTestBase& Test, const FString& Label, const FString& Input,
        const FP4TextureProfileCaptureBudget& Budget = {}, const TFunction<bool()>& Cancel = {}) const
    {
        FP4TextureProfileCapture Out;
        FP4CapturedTextureGroup Sentinel;
        Sentinel.Name = TEXT("Stale selected identity");
        Sentinel.Value = MIN_int64;
        Out.Entries.Add(Sentinel);
        Out.DeclaredSourceSha256 = TEXT("stale SHA");
        Out.DeclaredCapturedAt = TEXT("stale timestamp");
        Out.DeclaredScope = TEXT("stale scope");
        Out.RuntimeProvenanceAuthenticated = Out.DefaultsProvenanceAuthenticated = Out.CaptureAtomic = true;
        FString Error(TEXT("stale diagnostic"));
        const FString Before = Input;
        const auto BeforeBudget = Budget;
        const bool Accepted = P4ReadTextureProfileCapture(Input, Budget, Cancel, Out, Error);
        Test.TestFalse(Label + TEXT(" refuses"), Accepted);
        Test.TestTrue(Label + TEXT(" clears every output field and authentication flag"),
            Out.Entries.Num() == 0 && Out.DeclaredSourceSha256.IsEmpty() && Out.DeclaredCapturedAt.IsEmpty() &&
                Out.DeclaredScope.IsEmpty() && !Out.RuntimeProvenanceAuthenticated &&
                !Out.DefaultsProvenanceAuthenticated && !Out.CaptureAtomic);
        Test.TestTrue(Label + TEXT(" supplies a fresh explicit diagnostic"),
            !Error.IsEmpty() && Error != TEXT("stale diagnostic"));
        Test.TestTrue(Label + TEXT(" leaves exact UTF16 input untouched"), P4CaptureSameText(Input, Before));
        Test.TestTrue(Label + TEXT(" leaves caller budget untouched"), P4CaptureSameBudget(Budget, BeforeBudget));
        return !Accepted;
    }
};
} //namespace P4TextureCaptureEvidenceTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TextureCaptureEnvelopeAutomation,
    "P4MapKit.Quality.TextureCaptureEvidenceEnvelopes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TextureCaptureEnvelopeAutomation::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4TextureCaptureEvidenceTests;
    FP4CaptureEvidenceFixture Fixture;
    if (!Fixture.Load(*this) || !Fixture.Accepts(*this, Fixture.Snapshot, Fixture.Independent))
        return false;
    const TCHAR* Required[] = {
        TEXT("schema_version"), TEXT("source_sha256"), TEXT("captured_at"), TEXT("scope"), TEXT("enums")};
    for (const TCHAR* Field : Required)
    {
        auto Document = P4CaptureDocument(Fixture.Snapshot);
        Document->RemoveField(Field);
        Fixture.Refuses(*this, FString(TEXT("Missing required ")) + Field, P4CaptureRender(Document));
    }
    const TSharedPtr<FJsonValue> Shapes[] = {MakeShared<FJsonValueNull>(), MakeShared<FJsonValueBoolean>(false),
        MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>()),
        MakeShared<FJsonValueObject>(MakeShared<FJsonObject>())};
    const TCHAR* ScalarFields[] = {TEXT("schema_version"), TEXT("source_sha256"), TEXT("captured_at"), TEXT("scope")};
    for (const TCHAR* Field : ScalarFields)
        for (const auto& Shape : Shapes)
        {
            auto Document = P4CaptureDocument(Fixture.Snapshot);
            Document->SetField(Field, Shape);
            Fixture.Refuses(*this, FString(TEXT("Wrong declaration shape ")) + Field, P4CaptureRender(Document));
        }
    for (const auto& Shape : Shapes)
    {
        auto Document = P4CaptureDocument(Fixture.Snapshot);
        Document->SetField(TEXT("enums"), Shape);
        Fixture.Refuses(*this, TEXT("Enums envelope has wrong container"), P4CaptureRender(Document));
        Document = P4CaptureDocument(Fixture.Snapshot);
        Document->SetArrayField(TEXT("enums"), {Shape});
        Fixture.Refuses(*this, TEXT("Enum record is not qualified target evidence"), P4CaptureRender(Document));
    }
    const TCHAR* EnumFields[] = {TEXT("path"), TEXT("status"), TEXT("values")};
    for (const TCHAR* Field : EnumFields)
    {
        auto Document = P4CaptureDocument(Fixture.Snapshot);
        P4CaptureTarget(Document)->RemoveField(Field);
        Fixture.Refuses(*this, FString(TEXT("Missing target evidence ")) + Field, P4CaptureRender(Document));
        Document = P4CaptureDocument(Fixture.Snapshot);
        P4CaptureTarget(Document)->SetField(Field, MakeShared<FJsonValueBoolean>(true));
        Fixture.Refuses(*this, FString(TEXT("Wrong target evidence type ")) + Field, P4CaptureRender(Document));
    }
    const TCHAR* EntryFields[] = {TEXT("name"), TEXT("value")};
    for (const TCHAR* Field : EntryFields)
    {
        auto Document = P4CaptureDocument(Fixture.Snapshot);
        P4CaptureEntry(Document)->RemoveField(Field);
        Fixture.Refuses(*this, FString(TEXT("Incomplete selected entry ")) + Field, P4CaptureRender(Document));
        for (const auto& Shape : Shapes)
        {
            Document = P4CaptureDocument(Fixture.Snapshot);
            P4CaptureEntry(Document)->SetField(Field, Shape);
            Fixture.Refuses(*this, FString(TEXT("Wrong selected entry type ")) + Field, P4CaptureRender(Document));
        }
    }
    auto Document = P4CaptureDocument(Fixture.Snapshot);
    auto Entries = P4CaptureTarget(Document)->GetArrayField(TEXT("values"));
    Entries[0] = MakeShared<FJsonValueNumber>(0);
    P4CaptureTarget(Document)->SetArrayField(TEXT("values"), Entries);
    Fixture.Refuses(*this, TEXT("Selected values contain a non-record"), P4CaptureRender(Document));
    Document = P4CaptureDocument(Fixture.Snapshot);
    P4CaptureEntry(Document)->SetStringField(TEXT("name"), TEXT(""));
    Fixture.Refuses(*this, TEXT("Selected identity is empty"), P4CaptureRender(Document));
    Document = P4CaptureDocument(Fixture.Snapshot);
    P4CaptureEntry(Document)->SetStringField(TEXT("name"), FString::ChrN(NAME_SIZE, TEXT('x')));
    Fixture.Refuses(*this, TEXT("Selected identity cannot fit an engine FName"), P4CaptureRender(Document));
    const FString Complete = P4CaptureRender(Fixture.Independent);
    Fixture.Refuses(*this, TEXT("Target path embedded NUL cannot substitute an original object identity"),
        Complete.Replace(TEXT("/Script/Engine.TextureGroup\""), TEXT("/Script/Engine.TextureGroup\\u0000replaced\"")));
    Fixture.Refuses(*this, TEXT("Observed status embedded NUL cannot qualify a different record status"),
        Complete.Replace(TEXT("\"observed\""), TEXT("\"observed\\u0000replaced\"")));
    Fixture.Refuses(*this, TEXT("Duplicate unknown keys after complete valid target still refuse publication"),
        Complete.LeftChop(1) + TEXT(",\"after_target\":{\"same\":1,\"same\":2}}"));
    Fixture.Refuses(*this, TEXT("Exact duplicate keys with embedded NUL are detected without C-string truncation"),
        Complete.LeftChop(1) + TEXT(",\"after_target\":{\"key\\u0000x\":1,\"key\\u0000x\":2}}"));
    Fixture.Unchanged(*this);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TextureCaptureTransportAutomation,
    "P4MapKit.Quality.TextureCaptureTimeAndTransport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TextureCaptureTransportAutomation::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4TextureCaptureEvidenceTests;
    FP4CaptureEvidenceFixture Fixture;
    if (!Fixture.Load(*this))
        return false;
    const TCHAR* ValidTimes[] = {TEXT("2024-02-29T23:59:59Z"), TEXT("2026-01-02T03:04:05.1-07:30"),
        TEXT("2026-01-02T03:04:05.123456789+00:00"), TEXT("0001-01-01T00:00:00+23:59"),
        TEXT("9999-12-31T23:59:59-23:59")};
    for (const TCHAR* Time : ValidTimes)
    {
        auto Document = P4CaptureDocument(Fixture.Snapshot);
        Document->SetStringField(TEXT("captured_at"), Time);
        Fixture.Accepts(*this, P4CaptureRender(Document), Document);
    }
    const TCHAR* InvalidTimes[] = {TEXT("2026-01-02"), TEXT("2026/01-02T03:04:05Z"), TEXT("2026-01/02T03:04:05Z"),
        TEXT("2026-01-02t03:04:05Z"), TEXT("2026-01-02T03/04:05Z"), TEXT("2026-01-02T03:04/05Z"),
        TEXT("202x-01-02T03:04:05Z"), TEXT("2026-0x-02T03:04:05Z"), TEXT("2026-01-0xT03:04:05Z"),
        TEXT("2026-01-02T0x:04:05Z"), TEXT("2026-01-02T03:0x:05Z"), TEXT("2026-01-02T03:04:0xZ"),
        TEXT("2026-01-02T24:00:00Z"), TEXT("2026-01-02T03:60:00Z"), TEXT("2026-01-02T03:04:60Z"),
        TEXT("2026-01-02T03:04:05.Z"), TEXT("2026-01-02T03:04:05.1234567890Z"), TEXT("2026-01-02T03:04:05"),
        TEXT("2026-01-02T03:04:05x00:00"), TEXT("2026-01-02T03:04:05+00/00"), TEXT("2026-01-02T03:04:05+0x:00"),
        TEXT("2026-01-02T03:04:05+00:0x"), TEXT("2026-01-02T03:04:05+24:00"), TEXT("2026-01-02T03:04:05+00:60")};
    for (const TCHAR* Time : InvalidTimes)
    {
        auto Document = P4CaptureDocument(Fixture.Snapshot);
        Document->SetStringField(TEXT("captured_at"), Time);
        Fixture.Refuses(*this, FString(TEXT("Unqualified timestamp ")) + Time, P4CaptureRender(Document));
    }
    const FString Complete = P4CaptureRender(Fixture.Independent);
    Fixture.Accepts(*this, TEXT(" \t\r\n") + Complete + TEXT(" \r\n\t"), Fixture.Independent);
    Fixture.Accepts(
        *this, Complete.Replace(TEXT("TEXTUREGROUP_World\""), TEXT("TEXTUREGROUP_Wor\\u006cd\"")), Fixture.Independent);
    auto Document = P4CaptureDocument(Fixture.Snapshot);
    // Independently construct a valid non-BMP scalar through the installed
    // engine UTF8 converter; the reader must preserve its selected UTF16 units.
    P4CaptureEntry(Document)->SetStringField(
        TEXT("name"), FString(TEXT("TEXTUREGROUP_")) + UTF8_TO_TCHAR("\xF0\x9F\x98\x80"));
    Fixture.Accepts(
        *this, Complete.Replace(TEXT("TEXTUREGROUP_World\""), TEXT("TEXTUREGROUP_\\uD83D\\uDE00\"")), Document);
    Document = P4CaptureDocument(Fixture.Snapshot);
    const FString Scope = Document->GetStringField(TEXT("scope")) + TEXT("\b\f\n\r\t/\\\"");
    Document->SetStringField(TEXT("scope"), Scope);
    Fixture.Accepts(*this, P4CaptureRender(Document), Document);
    Fixture.Refuses(*this, TEXT("Selected SHA embedded NUL never authenticates stale output"),
        Complete.Replace(TEXT("\"source_sha256\":\""), TEXT("\"source_sha256\":\"\\u0000")));
    Fixture.Refuses(*this, TEXT("Selected timestamp embedded NUL never survives projection"),
        Complete.Replace(TEXT("\"captured_at\":\""), TEXT("\"captured_at\":\"\\u0000")));
    const FString Escapes[] = {
        TEXT("\\uDC00"), TEXT("\\uD800\\u0041"), TEXT("\\uD800\\uDC0G"), TEXT("\\u12"), TEXT("\\u00G0"), TEXT("\\q")};
    for (const FString& Escape : Escapes)
        Fixture.Refuses(*this, TEXT("Unrepresentable selected escape refuses completely"),
            Complete.Replace(TEXT("TEXTUREGROUP_World\""), *(Escape + TEXT("\""))));
    Document = P4CaptureDocument(Fixture.Snapshot);
    Document->SetStringField(TEXT("source_sha256"), FString::ChrN(64, TEXT('g')));
    Fixture.Refuses(*this, TEXT("Hash has correct length but nonhex characters"), P4CaptureRender(Document));
    Document = P4CaptureDocument(Fixture.Snapshot);
    Document->SetStringField(TEXT("scope"), TEXT("Non-atomic metadata snapshot."));
    Fixture.Refuses(*this, TEXT("Scope omits loaded-object limitation"), P4CaptureRender(Document));
    Fixture.Unchanged(*this);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TextureCaptureBudgetAutomation,
    "P4MapKit.Quality.TextureCaptureBudgetAndLateCancellation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TextureCaptureBudgetAutomation::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4TextureCaptureEvidenceTests;
    FP4CaptureEvidenceFixture Fixture;
    if (!Fixture.Load(*this))
        return false;
    TArray<FP4TextureProfileCaptureBudget> Invalid;
    FP4TextureProfileCaptureBudget Budget;
    Budget.MaxNotations = 0;
    Invalid.Add(Budget);
    Budget = {};
    Budget.MaxNotations = MAX_uint64 / 4 + 1;
    Invalid.Add(Budget);
    Budget = {};
    Budget.MaxDepth = 0;
    Invalid.Add(Budget);
    Budget = {};
    Budget.MaxDepth = 257;
    Invalid.Add(Budget);
    Budget = {};
    Budget.MaxObjectKeys = 0;
    Invalid.Add(Budget);
    Budget = {};
    Budget.MaxObjectKeys = MAX_uint32;
    Invalid.Add(Budget);
    Budget = {};
    Budget.MaxStringCharacters = 0;
    Invalid.Add(Budget);
    Budget = {};
    Budget.MaxStringCharacters = MAX_uint32;
    Invalid.Add(Budget);
    Budget = {};
    Budget.MaxEnumEntries = 0;
    Invalid.Add(Budget);
    Budget = {};
    Budget.MaxEnumEntries = MAX_uint32;
    Invalid.Add(Budget);
    for (const auto& Rejected : Invalid)
    {
        uint32 Calls = 0;
        Fixture.Refuses(*this, TEXT("Invalid caller resource contract"), Fixture.Snapshot, Rejected,
            [&]()
            {
                ++Calls;
                return false;
            });
        TestEqual(TEXT("Invalid budget refuses before parser/cancellation work"), Calls, uint32(0));
    }
    Fixture.Refuses(*this, TEXT("Empty immutable snapshot has no evidence"), TEXT(""));
    Budget = {};
    Budget.MaxCharacters = uint64(Fixture.Snapshot.Len());
    Fixture.Accepts(*this, Fixture.Snapshot, Fixture.Independent, Budget);
    uint32 SuccessfulPolls = 0;
    FP4TextureProfileCapture Result;
    FString Error;
    TestTrue(TEXT("Record real cooperative poll sequence on valid original projection"),
        P4ReadTextureProfileCapture(
            Fixture.Snapshot, {},
            [&]()
            {
                ++SuccessfulPolls;
                return false;
            },
            Result, Error));
    Fixture.Matches(*this, Result, Fixture.Independent);
    if (!TestTrue(TEXT("Valid original projection reaches multiple cooperative polls"), SuccessfulPolls > 1))
        return false;
    uint32 CancelPolls = 0;
    Fixture.Refuses(*this, TEXT("Cancellation at last observed yield cannot publish already selected target"),
        Fixture.Snapshot, {}, [&]() { return ++CancelPolls == SuccessfulPolls; });
    TestEqual(TEXT("Late cancellation reached recorded final poll"), CancelPolls, SuccessfulPolls);
    auto Document = P4CaptureDocument(Fixture.Snapshot);
    Document->SetStringField(TEXT("scope"), TEXT("Non-atomic; only loaded objects."));
    const FString Compact = P4CaptureRender(Document);
    const FString LongNumber = TEXT("1") + FString::ChrN(256, TEXT('0'));
    Budget = {};
    Budget.MaxStringCharacters = 128;
    Fixture.Refuses(*this, TEXT("Large unrelated numeric token obeys bounded scanning contract"),
        Compact.LeftChop(1) + TEXT(",\"unselected_numeric\":") + LongNumber + TEXT("}"), Budget);
    Fixture.Accepts(*this, Fixture.Snapshot, Fixture.Independent);
    Fixture.Unchanged(*this);
    return !HasAnyErrors();
}
#endif

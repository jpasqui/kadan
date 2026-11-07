#include "P4TextureProfileCapture.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
bool P4TestTextureProfileCapture(FString& Failure, uint32& Checks)
{
    Failure.Empty();
    Checks = 0;
    const auto Check = [&](bool Value, const TCHAR* Label)
    {
        ++Checks;
        if (!Value && Failure.IsEmpty())
            Failure = Label;
        return Value;
    };
    const auto Record = [](int32 Count)
    {
        FString S = TEXT("{\"values\":[");
        for (int32 N = 0; N < Count; ++N)
        {
            if (N)
                S += TEXT(",");
            S += FString::Printf(TEXT("{\"value\":%d,\"name\":\"CaptureFixture_%d\"}"), N, N);
        }
        return S + TEXT("],\"status\":\"observed\",\"path\":\"/Script/Engine.TextureGroup\"}");
    };
    const FString Prefix = TEXT(
        "{\"schema_version\":2,\"source_sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\",\"captured_at\":\"2026-01-02T03:04:05.123456+00:00\",\"scope\":\"Non-atomic metadata snapshot; only loaded objects.\",\"enums\":[");
    const FString Valid = Prefix + Record(65) + TEXT("]}");
    FP4TextureProfileCaptureBudget Budget;
    FP4TextureProfileCapture Out;
    FString Error;
    if (!Check(P4ReadTextureProfileCapture(Valid, Budget, {}, Out, Error), TEXT("capture-valid-streaming-fixture")))
    {
        Failure += TEXT(": ") + Error;
        return false;
    }
    if (!Check(Out.Entries.Num() == 65 &&
                Out.Entries[0].Name.Equals(TEXT("CaptureFixture_0"), ESearchCase::CaseSensitive) &&
                Out.Entries[64].Value == 64 &&
                Out.DeclaredCapturedAt.Equals(TEXT("2026-01-02T03:04:05.123456+00:00"), ESearchCase::CaseSensitive) &&
                !Out.RuntimeProvenanceAuthenticated && !Out.DefaultsProvenanceAuthenticated && !Out.CaptureAtomic,
            TEXT("capture-order-declared-scope-only")))
        return false;
    for (int32 N = 0; N < 65; ++N)
        if (!Check(Out.Entries[N].Value == N &&
                    Out.Entries[N].Name.Equals(
                        FString::Printf(TEXT("CaptureFixture_%d"), N), ESearchCase::CaseSensitive),
                TEXT("capture-all-original-order-values")))
            return false;
    uint32 RefusalCase = 0;
    const auto Refuses =
        [&](const FString& Input, const TCHAR* Expected = nullptr, const TFunction<bool()>& Cancel = {})
    {
        FP4TextureProfileCapture Stale = Out;
        FString Why;
        const bool Good = P4ReadTextureProfileCapture(Input, Budget, Cancel, Stale, Why);
        const FString Label = FString::Printf(
            TEXT(
                "capture-refusal-%u check=%u accepted=%d error=%s expected=%s entries=%d sha_len=%d time_len=%d scope_len=%d"),
            ++RefusalCase, Checks + 1, int32(Good), *Why, Expected ? Expected : TEXT("any-explicit-refusal"),
            Stale.Entries.Num(), Stale.DeclaredSourceSha256.Len(), Stale.DeclaredCapturedAt.Len(),
            Stale.DeclaredScope.Len());
        return Check(!Good && Stale.Entries.Num() == 0 && Stale.DeclaredSourceSha256.IsEmpty() && !Why.IsEmpty() &&
                (!Expected || Why.Equals(Expected, ESearchCase::CaseSensitive)),
            *Label);
    };
    if (!Refuses(Prefix + Record(64) + TEXT("]}"), TEXT("capture-target-unqualified")) ||
        !Refuses(Prefix + Record(66) + TEXT("]}"), TEXT("capture-target-unqualified")) ||
        !Refuses(Prefix + Record(65) + TEXT(",") + Record(65) + TEXT("]}"), TEXT("capture-duplicate-target")) ||
        !Refuses(Valid.Replace(TEXT("/Script/Engine.TextureGroup"), TEXT("/script/engine.texturegroup")),
            TEXT("capture-target-identity-replaced")) ||
        !Refuses(Valid.Replace(TEXT("CaptureFixture_64"), TEXT("capturefixture_0")),
            TEXT("capture-entry-identity-replaced")) ||
        !Refuses(Valid.Replace(TEXT("\"value\":64"), TEXT("\"value\":0")), TEXT("capture-entry-identity-replaced")) ||
        !Refuses(Valid.Replace(TEXT("\"name\":\"CaptureFixture_0\""),
                     TEXT("\"name\":\"CaptureFixture_0\",\"name\":\"replaced\"")),
            TEXT("capture-duplicate-key")) ||
        !Refuses(Valid.Replace(TEXT("\"schema_version\":2"), TEXT("\"schema_version\":2,\"schema_version\":2")),
            TEXT("capture-duplicate-key")) ||
        !Refuses(Valid.LeftChop(1)) || !Refuses(Valid + TEXT("{}")) || !Refuses(TEXT("[]")) ||
        !Refuses(Valid.Replace(TEXT("\"observed\""), TEXT("\"unresolved\"")), TEXT("capture-target-unqualified")))
        return false;
    const FString StrictCases[] = {
        Valid.Replace(TEXT("\"schema_version\":2"), TEXT("\"schema_version\":2,\"unknown\":True")),
        Valid.Replace(TEXT("\"schema_version\":2"), TEXT("\"schema_version\":2,\"unknown\":01")),
        Valid.Replace(TEXT("\"schema_version\":2"), TEXT("\"schema_version\":2,\"unknown\":{\"x\":1,\"x\":2}")),
        Valid.Replace(TEXT("\"schema_version\":2"), TEXT("\"schema_version\":2,\"unknown\":[1,]")),
        Valid.Replace(TEXT("\"schema_version\":2"), TEXT("\"schema_version\":2,\"unknown\":\"raw\tcontrol\"")),
        Valid.Replace(TEXT("\"schema_version\":2"), TEXT("\"schema_version\":2,\"unknown\":\"\\ud800\"")),
        Valid.Replace(TEXT("CaptureFixture_0"), TEXT("CaptureFixture_\\u0000")),
        Valid.Replace(TEXT("only loaded objects."), TEXT("only loaded objects.\\u0000")),
        Valid.Replace(TEXT("only loaded objects."), TEXT("only loaded objects.\\ufffe")),
        Valid.Replace(TEXT("only loaded objects."), TEXT("only loaded objects.\\uffff")),
        Valid.Replace(TEXT("123456+00:00"), TEXT("123456+00:00junk")),
        Valid.Replace(TEXT("2026-01-02"), TEXT("2026-02-31")),
        Valid.Replace(TEXT("Non-atomic metadata snapshot; only loaded objects."), TEXT("complete atomic defaults")),
        Valid.Replace(TEXT("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"), TEXT("not-a-hash")),
        Valid.Replace(TEXT("\"value\":0"), TEXT("\"value\":9223372036854775808")),
        Valid.Replace(TEXT("\"value\":0"), TEXT("\"value\":0.5")),
        Valid.Replace(TEXT("\"schema_version\":2"), TEXT("\"schema_version\":1"))};
    const TCHAR* StrictNames[] = {TEXT("uppercase-literal"), TEXT("leading-zero-number"),
        TEXT("unrelated-duplicate-key"), TEXT("trailing-array-comma"), TEXT("raw-string-tab"),
        TEXT("unpaired-surrogate"), TEXT("selected-name-nul"), TEXT("scope-nul"), TEXT("scope-fffe"),
        TEXT("scope-ffff"), TEXT("timestamp-trailing-junk"), TEXT("invalid-calendar-date"), TEXT("atomic-scope"),
        TEXT("invalid-hash"), TEXT("signed64-overflow"), TEXT("fractional-ordinal"), TEXT("wrong-schema")};
    static_assert(
        UE_ARRAY_COUNT(StrictCases) == UE_ARRAY_COUNT(StrictNames), "Strict fixtures need precise diagnostics");
    for (uint32 Index = 0; Index < UE_ARRAY_COUNT(StrictCases); ++Index)
        if (!Refuses(StrictCases[Index]))
        {
            Failure = FString::Printf(TEXT("strict[%u]=%s: %s"), Index, StrictNames[Index], *Failure);
            return false;
        }
    if (!Refuses(Valid, TEXT("capture-cancelled"), []() { return true; }))
        return false;
    uint32 Calls = 0;
    if (!Refuses(Valid, TEXT("capture-cancelled"), [&]() { return ++Calls > 10; }))
        return false;
    const auto Accepts = [&](const FString& Input)
    {
        FP4TextureProfileCapture R;
        FString Why;
        return Check(P4ReadTextureProfileCapture(Input, Budget, {}, R, Why) && R.Entries.Num() == 65,
            TEXT("capture-syntax-only-unknown-fields-retained-in-snapshot"));
    };
    if (!Accepts(Valid.Replace(TEXT("\"schema_version\":2"),
            TEXT(
                "\"schema_version\":2,\"unknown\":{\"x\":1,\"X\":2,\"unicode\":\"\\ud83d\\ude00\",\"nullable\":null,\"bool\":true}"))))
        return false;
    if (!Accepts(Valid.Replace(TEXT("\"schema_version\":2"),
            TEXT("\"schema_version\":2,\"unknown\":{\"key\\u0000x\":1,\"key\\u0000y\":2}"))))
        return false;
    FP4TextureProfileCapture Sparse;
    FString Why;
    const FString Signed = Valid.Replace(TEXT("\"value\":0"), TEXT("\"value\":-9223372036854775808"))
                               .Replace(TEXT("\"value\":64"), TEXT("\"value\":9223372036854775807"));
    if (!Check(P4ReadTextureProfileCapture(Signed, Budget, {}, Sparse, Why) && Sparse.Entries[0].Value == MIN_int64 &&
                Sparse.Entries[64].Value == MAX_int64,
            TEXT("capture-exact-signed64-not-double-rounded-or-indexed")))
        return false;
    Budget.MaxCharacters = uint64(Valid.Len()) - 1;
    if (!Refuses(Valid, TEXT("capture-input-budget")))
        return false;
    Budget = {};
    Budget.MaxDepth = 1;
    if (!Refuses(Valid, TEXT("capture-depth-budget")))
        return false;
    Budget = {};
    Budget.MaxObjectKeys = 1;
    if (!Refuses(Valid, TEXT("capture-key-budget")))
        return false;
    Budget = {};
    Budget.MaxEnumEntries = 64;
    if (!Refuses(Valid, TEXT("capture-enum-entry-budget")))
        return false;
    Budget = {};
    Budget.MaxNotations = 20;
    if (!Refuses(Valid))
        return false;
    Budget = {};
    Budget.MaxStringCharacters = 4;
    if (!Refuses(Valid, TEXT("capture-string-budget")))
        return false;
    return true;
}
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TextureProfileCaptureAutomation, "P4MapKit.Quality.TextureProfileCapture",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TextureProfileCaptureAutomation::RunTest(const FString& Parameters)
{
    (void)Parameters;
    FString Failure;
    uint32 Checks = 0;
    const bool Passed = P4TestTextureProfileCapture(Failure, Checks);
    TestTrue(FString::Printf(TEXT("Streaming native capture extraction (%u checks): %s"), Checks, *Failure), Passed);
    TestTrue(TEXT("Exact enumeration/syntax/identity/budget/cancellation fixtures executed"), Checks >= 100);
    return Passed && Checks >= 100;
}
#endif

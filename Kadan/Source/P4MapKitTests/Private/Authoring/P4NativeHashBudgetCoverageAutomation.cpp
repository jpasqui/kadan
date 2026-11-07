#include "P4NativeHash.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS && PLATFORM_WINDOWS
#include "Misc/AutomationTest.h"

namespace P4NativeHashBudgetAfter222
{
bool AllCodeUnitsRetained(const FString& Text, int32 ExpectedCount, TCHAR ExpectedUnit)
{
    if (Text.Len() != ExpectedCount)
        return false;
    for (int32 I = 0; I < Text.Len(); ++I)
        if (Text[I] != ExpectedUnit)
            return false;
    return true;
}
} //namespace P4NativeHashBudgetAfter222

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4NativeUtf8StorageBudget, "P4MapKit.Quality.NativeUtf8StorageBudget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4NativeUtf8StorageBudget::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4NativeHashBudgetAfter222;
    if (!TestEqual(TEXT("Installed Windows UE UTF16 code-unit width"), int32(sizeof(TCHAR)), 2))
        return false;
    // ASCII is otherwise valid UTF8; refusal specifically protects the input
    // storage budget before any conversion or hash can publish a digest.
    constexpr int64 BudgetBytes = 64LL * 1024 * 1024;
    constexpr int32 Count = int32(BudgetBytes / 2 + 1);
    const FString Input = FString::ChrN(Count, TEXT('x'));
    TestTrue(TEXT("Caller supplies a valid nonempty ASCII string exceeding the native input storage budget"),
        int64(Input.Len()) * 2 == BudgetBytes + 2);
    FString Digest(TEXT("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    TestFalse(TEXT("Oversized input cannot publish a digest for a truncated prefix"), P4HashUtf8(Input, Digest));
    TestTrue(TEXT("Refusal clears the caller's previously valid digest"), Digest.IsEmpty());
    TestTrue(TEXT("Every input code unit and the exact native string length survive refusal"),
        AllCodeUnitsRetained(Input, Count, TEXT('x')));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4NativeUtf8EncodedBudget, "P4MapKit.Quality.NativeUtf8EncodedBudget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4NativeUtf8EncodedBudget::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4NativeHashBudgetAfter222;
    if (!TestEqual(TEXT("Installed Windows UE UTF16 code-unit width"), int32(sizeof(TCHAR)), 2))
        return false;
    constexpr int64 BudgetBytes = 64LL * 1024 * 1024;
    // U+0800 is a valid non-surrogate BMP scalar whose standard UTF8 spelling
    // is the independently literal three bytes E0 A0 80. The short control's
    // digest below was computed from those bytes, not from the production API.
    constexpr TCHAR Scalar = TCHAR(0x0800);
    FString Control;
    Control.AppendChar(Scalar);
    FString Digest;
    if (!TestTrue(TEXT("Actual provider accepts the genuine Unicode control"), P4HashUtf8(Control, Digest)))
        return false;
    TestTrue(TEXT("Unicode control matches independent literal-byte SHA256"),
        Digest.Equals(
            TEXT("a36ae1af6063cc9281d4010e979a1e3fd3d9ee3e804b7093d2b00f6ee6388bec"), ESearchCase::CaseSensitive));
    TestTrue(TEXT("Short caller-owned Unicode control is unchanged"), AllCodeUnitsRetained(Control, 1, Scalar));
    constexpr int32 Count = int32(BudgetBytes / 3 + 1);
    const FString Input = FString::ChrN(Count, Scalar);
    TestTrue(TEXT("Native input storage is under budget but independently counted UTF8 bytes exceed it"),
        int64(Input.Len()) * 2 < BudgetBytes && int64(Input.Len()) * 3 == BudgetBytes + 2);
    TestFalse(TEXT("UTF8 expansion beyond the byte budget refuses instead of publishing a truncated digest"),
        P4HashUtf8(Input, Digest));
    TestTrue(TEXT("Encoding-budget refusal clears the genuine prior digest"), Digest.IsEmpty());
    TestTrue(TEXT("Every native Unicode scalar and exact length survive refusal"),
        AllCodeUnitsRetained(Input, Count, Scalar));
    return !HasAnyErrors();
}
#endif

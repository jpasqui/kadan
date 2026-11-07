#include "P4OriginalClassCoverage.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalClassStrictCoverageTest, "P4MapKit.Kadan.StrictClassDefaultCoverage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalClassStrictCoverageTest::RunTest(const FString&)
{
    using Scope = EP4OriginalClassDefaultObservation;
    FP4OriginalClassComparisonResult Same;
    Same.PackageKnown = Same.ValuesKnown = Same.TemplatesKnown = Same.ComparedKnown = Same.DefaultsReadable =
        Same.Same = true;
    Same.Compared = 127;
    Same.GameDefaultFactsDigest = Same.DefaultFactsDigest = FString::ChrN(64, TEXT('a'));
    FString Reason;
    TestTrue(TEXT("Complete observed native stored defaults can qualify independently of ordinary import policy"),
        P4StrictOriginalClassDefaultsQualified(Same, Scope::NativeStoredDefaults, Reason));
    auto Missing = Same;
    Missing.ValuesNotHeld = 26;
    TestFalse(TEXT("The real same=true/127lines/26unheld coverage shape cannot qualify a complete CDO"),
        P4StrictOriginalClassDefaultsQualified(Missing, Scope::NativeStoredDefaults, Reason));
    TestTrue(TEXT("Unsupported field count remains explicit in strict refusal"), Reason.EndsWith(TEXT(": 26")));
    auto Unknown = Same;
    Unknown.ValuesKnown = false;
    TestFalse(TEXT("An empty resume array cannot stand in for known class coverage"),
        P4StrictOriginalClassDefaultsQualified(Unknown, Scope::NativeStoredDefaults, Reason));
    auto Different = Same;
    Different.Same = false;
    Different.Differences = 1;
    TestFalse(TEXT("An unclosed class-default difference refuses"),
        P4StrictOriginalClassDefaultsQualified(Different, Scope::NativeStoredDefaults, Reason));
    TestFalse(TEXT("A temporary exact source loan cannot masquerade as persistent native defaults"),
        P4StrictOriginalClassDefaultsQualified(Same, Scope::TemporarilyBorrowedForCook, Reason));
    auto Unbound = Same;
    Unbound.DefaultFactsDigest.Reset();
    TestFalse(TEXT("Legacy same without both bound fact digests is not strict proof"),
        P4StrictOriginalClassDefaultsQualified(Unbound, Scope::NativeStoredDefaults, Reason));
    auto Unreadable = Same;
    Unreadable.DefaultsReadable = false;
    TestFalse(TEXT("Undecoded default/component native payload cannot qualify"),
        P4StrictOriginalClassDefaultsQualified(Unreadable, Scope::NativeStoredDefaults, Reason));
    return !HasAnyErrors();
}
#endif

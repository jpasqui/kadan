#include "P4KadanModIdentity.h"
#include "P4MapKitCore/export_contract.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKadanModIdentity, "P4MapKit.Kadan.ModIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKadanModIdentity::RunTest(const FString&)
{
    TestEqual(TEXT("A creator name suggests the expected permanent identity"),
        P4KadanSuggestedModId(TEXT("Pikmix_Kadan")), FString(TEXT("pikmix_kadan")));
    TestEqual(TEXT("Word separators collapse without leading or trailing punctuation"),
        P4KadanSuggestedModId(TEXT("  My... mod!!  ")), FString(TEXT("my_mod")));
    TestEqual(TEXT("A numeric identity remains valid"), P4KadanSuggestedModId(TEXT("99")), FString(TEXT("99")));
    TestTrue(TEXT("An empty name does not invent an identity"), P4KadanSuggestedModId(FString()).IsEmpty());
    TestTrue(TEXT("Punctuation alone does not invent an identity"), P4KadanSuggestedModId(TEXT("!_-. ")).IsEmpty());
    TestEqual(
        TEXT("Tsuchi's own ID is never suggested"), P4KadanSuggestedModId(TEXT("Tsuchi")), FString(TEXT("tsuchi_mod")));
    TestFalse(TEXT("The export contract refuses Tsuchi's own ID"), P4MapKit::Export::ValidId("tsuchi"));
    for (const FString& Name : {FString(TEXT("Pikmix_Kadan")), FString(TEXT("Tsuchi.demo")), FString(TEXT("TSUCHI")),
             FString::ChrN(80, TCHAR('A')), FString::ChrN(46, TCHAR('b')) + TEXT(" x")})
    {
        const FString Id = P4KadanSuggestedModId(Name);
        TestTrue(TEXT("Suggestions pass the actual export ID contract"), P4MapKit::Export::ValidId(TCHAR_TO_UTF8(*Id)));
    }
    return !HasAnyErrors();
}
#endif

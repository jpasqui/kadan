#include "Misc/AutomationTest.h"
#include "P4KadanExportIssues.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKadanExportIssues, "P4MapKit.Kadan.ExportIssues",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FKadanExportIssues::RunTest(const FString&)
{
    FP4ExportSelectionRow Row;
    Row.Package = TEXT("/Game/Mods/Example/Required");
    Row.SelectingRoot = TEXT("/Game/Mods/Example/Root");
    Row.Reason = TEXT("Unknown producer diagnostic\nwith exact detail: 17; original text");
    auto Issue = P4KadanExportIssue(Row);
    TestEqual(TEXT("Exact unknown diagnostics are retained"), Issue.Detail, Row.Reason);
    TestEqual(TEXT("A dependency points to its actual selecting root"), Issue.SelectingRoot,
              Row.SelectingRoot);
    TestTrue(TEXT("A dependency directs removal to its root"),
             Issue.NextStep.Contains(TEXT("remove that root")));
    TestFalse(TEXT("Unknown errors do not claim a map repair"), Issue.NeedsPersistentMap);
    Row.Issue = EP4ExportIssueKind::PersistentMapRequired;
    Row.Reason = TEXT("The map fixture needs its parent.");
    Issue = P4KadanExportIssue(Row);
    TestTrue(TEXT("The typed persistent map issue gets a map action after wording changes"),
        Issue.NeedsPersistentMap);
    Row.Explicit = true;
    Row.Issue = EP4ExportIssueKind::None;
    Row.Reason = TEXT("Unsupported route");
    TestTrue(TEXT("Explicit content directs removal to its selected root"),
             P4KadanExportIssue(Row).NextStep.Contains(TEXT("selected root")));
    const FString Diagnostic = TEXT("The navigation build stopped.\nExact diagnostic: 17; original text");
    TestEqual(TEXT("The same export error and refusal are displayed once"),
        P4KadanExportDetails(Diagnostic, {Diagnostic}), Diagnostic);
    TestEqual(TEXT("A joined refusal list is not appended twice"),
        P4KadanExportDetails(TEXT("first\nsecond"), {TEXT("first"), TEXT("second")}),
        FString(TEXT("first\nsecond")));
    TestEqual(TEXT("Distinct details retain their exact text and order"),
        P4KadanExportDetails(Diagnostic, {Diagnostic, TEXT("another exact detail"), TEXT("another exact detail")}),
        Diagnostic + TEXT("\nanother exact detail"));
    return true;
}
#endif

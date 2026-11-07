#if P4_MAPKIT_TESTS
#include "P4TableExport.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TableManifestChanges, "P4MapKit.Export.TableManifestChanges",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TableManifestChanges::RunTest(const FString&)
{
    const FString Before = TEXT(
        "[{\"Name\":\"FIRST\",\"Life\":100,\"Numbers\":[10,20],\"Point\":{\"X\":1,\"Y\":2}},{\"Name\":\"SECOND\",\"Life\":200,\"Numbers\":[30],\"Point\":{\"X\":3,\"Y\":4}}]");
    const FString After = TEXT(
        "[{\"Name\":\"FIRST\",\"Life\":150,\"Numbers\":[10,20,30],\"Point\":{\"X\":1,\"Y\":2}},{\"Name\":\"SECOND\",\"Life\":200,\"Numbers\":[30],\"Point\":{\"X\":3,\"Y\":4}},{\"Name\":\"example.mod:big\",\"Life\":250,\"Numbers\":[30],\"Point\":{\"X\":3,\"Y\":4}}]");
    TSharedPtr<FJsonObject> Changes;
    FString Error;
    TestTrue(TEXT("unchanged tables produce no manifest rows"),
        P4TableChanges(TEXT("example.mod"), Before, Before, Changes, Error) && !Changes->Values.Num());
    if (!TestTrue(TEXT("values, new rows and array sizes become manifest changes"),
            P4TableChanges(TEXT("example.mod"), Before, After, Changes, Error)))
        return false;
    TestEqual(TEXT("unchanged stock row is omitted"), Changes->Values.Num(), 2);
    auto First = Changes->GetObjectField(TEXT("FIRST")), Added = Changes->GetObjectField(TEXT("example.mod:big"));
    TestEqual(TEXT("changed scalar"), First->GetNumberField(TEXT("Life")), 150.0);
    TestEqual(TEXT("array length"), First->GetObjectField(TEXT("Numbers"))->GetNumberField(TEXT("length")), 3.0);
    TestEqual(TEXT("new element value"), First->GetNumberField(TEXT("Numbers[2]")), 30.0);
    TestFalse(TEXT("unchanged array element is omitted"), First->HasField(TEXT("Numbers[0]")));
    TestEqual(TEXT("closest stock donor is named instead of copied"), Added->GetStringField(TEXT("from")),
        FString(TEXT("SECOND")));
    TestEqual(TEXT("only creator value and donor enter new row"), Added->Values.Num(), 2);
    TestFalse(
        TEXT("deleted stock row is refused"), P4TableChanges(TEXT("example.mod"), Before, TEXT("[]"), Changes, Error));
    TestFalse(TEXT("renamed stock row is refused"),
        P4TableChanges(
            TEXT("example.mod"), TEXT("[{\"Name\":\"FIRST\"}]"), TEXT("[{\"Name\":\"OTHER\"}]"), Changes, Error));
    TestFalse(TEXT("reordered stock rows are refused"),
        P4TableChanges(TEXT("example.mod"), TEXT("[{\"Name\":\"FIRST\"},{\"Name\":\"SECOND\"}]"),
            TEXT("[{\"Name\":\"SECOND\"},{\"Name\":\"FIRST\"}]"), Changes, Error));
    TestFalse(TEXT("new row belonging to another mod is refused"),
        P4TableChanges(TEXT("example.mod"), TEXT("[{\"Name\":\"FIRST\"}]"),
            TEXT("[{\"Name\":\"FIRST\"},{\"Name\":\"other.mod:row\"}]"), Changes, Error));
    TestTrue(TEXT("64 bit integer edits remain distinct above double precision"),
        P4TableChanges(TEXT("example.mod"), TEXT("[{\"Name\":\"FIRST\",\"Id\":9007199254740992}]"),
            TEXT("[{\"Name\":\"FIRST\",\"Id\":9007199254740993}]"), Changes, Error) &&
            Changes->HasField(TEXT("FIRST")));
    TestTrue(TEXT("final manifest keeps every bit of the integer edit"),
        P4TableManifestJson(Changes).Contains(TEXT("9007199254740993")));
    return true;
}
#endif

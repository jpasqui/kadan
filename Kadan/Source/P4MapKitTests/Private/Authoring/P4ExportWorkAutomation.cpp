#include "P4ExportWork.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKadanExportWork, "P4MapKit.Kadan.ExportWork",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKadanExportWork::RunTest(const FString&)
{
    const FString Id = TEXT("drive.test");
    const FString Requested = TEXT("C:/Project/Intermediate/Kadan/ExportWork");
    const TArray<FString> Mounted = {TEXT("C:/Project/Saved"), TEXT("C:/Project/Content")};
    const FString OtherDrive = P4ExportWorkingDirectory(TEXT("D:/Mods"), Requested, Id, Mounted);
    TestTrue(TEXT("Output on another drive chooses that drive even when the project is on C"),
        OtherDrive.StartsWith(TEXT("D:/")));
    TestFalse(TEXT("Private staging is outside the output folder"),
        FPaths::IsUnderDirectory(OtherDrive, TEXT("D:/Mods")));
    const FString Target = TEXT("D:/Mods/") + Id;
    TestFalse(TEXT("Private staging is outside the published mod"), FPaths::IsUnderDirectory(OtherDrive, Target));
    TestFalse(TEXT("The target is outside private staging"), FPaths::IsUnderDirectory(Target, OtherDrive));
    TestNotEqual(TEXT("Staging and target are independent"), OtherDrive, Target);
    TestEqual(TEXT("The same mod reuses its work folder"),
        P4ExportWorkingDirectory(TEXT("D:/Mods/"), Requested, Id, Mounted), OtherDrive);
    TestNotEqual(TEXT("Different mods have independent work folders"),
        P4ExportWorkingDirectory(TEXT("D:/Mods"), Requested, TEXT("second.mod"), Mounted), OtherDrive);
    TestEqual(TEXT("Existing same-volume work is reused by normal exports and private previews"),
        P4ExportWorkingDirectory(TEXT("C:/Mods"), Requested, Id, Mounted),
        Requested / (Id + TEXT(".export-work")));
    const FString Network = P4ExportWorkingDirectory(TEXT("//server/share/Mods"),
        TEXT("//server/share/Work"), Id, Mounted);
    TestTrue(TEXT("Network output keeps private staging on its share"),
        Network.StartsWith(TEXT("//server/share/")));
    TestFalse(TEXT("Network staging stays outside the output folder"),
        FPaths::IsUnderDirectory(Network, TEXT("//server/share/Mods")));
    const FString Eden = P4ExportWorkingDirectory(
        TEXT("C:/Users/Creator/eden/load/0100B7C00933A000"), Requested, Id, Mounted);
    TestFalse(TEXT("Eden's load directory does not contain export work"),
        FPaths::IsUnderDirectory(Eden, TEXT("C:/Users/Creator/eden/load")));
    TestTrue(TEXT("Eden work remains on the output volume"), Eden.StartsWith(TEXT("C:/")));
    const FString InsideTarget = P4ExportWorkingDirectory(TEXT("C:/Mods"),
        TEXT("C:/Mods/drive.test/Work"), Id, Mounted);
    TestFalse(TEXT("A requested work folder inside the target is replaced by independent staging"),
        FPaths::IsUnderDirectory(InsideTarget, TEXT("C:/Mods/drive.test")));
    for (const FString& Output : {TEXT("C:/Project/Saved/Exports"), TEXT("C:/Project/Content/Exports")})
    {
        const FString Work = P4ExportWorkingDirectory(Output, TEXT("D:/Work"), Id, Mounted);
        TestFalse(TEXT("The work path is available"), Work.IsEmpty());
        TestTrue(TEXT("Work stays on the output volume"), Work.StartsWith(TEXT("C:/")));
        for (const FString& Directory : Mounted)
        {
            TestFalse(TEXT("Work stays outside Unreal's mounted directories"),
                Work.Equals(Directory, ESearchCase::IgnoreCase) || FPaths::IsUnderDirectory(Work, Directory));
        }
        TestFalse(TEXT("Work remains outside the target"), FPaths::IsUnderDirectory(Work, Output / Id));
    }
    TestTrue(TEXT("An empty output cannot choose staging"),
        P4ExportWorkingDirectory(FString(), Requested, Id, Mounted).IsEmpty());
    TestTrue(TEXT("An identity cannot escape the private work folder"),
        P4ExportWorkingDirectory(TEXT("D:/Mods"), Requested, TEXT("../escape"), Mounted).IsEmpty());
    return !HasAnyErrors();
}
#endif

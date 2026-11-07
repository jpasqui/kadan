#include "P4ExportContainer.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4DiskFull, "P4MapKit.Kadan.DiskFull",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4DiskFull::RunTest(const FString&)
{
    const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Kadan/DiskFull"));
    IFileManager& Files = IFileManager::Get();
    Files.DeleteDirectory(*Root, false, true);
    Files.MakeDirectory(*Root, true);

    // The message appears below the floor and names the disk's folder.
    const FString Before = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_KADAN_DISK_FLOOR_GB"));
    FPlatformMisc::SetEnvironmentVar(TEXT("P4_KADAN_DISK_FLOOR_GB"), TEXT("1000000000"));
    const FString Full = P4DiskFullMessage(Root);
    TestTrue(TEXT("Under the floor the message says the disk is full"), Full.Contains(TEXT("almost full")));
    TestTrue(TEXT("The message names the folder"), Full.Contains(Root));
    FPlatformMisc::SetEnvironmentVar(TEXT("P4_KADAN_DISK_FLOOR_GB"), TEXT("0"));
    TestTrue(TEXT("Over the floor there is no message"), P4DiskFullMessage(Root).IsEmpty());
    FPlatformMisc::SetEnvironmentVar(TEXT("P4_KADAN_DISK_FLOOR_GB"), *Before);

    const FString From = Root / TEXT("from.bin");
    FFileHelper::SaveStringToFile(TEXT("kept"), *From);

    // A good copy arrives whole, with no part file.
    const FString To = Root / TEXT("to.bin");
    TestTrue(TEXT("The copy works"), P4CopyFileWhole(From, To));
    TestTrue(TEXT("The file is there"), Files.FileExists(*To));
    TestFalse(TEXT("No part file stays"), Files.FileExists(*(To + TEXT(".part"))));

    // A folder in the way makes the final move fail. Unreal logs that as an error unless told not to, and an error
    // in the log fails this test, so a failed library save can no longer fail an import's run.
    const FString Blocked = Root / TEXT("blocked.bin");
    Files.MakeDirectory(*Blocked, true);
    TestFalse(TEXT("The copy reports the failure"), P4CopyFileWhole(From, Blocked));
    TestFalse(TEXT("No part file stays after a failure"), Files.FileExists(*(Blocked + TEXT(".part"))));

    Files.DeleteDirectory(*Root, false, true);
    return true;
}

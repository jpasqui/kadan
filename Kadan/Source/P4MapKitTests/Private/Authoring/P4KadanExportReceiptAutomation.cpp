#include "P4KadanExportReceipt.h"
#include "P4ExportDriver.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKadanExportReceipt, "P4MapKit.Kadan.ExportReceipt",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKadanExportReceipt::RunTest(const FString&)
{
    const FString Parent = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Kadan/ReceiptTests"));
    const FString Project = Parent / FGuid::NewGuid().ToString(EGuidFormats::Digits);
    if (!TestTrue(TEXT("The receipt fixture stays within its owned directory"), FPaths::IsUnderDirectory(Project, Parent)))
    {
        return false;
    }
    ON_SCOPE_EXIT
    {
        IFileManager::Get().DeleteDirectory(*Project, false, true);
    };
    FP4ModDescriptor Mod;
    Mod.Id = TEXT("receipt_test");
    Mod.Name = TEXT("Receipt test");
    Mod.Version = TEXT("1.0.0");
    FP4KadanExportReceipt Read;
    FString Error;
    TestFalse(TEXT("A missing receipt is absent"), P4KadanReadExportReceipt(Project, Mod.Id, Read, Error));
    TestTrue(TEXT("Missing receipt discovery has no error"), Error.IsEmpty());
    TestFalse(TEXT("A read does not create the project directory"), IFileManager::Get().DirectoryExists(*Project));
    FP4ExportModResult Published;
    Published.Published = true;
    Published.Directory = Project / TEXT("output") / Mod.Id;
    Published.Files = {TEXT("romfs/tsuchi/mods/receipt_test/mod.json")};
    Published.Told = {TEXT("A controlled fixture shipped without baked lighting.")};
    Published.UnbakedActors = {TEXT("Controlled fixture")};
    FP4KadanExportReceipt First;
    if (!TestTrue(TEXT("A published result produces a persistent receipt"),
        P4KadanPublishedExportReceipt(Project, Mod, Published, First, Error)) ||
        !TestTrue(TEXT("The saved receipt reads back"), P4KadanReadExportReceipt(Project, Mod.Id, Read, Error)))
    {
        AddError(Error);
        return false;
    }
    TestEqual(TEXT("The successful version is retained"), Read.Mod.Version, Mod.Version);
    TestEqual(TEXT("The exact successful directory is retained"), Read.Directory, Published.Directory);
    TestTrue(TEXT("Omission notes remain persistent"), Read.Notes == Published.Told);
    TestEqual(TEXT("Unbaked actor count remains visible"), Read.UnbakedActors, 1);
    TestFalse(TEXT("Publication does not claim a game test"), Read.GameTestRecorded);
    const FString File = Project / TEXT("Saved/Kadan/ExportReceipts") / (Mod.Id + TEXT(".json"));
    TArray<uint8> Before;
    FFileHelper::LoadFileToArray(Before, *File);
    FP4ExportModResult Failed;
    FP4KadanExportReceipt Refused;
    Mod.Version = TEXT("2.0.0");
    TestFalse(TEXT("A failed newer export cannot replace the receipt"),
        P4KadanPublishedExportReceipt(Project, Mod, Failed, Refused, Error));
    TArray<uint8> After;
    FFileHelper::LoadFileToArray(After, *File);
    TestTrue(TEXT("A failed newer export preserves the receipt bytes"), Before == After);
    Read.GameTestRecorded = true;
    Read.TestedUtc = FDateTime::UtcNow();
    TestTrue(TEXT("An explicit controlled test attestation can be recorded"), P4KadanWriteExportReceipt(Project, Read, Error));
    TestTrue(TEXT("The controlled attestation reads back"), P4KadanReadExportReceipt(Project, Read.Mod.Id, First, Error));
    TestTrue(TEXT("Only the explicit record marks game testing complete"), First.GameTestRecorded);
    TestFalse(TEXT("A traversal identity is not read"), P4KadanReadExportReceipt(Project, TEXT("../other"), First, Error));
    return !HasAnyErrors();
}
#endif

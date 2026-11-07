#include "P4ExportPublication.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKadanExportPublication, "P4MapKit.Kadan.ExportPublication",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKadanExportPublication::RunTest(const FString&)
{
    const FString Parent = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Kadan/PublicationTests"));
    const FString Root = Parent / FGuid::NewGuid().ToString(EGuidFormats::Digits);
    if (!TestTrue(TEXT("The owned fixture stays inside its intended directory"), FPaths::IsUnderDirectory(Root, Parent)))
    {
        return false;
    }
    ON_SCOPE_EXIT
    {
        IFileManager::Get().DeleteDirectory(*Root, false, true);
    };
    const FString Target = Root / TEXT("published");
    const FString Prepared = Root / TEXT("prepared");
    IFileManager::Get().MakeDirectory(*Target, true);
    IFileManager::Get().MakeDirectory(*Prepared, true);
    const FString Leaf = TEXT("fixture.txt");
    TestTrue(TEXT("The previous fixture is written"), FFileHelper::SaveStringToFile(TEXT("previous"), *(Target / Leaf)));
    TestTrue(TEXT("The prepared fixture is written"), FFileHelper::SaveStringToFile(TEXT("new"), *(Prepared / Leaf)));
    TArray<FString> Told;
    FString Error;
    TestTrue(TEXT("A validated prepared directory publishes"), P4PublishPreparedExport(Prepared, Target, true, Told, Error));
    FString Readback;
    TestTrue(TEXT("The publication can be read"), FFileHelper::LoadFileToString(Readback, *(Target / Leaf)));
    TestEqual(TEXT("Publication contains the new bytes"), Readback, FString(TEXT("new")));
    TestFalse(TEXT("Successful publication leaves no previous transaction"), IFileManager::Get().DirectoryExists(*(Target + TEXT(".previous"))));
    TestFalse(TEXT("A missing prepared directory fails publication"), P4PublishPreparedExport(Prepared, Target, true, Told, Error));
    TestTrue(TEXT("The diagnostic retains the actual operating-system failure"), Error.Contains(TEXT("Windows error")));
    Readback.Empty();
    TestTrue(TEXT("Rollback restores the prior publication"), FFileHelper::LoadFileToString(Readback, *(Target / Leaf)));
    TestEqual(TEXT("Rollback preserves its exact bytes"), Readback, FString(TEXT("new")));
    TestFalse(TEXT("A successful rollback leaves no recovery directory"), IFileManager::Get().DirectoryExists(*(Target + TEXT(".previous"))));
    const FString Recovery = Target + TEXT(".previous");
    IFileManager::Get().MakeDirectory(*Recovery, true);
    TestFalse(TEXT("An unresolved transaction is preserved"), P4PublishPreparedExport(Prepared, Target, true, Told, Error));
    TestTrue(TEXT("The unresolved recovery directory remains"), IFileManager::Get().DirectoryExists(*Recovery));
    Readback.Empty();
    TestTrue(TEXT("The current publication still reads"), FFileHelper::LoadFileToString(Readback, *(Target / Leaf)));
    TestEqual(TEXT("Refusing a pending transaction preserves the publication"), Readback, FString(TEXT("new")));
    return !HasAnyErrors();
}
#endif

#include "P4KadanEdenOutput.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKadanEdenOutput, "P4MapKit.Kadan.EdenOutput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKadanEdenOutput::RunTest(const FString&)
{
    const FString Parent = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Kadan/EdenOutputTests"));
    const FString Root = Parent / FGuid::NewGuid().ToString(EGuidFormats::Digits);
    if (!TestTrue(TEXT("The filesystem fixture stays inside its owned directory"), FPaths::IsUnderDirectory(Root, Parent)))
    {
        return false;
    }
    ON_SCOPE_EXIT
    {
        IFileManager::Get().DeleteDirectory(*Root, false, true);
    };
    TestTrue(TEXT("Missing profiles do not produce an output suggestion"), P4KadanFindEdenOutput(Root).IsEmpty());
    TestFalse(TEXT("Discovery never creates a missing profile"), IFileManager::Get().DirectoryExists(*Root));
    TestTrue(TEXT("An absent user directory does not search the process working directory"),
        P4KadanFindEdenOutput(FString()).IsEmpty());
    const FString Default = Root / TEXT("eden/load/0100B7C00933A000");
    TestTrue(TEXT("The default title fixture is created"), IFileManager::Get().MakeDirectory(*Default, true));
    TestEqual(TEXT("The suggestion is the title directory, without adding a mod identity"),
        P4KadanFindEdenOutput(Root), Default);
    const FString CustomLoad = Root / TEXT("custom-load");
    const FString CustomTitle = CustomLoad / TEXT("0100B7C00933A000");
    TestTrue(TEXT("The configured title fixture is created"), IFileManager::Get().MakeDirectory(*CustomTitle, true));
    const FString Config = Root / TEXT("eden/config/qt-config.ini");
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Config), true);
    TestTrue(TEXT("An actual Qt-style configured load directory is written"),
        FFileHelper::SaveStringToFile(TEXT("[Paths]\nload_directory=\"") + CustomLoad + TEXT("\"\nload_directory\\default=false\n"),
            *Config, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    TestEqual(TEXT("An existing absolute configured load directory takes precedence"),
        P4KadanFindEdenOutput(Root), CustomTitle);
    TestTrue(TEXT("A relative portable path is retained as configuration without assuming its executable directory"),
        FFileHelper::SaveStringToFile(TEXT("[Paths]\nload_directory=user/load\n"), *Config,
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    TestEqual(TEXT("An unresolved relative path falls back to the existing standard profile"),
        P4KadanFindEdenOutput(Root), Default);
    return !HasAnyErrors();
}
#endif

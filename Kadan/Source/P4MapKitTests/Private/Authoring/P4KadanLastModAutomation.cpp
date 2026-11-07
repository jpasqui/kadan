#include "P4KadanLastMod.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKadanLastMod, "P4MapKit.Kadan.LastMod",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKadanLastMod::RunTest(const FString&)
{
    const FString Root = FPaths::ProjectSavedDir() / TEXT("KadanTests");
    IFileManager::Get().MakeDirectory(*Root, true);
    const FString First = FPaths::CreateTempFilename(*Root, TEXT("last-mod-a"), TEXT(".ini"));
    const FString Second = FPaths::CreateTempFilename(*Root, TEXT("last-mod-b"), TEXT(".ini"));
    ON_SCOPE_EXIT
    {
        GConfig->UnloadFile(First);
        GConfig->UnloadFile(Second);
        IFileManager::Get().Delete(*First);
        IFileManager::Get().Delete(*Second);
    };
    TestTrue(TEXT("A project without a remembered identity starts empty"), P4KadanLastModId(First).IsEmpty());
    P4KadanRememberModId(First, TEXT("second"));
    P4KadanRememberModId(Second, TEXT("other-project"));
    GConfig->UnloadFile(First);
    TestEqual(TEXT("The saved ID reloads from disk"), P4KadanLastModId(First), FString(TEXT("second")));
    TestEqual(TEXT("Each project has its own last mod"), P4KadanLastModId(Second), FString(TEXT("other-project")));
    P4KadanRememberModId(First, FString());
    TestEqual(TEXT("An unsaved draft retains the last saved identity"), P4KadanLastModId(First), FString(TEXT("second")));
    P4KadanRememberModId(First, TEXT("first"));
    TestEqual(TEXT("An accepted switch replaces the remembered ID"), P4KadanLastModId(First), FString(TEXT("first")));
    TSharedPtr<FP4ModDescriptor> Available = MakeShared<FP4ModDescriptor>();
    Available->Id = TEXT("first");
    TSharedPtr<FP4ModDescriptor> Other = MakeShared<FP4ModDescriptor>();
    Other->Id = TEXT("other");
    TArray<TSharedPtr<FP4ModDescriptor>> Mods = {Available, Other};
    TestTrue(TEXT("The remembered mod is selected when present"),
        P4KadanFindRememberedMod(P4KadanLastModId(First), Mods) == Available);
    Mods.Remove(Available);
    TestFalse(TEXT("Deleting the remembered mod leaves no selection"),
        P4KadanFindRememberedMod(P4KadanLastModId(First), Mods).IsValid());
    return !HasAnyErrors();
}
#endif

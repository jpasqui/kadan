#include "P4KadanImport.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

// Changing the containers or retoc invalidates the conversion cache. When a converted file is still in use the
// delete is partial: the new identity must not be stamped on the old files, which would then pass as current.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanConversionCacheInvalidation, "P4MapKit.Kadan.ConversionCacheInvalidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4KadanConversionCacheInvalidation::RunTest(const FString&)
{
    const FString Work = FPaths::ConvertRelativePathToFull(
        FPaths::ProjectSavedDir() / TEXT("P4MapKit/Tests/ConversionCacheInvalidation"));
    IFileManager::Get().DeleteDirectory(*Work, false, true);
    ON_SCOPE_EXIT
    {
        IFileManager::Get().DeleteDirectory(*Work, false, true);
    };
    FP4KadanImportSettings Settings;
    Settings.GamePaks = Work / TEXT("paks");
    Settings.Retoc = Work / TEXT("retoc-a.exe");
    Settings.ConvertedCache = Work / TEXT("cache");
    if (!TestTrue(TEXT("The stand-in inputs are written"),
            IFileManager::Get().MakeDirectory(*Settings.GamePaks, true) &&
                FFileHelper::SaveStringToFile(TEXT("a"), *Settings.Retoc) &&
                FFileHelper::SaveStringToFile(TEXT("b"), *(Work / TEXT("retoc-b.exe")))))
        return false;
    if (!TestEqual(
            TEXT("The first identity makes the cache"), P4KadanConversionCache(Settings), Settings.ConvertedCache))
        return false;
    const FString Old = Settings.ConvertedCache / TEXT("Game/Old.uasset");
    if (!TestTrue(TEXT("A converted package is cached"), FFileHelper::SaveStringToFile(TEXT("old"), *Old)))
        return false;
    FP4KadanImportSettings Other = Settings;
    Other.Retoc = Work / TEXT("retoc-b.exe");
    {
        // The old file is open, as an import still reading it holds it: Windows refuses to delete it.
        TUniquePtr<FArchive> Busy(IFileManager::Get().CreateFileReader(*Old));
        TestTrue(TEXT("The old file is held open"), Busy.IsValid());
        TestTrue(TEXT("A partial invalidation gives no cache"), P4KadanConversionCache(Other).IsEmpty());
        FString Cache;
        FString Error;
        TestFalse(TEXT("The surviving old files do not pass as the other identity's cache"),
            P4KadanRetainedConversionCacheCurrent(Other, Cache, Error));
    }
    TestEqual(
        TEXT("Once the old file is free, invalidation completes"), P4KadanConversionCache(Other), Other.ConvertedCache);
    TestFalse(TEXT("No old file is left in the new cache"), IFileManager::Get().FileExists(*Old));
    return !HasAnyErrors();
}

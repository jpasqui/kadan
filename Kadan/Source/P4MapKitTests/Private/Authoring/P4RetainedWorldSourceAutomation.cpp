#if P4_MAPKIT_TESTS
#include "P4KadanImport.h"
#include "P4KadanTestSeams.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4RetainedWorldSourceTest, "P4MapKit.Kadan.ResumeWalksRetainedWorldSources",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4RetainedWorldSourceTest::RunTest(const FString&)
{
    const FString Header = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETAINED_WORLD_HEADER"));
    if (Header.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. Select the owned retained Area001_P source header."));
        return true;
    }
    const auto* Seams = P4KadanTestSeams();
    if (!TestTrue(TEXT("The acquisition-veto diagnostic is available"), Seams && Seams->BeginAcquisitionVeto))
        return false;
    TArray<TSharedPtr<FP4KadanSourceAsset>> Sources;
    TArray<FString> Problems;
    if (!TestTrue(
            TEXT("Actual retained source headers scan"), P4KadanScanGame(FPaths::GetPath(Header), Sources, Problems)))
        return false;
    TSharedPtr<FP4KadanSourceAsset> Root;
    for (const auto& Source : Sources)
        if (FPaths::IsSamePath(Source->Header, Header))
            Root = Source;
    const FString Child = TEXT("/Game/Carrot4/Maps/Main/Area/Area001/Sublevels/Area001_GameArea");
    if (!TestTrue(TEXT("The root's original source explicitly names the retained child level"),
            Root.IsValid() &&
                Root->Dependencies.ContainsByPredicate(
                    [&](const FString& Name) { return FName(*Name) == FName(*Child); }) &&
                FPackageName::DoesPackageExist(Child)))
        return false;
    FP4KadanImportSettings Settings;
    Settings.GameFolder = FPaths::GetPath(Header);
    Settings.AllowGameBackedLevels = true;
    Settings.GamePaks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    Settings.Retoc = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETOC"));
    const auto Plan = P4KadanPlanImport(
        TArray<TSharedPtr<FP4KadanSourceAsset>>{Root}, TSet<FString>{Root->Package}, true);
    // Block before conversion or import. The test observes which CURRENT sources a resume
    // requests, without replacing a map, loading actors or fabricating package presence.
    void* Veto = Seams->BeginAcquisitionVeto(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
    if (!TestNotNull(TEXT("The source acquisition guard starts"), Veto))
        return false;
    ON_SCOPE_EXIT
    {
        Seams->EndAcquisitionVeto(Veto);
    };
    const auto Result = P4KadanRunImport(Settings, Plan, true);
    const TSet<FString>* Requested = Seams->AcquisitionVetoPackages(Veto);
    bool AskedChild = false;
    if (Requested)
        for (const FString& Name : *Requested)
            AskedChild |= FName(*Name) == FName(*Child);
    TestTrue(
        TEXT("A resume traverses an already-saved sublevel's source to repair its missing dependencies"), AskedChild);
    TestTrue(TEXT("The diagnostic stopped before acquiring or mutating those sources"),
        !Result.Ok && Seams->AcquisitionVetoDenied(Veto) > 0);
    return !HasAnyErrors();
}
#endif

#if P4_MAPKIT_TESTS
#include "P4KadanTestSeams.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SavedStreamingSnapshotTest, "P4MapKit.Kadan.ReimportSnapshotsSavedStreamingGraph",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4SavedStreamingSnapshotTest::RunTest(const FString&)
{
    const FString Map = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REIMPORT_GRAPH_MAP"));
    if (!Map.EndsWith(TEXT("/Cave027_F00_P")))
    {
        AddWarning(TEXT("P4MapKit test not run. Select the owned saved Cave027 streaming fixture."));
        return true;
    }
    auto& Features = IModularFeatures::Get();
    if (!TestTrue(TEXT("The real saved-level transaction seam exists"),
                  Features.IsModularFeatureAvailable(FP4KadanSavedLevelTestSeams::FeatureName())))
        return false;
    const auto& Seams =
        Features.GetModularFeature<FP4KadanSavedLevelTestSeams>(FP4KadanSavedLevelTestSeams::FeatureName());
    FString Error;
    void* Scope = Seams.Begin(Map, Error);
    if (!TestNotNull(TEXT("The selected existing root starts its transaction: ") + Error, Scope))
        return false;
    ON_SCOPE_EXIT
    {
        Seams.End(Scope);
    };
    const FString Package = TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave027/Cave027_F00/Sublevels/Cave027_F00_Background");
    // An active transaction refuses a previously uncaptured child. This read cannot create
    // a late snapshot after dependency mutations; success proves the root captured it early.
    void* Child = Seams.Begin(Package, Error);
    TestNotNull(TEXT("Root-only selection captured its saved streaming child before any dependency work: ") + Error,
                Child);
    if (Child)
        Seams.End(Child);
    AddInfo(Error);
    return !HasAnyErrors();
}
#endif

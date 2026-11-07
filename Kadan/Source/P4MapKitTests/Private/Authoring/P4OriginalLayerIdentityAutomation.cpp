#if P4_MAPKIT_TESTS
#include "Misc/AutomationTest.h"
#include "P4OriginalLayerIdentity.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalLayerBaselineIdentity,
                                 "P4MapKit.NativeImport.OriginalLayerBaselineIdentity",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FP4OriginalLayerBaselineIdentity::RunTest(const FString&)
{
    const TArray<FString> Folded = {TEXT("world.stream | worldasset = source")};
    const TArray<FString> Legacy = {TEXT("World.Stream | worldasset = source")};
    TestTrue(TEXT("Folded cook names match Unreal object identity"),
             P4OriginalLayerRecorded(TEXT("World.Stream"), Folded));
    TestTrue(TEXT("Runtime subobject separators match the actual cooked baseline form"),
             P4OriginalLayerRecorded(TEXT("World:Stream"), Folded));
    TestTrue(TEXT("Legacy names retain the same identity rule"),
             P4OriginalLayerRecorded(TEXT("world.stream"), Legacy));
    TestFalse(TEXT("An unrecorded creator stream stays outside the original presentation"),
              P4OriginalLayerRecorded(TEXT("World.Added"), Folded));
    TestFalse(TEXT("A shared prefix is not the same streaming object"),
              P4OriginalLayerRecorded(TEXT("World.Strea"), Folded));
    TestFalse(TEXT("An empty baseline cannot admit a stream"),
              P4OriginalLayerRecorded(TEXT("World.Stream"), {}));
    return !HasAnyErrors();
}
#endif

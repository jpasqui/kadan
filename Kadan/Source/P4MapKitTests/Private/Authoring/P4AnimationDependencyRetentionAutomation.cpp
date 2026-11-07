#if P4_MAPKIT_TESTS
#include "P4AnimationDependencyRetention.h"
#include "P4KadanImport.h"
#include "P4OriginalRecord.h"
#include "Animation/AnimSequence.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4AnimationDependencyRetentionTest, "P4MapKit.Animation.UnchangedMapDependency",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4AnimationDependencyRetentionTest::RunTest(const FString&)
{
    const FString Folder = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ANIMATION_RETENTION_SOURCE"));
    if (Folder.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. Select the owned actual animation source."));
        return true;
    }
    FP4KadanSourceAsset Source;
    Source.Package = TEXT("/Game/Carrot4/Characters/Player/Animation/AS_Pikmin_Damage");
    Source.Kind = TEXT("AnimSequence");
    Source.Header = Folder / TEXT("Carrot4/Content/Carrot4/Characters/Player/Animation/AS_Pikmin_Damage.uasset");
    Source.Body = FPaths::ChangeExtension(Source.Header, TEXT("uexp"));
    auto* Sequence = LoadObject<UAnimSequence>(nullptr, *(Source.Package + TEXT(".AS_Pikmin_Damage")));
    if (!TestNotNull(TEXT("Actual shared animation opens"), Sequence))
        return false;
    const bool Dirty = Sequence->GetOutermost()->IsDirty();
    const float Rate = Sequence->RateScale;
    ON_SCOPE_EXIT
    {
        Sequence->RateScale = Rate;
        Sequence->GetOutermost()->SetDirtyFlag(Dirty);
    };
    auto Root = MakeShared<FP4KadanSourceAsset>();
    Root->Package = TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave027/Cave027_F00/Cave027_F00_P");
    Root->Kind = TEXT("World");
    FP4KadanImportPlan Plan;
    Plan.RequestedPackages.Add(Root->Package);
    Plan.Scanned = {Root, MakeShared<FP4KadanSourceAsset>(Source)};
    FP4OriginalRecord Record;
    FString Why;
    P4ReadOriginalRecord(Sequence, Record, Why);
    AddInfo(FString::Printf(TEXT("Actual record v%d kind=%s schema=%s reference-only=%d fidelity=%s dirty=%d"),
                            Record.Version, *Record.Kind, *Record.ComparisonSchema, Record.ReferenceOnly,
                            *Record.Fidelity, Dirty));
    UObject* Existing = nullptr;
    FString Error;
    const double Began = FPlatformTime::Seconds();
    if (!TestTrue(TEXT("An identical verified animation dependency is retained without replacement"),
                  P4RetainUnchangedAnimationDependency(Plan, Source, Existing, Error) && Existing == Sequence))
    {
        AddError(Error);
        return false;
    }
    AddInfo(FString::Printf(TEXT("Actual dependency source and native proof took %.3fs"),
                            FPlatformTime::Seconds() - Began));
    Sequence->GetOutermost()->SetDirtyFlag(true);
    TestFalse(TEXT("Unsaved creator work takes normal preservation path"),
              P4RetainUnchangedAnimationDependency(Plan, Source, Existing, Error));
    Sequence->GetOutermost()->SetDirtyFlag(false);
    Sequence->RateScale = Rate + .25f;
    TestFalse(TEXT("Changed typed animation settings cannot claim identical source content"),
              P4RetainUnchangedAnimationDependency(Plan, Source, Existing, Error));
    Sequence->RateScale = Rate;
    auto Requested = Plan;
    Requested.RequestedPackages.Add(Source.Package);
    TestFalse(TEXT("An explicitly requested animation is still reimported"),
              P4RetainUnchangedAnimationDependency(Requested, Source, Existing, Error));
    auto Missing = Source;
    Missing.Header += TEXT(".absent");
    TestFalse(TEXT("Missing incoming source cannot retain a historical match"),
              P4RetainUnchangedAnimationDependency(Plan, Missing, Existing, Error));
    TestTrue(TEXT("All controls leave the actual source animation reusable"),
             P4RetainUnchangedAnimationDependency(Plan, Source, Existing, Error) && Existing == Sequence);
    return !HasAnyErrors();
}
#endif

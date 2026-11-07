#include "P4StrictRequiredClasses.h"
#include "P4KadanImportSources.h"
#include "P4KadanImport.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureCube.h"
#include "HAL/PlatformMisc.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UObject/Class.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4StrictRequiredClassesTest, "P4MapKit.Kadan.StrictRequiredClassPostflight",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4StrictRequiredClassesTest::RunTest(const FString&)
{
    // Reuse the same retained bounded game pair as CurrentClassSourceBinding.
    // Comparison is a policy input here; these controls do not claim that a texture
    // class is the original Blueprint or that synthetic facts prove native recovery.
    const FString Package = TEXT("/Game/Carrot4/Placeables/Teki/GBaby");
    const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    auto Asset = MakeShared<FP4KadanSourceAsset>();
    Asset->Package = Package;
    Asset->Header = Root /
        TEXT("host/Saved/P4MapKit/FullNativeCave000/game/Carrot4/Content/Carrot4/Placeables/Teki/GBaby.uasset");
    Asset->Body = FPaths::ChangeExtension(Asset->Header, TEXT("uexp"));
    FP4KadanImportSourceScope SourceScope({Asset});
    FP4KadanSourceBinding Initial;
    FString Error;
    if (!TestTrue(TEXT("Policy controls reuse a validated actual source pair"),
            P4KadanReadBoundSource(Package, Initial, Error)))
    {
        AddError(Error);
        return false;
    }
    UClass* FirstClass = UTexture2D::StaticClass();
    FirstClass->GetDefaultObject();
    UClass* OtherClass = UTextureCube::StaticClass();
    OtherClass->GetDefaultObject();
    FP4StrictWorldClassInventory First;
    First.WorldPackage = TEXT("/Game/__P4Postflight/First");
    First.Known = true;
    First.WorldSourceDigest = FString::ChrN(64, TEXT('b'));
    First.RequiredClassPackages = {Package, Package};
    auto Resumed = First;
    Resumed.WorldPackage = TEXT("/Game/__P4Postflight/Resumed");
    Resumed.Resumed = true;
    const TArray<FString> Selected = {First.WorldPackage, Resumed.WorldPackage};
    int32 Reads = 0, Comparisons = 0, Mode = 0;
    auto Read = [&](const FString& Name, FP4KadanSourceBinding& Source, UClass*& Class, FString& Reason)
    {
        ++Reads;
        if (!P4KadanReadBoundSource(Name, Source, Reason))
            return false;
        if (Mode == 3)
        {
            Reason = TEXT("source-parent-class-identity-differs");
            return false;
        }
        Class = (Mode == 2 && Reads >= 3) ? OtherClass : FirstClass;
        // Byte mutation is deliberately after the real read: the helper's mandatory
        // current-source check, not a fake source-validator callback, must catch it.
        if (Mode == 1 && Reads >= 3)
            Source.Body[0] ^= 1;
        if (Mode == 4 && Reads >= 4)
            Source.Body[0] ^= 1;
        return true;
    };
    auto Compare = [&](const FP4KadanSourceBinding& Source)
    {
        ++Comparisons;
        FP4OriginalClassComparisonResult Result;
        Result.Package = Source.Package;
        Result.PackageKnown = Result.ValuesKnown = Result.TemplatesKnown = Result.ComparedKnown = true;
        Result.DefaultsReadable = Result.Same = true;
        Result.Compared = 127;
        Result.GameDefaultFactsDigest = Result.DefaultFactsDigest = FString::ChrN(64, TEXT('a'));
        return Result;
    };
    using Observation = EP4OriginalClassDefaultObservation;
    auto Proof = P4EvaluateStrictRequiredClasses(
        Selected, {First, Resumed}, Read, Compare, Observation::NativeStoredDefaults);
    TestTrue(TEXT("Shared class proof qualifies both current and resumed inventories"), Proof.Qualified);
    TestEqual(TEXT("Union deduplicates within and across worlds"), Proof.Classes.Num(), 1);
    TestEqual(TEXT("Shared class is compared only once"), Comparisons, 1);
    TestEqual(TEXT("Source is checked at observation and both world consumptions"), Reads, 4);
    TestTrue(TEXT("Resumed world has its own required-class proof"),
        Proof.Worlds.Num() == 2 && Proof.Worlds[1].Resumed && Proof.Worlds[1].Qualified);

    Reads = Comparisons = 0;
    Proof = P4EvaluateStrictRequiredClasses(Selected, {First}, Read, Compare, Observation::NativeStoredDefaults);
    TestFalse(TEXT("Resume cannot omit its required-class inventory"), Proof.Qualified);
    auto Unknown = Resumed;
    Unknown.Known = false;
    Unknown.RequiredClassPackages.Reset();
    Proof = P4EvaluateStrictRequiredClasses(
        Selected, {First, Unknown}, Read, Compare, Observation::NativeStoredDefaults);
    TestFalse(TEXT("An empty unknown resume inventory cannot qualify"), Proof.Qualified);
    auto NoClasses = Resumed;
    NoClasses.RequiredClassPackages.Reset();
    Proof = P4EvaluateStrictRequiredClasses(
        {Resumed.WorldPackage}, {NoClasses}, Read, Compare, Observation::NativeStoredDefaults);
    TestTrue(TEXT("A known source-bound world can genuinely require no Blueprint classes"), Proof.Qualified);

    Mode = 1;
    Reads = Comparisons = 0;
    Proof = P4EvaluateStrictRequiredClasses(
        Selected, {First, Resumed}, Read, Compare, Observation::NativeStoredDefaults);
    TestFalse(TEXT("Source mutation at consumption refuses an earlier successful class comparison"), Proof.Qualified);
    TestTrue(TEXT("Shared source failure invalidates every world receipt"),
        Proof.Worlds.Num() == 2 && !Proof.Worlds[0].Qualified && !Proof.Worlds[1].Qualified);
    TestEqual(TEXT("Mutation never triggers recook or substitute source selection"), Comparisons, 1);

    Mode = 4;
    Reads = Comparisons = 0;
    Proof = P4EvaluateStrictRequiredClasses(
        Selected, {First, Resumed}, Read, Compare, Observation::NativeStoredDefaults);
    TestFalse(TEXT("Mutation only at the resumed world's consumption still refuses"), Proof.Qualified);
    TestTrue(TEXT("A later failed shared proof invalidates the earlier world's receipt"),
        Proof.Worlds.Num() == 2 && !Proof.Worlds[0].Qualified && !Proof.Worlds[1].Qualified);

    Mode = 2;
    Reads = Comparisons = 0;
    Proof = P4EvaluateStrictRequiredClasses(
        Selected, {First, Resumed}, Read, Compare, Observation::NativeStoredDefaults);
    TestFalse(TEXT("Changed native class and CDO identities refuse cached proof"), Proof.Qualified);
    TestTrue(TEXT("Identity refusal is explicit"),
        Proof.Classes.Num() == 1 && Proof.Classes[0].Reason.Contains(TEXT("identity changed")));

    Mode = 3;
    Reads = Comparisons = 0;
    Proof = P4EvaluateStrictRequiredClasses(
        Selected, {First, Resumed}, Read, Compare, Observation::NativeStoredDefaults);
    TestFalse(TEXT("Authoritative source-parent refusal cannot be replaced by loaded ancestry"), Proof.Qualified);
    TestEqual(TEXT("An ancestry refusal never reaches the native cook"), Comparisons, 0);

    Mode = 0;
    Reads = Comparisons = 0;
    Proof = P4EvaluateStrictRequiredClasses(
        Selected, {First, Resumed}, Read, Compare, Observation::TemporarilyBorrowedForCook);
    TestFalse(TEXT("Temporarily borrowed cook defaults cannot qualify persistent class storage"), Proof.Qualified);
    TestEqual(TEXT("Temporary observation is refused without cooking"), Comparisons, 0);
    return !HasAnyErrors();
}
#endif

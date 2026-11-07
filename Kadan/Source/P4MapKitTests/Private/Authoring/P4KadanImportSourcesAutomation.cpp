#include "P4KadanImportSources.h"
#include "P4KadanImport.h"
#include "HAL/PlatformMisc.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanImportSourcesTest, "P4MapKit.Kadan.CurrentClassSourceBinding",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4KadanImportSourcesTest::RunTest(const FString&)
{
    const FString Package = TEXT("/Game/Carrot4/Placeables/Teki/GBaby");
    const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    auto Source = MakeShared<FP4KadanSourceAsset>();
    Source->Package = Package;
    Source->Header = Root /
        TEXT("host/Saved/P4MapKit/FullNativeCave000/game/Carrot4/Content/Carrot4/Placeables/Teki/GBaby.uasset");
    Source->Body = FPaths::ChangeExtension(Source->Header, TEXT("uexp"));
    FP4KadanSourceBinding Bound;
    FString Error;
    TestFalse(TEXT("No arbitrary cache lookup without a scope"), P4KadanHasBoundSource(Package));
    {
        FP4KadanImportSourceScope Outer({Source});
        if (!TestTrue(TEXT("Exact retained source is bound"), P4KadanReadBoundSource(Package, Bound, Error)))
        {
            AddError(Error);
            return false;
        }
        TestEqual(TEXT("Read canonical package is preserved"), Bound.Package, Package);
        TestTrue(TEXT("Exact source snapshot is still current"), P4KadanSourceBindingCurrent(Bound, Error));
        FP4KadanSourceBinding Changed = Bound;
        Changed.Body[0] ^= 1;
        TestFalse(TEXT("Mutated in-memory comparison input is refused"), P4KadanSourceBindingCurrent(Changed, Error));
        Changed = Bound;
        Changed.BodyPath += TEXT(".missing");
        TestFalse(TEXT("Missing registered source cannot pass recheck"), P4KadanSourceBindingCurrent(Changed, Error));
        {
            auto Wrong = MakeShared<FP4KadanSourceAsset>(*Source);
            Wrong->Package = TEXT("/Game/__P4SourceBinding/WrongIdentity");
            FP4KadanImportSourceScope Nested({Wrong});
            TestFalse(TEXT("Nested scope does not borrow the previous registry"), P4KadanHasBoundSource(Package));
            TestTrue(TEXT("Wrong identity stays bound and cannot fall back"), P4KadanHasBoundSource(Wrong->Package));
            TestFalse(TEXT("Valid bytes of another canonical package are refused"),
                P4KadanReadBoundSource(Wrong->Package, Changed, Error));
            TestTrue(TEXT("Failed read exposes no bytes or digest"),
                Changed.Header.Num() == 0 && Changed.Body.Num() == 0 && Changed.SourceDigest.IsEmpty());
        }
        TestTrue(TEXT("Prior registry restored after nested scope"), P4KadanReadBoundSource(Package, Changed, Error));
        Outer.AddSources({Source});
        TestTrue(TEXT("Repeated same registered source is accepted"), P4KadanReadBoundSource(Package, Changed, Error));
        auto Conflict = MakeShared<FP4KadanSourceAsset>(*Source);
        Conflict->Header += TEXT(".different");
        Outer.AddSources({Conflict});
        TestTrue(TEXT("Ambiguous package remains bound"), P4KadanHasBoundSource(Package));
        TestFalse(TEXT("Conflicting source paths refuse without selecting a winner"),
            P4KadanReadBoundSource(Package, Changed, Error));
    }
    TestFalse(TEXT("Registry released at end of run"), P4KadanHasBoundSource(Package));
    auto Missing = MakeShared<FP4KadanSourceAsset>();
    Missing->Package = Package;
    {
        FP4KadanImportSourceScope Scope({Missing});
        TestTrue(TEXT("Missing source pair is explicitly bound"), P4KadanHasBoundSource(Package));
        TestFalse(
            TEXT("Missing pair does not acquire or guess a source"), P4KadanReadBoundSource(Package, Bound, Error));
        TestFalse(
            TEXT("Traversal is never a source identity"), P4KadanReadBoundSource(TEXT("/Game/../Other"), Bound, Error));
    }
    return true;
}
#endif

#if P4_MAPKIT_TESTS
#include "P4ParticleReference.h"
#include "P4OriginalRecord.h"
#include "AssetRegistryModule.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Particles/ParticleSystem.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4GameOnlyParticleReferenceTest, "P4MapKit.Particles.GameOnlyReference",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4GameOnlyParticleReferenceTest::RunTest(const FString&)
{
    FP4OriginalAssetImportResult Refused;
    const FString Name = TEXT("PS_Reference_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    Refused.Package = TEXT("/Game/Mods/ParticleReferenceTest/") + Name;
    Refused.AssetPath = Refused.Package + TEXT(".") + Name;
    Refused.Class = TEXT("/Script/Engine.ParticleSystem");
    Refused.AssetIdentityKnown = true;
    Refused.ObjectsKnown = true;
    Refused.Error = EP4OriginalAssetImportError::ClassUnavailable;
    Refused.ErrorDetail = TEXT(
        "asset-import-class-not-in-the-project: /Script/Engine.ParticleModuleLocationPrimitiveC4Sphere");
    auto Other = Refused;
    Other.ErrorDetail = TEXT("asset-import-class-not-in-the-project: /Script/Engine.UnrelatedModule");
    TestFalse(TEXT("An arbitrary missing class cannot become a particle reference"),
        P4KeepUnsupportedParticleReference(Other, false).IsValid());
    const auto Result = P4KeepUnsupportedParticleReference(Refused, false);
    if (!TestTrue(TEXT("The known game-only effect gets a loadable typed reference"),
            Result.IsValid() && Result.Asset && Result.Asset->IsA<UParticleSystem>()))
    {
        AddError(Result.ErrorDetail);
        return false;
    }
    ON_SCOPE_EXIT
    {
        FAssetRegistryModule::AssetDeleted(Result.Asset);
        Result.Asset->ClearFlags(RF_Public | RF_Standalone);
        Result.Asset->MarkPendingKill();
        Result.Asset->GetOutermost()->SetDirtyFlag(false);
    };
    TestTrue(TEXT("Dependent references resolve to the protected asset"),
        FindObject<UParticleSystem>(nullptr, *Refused.AssetPath) == Result.Asset);
    const auto Status = P4QueryOriginalAsset(Result.Asset);
    TestTrue(TEXT("The reference is explicitly read-only game content"),
        Status.IsOriginal() && Status.Record.ReadOnly && Status.Record.ReferenceOnly);
    TestTrue(TEXT("No recovered effect or exact comparison is claimed"),
        Status.Record.Fidelity == TEXT("identity-only") && !Result.ComparedKnown && !Result.DifferencesKnown);
    TestTrue(TEXT("The missing payload is named"),
        Result.ValuesNotHeld.Num() == 1 && Result.ValuesNotHeld[0].Contains(TEXT("game-only module")));
    return !HasAnyErrors();
}
#endif

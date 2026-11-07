#if WITH_DEV_AUTOMATION_TESTS
#include "P4KadanAcquisitionVeto.h"
#include "P4KadanTestSeams.h"
#include "P4KadanImport.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanAcquisitionVetoTest, "P4MapKit.Kadan.ScopedSourceAcquisitionVeto",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4KadanAcquisitionVetoTest::RunTest(const FString&)
{
    P4_REQUIRE_KADAN_TEST_SEAMS();
    const TArray<FString> Missing{TEXT("/Game/P4AcquisitionVetoProbe/Missing")};
    FString Error;
    TestTrue(
        TEXT("Normal acquisition is unchanged without a dev scope"), P4KadanSourceAcquisitionAllowed(Missing, Error));
    {
        FP4KadanScopedAcquisitionVeto Foreign(FPaths::ProjectDir() / TEXT("a-different-project"));
        TestFalse(TEXT("A foreign project cannot install a veto"), Foreign.IsActive());
        TestTrue(TEXT("A foreign scope does not affect normal acquisition"),
            P4KadanSourceAcquisitionAllowed(Missing, Error));
    }
    {
        FP4KadanScopedAcquisitionVeto Outer(FPaths::ProjectDir());
        TestTrue(TEXT("The exact game-thread project owns its scope"), Outer.IsActive());
        TestTrue(TEXT("No acquisition batch remains permitted"), P4KadanSourceAcquisitionAllowed({}, Error));
        TestFalse(TEXT("A real missing batch is denied"), P4KadanSourceAcquisitionAllowed(Missing, Error));
        TestEqual(TEXT("The current scope records the denied batch"), Outer.DeniedBatches(), 1);
        {
            FP4KadanScopedAcquisitionVeto Inner(FPaths::ProjectDir());
            TestFalse(TEXT("Nested scope denies independently"), P4KadanSourceAcquisitionAllowed(Missing, Error));
            TestEqual(TEXT("Nested denial belongs to the active inner scope"), Inner.DeniedBatches(), 1);
            TestEqual(TEXT("Nested scope did not alter its outer evidence"), Outer.DeniedBatches(), 1);
        }
        TestFalse(
            TEXT("Destroying inner scope restores the outer veto"), P4KadanSourceAcquisitionAllowed(Missing, Error));
        TestEqual(TEXT("Restored outer scope receives the next batch"), Outer.DeniedBatches(), 2);
    }
    TestTrue(TEXT("Destroying the owner restores normal acquisition"), P4KadanSourceAcquisitionAllowed(Missing, Error));

    // Exercise the actual BringIn boundary, not a local imitation of converter
    // control flow. These synthetic source names are never decoded or published:
    // the veto must return before container indexing, staging, or any asset work.
    const FString Root = TEXT("/Game/P4AcquisitionVetoProbe/Root");
    if (!TestTrue(TEXT("Synthetic test destinations are absent"),
            !FPackageName::DoesPackageExist(Root) && !FindPackage(nullptr, *Root) &&
                !FPackageName::DoesPackageExist(Missing[0]) && !FindPackage(nullptr, *Missing[0])))
        return false;
    auto Source = MakeShared<FP4KadanSourceAsset>();
    Source->Package = Root;
    Source->Kind = TEXT("Texture2D");
    Source->Dependencies = Missing;
    FP4KadanImportPlan Plan;
    Plan.Scanned.Add(Source);
    Plan.Assets.Add(Source);
    Plan.Roots = 1;
    FP4KadanImportSettings Settings;
    Settings.GamePaks = FPaths::ProjectSavedDir();
    Settings.Retoc = FPaths::ProjectSavedDir() / TEXT("P4MapKit/AcquisitionVeto/ThisConverterMustNotBeInvoked.exe");
    Settings.ConvertedCache = FPaths::ProjectSavedDir() / TEXT("P4MapKit/AcquisitionVeto/UncreatedCache");
    if (!TestFalse(TEXT("No test cache exists before the blocked actual import"),
            IFileManager::Get().DirectoryExists(*Settings.ConvertedCache)))
        return false;
    FP4KadanImportResult Result;
    {
        FP4KadanScopedAcquisitionVeto Hold(FPaths::ProjectDir());
        Result = P4KadanRunImport(Settings, Plan, true);
        TestEqual(TEXT("Actual BringIn reaches precisely one veto before any converter"), Hold.DeniedBatches(), 1);
        TestTrue(TEXT("The veto records the actual missing dependency identity"),
            Hold.DeniedPackages().Contains(Missing[0]));
    }
    TestFalse(TEXT("Blocked acquisition makes this import fail explicitly"), Result.Ok);
    TestFalse(TEXT("The dev veto does not masquerade as user cancellation"), Result.Cancelled);
    TestTrue(TEXT("No asset is imported or published"),
        Result.Imported.Num() == 0 && !FPackageName::DoesPackageExist(Root) && !FindPackage(nullptr, *Root));
    TestFalse(
        TEXT("The converter cache was never created"), IFileManager::Get().DirectoryExists(*Settings.ConvertedCache));
    TSharedPtr<FJsonObject> Report;
    const TSharedPtr<FJsonObject>* Steps = nullptr;
    TestTrue(TEXT("Actual importer records refusal before its container-index operation"),
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Result.Report), Report) && Report.IsValid() &&
            Report->TryGetObjectField(TEXT("steps"), Steps) && !(*Steps)->HasField(TEXT("container index")) &&
            Result.Problems.Num() == 1 &&
            Result.Problems[0].Contains(TEXT("dev fixture forbids unexpected source acquisition")));
    TestTrue(TEXT("Actual-import scope is gone afterward"), P4KadanSourceAcquisitionAllowed(Missing, Error));
    return !HasAnyErrors();
}
#endif

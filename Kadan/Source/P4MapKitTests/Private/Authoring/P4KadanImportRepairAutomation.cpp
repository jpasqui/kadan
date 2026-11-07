#include "Misc/AutomationTest.h"
#include "P4KadanImport.h"
#include "P4KadanImportDependencies.h"
#include "P4KadanImportRepair.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKadanImportOutcomes, "P4MapKit.Kadan.ImportOutcomes",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKadanImportOutcomes::RunTest(const FString&)
{
    FP4KadanImportPlan Plan;
    auto Source = MakeShared<FP4KadanSourceAsset>();
    Source->Package = TEXT("/Game/Fixture/Root");
    Source->Kind = TEXT("StaticMesh");
    Source->Dependencies = {TEXT("/Game/Fixture/Existing"), TEXT("/Game/Fixture/Shared"),
                            TEXT("/Game/Fixture/shared"), TEXT("/Game/Fixture/Soft"), Source->Package};
    Source->SoftDependencies = {TEXT("/Game/Fixture/Soft")};
    Plan.Assets.Add(Source);
    TArray<FString> Required;
    FString Error;
    const TSet<FString> Selected = {Source->Package};
    TestTrue(TEXT("Required references use the source execution edges"),
             P4KadanImportKnownRequiredReferences(Plan, Selected, Required, Error));
    TestEqual(TEXT("Existing references remain visible without an acquired source row"), Required.Num(), 2);
    TestTrue(TEXT("The root and duplicate-case identities are excluded"),
             Required.Contains(TEXT("/Game/Fixture/Existing")) &&
                 Required.Contains(TEXT("/Game/Fixture/Shared")));
    Source->Kind = TEXT("World");
    TestTrue(TEXT("World references use the same soft-reference rule as the importer"),
             P4KadanImportKnownRequiredReferences(Plan, Selected, Required, Error) && Required.Num() == 3);
    FP4KadanImportResult Result;
    Result.Imported.Add(TEXT("/Game/Fixture/Refreshed"), TEXT("Texture2D"));
    Result.Imported.Add(TEXT("/Game/Fixture/Dependency"), TEXT("MaterialParameterCollection"));
    Result.Report = TEXT("{\"assets\":{") TEXT("\"/Game/Fixture/Refreshed\":{\"valid\":true},")
        TEXT("\"/Game/Fixture/Creator\":{\"valid\":true,\"state\":\"existing creator content preserved\"},")
            TEXT("\"/Game/Fixture/Dependency\":{\"valid\":true,\"state\":\"existing unchanged source "
                 "dependency retained\"},")
                TEXT("\"/Game/Fixture/Dirty\":{\"valid\":false,\"error\":\"Exact backend diagnostic\"}")
                    TEXT("}}");
    const auto Rows = P4KadanImportOutcomes(Result);
    TestEqual(TEXT("Every attempted package has an outcome"), Rows.Num(), 4);
    TestTrue(TEXT("Refreshed packages are labeled from the actual import result"),
             Rows.Contains(TEXT("Imported · /Game/Fixture/Refreshed")));
    TestTrue(TEXT("Creator changes remain distinct from refreshed imports"),
             Rows.Contains(TEXT("Kept · /Game/Fixture/Creator")));
    TestTrue(TEXT("Retained dependencies are not described as reconstructed"),
             Rows.Contains(TEXT("Kept · /Game/Fixture/Dependency")));
    TestTrue(TEXT("Refusals retain their exact diagnostic"),
             Rows.Contains(TEXT("Refused · /Game/Fixture/Dirty\nExact backend diagnostic")));
    Result.Report = TEXT("not json");
    TestEqual(TEXT("Missing report data does not invent outcomes"), P4KadanImportOutcomes(Result).Num(), 0);
    return !HasAnyErrors();
}
#endif

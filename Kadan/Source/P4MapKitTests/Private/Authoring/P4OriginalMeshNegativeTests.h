#pragma once
// Native negative test declarations; never production import behavior.
#include "P4OriginalMeshStaging.h"
class FAutomationTestBase;
#if WITH_DEV_AUTOMATION_TESTS && P4_MAPKIT_TESTS
namespace P4OriginalMeshNegativeTests
{
FP4OriginalMeshSource OriginalMeshTestSentinel(const FP4OriginalMeshSource& Fixture);
bool OriginalMeshTestUnchanged(const FP4OriginalMeshSource& After, const FP4OriginalMeshSource& Before);
}
bool P4TestOriginalMeshCancellation(FAutomationTestBase& Test, const FString& HeaderFile, const FString& Export);
bool P4TestOriginalMeshMaterialNegatives(FAutomationTestBase& Test,
    const FString& OriginalCylinderHeader, const FString& OwnedPrivateScratchDirectory);
#endif

#include "P4KadanTestSeams.h"
#if P4_KADAN_TEST_SEAMS
#include "P4OriginalLevelImport.h"
#include "P4OriginalClassComparison.h"
#include "P4KadanAcquisitionVeto.h"
#include "P4GameFunctionStandIns.h"
#include "P4SceneTeardown.h"

// The development implementations, each beside the production code it exercises.
namespace P4LevelContent
{
bool P4UnheldClassValuesForTest(const UStruct*, const FString&, FString&, int32&);
bool P4UnheldClassValueDetailsForTest(const UStruct*, const FString&, FString&, int32&, TArray<FP4OriginalClassValueNotHeld>&);
}
namespace P4Kadan { void ForgetGameActorNamesForTest(UWorld*); }
bool P4TerrainTrackingLinesForTest(const FString&, const FString&, TArray<FString>&, TArray<FString>&);
FString P4LevelClassLineForTest(const FString&, bool);
bool P4ClassComponentTailForTest(UClass*, const TArray<uint8>&, int32&);
bool P4ClassLightingTailForTest(UClass*, const TArray<uint8>&, bool, TArray<FString>&, TArray<FString>&);
TArray<FString> P4ClassSourceAdmissionForTest(const FString&, const FString&, TSet<FName>&, int32&);
FP4OriginalLevelImportResult P4RepairRetainedPlayerDefaultsForTest(const FString&, const FString&, const FString&, const FString&);
bool P4ApplyDeclaredLevelDefaultsForTest(UObject*, UObject*, const FString&, bool, FString&);
bool P4OriginalLevelStorageSubobjectMatchesForTest(UObject*, UObject*, UClass*, const FString&, uint32, UObject*);
FP4OriginalLevelImportResult P4ProbeOriginalGameBackedFamiliesForTest(const FString&, const FString&);
void P4DiscardOriginalFamilyProbeForTest(UWorld*);
bool P4MaterialFamilyStageControlsForTest(TArray<FString>&);
bool P4MaterialCollectionValueControl(UMaterialParameterCollection*, const FP4MaterialUsedResourceRequest&, const FP4KadanSourceBinding&, FString&);
bool P4MaterialCollectionLayoutControl(const FShaderParametersMetadata&, int32, int32, FString&);
bool P4MaterialCollectionLifecycleControl(UMaterialParameterCollection*, FString&);
void* P4BeginKadanAcquisitionVeto(const FString&);
void P4EndKadanAcquisitionVeto(void*);
bool P4KadanAcquisitionVetoActive(const void*);
int32 P4KadanAcquisitionVetoDenied(const void*);
const TSet<FString>* P4KadanAcquisitionVetoPackages(const void*);
void* P4BeginWrappedBlueprintSource(const FString&, const FString&);
void P4EndWrappedBlueprintSource(void*);
int32 P4WrappedBlueprintSourceFailures(const void*);

void* P4BeginSavedLevelReimportForTest(const FString&, FString&);
void P4EndSavedLevelReimportForTest(void*);

namespace
{
FP4KadanSavedLevelTestSeams SavedLevelSeams;
FP4KadanSceneTeardownTestSeams SceneTeardownSeams;
FP4KadanTestSeams Seams;
bool Registered = false;
}

void P4RegisterKadanTestSeams()
{
    if (Registered) return;
    Seams.UnheldClassValues = &P4LevelContent::P4UnheldClassValuesForTest;
    Seams.UnheldClassValueDetails = &P4LevelContent::P4UnheldClassValueDetailsForTest;
    Seams.TerrainTrackingLines = &P4TerrainTrackingLinesForTest;
    Seams.ClassComponentTail = &P4ClassComponentTailForTest;
    Seams.ClassLightingTail = &P4ClassLightingTailForTest;
    Seams.ClassSourceAdmission = &P4ClassSourceAdmissionForTest;
    Seams.RepairRetainedPlayerDefaults = &P4RepairRetainedPlayerDefaultsForTest;
    Seams.ApplyDeclaredLevelDefaults = &P4ApplyDeclaredLevelDefaultsForTest;
    Seams.LevelStorageSubobjectMatches = &P4OriginalLevelStorageSubobjectMatchesForTest;
    Seams.ProbeGameBackedFamilies = &P4ProbeOriginalGameBackedFamiliesForTest;
    Seams.DiscardFamilyProbe = &P4DiscardOriginalFamilyProbeForTest;
    Seams.MaterialFamilyStageControls = &P4MaterialFamilyStageControlsForTest;
    Seams.ForgetGameActorNames = &P4Kadan::ForgetGameActorNamesForTest;
    Seams.MaterialCollectionValues = &P4MaterialCollectionValueControl;
    Seams.MaterialCollectionLayout = &P4MaterialCollectionLayoutControl;
    Seams.MaterialCollectionLifecycle = &P4MaterialCollectionLifecycleControl;
    Seams.SourceAcquisitionAllowed = &P4KadanSourceAcquisitionAllowed;
    Seams.BeginAcquisitionVeto = &P4BeginKadanAcquisitionVeto;
    Seams.EndAcquisitionVeto = &P4EndKadanAcquisitionVeto;
    Seams.AcquisitionVetoActive = &P4KadanAcquisitionVetoActive;
    Seams.AcquisitionVetoDenied = &P4KadanAcquisitionVetoDenied;
    Seams.AcquisitionVetoPackages = &P4KadanAcquisitionVetoPackages;
    Seams.BeginWrappedBlueprintSource = &P4BeginWrappedBlueprintSource;
    Seams.EndWrappedBlueprintSource = &P4EndWrappedBlueprintSource;
    Seams.WrappedBlueprintSourceFailures = &P4WrappedBlueprintSourceFailures;
    Seams.StartGameFunctionStandIns = &P4StartGameFunctionStandIns;
    Seams.StopGameFunctionStandIns = &P4StopGameFunctionStandIns;
    Seams.GameFunctionStandIns = &P4GameFunctionStandIns;
    Seams.LevelClassLine = &P4LevelClassLineForTest;
    SavedLevelSeams.Begin = &P4BeginSavedLevelReimportForTest;
    SavedLevelSeams.End = &P4EndSavedLevelReimportForTest;
    SceneTeardownSeams.Active = &P4SceneTeardownGuardActive;
    SceneTeardownSeams.Start = &P4StartSceneTeardownGuard;
    SceneTeardownSeams.Stop = &P4StopSceneTeardownGuard;
    IModularFeatures::Get().RegisterModularFeature(FP4KadanSceneTeardownTestSeams::FeatureName(), &SceneTeardownSeams);
    IModularFeatures::Get().RegisterModularFeature(FP4KadanSavedLevelTestSeams::FeatureName(), &SavedLevelSeams);
    IModularFeatures::Get().RegisterModularFeature(FP4KadanTestSeams::FeatureName(), &Seams);
    Registered = true;
}

void P4UnregisterKadanTestSeams()
{
    if (!Registered) return;
    IModularFeatures::Get().UnregisterModularFeature(FP4KadanSceneTeardownTestSeams::FeatureName(), &SceneTeardownSeams);
    IModularFeatures::Get().UnregisterModularFeature(FP4KadanSavedLevelTestSeams::FeatureName(), &SavedLevelSeams);
    IModularFeatures::Get().UnregisterModularFeature(FP4KadanTestSeams::FeatureName(), &Seams);
    Registered = false;
}
#endif

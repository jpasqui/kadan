// Per-asset in-place imports. No batch transaction or package-file fingerprints.
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
class UBlueprint;
class UClass;
class ULevelSequence;
namespace P4Reimport
{
 bool PreflightPlan(const TSharedPtr<FJsonObject>& Plan, bool bReimport, FString& Why);
 bool BeginRun(FString& Why);
 bool IsRunActive();
 void WithCompilerSavePolicy(const TFunction<void()>& Operation);
 bool BeginPlan(const TSharedPtr<FJsonObject>& Plan, bool bReimport, FString& Why);
 bool ReuseBlueprint(UBlueprint* Blueprint, UClass* Parent, FString& Why);
 void ResetReimportSourceDefaults(UBlueprint* Blueprint);
 bool ReuseSequence(ULevelSequence* Sequence, FString& Why);
 void MarkImported(UObject* Asset);
 // Scoped to an actual source asset owned/mutated by the current native run.
 bool SetReconstructionIncomplete(UObject* Asset, bool Incomplete);
 bool IsFreshBlueprint(UBlueprint* Blueprint);
 // A new declaration or an untouched stand-in admitted before any mutation in this run.
 bool CanInitializeAnimationOwnership(UBlueprint* Blueprint);
 bool HasPreparedAnimation(UBlueprint* Blueprint);
 // The game's value of a property the editor does not hold as it is, kept in the asset's record with a note.
 // GameAsset names the game's asset the value refers to when that asset stays the game's.
 void RetainOriginalValue(UObject* Asset, const TSharedPtr<FJsonObject>& Value, const FString& GameAsset = FString());
 // A package this run imports (a selected Blueprint or sequence, made or filled during the run).
 bool IsRunPackage(const FString& PackageName);
 void RecordStandInCreated(UObject* Asset);
 bool IsFreshStandIn(UObject* Asset);
 P4BLUEPRINTIMPORT_API bool IsUntouchedStandIn(UObject* Asset, FString& Why);
 bool ConfigureStandIn(UObject* Asset, const TFunction<void()>& Configure, FString& Why);
 bool PrepareStandInSave(const FString& PackageName, FString& Why);
 void CancelStandInSave(const FString& PackageName);
 bool FinishStandInPackage(const FString& PackageName, FString& Why);
 bool PrepareImportedRecord(UObject* Asset, const FString& PlanFile, FString& Why);
 bool FinishPackage(const FString& PackageName, const FString& PlanFile, FString& Why);
 // Seal only source animation objects rebuilt and saved by this synchronous import.
 bool SealAnimationPackages(const TArray<FString>& PackageNames, FString& Why);
 // Fresh declarations whose source import failed must not occupy game paths in the next map.
 bool DiscardIncompleteCreatedAssets(FString& Why);
 void EndRun();
}

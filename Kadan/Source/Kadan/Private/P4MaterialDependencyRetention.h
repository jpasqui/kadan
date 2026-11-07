#pragma once
#include "CoreMinimal.h"
struct FP4KadanImportPlan;
struct FP4KadanSourceAsset;
class UObject;

// A complete unchanged collection needed by a material's source references can
// retain its actual object instead of an identical save/reload notifying its
// material referencers. This grants no shader-owner/resource proof. False leaves
// ordinary strict reimport/preservation policy in charge; explicit roots never qualify.
KADAN_API bool P4RetainUnchangedMaterialCollectionDependency(const FP4KadanImportPlan& Plan,
    const FP4KadanSourceAsset& Source,UObject*& Existing,FString& Error);

// Called only after ordinary complete-family read-only Recover. The actual
// family source scope and its current guard must remain alive. This retains
// existing recovered-reference policy, including original render-setting notes.
KADAN_API bool P4RetainUnchangedRecoveredMaterialDependency(const FP4KadanImportPlan& Plan,
    const FP4KadanSourceAsset& Source,const FString& CurrentRecipeMd5,
    const TFunction<bool(FString&)>& SourceCurrent,UObject*& Existing,FString& Error);

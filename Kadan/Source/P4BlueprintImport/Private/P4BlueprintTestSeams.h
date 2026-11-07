#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Features/IModularFeature.h"
#include "Features/IModularFeatures.h"

// Development test seams of the Blueprint import module. Only a build made with
// P4_MAPKIT_TESTS=1 in the environment (dev_build.py --tests) defines P4_BLUEPRINT_TEST_SEAMS and
// registers them when the module starts; release builds set it to 0 and hold none. The separate
// test module finds them at run time, so it also loads beside the shipping DLLs.
#ifndef P4_BLUEPRINT_TEST_SEAMS
#define P4_BLUEPRINT_TEST_SEAMS 0
#endif

struct FP4BlueprintTestSeams : public IModularFeature
{
	static FName FeatureName() { return FName(TEXT("P4BlueprintTestSeams")); }
	// Exercises the actual source-subobject and Apply branches without a later compiler reset.
	FString (*ApplySourceValues)(UObject*, const TSharedPtr<FJsonObject>&) = nullptr;
};

#if defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
inline const FP4BlueprintTestSeams* P4BlueprintTestSeams()
{
	IModularFeatures& Features = IModularFeatures::Get();
	return Features.IsModularFeatureAvailable(FP4BlueprintTestSeams::FeatureName())
		? &Features.GetModularFeature<FP4BlueprintTestSeams>(FP4BlueprintTestSeams::FeatureName()) : nullptr;
}
inline FString P4BlueprintApplySourceValuesForTest(UObject* Target, const TSharedPtr<FJsonObject>& Record)
{
	const FP4BlueprintTestSeams* Seams = P4BlueprintTestSeams();
	return Seams ? Seams->ApplySourceValues(Target, Record) : FString(TEXT("This Blueprint import build has no development test seams"));
}
// Inside RunTest. The words match P4_MAPKIT_NOT_RUN_TEXT, which dev_tests.py counts as not run.
#define P4_REQUIRE_BLUEPRINT_TEST_SEAMS() \
	if (!P4BlueprintTestSeams()) { AddWarning(TEXT("P4MapKit test not run. This Blueprint import build has no development test seams.")); return true; }
#endif

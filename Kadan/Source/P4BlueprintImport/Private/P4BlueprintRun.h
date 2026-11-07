// Native orchestration of the versioned per-package Blueprint plans.
#pragma once
#include "CoreMinimal.h"
struct FP4BlueprintImportRunResult;

namespace P4BlueprintRun
{
	struct FOptions
	{
		FString PlansDirectory;
		FString ResultFile;
		FString Kinds = TEXT("Blueprint,AnimBlueprint,WidgetBlueprint,LevelSequence");
		FString Match;
		TSet<FString> Packages;
		bool bExactSelection = false;
		int32 Limit = 0;
		bool bReimport = false;
		TFunction<bool(const TMap<FName, UClass*>&, FString&)> AfterDeclaration;
		TFunction<bool(const FString&,const TSharedPtr<FJsonObject>&,FString&)> ValidateWidgetSource;
		// A map import: a game asset the project does not hold stays the game's and gets no stand-in.
		bool bKeepAbsentAsTheGames = false;
	};

	// Returns the host report, including all per-pass reports and save failures. ResultFile, when
	// supplied, is replaced at checkpoints. Cancellation is honored before persistence starts.
	FString Run(const FOptions& Options, FP4BlueprintImportRunResult* NativeResult = nullptr);
}

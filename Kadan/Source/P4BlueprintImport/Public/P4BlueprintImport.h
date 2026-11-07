// Makes ordinary Blueprint assets from the import plans of decomp/blueprints/plan.py: one plan is one
// Blueprint of the game. Called from Python as unreal.P4BlueprintImport.<function>. Each function
// returns a JSON report: {"ok": bool, "problems": [text], "made": {what: count}}.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "P4BlueprintImport.generated.h"

class FJsonObject;

// C++ authoring UI consumes this directly; JSON is only the Python adapter / saved receipt.
struct P4BLUEPRINTIMPORT_API FP4BlueprintImportRunResult
{
	bool bOk = false;
	bool bCancelled = false;
	int32 Count = 0;
	double Elapsed = 0;
	FString Phase;
	TArray<FString> Errors, Problems, PreservedDependencies;
	TSet<FName> CompletedPackages;
	TMap<FString, FString> PackageErrors;
	// A result/read/exclusive-run infrastructure failure cannot be downgraded
	// to keeping individual unsupported assets as original game references.
	bool bInfrastructureFailure = false;
	// With KeepAbsentAsTheGames: the game assets the Blueprints name that the project does not hold. They
	// stay the game's (no stand-in is made); each maps to the first value or pin that names it.
	TMap<FString, FString> KeptAsTheGames;
};

// Only importer-owned incomplete source rows are marked. Ordinary creator
// Blueprints are unaffected; this query neither loads assets nor creates metadata.
P4BLUEPRINTIMPORT_API bool P4BlueprintReconstructionIncomplete(const UObject* Asset);

UCLASS()
class P4BLUEPRINTIMPORT_API UP4BlueprintImport : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	// One parent-first native import, with progress/cancel and bounded garbage collection.
	UFUNCTION(BlueprintCallable, Category = "P4Blueprints")
	static FString Run(const FString& PlansDirectory, const FString& ResultFile, const FString& Kinds,
		const FString& Match, int32 Limit = 0, bool Reimport = false);

	// Exact package roots selected in the editor; includes their required plan dependencies.
	UFUNCTION(BlueprintCallable, Category = "P4Blueprints")
	static FString RunSelected(const FString& PlansDirectory, const FString& ResultFile,
		const TArray<FString>& Packages, bool Reimport = false);

	// Synchronous dependency import after successful genuine class declarations and before
	// widget/signature/default values. Failure aborts the batch with the caller's named reason.
	// KeepAbsentAsTheGames (a map import): a game asset the project does not hold gets no stand-in. A value
	// that names it keeps the game's value in the Blueprint's record with a note, and the result names it.
	static FP4BlueprintImportRunResult RunSelectedNative(const FString& PlansDirectory, const FString& ResultFile,
		const TArray<FString>& Packages, bool Reimport = false,
		const TFunction<bool(const TMap<FName, UClass*>&, FString&)>& AfterDeclaration = {}, bool KeepAbsentAsTheGames = false,
		const TFunction<bool(const FString&,const TSharedPtr<FJsonObject>&,FString&)>& ValidateWidgetSource = {});

	// The asset with its parent class, variables and component tree, compiled. The parent class and
	// the classes the variables name must be in the project already.
	UFUNCTION(BlueprintCallable, Category = "P4Blueprints")
	static FString Declare(const FString& PlanFile);

	// The stored values: class defaults, component templates, and this Blueprint's own templates
	// for components a parent declares. An asset of the game a value names gets an empty stand-in of
	// its class at that path when the project has none, so the reference is kept by path. A map import
	// (RunSelectedNative with KeepAbsentAsTheGames) makes none: the asset stays the game's.
	UFUNCTION(BlueprintCallable, Category = "P4Blueprints")
	static FString Values(const FString& PlanFile);

	// The widget tree and the animations of a widget Blueprint from the plan's "widget": every widget,
	// slot, animation, movie scene, track and section as an object of its class with its stored values.
	// Then a compile; compiler errors are in the report.
	UFUNCTION(BlueprintCallable, Category = "P4Blueprints")
	static FString Widgets(const FString& PlanFile);

	// What a map's level Blueprint holds, from one record of decomp/blueprints/levels.py (as JSON text):
	// its variables with their values and its events. The level Blueprint is made if the map has none.
	// For a level import: the level Blueprint is part of the map.
	UFUNCTION(BlueprintCallable, Category = "P4Blueprints")
	static FString LevelScript(UWorld* World, const FString& Record);

	// A level sequence from the plan's "sequence": the asset, its movie scene with every track and
	// section, the objects it spawns, and its Blueprint (the director) with the director's variables.
	UFUNCTION(BlueprintCallable, Category = "P4Blueprints")
	static FString Sequence(const FString& PlanFile);

	// Ties the events of a widget Blueprint's animation event tracks to their endpoints (custom events
	// or functions of the Blueprint, which Signatures has made), from the plan's "widget" events.
	UFUNCTION(BlueprintCallable, Category = "P4Blueprints")
	static FString WidgetEvents(const FString& PlanFile);

	// Gives the animation objects of a widget Blueprint the signatures of the plan again. The compiler
	// gives every animation with an event track new ones each time it compiles.
	UFUNCTION(BlueprintCallable, Category = "P4Blueprints")
	static FString WidgetSignatures(const FString& PlanFile);

	// The function graphs with their parameters, the local variables and the event nodes of the plan's
	// graphs. After this the Blueprint's functions and events can be called from other graphs.
	UFUNCTION(BlueprintCallable, Category = "P4Blueprints")
	static FString Signatures(const FString& PlanFile);

	// The nodes and wires of the plan's graphs, then a compile. Compiler errors are in the report.
	UFUNCTION(BlueprintCallable, Category = "P4Blueprints")
	static FString Graphs(const FString& PlanFile);

	// The animation graph of an animation Blueprint from the plan's "anim": the animation nodes with
	// their settings, the pose wires, the state machines with states, transitions and rule graphs.
	// Then a compile; compiler errors are in the report.
	UFUNCTION(BlueprintCallable, Category = "P4Blueprints")
	static FString AnimGraphs(const FString& PlanFile);

	// A picture of one graph of a Blueprint as the graph editor draws it, the whole graph in view,
	// written as a PNG. Graph: a function's name, or EventGraph. Needs a real display device (not -NullRHI).
	UFUNCTION(BlueprintCallable, Category = "P4Blueprints")
	static bool RenderGraph(const FString& BlueprintPath, const FString& Graph, const FString& File, int32 Width, int32 Height);

	// Every ID stored in a Blueprint's package, the IDs of its nodes' pins, and the nodes the editor's
	// undo would not record, as JSON (P4Ids::Dump). Two imports of one Blueprint give the same text.
	UFUNCTION(BlueprintCallable, Category = "P4Blueprints")
	static FString Ids(const FString& BlueprintPath);

	// Tries the editor's undo on every node of a Blueprint: moves the node and cuts the wires of one
	// pin as one step, undoes the step, and checks that the node is back where it was with its wires.
	// The report counts the nodes tried and the nodes put back. Nothing is saved.
	UFUNCTION(BlueprintCallable, Category = "P4Blueprints")
	static FString UndoCheck(const FString& BlueprintPath);

	// What the editor's own classes and structs declare: {"<path>": {"p": [property names], "f": [function
	// names]}} for every class and struct in memory, written as JSON. The stub generator compares the
	// game's engine classes with it to find what the game's engine adds.
	UFUNCTION(BlueprintCallable, Category = "P4Blueprints")
	static bool DumpDeclarations(const FString& File);

	// Packages of the stand-ins made since the last call, for saving.
	UFUNCTION(BlueprintCallable, Category = "P4Blueprints")
	static TArray<FString> TakeStandIns();

	// Persistent package metadata and asset registry key P4BlueprintStandIn=1. A real importer
	// clears the metadata only after filling the asset successfully; tagged assets cannot ship.
	UFUNCTION(BlueprintCallable, Category = "P4Blueprints")
	static bool IsStandIn(UObject* Asset);

	// Shared absent-package guard used by Values and animation graph references.
	UFUNCTION(BlueprintCallable, Category = "P4Blueprints")
	static FString StandInAsset(const FString& AssetPath, const FString& ClassPath);

	// Publish the neutral saved-file baseline only for a stand-in made in this session.
	UFUNCTION(BlueprintCallable, Category = "P4Blueprints")
	static FString CaptureStandIn(const FString& PackageName);

	// Validate an untouched fresh stand-in before saving, then publish its saved-file baseline.
	// Native save changes are authorized only across this single guarded save operation.
	UFUNCTION(BlueprintCallable, Category = "P4Blueprints")
	static FString SaveStandIn(const FString& PackageName);

	// A tagged stand-in is fillable only while its creation/saved fingerprint still matches.
	UFUNCTION(BlueprintCallable, Category = "P4Blueprints")
	static FString StandInStatus(UObject* Asset);

	// Tagged assets currently known to the registry, including ones made by an earlier session.
	UFUNCTION(BlueprintCallable, Category = "P4Blueprints")
	static TArray<FString> StandIns();
};

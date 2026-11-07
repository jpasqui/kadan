// Editor helpers for the stub classes that UE 4.26's Python cannot do itself: a component on a placed
// actor, and a Blueprint with components. Experimental (map kit step 2). Called from Python as
// unreal.P4StubTools.<function>.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Templates/SubclassOf.h"
#include "P4StubTools.generated.h"

class AActor;
class UActorComponent;
class UBlueprint;
class USceneComponent;

UCLASS()
class P4STUBSEDITOR_API UP4StubTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	// A component added to one placed actor, as the Details panel's Add Component does.
	UFUNCTION(BlueprintCallable, Category = "P4Stubs")
	static UActorComponent* AddInstanceComponent(AActor* Actor, TSubclassOf<UActorComponent> ComponentClass, FName Name, USceneComponent* Parent);

	// A new actor Blueprint at a package path (/Game/...), with the given parent class.
	UFUNCTION(BlueprintCallable, Category = "P4Stubs")
	static UBlueprint* CreateActorBlueprint(const FString& PackageName, UClass* ParentClass);

	// A component of a Blueprint (a variable with a template named <VariableName>_GEN_VARIABLE).
	// ParentVariableName: the component it is attached under, in this Blueprint or a parent one. None: a root node.
	// Returns the template.
	UFUNCTION(BlueprintCallable, Category = "P4Stubs")
	static UActorComponent* AddBlueprintComponent(UBlueprint* Blueprint, TSubclassOf<UActorComponent> ComponentClass, FName VariableName, FName ParentVariableName);

	// This Blueprint's own template for a component a parent Blueprint declares. Change at least one
	// property on it before compiling: the engine drops an override that equals the parent's template.
	UFUNCTION(BlueprintCallable, Category = "P4Stubs")
	static UActorComponent* OverrideInheritedComponent(UBlueprint* Blueprint, FName VariableName);

	UFUNCTION(BlueprintCallable, Category = "P4Stubs")
	static bool CompileBlueprint(UBlueprint* Blueprint);

	// Set one property of an object from Unreal's text form, e.g. "(DebugUniqueId=5)" for a struct (members
	// not named keep their values). For properties Python cannot write (types Blueprints cannot hold).
	UFUNCTION(BlueprintCallable, Category = "P4Stubs")
	static bool SetPropertyText(UObject* Object, FName Property, const FString& Text);

	// Names of the component templates the compiled class and its Blueprint parents hold, for checking.
	UFUNCTION(BlueprintCallable, Category = "P4Stubs")
	static TArray<FString> BlueprintTemplates(UBlueprint* Blueprint);
};

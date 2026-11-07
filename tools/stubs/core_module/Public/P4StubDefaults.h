// The default values of the stub classes, as the game's own class default objects have them.
// A cooked level stores only what differs from the defaults, so a stub must start from the game's.
// The generated constructors call FP4StubDefaults::Apply with the rows gen_headers.py wrote from
// the capture (tools/navigation/stubs/eden_defaults.py).

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "P4StubDefaults.generated.h"

struct FP4StubDefault
{
	enum EKind : uint8
	{
		Text,       // Value is Unreal's text form of the property
		Subobject,  // Value names a default subobject of the object being constructed ("Mesh", "Mesh.Inner")
		Object,     // Value is the path of an object that is in memory (a class); never loaded
	};

	const TCHAR* Sub;       // "" for the object itself, else the default subobject the row is for
	const TCHAR* Property;  // "Life", "PrimaryActorTick.bCanEverTick", "Sizes[2]"
	const TCHAR* Value;
	EKind Kind;
};

// A default value of one of the engine's own classes where the game's engine has another. A stub
// class sets its values in its constructor; an engine class has no stub, and a Blueprint or a level
// uses it as it is. The module puts its own function in front of the class's constructor function
// (UClass::ClassConstructor, which the engine calls for every object of the class), so that every
// object starts with the game's value: only then does a saved file hold the same values the game's
// own files hold, which store what differs from the default. gen_headers.py writes the rows
// (P4EngineDefaults.cpp) from the same capture as the others.
struct FP4EngineDefault
{
	const TCHAR* Class;     // "/Script/Engine.PointLightComponent": that class and every class below it
	const TCHAR* Property;
	const TCHAR* Value;     // Unreal's text form
};

// Ends with a row of null pointers.
extern const FP4EngineDefault GP4EngineDefaults[];

struct P4STUBSCORE_API FP4StubDefaults
{
	// Called by a stub class's constructor with the rows of that class.
	static void Apply(UObject* Object, const TCHAR* Class, const FP4StubDefault* Rows, int32 Count);
};

// For checks from Python: unreal.P4StubDefaultsLibrary.
UCLASS()
class P4STUBSCORE_API UP4StubDefaultsLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	// One line per row that could not be applied: "Class|Sub|Property|Value|reason".
	UFUNCTION(BlueprintCallable, Category = "P4Stubs")
	static TArray<FString> Problems();

	// How many rows were applied to class default objects.
	UFUNCTION(BlueprintCallable, Category = "P4Stubs")
	static int32 AppliedToDefaults();

	// Unreal's text form of a property of an object or of one of its default subobjects. Empty Sub: the
	// object itself. "?" followed by the reason when there is no such subobject or property.
	UFUNCTION(BlueprintCallable, Category = "P4Stubs")
	static FString Read(UObject* Object, const FString& Sub, const FString& Property);

	// True when the property holds what Unreal reads from the text (the text form of a row's value).
	UFUNCTION(BlueprintCallable, Category = "P4Stubs")
	static bool Matches(UObject* Object, const FString& Sub, const FString& Property, const FString& Value);
};

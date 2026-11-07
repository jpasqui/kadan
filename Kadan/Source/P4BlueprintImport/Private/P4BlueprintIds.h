// The IDs a Blueprint asset stores (of variables, graphs, nodes and pins), made from the Blueprint's path
// and each thing's name instead of at random: two imports of one Blueprint, in two projects, hold the
// same IDs, so a Blueprint built on a recovered one in one project finds its parts in another.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UBlueprint;

namespace P4Ids
{
	// The ID of a text: the same text gives the same ID everywhere (the recipe of the engine's own
	// UBlueprintCore::GenerateDeterministicGuid).
	FGuid From(const FString& Text);

	// The ID of a variable of a Blueprint, by its name.
	FGuid OfVariable(const UBlueprint* Blueprint, FName Name);

	// The ID of a component of a Blueprint whose ID the game's file does not hold, by its variable name.
	FGuid OfComponent(const UBlueprint* Blueprint, FName Name);

	// Gives a Blueprint, and every variable, local variable, graph, node, pin and notify of it, its ID. A graph is named
	// by its place (its name, under the node that holds it), an event by its function, an animation node
	// by the name the game's class gives it, any other node by its number among the graph's nodes. To be
	// called whenever nodes or graphs were made, before a compile: calling it again changes nothing
	// that another Blueprint may already name (variables, function graphs, custom events).
	void Name(UBlueprint* Blueprint);

	// Gives each dynamic binding object of the Blueprint's class (a component event binding, ...) the first free name
	// of its class in the class, as the first compile of the Blueprint names it and the game's file holds it. The
	// engine numbers new objects of a class across the whole editor session, so a later compile names them _1, _2.
	// To be called after a compile.
	void NameBindings(UBlueprint* Blueprint);

	// Every ID found in the objects of a Blueprint's package (each property that is an ID, by the
	// object's path and the property's place), the IDs of its nodes' pins, and how many of its nodes
	// the editor's undo would not record. For checking two imports against each other.
	TSharedRef<FJsonObject> Dump(UBlueprint* Blueprint);
}

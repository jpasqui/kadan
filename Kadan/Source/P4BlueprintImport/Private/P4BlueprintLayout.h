// Where the nodes of a recovered graph go on screen. The game's files hold no positions, and a plan
// holds none: each graph is laid out here from its own nodes and wires when it has been made.

#pragma once

#include "CoreMinimal.h"

class UEdGraph;
class UEdGraphNode;

namespace P4Layout
{
	// An ordinary graph. Execution runs left to right from each entry or event; the first wire out of a
	// node stays on its row and every further one (the false side of a branch, a loop's Completed) starts
	// a row below. The nodes that only supply values sit directly left of the node that uses them, just
	// under the row, each one's own suppliers left of it again, packed so that nothing overlaps. Sizes
	// are estimates of what the editor draws.
	void Graph(UEdGraph* Graph);

	// The ordinary nodes that compute the inputs of one node (an animation node, a transition's rule):
	// left of it, nearest first.
	void Inputs(UEdGraphNode* Node, const TArray<UEdGraphNode*>& Computing);

	// A pose graph, as a tree: the output on the right, what feeds a node left of it in the order of its
	// pins, every node beside the middle of what feeds it. The nodes that compute a node's inputs
	// (Computing) go under it, and what feeds it starts left of them. A pose nothing uses (a saved pose,
	// used by name) is a tree of its own under the others.
	void Poses(UEdGraph* Graph, const TMap<UEdGraphNode*, TArray<UEdGraphNode*>>& Computing);

	// The states of a state machine, by number: left to right by how many transitions away from the
	// first state they are, at most eight to a column. Transitions: (state it leaves, state it goes to).
	TArray<FIntPoint> States(int32 Count, const TArray<FIntPoint>& Transitions, int32 Initial);
}

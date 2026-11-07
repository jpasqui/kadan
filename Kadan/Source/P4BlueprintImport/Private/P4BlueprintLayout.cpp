// Where the nodes of a recovered graph go on screen. See the header.

#include "P4BlueprintLayout.h"
#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_Root.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_MakeArray.h"
#include "K2Node_Select.h"
#include "K2Node_Self.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"

namespace
{
	bool IsExec(const UEdGraphPin* Pin)
	{
		return Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
	}

	// How many pins the editor draws on each side of a node.
	void Rows(const UEdGraphNode* Node, int32& In, int32& Out)
	{
		In = Out = 0;
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && !Pin->bHidden)
			{
				++(Pin->Direction == EGPD_Input ? In : Out);
			}
		}
	}

	// About the size the editor draws a node: (width, height).
	FIntPoint SizeOf(const UEdGraphNode* Node)
	{
		int32 In = 0, Out = 0;
		Rows(Node, In, Out);
		const int32 Tall = FMath::Max3(In, Out, 1);
		const int32 Letters = Node->GetNodeTitle(ENodeTitleType::ListView).ToString().Len();
		if (Node->IsA<UK2Node_VariableGet>() || Node->IsA<UK2Node_Self>())
		{
			return FIntPoint(60 + 8 * Letters, 46);
		}
		if (Node->IsA<UK2Node_VariableSet>())
		{
			return FIntPoint(110 + 8 * Letters, 76);
		}
		if (Node->IsA<UK2Node_MakeArray>() || Node->IsA<UK2Node_Select>())
		{
			return FIntPoint(310, 70 + 34 * Tall);
		}
		const UK2Node* Plain = Cast<UK2Node>(Node);
		if (Plain && Plain->ShouldDrawCompact())
		{
			return FIntPoint(130, 40 + 26 * FMath::Max(Tall, 2));
		}
		return FIntPoint(FMath::Clamp(9 * Letters + 70, 230, 480), 60 + 28 * Tall);
	}

	// How many columns left of the node each of the nodes that compute its inputs stands: one more than
	// the farthest node it supplies.
	TMap<UEdGraphNode*, int32> Columns(UEdGraphNode* Node, const TArray<UEdGraphNode*>& Computing)
	{
		TMap<UEdGraphNode*, int32> Far;
		for (int32 Round = 0; Round <= Computing.Num(); ++Round)
		{
			for (UEdGraphNode* Each : Computing)
			{
				for (UEdGraphPin* Pin : Each->Pins)
				{
					for (UEdGraphPin* To : Pin && Pin->Direction == EGPD_Output ? Pin->LinkedTo : TArray<UEdGraphPin*>())
					{
						UEdGraphNode* Target = To->GetOwningNode();
						if (Target == Node || Computing.Contains(Target))
						{
							const int32 Beyond = Target == Node ? 0 : Far.FindRef(Target);
							int32& Mine = Far.FindOrAdd(Each);
							Mine = FMath::Max(Mine, Beyond + 1);
						}
					}
				}
			}
		}
		for (UEdGraphNode* Each : Computing)
		{
			int32& Mine = Far.FindOrAdd(Each);
			Mine = FMath::Max(Mine, 1);
		}
		return Far;
	}
}

void P4Layout::Graph(UEdGraph* Graph)
{
	TMap<UEdGraphNode*, TArray<UEdGraphNode*>> Runs;    // node -> the nodes its execution wires go to, in pin order
	TMap<UEdGraphNode*, TArray<UEdGraphNode*>> Feeds;   // node -> the nodes whose values it takes, in pin order
	TSet<UEdGraphNode*> RunInto;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		for (UEdGraphPin* Pin : Node ? Node->Pins : TArray<UEdGraphPin*>())
		{
			for (UEdGraphPin* Other : Pin ? Pin->LinkedTo : TArray<UEdGraphPin*>())
			{
				if (Pin->Direction == EGPD_Output && IsExec(Pin))
				{
					Runs.FindOrAdd(Node).AddUnique(Other->GetOwningNode());
					RunInto.Add(Other->GetOwningNode());
				}
				else if (Pin->Direction == EGPD_Input && !IsExec(Pin))
				{
					Feeds.FindOrAdd(Node).AddUnique(Other->GetOwningNode());
				}
			}
		}
	}
	TArray<UEdGraphNode*> Starts;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node && (Node->IsA<UK2Node_FunctionEntry>() || Node->IsA<UK2Node_Event>()))
		{
			Starts.Add(Node);
		}
	}
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node && Runs.Contains(Node) && !RunInto.Contains(Node))
		{
			Starts.AddUnique(Node);
		}
	}
	struct FStep
	{
		UEdGraphNode* Node;
		int32 Depth;
		int32 Row;
	};
	TMap<UEdGraphNode*, int32> Column, Lane;
	TArray<UEdGraphNode*> Order;
	int32 Lanes = 0;
	for (UEdGraphNode* Begin : Starts)
	{
		if (Column.Contains(Begin))
		{
			continue;
		}
		TArray<FStep> Stack;
		Stack.Add({ Begin, 0, Lanes++ });
		while (Stack.Num())
		{
			const FStep Step = Stack.Pop();
			if (Column.Contains(Step.Node))
			{
				continue;
			}
			Column.Add(Step.Node, Step.Depth);
			Lane.Add(Step.Node, Step.Row);
			Order.Add(Step.Node);
			TArray<FStep> Later;
			int32 Number = 0;
			for (UEdGraphNode* Other : Runs.FindRef(Step.Node))
			{
				const bool bFirst = Number++ == 0;
				if (!Column.Contains(Other))
				{
					Later.Add({ Other, Step.Depth + 1, bFirst ? Step.Row : Lanes++ });
				}
			}
			for (int32 At = Later.Num() - 1; At >= 0; --At)
			{
				Stack.Add(Later[At]);
			}
		}
	}
	TMap<int32, int32> Widest;
	int32 Deepest = 0;
	for (UEdGraphNode* Node : Order)
	{
		int32& Wide = Widest.FindOrAdd(Column[Node]);
		Wide = FMath::Max(Wide, SizeOf(Node).X);
		Deepest = FMath::Max(Deepest, Column[Node] + 1);
	}
	TArray<int32> Left;
	int32 X = 0;
	for (int32 Number = 0; Number < Deepest; ++Number)
	{
		Left.Add(X);
		X += Widest.FindRef(Number) + 120;
	}
	TArray<FIntRect> Taken;
	TSet<UEdGraphNode*> Done;
	// The first top at or below the one asked where a rectangle touches nothing placed.
	auto Free = [&Taken](int32 AtLeft, int32 Top, FIntPoint Size)
	{
		for (bool bMoved = true; bMoved;)
		{
			bMoved = false;
			for (const FIntRect& Each : Taken)
			{
				if (AtLeft < Each.Max.X + 30 && Each.Min.X < AtLeft + Size.X + 30 && Top < Each.Max.Y + 20 && Each.Min.Y < Top + Size.Y + 20)
				{
					Top = Each.Max.Y + 20;
					bMoved = true;
				}
			}
		}
		return Top;
	};
	auto Put = [&Taken, &Done](UEdGraphNode* Node, int32 AtLeft, int32 Top)
	{
		const FIntPoint Size = SizeOf(Node);
		Node->NodePosX = AtLeft;
		Node->NodePosY = Top;
		Taken.Add(FIntRect(AtLeft, Top, AtLeft + Size.X, Top + Size.Y));
		Done.Add(Node);
		return Top + Size.Y;
	};
	// Places the nodes a node takes values from, to its left from the given top down. Returns the bottom.
	TFunction<int32(UEdGraphNode*, int32, int32)> Supply = [&](UEdGraphNode* User, int32 Right, int32 Top)
	{
		int32 Bottom = Top;
		for (UEdGraphNode* Supplier : Feeds.FindRef(User))
		{
			if (Done.Contains(Supplier))
			{
				continue;
			}
			const FIntPoint Size = SizeOf(Supplier);
			const int32 AtLeft = Right - Size.X - 50;
			const int32 At = Free(AtLeft, Top, Size);
			int32 Low = Put(Supplier, AtLeft, At);
			Low = FMath::Max(Low, Supply(Supplier, AtLeft, At));
			Bottom = FMath::Max(Bottom, Low);
			Top = Low + 14;
		}
		return Bottom;
	};
	int32 Y = 0;
	for (int32 Number = 0; Number < Lanes; ++Number)
	{
		int32 Low = Y;
		for (UEdGraphNode* Node : Order)
		{
			if (Lane[Node] == Number)
			{
				Low = FMath::Max(Low, Put(Node, Left[Column[Node]], Y));
			}
		}
		for (UEdGraphNode* Node : Order)
		{
			if (Lane[Node] == Number)
			{
				Low = FMath::Max(Low, Supply(Node, Left[Column[Node]], Y + 50));
			}
		}
		Y = Low + 90;
	}
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node && !Done.Contains(Node))
		{
			Node->NodePosX = -400;
			Node->NodePosY = Y;
			Y += 140;
		}
	}
}

void P4Layout::Inputs(UEdGraphNode* Node, const TArray<UEdGraphNode*>& Computing)
{
	const TMap<UEdGraphNode*, int32> Far = Columns(Node, Computing);
	TMap<int32, int32> Rows;
	for (UEdGraphNode* Each : Computing)
	{
		const int32 Depth = Far[Each];
		Each->NodePosX = Node->NodePosX - 320 * Depth;
		Each->NodePosY = Node->NodePosY + 100 * Rows.FindOrAdd(Depth)++;
	}
}

void P4Layout::Poses(UEdGraph* Graph, const TMap<UEdGraphNode*, TArray<UEdGraphNode*>>& Computing)
{
	TMap<UEdGraphNode*, TArray<UEdGraphNode*>> Feeds;
	TSet<UEdGraphNode*> Used, Done;
	TArray<UEdGraphNode*> All;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node || !Node->IsA<UAnimGraphNode_Base>())
		{
			continue;
		}
		All.Add(Node);
		for (UEdGraphPin* Pin : Node->Pins)
		{
			for (UEdGraphPin* From : Pin && Pin->Direction == EGPD_Input ? Pin->LinkedTo : TArray<UEdGraphPin*>())
			{
				if (From->GetOwningNode()->IsA<UAnimGraphNode_Base>())
				{
					Feeds.FindOrAdd(Node).AddUnique(From->GetOwningNode());
					Used.Add(From->GetOwningNode());
				}
			}
		}
	}
	TMap<UEdGraphNode*, FIntPoint> Beside;   // a node that computes inputs -> its place from its animation node
	// (height of the node with what computes its inputs, how many columns left those reach)
	auto Own = [&](UEdGraphNode* Node)
	{
		int32 In = 0, Out = 0;
		Rows(Node, In, Out);
		const int32 Tall = 90 + 26 * In;
		const bool bFed = Feeds.Contains(Node);
		const TArray<UEdGraphNode*>* Mine = Computing.Find(Node);
		TMap<int32, int32> Count;
		int32 Most = 0, Reach = 0;
		if (Mine)
		{
			const TMap<UEdGraphNode*, int32> Far = Columns(Node, *Mine);
			for (UEdGraphNode* Each : *Mine)
			{
				// Beside the node when no pose comes in from the left, under it otherwise.
				const int32 Depth = Far[Each];
				int32& Row = Count.FindOrAdd(Depth);
				Beside.Add(Each, FIntPoint(-320 * Depth, (bFed ? Tall + 20 : 0) + 100 * Row));
				Most = FMath::Max(Most, ++Row);
				Reach = FMath::Max(Reach, Depth);
			}
		}
		const int32 Whole = bFed ? Tall + (Most ? 100 * Most + 50 : 0) : FMath::Max(Tall, 100 * Most);
		return FIntPoint(Whole, Reach);
	};
	TFunction<int32(UEdGraphNode*, int32, int32)> Place = [&](UEdGraphNode* Node, int32 X, int32 Top)
	{
		if (Done.Contains(Node))
		{
			return 0;
		}
		Done.Add(Node);
		const FIntPoint Mine = Own(Node);
		int32 Below = 0;
		for (UEdGraphNode* Supplier : Feeds.FindRef(Node))
		{
			Below += Place(Supplier, X - 420 - 320 * Mine.Y, Top + Below);
		}
		Node->NodePosX = X;
		Node->NodePosY = Top + FMath::Max(0, (Below - 40 - Mine.X) / 2);
		return FMath::Max(Mine.X + 40, Below);
	};
	int32 Top = 0;
	for (int32 Round = 0; Round < 3; ++Round)
	{
		// The output's tree first, then the poses nothing uses, then a loop of wires (which a pose graph should not have).
		for (UEdGraphNode* Node : All)
		{
			const bool bOutput = Node->IsA<UAnimGraphNode_Root>();
			if (!Done.Contains(Node) && (Round == 2 || (!Used.Contains(Node) && (Round == 1 || bOutput))))
			{
				Top += Place(Node, 0, Top) + 60;
			}
		}
	}
	for (const TPair<UEdGraphNode*, FIntPoint>& Pair : Beside)
	{
		for (const TPair<UEdGraphNode*, TArray<UEdGraphNode*>>& Owner : Computing)
		{
			if (Owner.Value.Contains(Pair.Key))
			{
				Pair.Key->NodePosX = Owner.Key->NodePosX + Pair.Value.X;
				Pair.Key->NodePosY = Owner.Key->NodePosY + Pair.Value.Y;
			}
		}
	}
}

TArray<FIntPoint> P4Layout::States(int32 Count, const TArray<FIntPoint>& Transitions, int32 Initial)
{
	TMap<int32, int32> Far;
	TArray<int32> Queue;
	Far.Add(Initial, 0);
	Queue.Add(Initial);
	int32 Last = 0;
	for (int32 At = 0; At < Queue.Num(); ++At)
	{
		const int32 State = Queue[At];
		for (const FIntPoint& Edge : Transitions)
		{
			if (Edge.X == State && !Far.Contains(Edge.Y))
			{
				Far.Add(Edge.Y, Far[State] + 1);
				Queue.Add(Edge.Y);
				Last = FMath::Max(Last, Far[State] + 1);
			}
		}
	}
	++Last;   // the column of the states no transition leads to
	TArray<FIntPoint> Places;
	Places.SetNumZeroed(Count);
	const int32 Per = 8;
	int32 X = 260;
	for (int32 Depth = 0; Depth <= Last; ++Depth)
	{
		TArray<int32> Group;
		for (int32 State = 0; State < Count; ++State)
		{
			const int32* Held = Far.Find(State);
			if ((Held ? *Held : Last) == Depth)
			{
				Group.Add(State);
			}
		}
		for (int32 Number = 0; Number < Group.Num(); ++Number)
		{
			const int32 InColumn = FMath::Min(Per, Group.Num() - Number / Per * Per);
			// Every other column half a row lower, so a wire to a far column passes between states.
			Places[Group[Number]] = FIntPoint(X + 330 * (Number / Per),
				170 * (Number % Per) + 85 * (Per - InColumn) - 85 * (Per - 1) + 85 * (Number / Per % 2));
		}
		if (Group.Num())
		{
			X += 330 * ((Group.Num() + Per - 1) / Per) + 140;
		}
	}
	return Places;
}

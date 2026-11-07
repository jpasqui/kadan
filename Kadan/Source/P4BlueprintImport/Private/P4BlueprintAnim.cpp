// Animation graphs from a plan's "anim" (decomp/blueprints/anim.py): the animation nodes, the pose wires
// between them, and the state machines with their states, transitions and rule graphs.

#include "P4BlueprintImport.h"
#include "P4BlueprintReimport.h"
#include "P4BlueprintAnimOwnership.h"
#include "P4BlueprintGraphs.h"
#include "P4BlueprintIds.h"
#include "P4BlueprintLayout.h"
#include "P4BlueprintPlan.h"
#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_UseCachedPose.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_StateResult.h"
#include "AnimGraphNode_TransitionResult.h"
#include "AnimStateConduitNode.h"
#include "AnimStateEntryNode.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimationAsset.h"
#include "Animation/BlendProfile.h"
#include "Animation/Skeleton.h"
#include "AnimationGraph.h"
#include "AnimationCustomTransitionGraph.h"
#include "AnimGraphNode_CustomTransitionResult.h"
#include "AnimationStateGraph.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationTransitionGraph.h"
#include "Curves/CurveFloat.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphUtilities.h"
#include "K2Node_AnimGetter.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

// In P4BlueprintImport.cpp: an empty asset of a class at a path when the project has none there.
void P4MakeStandIn(const FString& Path, const FString& ClassPath, TArray<FString>& Problems);

namespace P4Anim
{
	using FReport = P4Graphs::FReport;

	// The member of a graph node that is its animation node (most classes call it Node, not all).
	FStructProperty* NodeMember(UClass* Class, UScriptStruct* Struct = nullptr)
	{
		for (TFieldIterator<FStructProperty> It(Class); It; ++It)
		{
			if (Struct ? It->Struct == Struct : It->Struct->IsChildOf(FAnimNode_Base::StaticStruct()))
			{
				return *It;
			}
		}
		return nullptr;
	}

	// The settings of a node that the plan gives a value from a wire: their pins are shown.
	void ShowPins(UAnimGraphNode_Base* Node, const TSharedPtr<FJsonObject>& Record)
	{
		const TArray<TSharedPtr<FJsonValue>>* Shown = nullptr;
		if (!Record->TryGetArrayField(TEXT("pins"), Shown))
		{
			return;
		}
		for (const TSharedPtr<FJsonValue>& Item : *Shown)
		{
			for (FOptionalPinFromProperty& Optional : Node->ShowPinForProperties)
			{
				if (Optional.PropertyName == FName(*Item->AsString()))
				{
					Optional.bShowPin = true;
				}
			}
		}
	}

	// The ordinary Blueprint nodes that compute a node's inputs, wired into its pins. In the plan the
	// node itself is "@". Returns the nodes made.
	TArray<UEdGraphNode*> Inputs(UBlueprint* Blueprint, UEdGraph* Graph, UAnimGraphNode_Base* Node, const TSharedPtr<FJsonObject>& Record, FReport& Report)
	{
		TArray<UEdGraphNode*> Computing;
		const TSharedPtr<FJsonObject>* Computed = nullptr;
		if (!Record->TryGetObjectField(TEXT("inputs"), Computed))
		{
			return Computing;
		}
		const UEdGraphSchema* Schema = Graph->GetSchema();
		TMap<FString, UEdGraphNode*> Made;
		for (const TSharedPtr<FJsonValue>& Item : (*Computed)->GetArrayField(TEXT("nodes")))
		{
			const TSharedPtr<FJsonObject> Each = Item->AsObject();
			if (UEdGraphNode* One = P4Graphs::Make(Blueprint, Graph, Each, Report))
			{
				Computing.Add(One);
				Made.Add(Each->GetStringField(TEXT("id")), One);
				Report.Count(TEXT("nodes that compute inputs"));
			}
		}
		for (const TSharedPtr<FJsonValue>& Item : (*Computed)->GetArrayField(TEXT("links")))
		{
			const TArray<TSharedPtr<FJsonValue>>& Link = Item->AsArray();
			UEdGraphNode** From = Made.Find(Link[0]->AsString());
			UEdGraphNode* To = Link[2]->AsString() == TEXT("@") ? Node : Made.FindRef(Link[2]->AsString());
			UEdGraphPin* Out = From ? P4Graphs::Pin(*From, Link[1]->AsString(), EGPD_Output) : nullptr;
			UEdGraphPin* In = To ? P4Graphs::Pin(To, Link[3]->AsString(), EGPD_Input) : nullptr;
			if (!Out || !In || !Schema->TryCreateConnection(Out, In))
			{
				Report.Problem(FString::Printf(TEXT("%s: no wire %s.%s to %s.%s"), *Node->GetName(), *Link[0]->AsString(), *Link[1]->AsString(),
					*Link[2]->AsString(), *Link[3]->AsString()));
				continue;
			}
			Report.Count(TEXT("wires into inputs"));
		}
		for (const TSharedPtr<FJsonValue>& Item : (*Computed)->GetArrayField(TEXT("nodes")))
		{
			const TSharedPtr<FJsonObject> Each = Item->AsObject();
			if (UEdGraphNode** One = Made.Find(Each->GetStringField(TEXT("id"))))
			{
				P4Graphs::Defaults(*One, Each, Node->GetName(), Report);
			}
		}
		return Computing;
	}

	// Settings of the graph node itself, as text: {property: text}.
	void NodeProperties(UAnimGraphNode_Base* Node, const TSharedPtr<FJsonObject>& Record, FReport& Report)
	{
		const TSharedPtr<FJsonObject>* Extra = nullptr;
		if (!Record->TryGetObjectField(TEXT("properties"), Extra))
		{
			return;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Extra)->Values)
		{
			FProperty* Property = Node->GetClass()->FindPropertyByName(FName(*Pair.Key));
			TArray<FString> Refused;
			int32 Transient = 0;
			if (!Property || !P4Plan::ImportText(Property, Pair.Value->AsString(), Property->ContainerPtrToValuePtr<void>(Node), Node, Refused, &Transient))
			{
				Report.Problem(FString::Printf(TEXT("%s: the graph node did not take %s"), *Node->GetName(), *Pair.Key));
			}
			for (const FString& Line : Refused)
			{
				Report.Problem(FString::Printf(TEXT("%s: %s: the editor refused part of the value: %s"), *Node->GetName(), *Pair.Key, *Line));
			}
			if (Transient)
			{
				Report.Count(TEXT("transient members not imported (the state of a running node)"), Transient);
			}
		}
	}

	// The graph node class that holds a given animation node struct. When a class and a class derived
	// from it both hold the struct, the base one is the plain node.
	UClass* NodeClassFor(UScriptStruct* Struct)
	{
		static TMap<UScriptStruct*, UClass*> Known;
		if (UClass** Found = Known.Find(Struct))
		{
			return *Found;
		}
		UClass* Best = nullptr;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (!It->IsChildOf(UAnimGraphNode_Base::StaticClass()) || It->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
			{
				continue;
			}
			if (NodeMember(*It, Struct) && (!Best || Best->IsChildOf(*It)))
			{
				Best = *It;
			}
		}
		Known.Add(Struct, Best);
		return Best;
	}

	// The name the compiled class gives the node's property, and the node's settings.
	void Configure(UAnimGraphNode_Base* Node, const FString& Name, const FString& Text, FReport& Report,
		const TSharedPtr<FJsonObject>& Record = nullptr)
	{
		if (!Name.IsEmpty() && Node->GetName() != Name)
		{
			if (UObject* Other = StaticFindObjectFast(nullptr, Node->GetOuter(), FName(*Name)))
			{
				Other->Rename(nullptr, nullptr, REN_DontCreateRedirectors | REN_NonTransactional | REN_ForceNoResetLoaders);
			}
			Node->Rename(*Name, nullptr, REN_DontCreateRedirectors | REN_NonTransactional | REN_ForceNoResetLoaders);
		}
		FStructProperty* Member = NodeMember(Node->GetClass());
		TArray<FString> Refused;
		int32 Transient = 0;
		if (Member && !Text.IsEmpty() && !P4Plan::ImportText(Member, Text, Member->ContainerPtrToValuePtr<void>(Node), Node, Refused, &Transient))
		{
			Report.Problem(FString::Printf(TEXT("%s did not take its settings"), *Name));
		}
		for (const FString& Line : Refused)
		{
			Report.Problem(FString::Printf(TEXT("%s: the editor refused part of its settings: %s"), *Name, *Line));
		}
		if (Transient)
		{
			Report.Count(TEXT("transient members not imported (the state of a running node)"), Transient);
		}
		// New pins: a pin takes its default from the node's setting when it is made, and a rebuilt
		// node would keep the old pin's value instead. The list of settings that can be pins is made
		// with the pins, so the ones to show are marked after a first round and the pins made again.
		for (int32 Round = 0; Round < (Record.IsValid() ? 2 : 1); ++Round)
		{
			for (UEdGraphPin* Old : TArray<UEdGraphPin*>(Node->Pins))
			{
				Node->RemovePin(Old);
			}
			Node->AllocateDefaultPins();
			if (Record.IsValid())
			{
				ShowPins(Node, Record);
			}
		}
	}

	UAnimGraphNode_Base* MakeNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Record, FReport& Report)
	{
		const FString Name = Record->GetStringField(TEXT("name"));
		const FString StructPath = Record->GetStringField(TEXT("struct"));
		UScriptStruct* Struct = LoadObject<UScriptStruct>(nullptr, *StructPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		UClass* Class = Struct ? NodeClassFor(Struct) : nullptr;
		if (!Class)
		{
			Report.Problem(FString::Printf(TEXT("%s: the editor has no graph node for %s"), *Name, *StructPath));
			return nullptr;
		}
		if (UObject* Other = StaticFindObjectFast(nullptr, Graph, FName(*Name)))
		{
			Other->Rename(nullptr, nullptr, REN_DontCreateRedirectors | REN_NonTransactional | REN_ForceNoResetLoaders);
		}
		UAnimGraphNode_Base* Node = NewObject<UAnimGraphNode_Base>(Graph, Class, FName(*Name), RF_Transactional);
		Node->CreateNewGuid();
		Node->PostPlacedNewNode();
		FStructProperty* Member = NodeMember(Class, Struct);
		const FString Text = Record->GetStringField(TEXT("text"));
		TArray<FString> Refused;
		int32 Transient = 0;
		if (Member && !Text.IsEmpty() && !P4Plan::ImportText(Member, Text, Member->ContainerPtrToValuePtr<void>(Node), Node, Refused, &Transient))
		{
			Report.Problem(FString::Printf(TEXT("%s did not take its settings"), *Name));
		}
		for (const FString& Line : Refused)
		{
			Report.Problem(FString::Printf(TEXT("%s: the editor refused part of its settings: %s"), *Name, *Line));
		}
		if (Transient)
		{
			Report.Count(TEXT("transient members not imported (the state of a running node)"), Transient);
		}
		NodeProperties(Node, Record, Report);
		Node->AllocateDefaultPins();
		if (Record->HasField(TEXT("pins")))
		{
			ShowPins(Node, Record);
			for (UEdGraphPin* Old : TArray<UEdGraphPin*>(Node->Pins))
			{
				Node->RemovePin(Old);
			}
			Node->AllocateDefaultPins();
		}
		Graph->AddNode(Node, false, false);
		Report.Count(TEXT("animation nodes"));
		return Node;
	}

	void BuildMachine(UAnimGraphNode_StateMachine* Machine, const TSharedPtr<FJsonObject>& Record, FReport& Report);

	// The state machines made so far by the name of their node, with their states and transitions by
	// the numbers the compiled class gives them.
	struct FMachineParts
	{
		UAnimGraphNode_StateMachine* Node = nullptr;
		TArray<UAnimStateNodeBase*> States;
		TMap<int32, UAnimStateTransitionNode*> Transitions;
	};
	TMap<FString, FMachineParts> Machines;

	// Ties each anim getter node to the machine and the state or transition its plan names.
	void TieGetters(FReport& Report)
	{
		for (const TPair<UEdGraphNode*, TSharedPtr<FJsonObject>>& Pair : Report.Getters)
		{
			UK2Node_AnimGetter* Getter = Cast<UK2Node_AnimGetter>(Pair.Key);
			const FString Name = Pair.Value->GetStringField(TEXT("machine"));
			FMachineParts* Parts = Machines.Find(Name);
			if (!Getter || !Parts)
			{
				Report.Problem(FString::Printf(TEXT("a getter names the state machine %s, which was not made"), *Name));
				continue;
			}
			Getter->SourceNode = Parts->Node;
			double Number = 0;
			if (Pair.Value->TryGetNumberField(TEXT("state"), Number))
			{
				Getter->SourceStateNode = Parts->States.IsValidIndex((int32)Number) ? Parts->States[(int32)Number] : nullptr;
			}
			else if (Pair.Value->TryGetNumberField(TEXT("transition"), Number))
			{
				Getter->SourceStateNode = Parts->Transitions.FindRef((int32)Number);
			}
			else
			{
				Report.Count(TEXT("getters of a state machine"));
				continue;
			}
			if (!Getter->SourceStateNode)
			{
				Report.Problem(FString::Printf(TEXT("a getter names a state or transition of %s that was not made"), *Name));
				continue;
			}
			Report.Count(TEXT("getters of a state or transition"));
		}
		Report.Getters.Reset();
	}

	// The nodes and pose wires of one graph. Result: the node the graph already has (the output pose of
	// the top graph or of a state), which stands for the plan's node of the given name.
	UBlueprint* Building = nullptr;   // the Blueprint being built

	void BuildGraph(UEdGraph* Graph, UAnimGraphNode_Base* Result, const FString& ResultName, const TSharedPtr<FJsonObject>& Record, FReport& Report)
	{
		const UEdGraphSchema* Schema = Graph->GetSchema();
		TMap<FString, UAnimGraphNode_Base*> Nodes;
		TMap<UEdGraphNode*, TArray<UEdGraphNode*>> Computing;   // an animation node -> the nodes that compute its inputs
		for (const TSharedPtr<FJsonValue>& Item : Record->GetArrayField(TEXT("nodes")))
		{
			const TSharedPtr<FJsonObject> Node = Item->AsObject();
			const FString Name = Node->GetStringField(TEXT("name"));
			UAnimGraphNode_Base* Made = nullptr;
			if (Result && Name == ResultName)
			{
				Made = Result;
				Configure(Made, Name, Node->GetStringField(TEXT("text")), Report, Node);
			}
			else
			{
				Made = MakeNode(Graph, Node, Report);
			}
			if (!Made)
			{
				continue;
			}
			Nodes.Add(Name, Made);
			const TArray<UEdGraphNode*> Wired = Inputs(Building, Graph, Made, Node, Report);
			if (Wired.Num())
			{
				Computing.Add(Made, Wired);
			}
			const TSharedPtr<FJsonObject>* Machine = nullptr;
			if (Node->TryGetObjectField(TEXT("machine"), Machine))
			{
				if (UAnimGraphNode_StateMachine* Owner = Cast<UAnimGraphNode_StateMachine>(Made))
				{
					BuildMachine(Owner, *Machine, Report);
				}
			}
		}
		// The compiler numbers animation nodes in the order of the graph's list of nodes. The node the
		// graph already had (its output) takes its place in the plan's order with the others.
		TArray<UEdGraphNode*> Ordered;
		for (const TSharedPtr<FJsonValue>& Item : Record->GetArrayField(TEXT("nodes")))
		{
			if (UAnimGraphNode_Base** Each = Nodes.Find(Item->AsObject()->GetStringField(TEXT("name"))))
			{
				Ordered.AddUnique(*Each);
			}
		}
		Graph->Nodes.RemoveAll([&Ordered](UEdGraphNode* Each) { return Ordered.Contains(Each); });
		Graph->Nodes.Append(Ordered);
		for (const TSharedPtr<FJsonValue>& Item : Record->GetArrayField(TEXT("links")))
		{
			const TArray<TSharedPtr<FJsonValue>>& Link = Item->AsArray();
			UAnimGraphNode_Base** From = Nodes.Find(Link[0]->AsString());
			UAnimGraphNode_Base** To = Nodes.Find(Link[1]->AsString());
			if (UAnimGraphNode_UseCachedPose* Use = To ? Cast<UAnimGraphNode_UseCachedPose>(*To) : nullptr)
			{
				// A saved pose is used by naming its node, not by a wire.
				UAnimGraphNode_SaveCachedPose* Save = From ? Cast<UAnimGraphNode_SaveCachedPose>(*From) : nullptr;
				if (Save)
				{
					Use->SaveCachedPoseNode = Save;
					Report.Count(TEXT("saved poses used"));
				}
				else
				{
					Report.Problem(FString::Printf(TEXT("%s names no saved pose"), *Link[1]->AsString()));
				}
				continue;
			}
			UEdGraphPin* Out = nullptr;
			UEdGraphPin* In = nullptr;
			for (UEdGraphPin* Each : From ? (*From)->Pins : TArray<UEdGraphPin*>())
			{
				if (Each->Direction == EGPD_Output && Each->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct && !Out)
				{
					Out = Each;
				}
			}
			for (UEdGraphPin* Each : To ? (*To)->Pins : TArray<UEdGraphPin*>())
			{
				if (Each->Direction == EGPD_Input && Each->PinName.ToString().Equals(Link[2]->AsString(), ESearchCase::IgnoreCase))
				{
					In = Each;
				}
			}
			if (!Out || !In || !Schema->TryCreateConnection(Out, In))
			{
				Report.Problem(FString::Printf(TEXT("no pose wire from %s to %s.%s"), *Link[0]->AsString(), *Link[1]->AsString(), *Link[2]->AsString()));
				continue;
			}
			Report.Count(TEXT("pose wires"));
		}
		TMap<UEdGraphNode*, FIntPoint> CreatorPositions;
		for (UEdGraphNode* Node : Graph->Nodes)
			if (P4AnimOwnership::Marked(Node, TEXT("P4BlueprintCreatorNode")))
				CreatorPositions.Add(Node, FIntPoint(Node->NodePosX, Node->NodePosY));
		P4Layout::Poses(Graph, Computing);   // source nodes have no game positions
		for (const auto& Position : CreatorPositions)
		{
			Position.Key->NodePosX = Position.Value.X;
			Position.Key->NodePosY = Position.Value.Y;
		}
	}

	void BuildMachine(UAnimGraphNode_StateMachine* Owner, const TSharedPtr<FJsonObject>& Machine, FReport& Report)
	{
		UAnimationStateMachineGraph* Graph = Owner->EditorStateMachineGraph;
		if (!Graph)
		{
			Report.Problem(FString::Printf(TEXT("%s has no state machine graph"), *Owner->GetName()));
			return;
		}
		FBlueprintEditorUtils::RenameGraph(Graph, Machine->GetStringField(TEXT("name")));
		TArray<UAnimStateNodeBase*> States;
		States.SetNumZeroed(Machine->GetArrayField(TEXT("states")).Num());
		TArray<FIntPoint> Leaves;
		for (const TSharedPtr<FJsonValue>& Item : Machine->GetArrayField(TEXT("transitions")))
		{
			Leaves.Add(FIntPoint((int32)Item->AsObject()->GetNumberField(TEXT("source")), (int32)Item->AsObject()->GetNumberField(TEXT("target"))));
		}
		const TArray<FIntPoint> Places = P4Layout::States(States.Num(), Leaves, (int32)Machine->GetNumberField(TEXT("initial")));
		for (const TSharedPtr<FJsonValue>& Item : Machine->GetArrayField(TEXT("states")))
		{
			const TSharedPtr<FJsonObject> State = Item->AsObject();
			const FString Name = State->GetStringField(TEXT("name"));
			UAnimStateNodeBase* Made = nullptr;
			if (State->GetBoolField(TEXT("conduit")))
			{
				FGraphNodeCreator<UAnimStateConduitNode> Creator(*Graph);
				UAnimStateConduitNode* Conduit = Creator.CreateNode();
				Creator.Finalize();
				Made = Conduit;
				if (UAnimationTransitionGraph* Rule = Cast<UAnimationTransitionGraph>(Conduit->BoundGraph))
				{
					const TSharedPtr<FJsonObject>* Record = nullptr;
					State->TryGetObjectField(TEXT("rule_node"), Record);
					Configure(Rule->GetResultNode(), State->GetStringField(TEXT("rule")), State->GetStringField(TEXT("rule_text")), Report,
						Record ? *Record : nullptr);
					if (Record)
					{
						P4Layout::Inputs(Rule->GetResultNode(), Inputs(Building, Rule, Rule->GetResultNode(), *Record, Report));
					}
				}
			}
			else
			{
				FGraphNodeCreator<UAnimStateNode> Creator(*Graph);
				UAnimStateNode* Plain = Creator.CreateNode();
				Creator.Finalize();
				Plain->bAlwaysResetOnEntry = State->GetBoolField(TEXT("always_reset"));
				const TArray<TSharedPtr<FJsonValue>>& Notifies = State->GetArrayField(TEXT("notifies"));
				FAnimNotifyEvent* Events[3] = { &Plain->StateEntered, &Plain->StateLeft, &Plain->StateFullyBlended };
				for (int32 Which = 0; Which < 3 && Which < Notifies.Num(); ++Which)
				{
					if (!Notifies[Which]->AsString().IsEmpty())
					{
						Events[Which]->NotifyName = FName(*Notifies[Which]->AsString());
					}
				}
				Made = Plain;
				if (UAnimationStateGraph* Inner = Cast<UAnimationStateGraph>(Plain->BoundGraph))
				{
					BuildGraph(Inner, Inner->GetResultNode(), State->GetStringField(TEXT("result")), State->GetObjectField(TEXT("graph")), Report);
				}
			}
			if (Made->GetBoundGraph())
			{
				FBlueprintEditorUtils::RenameGraph(Made->GetBoundGraph(), Name);
			}
			const int32 Index = (int32)State->GetNumberField(TEXT("index"));
			if (States.IsValidIndex(Index))
			{
				States[Index] = Made;
				Made->NodePosX = Places[Index].X;
				Made->NodePosY = Places[Index].Y;
			}
			Report.Count(TEXT("states"));
		}
		const int32 Initial = (int32)Machine->GetNumberField(TEXT("initial"));
		if (Graph->EntryNode && States.IsValidIndex(Initial) && States[Initial] && Graph->EntryNode->Pins.Num() && States[Initial]->GetInputPin())
		{
			Graph->EntryNode->Pins[0]->MakeLinkTo(States[Initial]->GetInputPin());
		}
		UEnum* Blends = FindObject<UEnum>(ANY_PACKAGE, TEXT("EAlphaBlendOption"));
		UEnum* Logic = FindObject<UEnum>(ANY_PACKAGE, TEXT("ETransitionLogicType"));
		TMap<FString, UAnimStateTransitionNode*> ByRule;
		TMap<int32, UAnimStateTransitionNode*> Transitions;
		for (const TSharedPtr<FJsonValue>& Item : Machine->GetArrayField(TEXT("transitions")))
		{
			const TSharedPtr<FJsonObject> Edge = Item->AsObject();
			const int32 Source = (int32)Edge->GetNumberField(TEXT("source"));
			const int32 Target = (int32)Edge->GetNumberField(TEXT("target"));
			if (!States.IsValidIndex(Source) || !States.IsValidIndex(Target) || !States[Source] || !States[Target])
			{
				Report.Problem(TEXT("a transition names a state that is not there"));
				continue;
			}
			FGraphNodeCreator<UAnimStateTransitionNode> Creator(*Graph);
			UAnimStateTransitionNode* Made = Creator.CreateNode();
			Creator.Finalize();
			Made->CreateConnections(States[Source], States[Target]);
			Transitions.Add((int32)Edge->GetNumberField(TEXT("index")), Made);
			Made->PriorityOrder = (int32)Edge->GetNumberField(TEXT("priority"));
			Made->CrossfadeDuration = FCString::Atof(*Edge->GetStringField(TEXT("crossfade")));
			// The rule the editor writes itself: the transition is taken when the state's animation has played.
			bool bAutomatic = false;
			Edge->TryGetBoolField(TEXT("automatic"), bAutomatic);
			Made->bAutomaticRuleBasedOnSequencePlayerInState = bAutomatic;
			// The notifies a transition sends when it starts, ends and is interrupted.
			const TArray<TSharedPtr<FJsonValue>>& Sent = Edge->GetArrayField(TEXT("notifies"));
			FAnimNotifyEvent* Events[3] = { &Made->TransitionStart, &Made->TransitionEnd, &Made->TransitionInterrupt };
			for (int32 Which = 0; Which < 3 && Which < Sent.Num(); ++Which)
			{
				if (!Sent[Which]->AsString().IsEmpty())
				{
					Events[Which]->NotifyName = FName(*Sent[Which]->AsString());
				}
			}
			FString Text;
			if (Blends && Edge->TryGetStringField(TEXT("blend_mode"), Text) && Blends->GetValueByNameString(Text) != INDEX_NONE)
			{
				Made->BlendMode = (EAlphaBlendOption)Blends->GetValueByNameString(Text);
			}
			if (Logic && Edge->TryGetStringField(TEXT("logic"), Text) && Logic->GetValueByNameString(Text) != INDEX_NONE)
			{
				Made->LogicType = (ETransitionLogicType::Type)Logic->GetValueByNameString(Text);
			}
			if (Edge->TryGetStringField(TEXT("curve"), Text) && !Text.IsEmpty())
			{
				Made->CustomBlendCurve = LoadObject<UCurveFloat>(nullptr, *Text, nullptr, LOAD_NoWarn | LOAD_Quiet);
			}
			if (Edge->TryGetStringField(TEXT("profile"), Text) && !Text.IsEmpty())
			{
				Made->BlendProfile = LoadObject<UBlendProfile>(nullptr, *Text, nullptr, LOAD_NoWarn | LOAD_Quiet);
			}
			Made->NodePosX = (States[Source]->NodePosX + States[Target]->NodePosX) / 2;
			Made->NodePosY = (States[Source]->NodePosY + States[Target]->NodePosY) / 2;
			const FString RuleName = Edge->GetStringField(TEXT("rule"));
			if (UAnimStateTransitionNode** First = ByRule.Find(RuleName))
			{
				// Several transitions with one rule graph: the editor's "shared rule". Its name is not in the game.
				if (!(*First)->bSharedRules)
				{
					(*First)->MakeRulesShareable(FString::Printf(TEXT("Shared %s"), *RuleName));
				}
				Made->UseSharedRules(*First);
				Report.Count(TEXT("transitions that share a rule"));
			}
			else
			{
				if (UAnimationTransitionGraph* Rule = Cast<UAnimationTransitionGraph>(Made->BoundGraph))
				{
					const TSharedPtr<FJsonObject>* Record = nullptr;
					Edge->TryGetObjectField(TEXT("rule_node"), Record);
					Configure(Rule->GetResultNode(), RuleName, Edge->GetStringField(TEXT("rule_text")), Report, Record ? *Record : nullptr);
					if (Record)
					{
						P4Layout::Inputs(Rule->GetResultNode(), Inputs(Building, Rule, Rule->GetResultNode(), *Record, Report));
					}
				}
				ByRule.Add(RuleName, Made);
			}
			const TSharedPtr<FJsonObject>* Custom = nullptr;
			if (Edge->TryGetObjectField(TEXT("custom_graph"), Custom))
			{
				// A transition that blends with a graph of its own. The editor makes that graph when the
				// transition's logic is set to custom.
				FPropertyChangedEvent Changed(FindFProperty<FProperty>(UAnimStateTransitionNode::StaticClass(), TEXT("LogicType")));
				Made->PostEditChangeProperty(Changed);
				UAnimationCustomTransitionGraph* Blend = Cast<UAnimationCustomTransitionGraph>(Made->CustomTransitionGraph);
				if (Blend && Blend->GetResultNode())
				{
					// The editor starts the graph with the two poses already in it: the plan has its own.
					for (UEdGraphNode* Given : TArray<UEdGraphNode*>(Blend->Nodes))
					{
						if (Given != Blend->GetResultNode())
						{
							Given->BreakAllNodeLinks();
							Blend->RemoveNode(Given);
						}
					}
					BuildGraph(Blend, Blend->GetResultNode(), Edge->GetStringField(TEXT("custom")), *Custom, Report);
				}
				else
				{
					Report.Problem(TEXT("the editor made no custom blend graph for a transition"));
				}
			}
			Report.Count(TEXT("transitions"));
		}
		FMachineParts& Parts = Machines.Add(Owner->GetName());
		Parts.Node = Owner;
		Parts.States = States;
		Parts.Transitions = Transitions;
	}
}

FString UP4BlueprintImport::AnimGraphs(const FString& PlanFile)
{
	P4Anim::FReport Report;
	const TSharedPtr<FJsonObject> Plan = P4Graphs::LoadPlan(PlanFile, Report);
	if (!Plan.IsValid())
	{
		return Report.Text();
	}
	const FString Path = Plan->GetStringField(TEXT("package")) + TEXT(".") + Plan->GetStringField(TEXT("name"));
	UAnimBlueprint* Blueprint = LoadObject<UAnimBlueprint>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	const TSharedPtr<FJsonObject>* Anim = nullptr;
	if (!Blueprint || !Plan->TryGetObjectField(TEXT("anim"), Anim))
	{
		Report.Problem(TEXT("the animation Blueprint or its plan is not there"));
		Report.bStopped = true;
		return Report.Text();
	}
	// The animations, curves and blend profiles the nodes name: stand-ins on the Blueprint's skeleton.
	FString OwnershipError;
	if (!P4Reimport::HasPreparedAnimation(Blueprint) && !P4AnimOwnership::Reset(Blueprint, Plan, OwnershipError))
	{
		Report.Problem(OwnershipError); Report.bStopped = true; return Report.Text();
	}
	const P4AnimOwnership::FCreated Created(Blueprint);
	for (const TSharedPtr<FJsonValue>& Item : Plan->GetArrayField(TEXT("assets")))
	{
		const FString AssetPath = Item->AsObject()->GetStringField(TEXT("path"));
		P4MakeStandIn(AssetPath, Item->AsObject()->GetStringField(TEXT("cls")), Report.Problems);
		UAnimationAsset* Asset = LoadObject<UAnimationAsset>(nullptr, *AssetPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (Asset && !Asset->GetSkeleton() && Blueprint->TargetSkeleton)
		{
			FString Why;
			if (!P4Reimport::ConfigureStandIn(Asset, [&]() { Asset->SetSkeleton(Blueprint->TargetSkeleton); }, Why))
			{
				Report.Problem(FString::Printf(TEXT("animation %s: %s"), *AssetPath, *Why));
			}
		}
	}
	UAnimationGraph* Top = nullptr;
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (UAnimationGraph* Animation = Cast<UAnimationGraph>(Graph))
		{
			Top = Animation;
		}
	}
	TArray<UAnimGraphNode_Root*> Roots;
	if (Top)
	{
		Top->GetNodesOfClass(Roots);
	}
	if (!Top || !Roots.Num())
	{
		Report.Problem(TEXT("the animation Blueprint has no animation graph with an output pose"));
		Report.bStopped = true;
		return Report.Text();
	}
	// The sync groups in the order the Blueprint lists them (the compiled class keeps that order).
	const TArray<TSharedPtr<FJsonValue>>* Groups = nullptr;
	if ((*Anim)->TryGetArrayField(TEXT("sync_groups"), Groups))
	{
		Blueprint->Groups.Reset();
		for (const TSharedPtr<FJsonValue>& Item : *Groups)
		{
			FAnimGroupInfo Group;
			Group.Name = FName(*Item->AsString());
			Blueprint->Groups.Add(Group);
		}
	}
	P4Anim::Building = Blueprint;
	P4Anim::Machines.Reset();
	P4Graphs::ForgetShared();
	P4Anim::BuildGraph(Top, Roots[0], (*Anim)->GetStringField(TEXT("root")), (*Anim)->GetObjectField(TEXT("graph")), Report);
	P4Anim::TieGetters(Report);
	P4Anim::Machines.Reset();
	Created.Mark(Blueprint, Top, Roots[0]);
	P4Ids::Name(Blueprint);
	P4Reimport::WithCompilerSavePolicy([&]() { FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint); });
	FCompilerResultsLog Log;
	Log.bSilentMode = true;
	Log.bAnnotateMentionedNodes = false;
	const auto CompileBlueprint = [&]() { P4Reimport::WithCompilerSavePolicy([&]() {
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection | EBlueprintCompileOptions::SkipSave, &Log); }); };
	if (P4Reimport::IsFreshStandIn(Blueprint->TargetSkeleton))
	{
		FString Why;
		if (!P4Reimport::ConfigureStandIn(Blueprint->TargetSkeleton, CompileBlueprint, Why))
		{
			Report.Problem(Why); Report.bStopped = true; return Report.Text();
		}
	}
	else { CompileBlueprint(); }
	int32 Shown = 0;
	for (const TSharedRef<FTokenizedMessage>& Message : Log.Messages)
	{
		if (Message->GetSeverity() == EMessageSeverity::Error && Shown++ < 12)
		{
			Report.Problem(FString::Printf(TEXT("compiler: %s"), *Message->ToText().ToString()));
		}
	}
	Report.Count(TEXT("compiler errors"), Log.NumErrors);
	Report.Count(TEXT("compiler warnings"), Log.NumWarnings);
	if (Blueprint->Status == BS_Error)
	{
		Report.bStopped = true;
	}
	Blueprint->MarkPackageDirty();
	return Report.Text();
}

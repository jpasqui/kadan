// Function and event graphs from a plan's "graphs" (decomp/blueprints/k2.py): ordinary nodes and wires.

#include "P4BlueprintImport.h"
#include "P4BlueprintGraphs.h"
#include "P4BlueprintIds.h"
#include "P4BlueprintLayout.h"
#include "P4BlueprintPlan.h"
#include "P4BlueprintReimport.h"
#include "UObject/MetaData.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SimpleConstructionScript.h"
#include "Animation/AnimBlueprint.h"
#include "K2Node_AnimGetter.h"
#include "K2Node_AssignmentStatement.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_TemporaryVariable.h"
#include "K2Node_CallArrayFunction.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CallParentFunction.h"
#include "K2Node_CastByteToEnum.h"
#include "K2Node_ClassDynamicCast.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_EnumLiteral.h"
#include "K2Node_Event.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_GetArrayItem.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_MakeArray.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_MathExpression.h"
#include "K2Node_Select.h"
#include "K2Node_Self.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_SwitchInteger.h"
#include "K2Node_SwitchName.h"
#include "K2Node_SwitchString.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"

// In P4BlueprintImport.cpp: whether a game asset the project does not hold stays the game's in this run.
bool P4KeepAsTheGames(const FString& Path, const FString& Where);

namespace P4Graphs
{
	TSharedPtr<FJsonObject> LoadPlan(const FString& File, FReport& Report)
	{
		FString Why;
		TSharedPtr<FJsonObject> Plan = P4Plan::Load(File, Why);
		if (!Plan.IsValid())
		{
			Report.Stop(Why);
		}
		return Plan;
	}

	UBlueprint* Find(const TSharedPtr<FJsonObject>& Plan)
	{
		const FString Path = Plan->GetStringField(TEXT("package")) + TEXT(".") + Plan->GetStringField(TEXT("name"));
		return LoadObject<UBlueprint>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	bool PinType(const TSharedPtr<FJsonObject>& Record, FEdGraphPinType& Out, FString& Why)
	{
		Out.PinCategory = FName(*Record->GetStringField(TEXT("c")));
		FString Path;
		if (Record->TryGetStringField(TEXT("o"), Path))
		{
			UObject* Object = StaticLoadObject(UObject::StaticClass(), nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (!Object)
			{
				Why = FString::Printf(TEXT("%s is not in the project"), *Path);
				return false;
			}
			Out.PinSubCategoryObject = Object;
		}
		FString Container;
		if (Record->TryGetStringField(TEXT("k"), Container))
		{
			Out.ContainerType = Container == TEXT("array") ? EPinContainerType::Array
				: Container == TEXT("set") ? EPinContainerType::Set : EPinContainerType::Map;
			const TSharedPtr<FJsonObject>* Value = nullptr;
			if (Out.ContainerType == EPinContainerType::Map)
			{
				FEdGraphPinType ValueType;
				if (!Record->TryGetObjectField(TEXT("v"), Value) || !PinType(*Value, ValueType, Why))
				{
					return false;
				}
				Out.PinValueType = FEdGraphTerminalType::FromPinType(ValueType);
			}
		}
		return true;
	}

	// The class a plan names: this Blueprint's own class is its skeleton while it is being edited.
	UClass* ClassOf(UBlueprint* Blueprint, const FString& Path)
	{
		if (Path.IsEmpty() || (Blueprint->GeneratedClass && Path == Blueprint->GeneratedClass->GetPathName()))
		{
			return Blueprint->SkeletonGeneratedClass;
		}
		UClass* Class = LoadObject<UClass>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (UBlueprint* Other = Class ? Cast<UBlueprint>(Class->ClassGeneratedBy) : nullptr)
		{
			return Other->SkeletonGeneratedClass ? Other->SkeletonGeneratedClass : Class;
		}
		return Class;
	}

	template <typename T>
	T* Place(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Record)
	{
		// Transactional, as the editor makes its own nodes: without the flag an undo does not put back
		// a node a user changed. The node's ID is given with all the others (P4Ids::Name).
		T* Node = NewObject<T>(Graph, NAME_None, RF_Transactional);
		Graph->AddNode(Node, false, false);
		return Node;
	}

	void UserPins(UK2Node_EditablePinBase* Node, const TSharedPtr<FJsonObject>& Record, const TCHAR* Field,
		EEdGraphPinDirection Direction, FReport& Report)
	{
		const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
		if (!Record->TryGetArrayField(Field, List))
		{
			return;
		}
		for (const TSharedPtr<FJsonValue>& Item : *List)
		{
			const TSharedPtr<FJsonObject> Pin = Item->AsObject();
			FEdGraphPinType Type;
			FString Why;
			if (!PinType(Pin->GetObjectField(TEXT("type")), Type, Why))
			{
				Report.Problem(FString::Printf(TEXT("pin %s: %s"), *Pin->GetStringField(TEXT("name")), *Why));
				continue;
			}
			bool bReference = false, bConst = false;
			Pin->TryGetBoolField(TEXT("ref"), bReference);
			Pin->TryGetBoolField(TEXT("const"), bConst);
			Type.bIsReference = bReference;
			Type.bIsConst = bConst;
			Node->CreateUserDefinedPin(FName(*Pin->GetStringField(TEXT("name"))), Type, Direction, false);
		}
	}

	UEdGraph* FindGraph(UBlueprint* Blueprint, const FString& Name, const FString& Kind)
	{
		if (Kind == TEXT("event"))
		{
			return FBlueprintEditorUtils::FindEventGraph(Blueprint);
		}
		for (UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			if (Graph && Graph->GetName() == Name)
			{
				return Graph;
			}
		}
		return nullptr;
	}

	UK2Node_Event* FindEvent(UEdGraph* Graph, const FString& Name)
	{
		TArray<UK2Node_Event*> Events;
		Graph->GetNodesOfClass(Events);
		for (UK2Node_Event* Event : Events)
		{
			if (Event->GetFunctionName() == FName(*Name))
			{
				return Event;
			}
		}
		return nullptr;
	}

	// A pin by the name the plan uses. Cast and struct nodes name pins after types, selects after the index type.
	UEdGraphPin* Pin(UEdGraphNode* Node, const FString& Name, EEdGraphPinDirection Direction)
	{
		if (UK2Node_DynamicCast* Cast = ::Cast<UK2Node_DynamicCast>(Node))
		{
			if (Name == TEXT("As")) { return Cast->GetCastResultPin(); }
			if (Name == TEXT("Object") || Name == TEXT("Class")) { return Cast->GetCastSourcePin(); }
			if (Name == TEXT("bSuccess")) { return Cast->GetBoolSuccessPin(); }
			if (Name == TEXT("CastFailed")) { return Cast->GetInvalidCastPin(); }
			if (Name == TEXT("then")) { return Cast->GetValidCastPin(); }
		}
		if (UK2Node_Select* Select = ::Cast<UK2Node_Select>(Node))
		{
			if (Name == TEXT("Index")) { return Select->GetIndexPin(); }
			if (Name == TEXT("ReturnValue")) { return Select->GetReturnValuePin(); }
			if (Name.StartsWith(TEXT("Option ")))
			{
				TArray<UEdGraphPin*> Options;
				Select->GetOptionPins(Options);
				const int32 Index = FCString::Atoi(*Name.Mid(7));
				return Options.IsValidIndex(Index) ? Options[Index] : nullptr;
			}
		}
		if (UK2Node_Switch* Switch = ::Cast<UK2Node_Switch>(Node))
		{
			if (Name == TEXT("Selection")) { return Switch->GetSelectionPin(); }
			if (Name == TEXT("default")) { return Switch->GetDefaultPin(); }
			// A case of a switch on an enum is the entry's number in the plan and the entry's name on the node.
			UK2Node_SwitchEnum* ByEnum = ::Cast<UK2Node_SwitchEnum>(Node);
			if (ByEnum && ByEnum->Enum && Name.IsNumeric())
			{
				return Node->FindPin(FName(*ByEnum->Enum->GetNameStringByValue(FCString::Atoi64(*Name))), EGPD_Output);
			}
		}
		if (UK2Node_BreakStruct* Break = ::Cast<UK2Node_BreakStruct>(Node))
		{
			if (Direction == EGPD_Input)
			{
				for (UEdGraphPin* Each : Node->Pins)
				{
					if (Each->Direction == EGPD_Input) { return Each; }
				}
			}
		}
		if (::Cast<UK2Node_MacroInstance>(Node))
		{
			// By name (a loop's Array, LoopBody, Array Element, Completed, Break), else by role: the
			// execution pins, and the one value in and the one value out.
			for (UEdGraphPin* Each : Node->Pins)
			{
				if (Each->Direction == Direction && Each->PinName.ToString().Replace(TEXT(" "), TEXT("")).Equals(Name.Replace(TEXT(" "), TEXT("")), ESearchCase::IgnoreCase))
				{
					return Each;
				}
			}
			if (Name != TEXT("execute") && Name != TEXT("then") && Name != TEXT("in") && Name != TEXT("out"))
			{
				return nullptr;
			}
			const bool bExec = Name == TEXT("execute") || Name == TEXT("then");
			for (UEdGraphPin* Each : Node->Pins)
			{
				if (Each->Direction == Direction && (Each->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) == bExec)
				{
					return Each;
				}
			}
			return nullptr;
		}
		if (UK2Node_GetArrayItem* Item = ::Cast<UK2Node_GetArrayItem>(Node))
		{
			if (Name == TEXT("Array")) { return Item->GetTargetArrayPin(); }
			if (Name == TEXT("Dimension 1")) { return Item->GetIndexPin(); }
			if (Name == TEXT("Output")) { return Item->GetResultPin(); }
		}
		for (UEdGraphPin* Each : Node->Pins)
		{
			if (Each->Direction == Direction && Each->PinName.ToString().Equals(Name, ESearchCase::IgnoreCase))
			{
				return Each;
			}
		}
		if (Direction == EGPD_Output && Name == TEXT("ReturnValue"))
		{
			// The only data output of a node with one (a function whose return value has another name).
			UEdGraphPin* Only = nullptr;
			for (UEdGraphPin* Each : Node->Pins)
			{
				if (Each->Direction == EGPD_Output && Each->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
				{
					if (Only) { return nullptr; }
					Only = Each;
				}
			}
			return Only;
		}
		return nullptr;
	}

	void DeclareGraph(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Record, FReport& Report)
	{
		const FString Name = Record->GetStringField(TEXT("name"));
		const FString Kind = Record->GetStringField(TEXT("kind"));
		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		if (Kind == TEXT("event"))
		{
			UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
			if (!Graph)
			{
				Report.Stop(TEXT("this Blueprint has no event graph"));
				return;
			}
			// The editor puts greyed-out BeginPlay, Tick and overlap events in a new graph.
			for (UEdGraphNode* Old : TArray<UEdGraphNode*>(Graph->Nodes))
			{
				// ReuseBlueprint marks independent creator additions before source nodes
				// are rebuilt. Fresh factory placeholder events have no creator marker.
				if (Old && FString(Old->GetOutermost()->GetMetaData()->GetValue(Old, TEXT("P4BlueprintCreatorNode"))) != TEXT("1")) Graph->RemoveNode(Old);
			}
			for (const TSharedPtr<FJsonValue>& Item : Record->GetArrayField(TEXT("nodes")))
			{
				const TSharedPtr<FJsonObject> Node = Item->AsObject();
				const FString Type = Node->GetStringField(TEXT("t"));
				const FString Function = Node->HasField(TEXT("fn")) ? Node->GetStringField(TEXT("fn")) : FString();
				if (Type == TEXT("event"))
				{
					UClass* Owner = ClassOf(Blueprint, Node->GetStringField(TEXT("cls")));
					UFunction* Signature = Owner ? Owner->FindFunctionByName(FName(*Function)) : nullptr;
					if (!Signature)
					{
						Report.Problem(FString::Printf(TEXT("event %s: no such function in %s"), *Function, *Node->GetStringField(TEXT("cls"))));
						continue;
					}
					UK2Node_Event* Event = Place<UK2Node_Event>(Graph, Node);
					Event->EventReference.SetFromField<UFunction>(Signature, false);
					Event->bOverrideFunction = true;
					Event->AllocateDefaultPins();
					Report.Count(TEXT("events"));
				}
				else if (Type == TEXT("custom_event"))
				{
					UK2Node_CustomEvent* Event = Place<UK2Node_CustomEvent>(Graph, Node);
					Event->CustomFunctionName = FName(*Function);
					Event->AllocateDefaultPins();
					UserPins(Event, Node, TEXT("inputs"), EGPD_Output, Report);
					Report.Count(TEXT("events"));
				}
				else if (Type == TEXT("bound_event"))
				{
					const FName Component(*Node->GetStringField(TEXT("component")));
					const FString Delegate = Node->GetStringField(TEXT("delegate")) + TEXT("__DelegateSignature");
					FString DelegateProperty;
					Node->TryGetStringField(TEXT("delegate_property"), DelegateProperty);
					FObjectProperty* Property = FindFProperty<FObjectProperty>(Blueprint->SkeletonGeneratedClass, Component);
					FMulticastDelegateProperty* Found = nullptr;
					for (TFieldIterator<FMulticastDelegateProperty> It(Property ? Property->PropertyClass : nullptr); It && !Found; ++It)
					{
						if (It->SignatureFunction && It->SignatureFunction->GetName() == Delegate
							&& (DelegateProperty.IsEmpty() || It->GetName() == DelegateProperty))
						{
							Found = *It;
						}
					}
					if (!Found && !Property && DelegateProperty.IsEmpty())
					{
						// The game's own Blueprint has an event for a component that is no longer there (the
						// component was renamed or removed and the event node left behind). The compiler
						// still makes the event's function, so the node is made the same way: bound to the
						// first native class that has a delegate of that signature.
						for (TObjectIterator<UClass> Class; Class && !Found; ++Class)
						{
							if (!Class->HasAnyClassFlags(CLASS_Native))
							{
								continue;
							}
							for (TFieldIterator<FMulticastDelegateProperty> It(*Class, EFieldIteratorFlags::ExcludeSuper); It && !Found; ++It)
							{
								if (It->SignatureFunction && It->SignatureFunction->GetName() == Delegate)
								{
									Found = *It;
								}
							}
						}
						if (Found)
						{
							UK2Node_ComponentBoundEvent* Event = Place<UK2Node_ComponentBoundEvent>(Graph, Node);
							Event->ComponentPropertyName = Component;
							Event->DelegatePropertyName = Found->GetFName();
							Event->DelegateOwnerClass = CastChecked<UClass>(Found->GetOwner<UObject>())->GetAuthoritativeClass();
							Event->EventReference.SetFromField<UFunction>(Found->SignatureFunction, false);
							Event->CustomFunctionName = FName(*Function);
							Event->bOverrideFunction = false;
							Event->bInternalEvent = true;
							Event->AllocateDefaultPins();
							Report.Count(TEXT("events for a component that is not there"));
							continue;
						}
					}
					if (!Found)
					{
						const FString Why = FString::Printf(TEXT("event %s: %s"), *Function, Property
							? *FString::Printf(TEXT("class %s of component %s has no delegate %s"), *Property->PropertyClass->GetName(), *Component.ToString(), *Delegate)
							: *FString::Printf(TEXT("no delegate %s for the missing component %s"), *Delegate, *Component.ToString()));
						// An explicit cooked binding cannot continue as an inferred or
						// missing event. Stop before the runner reaches its graph pass.
						if (DelegateProperty.IsEmpty()) Report.Problem(Why);
						else Report.Stop(Why + TEXT(" (source delegate property ") + DelegateProperty + TEXT(")"));
						continue;
					}
					UK2Node_ComponentBoundEvent* Event = Place<UK2Node_ComponentBoundEvent>(Graph, Node);
					Event->InitializeComponentBoundEventParams(Property, Found);
					Event->CustomFunctionName = FName(*Function);
					Event->AllocateDefaultPins();
					Report.Count(TEXT("events"));
				}
			}
			return;
		}
		UEdGraph* Graph = FindGraph(Blueprint, Name, Kind);
		FString Override;
		Record->TryGetStringField(TEXT("override"), Override);
		if (!Graph)
		{
			Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, FName(*Name), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
			UClass* From = nullptr;
			if (!Override.IsEmpty())
			{
				FString OwnerPath, Function;
				Override.Split(TEXT("."), &OwnerPath, &Function, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
				From = ClassOf(Blueprint, OwnerPath);
				if (!From || !From->FindFunctionByName(FName(*Name)))
				{
					Report.Problem(FString::Printf(TEXT("function %s: the function it overrides (%s) is not in the project"), *Name, *Override));
					From = nullptr;
				}
			}
			FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, Graph, From == nullptr, From);
			if (!From)
			{
				TArray<UK2Node_FunctionEntry*> Entries;
				Graph->GetNodesOfClass(Entries);
				if (Entries.Num())
				{
					UserPins(Entries[0], Record, TEXT("inputs"), EGPD_Output, Report);
					const uint32 Flags = (uint32)P4Plan::Flags(Record, TEXT("flags"));
					Entries[0]->AddExtraFlags(Flags & (FUNC_BlueprintPure | FUNC_Const | FUNC_Private | FUNC_Protected));
					if (!(Flags & FUNC_Public))
					{
						Entries[0]->ClearExtraFlags(FUNC_Public);   // private, protected, or held without an access flag
					}
					const TArray<TSharedPtr<FJsonValue>>* Outputs = nullptr;
					if (Record->TryGetArrayField(TEXT("outputs"), Outputs) && Outputs->Num())
					{
						UK2Node_FunctionResult* Result = FBlueprintEditorUtils::FindOrCreateFunctionResultNode(Entries[0]);
						UserPins(Result, Record, TEXT("outputs"), EGPD_Input, Report);
					}
				}
			}
		}
		for (const TSharedPtr<FJsonValue>& Item : Record->GetArrayField(TEXT("locals")))
		{
			const TSharedPtr<FJsonObject> Local = Item->AsObject();
			FEdGraphPinType Type;
			FString Why;
			FString Default;
			Local->TryGetStringField(TEXT("default"), Default);
			if (!PinType(Local->GetObjectField(TEXT("type")), Type, Why) ||
				!FBlueprintEditorUtils::AddLocalVariable(Blueprint, Graph, FName(*Local->GetStringField(TEXT("name"))), Type, Default))
			{
				Report.Problem(FString::Printf(TEXT("function %s: local %s was not made (%s)"), *Name, *Local->GetStringField(TEXT("name")), *Why));
			}
		}
		Report.Count(TEXT("function graphs"));
	}

	void SetVariable(UK2Node_Variable* Node, UBlueprint* Blueprint, UEdGraph* Graph, const TSharedPtr<FJsonObject>& Record)
	{
		const FName Name(*Record->GetStringField(TEXT("var")));
		bool bLocal = false;
		Record->TryGetBoolField(TEXT("local"), bLocal);
		FString ClassPath;
		if (bLocal)
		{
			const FGuid Guid = FBlueprintEditorUtils::FindLocalVariableGuidByName(Blueprint, Graph, Name);
			if (Guid.IsValid())
			{
				Node->VariableReference.SetLocalMember(Name, Graph->GetName(), Guid);
				return;
			}
		}
		if (Record->TryGetStringField(TEXT("cls"), ClassPath))
		{
			UClass* Owner = ClassOf(Blueprint, ClassPath);
			if (Owner && !Blueprint->SkeletonGeneratedClass->IsChildOf(Owner))
			{
				Node->VariableReference.SetExternalMember(Name, Owner);
				return;
			}
		}
		Node->VariableReference.SetSelfMember(Name);
	}

	// The return nodes of the graph being built that a plan node already stands for.
	TSet<UEdGraphNode*> ResultsTaken;

	// The Math Expression nodes made for the Blueprint being built, by the variable the game's code
	// keeps each one's result in: a plan names such a node once for every node that takes its value.
	TMap<FString, TWeakObjectPtr<UEdGraphNode>> MathNodes;

	void ForgetShared()
	{
		MathNodes.Reset();
	}

	UEdGraphNode* Make(UBlueprint* Blueprint, UEdGraph* Graph, const TSharedPtr<FJsonObject>& Record, FReport& Report)
	{
		const FString Type = Record->GetStringField(TEXT("t"));
		if (Type == TEXT("entry"))
		{
			TArray<UK2Node_FunctionEntry*> Entries;
			Graph->GetNodesOfClass(Entries);
			return Entries.Num() ? Entries[0] : nullptr;
		}
		if (Type == TEXT("event") || Type == TEXT("custom_event") || Type == TEXT("bound_event"))
		{
			return FindEvent(Graph, Record->GetStringField(TEXT("fn")));
		}
		if (Type == TEXT("result"))
		{
			TArray<UK2Node_FunctionResult*> Results;
			Graph->GetNodesOfClass(Results);
			// The return node the function was declared with comes first, then one more for each further
			// place the function returns from. (Whether a node is free cannot be told from its pins: an
			// integer pin holds "0" from the start.)
			for (UK2Node_FunctionResult* Result : Results)
			{
				if (!ResultsTaken.Contains(Result))
				{
					ResultsTaken.Add(Result);
					return Result;
				}
			}
			if (!Results.Num())
			{
				// A function without outputs that has a Return node all the same.
				TArray<UK2Node_FunctionEntry*> Entries;
				Graph->GetNodesOfClass(Entries);
				UK2Node_FunctionResult* First = Entries.Num() ? FBlueprintEditorUtils::FindOrCreateFunctionResultNode(Entries[0]) : nullptr;
				if (First)
				{
					ResultsTaken.Add(First);
				}
				return First;
			}
			UK2Node_FunctionResult* More = Place<UK2Node_FunctionResult>(Graph, Record);
			More->FunctionReference = Results[0]->FunctionReference;
			for (const TSharedPtr<FUserPinInfo>& Info : Results[0]->UserDefinedPins)
			{
				// A copy for each return node: a pin's default value is kept in this record.
				More->UserDefinedPins.Add(MakeShared<FUserPinInfo>(*Info));
			}
			More->AllocateDefaultPins();
			ResultsTaken.Add(More);
			return More;
		}
		if (Type == TEXT("call") || Type == TEXT("call_parent"))
		{
			const FString Function = Record->GetStringField(TEXT("fn"));
			UClass* Owner = ClassOf(Blueprint, Record->GetStringField(TEXT("cls")));
			UFunction* Found = Owner ? Owner->FindFunctionByName(FName(*Function)) : nullptr;
			if (!Found)
			{
				Found = Blueprint->SkeletonGeneratedClass->FindFunctionByName(FName(*Function));
			}
			if (!Found)
			{
				Report.Problem(FString::Printf(TEXT("call %s: no such function in %s"), *Function, *Record->GetStringField(TEXT("cls"))));
				return nullptr;
			}
			const TSharedPtr<FJsonObject>* Getter = nullptr;
			UAnimBlueprint* Animation = Cast<UAnimBlueprint>(Blueprint);
			if (Animation && Record->TryGetObjectField(TEXT("getter"), Getter))
			{
				// A function the animation editor gives the numbers of a state machine and of a state or
				// transition: the node names them, and their pins are hidden.
				UK2Node_AnimGetter* Asked = Place<UK2Node_AnimGetter>(Graph, Record);
				UClass* Native = Animation->ParentClass;
				while (Native && !Native->HasAnyClassFlags(CLASS_Native))
				{
					Native = Native->GetSuperClass();
				}
				Asked->GetterClass = Native;
				Asked->SourceAnimBlueprint = Animation;
				Asked->SetFromFunction(Found);
				Asked->CachedTitle = FText::FromString(FString::Printf(TEXT("%s (%s)"), *Found->GetDisplayNameText().ToString(),
					*(*Getter)->GetStringField(TEXT("of"))));
				Asked->AllocateDefaultPins();
				Report.Getters.Emplace(Asked, *Getter);
				return Asked;
			}
			UK2Node_CallFunction* Node = Type == TEXT("call_parent") ? Place<UK2Node_CallParentFunction>(Graph, Record)
				: Found->HasMetaData(FBlueprintMetadata::MD_ArrayParam)
				? Place<UK2Node_CallArrayFunction>(Graph, Record) : Place<UK2Node_CallFunction>(Graph, Record);
			Node->SetFromFunction(Found);
			Node->AllocateDefaultPins();
			return Node;
		}
		if (Type == TEXT("get") || Type == TEXT("set"))
		{
			UK2Node_Variable* Node = Type == TEXT("get") ? static_cast<UK2Node_Variable*>(Place<UK2Node_VariableGet>(Graph, Record))
				: static_cast<UK2Node_Variable*>(Place<UK2Node_VariableSet>(Graph, Record));
			SetVariable(Node, Blueprint, Graph, Record);
			Node->AllocateDefaultPins();
			if (!Node->GetPropertyForVariable())
			{
				Report.Problem(FString::Printf(TEXT("variable %s was not found"), *Record->GetStringField(TEXT("var"))));
			}
			return Node;
		}
		if (Type == TEXT("branch"))
		{
			UK2Node_IfThenElse* Node = Place<UK2Node_IfThenElse>(Graph, Record);
			Node->AllocateDefaultPins();
			return Node;
		}
		if (Type == TEXT("sequence"))
		{
			UK2Node_ExecutionSequence* Node = Place<UK2Node_ExecutionSequence>(Graph, Record);
			Node->AllocateDefaultPins();
			return Node;
		}
		if (Type == TEXT("cast"))
		{
			UClass* Target = LoadObject<UClass>(nullptr, *Record->GetStringField(TEXT("cls")), nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (!Target)
			{
				Report.Problem(FString::Printf(TEXT("cast: class %s is not in the project"), *Record->GetStringField(TEXT("cls"))));
				return nullptr;
			}
			UK2Node_DynamicCast* Node = Record->GetBoolField(TEXT("meta"))
				? static_cast<UK2Node_DynamicCast*>(Place<UK2Node_ClassDynamicCast>(Graph, Record)) : Place<UK2Node_DynamicCast>(Graph, Record);
			Node->TargetType = Target;
			Node->SetPurity(Record->GetBoolField(TEXT("pure")));
			Node->AllocateDefaultPins();
			return Node;
		}
		if (Type == TEXT("select"))
		{
			UK2Node_Select* Node = Place<UK2Node_Select>(Graph, Record);
			Node->AllocateDefaultPins();
			for (int32 Have = 2; Have < (int32)Record->GetNumberField(TEXT("options")); ++Have)
			{
				Node->AddInputPin();
			}
			// The type of what is selected, when the plan knows it: constants alone do not tell the node.
			const TSharedPtr<FJsonObject>* Kind = nullptr;
			FEdGraphPinType Held;
			FString Why;
			if (Record->TryGetObjectField(TEXT("type"), Kind) && PinType(*Kind, Held, Why))
			{
				TArray<UEdGraphPin*> Options;
				Node->GetOptionPins(Options);
				Options.Add(Node->GetReturnValuePin());
				for (UEdGraphPin* Each : Options)
				{
					if (Each && Each->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard)
					{
						Each->PinType = Held;
					}
				}
			}
			return Node;
		}
		if (Type == TEXT("switch_name") || Type == TEXT("switch_string"))
		{
			// The case pins are the plan's names, in the order of their code.
			TArray<FName> Cases;
			for (const TSharedPtr<FJsonValue>& Item : Record->GetArrayField(TEXT("cases")))
			{
				Cases.Add(FName(*Item->AsString()));
			}
			if (Type == TEXT("switch_name"))
			{
				UK2Node_SwitchName* Node = Place<UK2Node_SwitchName>(Graph, Record);
				Node->PinNames = Cases;
				Node->AllocateDefaultPins();
				return Node;
			}
			UK2Node_SwitchString* Node = Place<UK2Node_SwitchString>(Graph, Record);
			Node->PinNames = Cases;
			bool bCase = false;
			Record->TryGetBoolField(TEXT("case_sensitive"), bCase);
			if (bCase)
			{
				// The node picks its comparison function when this setting changes.
				Node->bIsCaseSensitive = true;
				FPropertyChangedEvent Changed(FindFProperty<FProperty>(UK2Node_SwitchString::StaticClass(), TEXT("bIsCaseSensitive")));
				Node->PostEditChangeProperty(Changed);
			}
			Node->AllocateDefaultPins();
			return Node;
		}
		if (Type == TEXT("switch_enum"))
		{
			UEnum* Enum = LoadObject<UEnum>(nullptr, *Record->GetStringField(TEXT("enum")), nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (!Enum)
			{
				Report.Problem(FString::Printf(TEXT("enum %s is not in the project"), *Record->GetStringField(TEXT("enum"))));
				return nullptr;
			}
			UK2Node_SwitchEnum* Node = Place<UK2Node_SwitchEnum>(Graph, Record);
			// A pin for every entry: the node lists the enum's entries when it makes its pins. (Its own
			// SetEnum does the same and is not exported from the engine's module.)
			Node->Enum = Enum;
			bool bDefault = false;
			Record->TryGetBoolField(TEXT("has_default"), bDefault);
			Node->bHasDefaultPin = bDefault;
			Node->AllocateDefaultPins();
			return Node;
		}
		if (Type == TEXT("switch_int"))
		{
			UK2Node_SwitchInteger* Node = Place<UK2Node_SwitchInteger>(Graph, Record);
			Node->StartIndex = (int32)Record->GetNumberField(TEXT("start"));
			Node->AllocateDefaultPins();
			for (int32 Have = 0; Have < (int32)Record->GetNumberField(TEXT("cases")); ++Have)
			{
				Node->AddPinToSwitchNode();
			}
			return Node;
		}
		if (Type == TEXT("make_array"))
		{
			UK2Node_MakeArray* Node = Place<UK2Node_MakeArray>(Graph, Record);
			Node->NumInputs = FMath::Max(1, (int32)Record->GetNumberField(TEXT("inputs")));
			Node->AllocateDefaultPins();
			const TSharedPtr<FJsonObject>* Element = nullptr;
			FEdGraphPinType ElementType;
			FString Why;
			if (Record->TryGetObjectField(TEXT("type"), Element) && PinType(*Element, ElementType, Why))
			{
				for (UEdGraphPin* Each : Node->Pins)
				{
					Each->PinType = ElementType;
					Each->PinType.ContainerType = Each->Direction == EGPD_Output ? EPinContainerType::Array : EPinContainerType::None;
				}
			}
			return Node;
		}
		if (Type == TEXT("break_struct"))
		{
			UScriptStruct* Struct = LoadObject<UScriptStruct>(nullptr, *Record->GetStringField(TEXT("struct")), nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (!Struct)
			{
				Report.Problem(FString::Printf(TEXT("struct %s is not in the project"), *Record->GetStringField(TEXT("struct"))));
				return nullptr;
			}
			UK2Node_BreakStruct* Node = Place<UK2Node_BreakStruct>(Graph, Record);
			Node->StructType = Struct;
			Node->AllocateDefaultPins();
			return Node;
		}
		if (Type == TEXT("byte_to_enum"))
		{
			UEnum* Enum = LoadObject<UEnum>(nullptr, *Record->GetStringField(TEXT("enum")), nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (!Enum)
			{
				Report.Problem(FString::Printf(TEXT("enum %s is not in the project"), *Record->GetStringField(TEXT("enum"))));
				return nullptr;
			}
			UK2Node_CastByteToEnum* Node = Place<UK2Node_CastByteToEnum>(Graph, Record);
			Node->Enum = Enum;
			Node->bSafe = true;
			Node->AllocateDefaultPins();
			return Node;
		}
		if (Type == TEXT("make_struct"))
		{
			UScriptStruct* Struct = LoadObject<UScriptStruct>(nullptr, *Record->GetStringField(TEXT("struct")), nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (!Struct)
			{
				Report.Problem(FString::Printf(TEXT("struct %s is not in the project"), *Record->GetStringField(TEXT("struct"))));
				return nullptr;
			}
			UK2Node_MakeStruct* Node = Place<UK2Node_MakeStruct>(Graph, Record);
			Node->StructType = Struct;
			Node->bMadeAfterOverridePinRemoval = true;
			Node->AllocateDefaultPins();
			return Node;
		}
		if (Type == TEXT("temp_var"))
		{
			// The variable of a macro, as the node a macro graph holds it in.
			UK2Node_TemporaryVariable* Node = Place<UK2Node_TemporaryVariable>(Graph, Record);
			FString Why;
			if (!PinType(Record->GetObjectField(TEXT("type")), Node->VariableType, Why))
			{
				Report.Problem(FString::Printf(TEXT("macro variable %s: %s"), *Record->GetStringField(TEXT("var")), *Why));
			}
			Node->NodeComment = Record->GetStringField(TEXT("comment"));
			Node->AllocateDefaultPins();
			return Node;
		}
		if (Type == TEXT("assign"))
		{
			UK2Node_AssignmentStatement* Node = Place<UK2Node_AssignmentStatement>(Graph, Record);
			Node->AllocateDefaultPins();
			return Node;
		}
		if (Type == TEXT("math"))
		{
			// A Math Expression node: the editor writes its inner nodes from the text. One whose value
			// several nodes take is named once for each of them ("of": the variable the game's code keeps
			// its result in), and made once.
			FString Of;
			if (Record->TryGetStringField(TEXT("of"), Of))
			{
				UEdGraphNode* Known = MathNodes.FindRef(Of).Get();
				if (Known && Known->GetGraph() == Graph)
				{
					return Known;   // whichever record comes first makes it; the one with its inputs wires them
				}
			}
			UK2Node_MathExpression* Node = Place<UK2Node_MathExpression>(Graph, Record);
			if (!Of.IsEmpty())
			{
				MathNodes.Add(Of, Node);
			}
			Node->PostPlacedNewNode();
			Node->AllocateDefaultPins();
			Node->Expression = Record->GetStringField(TEXT("expression"));
			FPropertyChangedEvent Changed(FindFProperty<FProperty>(UK2Node_MathExpression::StaticClass(), TEXT("Expression")));
			Node->PostEditChangeProperty(Changed);
			if (!Node->FindPin(TEXT("ReturnValue")))
			{
				Report.Problem(FString::Printf(TEXT("the editor did not take the expression %s"), *Node->Expression));
			}
			Report.Count(TEXT("math expressions"));
			return Node;
		}
		if (Type == TEXT("array_get"))
		{
			UK2Node_GetArrayItem* Node = Place<UK2Node_GetArrayItem>(Graph, Record);
			Node->AllocateDefaultPins();
			return Node;
		}
		if (Type == TEXT("macro"))
		{
			// One of the editor's standard macros (++, --).
			UBlueprint* Library = LoadObject<UBlueprint>(nullptr, TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros"));
			const FString Wanted = Record->GetStringField(TEXT("macro"));
			UEdGraph* Macro = nullptr;
			for (UEdGraph* Each : Library ? Library->MacroGraphs : TArray<UEdGraph*>())
			{
				if (Each && Each->GetName() == Wanted)
				{
					Macro = Each;
				}
			}
			if (!Macro)
			{
				Report.Problem(FString::Printf(TEXT("the standard macro %s was not found"), *Wanted));
				return nullptr;
			}
			UK2Node_MacroInstance* Node = Place<UK2Node_MacroInstance>(Graph, Record);
			Node->SetMacroGraph(Macro);
			Node->AllocateDefaultPins();
			return Node;
		}
		if (Type == TEXT("self"))
		{
			UK2Node_Self* Node = Place<UK2Node_Self>(Graph, Record);
			Node->AllocateDefaultPins();
			return Node;
		}
		Report.Problem(FString::Printf(TEXT("node kind %s is not made yet"), *Type));
		return nullptr;
	}

	void Defaults(UEdGraphNode* Node, const TSharedPtr<FJsonObject>& Record, const FString& Where, FReport& Report)
	{
		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		const TSharedPtr<FJsonObject>* Texts = nullptr;
		if (Record->TryGetObjectField(TEXT("defaults"), Texts))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Texts)->Values)
			{
				UEdGraphPin* Target = Pin(Node, Pair.Key, EGPD_Input);
				if (!Target)
				{
					Report.Problem(FString::Printf(TEXT("%s: no input pin %s"), *Where, *Pair.Key));
					continue;
				}
				FString Text = Pair.Value->AsString();
				// The bytecode holds an enum value as a number, a pin holds its name.
				if (UEnum* Enum = Cast<UEnum>(Target->PinType.PinSubCategoryObject.Get()))
				{
					if (Text.IsNumeric())
					{
						Text = Enum->GetNameStringByValue(FCString::Atoi64(*Text));
					}
				}
				// The editor leaves a pin as it is when it finds the value wrong for the pin, and says
				// nothing: the same test is made here first, so the import says it.
				FString UseValue;
				UObject* UseObject = nullptr;
				FText UseText;
				Schema->GetPinDefaultValuesFromString(Target->PinType, Target->GetOwningNodeUnchecked(), Text, UseValue, UseObject, UseText);
				const FString Wrong = Schema->IsPinDefaultValid(Target, UseValue, UseObject, UseText);
				if (!Wrong.IsEmpty())
				{
					Report.Problem(FString::Printf(TEXT("%s: pin %s: the editor refused the value %s: %s"), *Where, *Pair.Key, *Text.Left(80), *Wrong.Left(120)));
					continue;
				}
				Schema->TrySetDefaultValue(*Target, Text);
			}
		}
		if (Record->TryGetObjectField(TEXT("objects"), Texts))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Texts)->Values)
			{
				UEdGraphPin* Target = Pin(Node, Pair.Key, EGPD_Input);
				UObject* Object = StaticLoadObject(UObject::StaticClass(), nullptr, *Pair.Value->AsString(), nullptr, LOAD_NoWarn | LOAD_Quiet);
				// A game asset that stays the game's: the pin is left empty and the asset is told.
				if (Target && !Object && P4KeepAsTheGames(Pair.Value->AsString(), Node->GetOutermost()->GetName() + TEXT(" ") + Where + TEXT(": pin ") + Pair.Key))
				{
					Report.Count(TEXT("pins naming game assets kept as the game's"));
					continue;
				}
				if (!Target || !Object)
				{
					Report.Problem(FString::Printf(TEXT("%s: pin %s did not take %s"), *Where, *Pair.Key, *Pair.Value->AsString()));
					continue;
				}
				Schema->TrySetDefaultObject(*Target, Object);
			}
		}
	}

	void BuildGraph(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Record, FReport& Report)
	{
		const FString Name = Record->GetStringField(TEXT("name"));
		UEdGraph* Graph = FindGraph(Blueprint, Name, Record->GetStringField(TEXT("kind")));
		if (!Graph)
		{
			Report.Problem(FString::Printf(TEXT("graph %s was not declared"), *Name));
			return;
		}
		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		TMap<FString, UEdGraphNode*> Nodes;
		ResultsTaken.Reset();
		if (Record->GetStringField(TEXT("kind")) != TEXT("event"))
		{
			// The editor puts a call of the parent's function into some new graphs (a child Blueprint's
			// construction script). The game's code says which nodes the graph has, so those go.
			for (UEdGraphNode* Old : TArray<UEdGraphNode*>(Graph->Nodes))
			{
				if (Old && !Old->IsA<UK2Node_FunctionEntry>() && !Old->IsA<UK2Node_FunctionResult>() &&
                    FString(Old->GetOutermost()->GetMetaData()->GetValue(Old, TEXT("P4BlueprintCreatorNode"))) != TEXT("1"))
				{
					Graph->RemoveNode(Old);
				}
			}
		}
		for (const TSharedPtr<FJsonValue>& Item : Record->GetArrayField(TEXT("nodes")))
		{
			const TSharedPtr<FJsonObject> Node = Item->AsObject();
			UEdGraphNode* Made = Make(Blueprint, Graph, Node, Report);
			if (!Made)
			{
				Report.Problem(FString::Printf(TEXT("%s: node %s (%s) was not made"), *Name, *Node->GetStringField(TEXT("id")), *Node->GetStringField(TEXT("t"))));
				continue;
			}
			Nodes.Add(Node->GetStringField(TEXT("id")), Made);
			Report.Count(TEXT("nodes"));
		}
		// Wires first: a wildcard pin (an array, a select) takes its type from what it is wired to, and a
		// default value needs that type.
		TSet<FString> Replaced;   // nodes of the plan made as another kind of node: their constants are set already
		for (const TSharedPtr<FJsonValue>& Item : Record->GetArrayField(TEXT("links")))
		{
			const TArray<TSharedPtr<FJsonValue>>& Link = Item->AsArray();
			UEdGraphNode** From = Nodes.Find(Link[0]->AsString());
			UEdGraphNode** To = Nodes.Find(Link[2]->AsString());
			UEdGraphPin* Out = From ? Pin(*From, Link[1]->AsString(), EGPD_Output) : nullptr;
			UEdGraphPin* In = To ? Pin(*To, Link[3]->AsString(), EGPD_Input) : nullptr;
			if (!Out || !In)
			{
				Report.Problem(FString::Printf(TEXT("%s: no pin for the wire %s.%s to %s.%s"), *Name, *Link[0]->AsString(),
					*Link[1]->AsString(), *Link[2]->AsString(), *Link[3]->AsString()));
				continue;
			}
			const FPinConnectionResponse Response = Schema->CanCreateConnection(Out, In);
			UEnum* Wanted = Cast<UEnum>(In->PinType.PinSubCategoryObject.Get());
			if (Response.Response == CONNECT_RESPONSE_MAKE_WITH_CONVERSION_NODE && Wanted &&
				Out->PinType.PinCategory == UEdGraphSchema_K2::PC_Byte && !Out->PinType.PinSubCategoryObject.IsValid())
			{
				// A plain byte into a pin of an enum. A "literal enum" node compiles to a call of
				// MakeLiteralByte with a constant: that call is the node. Any other byte goes through
				// a "byte to enum" node of the kind that adds no code.
				UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Out->GetOwningNode());
				FString Number;
				for (const TSharedPtr<FJsonValue>& Each : Record->GetArrayField(TEXT("nodes")))
				{
					const TSharedPtr<FJsonObject>* Held = nullptr;
					if (Each->AsObject()->GetStringField(TEXT("id")) == Link[0]->AsString() && Each->AsObject()->TryGetObjectField(TEXT("defaults"), Held))
					{
						(*Held)->TryGetStringField(TEXT("Value"), Number);
					}
				}
				if (Call && Call->FunctionReference.GetMemberName() == FName(TEXT("MakeLiteralByte")) && !Number.IsEmpty())
				{
					UK2Node_EnumLiteral* Literal = NewObject<UK2Node_EnumLiteral>(Graph, NAME_None, RF_Transactional);
					Literal->Enum = Wanted;
					Graph->AddNode(Literal, false, false);
					Literal->AllocateDefaultPins();
					if (UEdGraphPin* Value = Literal->FindPin(UK2Node_EnumLiteral::GetEnumInputPinName()))
					{
						Value->DefaultValue = Wanted->GetNameStringByValue(FCString::Atoi(*Number));
					}
					Graph->RemoveNode(Call);
					Nodes.Add(Link[0]->AsString(), Literal);
					Replaced.Add(Link[0]->AsString());
					Out = Literal->FindPin(UEdGraphSchema_K2::PN_ReturnValue);
				}
				else
				{
					UK2Node_CastByteToEnum* Convert = NewObject<UK2Node_CastByteToEnum>(Graph, NAME_None, RF_Transactional);
					Convert->Enum = Wanted;
					Convert->bSafe = false;
					Graph->AddNode(Convert, false, false);
					Convert->AllocateDefaultPins();
					Schema->TryCreateConnection(Out, Convert->FindPin(FName(TEXT("Byte"))));
					Out = Convert->FindPin(UEdGraphSchema_K2::PN_ReturnValue);
				}
				if (Out && Schema->TryCreateConnection(Out, In))
				{
					Report.Count(TEXT("wires"));
					continue;
				}
			}
			if (Response.Response == CONNECT_RESPONSE_DISALLOW || Response.Response == CONNECT_RESPONSE_MAKE_WITH_CONVERSION_NODE)
			{
				Report.Problem(FString::Printf(TEXT("%s: the wire %s.%s to %s.%s is refused: %s"), *Name, *Link[0]->AsString(),
					*Link[1]->AsString(), *Link[2]->AsString(), *Link[3]->AsString(), *Response.Message.ToString()));
				continue;
			}
			Schema->TryCreateConnection(Out, In);
			Report.Count(TEXT("wires"));
		}
		for (const TSharedPtr<FJsonValue>& Item : Record->GetArrayField(TEXT("nodes")))
		{
			const TSharedPtr<FJsonObject> Node = Item->AsObject();
			UEdGraphNode** Made = Nodes.Find(Node->GetStringField(TEXT("id")));
			if (Made && !Replaced.Contains(Node->GetStringField(TEXT("id"))))
			{
				Defaults(*Made, Node, Name, Report);
			}
		}
		// The game has no positions, but retained creator nodes do. The source layout
		// walks the whole graph, including independent additions that reimport keeps.
		TMap<UEdGraphNode*, FIntPoint> CreatorPositions;
		for (UEdGraphNode* Node : Graph->Nodes)
			if (Node && FString(Node->GetOutermost()->GetMetaData()->GetValue(Node, TEXT("P4BlueprintCreatorNode"))) == TEXT("1"))
				CreatorPositions.Add(Node, FIntPoint(Node->NodePosX, Node->NodePosY));
		P4Layout::Graph(Graph);
		for (const auto& Position : CreatorPositions)
		{
			Position.Key->NodePosX = Position.Value.X;
			Position.Key->NodePosY = Position.Value.Y;
		}
		// A node that runs (it has a white input pin) and that nothing leads to is a mistake of the
		// import, or code the game's compiler kept that cannot be reached. Either way it is reported.
		bool bBody = true;
		Record->TryGetBoolField(TEXT("body"), bBody);
		for (UEdGraphNode* Each : Graph->Nodes)
		{
			UEdGraphPin* Execute = bBody && Each ? Each->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input) : nullptr;
			if (Execute && Execute->LinkedTo.Num() == 0)
			{
				Report.Problem(FString::Printf(TEXT("%s: nothing leads to the node %s"), *Name, *Each->GetNodeTitle(ENodeTitleType::ListView).ToString()));
			}
		}
	}
}

FString UP4BlueprintImport::Signatures(const FString& PlanFile)
{
	P4Graphs::FReport Report;
	const TSharedPtr<FJsonObject> Plan = P4Graphs::LoadPlan(PlanFile, Report);
	UBlueprint* Blueprint = Plan.IsValid() ? P4Graphs::Find(Plan) : nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
	if (!Blueprint || !Plan->TryGetArrayField(TEXT("graphs"), Graphs))
	{
		if (Plan.IsValid() && !Blueprint)
		{
			Report.Stop(TEXT("the Blueprint is not in the project"));
		}
		return Report.Text();
	}
	for (const TSharedPtr<FJsonValue>& Item : *Graphs)
	{
		P4Graphs::DeclareGraph(Blueprint, Item->AsObject(), Report);
	}
	TArray<UEdGraph*> DeclaredGraphs;
	Blueprint->GetAllGraphs(DeclaredGraphs);
	for (const TSharedPtr<FJsonValue>& Item : *Graphs)
		for (UEdGraph* Graph : DeclaredGraphs)
			if (Graph && Graph->GetName() == Item->AsObject()->GetStringField(TEXT("name")))
				Graph->GetOutermost()->GetMetaData()->SetValue(Graph, TEXT("P4BlueprintSourceGraph"), TEXT("1"));
	// Before anything names them: another Blueprint's call node holds the ID of the function graph or
	// custom event it calls, and a node that reads a local variable holds the variable's.
	P4Ids::Name(Blueprint);
	P4Reimport::WithCompilerSavePolicy([&]() { FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint); });
	return Report.Text();
}

FString UP4BlueprintImport::Graphs(const FString& PlanFile)
{
	P4Graphs::FReport Report;
	const TSharedPtr<FJsonObject> Plan = P4Graphs::LoadPlan(PlanFile, Report);
	UBlueprint* Blueprint = Plan.IsValid() ? P4Graphs::Find(Plan) : nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
	if (!Blueprint || !Plan->TryGetArrayField(TEXT("graphs"), Graphs))
	{
		return Report.Text();
	}
	P4Graphs::ForgetShared();
	for (const TSharedPtr<FJsonValue>& Item : *Graphs)
	{
		P4Graphs::BuildGraph(Blueprint, Item->AsObject(), Report);
	}
	P4Ids::Name(Blueprint);
	FCompilerResultsLog Log;
	Log.bSilentMode = true;
	Log.bAnnotateMentionedNodes = false;
	P4Reimport::WithCompilerSavePolicy([&]() {
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection | EBlueprintCompileOptions::SkipSave, &Log);
	});
	P4Ids::NameBindings(Blueprint);
	int32 Shown = 0;
	for (const TSharedRef<FTokenizedMessage>& Message : Log.Messages)
	{
		if (Message->GetSeverity() == EMessageSeverity::Error && Shown++ < 12)
		{
			Report.Problem(FString::Printf(TEXT("compiler: %s"), *Message->ToText().ToString()));
		}
	}
	if (Blueprint->Status == BS_Error)
	{
		Report.bStopped = true;
	}
	Report.Count(TEXT("compiler errors"), Log.NumErrors);
	Report.Count(TEXT("compiler warnings"), Log.NumWarnings);
	Blueprint->MarkPackageDirty();
	return Report.Text();
}

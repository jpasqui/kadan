// The IDs a Blueprint asset stores, made from names. See the header.

#include "P4BlueprintIds.h"
#include "AnimGraphNode_Base.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/DynamicBlueprintBinding.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "Misc/SecureHash.h"
#include "UObject/Package.h"
#include "UObject/MetaData.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

namespace
{
	FString Base(const UBlueprint* Blueprint)
	{
		return Blueprint->GetPathName().ToLower();   // engine names do not tell upper case from lower
	}

	void NameGraph(UEdGraph* Graph, const FString& Where, TSet<UEdGraph*>& Seen)
	{
		if (!Graph || Seen.Contains(Graph))
		{
			return;
		}
		Seen.Add(Graph);
		Graph->GraphGuid = P4Ids::From(Where);
		TSet<FGuid> Given;
		TMap<UObject*, FString> Named;
		int32 Number = 0;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node)
			{
				continue;
			}
			if (FString(Node->GetOutermost()->GetMetaData()->GetValue(Node, TEXT("P4BlueprintCreatorNode"))) == TEXT("1"))
			{ Given.Add(Node->NodeGuid); continue; }
			FString Key;
			if (const UK2Node_Event* Event = Cast<UK2Node_Event>(Node))
			{
				// Another Blueprint that calls a custom event names it by this ID.
				Key = TEXT("event ") + Event->GetFunctionName().ToString();
			}
			else if (Node->IsA<UAnimGraphNode_Base>())
			{
				Key = TEXT("animation node ") + Node->GetName();   // the name the game's class gives it
			}
			else if (Node->IsA<UK2Node_FunctionEntry>())
			{
				Key = TEXT("entry");
			}
			else
			{
				Key = FString::Printf(TEXT("node %d %s"), Number++, *Node->GetClass()->GetName());
			}
			FString Whole = Where + TEXT(" ") + Key.ToLower();
			FGuid Id = P4Ids::From(Whole);
			for (int32 Again = 2; Given.Contains(Id); ++Again)   // no two nodes of a graph share an ID
			{
				Whole = FString::Printf(TEXT("%s %s %d"), *Where, *Key.ToLower(), Again);
				Id = P4Ids::From(Whole);
			}
			Given.Add(Id);
			Node->NodeGuid = Id;
			Named.Add(Node, Whole);
			if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node))
			{
				for (FBPVariableDescription& Local : Entry->LocalVariables)
				{
					Local.VarGuid = P4Ids::From(Where + TEXT(" local ") + Local.VarName.ToString().ToLower());
				}
			}
			// A notify a state or transition sends has an ID, which the compiler would make at random.
			auto NameNotify = [&Whole](FAnimNotifyEvent& Notify, const TCHAR* When)
			{
				if (Notify.NotifyName != NAME_None)
				{
					Notify.Guid = P4Ids::From(Whole + TEXT(" notify ") + When);
				}
			};
			if (UAnimStateNode* State = Cast<UAnimStateNode>(Node))
			{
				NameNotify(State->StateEntered, TEXT("entered"));
				NameNotify(State->StateLeft, TEXT("left"));
				NameNotify(State->StateFullyBlended, TEXT("fully blended"));
			}
			if (UAnimStateTransitionNode* Transition = Cast<UAnimStateTransitionNode>(Node))
			{
				NameNotify(Transition->TransitionStart, TEXT("start"));
				NameNotify(Transition->TransitionEnd, TEXT("end"));
				NameNotify(Transition->TransitionInterrupt, TEXT("interrupt"));
				// Transitions that share one rule graph hold one ID for it, with its name.
				if (Transition->bSharedRules)
				{
					Transition->SharedRulesGuid = P4Ids::From(Where + TEXT(" shared rule ") + Transition->SharedRulesName.ToLower());
				}
				if (Transition->bSharedCrossfade)
				{
					Transition->SharedCrossfadeGuid = P4Ids::From(Where + TEXT(" shared crossfade ") + Transition->SharedCrossfadeName.ToLower());
				}
			}
			TMap<FString, int32> Pins;
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin)
				{
					const FString Side = FString::Printf(TEXT(" %s %s"), Pin->Direction == EGPD_Input ? TEXT("in") : TEXT("out"), *Pin->PinName.ToString().ToLower());
					Pin->PinId = P4Ids::From(Whole + Side + FString::Printf(TEXT(" %d"), Pins.FindOrAdd(Side)++));
				}
			}
		}
		// The graphs inside this one: each under the node that holds it (a state, a transition, a state machine).
		for (UEdGraph* Inner : TArray<UEdGraph*>(Graph->SubGraphs))
		{
			if (Inner)
			{
				const FString* Holder = Named.Find(Inner->GetOuter());
				NameGraph(Inner, (Holder ? *Holder : Where) + TEXT("/") + Inner->GetName().ToLower(), Seen);
			}
		}
	}

	// Every ID a value holds, by its place.
	void Scan(const FProperty* Property, const void* Address, const FString& Place, const TSharedRef<FJsonObject>& Out)
	{
		if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
		{
			if (Struct->Struct == TBaseStructure<FGuid>::Get())
			{
				const FGuid& Id = *static_cast<const FGuid*>(Address);
				if (Id.IsValid())
				{
					Out->SetStringField(Place, Id.ToString());
				}
				return;
			}
			for (TFieldIterator<FProperty> It(Struct->Struct); It; ++It)
			{
				for (int32 Index = 0; Index < It->ArrayDim; ++Index)
				{
					Scan(*It, It->ContainerPtrToValuePtr<void>(Address, Index),
						Place + TEXT(".") + It->GetName() + (It->ArrayDim > 1 ? FString::Printf(TEXT("[%d]"), Index) : FString()), Out);
				}
			}
		}
		else if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
		{
			FScriptArrayHelper Elements(Array, Address);
			for (int32 Index = 0; Index < Elements.Num(); ++Index)
			{
				Scan(Array->Inner, Elements.GetRawPtr(Index), Place + FString::Printf(TEXT("[%d]"), Index), Out);
			}
		}
		else if (const FMapProperty* Map = CastField<FMapProperty>(Property))
		{
			FScriptMapHelper Pairs(Map, Address);
			int32 Number = 0;
			for (int32 Index = 0; Index < Pairs.GetMaxIndex(); ++Index)
			{
				if (Pairs.IsValidIndex(Index))
				{
					Scan(Map->KeyProp, Pairs.GetKeyPtr(Index), Place + FString::Printf(TEXT("{%d}key"), Number), Out);
					Scan(Map->ValueProp, Pairs.GetValuePtr(Index), Place + FString::Printf(TEXT("{%d}"), Number), Out);
					++Number;
				}
			}
		}
		else if (const FSetProperty* Set = CastField<FSetProperty>(Property))
		{
			FScriptSetHelper Elements(Set, Address);
			int32 Number = 0;
			for (int32 Index = 0; Index < Elements.GetMaxIndex(); ++Index)
			{
				if (Elements.IsValidIndex(Index))
				{
					Scan(Set->ElementProp, Elements.GetElementPtr(Index), Place + FString::Printf(TEXT("{%d}"), Number++), Out);
				}
			}
		}
	}
}

FGuid P4Ids::From(const FString& Text)
{
	const FTCHARToUTF8 Bytes(*Text);
	uint32 Hash[5];
	FSHA1::HashBuffer(Bytes.Get(), Bytes.Length(), reinterpret_cast<uint8*>(Hash));
	return FGuid(Hash[0], Hash[1], Hash[2], Hash[3]);
}

FGuid P4Ids::OfVariable(const UBlueprint* Blueprint, FName Name)
{
	return From(Base(Blueprint) + TEXT(" variable ") + Name.ToString().ToLower());
}

FGuid P4Ids::OfComponent(const UBlueprint* Blueprint, FName Name)
{
	return From(Base(Blueprint) + TEXT(" component ") + Name.ToString().ToLower());
}

void P4Ids::NameBindings(UBlueprint* Blueprint)
{
	UBlueprintGeneratedClass* Class = Blueprint ? Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass) : nullptr;
	if (!Class)
		return;
	for (UDynamicBlueprintBinding* Binding : Class->DynamicBindingObjects)
	{
		if (!Binding || Binding->GetOuter() != Class)
			continue;
		for (int32 Number = 0; Number < 64; ++Number)
		{
			const FName First(Binding->GetClass()->GetFName(), NAME_EXTERNAL_TO_INTERNAL(Number));
			if (Binding->GetFName() == First)
				break;
			if (!StaticFindObjectFast(nullptr, Class, First))
			{
				Binding->Rename(*First.ToString(), nullptr, REN_DontCreateRedirectors | REN_ForceNoResetLoaders | REN_DoNotDirty | REN_NonTransactional);
				break;
			}
		}
	}
}

void P4Ids::Name(UBlueprint* Blueprint)
{
	// The Blueprint's own ID. The engine has no call that sets it to a given value.
	if (FStructProperty* Own = FindFProperty<FStructProperty>(UBlueprintCore::StaticClass(), TEXT("BlueprintGuid")))
	{
		*Own->ContainerPtrToValuePtr<FGuid>(Blueprint) = From(Base(Blueprint) + TEXT(" blueprint"));
	}
	TSharedPtr<FJsonObject> Source;
	const FString SourceText = Blueprint->GetOutermost()->GetMetaData()->GetValue(Blueprint, TEXT("P4BlueprintSourceMembers"));
	if (!SourceText.IsEmpty()) FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(SourceText), Source);
	for (FBPVariableDescription& Variable : Blueprint->NewVariables)
	{
		if (!Source.IsValid() || !Variable.VarGuid.IsValid()) Variable.VarGuid = OfVariable(Blueprint, Variable.VarName);
	}
	const FString Where = Base(Blueprint) + TEXT(" graph ");
	TSet<UEdGraph*> Seen;
	TArray<UEdGraph*> Top;
	Top.Append(Blueprint->UbergraphPages);
	Top.Append(Blueprint->FunctionGraphs);
	Top.Append(Blueprint->MacroGraphs);
	Top.Append(Blueprint->DelegateSignatureGraphs);
	for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces)
	{
		Top.Append(Interface.Graphs);
	}
	for (UEdGraph* Graph : Top)
	{
		if (Graph)
		{
			if (!Source.IsValid() || !Graph->GraphGuid.IsValid() ||
                FString(Graph->GetOutermost()->GetMetaData()->GetValue(Graph, TEXT("P4BlueprintSourceGraph"))) == TEXT("1"))
                NameGraph(Graph, Where + Graph->GetName().ToLower(), Seen);
		}
	}
	TArray<UEdGraph*> All;
	Blueprint->GetAllGraphs(All);
	for (UEdGraph* Graph : All)   // a graph none of the above leads to, by its path in the Blueprint
	{
		if (Graph)
		{
			if (!Source.IsValid() || !Graph->GraphGuid.IsValid() ||
                FString(Graph->GetOutermost()->GetMetaData()->GetValue(Graph, TEXT("P4BlueprintSourceGraph"))) == TEXT("1"))
                NameGraph(Graph, Where + Graph->GetPathName(Blueprint).ToLower(), Seen);
		}
	}
}

TSharedRef<FJsonObject> P4Ids::Dump(UBlueprint* Blueprint)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> Ids = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> Pins = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Unrecorded;
	UPackage* Package = Blueprint->GetOutermost();
	TArray<UObject*> Objects;
	GetObjectsWithOuter(Package, Objects, true);
	int32 Nodes = 0;
	for (UObject* Object : Objects)
	{
		if (!Object || Object->HasAnyFlags(RF_Transient))
		{
			continue;
		}
		const FString Path = Object->GetPathName(Package);
		for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
		{
			for (int32 Index = 0; Index < It->ArrayDim; ++Index)
			{
				Scan(*It, It->ContainerPtrToValuePtr<void>(Object, Index),
					Path + TEXT(":") + It->GetName() + (It->ArrayDim > 1 ? FString::Printf(TEXT("[%d]"), Index) : FString()), Ids);
			}
		}
		if (UEdGraphNode* Node = Cast<UEdGraphNode>(Object))
		{
			++Nodes;
			if (!Node->HasAnyFlags(RF_Transactional))
			{
				Unrecorded.Add(MakeShared<FJsonValueString>(Path));
			}
			TMap<FString, int32> Count;
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin)
				{
					const FString Side = FString::Printf(TEXT("%s %s"), Pin->Direction == EGPD_Input ? TEXT("in") : TEXT("out"), *Pin->PinName.ToString());
					Pins->SetStringField(FString::Printf(TEXT("%s|%s %d"), *Path, *Side, Count.FindOrAdd(Side)++), Pin->PinId.ToString());
				}
			}
		}
	}
	Root->SetObjectField(TEXT("ids"), Ids);
	Root->SetObjectField(TEXT("pins"), Pins);
	Root->SetNumberField(TEXT("nodes"), Nodes);
	Root->SetArrayField(TEXT("nodes_undo_would_not_record"), Unrecorded);
	return Root;
}

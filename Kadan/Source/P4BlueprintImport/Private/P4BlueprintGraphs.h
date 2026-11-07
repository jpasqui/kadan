// What the graph builders share: the report, and the making of ordinary Blueprint nodes from a plan.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

class UBlueprint;
class UEdGraph;

namespace P4Graphs
{
	struct FReport
	{
		TArray<FString> Problems;
		TMap<FString, int32> Made;
		// Synthesized binding objects only: their class is a native property constraint, never a
		// recovered original declaration or appearance. Ordinary reports omit this field.
		bool bStopped = false;
		// Nodes that name a state machine and one of its states or transitions (anim getters), with
		// what they name: these are tied to them when the whole animation graph is made.
		TArray<TPair<UEdGraphNode*, TSharedPtr<FJsonObject>>> Getters;

		void Problem(const FString& Text) { Problems.Add(Text); }
		void Stop(const FString& Text) { Problems.Add(Text); bStopped = true; }
		void Count(const TCHAR* What, int32 Number = 1) { Made.FindOrAdd(What) += Number; }

		FString Text() const
		{
			TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
			Root->SetBoolField(TEXT("ok"), !bStopped);
			TArray<TSharedPtr<FJsonValue>> List;
			for (const FString& Line : Problems)
			{
				List.Add(MakeShared<FJsonValueString>(Line));
			}
			Root->SetArrayField(TEXT("problems"), List);
			TSharedRef<FJsonObject> Counts = MakeShared<FJsonObject>();
			for (const TPair<FString, int32>& Pair : Made)
			{
				Counts->SetNumberField(Pair.Key, Pair.Value);
			}
			Root->SetObjectField(TEXT("made"), Counts);
			FString Out;
			TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
				TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
			FJsonSerializer::Serialize(Root, Writer);
			return Out;
		}
	};

	// The import plan of a file (P4Plan::Load). Null with the run stopped when it does not read.
	TSharedPtr<FJsonObject> LoadPlan(const FString& File, FReport& Report);

	// One node of a plan's graph (decomp/blueprints/k2.py), made in the graph. Null with a problem reported.
	UEdGraphNode* Make(UBlueprint* Blueprint, UEdGraph* Graph, const TSharedPtr<FJsonObject>& Record, FReport& Report);

	// Forgets the nodes that several plan records stand for (a Math Expression node whose value two
	// animation nodes take): to be called before the graphs of another Blueprint are built.
	void ForgetShared();

	// A pin by the name a plan uses.
	UEdGraphPin* Pin(UEdGraphNode* Node, const FString& Name, EEdGraphPinDirection Direction);

	// The constant values a plan gives a node's pins.
	void Defaults(UEdGraphNode* Node, const TSharedPtr<FJsonObject>& Record, const FString& Where, FReport& Report);
}

#pragma once
#include "CoreMinimal.h"
class UMaterialInterface;
// Bounded actual ancestry, including a mesh's default slots. No package/name
// absence or renderer inference admits an unsupported component factory.
KADAN_API bool P4MaterialReferenceUsesGraphs(UObject* Target,const TSet<UMaterialInterface*>& Graphs,bool& Uses,FString& Error);
// Includes live Blueprint generated defaults and construction templates, whose
// outers are not necessarily the authoring Blueprint object.
KADAN_API bool P4GatherCreatorReferenceObjects(UObject* Asset, TArray<UObject*>& Objects, FString& Error);

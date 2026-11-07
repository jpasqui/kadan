#pragma once
#include "CoreMinimal.h"
class UMaterial;

// A fresh unsaved P4BuildMaterialFromGraph candidate, not ownership or renderer
// proof. Checks typed properties, input/pin semantics and source render settings.
// The builder creates source nodes first in SortIds order, then channel adapters;
// this deliberately refuses arbitrary reordered/artist-authored graphs.
KADAN_API bool P4FreshMaterialCandidateMatchesSource(UMaterial* Candidate,
    const FString& SourceGraph, FString& Error);

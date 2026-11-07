#pragma once
#include "CoreMinimal.h"

namespace P4LegacyParticleReference
{
// A plain stock ParticleSystem appends no native data after UObject's GUID.
// Extra fields, objects, nonzero GUIDs and unknown native bytes add other lines.
inline bool CanonicalCookLines(const FString& Name, const TArray<FString>& Lines)
{
    const FString Prefix = Name + TEXT(" | ");
    const FString Class = Prefix + TEXT("class /Script/Engine.ParticleSystem, kind 00000003");
    const FString Archetype = Prefix + TEXT("made from /Script/Engine.Default__ParticleSystem");
    return Lines.Num() == 2 && ((Lines[0].Equals(Class, ESearchCase::CaseSensitive) && Lines[1].Equals(Archetype, ESearchCase::CaseSensitive)) ||
        (Lines[1].Equals(Class, ESearchCase::CaseSensitive) && Lines[0].Equals(Archetype, ESearchCase::CaseSensitive)));
}
}

// Isolated CURRENT empty-state proof, not permission to replace an asset.
// No historic source/comparison digest exists for this identity-only family.
// A caller still needs incoming complete source admission and ordinary strict
// full import/cook/cold Finish. This helper does not alter the original record.
KADAN_API bool P4ProveCanonicalLegacyParticleReference(UObject* Asset, FString& Error);

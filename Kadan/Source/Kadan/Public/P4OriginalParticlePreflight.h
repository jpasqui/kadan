#pragma once
#include "CoreMinimal.h"

class UParticleSystem;
class UObject;
class FStructProperty;

// Where one raw distribution lives in its owner: a field of the owner (Index selects the
// element of a fixed array), or a Member of the struct at Index of an owner's array Field.
struct FP4ParticleRawDistributionPlace
{
    FString Field, Member;
    int32 Index = 0;
};

// Visits every raw distribution in the owner shapes the engine's bake check accepts.
// The engine's cook drops a baked editor curve only in these places. Visit returns
// false to stop; the function returns false then.
KADAN_API bool P4ForEachParticleRawDistribution(UObject* Owner,
    TFunctionRef<bool(const FP4ParticleRawDistributionPlace&, const FStructProperty*, void*)> Visit);
// The raw distribution at a place, or false when the owner has no such place.
KADAN_API bool P4FindParticleRawDistribution(UObject* Owner, const FP4ParticleRawDistributionPlace& Place,
    const FStructProperty*& Raw, void*& Value);

// Runs after every source property of an original particle system is applied and before
// its ordinary PostLoad. The game's cook keeps only the baked lookup table of a bakeable
// distribution and drops the editor curve object. This rebuilds each such curve from its
// own table as a real editor distribution: constant, uniform or a linear curve through
// every table entry, verified to give back every entry exactly. The table stays as the
// game has it. A table no stock distribution can produce is refused, never guessed.
// It also holds the source tables and GPU emitter resource data, which the editor's
// rebuilds replace, so the custody capture can restore and record them afterwards.
KADAN_API bool P4PreflightOriginalParticleEditorBuild(UParticleSystem* System, FString& Error);
// The table reconstruction alone, for one particle module or other owner object.
// Made counts the distributions created; Places, when given, receives where each one is.
KADAN_API bool P4ReconstructOriginalParticleDistributions(UObject* Owner, int32& Made, FString& Error,
    TArray<FP4ParticleRawDistributionPlace>* Places = nullptr);
// Holds the source tables of the reconstructed curves and the source resource data of
// every GPU emitter until the custody capture of the same import consumes them.
KADAN_API bool P4HoldOriginalParticleSource(UParticleSystem* System,
    const TArray<TPair<UObject*, FP4ParticleRawDistributionPlace>>& Curves, FString& Error);

namespace P4LevelContent { struct FFile; }
// Tells the ranges of the raw distributions whose table is the game's and whose ranges are not.
void P4AddParticleRangesNotHeld(const P4LevelContent::FFile& Game, const P4LevelContent::FFile& Mine, TArray<FString>& NotHeld);

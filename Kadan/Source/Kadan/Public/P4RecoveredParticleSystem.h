#pragma once
#include "CoreMinimal.h"
#include "Particles/ParticleSystem.h"
#include <vector>

class UParticleModuleRequired;
struct FP4KadanSourceBinding;
struct FP4OriginalValueNote;

// Separate native storage for the original game's additional bool properties.
// Uses the real ParticleSystem constructor/vtable, with no change to the stock class.
KADAN_API UClass* P4RecoveredParticleSystemClass();
KADAN_API UParticleSystem* P4NewRecoveredParticleSystem(UObject* Outer = nullptr,
    FName Name = NAME_None, EObjectFlags Flags = RF_NoFlags);
KADAN_API void P4RefreshRecoveredParticleSystemDefaults();
KADAN_API bool P4TranslateRecoveredParticleCook(std::vector<uint8>& Header,
    std::vector<uint8>& Body, FString& Error);

// Called only after all source properties and actual resource dependencies have been applied.
// Exact source geometry must first equal a separate native Build from the actual texture pixels.
// Then the stock public Serialize consumes the bounded native payload, before ordinary PostLoad.
KADAN_API bool P4ApplyOriginalParticleRequiredTail(UParticleModuleRequired* Module,
    const TArray<uint8>& Tail, FString& Error);
// Independent live readback used again after normal PostLoad, cold reload, and cook.
KADAN_API bool P4OriginalParticleRequiredTailMatches(const UParticleModuleRequired* Module,
    const TArray<uint8>& Tail, FString& Error);
KADAN_API bool P4ParticleRequiredTailRegeneratesFromTextureSource(const UParticleModuleRequired* Module,
    const TArray<uint8>& Tail, FString& Error);
// Full native comparison lines for source/cooked ReadNodes. Every retained coordinate word
// participates, so an equal byte count alone cannot qualify geometry or an original baseline.
KADAN_API bool P4DescribeOriginalParticleRequiredTail(const TArray<uint8>& Tail,
    TArray<FString>& NativeLines, FString& Error);

// Source inventories separate export names with dots; live nested UObject paths use colons.
// Resolve only the exact source asset/name/outer chain, never a global leaf-name search.
KADAN_API UObject* P4FindOriginalParticleObject(UParticleSystem* System, const FString& SourceRelative);

// Exact original H+B/current source binding is the authority; native-tail-only input cannot
// authorize this path. Persists editor-only metadata and refreshes the stock CPU/GPU resource.
KADAN_API bool P4CaptureOriginalParticleCutouts(UParticleSystem* System,
    const FP4KadanSourceBinding& Source, FString& Error);
// Cold-load hook. A matching source-bound original record (including partial originals) and
// unchanged exact native inputs are required. Unsupported-value notes remain unheld. Changed
// creator source/parameters are left to the engine's normal geometry build.
KADAN_API bool P4RestoreOriginalParticleCutouts(UParticleSystem* System, int32& Restored, FString& Error);
// Comparison preparation also covers particles loaded before the cold-load hook. Fresh
// pre-record imports only validate custody; malformed present metadata always refuses.
KADAN_API bool P4PrepareOriginalParticleState(UParticleSystem* System, FString& Error);
// Whether a particle system is a copy carrying the custody of the game original it was copied from.
KADAN_API bool P4IsCopiedParticleCustody(UParticleSystem* System);
// Makes the custody a fresh copy carries from Original tell the copy's own GPU emitter inputs.
KADAN_API bool P4RebindCopiedParticleCustody(UParticleSystem* Original, UParticleSystem* Copy, FString& Error);
KADAN_API bool P4OriginalParticleCutoutCustodyNote(UParticleSystem* System,
    FP4OriginalValueNote& Note, FString& Error);
KADAN_API bool P4OriginalParticleCutoutCacheKeys(UParticleSystem* System,
    TArray<FString>& Keys, FString& Error);

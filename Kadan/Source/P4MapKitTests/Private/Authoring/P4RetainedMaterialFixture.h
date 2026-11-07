#pragma once
#if WITH_DEV_AUTOMATION_TESTS
#include "CoreMinimal.h"
#include "P4KadanImportSources.h"
class UMaterial;
class FJsonObject;
struct FP4KadanSourceAsset;
// Shared dev-fixture evidence, implemented by the existing candidate audit.
// No private corpus discovery, source acquisition, or asset writes occur here.
namespace P4RetainedMaterialCandidateProof
{
FString Hash(const TArray<uint8>& Bytes);
bool Files(const FString& Package,TMap<FString,FString>& Out);
bool SourceFiles(const TArray<FP4KadanSourceBinding>& Bindings,TMap<FString,FString>& Out);
bool ArchiveFile(const FString& Path,FString& Out);
TMap<FString,TArray<uint8>> Native(UMaterial* Material);
TMap<FName,FString> Metadata(UObject* Object);
TArray<TArray<FString>> Families();
bool FamilyView(const FString& Archive,TArray<TSharedPtr<FP4KadanSourceAsset>>& Sources,
    TArray<FP4KadanSourceBinding>& Bindings,FString& ManifestPath,FString& ManifestHash,FString& ArchiveState,FString& Error);
bool HistoricalPins(const FString& Text,int32 Index,FString& Error);
// Read-only prerequisite for the separately owned actual Shadow consumer LOOK.
// Requires the authoritative completed-cold transaction and rechecks its files,
// source/record/native semantics and live shader map; the phase string is not proof.
bool QualifiedShadowForLook(UMaterial*& Out,TSharedPtr<FJsonObject>& Receipt,FString& Error);
}
#endif

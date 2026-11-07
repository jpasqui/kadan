#pragma once
#include "CoreMinimal.h"
#include "P4KadanImport.h"

class AActor;
class UMaterialInterface;

// What the game's own cooked master of a material lets the game draw. The game checks these usage flags on its
// master and draws the default material where one is off. The editor's copy of the master says nothing about this:
// the editor turns a flag on the moment a material is put on such a mesh, and recompiles.
class KADAN_API FP4GameMaterialUsage
{
public:
    explicit FP4GameMaterialUsage(const FP4KadanImportSettings& InContainers) : Containers(InContainers) {}
    // True unless the material's master is one of the game's and its cooked copy has this flag off (a property name
    // of UMaterial, such as bUsedWithStaticLighting). A master that cannot be read counts as off, and Error says why.
    bool Allows(UMaterialInterface* Material, FName Flag, FString& Error);
    // A sentence for each part of the actor the game draws with its default material, because the part's kind of mesh
    // needs a usage its material's master does not have. Baked: the actor's static meshes keep baked lighting.
    void WhyDefaultMaterial(const AActor* Actor, bool Baked, TArray<FString>& Out);

private:
    FP4KadanImportSettings Containers;
    TMap<FName, TSet<FName>> Read;
    TMap<FName, FString> Unread;
};

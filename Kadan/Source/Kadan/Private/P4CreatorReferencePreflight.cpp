#include "P4CreatorReferencePreflight.h"
#include "P4GamePaths.h"
#include "P4SwitchShaderMapFreeze.h"
#include "P4TranslucentMaterialStaging.h"
#include "P4MaterialFactoryContext.h"
#include "P4MaterialReferenceContext.h"
#include "P4OriginalRecord.h"
#include "P4GameReplacement.h"
#include "P4KadanGameContent.h"
#include "Components/MeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/LightComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "GameFramework/Actor.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpressionTextureBase.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/SoftObjectPath.h"

bool P4PreflightCreatorReferences(const FP4ModDescriptor& Mod, const FP4CreatorReferenceInputs& Inputs,
    const FP4ExportGameInventory* Inventory, const TMap<FName, FString>& Claims, bool CustomMaterials,
    TArray<FString>& OutPackages, FString& Error)
{
    OutPackages.Reset();
    Error.Reset();
    if (!IsInGameThread())
    {
        Error = TEXT("Creator reference inspection requires the editor thread.");
        return false;
    }
    FP4ModNames Names;
    if (!P4ModDerivedNames(Mod, Names, Error))
        return false;
    if (Inventory && !Inventory->IsValid())
    {
        Error = TEXT("Read the actual game package inventory before inspecting creator references.");
        return false;
    }
    TArray<TStrongObjectPtr<UObject>> Retained;
    for (AActor* Actor : Inputs.AddedActors)
    {
        if (!IsValid(Actor))
        {
            Error = TEXT("An added actor disappeared before export review.");
            return false;
        }
        Retained.Emplace(Actor);
    }
    const auto AddPackage = [&](const FString& Package) -> bool
    {
        // The creator's file in place of a game asset ships at the game's path (K9).
        const bool Replacement = P4IsGameReplacementPackage(Package);
        if (!Replacement && Inventory && Inventory->Contains(FName(*Package)))
            return true;
        if (!Replacement && !Package.StartsWith(Names.PackageRoot + TEXT("/"), ESearchCase::CaseSensitive))
        {
            // The creator's assets ship where they are, other mods' folders too: several mods of one project can
            // ship the same asset. Older callers have no inventory. An already imported original stays the game's.
            if (!Inventory)
            {
                UObject* Original = LoadObject<UObject>(
                    nullptr, *P4ObjectPathOf(Package), nullptr, LOAD_NoWarn | LOAD_Quiet);
                if (Original && P4QueryOriginalAsset(Original).IsOriginal())
                    return true;
            }
        }
        OutPackages.AddUnique(Package);
        return true;
    };
    const auto InspectLive = [&](UObject* Root) -> bool
    {
        TArray<UObject*> Objects;
        if (!P4GatherCreatorReferenceObjects(Root, Objects, Error))
            return false;
        for (UObject* Object : Objects)
        {
            TArray<UObject*> Used;
            if (ULightComponent* Light = Cast<ULightComponent>(Object))
                Used.Add(Light->LightFunctionMaterial);
            if (UMeshComponent* Component = Cast<UMeshComponent>(Object))
            {
                if (Component->GetNumMaterials() > 256)
                {
                    Error = TEXT("Component material inspection exceeds its bounded slot limit.");
                    return false;
                }
                if (UStaticMeshComponent* Static = Cast<UStaticMeshComponent>(Component))
                    Used.Add(Static->GetStaticMesh());
                if (USkeletalMeshComponent* Skinned = Cast<USkeletalMeshComponent>(Component))
                    Used.Add(Skinned->SkeletalMesh);
                for (int32 Slot = 0; Slot < Component->GetNumMaterials(); ++Slot)
                    Used.Add(Component->GetMaterial(Slot));
            }
            if (UStaticMesh* Mesh = Cast<UStaticMesh>(Object))
            {
                if (Mesh->StaticMaterials.Num() > 256)
                {
                    Error = TEXT("Mesh material inspection exceeds its bounded slot limit.");
                    return false;
                }
                for (const FStaticMaterial& Slot : Mesh->StaticMaterials)
                    Used.Add(Slot.MaterialInterface);
            }
            if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(Object))
            {
                if (Mesh->Materials.Num() > 256)
                {
                    Error = TEXT("Skeletal mesh material inspection exceeds its bounded slot limit.");
                    return false;
                }
                for (const FSkeletalMaterial& Slot : Mesh->Materials)
                    Used.Add(Slot.MaterialInterface);
            }
            if (UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(Object))
            {
                Used.Add(Instance->Parent);
                if (Instance->TextureParameterValues.Num() > 256)
                {
                    Error = TEXT("Material instance texture inspection exceeds its bounded parameter limit.");
                    return false;
                }
                for (const FTextureParameterValue& Parameter : Instance->TextureParameterValues)
                    Used.Add(Parameter.ParameterValue);
            }
            if (UMaterialExpressionTextureBase* Expression = Cast<UMaterialExpressionTextureBase>(Object))
                Used.Add(Expression->Texture);
            for (UObject* Asset : Used)
                if (Asset && P4IsContentPackageIgnoringCase(Asset->GetOutermost()->GetName()))
                    if (!AddPackage(Asset->GetOutermost()->GetName()))
                        return false;
        }
        return true;
    };
    for (const FString& Package : Inputs.Packages)
        if (!AddPackage(Package))
            return false;
    for (AActor* Actor : Inputs.AddedActors)
        if (!InspectLive(Actor))
            return false;
    TArray<UObject*> Assets;
    // Resolve current in-memory references before relying on registry edges.
    // New instances/mesh assignments need not have been saved yet.
    for (int32 Index = 0; Index < OutPackages.Num(); ++Index)
    {
        if (OutPackages.Num() > 100000)
        {
            Error = TEXT("Creator material closure exceeds the export limit.");
            return false;
        }
        const FString Package = OutPackages[Index];
        UObject* Asset = LoadObject<UObject>(nullptr, *P4ObjectPathOf(Package), nullptr, LOAD_NoWarn | LOAD_Quiet);
        if (!IsValid(Asset))
        {
            Error = TEXT("A creator asset could not be loaded: ") + Package;
            return false;
        }
        Retained.Emplace(Asset);
        Assets.Add(Asset);
        // Current component templates, level actors and defaults can differ
        // from every saved registry edge. Apply the same origin/claim rules.
        if (!InspectLive(Asset))
            return false;
    }
    TSet<UMaterialInterface*> Graphs;
    TSet<FString> GraphPaths;
    for (UObject* Asset : Assets)
        if (UMaterial* Graph = Cast<UMaterial>(Asset))
        {
            Graphs.Add(Graph);
            GraphPaths.Add(Graph->GetPathName());
        }
    // The game's content digests are read here, where every asset of the closure is retained, and garbage
    // collected between reads. The availability checks below and the export after them read none.
    P4PrepareGameContentMatches(Assets);
    FP4GameContentGatherHold GatherHold;
    for (UObject* Asset : Assets)
    {
        const FP4ExportAvailability Availability = P4CreatorPreflightAvailability(Asset, Mod, CustomMaterials);
        if (Availability.Route != EP4ExportAssetRoute::CreatorTexture &&
            Availability.Route != EP4ExportAssetRoute::CreatorStaticMesh &&
            Availability.Route != EP4ExportAssetRoute::CreatorMaterialInstance &&
            Availability.Route != EP4ExportAssetRoute::CreatorMaterial &&
            Availability.Route != EP4ExportAssetRoute::CreatorBlueprint &&
            Availability.Route != EP4ExportAssetRoute::GameActorBlueprint &&
            Availability.Route != EP4ExportAssetRoute::CreatorLevel &&
            Availability.Route != EP4ExportAssetRoute::CreatorSound &&
            Availability.Route != EP4ExportAssetRoute::CreatorSkinned &&
            Availability.Route != EP4ExportAssetRoute::CreatorLighting &&
            Availability.Route != EP4ExportAssetRoute::PreviewObject)
        {
            Error = Asset->GetOutermost()->GetName() + TEXT(": ") + Availability.Reason;
            return false;
        }
    }
    for (const FP4ActorAssetReference& Reference : Inputs.AssetValues)
    {
        if (GraphPaths.Contains(Reference.Target) && !Reference.MaterialInterface)
        {
            Error = TEXT("The change to ") + Reference.Path + TEXT(" of ") + Reference.Actor +
                TEXT(" needs a material interface reference for a new shader.");
            return false;
        }
        const FString Target = FSoftObjectPath(Reference.Target).GetLongPackageName();
        if (!P4IsContentPackage(Target) || (Inventory && Inventory->Contains(FName(*Target))))
            continue;
        UObject* Referenced = LoadObject<UObject>(nullptr, *Reference.Target, nullptr, LOAD_NoWarn | LOAD_Quiet);
        if (Referenced)
            Retained.Emplace(Referenced);
        bool Uses = false;
        if (!P4MaterialReferenceUsesGraphs(Referenced, Graphs, Uses, Error) ||
            !P4ValidateActorShaderContext(Reference, Uses, Error))
            return false;
    }
    if (Graphs.Num())
    {
        for (UObject* Asset : Assets)
            if (!Asset->IsA<UMaterial>() &&
                !FP4TranslucentMaterialStaging::PreflightReferences(Asset, Graphs, Names.PackageRoot, Error))
                return false;
        for (AActor* Actor : Inputs.AddedActors)
            if (!FP4TranslucentMaterialStaging::PreflightReferences(Actor, Graphs, Names.PackageRoot, Error))
                return false;
        // Each consumer draws the graph with a renderer its map has to cover. Through UE's shaders the export compiles
        // the map for every renderer its consumers use (FP4TranslucentMaterialStaging::Prepare).
        const FP4MaterialFactoryLookup Renderers =
            [](UMaterialInterface* Graph, uint64 Factory, const FString& Consumer, FString& Why)
        {
            if (P4UseSwitchShaderRoute() && P4KnownMaterialFactory(Factory))
            {
                Why.Reset();
                return true;
            }
            Why = FP4TranslucentMaterialStaging::RendererIssue(Graph, Factory, Consumer);
            return Why.IsEmpty();
        };
        for (UObject* Asset : Assets)
            if (!Asset->IsA<UMaterial>() && !P4ValidateMaterialFactoryConsumers(Asset, Graphs, Renderers, Error))
                return false;
        for (AActor* Actor : Inputs.AddedActors)
            if (!P4ValidateMaterialFactoryConsumers(Actor, Graphs, Renderers, Error))
                return false;
    }
    return true;
}

#include "P4TranslucentMaterialStaging.h"
#include "P4MapKitCore/material_engine_hashes.h"
#include "P4GamePaths.h"
#include "P4MapKitCore/own_export.h"
#include "P4MapKitCore/sage_own_map.h"
#include "P4NativeCustomCompilerBridge.h"
#include "P4MaterialReferenceContext.h"
#include "P4MaterialFactoryContext.h"
#include "P4SwitchShaderMapFreeze.h"
#include "P4MaterialInstancePermutation.h"
#include "P4OriginalRecord.h"
#include "P4GameReplacement.h"
#include "P4CreatorSource.h"
#include "P4GamePreviewWorld.h"
#include "P4ModDescriptor.h"
#include "AssetRegistryModule.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MaterialShared.h"
#include "Interfaces/ITargetPlatform.h"
#include "Misc/FileHelper.h"
#include <algorithm>
#include "Materials/MaterialInstance.h"
#include "StaticParameterSet.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

namespace
{
using FRemap = TMap<UMaterialInterface*, UMaterialInterface*>;
struct FCandidate
{
    UObject* Object;
    FProperty* Field;
};
// A soft reference to a material of the creator's that is not loaded yet. Outside the mod's folder only a
// material the registry lists without the game's lock is read, so game assets are not loaded for this.
bool OwnMaterialPath(const FString& Root, const FSoftObjectPath& Path)
{
    const FString Package = Path.GetLongPackageName();
    if (Package.StartsWith(Root + TEXT("/"), ESearchCase::IgnoreCase))
        return true;
    if (!P4IsModContentPath(Root, Package))
        return false;
    const FAssetData Asset = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"))
                                 .Get()
                                 .GetAssetByObjectPath(FName(*Path.GetAssetPathString()));
    FString Locked;
    if (!Asset.IsValid() || (Asset.GetTagValue(FName(TEXT("P4KadanReadOnly")), Locked) && Locked == TEXT("1")))
        return false;
    return Asset.AssetClass == FName(TEXT("Material")) || Asset.AssetClass == FName(TEXT("MaterialInstanceConstant"));
}
bool UsesGeneratedGraph(UObject* Object, const FRemap& Replacements, bool& Uses, FString& Error)
{
    TSet<UMaterialInterface*> Graphs;
    for (const auto& Pair : Replacements)
    {
        Graphs.Add(Pair.Key);
        Graphs.Add(Pair.Value);
    }
    return P4MaterialReferenceUsesGraphs(Object, Graphs, Uses, Error);
}
bool HoldsReference(const FProperty* Field, int32 Depth = 0)
{
    if (Depth > 24)
        return true;
    if (CastField<FObjectPropertyBase>(Field))
        return true;
    if (const FArrayProperty* Array = CastField<FArrayProperty>(Field))
        return HoldsReference(Array->Inner, Depth + 1);
    if (const FMapProperty* Map = CastField<FMapProperty>(Field))
        return HoldsReference(Map->KeyProp, Depth + 1) || HoldsReference(Map->ValueProp, Depth + 1);
    if (const FSetProperty* Set = CastField<FSetProperty>(Field))
        return HoldsReference(Set->ElementProp, Depth + 1);
    if (const FStructProperty* Struct = CastField<FStructProperty>(Field))
        for (TFieldIterator<FProperty> Member(Struct->Struct); Member; ++Member)
            if (HoldsReference(*Member, Depth + 1))
                return true;
    return false;
}
bool Walk(FProperty* Field, void* Value, const FRemap& Replacements, const FString& Root, bool Apply, bool Allowed,
    bool MeshAllowed, bool& Changed, int32& Budget, int32& Loads, FString& Error, int32 Depth = 0)
{
    if (!HoldsReference(Field))
        return true;
    if (++Budget > 262144 || Depth > 24)
    {
        Error = TEXT("Material reference inspection exceeded its bounded package budget.");
        return false;
    }
    for (int32 Index = 0; Index < Field->ArrayDim; ++Index)
    {
        void* At = static_cast<uint8*>(Value) + Index * Field->ElementSize;
        if (FSoftObjectProperty* Soft = CastField<FSoftObjectProperty>(Field))
        {
            const FSoftObjectPtr Before = Soft->GetPropertyValue(At);
            UMaterialInterface* Target = nullptr;
            for (const auto& Pair : Replacements)
                if (Before.ToSoftObjectPath().ToString() == Pair.Key->GetPathName())
                {
                    Target = Pair.Value;
                    break;
                }
            UObject* Loaded = Before.Get();
            const FSoftObjectPath Path = Before.ToSoftObjectPath();
            if (!Loaded && OwnMaterialPath(Root, Path))
            {
                if (++Loads > 256)
                {
                    Error = TEXT("Own soft material inspection exceeds its bounded loading limit.");
                    return false;
                }
                Loaded = LoadObject<UObject>(nullptr, *Path.ToString(), nullptr, LOAD_NoWarn | LOAD_Quiet);
            }
            bool Uses = false;
            if (!UsesGeneratedGraph(Loaded, Replacements, Uses, Error))
                return false;
            if (!Target && !Uses)
                continue;
            if (Loaded && (Loaded->IsA<UStaticMesh>() || Loaded->IsA<USkeletalMesh>()))
            {
                if (!MeshAllowed ||
                    !((Loaded->IsA<UStaticMesh>() && Soft->PropertyClass == UStaticMesh::StaticClass()) ||
                        (Loaded->IsA<USkeletalMesh>() && Soft->PropertyClass == USkeletalMesh::StaticClass())))
                {
                    Error = TEXT("A mesh's new material is outside a supported static or skeletal component slot. ") +
                        Field->GetName();
                    return false;
                }
                continue;
            }
            if (!Allowed || Soft->PropertyClass != UMaterialInterface::StaticClass())
            {
                Error = TEXT("A custom material has a concrete or unsupported soft reference in ") + Field->GetName() +
                    TEXT(" (") + Soft->PropertyClass->GetPathName() + TEXT(") to ") + Path.ToString() + TEXT(".");
                return false;
            }
            if (!Target)
                continue; // A child keeps its own identity, but its rendering context still qualifies.
            Changed = true;
            if (Apply)
                Soft->SetPropertyValue(At, FSoftObjectPtr(Target));
        }
        else if (FObjectPropertyBase* Reference = CastField<FObjectPropertyBase>(Field))
        {
            UObject* Referenced = Reference->GetObjectPropertyValue(At);
            UMaterialInterface* Material = Cast<UMaterialInterface>(Referenced);
            UMaterialInterface* const* Target = Replacements.Find(Material);
            bool Uses = false;
            if (!UsesGeneratedGraph(Referenced, Replacements, Uses, Error))
                return false;
            if (!Target && !Uses)
                continue;
            if (Referenced && (Referenced->IsA<UStaticMesh>() || Referenced->IsA<USkeletalMesh>()))
            {
                if (!MeshAllowed ||
                    !((Referenced->IsA<UStaticMesh>() && Reference->PropertyClass == UStaticMesh::StaticClass()) ||
                        (Referenced->IsA<USkeletalMesh>() && Reference->PropertyClass == USkeletalMesh::StaticClass())))
                {
                    Error = TEXT("A mesh's new material is outside a supported static or skeletal component slot. ") +
                        Field->GetName();
                    return false;
                }
                continue;
            }
            // A saved construction-script MID keeps its identity: the sky
            // Blueprint variable is concretely typed MID and cannot hold a MIC.
            // Only this scope's exact authored-world copy qualifies, and only
            // its Parent interface is subsequently remapped.
            if (!Target && Reference->PropertyClass == UMaterialInstanceDynamic::StaticClass() &&
                P4IsGamePreviewMaterialInstance(Referenced))
                continue;
            if (!Allowed || Reference->PropertyClass != UMaterialInterface::StaticClass())
            {
                Error = TEXT("A custom material has a concrete or unsupported reference in ") + Field->GetName() +
                    TEXT(" (") + Reference->PropertyClass->GetPathName() + TEXT(") to ") +
                    (Referenced
                            ? Referenced->GetPathName() + TEXT(" (") + Referenced->GetClass()->GetPathName() + TEXT(")")
                            : TEXT("None")) +
                    TEXT(". Use a material interface slot.");
                return false;
            }
            if (!Target)
                continue;
            Changed = true;
            if (Apply)
                Reference->SetObjectPropertyValue(At, *Target);
        }
        else if (FStructProperty* Struct = CastField<FStructProperty>(Field))
        {
            for (TFieldIterator<FProperty> Member(Struct->Struct); Member; ++Member)
                if (!Walk(*Member, Member->ContainerPtrToValuePtr<void>(At), Replacements, Root, Apply, Allowed,
                        MeshAllowed, Changed, Budget, Loads, Error, Depth + 1))
                    return false;
        }
        else if (FArrayProperty* Array = CastField<FArrayProperty>(Field))
        {
            FScriptArrayHelper Items(Array, At);
            for (int32 Item = 0; Item < Items.Num(); ++Item)
                if (!Walk(Array->Inner, Items.GetRawPtr(Item), Replacements, Root, Apply, Allowed, MeshAllowed, Changed,
                        Budget, Loads, Error, Depth + 1))
                    return false;
        }
        else if (FMapProperty* Map = CastField<FMapProperty>(Field))
        {
            FScriptMapHelper Items(Map, At);
            for (int32 Item = 0; Item < Items.GetMaxIndex(); ++Item)
                if (Items.IsValidIndex(Item) &&
                    (!Walk(Map->KeyProp, Items.GetKeyPtr(Item), Replacements, Root, Apply, false, false, Changed,
                         Budget, Loads, Error, Depth + 1) ||
                        !Walk(Map->ValueProp, Items.GetValuePtr(Item), Replacements, Root, Apply, false, false, Changed,
                            Budget, Loads, Error, Depth + 1)))
                    return false;
        }
        else if (FSetProperty* Set = CastField<FSetProperty>(Field))
        {
            FScriptSetHelper Items(Set, At);
            for (int32 Item = 0; Item < Items.GetMaxIndex(); ++Item)
                if (Items.IsValidIndex(Item) &&
                    !Walk(Set->ElementProp, Items.GetElementPtr(Item), Replacements, Root, Apply, false, false, Changed,
                        Budget, Loads, Error, Depth + 1))
                    return false;
        }
    }
    return true;
}
bool GatherCandidates(UObject* Asset, const FRemap& Replacements, const FString& ModRoot,
    TArray<FCandidate>& Candidates, FString& Error, bool* DirectionalFunctions = nullptr)
{
    TArray<UObject*> Objects;
    if (!P4GatherCreatorReferenceObjects(Asset, Objects, Error))
        return false;
    int32 Budget = 0, Loads = 0;
    for (UObject* Object : Objects)
        for (TFieldIterator<FProperty> Field(Object->GetClass()); Field; ++Field)
        {
            // UE 4.26 marks USkeletalMesh::Materials transient: the mesh serializes its slots itself (USkeletalMesh::Serialize).
            const bool SkeletalSlots = Object->GetClass() == USkeletalMesh::StaticClass() &&
                Field->GetFName() == TEXT("Materials");
            if (Field->HasAnyPropertyFlags(CPF_Transient | CPF_EditorOnly) && !SkeletalSlots)
                continue;
            const bool LightFunctionSlot = (Object->GetClass() == USpotLightComponent::StaticClass() ||
                                               Object->GetClass() == UDirectionalLightComponent::StaticClass()) &&
                Field->GetFName() == TEXT("LightFunctionMaterial");
            if (LightFunctionSlot)
            {
                UMaterialInterface* Function = CastChecked<ULightComponent>(Object)->LightFunctionMaterial;
                bool Uses = false;
                if (!UsesGeneratedGraph(Function, Replacements, Uses, Error))
                    return false;
                if (Uses && (!Function->GetMaterial() || Function->GetMaterial()->MaterialDomain != MD_LightFunction))
                {
                    Error = Object->GetPathName() +
                        TEXT(" needs a Light Function domain material in LightFunctionMaterial.");
                    return false;
                }
                const ULightComponent* Light = CastChecked<ULightComponent>(Object);
                if (Uses &&
                    (Light->Mobility == EComponentMobility::Stationary || Object->IsA<UDirectionalLightComponent>()) &&
                    (!Light->CastShadows || !Light->CastDynamicShadows))
                {
                    Error = Object->GetPathName() +
                        TEXT(
                            " needs Cast Shadows and Cast Dynamic Shadows turned on to use a light function in the game.");
                    return false;
                }
                if (Uses && DirectionalFunctions && Object->IsA<UDirectionalLightComponent>())
                    *DirectionalFunctions = true;
            }
            const bool Slot = (Object->GetClass() == UStaticMesh::StaticClass() &&
                                  Field->GetFName() == TEXT("StaticMaterials")) ||
                (Object->GetClass() == USkeletalMesh::StaticClass() && Field->GetFName() == TEXT("Materials")) ||
                (Object->GetClass() == UMaterialInstanceConstant::StaticClass() &&
                    Field->GetFName() == TEXT("Parent")) ||
                (P4IsGamePreviewMaterialInstance(Object) && Field->GetFName() == TEXT("Parent")) ||
                ((P4StaticMaterialFactoryComponent(Object) ||
                     P4DrawnAsEngineClass(Object->GetClass()) == USkeletalMeshComponent::StaticClass()) &&
                    Field->GetFName() == TEXT("OverrideMaterials")) ||
                LightFunctionSlot;
            const bool MeshSlot = (P4StaticMaterialFactoryComponent(Object) &&
                                      Field->GetFName() == TEXT("StaticMesh")) ||
                (P4DrawnAsEngineClass(Object->GetClass()) == USkeletalMeshComponent::StaticClass() &&
                    Field->GetFName() == TEXT("SkeletalMesh"));
            bool Changed = false;
            if (!Walk(*Field, Field->ContainerPtrToValuePtr<void>(Object), Replacements, ModRoot, false, Slot, MeshSlot,
                    Changed, Budget, Loads, Error))
            {
                Error += TEXT(" Consumer: ") + Object->GetPathName() + TEXT(" (") + Object->GetClass()->GetPathName() +
                    TEXT(").");
                int32 OuterDepth = 0;
                for (UObject* Outer = Object->GetOuter(); Outer && OuterDepth++ < 8; Outer = Outer->GetOuter())
                    Error += FString::Printf(TEXT(" Outer: %s (%s), flags=0x%08x, valid=%d, pendingKill=%d."),
                        *Outer->GetPathName(), *Outer->GetClass()->GetPathName(), uint32(Outer->GetFlags()),
                        IsValid(Outer), Outer->IsPendingKill());
                return false;
            }
            if (Changed)
            {
                if (!Slot)
                {
                    Error = TEXT("This custom material reference is outside the supported material slots. ") +
                        Object->GetPathName() + TEXT(".") + Field->GetName();
                    return false;
                }
                Candidates.Add({Object, *Field});
            }
        }
    return true;
}
} //namespace

namespace
{
// The material factories Kadan's own maps cover: the static mesh's and, for opaque materials, the instanced mesh's.
constexpr uint64 LocalFactory = P4MapKit::EngineHash::LocalVertexFactory,
                 InstancedFactory = P4MapKit::EngineHash::InstancedStaticMeshVertexFactory;
// The cook asks a material for its shader maps per target platform. A runtime copy is given an empty list for the
// target, so the cook writes none (and compiles none); Kadan's block replaces that empty list in the cooked file.
struct FCookingResourcesTag
{
    using Type = TMap<const ITargetPlatform*, TArray<FMaterialResource*>> UMaterial::*;
    friend Type CookingResources(FCookingResourcesTag);
};
template <typename Tag, typename Tag::Type Member>
struct TReach
{
    friend typename Tag::Type CookingResources(Tag) { return Member; }
};
template struct TReach<FCookingResourcesTag, &UMaterial::CachedMaterialResourcesForCooking>;
struct FInstanceCookingResourcesTag
{
    using Type = TMap<const ITargetPlatform*, TArray<FMaterialResource*>> UMaterialInstance::*;
    friend Type CookingResources(FInstanceCookingResourcesTag);
};
template struct TReach<FInstanceCookingResourcesTag, &UMaterialInstance::CachedMaterialResourcesForCooking>;

// The same check export availability uses. An instance with only parameter edits, an edited game
// instance too, ships without a map of its own and draws with its parent's shaders.
bool NeedsInstanceMap(const UMaterialInstanceConstant* Instance)
{
    return P4MaterialInstanceNeedsOwnShaderMap(Instance);
}
} //namespace

FString FP4TranslucentMaterialStaging::RuntimePackage(const FString& Root, const FString& GraphPackage)
{
    if (!P4IsModContentPath(Root, GraphPackage) ||
        GraphPackage.StartsWith(Root + TEXT("/_KadanRuntimeMaterials/"), ESearchCase::IgnoreCase))
        return FString();
    // Keep the entire relative graph path: different folders may contain the same leaf. A graph outside the
    // mod's folder keeps its whole path below /Game in a folder of its own.
    if (!GraphPackage.StartsWith(Root + TEXT("/"), ESearchCase::CaseSensitive))
        return Root + TEXT("/_KadanRuntimeMaterials/_Game/") + P4ContentRelativePath(GraphPackage);
    return Root + TEXT("/_KadanRuntimeMaterials/") + GraphPackage.Mid(Root.Len() + 1);
}

FString FP4TranslucentMaterialStaging::RendererIssue(
    const UMaterialInterface* Graph, uint64 Factory, const FString& Consumer)
{
    const UMaterial* Material = Graph ? const_cast<UMaterialInterface*>(Graph)->GetMaterial() : nullptr;
    // Through UE's shaders a runtime material's map holds each renderer its usage names (Prepare sets it).
    if (P4UseSwitchShaderRoute() && P4MaterialUsageCovers(Material, Factory))
        return FString();
    const bool Opaque = Material && Material->GetBlendMode() == BLEND_Opaque;
    if (Factory == LocalFactory || (Factory == InstancedFactory && Opaque))
        return FString();
    if (Factory == InstancedFactory)
        return Consumer +
            TEXT(
                " is an instanced mesh. Only opaque materials of your own can be used on instanced meshes yet. Put this material on a plain static mesh.");
    return Consumer +
        TEXT(
            " draws this material with a renderer that materials of your own do not cover yet (skeletal meshes, spline meshes, particles). Use it on a static mesh.");
}

bool FP4TranslucentMaterialStaging::Prepare(const FString& Root, const TArray<UObject*>& Assets, FString& Error)
{
    if (!IsInGameThread() || Materials.Num() || Fields.Num() || !Compiled.empty() || Positions.Num())
    {
        Error = TEXT("A fresh material staging scope on the editor thread is required.");
        return false;
    }
    ModRoot = Root;
    // Validate names and compile all graphs before allocating a runtime package.
    struct FPlan
    {
        UMaterialInterface* Graph;
        FString Package;
        bool Instanced;
        TArray<uint64> Factories;
        FP4CreatorMaterialCompileResult Compiled;
    };
    TArray<FPlan> Plans;
    TSet<FString> Paths;
    TArray<TStrongObjectPtr<UObject>> HeldConsumers;
    for (UObject* Asset : Assets)
    {
        if (!IsValid(Asset))
        {
            Error = TEXT("A material consumer disappeared before compilation.");
            return false;
        }
        HeldConsumers.Emplace(Asset);
    }
    for (UObject* Asset : Assets)
        if (UMaterial* Graph = Cast<UMaterial>(Asset))
            Graphs.Emplace(Graph);
    // Through UE's shaders a material is compiled for every renderer the mod draws it with: its consumers say which.
    TMap<UMaterialInterface*, TArray<uint64>> Uses;
    if (P4UseSwitchShaderRoute())
    {
        TSet<UMaterialInterface*> All;
        for (UObject* Asset : Assets)
        {
            if (UMaterial* Graph = Cast<UMaterial>(Asset))
                All.Add(Graph);
            if (auto* Instance = Cast<UMaterialInstanceConstant>(Asset))
                if (NeedsInstanceMap(Instance))
                    All.Add(Instance);
        }
        const FP4MaterialFactoryLookup Collect =
            [&Uses](UMaterialInterface* Graph, uint64 Factory, const FString&, FString&)
        {
            Uses.FindOrAdd(Graph).AddUnique(Factory);
            return true;
        };
        for (UObject* Asset : Assets)
            if (!Asset->IsA<UMaterial>() && !P4ValidateMaterialFactoryConsumers(Asset, All, Collect, Error))
                return false;
        // A static child owns the nearest map, but renderer usage is still a master property.
        // Keep the runtime master usable by every renderer reached through its children.
        const auto Collected = Uses;
        for (const auto& Pair : Collected)
            if (auto* Instance = Cast<UMaterialInstanceConstant>(Pair.Key))
                if (UMaterial* Master = Instance->GetMaterial())
                    if (All.Contains(Master))
                        for (uint64 Factory : Pair.Value)
                            Uses.FindOrAdd(Master).AddUnique(Factory);
    }
    for (UObject* Asset : Assets)
    {
        UMaterialInterface* Graph = Cast<UMaterialInterface>(Asset);
        UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(Asset);
        if (!Graph || (Instance && !NeedsInstanceMap(Instance)))
            continue;
        const bool OriginalInstance = Instance && P4IsGameReplacement(Instance);
        if (!OriginalInstance && !P4IsTrustedGamePreviewEngineCopy(Graph) && !P4IsCreatorAsset(Graph, &Error))
        {
            return false;
        }
        const FString Package = OriginalInstance ? Graph->GetOutermost()->GetName()
                                                 : RuntimePackage(Root, Graph->GetOutermost()->GetName());
        if (Package.IsEmpty() || !FPackageName::IsValidLongPackageName(Package) || Paths.Contains(Package) ||
            (!OriginalInstance && (FindPackage(nullptr, *Package) || FPackageName::DoesPackageExist(Package))))
        {
            Error = TEXT("The generated material destination ") + Package + TEXT(" is occupied or invalid.");
            return false;
        }
        Paths.Add(Package);
        FP4CreatorMaterialCompileRequest Request;
        Request.CreatorMaterial = Graph->GetMaterial();
        Request.CreatorInstance = Instance;
        Request.RuntimeObjectPath = P4ObjectPathOf(Package);
        // An opaque material's map also holds the instanced mesh versions, so it draws on an instanced mesh wherever
        // one uses it.
        Request.Instanced = Graph->GetBlendMode() == BLEND_Opaque;
        Request.Factories = Uses.FindRef(Graph);
        FPlan Plan;
        Plan.Graph = Graph;
        Plan.Package = Package;
        Plan.Instanced = Request.Instanced;
        Plan.Factories = Request.Factories;
        Plan.Compiled = P4CompileCreatorMaterial(Request);
        if (!Plan.Compiled.Succeeded)
        {
            Error = TEXT("The material ") + Graph->GetPathName() + TEXT(" could not be exported. ") +
                Plan.Compiled.Error;
            return false;
        }
        if (ShaderFormat == TEXT("SF_GLSL_SAGE"))
        {
            // The Switch 2 builds: the same map, its programs compiled again for the console's GPU.
            try
            {
                auto Sage = P4MapKit::CustomMaterial::OwnSage::ConvertOwnMapToSage(
                    Plan.Compiled.Block, Plan.Compiled.MapHash, Plan.Compiled.Programs);
                Plan.Compiled.Block = MoveTemp(Sage.block);
                Plan.Compiled.MapHash = MoveTemp(Sage.map_hash);
                Plan.Compiled.Programs = MoveTemp(Sage.programs);
            }
            catch (const std::exception& Why)
            {
                Error = TEXT("The material ") + Graph->GetPathName() +
                    TEXT(" could not be exported for Nintendo Switch 2. ") + UTF8_TO_TCHAR(Why.what());
                return false;
            }
        }
        else if (ShaderFormat != TEXT("GLSL_SWITCH"))
        {
            Error = TEXT("Kadan cannot compile shaders for this game build's format, ") + ShaderFormat + TEXT(".");
            return false;
        }
        Plans.Add(MoveTemp(Plan));
    }
    // All closure consumers are checked against the renderers the maps cover before allocating any runtime package.
    TSet<UMaterialInterface*> PlannedGraphs;
    for (const FPlan& Plan : Plans)
        PlannedGraphs.Add(Plan.Graph);
    const FP4MaterialFactoryLookup PlannedLookup = [&](UMaterialInterface* Graph, uint64 Factory,
                                                       const FString& Consumer, FString& Why) -> bool
    {
        for (const FPlan& Plan : Plans)
            if (Plan.Graph == Graph)
            {
                // Through UE's shaders the map was compiled for each renderer its consumers use.
                if (P4UseSwitchShaderRoute() && P4KnownMaterialFactory(Factory) && Plan.Factories.Contains(Factory))
                {
                    Why.Reset();
                    return true;
                }
                Why = RendererIssue(Graph, Factory, Consumer);
                return Why.IsEmpty();
            }
        Why = TEXT("The actual material consumer has no compiled graph.");
        return false;
    };
    if (Plans.Num())
        for (UObject* Asset : Assets)
            if (!P4ValidateMaterialFactoryConsumers(Asset, PlannedGraphs, PlannedLookup, Error))
                return false;
    for (FPlan& Plan : Plans)
    {
        // A game instance keeps its package/object instead of a generated duplicate.
        // Creator references use the reversible field snapshots below.
        if (auto* Original = Cast<UMaterialInstanceConstant>(Plan.Graph))
        {
            if (P4IsGameReplacement(Original))
            {
                FOriginalInstance Kept;
                Kept.Instance.Reset(Original);
                Kept.HadPermutation = Original->bHasStaticPermutationResource;
                OriginalInstances.Add(MoveTemp(Kept));
                Positions.Add(Original, int32(Compiled.size()));
                Compiled.push_back(MoveTemp(Plan.Compiled));
                continue;
            }
        }
        UPackage* Package = CreatePackage(*Plan.Package);
        UMaterialInterface* Runtime = DuplicateObject<UMaterialInterface>(
            Plan.Graph, Package, FName(*FPackageName::GetLongPackageAssetName(Plan.Package)));
        if (!Runtime)
        {
            Error = TEXT("The runtime copy of ") + Plan.Graph->GetPathName() + TEXT(" could not be made.");
            return false;
        }
        Runtime->SetFlags(RF_Public | RF_Standalone);
        Runtime->ClearFlags(RF_Transactional);
        // The creator source record stays with the authored graph; the runtime copy is the game's kind of material.
        Runtime->RemoveUserDataOfClass(UP4CreatorSourceData::StaticClass());
        // The map holds the static lighting versions (and the instanced mesh versions for an opaque material); the
        // game draws a material only where its usage says it may.
        if (UMaterial* Material = Cast<UMaterial>(Runtime))
        {
            Material->bUsedWithStaticLighting = true;
            Material->bUsedWithInstancedStaticMeshes = Plan.Instanced;
            P4ApplyMaterialFactoryUsage(Material, Plan.Factories);
            P4ApplyGameRenderLimits(Material);
        }
        if (auto* Instance = Cast<UMaterialInstanceConstant>(Runtime))
        {
            P4MaterialInstancePrepareRecoveredViewPermutation(
                CastChecked<UMaterialInstanceConstant>(Plan.Graph), Instance);
            Instance->bHasStaticPermutationResource = true;
        }
        Materials.Emplace(Runtime);
        Package->SetDirtyFlag(false);
        Replacements.Add(Plan.Graph, Runtime);
        Positions.Add(Plan.Graph, int32(Compiled.size()));
        Positions.Add(Runtime, int32(Compiled.size()));
        Compiled.push_back(MoveTemp(Plan.Compiled));
    }
    // Runtime instances keep their native class and local override values. Their parents
    // use this transaction's runtime identities, including a statically overridden parent.
    for (const auto& Held : Materials)
        if (auto* Instance = Cast<UMaterialInstanceConstant>(Held.Get()))
        {
            if (UMaterialInterface* const* Parent = Replacements.Find(Instance->Parent))
                Instance->Parent = *Parent;
            Instance->UpdateOverridableBaseProperties();
        }
    // An original can be reparented to a creator material. That parent ships at
    // its generated runtime identity, while the original instance keeps its path.
    for (const auto& Kept : OriginalInstances)
    {
        if (!Remap(Kept.Instance.Get(), Error))
        {
            return false;
        }
    }
    Error.Reset();
    return true;
}

bool FP4TranslucentMaterialStaging::Remap(UObject* Asset, FString& Error, bool KeepEditorState)
{
    if (!IsInGameThread() || !IsValid(Asset) || Asset->IsA<UMaterial>() || !HasMaterials())
    {
        if (!HasMaterials())
        {
            Error.Reset();
            return true;
        }
        Error = TEXT("Material staging needs a valid non-graph editor object") +
            (Asset ? TEXT(": ") + Asset->GetPathName() : FString(TEXT(" (none loaded)"))) + TEXT(".");
        return false;
    }
    TSet<UMaterialInterface*> TrackedGraphs;
    for (const auto& Pair : Replacements)
    {
        TrackedGraphs.Add(Pair.Key);
        TrackedGraphs.Add(Pair.Value);
    }
    const FP4MaterialFactoryLookup Lookup = [&](UMaterialInterface* Graph, uint64 Factory, const FString& Consumer,
                                                FString& Why) -> bool
    {
        const int32* Position = Positions.Find(Graph);
        if (!Position)
        {
            Why = TEXT("A material consumer lost its compiled shader map.");
            return false;
        }
        Why = RendererIssue(Cast<UMaterialInterface>(CookAsset(Graph)), Factory, Consumer);
        return Why.IsEmpty();
    };
    if (!P4ValidateMaterialFactoryConsumers(Asset, TrackedGraphs, Lookup, Error))
        return false;
    TArray<FCandidate> Candidates;
    int32 Budget = 0, Loads = 0;
    // Inspect the complete object closure first; an unsupported slot never
    // leaves some earlier reference already changed.
    bool HasDirectionalFunctions = false;
    if (!GatherCandidates(Asset, Replacements, ModRoot, Candidates, Error, &HasDirectionalFunctions))
        return false;
    for (const FCandidate& Candidate : Candidates)
    {
        if (KeepEditorState &&
            !Packages.ContainsByPredicate(
                [&](const FKeptPackage& Kept) { return Kept.Package.Get() == Candidate.Object->GetOutermost(); }))
        {
            FKeptPackage Kept;
            Kept.Package.Reset(Candidate.Object->GetOutermost());
            Kept.Dirty = Kept.Package->IsDirty();
            Packages.Add(MoveTemp(Kept));
        }
        if (KeepEditorState)
        {
            FKeptField Kept;
            Kept.Object.Reset(Candidate.Object);
            Kept.Field = Candidate.Field;
            Kept.Value = FMemory::Malloc(Kept.Field->GetSize(), Kept.Field->GetMinAlignment());
            Kept.Field->InitializeValue(Kept.Value);
            Kept.Field->CopyCompleteValue(Kept.Value, Kept.Field->ContainerPtrToValuePtr<void>(Candidate.Object));
            Fields.Add(MoveTemp(Kept));
        }
        bool Changed = false;
        Budget = 0;
        if (!Walk(Candidate.Field, Candidate.Field->ContainerPtrToValuePtr<void>(Candidate.Object), Replacements,
                ModRoot, true, true, false, Changed, Budget, Loads, Error))
            return false;
    }
    DirectionalLightFunctions |= HasDirectionalFunctions;
    Error.Reset();
    return true;
}

bool FP4TranslucentMaterialStaging::PreflightReferences(UObject* Asset, const TSet<UMaterialInterface*>& Graphs,
    const FString& Root, FString& Error, bool* DirectionalFunctions)
{
    Error.Reset();
    if (!IsInGameThread() || !IsValid(Asset) || Asset->IsA<UMaterial>())
    {
        Error = TEXT("Material reference inspection requires a non-graph editor object.");
        return false;
    }
    TStrongObjectPtr<UObject> Kept(Asset);
    TArray<TStrongObjectPtr<UMaterialInterface>> KeptGraphs;
    FRemap Identity;
    for (UMaterialInterface* Graph : Graphs)
    {
        if (!IsValid(Graph))
        {
            Error = TEXT("An authored material disappeared during review.");
            return false;
        }
        KeptGraphs.Emplace(Graph);
        Identity.Add(Graph, Graph);
    }
    TArray<FCandidate> Candidates;
    bool HasDirectionalFunctions = false;
    if (!GatherCandidates(Asset, Identity, Root, Candidates, Error, &HasDirectionalFunctions))
        return false;
    if (DirectionalFunctions)
        *DirectionalFunctions = HasDirectionalFunctions;
    return true;
}

bool FP4TranslucentMaterialStaging::Restore(FString& Error)
{
    bool Exact = true;
    for (int32 Index = Fields.Num(); Index-- > 0;)
    {
        FKeptField& Kept = Fields[Index];
        if (IsValid(Kept.Object.Get()))
        {
            void* Current = Kept.Field->ContainerPtrToValuePtr<void>(Kept.Object.Get());
            Kept.Field->CopyCompleteValue(Current, Kept.Value);
            for (int32 Element = 0; Element < Kept.Field->ArrayDim; ++Element)
                Exact &= Kept.Field->Identical(static_cast<uint8*>(Current) + Element * Kept.Field->ElementSize,
                    static_cast<uint8*>(Kept.Value) + Element * Kept.Field->ElementSize);
        }
        else
            Exact = false;
        Kept.Field->DestroyValue(Kept.Value);
        FMemory::Free(Kept.Value);
        Kept.Value = nullptr;
    }
    Fields.Reset();
    for (FKeptPackage& Kept : Packages)
        Kept.Package->SetDirtyFlag(Kept.Dirty);
    Packages.Reset();
    Error = Exact ? FString() : TEXT("The export could not restore an editor material reference exactly.");
    return Exact;
}

bool FP4TranslucentMaterialStaging::Release(FString& Error)
{
    const bool Restored = Restore(Error);
    for (auto& Kept : OriginalInstances)
    {
        auto* Instance = Kept.Instance.Get();
        Instance->bHasStaticPermutationResource = Kept.HadPermutation;
        auto& Cooking = Instance->*CookingResources(FInstanceCookingResourcesTag());
        for (const auto* Target : Kept.AddedTargets)
        {
            Cooking.Remove(Target);
        }
    }
    OriginalInstances.Reset();
    bool NamesReleased = true;
    for (const auto& Held : Materials)
    {
        UMaterialInterface* Runtime = Held.Get();
        UPackage* Package = Runtime->GetOutermost();
        if (auto* Material = Cast<UMaterial>(Runtime))
            (Material->*CookingResources(FCookingResourcesTag())).Reset();
        if (auto* Instance = Cast<UMaterialInstanceConstant>(Runtime))
            (Instance->*CookingResources(FInstanceCookingResourcesTag())).Reset();
        Runtime->ClearFlags(RF_Public | RF_Standalone);
        Package->SetDirtyFlag(false);
        // Release the deterministic name without deleting any project files.
        NamesReleased &= Runtime->Rename(
            nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional);
        Runtime->MarkPendingKill();
        NamesReleased &= Package->Rename(
            *(TEXT("/Temp/KadanRuntimeMaterialReleased/") + FGuid::NewGuid().ToString(EGuidFormats::Digits)), nullptr,
            REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional);
        Package->MarkPendingKill();
    }
    Materials.Reset();
    Replacements.Reset();
    Graphs.Reset();
    Compiled.clear();
    Positions.Reset();
    if (!NamesReleased && Error.IsEmpty())
        Error = TEXT(
            "The export could not release a generated material destination. Reopen the editor before exporting again.");
    return Restored && NamesReleased;
}

FP4TranslucentMaterialStaging::~FP4TranslucentMaterialStaging()
{
    FString Error;
    if (!Release(Error))
        UE_LOG(LogTemp, Error, TEXT("%s"), *Error);
}

UObject* FP4TranslucentMaterialStaging::CookAsset(UObject* Authored) const
{
    if (UMaterialInterface* const* Target = Replacements.Find(Cast<UMaterialInterface>(Authored)))
        return *Target;
    return Authored;
}
TArray<FString> FP4TranslucentMaterialStaging::RuntimePackages() const
{
    TArray<FString> Out;
    for (const auto& Held : Materials)
        Out.Add(Held->GetOutermost()->GetName());
    return Out;
}
bool FP4TranslucentMaterialStaging::IsRuntimeAsset(const UObject* Asset) const
{
    return Materials.ContainsByPredicate(
               [&](const TStrongObjectPtr<UMaterialInterface>& Held) { return Held.Get() == Asset; }) ||
        OriginalInstances.ContainsByPredicate(
            [&](const FOriginalInstance& Held) { return Held.Instance.Get() == Asset; });
}
FString FP4TranslucentMaterialStaging::RuntimePath(const FString& AuthoredPath) const
{
    for (const auto& Pair : Replacements)
        if (AuthoredPath == Pair.Key->GetPathName())
            return Pair.Value->GetPathName();
    return AuthoredPath;
}
void FP4TranslucentMaterialStaging::BeginCook(UObject* Asset, const ITargetPlatform* Target) const
{
    UMaterialInterface* Runtime = Cast<UMaterialInterface>(Asset);
    if (!Runtime || !IsRuntimeAsset(Runtime) || !Target)
        return;
    if (auto* Material = Cast<UMaterial>(Runtime))
    {
        auto& Cooking = Material->*CookingResources(FCookingResourcesTag());
        if (!Cooking.Contains(Target))
            Cooking.Add(Target);
    }
    if (auto* Instance = Cast<UMaterialInstanceConstant>(Runtime))
    {
        auto& Cooking = Instance->*CookingResources(FInstanceCookingResourcesTag());
        if (!Cooking.Contains(Target))
        {
            Cooking.Add(Target);
            for (const auto& Kept : OriginalInstances)
            {
                if (Kept.Instance.Get() == Instance)
                {
                    Kept.AddedTargets.Add(Target);
                }
            }
        }
        Instance->bHasStaticPermutationResource = true;
    }
}
bool FP4TranslucentMaterialStaging::WriteCookedBlock(UObject* Asset, const FString& Base, FString& Error) const
{
    const int32* Position = Positions.Find(Cast<UMaterialInterface>(Asset));
    if (!Position || !IsRuntimeAsset(Asset))
    {
        Error = TEXT("The cooked material is not one of this export's runtime materials.");
        return false;
    }
    TArray<uint8> Header, Body;
    if (!FFileHelper::LoadFileToArray(Header, *(Base + TEXT(".uasset"))) ||
        !FFileHelper::LoadFileToArray(Body, *(Base + TEXT(".uexp"))))
    {
        Error = TEXT("The cooked material could not be read back.");
        return false;
    }
    P4MapKit::Export::RewrittenPackage Written;
    std::string Why;
    if (!P4MapKit::CustomMaterial::OwnExport::InsertBlock(
            std::vector<uint8>(Header.GetData(), Header.GetData() + Header.Num()),
            std::vector<uint8>(Body.GetData(), Body.GetData() + Body.Num()), TCHAR_TO_UTF8(*Asset->GetName()),
            Compiled[*Position].Block, Written, Why))
    {
        Error = TEXT("Kadan's shaders could not be put into the cooked material ") + Asset->GetName() + TEXT(". ") +
            UTF8_TO_TCHAR(Why.c_str());
        return false;
    }
    TArray<uint8> NewHeader(Written.Header.data(), int32(Written.Header.size())),
        NewBody(Written.Body.data(), int32(Written.Body.size()));
    if (!FFileHelper::SaveArrayToFile(NewHeader, *(Base + TEXT(".uasset"))) ||
        !FFileHelper::SaveArrayToFile(NewBody, *(Base + TEXT(".uexp"))))
    {
        Error = TEXT("The cooked material with Kadan's shaders could not be written.");
        return false;
    }
    Error.Reset();
    return true;
}
bool FP4TranslucentMaterialStaging::ShaderLibrary(TArray<uint8>& Out, FString& Error) const
{
    try
    {
        std::vector<std::pair<P4MapKit::CustomMaterial::Bytes, std::vector<P4MapKit::CustomMaterial::Bytes>>> Maps;
        for (const auto& Each : Compiled)
        {
            if (std::any_of(Maps.begin(), Maps.end(), [&](const auto& Map) { return Map.first == Each.MapHash; }))
                continue;
            Maps.push_back({Each.MapHash, Each.Programs});
        }
        const auto Library = P4MapKit::CustomMaterial::OwnMap::Library(Maps);
        Out.Reset();
        Out.Append(Library.data(), int32(Library.size()));
        Error.Reset();
        return true;
    }
    catch (const std::exception& Why)
    {
        Error = UTF8_TO_TCHAR(Why.what());
        return false;
    }
}

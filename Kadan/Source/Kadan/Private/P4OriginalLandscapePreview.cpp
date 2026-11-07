#include "P4OriginalLandscapePreview.h"
#include "P4Common.h"
#include "P4OriginalRecord.h"
#include "P4CreatorSource.h"
#include "P4NativeHash.h"
#include "P4MaterialGraph.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"
#include "LandscapeComponent.h"
#include "LandscapeMaterialInstanceConstant.h"
#include "Materials/Material.h"
#include "MaterialShared.h"
#include "StaticParameterSet.h"
#include "ShaderCompiler.h"
#include "RenderingThread.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace
{
FString CachePath(UWorld* World)
{
    return FPaths::ProjectSavedDir() / TEXT("Kadan/LandscapeColorPreviews") /
        (FMD5::HashAnsiString(*World->GetOutermost()->GetName()) + TEXT(".json"));
}
FString OwnerGuard(ULandscapeMaterialInstanceConstant* Owner)
{
    if (!Owner || !Owner->Parent || Owner->HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad))
        return FString();
    FString Text;
    // The textures a compiled shader used are filled in by the compile, so a freshly imported owner has them
    // and the same owner loaded from disk does not. Its values are the same either way.
    const FName Derived(TEXT("CachedReferencedTextures"));
    for (TFieldIterator<FProperty> It(Owner->GetClass()); It; ++It)
        if (!It->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_SkipSerialization) &&
            It->GetFName() != Derived)
        {
            FString Value;
            It->ExportText_InContainer(0, Value, Owner, nullptr, Owner, PPF_None);
            Text += It->GetPathName() + TEXT("=") + Value + TEXT("\n");
        }
    const auto& Static = Owner->GetStaticParameters();
    if (Static.StaticSwitchParameters.Num() || Static.StaticComponentMaskParameters.Num() ||
        Static.MaterialLayersParameters.Num())
        return FString();
    for (const auto& Weight : Static.TerrainLayerWeightParameters)
        Text += FString::Printf(TEXT("%s:%d:%d:%d:%s\n"), *Weight.ParameterInfo.Name.ToString(), Weight.WeightmapIndex,
            int32(Weight.bOverride), int32(Weight.bWeightBasedBlend), *Weight.ExpressionGUID.ToString());
    FString Digest;
    return P4HashUtf8(Text, Digest) ? Digest : FString();
}
struct FBorrowed
{
    TWeakObjectPtr<ULandscapeMaterialInstanceConstant> Owner;
    FMaterialResource* Resource = nullptr;
    FMaterialShaderMap* Applied = nullptr;
    TRefCountPtr<FMaterialShaderMap> Original;
};
TArray<FBorrowed> Borrowed;
TArray<TStrongObjectPtr<UMaterial>> Graphs;
TArray<TStrongObjectPtr<ULandscapeMaterialInstanceConstant>> Permutations;
FDelegateHandle CleanupHandle;
void Refresh(ULandscapeMaterialInstanceConstant* Owner)
{
    // Updating the shader owner itself recompiles its original parent and
    // replaces the borrowed shader map. Refresh the actual component instances,
    // just as a Landscape's own material update does, without recompiling it.
    UWorld* World = Owner ? Owner->GetTypedOuter<UWorld>() : nullptr;
    if (!World || !World->PersistentLevel)
        return;
    TSet<UMaterialInstanceConstant*> Slots;
    TSet<UWorld*> ChangedWorlds;
    for (AActor* Actor : World->PersistentLevel->Actors)
        if (IsValid(Actor))
        {
            TInlineComponentArray<ULandscapeComponent*> Parts;
            Actor->GetComponents(Parts);
            for (auto* Part : Parts)
            {
                bool Uses = false;
                for (auto* Slot : Part->MaterialInstances)
                {
                    int32 Depth = 0;
                    for (UMaterialInterface* Parent = Slot; Parent && ++Depth <= 64;)
                    {
                        if (Parent == Owner)
                        {
                            Slots.Add(Slot);
                            Uses = true;
                            break;
                        }
                        auto* Instance = Cast<UMaterialInstanceConstant>(Parent);
                        Parent = Instance ? Instance->Parent : nullptr;
                    }
                }
                if (Uses)
                {
                    Part->MarkRenderStateDirty();
                    if (Part->GetWorld())
                        ChangedWorlds.Add(Part->GetWorld());
                }
            }
        }
    {
        FMaterialUpdateContext Update;
        for (auto* Slot : Slots)
        {
            if (Slot != Owner)
                Update.AddMaterialInstance(Slot);
            Slot->GetRenderProxy()->CacheUniformExpressions_GameThread(true);
        }
    }
    for (UWorld* Changed : ChangedWorlds)
        if (!Changed->bIsTearingDown)
            Changed->SendAllEndOfFrameUpdates();
}
} // namespace
FString P4OriginalLandscapeOwnerGuard(ULandscapeMaterialInstanceConstant* Owner)
{
    return OwnerGuard(Owner);
}
bool P4CacheOriginalLandscapeColorPreviews(
    UWorld* World, const FString& SourceDigest, const TArray<TSharedPtr<FJsonValue>>& Recipes, FString& Error)
{
    Error.Reset();
    FP4OriginalRecord Record;
    if (!World || Recipes.Num() > 64 || P4HasCreatorSource(World) || !P4ReadOriginalRecord(World, Record, Error) ||
        Record.Kind != TEXT("level") || !P4ExactNativeString(Record.SourceDigest, SourceDigest))
        return false;
    const auto Compared = P4DiagnoseOriginalAsset(World);
    if (!Compared.Error.IsEmpty() || Compared.Comparison != EP4OriginalComparison::Equal)
    {
        Error = TEXT("Landscape color previews require an unchanged original level");
        return false;
    }
    TArray<TSharedPtr<FJsonValue>> Held;
    for (const auto& Value : Recipes)
    {
        const auto Row = Value->AsObject();
        FString Path, Parent, Graph;
        if (!Row.IsValid() || !Row->TryGetStringField(TEXT("object"), Path) ||
            !Row->TryGetStringField(TEXT("parent"), Parent) || !Row->TryGetStringField(TEXT("graph"), Graph) ||
            Graph.Len() > 1024 * 1024)
            continue;
        auto* Owner = FindObject<ULandscapeMaterialInstanceConstant>(nullptr, *Path);
        if (!Owner || !Owner->IsIn(World) || !Owner->Parent ||
            !P4ExactNativeString(Owner->Parent->GetPathName(), Parent))
            continue;
        const FString Guard = OwnerGuard(Owner);
        if (Guard.IsEmpty())
            continue;
        Row->SetStringField(TEXT("native_owner_guard"), Guard);
        Held.Add(Value);
    }
    auto Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("format"), 1);
    Root->SetStringField(TEXT("package"), World->GetOutermost()->GetName());
    Root->SetStringField(TEXT("source"), SourceDigest);
    Root->SetStringField(TEXT("scope"), TEXT("source base color only; drawing cache; no material admission"));
    Root->SetArrayField(TEXT("owners"), Held);
    FString Json;
    FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Json));
    const FString Path = CachePath(World);
    FString Existing;
    if (FFileHelper::LoadFileToString(Existing, *Path) && P4ExactNativeString(Existing, Json))
        return true;
    if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true) ||
        !FFileHelper::SaveStringToFile(Json, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        Error = TEXT("The landscape color drawing cache could not be written");
        return false;
    }
    return true;
}
int32 P4ApplyOriginalLandscapeColorPreviews(UWorld* World)
{
    if (!World || GUsingNullRHI)
        return 0;
    const bool Tell = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_LANDSCAPE_COLOR_CACHE")) == TEXT("1");
    const auto Skip = [Tell](const FString& What, const TCHAR* Why)
    {
        if (Tell)
            UE_LOG(LogTemp, Display, TEXT("Landscape preview skipped %s: %s"), *What, Why);
    };
    int32 Count = 0;
    for (ULevel* Level : World->GetLevels())
        if (Level)
        {
            auto* SourceWorld = Cast<UWorld>(Level->GetOuter());
            FP4OriginalRecord Record;
            FString Error, Json;
            if (!SourceWorld || P4HasCreatorSource(SourceWorld) || !P4ReadOriginalRecord(SourceWorld, Record, Error) ||
                Record.Kind != TEXT("level"))
                continue;
            const FString Path = CachePath(SourceWorld);
            const int64 Size = IFileManager::Get().FileSize(*Path);
            if (Size <= 0 || Size > 2 * 1024 * 1024 || !FFileHelper::LoadFileToString(Json, *Path))
                continue;
            const FString LevelName = SourceWorld->GetOutermost()->GetName();
            TSharedPtr<FJsonObject> Root;
            const TArray<TSharedPtr<FJsonValue>>* Owners = nullptr;
            FString Package, Source;
            double Format = 0;
            if (!P4ParseJson(Json, Root) || !Root->TryGetNumberField(TEXT("format"), Format) || Format != 1 ||
                !Root->TryGetStringField(TEXT("package"), Package) ||
                !Root->TryGetStringField(TEXT("source"), Source) || !P4ExactNativeString(Package, Record.Package) ||
                !P4ExactNativeString(Source, Record.SourceDigest) || !Root->TryGetArrayField(TEXT("owners"), Owners) ||
                Owners->Num() > 64)
            {
                Skip(LevelName, TEXT("the drawing cache is not this level's source"));
                continue;
            }
            for (const auto& Value : *Owners)
            {
                const auto Row = Value->Type == EJson::Object ? Value->AsObject() : nullptr;
                FString Object, Parent, Guard, Recipe;
                if (!Row.IsValid() || !Row->TryGetStringField(TEXT("object"), Object) ||
                    !Row->TryGetStringField(TEXT("parent"), Parent) ||
                    !Row->TryGetStringField(TEXT("native_owner_guard"), Guard) ||
                    !Row->TryGetStringField(TEXT("graph"), Recipe) || Recipe.Len() > 1024 * 1024)
                    continue;
                auto* Owner = FindObject<ULandscapeMaterialInstanceConstant>(nullptr, *Object);
                if (!Owner || !Owner->IsIn(SourceWorld) || !Owner->Parent)
                {
                    Skip(Object, TEXT("the owner is not loaded in its level"));
                    continue;
                }
                if (!P4ExactNativeString(Owner->Parent->GetPathName(), Parent))
                {
                    Skip(Object, TEXT("the owner's parent changed"));
                    continue;
                }
                if (!P4ExactNativeString(OwnerGuard(Owner), Guard))
                {
                    Skip(Object, TEXT("the owner's values differ from the import"));
                    continue;
                }
                if (Borrowed.ContainsByPredicate([&](const FBorrowed& B) { return B.Owner == Owner; }))
                    continue;
                FMaterialResource* Resource = Owner->GetMaterialResource(GMaxRHIFeatureLevel);
                // Never replace a shared master or a different owner's resource.
                if (!Resource || Resource->GetMaterialInterface() != Owner)
                {
                    Skip(Object, TEXT("the owner has no resource of its own"));
                    continue;
                }
                const FString Temporary = TEXT("/Game/__KadanLandscapeColorPreview/") +
                    FGuid::NewGuid().ToString(EGuidFormats::Digits);
                UMaterial* Graph = P4BuildMaterialFromGraph(Recipe, Temporary, Error);
                if (Graph)
                {
                    Graph->ClearFlags(RF_Public | RF_Standalone);
                    Graph->SetFlags(RF_Transient);
                    Graph->Rename(nullptr, GetTransientPackage(),
                        REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
                }
                // Only the material is kept. The package it was built in is not project content: Save All
                // must not list it, and nothing keeps it loaded.
                if (UPackage* Built = FindPackage(nullptr, *Temporary))
                {
                    Built->SetDirtyFlag(false);
                    Built->SetFlags(RF_Transient);
                }
                if (!Graph)
                {
                    Skip(Object, *(TEXT("the recovered colors do not build: ") + Error));
                    continue;
                }
                TStrongObjectPtr<UMaterial> HeldGraph(Graph);
                auto* Preview = NewObject<ULandscapeMaterialInstanceConstant>(GetTransientPackage());
                TStrongObjectPtr<ULandscapeMaterialInstanceConstant> HeldPreview(Preview);
                Preview->SetParentEditorOnly(Graph);
                FStaticParameterSet Static;
                Owner->GetStaticParameterValues(Static);
                Preview->UpdateStaticPermutation(Static);
                Preview->PostEditChange();
                if (GShaderCompilingManager)
                    GShaderCompilingManager->FinishAllCompilation();
                FlushRenderingCommands();
                auto* Compiled = Preview->GetMaterialResource(GMaxRHIFeatureLevel);
                if (!Compiled || !Compiled->HasValidGameThreadShaderMap() || Compiled->GetCompileErrors().Num())
                {
                    Skip(Object, Compiled && Compiled->GetCompileErrors().Num()
                            ? *(TEXT("the preview does not compile: ") + Compiled->GetCompileErrors()[0])
                            : TEXT("the preview has no shader map"));
                    continue;
                }
                Resource = Owner->GetMaterialResource(GMaxRHIFeatureLevel);
                if (!Resource || Resource->GetMaterialInterface() != Owner ||
                    !P4ExactNativeString(OwnerGuard(Owner), Guard))
                {
                    Skip(Object, TEXT("the owner changed while the preview compiled"));
                    continue;
                }
                if (Tell)
                    UE_LOG(LogTemp, Display, TEXT("Landscape preview applied %s"), *Object);
                FBorrowed Saved;
                Saved.Owner = Owner;
                Saved.Resource = Resource;
                Saved.Original = Resource->GetGameThreadShaderMap();
                Saved.Applied = Compiled->GetGameThreadShaderMap();
                TMap<FMaterial*, FMaterialShaderMap*> Replace;
                Replace.Add(Resource, Compiled->GetGameThreadShaderMap());
                SetShaderMapsOnMaterialResources(Replace);
                Refresh(Owner);
                Borrowed.Add(MoveTemp(Saved));
                Graphs.Add(MoveTemp(HeldGraph));
                Permutations.Add(MoveTemp(HeldPreview));
                ++Count;
            }
        }
    FlushRenderingCommands();
    if (Tell)
        UE_LOG(LogTemp, Display, TEXT("Landscape previews applied in %s: %d of %d levels"),
            *World->GetOutermost()->GetName(), Count, World->GetLevels().Num());
    return Count;
}
void P4RestoreOriginalLandscapeColorPreviews()
{
    TMap<FMaterial*, FMaterialShaderMap*> Restore;
    for (const auto& Saved : Borrowed)
        if (auto* Owner = Saved.Owner.Get())
        {
            auto* Resource = Owner->GetMaterialResource(GMaxRHIFeatureLevel);
            auto* Current = Resource ? Resource->GetGameThreadShaderMap() : nullptr;
            if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_LANDSCAPE_COLOR_CACHE")) == TEXT("1"))
                UE_LOG(LogTemp, Display,
                    TEXT(
                        "Landscape preview restore %s: resource_same=%d map_same=%d original=%s applied=%s current=%s"),
                    *Owner->GetPathName(), int32(Resource == Saved.Resource), int32(Current == Saved.Applied),
                    Saved.Original ? *Saved.Original->GetShaderMapId().BaseMaterialId.ToString() : TEXT("null"),
                    Saved.Applied ? *Saved.Applied->GetShaderMapId().BaseMaterialId.ToString() : TEXT("null"),
                    Current ? *Current->GetShaderMapId().BaseMaterialId.ToString() : TEXT("null"));
            // A render-state refresh may replace the resource or clone this shader
            // map. The transient graph's unique base identity identifies our map;
            // a later creator compilation has a different identity and is preserved.
            if (Current && Saved.Applied && Saved.Applied->GetShaderMapId().BaseMaterialId.IsValid() &&
                Current->GetShaderMapId().BaseMaterialId == Saved.Applied->GetShaderMapId().BaseMaterialId)
                Restore.Add(Resource, Saved.Original);
        }
    if (Restore.Num())
        SetShaderMapsOnMaterialResources(Restore);
    for (const auto& Saved : Borrowed)
        if (auto* Owner = Saved.Owner.Get())
            Refresh(Owner);
    FlushRenderingCommands();
    Borrowed.Reset();
    Permutations.Reset();
    Graphs.Reset();
}
int32 P4OriginalLandscapeColorPreviewCount()
{
    return Borrowed.Num();
}
void P4StartOriginalLandscapeColorPreviews()
{
    if (!CleanupHandle.IsValid())
        CleanupHandle = FWorldDelegates::OnWorldCleanup.AddLambda(
            [](UWorld* World, bool, bool)
            {
                if (Borrowed.ContainsByPredicate(
                        [&](const FBorrowed& Item) { return Item.Owner.IsValid() && Item.Owner->IsIn(World); }))
                    P4RestoreOriginalLandscapeColorPreviews();
            });
}
void P4StopOriginalLandscapeColorPreviews()
{
    P4RestoreOriginalLandscapeColorPreviews();
    FWorldDelegates::OnWorldCleanup.Remove(CleanupHandle);
    CleanupHandle.Reset();
}

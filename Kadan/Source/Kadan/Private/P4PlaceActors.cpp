#include "P4PlaceActors.h"
#include "P4GameNames.h"
#include "P4EditorExposure.h"
#include "P4PlaceActorDiscovery.h"
#include "P4KadanEditor.h"
#include "P4KadanReadOnly.h"
#include "P4OriginalLevelImport.h"
#include "P4OriginalRecord.h"
#include "P4OriginalComponentReferences.h"
#include "P4CollisionHelperDisplay.h"
#include "Engine/World.h"
#include "P4AuthoringRows.h"
#include "ActorFactories/ActorFactoryBlueprint.h"
#include "ActorFactories/ActorFactoryClass.h"
#include "Engine/Blueprint.h"
#include "AssetRegistryModule.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/CoreDelegates.h"
#include "Misc/PackageName.h"
#include "UObject/MetaData.h"
#include "UObject/UObjectIterator.h"
#include "P4NativeHash.h"
#include "P4TextureProperties.h"
#include "P4SkinnedContent.h"
#include "P4MeshSourceReconstruction.h"
#include "P4MaterialCollectionSource.h"
#include "P4EngineDeclarations.h"
#include "P4RecoveredParticleSystem.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/CoreRedirects.h"
#include "P4KadanTestSeams.h"
#include "P4GameFunctionStandIns.h"
#include "P4SwitchShaderCompiler.h"
#include "P4MaterialGameSettings.h"
#include "P4SceneTeardown.h"
#include "P4OriginalActorAttachments.h"
#include "P4OriginalLandscapePreview.h"
#include "P4NativeConstructionPreview.h"
#include "P4LevelExport.h"
#include "P4OriginalNavigationMesh.h"

void P4RegisterStreamingUndoCallbacks();
void P4UnregisterStreamingUndoCallbacks();

IMPLEMENT_MODULE(FKadanModule, Kadan);

bool P4VisitLoadedActorBlueprintParent(const FString& ParentExportText, TFunctionRef<void()> Load)
{
    const FString Path = FPackageName::ExportTextPathToObjectPath(ParentExportText);
    UClass* Parent = FindObject<UClass>(nullptr, *Path);
    if (!Parent || !Parent->IsChildOf(AActor::StaticClass()))
        return false;
    // Abstract native parents may have placeable Blueprint children. Their actual
    // generated class is checked later; discovery never constructs a parent CDO.
    Load();
    return true;
}

namespace
{
const FName Categories[] = {"P4Treasures", "P4Enemies", "P4Gameplay"};
const TCHAR* Labels[] = {TEXT("Pikmin 4 Treasures"), TEXT("Pikmin 4 Enemies"), TEXT("Pikmin 4 Gameplay")};

bool RecoveredActor(UClass* Class)
{
    if (!Class || !Class->IsChildOf(AActor::StaticClass()) ||
        Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_NotPlaceable) ||
        FKismetEditorUtilities::IsClassABlueprintSkeleton(Class))
        return false;
    // Membership follows the original ancestry, including user-authored Blueprint children.
    for (UClass* Parent = Class; Parent; Parent = Parent->GetSuperClass())
        if (Parent->GetOutermost()->GetName() == TEXT(P4_GAME_SCRIPT_PACKAGE) &&
            (Parent->GetName() == TEXT("GActor") || Parent->GetName() == TEXT("GenBase")))
            return true;
    return false;
}

FName CategoryFor(UClass* Class)
{
    for (UClass* Parent = Class; Parent; Parent = Parent->GetSuperClass())
    {
        if (Parent->GetPathName() == TEXT(P4_GAME_SCRIPT_PACKAGE ".OtakaraUniActor") ||
            Parent->GetPathName().Contains(TEXT("/Otakara/")))
            return Categories[0];
        if (Parent->GetPathName().Contains(TEXT("/Teki/")))
            return Categories[1];
    }
    // Original treasure/enemy Blueprint paths carry Otakara/Teki folders.
    return Categories[2];
}
} //namespace

void FKadanModule::StartupModule()
{
    // Assets saved with the plugin's old script package load through the plugin's
    // Config/DefaultKadan.ini redirect, which Unreal reads before any module starts.
    P4RegisterEngineDeclarations();
    // Before any world Kadan makes can be collected: see P4SceneTeardown.h.
    P4StartSceneTeardownGuard();
    P4StartOriginalActorAttachments();
    // The game classes (P4Stubs) load before this module; their animation stand-ins are in place before
    // any map opens.
    P4StartGameFunctionStandIns();
    P4RegisterSwitchShaderCompiler();
    P4RegisterMaterialGameSettings();
#if P4_KADAN_TEST_SEAMS
    P4RegisterKadanTestSeams();
#endif
    P4RegisterStreamingUndoCallbacks();
    P4NameGameTextureGroups();
    P4StartMeshSourceReconstruction();
    P4StartMaterialCollectionReadiness();
    OriginalAssetLoadedHandle = FCoreUObjectDelegates::OnAssetLoaded.AddLambda(
        [](UObject* Asset)
        {
            P4RestoreSkinnedSource(Asset);
            UParticleSystem* Particle = Cast<UParticleSystem>(Asset);
            if (Particle && Particle->GetClass() == P4RecoveredParticleSystemClass())
            {
                int32 Restored = 0;
                FString Error;
                if (!P4RestoreOriginalParticleCutouts(Particle, Restored, Error))
                    UE_LOG(LogTemp, Warning, TEXT("Original particle native source could not be restored for %s: %s"),
                        *Particle->GetPathName(), *Error);
            }
        });
    EngineInitHandle = FCoreDelegates::OnPostEngineInit.AddRaw(this, &FKadanModule::AfterEngineInit);
    if (GEditor)
        AfterEngineInit();
}

void FKadanModule::AfterEngineInit()
{
    if (!GEditor)
        return;
    P4StartEditorExposure();
    P4StartOriginalComponentReferences();
    P4Kadan::InitializeMutationGuards();
    P4RegisterKadanEditor();
    UP4AuthoringTools::EnableOriginalAssetDetails();
    if (GEngine && !ActorAddedHandle.IsValid())
        ActorAddedHandle = GEngine->OnLevelActorAdded().AddLambda(
            [](AActor* Actor)
            {
                // Reconstruction spawns into inactive worlds and must keep the source's
                // component structure, including actors whose source has no root.
                // ChildActorComponent announces its deferred spawn before FinishSpawning
                // runs the inherited SCS. Installing a root here makes UE skip the
                // original DefaultSceneRoot node and its entire scene-component tree.
                // OverrideParentComponent is already assigned at this notification.
                if (!Actor || Actor->IsChildActor() || !Actor->GetWorld() ||
                    Actor->GetWorld()->WorldType != EWorldType::Editor || !RecoveredActor(Actor->GetClass()) ||
                    !Actor->GetClass()->FindPropertyByName(TEXT("GenerateInfo")))
                    return;
                // Supply only the ordinary editor transform root when the recovered
                // constructor has no root; export uses the original generator row.
                UP4AuthoringTools::EnsureActorRoot(Actor);
            });
    if (!BlueprintHandle.IsValid())
        BlueprintHandle = GEditor->OnBlueprintCompiled().AddLambda([this]() { Refresh(); });
    P4StartOriginalLandscapeColorPreviews();
    if (!LayersHandle.IsValid())
        LayersHandle = FWorldDelegates::OnPreWorldInitialization.AddLambda(
            [](UWorld* World, const UWorld::InitializationValues) { P4ShowOriginalLayersForOpen(World); });
    if (!MapHandle.IsValid())
        MapHandle = FEditorDelegates::OnMapOpened.AddLambda(
            [this](const FString&, bool)
            {
                P4RestoreOriginalLandscapeColorPreviews();
                UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
                FP4OriginalRecord Record;
                FString Error;
                if (World && P4ReadOriginalRecord(World, Record, Error) && Record.Kind == TEXT("level"))
                {
                    TSet<FName> Expected;
                    // Shows the imported common and day sublevels and hides the night and hero ones.
                    // Only the editor visibility of the original streaming levels changes.
                    if (!P4ActivateOriginalMapEditorLayers(World, TEXT("day"), Expected, Error))
                        UE_LOG(
                            LogTemp, Warning, TEXT("Kadan could not show the original map's day layers: %s"), *Error);
                    P4HideOriginalCollisionHelpers(World);
                    P4RestoreOriginalActorAttachments(World);
                    P4ApplyOriginalLandscapeColorPreviews(World);
                }
                P4ApplyNativeConstructionPreviews(World);
                Refresh();
            });
    // A sublevel shown later gets the game classes' construction steps too.
    if (!LevelAddedHandle.IsValid())
        LevelAddedHandle = FWorldDelegates::LevelAddedToWorld.AddLambda(
            [](ULevel*, UWorld* World)
            {
                if (World && World->WorldType == EWorldType::Editor)
                    P4ApplyNativeConstructionPreviews(World);
            });
    // A save writes the map's own values, not the previews, and every navigation tile the map's meshes hold.
    if (!PreSaveHandle.IsValid())
        PreSaveHandle = FEditorDelegates::PreSaveWorld.AddLambda(
            [](uint32, UWorld* World)
            {
                P4SetNativePreviewsSuspended(true);
                P4GiveNavigationBoundsForSave(World, true);
            });
    if (!PostSaveHandle.IsValid())
        PostSaveHandle = FEditorDelegates::PostSaveWorld.AddLambda(
            [](uint32, UWorld* World, bool)
            {
                P4GiveNavigationBoundsForSave(World, false);
                P4SetNativePreviewsSuspended(false);
            });
    // Startup lists loaded actor classes. The explicit refresh discovers saved Blueprints
    // after the editor is ready rather than loading their entire dependency closures here.
    Refresh(false);
}

int32 FKadanModule::Refresh(bool bLoadAssets)
{
    if (!GEditor || IsRunningCommandlet() || bRefreshing)
        return Items.Num();
    TGuardValue<bool> Guard(bRefreshing, true);
    auto& Placement = IPlacementModeModule::Get();
    for (const auto& Item : Items)
        Placement.UnregisterPlaceableItem(Item);
    Items.Empty();
    for (int32 Index = 0; Index < static_cast<int32>(UE_ARRAY_COUNT(Categories)); ++Index)
        if (!Placement.GetRegisteredPlacementCategory(Categories[Index]))
            Placement.RegisterPlacementCategory(FPlacementCategoryInfo(
                FText::FromString(Labels[Index]), Categories[Index], Categories[Index].ToString(), 60 + Index));
    // Only editor Blueprints with a recovered native parent are loaded. Cooked game packages
    // aren't used as editor assets, and unrelated project Blueprints stay unloaded.
    if (bLoadAssets)
    {
        TArray<FAssetData> Blueprints;
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry")
            .Get()
            .GetAssetsByClass(UBlueprint::StaticClass()->GetFName(), Blueprints, true);
        for (const auto& Data : Blueprints)
        {
            FString Parent;
            if (Data.GetTagValue(FName("NativeParentClass"), Parent) &&
                Parent.Contains(TEXT(P4_GAME_SCRIPT_PACKAGE ".")))
                P4VisitLoadedActorBlueprintParent(Parent, [&Data]() { Data.GetAsset(); });
        }
    }
    for (TObjectIterator<UClass> It; It; ++It)
    {
        UClass* Class = *It;
        if (!RecoveredActor(Class))
            continue;
        UBlueprint* Blueprint = Cast<UBlueprint>(Class->ClassGeneratedBy);
        // Recoverable native types are placeable too; they do not need a wrapper Blueprint.
        if (Class->ClassGeneratedBy && !IsValid(Blueprint))
            continue;
        const UWorld* World = GEditor->GetEditorWorldContext().World();
        const bool OwnPreset = Blueprint &&
            Blueprint->GetOutermost()->GetMetaData()->GetValue(Blueprint, TEXT("P4GeneratorPresetPackage")) ==
                Blueprint->GetOutermost()->GetName();
        const FString SourceLevel = OwnPreset
            ? Blueprint->GetOutermost()->GetMetaData()->GetValue(Blueprint, TEXT("P4SourceLevel"))
            : FString();
        if (!World || (!SourceLevel.IsEmpty() && SourceLevel != World->GetOutermost()->GetName()))
            continue;
        // Compilation notifications can arrive mid-batch. Never force a Blueprint
        // load or CDO construction from those callbacks; explicit refresh may load assets.
        if (!Class->FindPropertyByName(TEXT("GenerateInfo")))
            continue;
        const AActor* Default = Cast<AActor>(Class->GetDefaultObject(bLoadAssets && !Blueprint));
        if (!Default)
            continue;
        UObject* Asset = Blueprint ? static_cast<UObject*>(Blueprint) : static_cast<UObject*>(Class);
        const FAssetData Data(Asset);
        UActorFactory* Factory = GEditor->FindActorFactoryByClass(
            Blueprint ? UActorFactoryBlueprint::StaticClass() : UActorFactoryClass::StaticClass());
        FText Error;
        if (!Factory || !Factory->CanCreateActorFrom(Data, Error))
            continue;
        auto Item = MakeShared<FPlaceableItem>(Factory, Data);
        Item->DisplayName = Blueprint ? FText::FromString(Blueprint->GetName()) : Class->GetDisplayNameText();
        const auto ID = Placement.RegisterPlaceableItem(CategoryFor(Class), Item);
        if (ID.IsSet())
            Items.Add(ID.GetValue());
    }
    return Items.Num();
}

void FKadanModule::ShutdownModule()
{
    P4UnregisterSwitchShaderCompiler();
    P4UnregisterMaterialGameSettings();
    P4StopEditorExposure();
    P4StopOriginalComponentReferences();
    P4StopOriginalActorAttachments();
    P4StopOriginalLandscapeColorPreviews();
    P4UnregisterKadanEditor();
    P4Kadan::ShutdownMutationGuards();
#if P4_KADAN_TEST_SEAMS
    P4UnregisterKadanTestSeams();
#endif
    P4UnregisterStreamingUndoCallbacks();
    P4StopMeshSourceReconstruction();
    P4StopMaterialCollectionReadiness();
    P4StopGameFunctionStandIns();
    P4StopSceneTeardownGuard();
    FCoreUObjectDelegates::OnAssetLoaded.Remove(OriginalAssetLoadedHandle);
    FCoreDelegates::OnPostEngineInit.Remove(EngineInitHandle);
    if (GEditor && BlueprintHandle.IsValid())
        GEditor->OnBlueprintCompiled().Remove(BlueprintHandle);
    FEditorDelegates::OnMapOpened.Remove(MapHandle);
    FWorldDelegates::OnPreWorldInitialization.Remove(LayersHandle);
    FWorldDelegates::LevelAddedToWorld.Remove(LevelAddedHandle);
    FEditorDelegates::PreSaveWorld.Remove(PreSaveHandle);
    FEditorDelegates::PostSaveWorld.Remove(PostSaveHandle);
    if (GEngine && ActorAddedHandle.IsValid())
        GEngine->OnLevelActorAdded().Remove(ActorAddedHandle);
    if (IPlacementModeModule::IsAvailable())
    {
        auto& Placement = IPlacementModeModule::Get();
        for (const auto& Item : Items)
            Placement.UnregisterPlaceableItem(Item);
        for (const FName Category : Categories)
            Placement.UnregisterPlacementCategory(Category);
    }
}

int32 UP4AuthoringTools::RefreshPlaceActors()
{
    return FModuleManager::LoadModuleChecked<FKadanModule>("Kadan").Refresh(true);
}

TArray<FString> UP4AuthoringTools::PlaceActorPaths(FName Category)
{
    TArray<FString> Result;
    TArray<TSharedPtr<FPlaceableItem>> Items;
    IPlacementModeModule::Get().GetItemsForCategory(Category, Items);
    for (const auto& Item : Items)
        Result.Add(Item->AssetData.ObjectPath.ToString());
    Result.Sort();
    return Result;
}

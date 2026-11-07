#if P4_MAPKIT_TESTS
#include "P4GamePreviewWorld.h"
#include "P4GameNames.h"
#include "P4GamePreviewNavigation.h"
#include "P4GamePreviewTemplate.h"
#include "P4KadanImport.h"
#include "P4TranslucentMaterialStaging.h"
#include "Components/StaticMeshComponent.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4GamePreviewMaterialInstancesTest, "P4MapKit.Game.PreviewMaterialInstances",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4GamePreviewMaterialInstancesTest::RunTest(const FString&)
{
    const FString NativeRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    const FString GamePaks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    if (NativeRoot.IsEmpty() || GamePaks.IsEmpty())
    {
        AddError(TEXT("The native source and game container fixture are required."));
        return false;
    }
    FString GameFiles = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_TRANSPARENT_GAMEFILES"));
    if (GameFiles.IsEmpty())
        GameFiles = NativeRoot / TEXT("host/Saved/P4MapKit/FullNativeCave000/game");
    if (IFileManager::Get().DirectoryExists(*(GameFiles / TEXT("Carrot4/Content/Carrot4"))))
        GameFiles /= TEXT("Carrot4/Content");
    FString Retoc = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETOC"));
    if (Retoc.IsEmpty())
        Retoc = FPaths::GetPath(FPaths::GetPath(NativeRoot)) / TEXT("private/retoc/retoc.exe");
    FString Repak = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REPAK"));
    if (Repak.IsEmpty())
        Repak = FPaths::GetPath(FPaths::GetPath(NativeRoot)) / TEXT("private/repak/repak.exe");
    const FString Archive = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_TRANSPARENT_SHADER_ARCHIVE"));

    TStrongObjectPtr<UMaterial> SourceGraph(
        LoadObject<UMaterial>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")));
    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (!TestNotNull(TEXT("the installed engine graph exists"), SourceGraph.Get()) ||
        !TestNotNull(TEXT("the installed mesh exists"), Cube))
        return false;
    const auto SourceDecalResponse = SourceGraph->MaterialDecalResponse;
    const FString Identity = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    TStrongObjectPtr<UBlueprint> Blueprint(FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(),
        CreatePackage(*(TEXT("/Temp/KadanPreviewMIDBlueprint/") + Identity)), TEXT("BP_SavedMID"), BPTYPE_Normal,
        UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()));
    if (!TestNotNull(TEXT("a real Blueprint can own the same concrete MID variable as the sky"), Blueprint.Get()))
        return false;
    FEdGraphPinType VariableType;
    VariableType.PinCategory = UEdGraphSchema_K2::PC_Object;
    VariableType.PinSubCategoryObject = UMaterialInstanceDynamic::StaticClass();
    if (!TestTrue(TEXT("the Blueprint editor creates the typed variable"),
            FBlueprintEditorUtils::AddMemberVariable(Blueprint.Get(), TEXT("SkyMaterial"), VariableType)))
        return false;
    FKismetEditorUtilities::CompileBlueprint(Blueprint.Get());
    UClass* SkyClass = Blueprint->GeneratedClass;
    FObjectPropertyBase* SkyVariable = FindFProperty<FObjectPropertyBase>(SkyClass, TEXT("SkyMaterial"));
    if (!TestTrue(TEXT("the Blueprint variable is concretely typed MID"),
            SkyVariable && SkyVariable->PropertyClass == UMaterialInstanceDynamic::StaticClass()))
        return false;

    const FString Root = TEXT(P4_PREVIEW_PACKAGE_ROOT) + Identity + TEXT("_0123456789abcdef");
    UPackage* SourcePackage = CreatePackage(*(TEXT("/Temp/KadanPreviewMIDSource/") + Identity));
    TStrongObjectPtr<UWorld> SourceWorld(
        UWorld::CreateWorld(EWorldType::EditorPreview, false, TEXT("SourceWorld"), SourcePackage));
    if (!TestNotNull(TEXT("an isolated source world exists"), SourceWorld.Get()))
        return false;
    ON_SCOPE_EXIT
    {
        SourceWorld->DestroyWorld(false);
    };
    FActorSpawnParameters Spawn;
    Spawn.bDeferConstruction = true;
    AActor* Sky = SourceWorld->SpawnActor<AActor>(SkyClass, FTransform::Identity, Spawn);
    if (!TestNotNull(TEXT("the actual Blueprint actor can own the fixture"), Sky))
        return false;
    UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Sky, TEXT("PreviewMesh"));
    Sky->AddInstanceComponent(Mesh);
    Sky->SetRootComponent(Mesh);
    Mesh->SetStaticMesh(Cube);
    Mesh->SetMobility(EComponentMobility::Static);
    Mesh->SetWorldScale3D(FVector(10, 10, .2f));
    UMaterialInstanceDynamic* Original = UMaterialInstanceDynamic::Create(SourceGraph.Get(), Mesh, TEXT("SavedMID"));
    if (!TestNotNull(TEXT("the native dynamic material constructor succeeds"), Original))
        return false;
    Original->SetScalarParameterValue(TEXT("PreviewScalar"), 7.25f);
    Original->SetVectorParameterValue(TEXT("PreviewVector"), FLinearColor(.1f, .3f, .7f, .9f));
    SkyVariable->SetObjectPropertyValue_InContainer(Sky, Original);
    Mesh->SetMaterial(0, Original);
    if (!TestTrue(TEXT("the world owns the nontransient MID exactly as a saved template does"),
            !Original->HasAnyFlags(RF_Transient) && Original->IsIn(SourceWorld.Get()) &&
                Original->ScalarParameterValues.Num() == 1 && Original->VectorParameterValues.Num() == 1))
        return false;

    UPackage* CopyPackage = CreatePackage(*(Root / TEXT("Area005_GameArea")));
    UPackage* GraphPackage = CreatePackage(*(Root / TEXT("Engine/BasicShapes/BasicShapeMaterial")));
    TStrongObjectPtr<UMaterial> Graph(
        Cast<UMaterial>(StaticDuplicateObject(SourceGraph.Get(), GraphPackage, TEXT("BasicShapeMaterial"))));
    FObjectDuplicationParameters Duplicate(SourceWorld.Get(), CopyPackage);
    Duplicate.DestName = FName(TEXT("Area005_GameArea"));
    Duplicate.DuplicateMode = EDuplicateMode::World;
    Duplicate.FlagMask = RF_AllFlags & ~RF_Transient;
    TStrongObjectPtr<UWorld> World(Cast<UWorld>(StaticDuplicateObjectEx(Duplicate)));
    if (!TestNotNull(TEXT("native duplication retains the source world"), World.Get()) ||
        !TestNotNull(TEXT("the engine graph has a disposable copy"), Graph.Get()))
        return false;
    // This fixture isolates MID ownership/remapping using the existing supported
    // opaque compiler profile. Keep the installed source's decal policy intact.
    Graph->MaterialDecalResponse = MDR_ColorRoughness;
    ON_SCOPE_EXIT
    {
        World->DestroyWorld(false);
        Graph->ClearFlags(RF_Public | RF_Standalone);
        Graph->Rename(
            nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional);
        Graph->MarkPendingKill();
        GraphPackage->SetDirtyFlag(false);
        CopyPackage->SetDirtyFlag(false);
    };
    AActor* CopiedSky = FindObject<AActor>(World->PersistentLevel, *Sky->GetName());
    UStaticMeshComponent* CopiedMesh = CopiedSky ? FindObject<UStaticMeshComponent>(CopiedSky, TEXT("PreviewMesh"))
                                                 : nullptr;
    UMaterialInstanceDynamic* Instance = CopiedSky
        ? Cast<UMaterialInstanceDynamic>(SkyVariable->GetObjectPropertyValue_InContainer(CopiedSky))
        : nullptr;
    if (!TestTrue(TEXT("native duplication keeps the typed and component references on the same distinct MID"),
            Instance && Instance != Original && CopiedMesh && CopiedMesh->GetMaterial(0) == Instance))
        return false;
    Instance->Parent = Graph.Get();
    TSet<UMaterialInterface*> Graphs;
    Graphs.Add(Graph.Get());
    FString Error;
    TestFalse(TEXT("an unproven MID cannot gain creator material permission by its path"),
        FP4TranslucentMaterialStaging::PreflightReferences(World.Get(), Graphs, Root, Error));
    {
        FP4GamePreviewEngineCopies EngineProof;
        FP4GamePreviewMaterialInstances Instances;
        if (!TestTrue(TEXT("an installed engine source qualifies only its exact private copy"),
                EngineProof.Add(Graph.Get(), SourceGraph.Get(), Error)) ||
            !TestTrue(TEXT("the exact source-world MID qualifies its own copy"),
                Instances.Add(Instance, Original, SourceWorld.Get(), Error)))
        {
            AddError(Error);
            return false;
        }
        TStrongObjectPtr<UMaterialInstanceDynamic> Unrelated(
            UMaterialInstanceDynamic::Create(SourceGraph.Get(), GetTransientPackage()));
        TestFalse(TEXT("an unrelated MID cannot be vouched for by this source world"),
            Instances.Add(Instance, Unrelated.Get(), SourceWorld.Get(), Error));
        TestFalse(
            TEXT("unrelated MID identity has no private proof"), P4IsGamePreviewMaterialInstance(Unrelated.Get()));
        if (!TestTrue(TEXT("the proven typed MID and its Parent pass native slot inspection"),
                FP4TranslucentMaterialStaging::PreflightReferences(World.Get(), Graphs, Root, Error)))
        {
            AddError(Error);
            return false;
        }
        const FString InstancePath = Instance->GetPathName();
        FP4KadanImportSettings NavigationSettings;
        NavigationSettings.GameFolder = GameFiles;
        NavigationSettings.GamePaks = GamePaks;
        NavigationSettings.Retoc = Retoc;
        NavigationSettings.ShaderArchive = Archive;
        FP4GamePreviewNavigation Navigation;
        FP4GamePreviewTemplate Template;
        if (!TestTrue(
                TEXT("the preview template resolves"), P4ReadGamePreviewTemplate(NavigationSettings, Template, Error)))
        {
            AddError(Error);
            return false;
        }
        World->PersistentLevel->bHasRerunConstructionScripts = false;
        if (!TestTrue(TEXT("native collision registration builds all game navigation profiles"),
                P4BuildGamePreviewNavigation(World.Get(), NavigationSettings, Template.World, Navigation, Error)))
        {
            AddError(Error);
            return false;
        }
        TestEqual(TEXT("six actual native navigation payloads were built"), Navigation.NativePayloads.Num(), 6);
        TestFalse(TEXT("navigation does not rerun authored construction scripts"),
            World->PersistentLevel->bHasRerunConstructionScripts);
        TestTrue(TEXT("navigation preserves the proven MID path, typed identity and parent"),
            Instance->GetPathName() == InstancePath && P4IsGamePreviewMaterialInstance(Instance) &&
                Instance->Parent == Graph.Get() &&
                SkyVariable->GetObjectPropertyValue_InContainer(CopiedSky) == Instance &&
                CopiedMesh->GetMaterial(0) == Instance);
        FP4TranslucentMaterialStaging Stage;
        if (!TestTrue(TEXT("the actual native compiler stages a runtime material"),
                Stage.Prepare(Root, {Graph.Get(), World.Get()}, Error)) ||
            !TestTrue(TEXT("the real staging walker remaps the owned MID Parent"), Stage.Remap(World.Get(), Error)))
        {
            AddError(Error);
            return false;
        }
        TestTrue(TEXT("only Parent changes to the generated runtime material"),
            Stage.IsRuntimeAsset(Instance->Parent) && Instance->Parent->GetClass() == UMaterial::StaticClass());
        TestTrue(TEXT("the typed Blueprint variable and component preserve exact MID identity"),
            SkyVariable->GetObjectPropertyValue_InContainer(CopiedSky) == Instance &&
                CopiedMesh->GetMaterial(0) == Instance);
        TestTrue(TEXT("scalar and vector overrides survive the parent staging"),
            Instance->ScalarParameterValues.Num() == 1 && Instance->VectorParameterValues.Num() == 1 &&
                Instance->ScalarParameterValues[0].ParameterValue == 7.25f &&
                Instance->VectorParameterValues[0].ParameterValue == FLinearColor(.1f, .3f, .7f, .9f));
        TestTrue(
            TEXT("proof remains valid while the copy Parent is staged"), P4IsGamePreviewMaterialInstance(Instance));
        if (!TestTrue(TEXT("staging restores the exact original Parent"), Stage.Restore(Error)))
        {
            AddError(Error);
            return false;
        }
        TestTrue(TEXT("restoration preserves identity and returns the editable private graph"),
            Instance->Parent == Graph.Get() && SkyVariable->GetObjectPropertyValue_InContainer(CopiedSky) == Instance);
    }
    TestFalse(TEXT("MID permission expires when its owning scope ends"), P4IsGamePreviewMaterialInstance(Instance));
    TestFalse(TEXT("expired scope no longer admits the same MID to ordinary creator staging"),
        FP4TranslucentMaterialStaging::PreflightReferences(World.Get(), Graphs, Root, Error));
    TestTrue(TEXT("the source world, Blueprint variable, material parent and overrides were never edited"),
        SkyVariable->GetObjectPropertyValue_InContainer(Sky) == Original && Mesh->GetMaterial(0) == Original &&
            Original->Parent == SourceGraph.Get() && SourceGraph->MaterialDecalResponse == SourceDecalResponse &&
            Original->ScalarParameterValues.Num() == 1 && Original->ScalarParameterValues[0].ParameterValue == 7.25f &&
            Original->VectorParameterValues.Num() == 1 &&
            Original->VectorParameterValues[0].ParameterValue == FLinearColor(.1f, .3f, .7f, .9f));
    return true;
}
#endif

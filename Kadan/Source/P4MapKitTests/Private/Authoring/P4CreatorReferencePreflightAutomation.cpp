#if P4_MAPKIT_TESTS
#include "P4CreatorReferencePreflight.h"
#include "P4CreatorMaterialFactories.h"
#include "P4CreatorSource.h"
#include "P4KadanCreatorReview.h"
#include "P4LevelChanges.h"
#include "P4NativeCustomCompilerBridge.h"
#include "P4TranslucentMaterialStaging.h"
#include "P4MaterialReferenceContext.h"
#include "P4MaterialFactoryContext.h"
#include "AssetToolsModule.h"
#include "AssetRegistryModule.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Factories/FbxFactory.h"
#include "Factories/FbxImportUI.h"
#include "Factories/TextureFactory.h"
#include "AssetImportTask.h"
#include "HAL/FileManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4CreatorShaderReviewPreflightTest, "P4MapKit.Export.CreatorShaderReviewPreflight",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4CreatorShaderReviewPreflightTest::RunTest(const FString&)
{
    FP4ModDescriptor Mod;
    Mod.Id = TEXT("native.creator.preflight.test");
    Mod.Name = TEXT("Native creator preflight");
    Mod.Version = TEXT("1.0.0");
    FP4ModNames Names;
    FString Error;
    if (!P4ModDerivedNames(Mod, Names, Error))
    {
        AddError(Error);
        return false;
    }
    FP4ModDescriptor OtherMod = Mod;
    OtherMod.Id += TEXT(".other");
    FP4ModNames OtherNames;
    if (!P4ModDerivedNames(OtherMod, OtherNames, Error))
    {
        AddError(Error);
        return false;
    }
    const FString OtherTexturePackage = OtherNames.PackageRoot / TEXT("T_Outside");
    if (FindPackage(nullptr, *OtherTexturePackage) || FPackageName::DoesPackageExist(OtherTexturePackage))
    {
        AddError(TEXT("An existing asset occupies the owned other-mod texture fixture."));
        return false;
    }
    for (const TCHAR* Name : {TEXT("M_Review"), TEXT("MI_Review"), TEXT("SM_Review"), TEXT("SK_Review"),
             TEXT("BP_Review"), TEXT("ReviewWorld"), TEXT("T_A"), TEXT("T_B")})
        if (FindPackage(nullptr, *(Names.PackageRoot / Name)) ||
            FPackageName::DoesPackageExist(Names.PackageRoot / Name))
        {
            AddError(TEXT("An existing asset occupies the owned review fixture; it will not be overwritten."));
            return false;
        }
    TArray<UObject*> Made;
    TArray<UPackage*> Packages;
    ON_SCOPE_EXIT
    {
        for (UObject* Object : Made)
            if (IsValid(Object))
            {
                FAssetRegistryModule::AssetDeleted(Object);
                Object->ClearFlags(RF_Public | RF_Standalone);
                Object->GetOutermost()->SetDirtyFlag(false);
                Object->Rename(
                    nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional);
                Object->MarkPendingKill();
            }
        for (UPackage* Package : Packages)
            if (IsValid(Package) && Package != GetTransientPackage())
            {
                Package->SetDirtyFlag(false);
                Package->Rename(*(TEXT("/Temp/KadanReviewFixture/") + FGuid::NewGuid().ToString(EGuidFormats::Digits)),
                    nullptr, REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional);
                Package->MarkPendingKill();
            }
    };
    const FString Work = FPaths::ProjectSavedDir() / TEXT("KadanCreatorReviewPreflight");
    IFileManager::Get().MakeDirectory(*Work, true);
    const FString Image = FPaths::ConvertRelativePathToFull(Work / TEXT("creator.tga"));
    const TArray<uint8> Pixels = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 32, 40, 255, 255, 255, 128};
    if (!FFileHelper::SaveArrayToFile(Pixels, *Image))
        return false;
    auto ImportTexture = [&](const FString& PackageName, const TCHAR* Leaf) -> UTexture2D*
    {
        TStrongObjectPtr<UTextureFactory> Importer(NewObject<UTextureFactory>());
        Importer->AssetImportTask = NewObject<UAssetImportTask>(Importer.Get());
        Importer->AssetImportTask->bAutomated = true;
        bool Cancelled = false;
        UTexture2D* Result = Cast<UTexture2D>(
            Importer->ImportObject(UTexture2D::StaticClass(), CreatePackage(*PackageName), FName(Leaf),
                RF_Public | RF_Standalone | RF_Transactional, Image, nullptr, Cancelled));
        Importer->CleanUp();
        if (Result)
        {
            FAssetRegistryModule::AssetCreated(Result);
            Made.Add(Result);
            Packages.AddUnique(Result->GetOutermost());
        }
        if (!Result || Cancelled || !P4HasCreatorSource(Result, &Error))
            return nullptr;
        return Result;
    };
    TStrongObjectPtr<UTexture2D> TextureA(ImportTexture(Names.PackageRoot / TEXT("T_A"), TEXT("T_A")));
    TStrongObjectPtr<UTexture2D> TextureB(ImportTexture(Names.PackageRoot / TEXT("T_B"), TEXT("T_B")));
    TStrongObjectPtr<UTexture2D> Outside(ImportTexture(OtherTexturePackage, TEXT("T_Outside")));
    if (!TestTrue(TEXT("Distinct actual file-factory textures have creator source"),
            TextureA.IsValid() && TextureB.IsValid() && Outside.IsValid()))
    {
        AddError(Error);
        return false;
    }
    auto& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    TStrongObjectPtr<UP4CreatorMaterialFactory> Factory(NewObject<UP4CreatorMaterialFactory>());
    TStrongObjectPtr<UMaterial> Graph(Cast<UMaterial>(
        Tools.CreateAsset(TEXT("M_Review"), Names.PackageRoot, UMaterial::StaticClass(), Factory.Get())));
    if (!TestNotNull(TEXT("The actual material constructor creates the graph"), Graph.Get()))
        return false;
    Made.Add(Graph.Get());
    Packages.AddUnique(Graph->GetOutermost());
    Graph->BlendMode = BLEND_Translucent;
    Graph->SetShadingModel(MSM_Unlit);
    Graph->TwoSided = false;
    Graph->bEnableSeparateTranslucency = true;
    Graph->bUseTranslucencyVertexFog = true;
    UMaterialExpressionConstant3Vector* Tint = NewObject<UMaterialExpressionConstant3Vector>(Graph.Get());
    Tint->Constant = FLinearColor(1.f, 0.f, 0.f);
    UMaterialExpressionConstant* Alpha = NewObject<UMaterialExpressionConstant>(Graph.Get());
    Alpha->R = 0.5f;
    UMaterialExpressionTextureSampleParameter2D* Mask = NewObject<UMaterialExpressionTextureSampleParameter2D>(
        Graph.Get());
    Mask->ParameterName = TEXT("Mask");
    Mask->Texture = TextureA.Get();
    UMaterialExpressionMultiply* Opacity = NewObject<UMaterialExpressionMultiply>(Graph.Get());
    Opacity->A.Expression = Alpha;
    Opacity->B.Expression = Mask;
    Opacity->B.OutputIndex = 4;
    Graph->Expressions = {Tint, Alpha, Mask, Opacity};
    Graph->EmissiveColor.Expression = Tint;
    Graph->Opacity.Expression = Opacity;
    Graph->PostEditChange();
    if (!TestTrue(TEXT("The fresh graph has real creator source"), P4HasCreatorSource(Graph.Get(), &Error)) ||
        !TestTrue(TEXT("The actual graph passes the shared model/settings boundary"),
            P4CreatorMaterialGraphIssue(Graph.Get()).IsEmpty()))
    {
        AddError(Error);
        return false;
    }
    TStrongObjectPtr<UP4CreatorMaterialInstanceFactory> InstanceFactory(NewObject<UP4CreatorMaterialInstanceFactory>());
    InstanceFactory->InitialParent = Graph.Get();
    TStrongObjectPtr<UMaterialInstanceConstant> Instance(Cast<UMaterialInstanceConstant>(Tools.CreateAsset(
        TEXT("MI_Review"), Names.PackageRoot, UMaterialInstanceConstant::StaticClass(), InstanceFactory.Get())));
    if (!TestNotNull(TEXT("The actual child-instance constructor succeeds"), Instance.Get()))
        return false;
    Made.Add(Instance.Get());
    Packages.AddUnique(Instance->GetOutermost());
    Instance->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Mask")), TextureB.Get());

    // Discovery must inspect live defaults without inventing creator origin
    // for an empty synthetic skeletal asset. Admission remains independent.
    UPackage* SkeletalPackage = CreatePackage(*(Names.PackageRoot / TEXT("SK_Review")));
    Packages.Add(SkeletalPackage);
    TStrongObjectPtr<USkeletalMesh> Skeletal(NewObject<USkeletalMesh>(SkeletalPackage, TEXT("SK_Review")));
    Skeletal->Materials.Add(FSkeletalMaterial(Instance.Get()));
    Made.Add(Skeletal.Get());
    FP4CreatorReferenceInputs SkeletalInputs;
    SkeletalInputs.Packages.Add(SkeletalPackage->GetName());
    TArray<FString> SkeletalClosure;
    const TMap<FName, FString> NoClaims;
    TestFalse(TEXT("An empty skeletal fixture is not admitted as creator source"),
        P4PreflightCreatorReferences(Mod, SkeletalInputs, nullptr, NoClaims, true, SkeletalClosure, Error));
    TestTrue(TEXT("Unsaved skeletal defaults discover their child and graph"),
        SkeletalClosure.Contains(Instance->GetOutermost()->GetName()) &&
            SkeletalClosure.Contains(Graph->GetOutermost()->GetName()));
    TestTrue(TEXT("Unsaved skeletal defaults discover both texture defaults and overrides"),
        SkeletalClosure.Contains(TextureA->GetOutermost()->GetName()) &&
            SkeletalClosure.Contains(TextureB->GetOutermost()->GetName()));
    TSet<UMaterialInterface*> GraphSet;
    GraphSet.Add(Graph.Get());
    bool SkeletalUses = false;
    TestTrue(TEXT("Skeletal context detects the actual graph through its instance parent"),
        P4MaterialReferenceUsesGraphs(Skeletal.Get(), GraphSet, SkeletalUses, Error) && SkeletalUses);
    TestTrue(TEXT("Skeletal default slots pass read-only remap inspection"),
        FP4TranslucentMaterialStaging::PreflightReferences(Skeletal.Get(), GraphSet, Names.PackageRoot, Error));

    // A real small file import qualifies the mesh. No synthetic origin marker
    // or arbitrary existing-object qualification is used by this fixture.
    const FString Input = FPaths::ConvertRelativePathToFull(Work / TEXT("creator.obj"));
    if (!FFileHelper::SaveStringToFile(
            TEXT(
                "o ReviewPlane\nv 0 0 0\nv 100 0 0\nv 0 100 0\nvt 0 0\nvt 1 0\nvt 0 1\nvn 0 0 1\nf 1/1/1 2/2/1 3/3/1\n"),
            *Input, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        return false;
    TStrongObjectPtr<UFbxFactory> MeshFactory(NewObject<UFbxFactory>());
    MeshFactory->AssetImportTask = NewObject<UAssetImportTask>(MeshFactory.Get());
    MeshFactory->AssetImportTask->bAutomated = true;
    MeshFactory->SetDetectImportTypeOnImport(false);
    MeshFactory->ImportUI->MeshTypeToImport = FBXIT_StaticMesh;
    MeshFactory->ImportUI->bAutomatedImportShouldDetectType = false;
    MeshFactory->ImportUI->bImportMaterials = false;
    MeshFactory->ImportUI->bImportTextures = false;
    bool Cancelled = false;
    TStrongObjectPtr<UStaticMesh> Mesh(Cast<UStaticMesh>(
        MeshFactory->ImportObject(UStaticMesh::StaticClass(), CreatePackage(*(Names.PackageRoot / TEXT("SM_Review"))),
            TEXT("SM_Review"), RF_Public | RF_Standalone | RF_Transactional, Input, nullptr, Cancelled)));
    MeshFactory->CleanUp();
    if (!TestTrue(TEXT("The actual file import creates a qualified static mesh"),
            Mesh.IsValid() && !Cancelled && P4HasCreatorSource(Mesh.Get(), &Error)))
    {
        AddError(Error);
        return false;
    }
    FAssetRegistryModule::AssetCreated(Mesh.Get());
    Made.Add(Mesh.Get());
    Packages.AddUnique(Mesh->GetOutermost());
    Mesh->SetMaterial(0, Instance.Get());
    UPackage* WorldPackage = CreatePackage(*(Names.PackageRoot / TEXT("ReviewWorld")));
    Packages.Add(WorldPackage);
    TStrongObjectPtr<UWorld> World(UWorld::CreateWorld(EWorldType::Editor, false, TEXT("ReviewWorld"), WorldPackage));
    if (!TestNotNull(TEXT("The native added-actor world exists"), World.Get()))
        return false;
    ON_SCOPE_EXIT
    {
        if (World.IsValid())
            World->DestroyWorld(false);
    };
    AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>();
    if (!TestNotNull(TEXT("The actual plain static actor exists"), Actor))
        return false;
    Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh.Get());
    AActor* SkeletalActor = World->SpawnActor<AActor>();
    USkeletalMeshComponent* SkeletalComponent = NewObject<USkeletalMeshComponent>(SkeletalActor);
    SkeletalActor->AddInstanceComponent(SkeletalComponent);
    SkeletalComponent->SetSkeletalMesh(Skeletal.Get());
    FP4CreatorReferenceInputs WorldInputs;
    WorldInputs.Packages.Add(WorldPackage->GetName());
    TArray<FString> WorldClosure;
    TestFalse(TEXT("Creator-world graph discovery does not bypass the disabled material capability"),
        P4PreflightCreatorReferences(Mod, WorldInputs, nullptr, NoClaims, false, WorldClosure, Error));
    TestTrue(TEXT("Selected creator world discovers unsaved actor mesh and graph defaults"),
        WorldClosure.Contains(Mesh->GetOutermost()->GetName()) && WorldClosure.Contains(SkeletalPackage->GetName()) &&
            WorldClosure.Contains(Graph->GetOutermost()->GetName()));
    TestTrue(TEXT("Actual level actors use only supported material references"),
        FP4TranslucentMaterialStaging::PreflightReferences(World.Get(), GraphSet, Names.PackageRoot, Error));

    UPackage* BlueprintPackage = CreatePackage(*(Names.PackageRoot / TEXT("BP_Review")));
    Packages.Add(BlueprintPackage);
    TStrongObjectPtr<UBlueprint> Blueprint(
        FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), BlueprintPackage, TEXT("BP_Review"),
            BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()));
    if (!TestNotNull(TEXT("Native Blueprint constructor creates actual construction templates"), Blueprint.Get()))
        return false;
    Made.Add(Blueprint.Get());
    USCS_Node* TemplateNode = Blueprint->SimpleConstructionScript->CreateNode(
        USkeletalMeshComponent::StaticClass(), TEXT("ReviewSkin"));
    Blueprint->SimpleConstructionScript->AddNode(TemplateNode);
    CastChecked<USkeletalMeshComponent>(TemplateNode->ComponentTemplate)->SetSkeletalMesh(Skeletal.Get());
    TArray<UObject*> BlueprintObjects;
    TestTrue(TEXT("Blueprint gathering includes the genuine CDO and SCS component"),
        P4GatherCreatorReferenceObjects(Blueprint.Get(), BlueprintObjects, Error) &&
            BlueprintObjects.Contains(Blueprint->GeneratedClass->GetDefaultObject(false)) &&
            BlueprintObjects.Contains(TemplateNode->ComponentTemplate));
    FP4CreatorReferenceInputs BlueprintInputs;
    BlueprintInputs.Packages.Add(BlueprintPackage->GetName());
    TArray<FString> BlueprintClosure;
    TestFalse(TEXT("Blueprint discovery cannot qualify an empty skeletal source"),
        P4PreflightCreatorReferences(Mod, BlueprintInputs, nullptr, NoClaims, true, BlueprintClosure, Error));
    TestTrue(TEXT("Unsaved construction template discovers skeletal mesh and material graph"),
        BlueprintClosure.Contains(SkeletalPackage->GetName()) &&
            BlueprintClosure.Contains(Graph->GetOutermost()->GetName()));
    TestTrue(TEXT("Blueprint templates pass the same supported slot inspection"),
        FP4TranslucentMaterialStaging::PreflightReferences(Blueprint.Get(), GraphSet, Names.PackageRoot, Error));
    FP4LevelChangesResult Changes;
    Changes.Level = TEXT("ReviewWorld.PersistentLevel");
    Changes.Added.Add(Actor->GetName());
    FP4CreatorReferenceInputs References;
    if (!TestTrue(TEXT("World-only level inspection keeps added actors and typed provenance"),
            P4InspectLevelCreatorReferences(World.Get(), Changes, References, Error)))
    {
        AddError(Error);
        return false;
    }
    TestEqual(TEXT("World-only selection has no explicit creator package seed"), References.Packages.Num(), 0);
    const bool GraphDirty = Graph->GetOutermost()->IsDirty(), MeshDirty = Mesh->GetOutermost()->IsDirty();
    TArray<FString> Closure;
    const TMap<FName, FString> Claims;
    TestFalse(TEXT("World-only new shader refuses before compilation while capability is false"),
        P4PreflightCreatorReferences(Mod, References, nullptr, Claims, false, Closure, Error));
    TestTrue(TEXT("A false-capability refusal names the actual creator asset"), Error.Contains(Names.PackageRoot));
    TestTrue(TEXT("Unsaved mesh is in the live closure"), Closure.Contains(Mesh->GetOutermost()->GetName()));
    TestTrue(
        TEXT("Unsaved child instance is in the live closure"), Closure.Contains(Instance->GetOutermost()->GetName()));
    TestTrue(TEXT("Unsaved graph parent is in the live closure"), Closure.Contains(Graph->GetOutermost()->GetName()));
    TestTrue(TEXT("Unsaved graph texture default is in the live closure"),
        Closure.Contains(TextureA->GetOutermost()->GetName()));
    TestTrue(TEXT("Distinct unsaved child texture override is in the live closure"),
        Closure.Contains(TextureB->GetOutermost()->GetName()));
    TestTrue(TEXT("The same plain static context passes the qualified read-only rule"),
        P4PreflightCreatorReferences(Mod, References, nullptr, Claims, true, Closure, Error));
    TestTrue(TEXT("Read-only context checks retain editor material assignments"),
        Mesh->GetMaterial(0) == Instance.Get() && Instance->Parent == Graph.Get());
    TestEqual(TEXT("Review preserves graph dirty state"), Graph->GetOutermost()->IsDirty(), GraphDirty);
    TestEqual(TEXT("Review preserves mesh dirty state"), Mesh->GetOutermost()->IsDirty(), MeshDirty);
    TestTrue(TEXT("Read-only preflight never allocates a generated runtime material package"),
        !FindPackage(nullptr,
            *FP4TranslucentMaterialStaging::RuntimePackage(Names.PackageRoot, Graph->GetOutermost()->GetName())));
    TMap<FName, FString> Claimed;
    Claimed.Add(FName(*TextureB->GetOutermost()->GetName()), TEXT("another.mod"));
    TestFalse(TEXT("An unsaved child texture override cannot silently cross a competing mod claim"),
        P4PreflightCreatorReferences(Mod, References, nullptr, Claimed, true, Closure, Error));
    TestTrue(TEXT("The claim refusal names the actual override texture"),
        Error.Contains(TextureB->GetOutermost()->GetName()));
    Instance->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Mask")), Outside.Get());
    TestFalse(TEXT("An unsaved child texture override in another mod is refused before cooking"),
        P4PreflightCreatorReferences(Mod, References, nullptr, Claims, true, Closure, Error));
    TestTrue(TEXT("The namespace refusal names the actual other-mod texture"), Error.Contains(OtherTexturePackage));
    Instance->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Mask")), TextureB.Get());
    const FString CasePackage = Names.PackageRoot.Replace(TEXT("/Game/"), TEXT("/game/"), ESearchCase::CaseSensitive) /
        TEXT("T_CaseVariant");
    if (FPackageName::IsValidLongPackageName(CasePackage))
    {
        if (FindPackage(nullptr, *CasePackage) || FPackageName::DoesPackageExist(CasePackage))
        {
            AddError(TEXT("An existing object occupies the case-variant negative fixture."));
            return false;
        }
        UPackage* CaseOuter = CreatePackage(*CasePackage);
        Packages.Add(CaseOuter);
        // Deliberately unqualified negative asset: it must reach the strict
        // namespace refusal, never disappear from texture inspection.
        UTexture2D* CaseTexture = NewObject<UTexture2D>(CaseOuter, TEXT("T_CaseVariant"));
        Made.Add(CaseTexture);
        if (CaseOuter->GetName().StartsWith(TEXT("/game/"), ESearchCase::CaseSensitive))
        {
            Instance->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Mask")), CaseTexture);
            TestFalse(TEXT("A noncanonical /game texture override cannot evade reference inspection"),
                P4PreflightCreatorReferences(Mod, References, nullptr, Claims, true, Closure, Error));
            TestTrue(TEXT("The case-variant refusal names the actual texture namespace"), Error.Contains(CasePackage));
            Instance->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Mask")), TextureB.Get());
        }
        else
            AddInfo(TEXT("The native engine canonicalized the negative package's mount casing."));
    }

    AActor* InstancedActor = World->SpawnActor<AActor>();
    UInstancedStaticMeshComponent* Instanced = NewObject<UInstancedStaticMeshComponent>(InstancedActor);
    InstancedActor->AddInstanceComponent(Instanced);
    Instanced->SetStaticMesh(Mesh.Get());
    References.AddedActors = {InstancedActor};
    if (P4UseSwitchShaderRoute())
    {
        // Through UE's shaders the export compiles the map for the instanced mesh it is drawn with.
        TestTrue(TEXT("A default-only child of a translucent graph on ISMC passes the review through UE's shaders: ") +
                Error,
            P4PreflightCreatorReferences(Mod, References, nullptr, Claims, true, Closure, Error));
    }
    else
    {
        // Kadan's own map of an opaque material covers instanced meshes; this graph is translucent, so the review
        // refuses it on an instanced mesh with what to do, as the export does.
        TestFalse(TEXT("A default-only child of a translucent graph on ISMC is refused by the review"),
            P4PreflightCreatorReferences(Mod, References, nullptr, Claims, true, Closure, Error));
        TestTrue(TEXT("The review's ISMC refusal names the instanced mesh and what to do: ") + Error,
            Error.Contains(Instanced->GetName()) && Error.Contains(TEXT("plain static mesh")));
    }
    const FString InstancedPath = Instanced->GetPathName();
    const FP4MaterialFactoryLookup NoInstancedShader =
        [&](UMaterialInterface*, uint64, const FString& Consumer, FString& Why)
    {
        if (Consumer != InstancedPath)
            return true;
        Why = Consumer + TEXT(" uses a creator material through an unsupported renderer.");
        return false;
    };
    TestFalse(TEXT("ISMC is refused at export when the compiled material has no instanced shader"),
        P4ValidateMaterialFactoryConsumers(InstancedActor, GraphSet, NoInstancedShader, Error));
    TestTrue(TEXT("The ISMC refusal names the instanced component"), Error.Contains(InstancedPath));

    FP4ExportSelectionPreview Preview;
    FP4ExportSelectionRow Row;
    Row.Package = Mesh->GetOutermost()->GetName();
    Row.Disposition = EP4ExportDisposition::CreatorPackage;
    Preview.Rows.Add(Row);
    const FP4KadanCreatorReview Review = P4ReviewCreatorSelection(Mod, Preview, nullptr);
    TestTrue(TEXT("Panel shipping review adds unsaved child-instance row"),
        Review.Rows.ContainsByPredicate(
            [&](const FP4ExportSelectionRow& Item) { return Item.Package == Instance->GetOutermost()->GetName(); }));
    TestTrue(TEXT("Panel shipping review adds unsaved authored graph row"),
        Review.Rows.ContainsByPredicate(
            [&](const FP4ExportSelectionRow& Item) { return Item.Package == Graph->GetOutermost()->GetName(); }));
    TestTrue(TEXT("Panel shipping review adds distinct unsaved child texture override row"),
        Review.Rows.ContainsByPredicate(
            [&](const FP4ExportSelectionRow& Item) { return Item.Package == TextureB->GetOutermost()->GetName(); }));
    if (!true)
        TestFalse(TEXT("Normal review cannot claim readiness while the public gate is false"), Review.CanExport());
    else
        TestTrue(TEXT("Normal shipping review becomes ready through the public gate"), Review.CanExport());
    References = FP4CreatorReferenceInputs();
    Changes.Added.Reset();
    TestTrue(TEXT("Ordinary no-shader level edits retain their readiness"),
        P4InspectLevelCreatorReferences(World.Get(), Changes, References, Error) &&
            P4PreflightCreatorReferences(Mod, References, nullptr, Claims, false, Closure, Error));
    return !HasAnyErrors();
}
#endif

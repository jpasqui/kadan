#if P4_MAPKIT_TESTS
#include "P4ExportDriver.h"
#include "P4ExportReadbackContext.h"
#include "P4CreatorMaterialReadback.h"
#include "P4OriginalRecord.h"
#include "P4AuthoringRows.h"
#include "P4KadanCreatorReview.h"
#include "P4NativeCustomCompilerBridge.h"
#include "P4TranslucentMaterialStaging.h"
#include "P4CreatorSource.h"
#include "P4CreatorMaterialFactories.h"
#include "P4MaterialInstance.h"
#include "P4TypedMaterialValueFixture.h"
#include "AssetImportTask.h"
#include "AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Components/DecalComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Texture2D.h"
#include "Factories/FbxFactory.h"
#include "Factories/FbxImportUI.h"
#include "Factories/TextureFactory.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/LinkerLoad.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/StrongObjectPtr.h"
#include "P4MapKitCore/package_reader.h"
#include "P4MapKitCore/native_custom_backend_archive.h"
#include <algorithm>

static bool RunCreatorMaterialExport(FAutomationTestBase& Test, bool Lit, bool Opaque = false, bool Masked = false,
    bool TwoSided = false, bool AllRenderers = false)
{
    if (!GEditor)
    {
        Test.AddError(TEXT("Transparent export qualification requires the native editor."));
        return false;
    }
    const FString NativeRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    const FString GamePaks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    if (NativeRoot.IsEmpty() || GamePaks.IsEmpty())
    {
        Test.AddError(TEXT("The native source and game container fixture are required."));
        return false;
    }
    FP4ExportModRequest Request;
    Request.Mod.Id = AllRenderers ? TEXT("native.allrenderers.test")
        : TwoSided                ? TEXT("native.twosided.test")
        : Masked                  ? TEXT("native.masked.test")
        : Opaque                  ? (Lit ? TEXT("native.opaque.lit.test") : TEXT("native.opaque.test"))
        : Lit                     ? TEXT("native.transparent.lit.test")
                                  : TEXT("native.transparent.test");
    Request.Mod.Name = TwoSided ? TEXT("Native Two-Sided Test")
        : Masked                ? TEXT("Native Masked Test")
        : Opaque                ? (Lit ? TEXT("Native Lit Opaque Test") : TEXT("Native Opaque Test"))
        : Lit                   ? TEXT("Native Lit Transparent Test")
                                : TEXT("Native Unlit Transparent Test");
    Request.Mod.Version = TEXT("1.0.0");
    FP4ModNames Names;
    FString Error;
    if (!P4ModDerivedNames(Request.Mod, Names, Error))
    {
        Test.AddError(Error);
        return false;
    }
    const FString Work = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() /
        (AllRenderers      ? TEXT("KadanAllRenderersExport")
                : TwoSided ? TEXT("KadanTwoSidedExport")
                : Masked   ? TEXT("KadanMaskedExport")
                : Opaque   ? (Lit ? TEXT("KadanLitOpaqueExport") : TEXT("KadanOpaqueExport"))
                : Lit      ? TEXT("KadanLitTranslucentExport")
                           : TEXT("KadanTranslucentExport")));
    Request.ProjectDirectory = Work / TEXT("project");
    Request.OutputDirectory = Work / TEXT("out");
    Request.WorkDirectory = FPaths::ProjectIntermediateDir() / TEXT("P4MapKit") / FPaths::GetCleanFilename(Work) /
        TEXT("work");
    Request.GamePaksDirectory = GamePaks;
    Request.ExtractedGameDirectory = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_TRANSPARENT_GAMEFILES"));
    if (Request.ExtractedGameDirectory.IsEmpty())
        Request.ExtractedGameDirectory = NativeRoot / TEXT("host/Saved/P4MapKit/FullNativeCave000/game");
    Request.ShaderArchiveFile = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_TRANSPARENT_SHADER_ARCHIVE"));
    const FString Repo = FPaths::GetPath(FPaths::GetPath(NativeRoot));
    Request.Tools.Retoc = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETOC"));
    Request.Tools.Repak = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REPAK"));
    if (Request.Tools.Retoc.IsEmpty())
        Request.Tools.Retoc = Repo / TEXT("private/retoc/retoc.exe");
    if (Request.Tools.Repak.IsEmpty())
        Request.Tools.Repak = Repo / TEXT("private/repak/repak.exe");
    const FString FixtureDescriptor = Request.ProjectDirectory / TEXT("Config/KadanMods") /
        (Request.Mod.Id + TEXT(".json"));
    if (IFileManager::Get().FileExists(*FixtureDescriptor))
    {
        FP4ModDescriptor Existing;
        if (!P4ReadModDescriptor(Request.ProjectDirectory, Request.Mod.Id, Existing, Error) ||
            Existing.Format != Request.Mod.Format || Existing.Id != Request.Mod.Id ||
            Existing.Name != Request.Mod.Name || Existing.Version != Request.Mod.Version)
        {
            Test.AddError(TEXT("An unrelated descriptor occupies the owned qualification scratch project."));
            return false;
        }
    }
    else if (!P4WriteModDescriptor(Request.ProjectDirectory, Request.Mod, nullptr, Error))
    {
        Test.AddError(Error);
        return false;
    }
    // Weak: the cold child below is killed mid-test, and export may collect garbage (its game content index).
    TArray<TWeakObjectPtr<UObject>> Made;
    for (const TCHAR* Name : {TEXT("M_Own"), TEXT("MI_Own"), TEXT("SM_Own"), TEXT("T_Own"), TEXT("MI_Cold")})
        if (FindPackage(nullptr, *(Names.PackageRoot / Name)) ||
            FPackageName::DoesPackageExist(Names.PackageRoot / Name))
        {
            Test.AddError(
                TEXT("A qualification fixture already occupies the selected package; it will not be overwritten."));
            return false;
        }
    ON_SCOPE_EXIT
    {
        TArray<UPackage*> Packages;
        for (const TWeakObjectPtr<UObject>& Weak : Made)
        {
            if (UObject* Asset = Weak.Get())
                Packages.AddUnique(Asset->GetOutermost());
        }
        for (const TWeakObjectPtr<UObject>& Weak : Made)
        {
            UObject* Asset = Weak.Get();
            if (!IsValid(Asset))
                continue;
            FAssetRegistryModule::AssetDeleted(Asset);
            Asset->ClearFlags(RF_Public | RF_Standalone);
            Asset->GetOutermost()->SetDirtyFlag(false);
            // A unique name: a Blueprint takes its generated class along, and a BP_Staging_C left in the
            // transient package under its own name stopped the editor when the next test compiled its own.
            const FName Released = MakeUniqueObjectName(
                GetTransientPackage(), Asset->GetClass(), FName(*(Asset->GetName() + TEXT("_Released"))));
            Asset->Rename(*Released.ToString(), GetTransientPackage(),
                REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional);
            Asset->MarkPendingKill();
        }
        for (UPackage* Package : Packages)
        {
            if (!IsValid(Package) || Package == GetTransientPackage())
                continue;
            Package->SetDirtyFlag(false);
            Package->Rename(*(TEXT("/Temp/KadanTransparentFixture/") + FGuid::NewGuid().ToString(EGuidFormats::Digits)),
                nullptr, REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional);
            Package->MarkPendingKill();
        }
    };
    IFileManager::Get().MakeDirectory(*Work, true);
    const FString Image = FPaths::ConvertRelativePathToFull(Work / TEXT("creator.tga"));
    const TArray<uint8> Pixels = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 0, 32, 40, 255, 255, 255, 255, 255, 255,
        255, 128, 255, 255, 255, 64, 255, 255, 255, 0};
    const FString MeshInput = FPaths::ConvertRelativePathToFull(Work / TEXT("creator.obj"));
    const FString Obj = TEXT(
        "o CreatorPlane\nv -100 0 -100\nv 100 0 -100\nv 100 0 100\nv -100 0 100\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvn 0 -1 0\nf 1/1/1 3/3/1 2/2/1\nf 1/1/1 4/4/1 3/3/1\n");
    if (!FFileHelper::SaveArrayToFile(Pixels, *Image) ||
        !FFileHelper::SaveStringToFile(Obj, *MeshInput, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        Test.AddError(TEXT("The actual creator inputs could not be written."));
        return false;
    }
    auto Import = [&](bool Mesh) -> UObject*
    {
        const FString Leaf = Mesh ? TEXT("SM_Own") : TEXT("T_Own");
        TStrongObjectPtr<UFactory> Factory(Mesh ? static_cast<UFactory*>(NewObject<UFbxFactory>())
                                                : static_cast<UFactory*>(NewObject<UTextureFactory>()));
        Factory->AssetImportTask = NewObject<UAssetImportTask>(Factory.Get());
        Factory->AssetImportTask->bAutomated = true;
        if (UFbxFactory* Fbx = Cast<UFbxFactory>(Factory.Get()))
        {
            Fbx->SetDetectImportTypeOnImport(false);
            Fbx->ImportUI->MeshTypeToImport = FBXIT_StaticMesh;
            Fbx->ImportUI->bAutomatedImportShouldDetectType = false;
            Fbx->ImportUI->bImportMaterials = false;
            Fbx->ImportUI->bImportTextures = false;
        }
        bool Cancelled = false;
        UObject* Asset = Factory->ImportObject(Mesh ? UStaticMesh::StaticClass() : UTexture2D::StaticClass(),
            CreatePackage(*(Names.PackageRoot / Leaf)), FName(*Leaf), RF_Public | RF_Standalone | RF_Transactional,
            Mesh ? MeshInput : Image, nullptr, Cancelled);
        Factory->CleanUp();
        if (Asset && !Cancelled)
        {
            FAssetRegistryModule::AssetCreated(Asset);
            Made.Add(Asset);
            return Asset;
        }
        return nullptr;
    };
    TStrongObjectPtr<UTexture2D> Texture(Cast<UTexture2D>(Import(false)));
    TStrongObjectPtr<UStaticMesh> Mesh(Cast<UStaticMesh>(Import(true)));
    if (!Test.TestNotNull(TEXT("Actual creator texture imported"), Texture.Get()) ||
        !Test.TestNotNull(TEXT("Actual creator mesh imported"), Mesh.Get()))
        return false;
    auto& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    TStrongObjectPtr<UP4CreatorMaterialFactory> Factory(NewObject<UP4CreatorMaterialFactory>());
    TStrongObjectPtr<UMaterial> Graph(
        Cast<UMaterial>(Tools.CreateAsset(TEXT("M_Own"), Names.PackageRoot, UMaterial::StaticClass(), Factory.Get())));
    if (!Test.TestNotNull(TEXT("Native creator material factory made the editable graph"), Graph.Get()))
        return false;
    Made.Add(Graph.Get());
    Graph->BlendMode = Masked ? BLEND_Masked : Opaque ? BLEND_Opaque : BLEND_Translucent;
    Graph->SetShadingModel(Lit ? MSM_DefaultLit : MSM_Unlit);
    Graph->TwoSided = TwoSided;
    UMaterialExpressionScalarParameter* Alpha = NewObject<UMaterialExpressionScalarParameter>(Graph.Get());
    Alpha->ParameterName = TEXT("Alpha");
    Alpha->DefaultValue = 0.5f;
    UMaterialExpressionVectorParameter* Tint = NewObject<UMaterialExpressionVectorParameter>(Graph.Get());
    Tint->ParameterName = TEXT("Tint");
    Tint->DefaultValue = FLinearColor(1.f, 0.125f, 0.0625f, 1.f);
    UMaterialExpressionTextureSampleParameter2D* Mask = NewObject<UMaterialExpressionTextureSampleParameter2D>(
        Graph.Get());
    Mask->ParameterName = TEXT("Mask");
    Mask->Texture = Texture.Get();
    UMaterialExpressionMultiply* Opacity = NewObject<UMaterialExpressionMultiply>(Graph.Get());
    Opacity->A.Expression = Alpha;
    Opacity->B.Connect(4, Mask);
    Graph->Expressions = {Alpha, Tint, Mask, Opacity};
    if (Lit)
    {
        Graph->TranslucencyLightingMode = TLM_Surface;
        Graph->bEnableSeparateTranslucency = false;
        Graph->bUseTranslucencyVertexFog = false;
        UMaterialExpressionScalarParameter* Roughness = NewObject<UMaterialExpressionScalarParameter>(Graph.Get());
        Roughness->ParameterName = TEXT("Roughness");
        Roughness->DefaultValue = 0.35f;
        UMaterialExpressionScalarParameter* Metallic = NewObject<UMaterialExpressionScalarParameter>(Graph.Get());
        Metallic->ParameterName = TEXT("Metallic");
        Metallic->DefaultValue = 0.05f;
        UMaterialExpressionVectorParameter* Normal = NewObject<UMaterialExpressionVectorParameter>(Graph.Get());
        Normal->ParameterName = TEXT("Normal");
        Normal->DefaultValue = FLinearColor(0.f, 0.f, 1.f, 1.f);
        UMaterialExpressionScalarParameter* Specular = NewObject<UMaterialExpressionScalarParameter>(Graph.Get());
        Specular->ParameterName = TEXT("Specular");
        Specular->DefaultValue = 0.5f;
        Graph->Expressions.Add(Roughness);
        Graph->Expressions.Add(Metallic);
        Graph->Expressions.Add(Normal);
        Graph->Expressions.Add(Specular);
        Graph->bTangentSpaceNormal = true;
        Graph->BaseColor.Expression = Tint;
        Graph->Roughness.Expression = Roughness;
        Graph->Metallic.Expression = Metallic;
        Graph->Normal.Expression = Normal;
        Graph->Specular.Expression = Specular;
        if (Opaque)
        {
            Graph->MaterialDecalResponse = MDR_ColorRoughness;
            UMaterialExpressionScalarParameter* AO = NewObject<UMaterialExpressionScalarParameter>(Graph.Get());
            AO->ParameterName = TEXT("AO");
            AO->DefaultValue = 0.75f;
            UMaterialExpressionMultiply* TexturedTint = NewObject<UMaterialExpressionMultiply>(Graph.Get());
            TexturedTint->A.Expression = Tint;
            TexturedTint->B.Expression = Mask;
            Graph->Expressions.Add(AO);
            Graph->Expressions.Add(TexturedTint);
            Graph->AmbientOcclusion.Expression = AO;
            Graph->BaseColor.Expression = TexturedTint;
        }
    }
    else if (Opaque)
        Graph->EmissiveColor.Expression = Tint;
    else
    {
        Graph->bEnableSeparateTranslucency = true;
        Graph->bUseTranslucencyVertexFog = true;
        Graph->EmissiveColor.Expression = Tint;
    }
    if (Masked)
    {
        Graph->OpacityMask.Expression = Opacity;
        Graph->OpacityMaskClipValue = 0.3f;
    }
    if (AllRenderers)
    {
        // The creator's own usage checkboxes, as in UE: the map holds each renderer's versions and the runtime
        // material may be drawn by them (particles and beams, spline, skeletal and morph target meshes).
        Graph->bUsedWithParticleSprites = true;
        Graph->bUsedWithMeshParticles = true;
        Graph->bUsedWithBeamTrails = true;
        Graph->bUsedWithSplineMeshes = true;
        Graph->bUsedWithSkeletalMesh = true;
        Graph->bUsedWithMorphTargets = true;
    }
    else if (!Opaque)
        Graph->Opacity.Expression = Opacity;
    Graph->PostEditChange();
    if (!Test.TestTrue(TEXT("Actual new graph has positive creator source"), P4HasCreatorSource(Graph.Get(), &Error)))
    {
        Test.AddError(Error);
        return false;
    }
    // MaterialEditor evaluates a transient duplicate. Its UI-only graph check
    // must work while both origin-qualified producer entries refuse before
    // acquiring source (the deliberate nonexistent archive cannot become its error).
    {
        TStrongObjectPtr<UMaterial> PreviewGraph(
            DuplicateObject<UMaterial>(Graph.Get(), GetTransientPackage(), NAME_None));
        if (!Test.TestNotNull(TEXT("Material editor transient graph duplicate exists"), PreviewGraph.Get()))
            return false;
        PreviewGraph->SetFlags(RF_Transient);
        PreviewGraph->ClearFlags(RF_Public | RF_Standalone);
        Test.TestTrue(TEXT("The actual authored graph passes editor graph/settings feedback"),
            P4CreatorMaterialEditorGraphIssue(Graph.Get()).IsEmpty());
        Test.TestEqual(TEXT("Preview and authored graph use the same graph/settings feedback"),
            P4CreatorMaterialEditorGraphIssue(PreviewGraph.Get()), P4CreatorMaterialEditorGraphIssue(Graph.Get()));
        const FString OriginIssue = P4CreatorMaterialGraphIssue(PreviewGraph.Get());
        Test.TestFalse(TEXT("Transient preview never qualifies as a creator export graph"), OriginIssue.IsEmpty());
        FP4CreatorMaterialCompileRequest PreviewRequest;
        PreviewRequest.CreatorMaterial = PreviewGraph.Get();
        const FP4CreatorMaterialCompileResult RefusedPreview = P4CompileCreatorMaterial(PreviewRequest);
        Test.TestFalse(TEXT("Transient preview cannot compile or export"), RefusedPreview.Succeeded);
        Test.TestEqual(TEXT("Preview producer refuses the exact origin issue before compiling"), RefusedPreview.Error,
            OriginIssue);
    }
    TArray<FString> Unknown;
    // A masked child keeps the texels whose alpha is over the clip (Alpha 1: two of the four), so the game shows the cut.
    const FString InstanceJson = TEXT("{\"parent\":\"") + Graph->GetPathName() + TEXT("\",\"scalars\":{\"Alpha\":") +
        (Masked ? TEXT("1") : TEXT("0.25")) + TEXT("},\"vectors\":{\"Tint\":[0.125,1,0.0625,1]}}");
    TStrongObjectPtr<UMaterialInstanceConstant> Instance(
        P4BuildMaterialInstance(InstanceJson, Names.PackageRoot / TEXT("MI_Own"), Unknown, Error));
    if (!Test.TestNotNull(TEXT("Own instance preserves dynamic overrides"), Instance.Get()))
    {
        Test.AddError(Error);
        return false;
    }
    Made.Add(Instance.Get());
    FAssetRegistryModule::AssetCreated(Instance.Get());
    Mesh->SetMaterial(0, Instance.Get());
    FP4ExportRoot Root;
    Root.Path = Names.PackageRoot / TEXT("SM_Own");
    Request.Roots.Add(Root);
    // Static children use native instance permutations through the Switch compiler.
    {
        const FString RuntimeName = FP4TranslucentMaterialStaging::RuntimePackage(
            Names.PackageRoot, Graph->GetOutermost()->GetName());
        const bool StaticResource = Instance->bHasStaticPermutationResource;
        const FMaterialInstanceBasePropertyOverrides Base = Instance->BasePropertyOverrides;
        ON_SCOPE_EXIT
        {
            Instance->bHasStaticPermutationResource = StaticResource;
            Instance->BasePropertyOverrides = Base;
        };
        Instance->bHasStaticPermutationResource = true;
        const FP4ExportAvailability StaticIssue = P4ExportAssetAvailability(Instance.Get(), Request.Mod);
        Test.TestEqual(TEXT("Normal review admits child static permutations with the Switch compiler"),
            StaticIssue.Route == EP4ExportAssetRoute::CreatorMaterialInstance, P4UseSwitchShaderRoute());
        Test.TestTrue(
            TEXT("Review leaves the runtime destination unallocated"), FindPackage(nullptr, *RuntimeName) == nullptr);
        Test.TestTrue(TEXT("Review preserves editor material references"),
            Mesh->GetMaterial(0) == Instance.Get() && Instance->Parent == Graph.Get());
        Instance->bHasStaticPermutationResource = false;
        Instance->BasePropertyOverrides.bOverride_TwoSided = true;
        const FP4ExportAvailability BaseIssue = P4ExportAssetAvailability(Instance.Get(), Request.Mod);
        Test.TestEqual(TEXT("Own base-property override uses a permutation even without a static resource flag"),
            BaseIssue.Route == EP4ExportAssetRoute::CreatorMaterialInstance, P4UseSwitchShaderRoute());
    }
    const bool GraphDirty = Graph->GetOutermost()->IsDirty(), MeshDirty = Mesh->GetOutermost()->IsDirty(),
               InstanceDirty = Instance->GetOutermost()->IsDirty();
    TStrongObjectPtr<USkeletalMesh> ScopedSkeletal(NewObject<USkeletalMesh>(GetTransientPackage()));
    ScopedSkeletal->Materials.Add(FSkeletalMaterial(Graph.Get()));
    const FString BlueprintPackage = Names.PackageRoot / TEXT("BP_Staging");
    if (FindPackage(nullptr, *BlueprintPackage) || FPackageName::DoesPackageExist(BlueprintPackage))
    {
        Test.AddError(TEXT("The owned staging Blueprint destination is occupied."));
        return false;
    }
    TStrongObjectPtr<UBlueprint> ScopedBlueprint(
        FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), CreatePackage(*BlueprintPackage),
            TEXT("BP_Staging"), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()));
    if (!Test.TestNotNull(TEXT("Native staging Blueprint constructor succeeds"), ScopedBlueprint.Get()))
        return false;
    Made.Add(ScopedBlueprint.Get());
    USCS_Node* SkeletalNode = ScopedBlueprint->SimpleConstructionScript->CreateNode(
        USkeletalMeshComponent::StaticClass(), TEXT("StagingSkin"));
    ScopedBlueprint->SimpleConstructionScript->AddNode(SkeletalNode);
    USkeletalMeshComponent* SkeletalTemplate = CastChecked<USkeletalMeshComponent>(SkeletalNode->ComponentTemplate);
    SkeletalTemplate->SetSkeletalMesh(ScopedSkeletal.Get());
    SkeletalTemplate->SetMaterial(0, Graph.Get());
    const bool BlueprintDirty = ScopedBlueprint->GetOutermost()->IsDirty();
    // Exercise the same scoped object changes when an operation is abandoned
    // before a cook; ordinary destruction must restore references and dirty bits.
    {
        FP4TranslucentMaterialStaging Abandoned;
        if (!Test.TestTrue(TEXT("Actual compiled material staging prepares a private representation"),
                Abandoned.Prepare(Names.PackageRoot, {Graph.Get()}, Error)))
        {
            Test.AddError(Error);
            return false;
        }
        if (AllRenderers)
        {
            // The creator ticked "Used with Skeletal Mesh": the map holds the skin factory's versions.
            Test.TestTrue(TEXT("A graph used with skeletal meshes is remapped on a skeletal mesh: ") + Error,
                Abandoned.Remap(ScopedSkeletal.Get(), Error));
            Test.TestTrue(
                TEXT("A graph used with skeletal meshes is remapped on a Blueprint skeletal template: ") + Error,
                Abandoned.Remap(ScopedBlueprint.Get(), Error));
        }
        else
        {
            // A graph without that usage is refused on a skeletal mesh with what to do.
            Test.TestFalse(TEXT("Skeletal graph defaults refuse a material of your own"),
                Abandoned.Remap(ScopedSkeletal.Get(), Error));
            Test.TestTrue(TEXT("The skeletal refusal names a static mesh"), Error.Contains(TEXT("static mesh")));
            Test.TestFalse(TEXT("A Blueprint skeletal template refuses a material of your own"),
                Abandoned.Remap(ScopedBlueprint.Get(), Error));
            Test.TestTrue(TEXT("Skeletal refusals leave the default and the construction override on the graph"),
                ScopedSkeletal->Materials[0].MaterialInterface == Graph.Get() &&
                    SkeletalTemplate->GetMaterial(0) == Graph.Get());
        }
        TStrongObjectPtr<UDecalComponent> Decal(NewObject<UDecalComponent>(GetTransientPackage()));
        Decal->SetDecalMaterial(Instance.Get());
        Test.TestFalse(TEXT("A decal cannot use the qualified static shader through a child instance"),
            Abandoned.Remap(Decal.Get(), Error));
        Test.TestTrue(TEXT("Unsupported decal refusal preserves child and graph references"),
            Decal->GetDecalMaterial() == Instance.Get() && Instance->Parent == Graph.Get());
        TStrongObjectPtr<UInstancedStaticMeshComponent> Instanced(
            NewObject<UInstancedStaticMeshComponent>(GetTransientPackage()));
        Instanced->SetStaticMesh(Mesh.Get());
        Test.TestTrue(TEXT("Instanced inspection reads genuine mesh defaults without component overrides"),
            Instanced->OverrideMaterials.Num() == 0);
        // An opaque material's map holds the instanced mesh versions; masked and translucent ones do not yet.
        const bool InstancedCovered = Opaque && !Masked;
        Test.TestEqual(
            TEXT("Instanced rendering inherits the shader from mesh defaults exactly when the map covers it"),
            Abandoned.Remap(Instanced.Get(), Error), InstancedCovered);
        TStrongObjectPtr<UStaticMeshComponent> PlainStatic(NewObject<UStaticMeshComponent>(GetTransientPackage()));
        PlainStatic->SetStaticMesh(Mesh.Get());
        Test.TestTrue(TEXT("Plain static renderer accepts default-only own child shader"),
            Abandoned.Remap(PlainStatic.Get(), Error));
        Instanced->SetMaterial(0, Instance.Get());
        Test.TestEqual(TEXT("Instanced rendering through a child follows the map's coverage"),
            Abandoned.Remap(Instanced.Get(), Error), InstancedCovered);
        if (!InstancedCovered)
            Test.TestTrue(TEXT("Unsupported instanced refusal preserves the material reference"),
                Instanced->GetMaterial(0) == Instance.Get());
        // Save a genuine own child, remove its in-memory identity, then inspect
        // a supported soft-reference type in an unsupported rendering slot.
        // Its ancestry must be resolved before slot qualification, rather than
        // treating an unloaded child as unrelated to the authored graph.
        const FString ColdPackage = Names.PackageRoot / TEXT("MI_Cold");
        const FString ColdPath = ColdPackage + TEXT(".MI_Cold");
        const FString ColdHeader = FPackageName::LongPackageNameToFilename(
            ColdPackage, FPackageName::GetAssetPackageExtension());
        TStrongObjectPtr<UMaterialInstanceConstant> Cold(
            P4BuildMaterialInstance(InstanceJson, ColdPackage, Unknown, Error));
        if (!Test.TestNotNull(TEXT("Actual own cold child is built"), Cold.Get()))
        {
            Test.AddError(Error);
            return false;
        }
        Made.Add(Cold.Get());
        FAssetRegistryModule::AssetCreated(Cold.Get());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(ColdHeader), true);
        ON_SCOPE_EXIT
        {
            if (UPackage* LoadedCold = FindPackage(nullptr, *ColdPackage))
                ResetLoaders(LoadedCold);
            for (const TCHAR* Extension : {TEXT("uasset"), TEXT("uexp")})
            {
                const FString File = FPaths::ChangeExtension(ColdHeader, Extension);
                Test.TestTrue(TEXT("Owned cold-child source file is removed after loader release"),
                    !IFileManager::Get().FileExists(*File) || IFileManager::Get().Delete(*File, false, false));
            }
        };
        if (!Test.TestTrue(TEXT("Cold child is saved by the native uncooked package writer"),
                GEditor->SavePackage(Cold->GetOutermost(), Cold.Get(), RF_Public | RF_Standalone, *ColdHeader, GError,
                    nullptr, false, true, SAVE_KeepGUID | SAVE_KeepDirty | SAVE_NoError)))
            return false;
        UPackage* ColdOriginalPackage = Cold->GetOutermost();
        FAssetRegistryModule::AssetDeleted(Cold.Get());
        Cold->ClearFlags(RF_Public | RF_Standalone);
        Cold->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional);
        Cold->MarkPendingKill();
        Cold.Reset();
        ColdOriginalPackage->SetDirtyFlag(false);
        ColdOriginalPackage->Rename(*(TEXT("/Temp/KadanColdChild/") + FGuid::NewGuid().ToString(EGuidFormats::Digits)),
            nullptr, REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional);
        ColdOriginalPackage->MarkPendingKill();
        TStrongObjectPtr<UP4TypedMaterialValueFixture> SoftHolder(
            NewObject<UP4TypedMaterialValueFixture>(GetTransientPackage()));
        SoftHolder->MaterialSoft = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(ColdPath));
        if (!Test.TestTrue(TEXT("Cold soft child has no cached object or package before staging"),
                SoftHolder->MaterialSoft.Get() == nullptr && FindPackage(nullptr, *ColdPackage) == nullptr))
            return false;
        Test.TestFalse(TEXT("Unloaded own child cannot bypass the supported rendering-context refusal"),
            Abandoned.Remap(SoftHolder.Get(), Error));
        UMaterialInstanceConstant* ReloadedCold = Cast<UMaterialInstanceConstant>(SoftHolder->MaterialSoft.Get());
        if (!Test.TestNotNull(TEXT("Context inspection resolved the actual cold child package"), ReloadedCold))
            return false;
        Made.Add(ReloadedCold);
        Test.TestTrue(TEXT("Cold child refusal leaves exact soft identity and editor ancestry untouched"),
            SoftHolder->MaterialSoft.ToSoftObjectPath().ToString() == ColdPath && ReloadedCold->Parent == Graph.Get() &&
                Instance->Parent == Graph.Get());
        if (!Test.TestTrue(
                TEXT("Scoped creator instance parent is remapped for cook"), Abandoned.Remap(Instance.Get(), Error)))
        {
            Test.AddError(Error);
            return false;
        }
        Test.TestTrue(TEXT("The private runtime parent is the runtime material"),
            Abandoned.IsRuntimeAsset(Instance->Parent) && Instance->Parent->GetClass() == UMaterial::StaticClass());
        // Scope ends without publication, as it does after a later cook failure.
    }
    Test.TestTrue(TEXT("Abandoned staging restores skeletal default and Blueprint template assignments"),
        ScopedSkeletal->Materials[0].MaterialInterface == Graph.Get() &&
            SkeletalTemplate->GetMaterial(0) == Graph.Get());
    Test.TestEqual(TEXT("Abandoned staging restores Blueprint package dirty state"),
        ScopedBlueprint->GetOutermost()->IsDirty(), BlueprintDirty);
    Test.TestTrue(TEXT("Abandoned staging restores editor parent and dirty state"),
        Instance->Parent == Graph.Get() && Instance->GetOutermost()->IsDirty() == InstanceDirty);
    FP4ExportModResult Result;
    if (!Test.TestTrue(TEXT("Native transparent compile, cook, container census and atomic export"),
            P4ExportMod(Request, Result, Error)))
    {
        Test.AddError(Error);
        return false;
    }
    const FString RuntimePackage = FP4TranslucentMaterialStaging::RuntimePackage(
        Names.PackageRoot, Graph->GetOutermost()->GetName());
    Test.TestTrue(TEXT("Generated runtime material included"), Result.Packages.Contains(RuntimePackage));
    Test.TestFalse(TEXT("Editable graph is absent from the cooked runtime container"),
        Result.Packages.Contains(Graph->GetOutermost()->GetName()));
    Test.TestTrue(TEXT("Authored texture included"), Result.Packages.Contains(Texture->GetOutermost()->GetName()));
    Test.TestTrue(TEXT("Mesh editor material reference restored"), Mesh->GetMaterial(0) == Instance.Get());
    Test.TestTrue(TEXT("Own instance editor graph parent restored"), Instance->Parent == Graph.Get());
    Test.TestTrue(TEXT("Graph is still an editable UMaterial"), Graph->GetClass() == UMaterial::StaticClass());
    Test.TestTrue(TEXT("Editor dirty states restored"),
        Graph->GetOutermost()->IsDirty() == GraphDirty && Mesh->GetOutermost()->IsDirty() == MeshDirty &&
            Instance->GetOutermost()->IsDirty() == InstanceDirty);
    const FString Metadata = Result.Directory / TEXT("romfs/tsuchi/mods") / Request.Mod.Id;
    FString ManifestText;
    TSharedPtr<FJsonObject> Manifest;
    Test.TestTrue(TEXT("Published manifest readable"),
        FFileHelper::LoadFileToString(ManifestText, *(Metadata / TEXT("mod.json"))) &&
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ManifestText), Manifest));
    Test.TestTrue(TEXT("The manifest names no game material to copy shaders from"),
        Manifest.IsValid() && !Manifest->HasField(TEXT("shaders")));
    for (const FString& File : Result.Files)
        Test.TestFalse(
            TEXT("No loose shader library ships beside the container"), File.EndsWith(TEXT(".ushaderbytecode")));
    const int64 PakSize = IFileManager::Get().FileSize(*(Metadata / (Names.Container + TEXT("_P.pak"))));
    Test.TestTrue(TEXT("The mod's .pak holds the shader library"), PakSize > 4096);
    // The same graph compiles to the same block, and the shipped runtime material carries it.
    FP4CreatorMaterialCompileRequest Again;
    Again.CreatorMaterial = Graph.Get();
    Again.Instanced = Opaque && !Masked;
    Again.RuntimeObjectPath = RuntimePackage + TEXT(".") + FPackageName::GetLongPackageAssetName(RuntimePackage);
    const FP4CreatorMaterialCompileResult Recompiled = P4CompileCreatorMaterial(Again);
    if (!Test.TestTrue(TEXT("The graph compiles again"), Recompiled.Succeeded))
    {
        Test.AddError(Recompiled.Error);
        return false;
    }
    Test.AddInfo(FString::Printf(TEXT("Kadan's own map: %d versions, %d programs, block %d bytes, pak %lld bytes"),
        Recompiled.Versions, int32(Recompiled.Programs.size()), int32(Recompiled.Block.size()), PakSize));
    if (P4UseSwitchShaderRoute())
    {
        // Through UE's shaders a material used on a skeletal mesh (and its morph targets) is compiled for the GPU skin
        // factories as well (P4ApplyMaterialFactoryUsage; the factories' hashed names as material_factory.h records them).
        FP4CreatorMaterialCompileRequest Skinned = Again;
        Skinned.Factories = {0x391c2b78c893a52aull, 0xdd651790dd308138ull};
        const FP4CreatorMaterialCompileResult WithSkin = P4CompileCreatorMaterial(Skinned);
        if (!Test.TestTrue(TEXT("The graph compiles for skeletal meshes through UE's shaders"), WithSkin.Succeeded))
            Test.AddError(WithSkin.Error);
        else
            Test.TestTrue(TEXT("The skeletal mesh map holds the skin factories' shaders too"),
                WithSkin.Versions > Recompiled.Versions || AllRenderers);
    }
    // The normal exporter has independently extracted the finished container
    // for its texture proof. Inspect that exact packed payload, not the staging
    // copy or a package supplied by an original game container.
    const FString ReadbackDirectory = Request.WorkDirectory / (Request.Mod.Id + TEXT(".export-work/readback"));
    const FString FinalUtoc = Metadata / (Names.Container + TEXT("_P.utoc"));
    if (!Test.TestTrue(
            TEXT("Material-only readback resolves original export metadata without extracting game packages"),
            P4ExtractCreatorMaterialReadback(Request.Tools, FinalUtoc, Request.GamePaksDirectory,
                Request.WorkDirectory / (Request.Mod.Id + TEXT(".export-work")), ReadbackDirectory, Names.PackageRoot,
                Result.Packages, Error)))
    {
        Test.AddError(Error);
        return false;
    }
    const FString ReadbackRoot = ReadbackDirectory / TEXT("Carrot4/Content");
    for (const FString& Shipped : Result.Packages)
    {
        const FString Header = ReadbackRoot / Shipped.Mid(6) + TEXT(".uasset");
        TArray<uint8> Bytes;
        const int64 Body = IFileManager::Get().FileSize(*FPaths::ChangeExtension(Header, TEXT("uexp")));
        if (!Test.TestTrue(TEXT("Finished package header can be independently inspected"),
                Body >= 0 && FFileHelper::LoadFileToArray(Bytes, *Header)))
            return false;
        const auto Package = P4MapKit::ReadPackageHeader(
            std::vector<uint8>(Bytes.GetData(), Bytes.GetData() + Bytes.Num()), size_t(Body));
        if (!Test.TestTrue(TEXT("Finished package inventory is well formed"), Package.Error.Code.empty()))
            return false;
        Test.TestTrue(TEXT("Finished own package imports contain no editor preview material"),
            std::none_of(Package.Names.begin(), Package.Names.end(),
                [](const std::string& Name) { return Name.rfind("/Game/P4MapKit/Preview/", 0) == 0; }));
        for (const char* EditorName : {"/Script/Kadan", "/Script/P4MapKitAuthoring", "P4CreatorSourceData"})
            Test.TestTrue(TEXT("Finished package contains no editor source class or plugin reference"),
                std::find(Package.Names.begin(), Package.Names.end(), EditorName) == Package.Names.end());
        for (const char* GameMaterial : {"MI_stg_Area001_BayWindow01", "MI_chr_Dodoro_00_M02_LODAll",
                 "MI_demo_Area500_OP1_SecondHalf_C001_StarTube01"})
            Test.TestTrue(TEXT("Finished package names no game material"),
                std::find(Package.Names.begin(), Package.Names.end(), GameMaterial) == Package.Names.end());
    }
    TArray<uint8> RuntimeBody;
    if (Test.TestTrue(TEXT("The shipped runtime material's data reads back"),
            FFileHelper::LoadFileToArray(RuntimeBody, *(ReadbackRoot / RuntimePackage.Mid(6) + TEXT(".uexp")))))
        Test.TestTrue(TEXT("The shipped runtime material carries Kadan's own shader map"),
            std::search(RuntimeBody.GetData(), RuntimeBody.GetData() + RuntimeBody.Num(), Recompiled.Block.begin(),
                Recompiled.Block.end()) != RuntimeBody.GetData() + RuntimeBody.Num());
    // A graph the export cannot build refuses the next transaction and keeps the published output and editor references.
    if (P4UseSwitchShaderRoute())
    {
        // Through UE's shaders the export builds any graph UE compiles: one whose code does not compile refuses.
        UMaterialExpressionCustom* Broken = NewObject<UMaterialExpressionCustom>(Graph.Get());
        Broken->Code = TEXT("return KadanUndefinedValue;");
        Broken->OutputType = CMOT_Float3;
        Graph->Expressions.Add(Broken);
        const FColorMaterialInput Emissive = Graph->EmissiveColor;
        Graph->EmissiveColor.Expression = Broken;
        Test.TestFalse(
            TEXT("A graph that does not compile refuses the next transaction"), P4ExportMod(Request, Result, Error));
        Test.TestTrue(TEXT("The refusal says the material did not compile"), Error.Contains(TEXT("did not compile")));
        Graph->EmissiveColor = Emissive;
        Graph->Expressions.Remove(Broken);
    }
    else
    {
        Graph->SetShadingModel(MSM_Subsurface);
        Test.TestFalse(TEXT("A graph with an unsupported shading model refuses the next transaction"),
            P4ExportMod(Request, Result, Error));
        Test.TestTrue(TEXT("The refusal says what to change"), Error.Contains(TEXT("Default Lit")));
        Graph->SetShadingModel(Lit ? MSM_DefaultLit : MSM_Unlit);
    }
    FString After;
    Test.TestTrue(TEXT("Prior manifest survives the refusal"),
        FFileHelper::LoadFileToString(After, *(Metadata / TEXT("mod.json"))) && After == ManifestText);
    Test.TestTrue(TEXT("The refusal preserves all editor material references"),
        Mesh->GetMaterial(0) == Instance.Get() && Instance->Parent == Graph.Get());
    if (true)
    {
        // Repeat through the public entry after both native/render contracts
        // qualify. This request has no private acceptance opt-in.
        Request.ShaderArchiveFile.Reset();
        Test.TestTrue(TEXT("Normal public material capability is enabled"),
            P4ExportAssetAvailability(Graph.Get(), Request.Mod).Route == EP4ExportAssetRoute::CreatorMaterial);
        FP4ExportGameInventory Inventory;
        FP4ExportSelectionPreview Preview;
        FP4ExportRoot GraphRoot;
        GraphRoot.Path = Graph->GetOutermost()->GetName();
        if (!Test.TestTrue(TEXT("Normal panel inspects the authored material selection"),
                P4ReadExportGameInventory(
                    Request.Tools, Request.GamePaksDirectory, Request.WorkDirectory, Inventory, Error) &&
                    P4PreviewExportSelection(Request.Mod, {GraphRoot}, Inventory, {}, Preview, Error)))
        {
            Test.AddError(Error);
            return false;
        }
        const FP4KadanCreatorReview Review = P4ReviewCreatorSelection(Request.Mod, Preview, nullptr);
        Test.TestTrue(TEXT("Normal panel review admits the authored graph without test flags"),
            Review.Blockers.Num() == 0 && Review.Ships > 0);
        const FString Project = FPaths::ProjectDir();
        const FString DescriptorFile = Project / TEXT("Config/KadanMods") / (Request.Mod.Id + TEXT(".json"));
        if (IFileManager::Get().FileExists(*DescriptorFile))
        {
            Test.AddError(TEXT(
                "An existing project descriptor blocks the owned public-entry fixture; it will not be overwritten."));
            return false;
        }
        if (!P4WriteModDescriptor(Project, Request.Mod, nullptr, Error))
        {
            Test.AddError(Error);
            return false;
        }
        ON_SCOPE_EXIT
        {
            IFileManager::Get().Delete(*DescriptorFile, false, true);
        };
        const FString Reply = UP4AuthoringTools::NativeExportMod(Request.Mod.Id,
            Mesh->GetOutermost()->GetName() + TEXT(",") + Graph->GetOutermost()->GetName(), TEXT(""),
            Request.GamePaksDirectory, Request.OutputDirectory, Request.WorkDirectory, Request.Tools.Retoc,
            Request.Tools.Repak, Request.ExtractedGameDirectory);
        TSharedPtr<FJsonObject> Public;
        bool Valid = false, Published = false;
        if (!Test.TestTrue(TEXT("Normal editor NativeExportMod compiles, cooks and publishes without private opt-in"),
                FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Reply), Public) && Public.IsValid() &&
                    Public->TryGetBoolField(TEXT("valid"), Valid) && Valid &&
                    Public->TryGetBoolField(TEXT("published"), Published) && Published))
        {
            Test.AddError(Reply);
            return false;
        }
        Test.TestTrue(TEXT("Public entry also restores editor graph references"),
            Mesh->GetMaterial(0) == Instance.Get() && Instance->Parent == Graph.Get());
    }
    return !Test.HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TranslucentExportTest, "P4MapKit.Export.TranslucentCreatorContainer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TranslucentExportTest::RunTest(const FString&)
{
    return RunCreatorMaterialExport(*this, false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4LitTranslucentExportTest, "P4MapKit.Export.LitTranslucentCreatorContainer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4LitTranslucentExportTest::RunTest(const FString&)
{
    return RunCreatorMaterialExport(*this, true);
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OpaqueExportTest, "P4MapKit.Export.OpaqueCreatorContainer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OpaqueExportTest::RunTest(const FString&)
{
    return RunCreatorMaterialExport(*this, false, true);
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OpaqueLitExportTest, "P4MapKit.Export.OpaqueLitCreatorContainer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OpaqueLitExportTest::RunTest(const FString&)
{
    return RunCreatorMaterialExport(*this, true, true);
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MaskedExportTest, "P4MapKit.Export.MaskedCreatorContainer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MaskedExportTest::RunTest(const FString&)
{
    return RunCreatorMaterialExport(*this, true, true, true);
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TwoSidedExportTest, "P4MapKit.Export.TwoSidedCreatorContainer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TwoSidedExportTest::RunTest(const FString&)
{
    return RunCreatorMaterialExport(*this, true, true, false, true);
}
// Through UE's shaders only: a lit opaque graph whose usage names every renderer, for the Eden framing of each.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4AllRenderersExportTest, "P4MapKit.Export.AllRenderersCreatorContainer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4AllRenderersExportTest::RunTest(const FString&)
{
    if (!P4UseSwitchShaderRoute())
    {
        AddInfo(TEXT("Kadan's own maps cover static and instanced meshes only."));
        return true;
    }
    return RunCreatorMaterialExport(*this, true, true, false, false, true);
}
#endif

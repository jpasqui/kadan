#if P4_MAPKIT_TESTS
// The sweep's checks of what the coverage tracker lists as partly working (P4_NATIVE_SWEEP_PARTS), placed on and
// beside the material row so one export and one game run show them all:
// - lights: a red point light and a green spot light over the row;
// - textures: a second row of cubes on top of the first, one texture compression format each, all with mips;
// - skinned: a creator skeletal mesh with its own two-bone skeleton and a looping clip, through the FBX importer and
//   Kadan's rebake, as a creator makes one;
// - foliage: a creator foliage type painted with small cubes;
// - enemy (after skinned): a child of the Dwarf Bulborb's base class with the creature's body and a copy of the
//   game's animation Blueprint retargeted to the creature, with a creature declaration, which a game run fights.
#include "P4ExportDriverTestShared.h"
#include "P4SkinnedCreator.h"
#include "AssetExportTask.h"
#include "AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Animation/SkeletalMeshActor.h"
#include "AnimationUtils.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/PointLight.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SpotLight.h"
#include "Engine/StaticMeshActor.h"
#include "Exporters/Exporter.h"
#include "Exporters/FbxExportOption.h"
#include "FileHelpers.h"
#include "FoliageType_InstancedStaticMesh.h"
#include "InstancedFoliageActor.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "ReferenceSkeleton.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "P4AddedContent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Animation/AnimBlueprint.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "EditorAnimUtils.h"

namespace P4ExportDriverTest
{
namespace
{
struct FPartsSkeletonAccess : USkeleton
{
    static void Bones(USkeleton* S, const TArray<TPair<FName, FTransform>>& Chain)
    {
        S->*(&FPartsSkeletonAccess::Guid) = FGuid::NewGuid();
        FReferenceSkeletonModifier Modifier(S->*(&FPartsSkeletonAccess::ReferenceSkeleton), S);
        for (int32 Index = 0; Index < Chain.Num(); ++Index)
        {
            (S->*(&FPartsSkeletonAccess::BoneTree)).AddDefaulted();
            Modifier.Add(FMeshBoneInfo(Chain[Index].Key, Chain[Index].Key.ToString(), Index - 1), Chain[Index].Value);
        }
    }
};

// The FBX exporters are not exported from UnrealEd in 4.26, so they are made by class name.
UExporter* FbxExporter(const TCHAR* ClassName)
{
    UClass* Class = FindObject<UClass>(ANY_PACKAGE, ClassName);
    return Class ? NewObject<UExporter>(GetTransientPackage(), Class) : nullptr;
}

AActor* Spawn(UWorld* World, UClass* Class, const FString& Name, const FTransform& Where)
{
    FActorSpawnParameters Params;
    Params.Name = FName(*(TEXT("P4PartialCheck_") + Name));
    Params.OverrideLevel = World->PersistentLevel;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AActor* Actor = World->SpawnActor(Class, &Where, Params);
    if (Actor)
        Actor->SetActorLabel(Name);
    return Actor;
}

bool Lights(FAutomationTestBase& Test, UWorld* World, const FVector& At, TArray<FString>& Names, TArray<AActor*>& Actors)
{
    auto* Point = Cast<APointLight>(Spawn(World, APointLight::StaticClass(), TEXT("RedPointLight"),
        FTransform(At + FVector(300, 100, 200))));
    auto* Spot = Cast<ASpotLight>(Spawn(World, ASpotLight::StaticClass(), TEXT("GreenSpotLight"),
        FTransform(FRotator(-90, 0, 0), At + FVector(900, 100, 400))));
    // A second spot light turned both ways, which shows whether a rotation other than straight down survives.
    auto* Turned = Cast<ASpotLight>(Spawn(World, ASpotLight::StaticClass(), TEXT("BlueSpotLightTurned"),
        FTransform(FRotator(-60, 45, 0), At + FVector(600, -100, 400))));
    if (!Test.TestTrue(TEXT("The partial check lights are placed"), Point && Spot && Turned))
        return false;
    Turned->SpotLightComponent->SetMobility(EComponentMobility::Movable);
    Turned->SpotLightComponent->SetLightColor(FLinearColor(0.1f, 0.2f, 1.f));
    Turned->SpotLightComponent->SetIntensity(4000.f);
    Point->PointLightComponent->SetMobility(EComponentMobility::Movable);
    Point->PointLightComponent->SetLightColor(FLinearColor(1.f, 0.1f, 0.05f));
    Point->PointLightComponent->SetIntensity(20000.f);
    Point->PointLightComponent->SetAttenuationRadius(700.f);
    Spot->SpotLightComponent->SetMobility(EComponentMobility::Movable);
    Spot->SpotLightComponent->SetLightColor(FLinearColor(0.1f, 1.f, 0.1f));
    Spot->SpotLightComponent->SetIntensity(40000.f);
    Spot->SpotLightComponent->SetAttenuationRadius(900.f);
    Spot->SpotLightComponent->SetOuterConeAngle(30.f);
    for (AActor* Actor : {static_cast<AActor*>(Point), static_cast<AActor*>(Spot), static_cast<AActor*>(Turned)})
    {
        Names.Add(Actor->GetActorLabel() + TEXT(" at ") + Actor->GetActorLocation().ToString());
        Actors.Add(Actor);
    }
    return true;
}

bool Textures(FAutomationTestBase& Test, UWorld* World, const FString& Root, const FVector& At, UStaticMesh* Cube,
    TArray<UObject*>& Made, TArray<FString>& Names, TArray<AActor*>& Actors)
{
    // One opaque checker per format; the formats Kadan's texture rows name.
    struct FFormat
    {
        const TCHAR* Name;
        TextureCompressionSettings Settings;
        bool Alpha;
    };
    const FFormat Formats[] = {{TEXT("Default"), TC_Default, false}, {TEXT("DefaultAlpha"), TC_Default, true},
        {TEXT("Grayscale"), TC_Grayscale, false}, {TEXT("Masks"), TC_Masks, false},
        {TEXT("UserInterface"), TC_EditorIcon, false}, {TEXT("HDR"), TC_HDR, false},
        {TEXT("HDRCompressed"), TC_HDR_Compressed, false}, {TEXT("BC7"), TC_BC7, false}};
    TArray<UPackage*> Packages;
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Formats); ++Index)
    {
        const FFormat& Format = Formats[Index];
        UTexture2D* Texture = ImportTga(Test, Root, FString(TEXT("T_Format_")) + Format.Name,
            [&](int32 X, int32 Y)
            {
                const bool Cell = ((X / 8 + Y / 8) % 2) != 0;
                return Cell ? FColor(30, 160, 60, Format.Alpha ? 0 : 255) : FColor(240, 240, 240, 255);
            },
            false);
        if (!Texture)
            return false;
        Texture->CompressionSettings = Format.Settings;
        if (Format.Settings == TC_Masks || Format.Settings == TC_HDR || Format.Settings == TC_HDR_Compressed)
            Texture->SRGB = false;
        Texture->MipGenSettings = TMGS_FromTextureGroup;
        Texture->PostEditChange();
        const FString MaterialName = FString(TEXT("M_Format_")) + Format.Name;
        auto* Material =
            NewObject<UMaterial>(CreatePackage(*(Root / MaterialName)), *MaterialName, RF_Public | RF_Standalone | RF_Transactional);
        FAssetRegistryModule::AssetCreated(Material);
        auto* Sample = NewObject<UMaterialExpressionTextureSample>(Material);
        Sample->Texture = Texture;
        Sample->AutoSetSampleType();
        Material->Expressions.Add(Sample);
        Material->BaseColor.Connect(0, Sample);
        if (Format.Alpha)
        {
            Material->BlendMode = BLEND_Masked;
            Material->OpacityMask.Connect(4, Sample);
        }
        Material->PreEditChange(nullptr);
        Material->PostEditChange();
        Made.Append({Texture, Material});
        Packages.Append({Texture->GetOutermost(), Material->GetOutermost()});
        const FVector Where = At + FVector(Index * 150.f, 0.f, 100.f);
        auto* Actor = Cast<AStaticMeshActor>(Spawn(World, AStaticMeshActor::StaticClass(), MaterialName, FTransform(Where)));
        if (!Test.TestNotNull(TEXT("A texture format cube is placed: ") + MaterialName, Actor))
            return false;
        Actor->GetStaticMeshComponent()->SetStaticMesh(Cube);
        Actor->GetStaticMeshComponent()->SetMaterial(0, Material);
        Names.Add(MaterialName + TEXT(" at ") + Where.ToString());
        Actors.Add(Actor);
    }
    return Test.TestTrue(TEXT("The texture format assets save"), UEditorLoadingAndSavingUtils::SavePackages(Packages, true));
}

// Two stacked boxes, the lower on bone kosi and the upper on bone atama, and a one-second clip that bobs kosi
// and swings atama. Made in memory, written to FBX and imported back through Unreal's factories, then rebaked by
// Kadan, so the assets are the creator's as a creator's own FBX import would make them.
bool Skinned(FAutomationTestBase& Test, UWorld* World, const FString& Root, const FVector& At, TArray<UObject*>& Made,
    TArray<FString>& Names, TArray<AActor*>& Actors)
{
    TArray<TPair<FName, FTransform>> Chain;
    Chain.Emplace(TEXT("kosi"), FTransform::Identity);
    Chain.Emplace(TEXT("atama"), FTransform(FVector(0, 0, 40)));
    USkeleton* Skeleton = NewObject<USkeleton>(CreatePackage(TEXT("/Temp/P4PartialSkeleton")), TEXT("P4PartialSkeleton"));
    FPartsSkeletonAccess::Bones(Skeleton, Chain);
    USkeletalMesh* Mesh = NewObject<USkeletalMesh>(CreatePackage(TEXT("/Temp/P4PartialCreature")), TEXT("Creature"));
    Mesh->Skeleton = Skeleton;
    {
        FReferenceSkeletonModifier Modifier(Mesh->RefSkeleton, Skeleton);
        for (int32 Index = 0; Index < Chain.Num(); ++Index)
            Modifier.Add(FMeshBoneInfo(Chain[Index].Key, Chain[Index].Key.ToString(), Index - 1), Chain[Index].Value);
    }
    Mesh->AddLODInfo();
    FSkeletalMeshLODModel* Model = new FSkeletalMeshLODModel();
    Mesh->GetImportedModel()->LODModels.Add(Model);
    FSkelMeshSection& Section = Model->Sections.AddDefaulted_GetRef();
    Section.MaxBoneInfluences = 1;
    Section.BoneMap = {0, 1};
    Section.bCastShadow = true;
    // Each box: six faces of four corners with the face's normal, 40 cm wide and 40 cm tall.
    const FVector Normals[] = {FVector(1, 0, 0), FVector(-1, 0, 0), FVector(0, 1, 0), FVector(0, -1, 0),
        FVector(0, 0, 1), FVector(0, 0, -1)};
    for (int32 Box = 0; Box < 2; ++Box)
        for (const FVector& N : Normals)
        {
            const FVector U = FMath::Abs(N.Z) > 0.5f ? FVector(1, 0, 0) : FVector(0, 0, 1);
            const FVector V = FVector::CrossProduct(N, U);
            const FVector Centre = FVector(0, 0, 20 + Box * 40) + N * 20;
            const int32 First = Section.SoftVertices.Num();
            const FVector2D Corners[] = {FVector2D(-1, -1), FVector2D(1, -1), FVector2D(1, 1), FVector2D(-1, 1)};
            for (const FVector2D& C : Corners)
            {
                FSoftSkinVertex& Vertex = Section.SoftVertices.AddZeroed_GetRef();
                Vertex.Position = Centre + U * (C.X * 20) + V * (C.Y * 20);
                Vertex.TangentX = U;
                Vertex.TangentY = V;
                Vertex.TangentZ = FVector4(N, 1);
                Vertex.Color = FColor::White;
                Vertex.InfluenceBones[0] = uint8(Box);
                Vertex.InfluenceWeights[0] = 255;
                Vertex.UVs[0] = FVector2D(C.X * 0.5f + 0.5f, C.Y * 0.5f + 0.5f);
            }
            for (int32 Corner : {0, 2, 1, 0, 3, 2})
                Model->IndexBuffer.Add(uint32(First + Corner));
        }
    Section.NumVertices = Section.SoftVertices.Num();
    Section.NumTriangles = Model->IndexBuffer.Num() / 3;
    Model->NumVertices = Section.NumVertices;
    Model->NumTexCoords = 1;
    Model->RequiredBones = {0, 1};
    Model->ActiveBoneIndices = {0, 1};
    Model->MaxImportVertex = Section.NumVertices - 1;
    Model->SyncronizeUserSectionsDataArray();
    Mesh->Materials.Add(
        FSkeletalMaterial(UMaterial::GetDefaultMaterial(MD_Surface), true, false, TEXT("Body"), TEXT("Body")));
    Mesh->SetImportedBounds(FBoxSphereBounds(FVector(0, 0, 40), FVector(20, 20, 40), 45.f));
    Mesh->InvalidateDeriveDataCacheGUID();
    Mesh->CalculateInvRefMatrices();
    Mesh->PostEditChange();
    UAnimSequence* Clip = NewObject<UAnimSequence>(CreatePackage(TEXT("/Temp/P4PartialClip")), TEXT("Swing"));
    Clip->SetSkeleton(Skeleton);
    Clip->SequenceLength = 1.f;
    Clip->SetRawNumberOfFrame(31);
    Clip->BoneCompressionSettings = FAnimationUtils::GetDefaultAnimationBoneCompressionSettings();
    Clip->CurveCompressionSettings = FAnimationUtils::GetDefaultAnimationCurveCompressionSettings();
    FRawAnimSequenceTrack Body, Head;
    for (int32 Frame = 0; Frame < 31; ++Frame)
    {
        const float Phase = FMath::Sin(Frame / 30.f * 2.f * PI);
        Body.PosKeys.Add(FVector(0, 0, 15.f * FMath::Abs(Phase)));
        Body.RotKeys.Add(FQuat::Identity);
        Head.PosKeys.Add(FVector(0, 0, 40));
        Head.RotKeys.Add(FRotator(0, 70.f * Phase, 25.f * Phase).Quaternion());
    }
    Body.ScaleKeys = {FVector::OneVector};
    Head.ScaleKeys = {FVector::OneVector};
    Clip->AddNewRawTrack(TEXT("kosi"), &Body);
    Clip->AddNewRawTrack(TEXT("atama"), &Head);
    Clip->MarkRawDataAsModified(false);
    Clip->RequestSyncAnimRecompression(false);
    Clip->SetPreviewMesh(Mesh, false);

    const FString Scratch = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("P4MapKit/PartialChecks"));
    IFileManager::Get().MakeDirectory(*Scratch, true);
    UFbxExportOption* Options = NewObject<UFbxExportOption>();
    Options->Collision = false;
    Options->LevelOfDetail = false;
    Options->bExportPreviewMesh = false;
    for (UObject* Object : TArray<UObject*>{Mesh, Clip})
    {
        UAssetExportTask* Task = NewObject<UAssetExportTask>();
        Task->Object = Object;
        Task->Options = Options;
        Task->Exporter = FbxExporter(Object == Mesh ? TEXT("SkeletalMeshExporterFBX") : TEXT("AnimSequenceExporterFBX"));
        Task->Filename = Scratch / (Object->GetName() + TEXT(".fbx"));
        Task->bAutomated = true;
        Task->bPrompt = false;
        Task->bReplaceIdentical = true;
        if (!Test.TestTrue(TEXT("The creature's FBX is written: ") + Object->GetName(), UExporter::RunAssetExportTask(Task)))
            return false;
    }
    const FString Source = Root / TEXT("CreatureSource"), Rebaked = Root / TEXT("Creature");
    auto Import = [&](const FString& Name, USkeleton* Target) -> UObject*
    {
        UFbxFactory* Factory = NewObject<UFbxFactory>();
        Factory->SetDetectImportTypeOnImport(false);
        P4ConfigureSkinnedFbx(Factory->ImportUI, Target, 1.f, 0.f, 30);
        UAssetImportTask* Task = NewObject<UAssetImportTask>();
        Task->Factory = Factory;
        Task->Options = Factory->ImportUI;
        Task->Filename = Scratch / (Name + TEXT(".fbx"));
        Task->DestinationPath = Source;
        Task->DestinationName = Name;
        Task->bAutomated = true;
        Task->bSave = false;
        FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().ImportAssetTasks({Task});
        return Task->Result.Num() ? Task->Result[0] : FindObject<UObject>(nullptr, *(Source / Name + TEXT(".") + Name));
    };
    auto* ImportedMesh = Cast<USkeletalMesh>(Import(TEXT("Creature"), nullptr));
    if (!Test.TestNotNull(TEXT("The creature's FBX imports as a skeletal mesh"), ImportedMesh))
        return false;
    auto* ImportedClip = Cast<UAnimSequence>(Import(TEXT("Swing"), ImportedMesh->Skeleton));
    if (!Test.TestNotNull(TEXT("The creature's clip imports"), ImportedClip))
        return false;
    // The imported copies are only the rebake's input; nothing in the level uses them, so they do not ship.
    const FString Reply = UP4SkinnedCreatorTools::RebakeSkeletalAssets(ImportedMesh, {ImportedClip}, Rebaked, 1.f, 0.f);
    if (!Test.TestTrue(TEXT("Kadan rebakes the creature: ") + Reply, Reply.StartsWith(TEXT("Created the rebaked"))))
        return false;
    auto* CreatureMesh = LoadObject<USkeletalMesh>(nullptr, *(Rebaked / TEXT("Creature.Creature")));
    auto* CreatureClip = LoadObject<UAnimSequence>(nullptr, *(Rebaked / TEXT("Swing.Swing")));
    if (!Test.TestTrue(TEXT("The rebaked creature and clip exist"), CreatureMesh && CreatureClip))
        return false;
    Made.Append({CreatureMesh, CreatureMesh->Skeleton, CreatureClip});
    // Its own material: a checker, so the bending shows.
    UTexture2D* Skin = ImportTga(Test, Root, TEXT("T_CreatureSkin"),
        [](int32 X, int32 Y) { return ((X / 16 + Y / 16) % 2) ? FColor(200, 40, 200, 255) : FColor(250, 250, 250, 255); },
        false);
    if (!Skin)
        return false;
    auto* Material = NewObject<UMaterial>(
        CreatePackage(*(Rebaked / TEXT("M_CreatureSkin"))), TEXT("M_CreatureSkin"), RF_Public | RF_Standalone | RF_Transactional);
    FAssetRegistryModule::AssetCreated(Material);
    auto* Sample = NewObject<UMaterialExpressionTextureSample>(Material);
    Sample->Texture = Skin;
    Sample->AutoSetSampleType();
    Material->Expressions.Add(Sample);
    Material->BaseColor.Connect(0, Sample);
    Material->PreEditChange(nullptr);
    Material->PostEditChange();
    CreatureMesh->Materials[0].MaterialInterface = Material;
    CreatureMesh->PostEditChange();
    Made.Append({Skin, Material});
    TArray<UPackage*> Packages;
    for (UObject* Object : {static_cast<UObject*>(CreatureMesh), static_cast<UObject*>(CreatureMesh->Skeleton),
             static_cast<UObject*>(CreatureClip), static_cast<UObject*>(Skin), static_cast<UObject*>(Material)})
        Packages.AddUnique(Object->GetOutermost());
    if (!Test.TestTrue(TEXT("The creature's assets save"), UEditorLoadingAndSavingUtils::SavePackages(Packages, true)))
        return false;
    for (int32 Index = 0; Index < 2; ++Index)
    {
        const FString Name = Index ? TEXT("CreatureHalfSpeed") : TEXT("Creature");
        const FVector Where = At + FVector(300 + Index * 300.f, 200, 50);
        auto* Actor = Cast<ASkeletalMeshActor>(Spawn(World, ASkeletalMeshActor::StaticClass(), Name, FTransform(Where)));
        if (!Test.TestNotNull(TEXT("The creature is placed"), Actor))
            return false;
        USkeletalMeshComponent* Component = Actor->GetSkeletalMeshComponent();
        Component->SetSkeletalMesh(CreatureMesh);
        Component->SetAnimationMode(EAnimationMode::AnimationSingleNode);
        Component->AnimationData.AnimToPlay = CreatureClip;
        Component->AnimationData.bSavedLooping = true;
        Component->AnimationData.bSavedPlaying = true;
        Component->AnimationData.SavedPlayRate = Index ? 0.5f : 1.f;
        Component->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        Names.Add(Name + TEXT(" at ") + Where.ToString());
        Actors.Add(Actor);
    }
    return true;
}

bool Foliage(FAutomationTestBase& Test, UWorld* World, const FString& Root, const FVector& At, UStaticMesh* Cube,
    TArray<UObject*>& Made, TArray<FString>& Names, TArray<AActor*>& Actors)
{
    auto* Type = NewObject<UFoliageType_InstancedStaticMesh>(
        CreatePackage(*(Root / TEXT("FT_SmallCube"))), TEXT("FT_SmallCube"), RF_Public | RF_Standalone | RF_Transactional);
    FAssetRegistryModule::AssetCreated(Type);
    Type->SetStaticMesh(Cube);
    Made.Add(Type);
    if (!Test.TestTrue(TEXT("The foliage type saves"), UEditorLoadingAndSavingUtils::SavePackages({Type->GetOutermost()}, true)))
        return false;
    // A level that already has a foliage actor is edited, not added to.
    const bool Existed = AInstancedFoliageActor::GetInstancedFoliageActorForLevel(World->PersistentLevel, false) != nullptr;
    AInstancedFoliageActor* Foliage = AInstancedFoliageActor::GetInstancedFoliageActorForLevel(World->PersistentLevel, true);
    if (!Test.TestNotNull(TEXT("The level has a foliage actor"), Foliage))
        return false;
    FFoliageInfo* Info = nullptr;
    UFoliageType* Added = Foliage->AddFoliageType(Type, &Info);
    if (!Test.TestTrue(TEXT("The foliage type is added to the level"), Added && Info))
        return false;
    // Twelve small cubes in a ring on top of the second row's middle cubes.
    for (int32 Index = 0; Index < 12; ++Index)
    {
        FFoliageInstance Instance;
        const float Angle = Index / 12.f * 2.f * PI;
        Instance.Location = At + FVector(1125.f, 200.f, 75.f) + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * 110.f;
        Instance.Rotation = FRotator(0, Index * 30.f, 0);
        Instance.DrawScale3D = FVector(0.25f);
        Info->AddInstance(Foliage, Added, Instance);
    }
    Info->Refresh(Foliage, true, true);
    Names.Add(FString::Printf(TEXT("Foliage FT_SmallCube x12 around %s, %s foliage actor"),
        *(At + FVector(1125.f, 200.f, 75.f)).ToString(), Existed ? TEXT("the level's") : TEXT("a new")));
    if (!Existed)
        Actors.Add(Foliage);
    return true;
}
bool Enemy(FAutomationTestBase& Test, UWorld* World, const FString& Root, FP4ExportModRequest& Request,
    TArray<UObject*>& Made, TArray<FString>& Names)
{
    // The way the game's own variants are made: a child of the Dwarf Bulborb's base class, which brings its AI, hit
    // spheres and carrying, with the creator's body and an animation Blueprint copied from the game's and retargeted
    // to the creator's skeleton, so it keeps the state names, transitions and notifies the AI asks for.
    const TCHAR* BasePath = TEXT("/Game/Carrot4/Placeables/Teki/Base/GKochappyBase.GKochappyBase_C");
    // A load of a package that is not there yet would leave an empty package behind that the import's later load
    // finds instead of the file, so the class is only loaded once its package exists.
    UClass* Parent = FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(FString(BasePath)))
        ? LoadObject<UClass>(nullptr, BasePath, nullptr, LOAD_NoWarn | LOAD_Quiet)
        : nullptr;
    if (!Parent)
    {
        const FString Reply = UP4AuthoringTools::NativeKadanImportMapFromContainers(Request.ExtractedGameDirectory,
            TEXT("/Game/Carrot4/Placeables/Teki/GKochappy"), Request.GamePaksDirectory, Request.Tools.Retoc,
            Request.Tools.Repak);
        Parent = LoadObject<UClass>(nullptr, BasePath, nullptr, LOAD_NoWarn | LOAD_Quiet);
        if (!Test.TestNotNull(TEXT("The Dwarf Bulborb's base class imports: ") + Reply.Left(800), Parent))
            return false;
    }
    auto* GameAnimation = LoadObject<UAnimBlueprint>(
        nullptr, TEXT("/Game/Carrot4/Characters/Teki/Kochappy/ABP_Kochappy.ABP_Kochappy"), nullptr, LOAD_NoWarn | LOAD_Quiet);
    auto* Body = LoadObject<USkeletalMesh>(nullptr, *(Root / TEXT("Creature/Creature.Creature")));
    auto* Skin = LoadObject<UMaterialInterface>(nullptr, *(Root / TEXT("Creature/M_CreatureSkin.M_CreatureSkin")));
    auto* Swing = LoadObject<UAnimSequence>(nullptr, *(Root / TEXT("Creature/Swing.Swing")));
    auto* Cube = LoadObject<UStaticMesh>(nullptr, *(Root / TEXT("SM_RowCube.SM_RowCube")));
    if (!Test.TestTrue(TEXT("The game's animation Blueprint and the creature's mesh, clip, material and carcass exist"),
            GameAnimation && GameAnimation->TargetSkeleton && Body && Body->Skeleton && Skin && Swing && Cube))
        return false;
    // Unreal's Retarget Anim Blueprints: the copy and the clips it plays move to the creature's skeleton.
    const FString AnimationFolder = Root / TEXT("BoxBugAnimation");
    EditorAnimUtils::FNameDuplicationRule Rule;
    Rule.FolderPath = AnimationFolder;
    Rule.ReplaceFrom = TEXT("Kochappy");
    Rule.ReplaceTo = TEXT("BoxBug");
    TArray<TWeakObjectPtr<UObject>> Retarget = {GameAnimation};
    UObject* Copied = EditorAnimUtils::RetargetAnimations(
        GameAnimation->TargetSkeleton, Body->Skeleton, Retarget, true, &Rule, false);
    UAnimBlueprint* Animation = Cast<UAnimBlueprint>(Copied);
    if (!Animation)
        Animation = LoadObject<UAnimBlueprint>(nullptr, *(AnimationFolder / TEXT("ABP_BoxBug.ABP_BoxBug")), nullptr,
            LOAD_NoWarn | LOAD_Quiet);
    if (!Test.TestTrue(TEXT("The game's animation Blueprint is copied onto the creature's skeleton"),
            Animation && Animation->TargetSkeleton == Body->Skeleton))
        return false;
    // The creator's own clip in the states that wait: the copy's other states keep the retargeted game clips, whose
    // lengths and notifies the AI times its states by.
    int32 Swapped = 0;
    TArray<UEdGraph*> Graphs;
    Animation->GetAllGraphs(Graphs);
    for (UEdGraph* Graph : Graphs)
        for (UEdGraphNode* Node : Graph ? Graph->Nodes : TArray<UEdGraphNode*>())
            if (auto* Player = Cast<UAnimGraphNode_SequencePlayer>(Node))
                if (Player->Node.Sequence && Player->Node.Sequence->GetName().Contains(TEXT("Wait")))
                {
                    Player->Node.Sequence = Swing;
                    ++Swapped;
                }
    FKismetEditorUtilities::CompileBlueprint(Animation);
    if (!Test.TestTrue(FString::Printf(TEXT("The copied animation Blueprint compiles with the creature's clip in %d states"), Swapped),
            Animation->GeneratedClass && Animation->Status != BS_Error && Swapped > 0))
        return false;
    const FString ChildPackage = Root / TEXT("BP_BoxBug");
    UBlueprint* Child = FKismetEditorUtilities::CreateBlueprint(Parent, CreatePackage(*ChildPackage), TEXT("BP_BoxBug"),
        BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    if (!Test.TestNotNull(TEXT("The enemy's child class is made"), Child))
        return false;
    FAssetRegistryModule::AssetCreated(Child);
    // The inherited body draws the creature with its own animation Blueprint and material; the carcass is the
    // creator's. A part the game's C++ class makes is set on the child's class defaults instead.
    const auto Override = [&](FName Name, UActorComponent* Template)
    {
        if (auto* Mesh = Cast<USkeletalMeshComponent>(Template))
        {
            Mesh->SkeletalMesh = Body;
            Mesh->AnimClass = Animation->GeneratedClass;
            Mesh->OverrideMaterials = {Skin};
        }
        else if (auto* Carcass = Cast<UStaticMeshComponent>(Template))
        {
            Carcass->SetStaticMesh(Cube);
            Carcass->OverrideMaterials = {Skin};
            Carcass->SetRelativeScale3D(FVector(0.3f, 0.3f, 0.15f));
        }
    };
    TArray<FString> Overridden;
    UInheritableComponentHandler* Handler = Child->GetInheritableComponentHandler(true);
    for (UClass* Class = Parent; Class; Class = Class->GetSuperClass())
        if (auto* Generated = Cast<UBlueprintGeneratedClass>(Class))
            if (Generated->SimpleConstructionScript)
                for (USCS_Node* Node : Generated->SimpleConstructionScript->GetAllNodes())
                {
                    const FName Name = Node ? Node->GetVariableName() : NAME_None;
                    if ((Name == TEXT("CarrotSkeletalMesh") || Name == TEXT("Carcass")) &&
                        !Overridden.Contains(Name.ToString()))
                        if (UActorComponent* Template = Handler->CreateOverridenComponentTemplate(FComponentKey(Node)))
                        {
                            Override(Name, Template);
                            Overridden.Add(Name.ToString());
                        }
                }
    FKismetEditorUtilities::CompileBlueprint(Child);
    if (AActor* Defaults = Child->GeneratedClass ? Child->GeneratedClass->GetDefaultObject<AActor>() : nullptr)
        for (const TCHAR* Part : {TEXT("CarrotSkeletalMesh"), TEXT("Carcass")})
            if (!Overridden.Contains(Part))
                if (auto* Template = Cast<UActorComponent>(Defaults->GetDefaultSubobjectByName(Part)))
                {
                    Template->Modify();
                    Override(Part, Template);
                    Overridden.Add(FString(Part) + TEXT(" (native)"));
                }
    // The parts the class has from its parents. The game's GKochappy adds the Dwarf Bulborb's AI component to this base
    // itself, so the creator's class does too, as a component of its own.
    TArray<FString> Inherited;
    bool HasAI = false;
    UClass* TekiAI = FindObject<UClass>(ANY_PACKAGE, TEXT("TekiAIComponent"));
    for (UClass* Class = Parent; Class; Class = Class->GetSuperClass())
        if (auto* Generated = Cast<UBlueprintGeneratedClass>(Class))
            if (Generated->SimpleConstructionScript)
                for (USCS_Node* Node : Generated->SimpleConstructionScript->GetAllNodes())
                    if (Node && Node->ComponentClass)
                    {
                        Inherited.AddUnique(Node->GetVariableName().ToString());
                        HasAI |= TekiAI && Node->ComponentClass->IsChildOf(TekiAI);
                    }
    if (AActor* Defaults = Parent->GetDefaultObject<AActor>())
    {
        TInlineComponentArray<UActorComponent*> Native(Defaults);
        for (UActorComponent* Part : Native)
            if (Part)
            {
                Inherited.AddUnique(Part->GetName());
                HasAI |= TekiAI && Part->IsA(TekiAI);
            }
    }
    TArray<FString> Added;
    if (!HasAI)
    {
        UClass* AIClass = FindObject<UClass>(ANY_PACKAGE, TEXT("KochappyAIComponent"));
        if (!Test.TestNotNull(TEXT("The game's Dwarf Bulborb AI component class exists"), AIClass))
            return false;
        USCS_Node* Node = Child->SimpleConstructionScript->CreateNode(AIClass, TEXT("KochappyAI"));
        Child->SimpleConstructionScript->AddNode(Node);
        Added.Add(TEXT("KochappyAI"));
    }
    FBlueprintEditorUtils::MarkBlueprintAsModified(Child);
    FKismetEditorUtilities::CompileBlueprint(Child);
    Names.Add(TEXT("Enemy parts inherited: ") + FString::Join(Inherited, TEXT(", ")) + TEXT("; added: ") +
        FString::Join(Added, TEXT(", ")));
    if (!Test.TestTrue(TEXT("The enemy's body is the creature: ") + FString::Join(Overridden, TEXT(", ")),
            Overridden.ContainsByPredicate([](const FString& Part) { return Part.StartsWith(TEXT("CarrotSkeletalMesh")); }) &&
                Child->GeneratedClass && Child->Status != BS_Error))
        return false;
    const FString KindPackage = Root / TEXT("Kind_BoxBug");
    auto* Kind = NewObject<UP4AddedContent>(CreatePackage(*KindPackage), TEXT("Kind_BoxBug"), RF_Public | RF_Standalone);
    FAssetRegistryModule::AssetCreated(Kind);
    Kind->Kind = EP4AddedContentKind::Creature;
    Kind->ShortName = TEXT("boxbug");
    Kind->ActorClass = Child->GeneratedClass.Get();
    Kind->From = TEXT("KOCHAPPY");
    Kind->DisplayName = TEXT("Box Bug");
    Kind->Life = 300.f;
    TSharedPtr<FJsonObject> Declared;
    FString Error;
    const bool Accepted = P4AddedContentDeclaration(Kind, FPaths::GetPath(Root), Declared, Error);
    if (!Test.TestTrue(TEXT("Kadan accepts the creature declaration: ") + Error, Accepted))
        return false;
    // Everything this made: the class, its declaration, the animation Blueprint and the retargeted clips.
    TArray<UPackage*> Packages = {Child->GetOutermost(), Kind->GetOutermost()};
    for (TObjectIterator<UPackage> It; It; ++It)
        if (It->GetName().StartsWith(AnimationFolder + TEXT("/")))
            Packages.AddUnique(*It);
    // The class and its animation Blueprint ship; the retargeted clips ship only where the animation Blueprint still
    // plays them, and the declaration goes into the manifest.
    Made.Append({Child, Animation, Kind});
    if (!Test.TestTrue(TEXT("The enemy's assets save"), UEditorLoadingAndSavingUtils::SavePackages(Packages, true)))
        return false;
    FP4ExportRoot ExportRoot;
    ExportRoot.Path = KindPackage;
    Request.Roots.Add(ExportRoot);
    Names.Add(FString::Printf(TEXT("Enemy %s (parent %s, animation %s with the creature's clip in %d states, %d packages), declared as boxbug from KOCHAPPY, life 300"),
        *Child->GeneratedClass->GetPathName(), *Parent->GetPathName(), *Animation->GetPathName(), Swapped, Packages.Num()));
    return true;
}
} // namespace

bool AddPartialChecks(FAutomationTestBase& Test, UWorld* World, const FString& Root, const FVector& At,
    const FString& Parts, FP4ExportModRequest& Request, TArray<UObject*>& Made, TArray<FString>& Names,
    TArray<AActor*>& Actors)
{
    TArray<FString> Wanted;
    Parts.ParseIntoArray(Wanted, TEXT(","));
    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, *(Root / TEXT("SM_RowCube.SM_RowCube")));
    if (!Test.TestNotNull(TEXT("The partial checks reuse the material row's cube"), Cube))
        return false;
    for (const FString& Part : Wanted)
    {
        const FString Name = Part.TrimStartAndEnd();
        const bool Ok = Name == TEXT("lights")     ? Lights(Test, World, At, Names, Actors)
            : Name == TEXT("textures")             ? Textures(Test, World, Root, At, Cube, Made, Names, Actors)
            : Name == TEXT("skinned")              ? Skinned(Test, World, Root, At, Made, Names, Actors)
            : Name == TEXT("foliage")              ? Foliage(Test, World, Root, At, Cube, Made, Names, Actors)
            : Name == TEXT("enemy")                ? Enemy(Test, World, Root, Request, Made, Names)
                                                   : Test.TestTrue(TEXT("Known partial check: ") + Name, false);
        if (!Ok)
            return false;
    }
    return true;
}
} // namespace P4ExportDriverTest
#endif

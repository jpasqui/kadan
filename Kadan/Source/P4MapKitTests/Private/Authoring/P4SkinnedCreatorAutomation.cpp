#include "P4SkinnedCreator.h"
#include "P4ReceiptPanelExport.h"
#include "P4CreatorSource.h"
#include "P4ExportDriver.h"
#include "P4KadanCreatorReview.h"
#include "P4ExportReadbackContext.h"
#include "P4CreatorMaterialFactories.h"
#include "P4NativeCustomCompilerBridge.h"
#include "P4MapKitCore/skinned_native.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AnimationUtils.h"
#include "AssetExportTask.h"
#include "AssetImportTask.h"
#include "AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/SkeletalMeshActor.h"
#include "Engine/World.h"
#include "Components/SkeletalMeshComponent.h"
#include "Exporters/Exporter.h"
#include "Exporters/FbxExportOption.h"
#include "Exporters/Exporter.h"

// The FBX exporters are not exported from UnrealEd in 4.26, so they are made by class name.
static UExporter* P4NewFbxExporter(const TCHAR* ClassName)
{
    UClass* Class = FindObject<UClass>(ANY_PACKAGE, ClassName);
    return Class ? NewObject<UExporter>(GetTransientPackage(), Class) : nullptr;
}
#include "Factories/FbxFactory.h"
#include "Factories/FbxImportUI.h"
#include "HAL/FileManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "ReferenceSkeleton.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
namespace P4ExportDriverTest
{
bool Request(FAutomationTestBase&, const FString&, const FString&, FP4ExportModRequest&, FP4ModNames&);
}
namespace
{
struct FOwnSkeletonAccess : USkeleton
{
    static void Root(USkeleton* S)
    {
        S->*(&FOwnSkeletonAccess::Guid) = FGuid::NewGuid();
        (S->*(&FOwnSkeletonAccess::BoneTree)).AddDefaulted();
        FReferenceSkeletonModifier Modifier(S->*(&FOwnSkeletonAccess::ReferenceSkeleton), S);
        Modifier.Add(FMeshBoneInfo(TEXT("kosi"), TEXT("kosi"), INDEX_NONE), FTransform::Identity);
    }
};
} //namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4CreatorSkinnedContainerTest, "P4MapKit.Export.CreatorSkinnedContainer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4CreatorSkinnedContainerTest::RunTest(const FString&)
{
    FP4ExportModRequest Request;
    FP4ModNames Names;
    if (!P4ExportDriverTest::Request(
            *this, TEXT("test.creatorskinned"), TEXT("Creator skinned assets"), Request, Names))
        return false;
    const FString SourceFolder = Names.PackageRoot / TEXT("Source"), OutputFolder = Names.PackageRoot / TEXT("Rebaked");
    TArray<UObject*> Owned;
    ON_SCOPE_EXIT
    {
        TSet<UPackage*> Packages;
        for (UObject* Object : Owned)
            if (IsValid(Object))
            {
                FAssetRegistryModule::AssetDeleted(Object);
                Packages.Add(Object->GetOutermost());
                Object->ClearFlags(RF_Public | RF_Standalone);
                Object->MarkPendingKill();
            }
        for (UPackage* Package : Packages)
        {
            if (Package->GetName().StartsWith(Names.PackageRoot + TEXT("/")))
                for (const TCHAR* Ext : {TEXT(".uasset"), TEXT(".uexp"), TEXT(".ubulk")})
                    IFileManager::Get().Delete(
                        *FPackageName::LongPackageNameToFilename(Package->GetName(), Ext), false, true);
            Package->SetDirtyFlag(false);
            Package->MarkPendingKill();
        }
    };
    USkeleton* Skeleton = NewObject<USkeleton>(
        CreatePackage(TEXT("/Temp/P4OwnSkinnedSkeleton")), TEXT("P4OwnSkinnedSkeleton"), RF_Public | RF_Standalone);
    FOwnSkeletonAccess::Root(Skeleton);
    Owned.Add(Skeleton);
    USkeletalMesh* Mesh = NewObject<USkeletalMesh>(
        CreatePackage(TEXT("/Temp/P4OwnSkinnedMesh")), TEXT("Mesh"), RF_Public | RF_Standalone);
    Owned.Add(Mesh);
    Mesh->Skeleton = Skeleton;
    {
        FReferenceSkeletonModifier Modifier(Mesh->RefSkeleton, Skeleton);
        Modifier.Add(FMeshBoneInfo(TEXT("kosi"), TEXT("kosi"), INDEX_NONE), FTransform::Identity);
    }
    Mesh->AddLODInfo();
    FSkeletalMeshLODModel* Model = new FSkeletalMeshLODModel();
    Mesh->GetImportedModel()->LODModels.Add(Model);
    Model->NumVertices = 3;
    Model->NumTexCoords = 1;
    Model->IndexBuffer = {0, 1, 2};
    Model->RequiredBones = {0};
    Model->ActiveBoneIndices = {0};
    Model->MaxImportVertex = 2;
    FSkelMeshSection& Section = Model->Sections.AddDefaulted_GetRef();
    Section.NumVertices = 3;
    Section.NumTriangles = 1;
    Section.MaxBoneInfluences = 1;
    Section.BoneMap = {0};
    Section.bCastShadow = true;
    Section.SoftVertices.SetNumZeroed(3);
    const FVector Points[] = {FVector(0, 0, 0), FVector(100, 0, 0), FVector(0, 100, 0)};
    for (int32 I = 0; I < 3; ++I)
    {
        FSoftSkinVertex& V = Section.SoftVertices[I];
        V.Position = Points[I];
        V.TangentX = FVector(1, 0, 0);
        V.TangentY = FVector(0, 1, 0);
        V.TangentZ = FVector4(0, 0, 1, 1);
        V.Color = FColor::White;
        V.InfluenceWeights[0] = 255;
        V.UVs[0] = FVector2D(I == 1 ? 1.f : 0.f, I == 2 ? 1.f : 0.f);
    }
    Model->SyncronizeUserSectionsDataArray();
    Mesh->Materials.Add(
        FSkeletalMaterial(UMaterial::GetDefaultMaterial(MD_Surface), true, false, TEXT("Surface"), TEXT("Surface")));
    Mesh->SetImportedBounds(FBoxSphereBounds(FVector(50, 50, 0), FVector(50, 50, 1), 71.f));
    Mesh->InvalidateDeriveDataCacheGUID();
    Mesh->CalculateInvRefMatrices();
    Mesh->PostEditChange();
    UAnimSequence* Clip = NewObject<UAnimSequence>(
        CreatePackage(TEXT("/Temp/P4OwnSkinnedClip")), TEXT("Clip"), RF_Public | RF_Standalone);
    Owned.Add(Clip);
    Clip->SetSkeleton(Skeleton);
    Clip->SequenceLength = 2.f / 30.f;
    Clip->SetRawNumberOfFrame(3);
    Clip->BoneCompressionSettings = FAnimationUtils::GetDefaultAnimationBoneCompressionSettings();
    Clip->CurveCompressionSettings = FAnimationUtils::GetDefaultAnimationCurveCompressionSettings();
    FRawAnimSequenceTrack Track;
    Track.PosKeys = {FVector::ZeroVector, FVector(10, 0, 0), FVector(20, 0, 0)};
    Track.RotKeys = {FQuat::Identity};
    Track.ScaleKeys = {FVector::OneVector};
    Clip->AddNewRawTrack(TEXT("kosi"), &Track);
    Clip->MarkRawDataAsModified(false);
    Clip->RequestSyncAnimRecompression(false);
    Clip->SetPreviewMesh(Mesh, false);
    TestFalse(TEXT("Procedural objects are not declared creator factory outputs"),
        P4HasCreatorSource(Mesh) || P4HasCreatorSource(Clip));
    const FString Scratch = FPaths::ProjectSavedDir() / TEXT("P4MapKit/CreatorSkinnedTest");
    IFileManager::Get().MakeDirectory(*Scratch, true);
    UFbxExportOption* Options = NewObject<UFbxExportOption>();
    Options->Collision = false;
    Options->LevelOfDetail = false;
    Options->VertexColor = true;
    Options->bExportPreviewMesh = false;
    Options->bExportMorphTargets = true;
    for (UObject* Object : TArray<UObject*>{Mesh, Clip})
    {
        UAssetExportTask* Task = NewObject<UAssetExportTask>();
        Task->Object = Object;
        Task->Options = Options;
        Task->Exporter = Object == Mesh ? P4NewFbxExporter(TEXT("SkeletalMeshExporterFBX"))
                                        : P4NewFbxExporter(TEXT("AnimSequenceExporterFBX"));
        Task->Filename = Scratch / (Object->GetName() + TEXT(".fbx"));
        Task->bAutomated = true;
        Task->bPrompt = false;
        Task->bReplaceIdentical = true;
        if (!TestTrue(TEXT("Native exporter writes own procedural FBX"), UExporter::RunAssetExportTask(Task)))
            return false;
    }
    auto Import = [&](const FString& Name, USkeleton* Target) -> UObject*
    {
        UFbxFactory* Factory = NewObject<UFbxFactory>();
        Factory->SetDetectImportTypeOnImport(false);
        P4ConfigureSkinnedFbx(Factory->ImportUI, Target, 1.f, 0.f, 30);
        UAssetImportTask* Task = NewObject<UAssetImportTask>();
        Task->Factory = Factory;
        Task->Options = Factory->ImportUI;
        Task->Filename = Scratch / (Name + TEXT(".fbx"));
        Task->DestinationPath = SourceFolder;
        Task->DestinationName = Name;
        Task->bAutomated = true;
        Task->bSave = false;
        FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().ImportAssetTasks({Task});
        for (UObject* Result : Task->Result)
            Owned.AddUnique(Result);
        if (Task->Result.Num())
            return Task->Result[0];
        // The engine can build the asset and leave Result empty: the object the import named.
        UObject* Named = FindObject<UObject>(nullptr, *(SourceFolder / Name + TEXT(".") + Name));
        if (Named)
            Owned.AddUnique(Named);
        return Named;
    };
    USkeletalMesh* ImportedMesh = Cast<USkeletalMesh>(Import(TEXT("Mesh"), nullptr));
    if (!TestNotNull(TEXT("Actual factory imports own skeletal FBX"), ImportedMesh))
        return false;
    Owned.AddUnique(ImportedMesh->Skeleton);
    if (!TestTrue(TEXT("Completed factory gives mesh and newly produced skeleton creator source"),
            P4HasCreatorSource(ImportedMesh) && P4HasCreatorSource(ImportedMesh->Skeleton)))
        return false;
    UAnimSequence* ImportedClip = Cast<UAnimSequence>(Import(TEXT("Clip"), ImportedMesh->Skeleton));
    if (!TestNotNull(TEXT("Actual factory imports own animation FBX"), ImportedClip))
        return false;
    TestTrue(TEXT("Completed animation factory records actual raw source"), P4HasCreatorSource(ImportedClip));
    const FString BlockedFolder = Names.PackageRoot / TEXT("Blocked"),
                  BlockedName = BlockedFolder / TEXT("Mesh_Skeleton");
    UPackage* BlockedPackage = CreatePackage(*BlockedName);
    USkeleton* Blocked = NewObject<USkeleton>(BlockedPackage, TEXT("Mesh_Skeleton"), RF_Public | RF_Standalone);
    FOwnSkeletonAccess::Root(Blocked);
    Owned.Add(Blocked);
    const FGuid BlockedGuid = Blocked->GetGuid();
    const bool HadCreator = P4HasCreatorSource(Blocked), HadGame = P4HasIntrinsicGameSource(Blocked);
    const FString BlockedReply = UP4SkinnedCreatorTools::RebakeSkeletalAssets(
        ImportedMesh, {ImportedClip}, BlockedFolder, 2.f, -90.f);
    TestTrue(TEXT("Loaded unregistered generated-skeleton identity blocks rebake before factory replacement"),
        BlockedReply.StartsWith(TEXT("Choose an empty destination")));
    TestTrue(TEXT("Blocked live skeleton retains its GUID and source role"),
        Blocked->GetGuid() == BlockedGuid && P4HasCreatorSource(Blocked) == HadCreator &&
            P4HasIntrinsicGameSource(Blocked) == HadGame);
    const FString BlockedFile = FPackageName::LongPackageNameToFilename(BlockedName, TEXT(".uasset"));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(BlockedFile), true);
    if (!TestTrue(TEXT("Pre-existing skeleton fixture saves its actual editor bytes"),
            UPackage::SavePackage(BlockedPackage, Blocked, RF_Public | RF_Standalone, *BlockedFile, GError)))
        return false;
    TArray<uint8> BeforeBlocked, AfterBlocked;
    if (!TestTrue(TEXT("Pre-existing saved skeleton bytes read back"),
            FFileHelper::LoadFileToArray(BeforeBlocked, *BlockedFile)))
        return false;
    const FString SavedReply = UP4SkinnedCreatorTools::RebakeSkeletalAssets(
        ImportedMesh, {ImportedClip}, BlockedFolder, 2.f, -90.f);
    TestTrue(TEXT("Saved generated-skeleton identity blocks rebake"),
        SavedReply.StartsWith(TEXT("Choose an empty destination")));
    TestTrue(TEXT("Refused rebake preserves every existing skeleton byte"),
        FFileHelper::LoadFileToArray(AfterBlocked, *BlockedFile) && BeforeBlocked == AfterBlocked);
    TestTrue(TEXT("Refused saved skeleton keeps its GUID and source provenance"),
        Blocked->GetGuid() == BlockedGuid && P4HasCreatorSource(Blocked) == HadCreator &&
            P4HasIntrinsicGameSource(Blocked) == HadGame);
    {
        USkeleton* CreatorSkeleton = ImportedMesh->Skeleton;
        ON_SCOPE_EXIT
        {
            ImportedMesh->Skeleton = CreatorSkeleton;
        };
        ImportedMesh->Skeleton = Blocked;
        const FString Refused = UP4SkinnedCreatorTools::RebakeSkeletalAssets(
            ImportedMesh, {}, Names.PackageRoot / TEXT("ReferenceOnly"), 2.f, -90.f);
        TestTrue(TEXT("An unproven referenced skeleton cannot be rebaked into creator content"),
            Refused.StartsWith(TEXT("Rebake a mesh with its creator skeleton")));
        TestTrue(TEXT("Referenced skeleton stays unchanged and gains no creator source"),
            Blocked->GetGuid() == BlockedGuid && P4HasCreatorSource(Blocked) == HadCreator);
    }
    const FString Result = UP4SkinnedCreatorTools::RebakeSkeletalAssets(
        ImportedMesh, {ImportedClip}, OutputFolder, 2.f, -90.f);
    if (!TestTrue(TEXT("Working Groink FBX round trip ports scale and facing"),
            Result.StartsWith(TEXT("Created the rebaked"))))
    {
        AddError(Result);
        return false;
    }
    auto Load = [&](const FString& Name)
    { return LoadObject<UObject>(nullptr, *(OutputFolder / Name + TEXT(".") + Name)); };
    USkeletalMesh* RebakedMesh = Cast<USkeletalMesh>(Load(TEXT("Mesh")));
    UAnimSequence* RebakedClip = Cast<UAnimSequence>(Load(TEXT("Clip")));
    if (!TestNotNull(TEXT("Rebaked mesh exists"), RebakedMesh) ||
        !TestNotNull(TEXT("Rebaked clip exists"), RebakedClip))
        return false;
    Owned.Add(RebakedMesh);
    Owned.Add(RebakedMesh->Skeleton);
    Owned.Add(RebakedClip);
    const FTransform& Root = RebakedMesh->RefSkeleton.GetRefBonePose()[0];
    TestTrue(TEXT("Uniform scale is baked on root bone"), Root.GetScale3D().Equals(FVector(2), 0.0001f));
    TestTrue(
        TEXT("Facing is baked on root bone"), Root.GetRotation().Equals(FRotator(0, -90, 0).Quaternion(), 0.0001f));
    TestEqual(TEXT("Rebake keeps clip frame count"), RebakedClip->GetRawNumberOfFrames(),
        ImportedClip->GetRawNumberOfFrames());
    for (UObject* Object : TArray<UObject*>{RebakedMesh, RebakedMesh->Skeleton, RebakedClip})
        TestTrue(TEXT("Existing panel routes native creator skinned content"),
            P4ExportAssetAvailability(Object, Request.Mod).Route == EP4ExportAssetRoute::CreatorSkinned);
    UMaterial* SkinGraph = nullptr;
    if (P4UseSwitchShaderRoute())
    {
        // Through UE's shaders a material of the creator's own draws on a skeletal mesh too: the export compiles its map
        // for the skin factories its consumers use (P4ApplyMaterialFactoryUsage). Two-sided, so the triangle shows both ways.
        UP4CreatorMaterialFactory* Factory = NewObject<UP4CreatorMaterialFactory>();
        SkinGraph = Cast<UMaterial>(FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"))
                .Get()
                .CreateAsset(TEXT("M_Skin"), OutputFolder, UMaterial::StaticClass(), Factory));
        if (!TestNotNull(TEXT("Creator material for the skeletal mesh"), SkinGraph))
            return false;
        Owned.Add(SkinGraph);
        UMaterialExpressionVectorParameter* Tint = NewObject<UMaterialExpressionVectorParameter>(SkinGraph);
        Tint->ParameterName = TEXT("Tint");
        Tint->DefaultValue = FLinearColor(0.125f, 1.f, 0.0625f, 1.f);
        SkinGraph->Expressions.Add(Tint);
        SkinGraph->BaseColor.Expression = Tint;
        SkinGraph->TwoSided = true;
        SkinGraph->PostEditChange();
        RebakedMesh->Materials[0].MaterialInterface = SkinGraph;
        RebakedMesh->PostEditChange();
    }
    FP4ExportSelectionPreview Selection;
    for (UObject* Object : TArray<UObject*>{RebakedMesh, RebakedMesh->Skeleton, RebakedClip})
    {
        FP4ExportSelectionRow Row;
        Row.Package = Object->GetOutermost()->GetName();
        Row.Disposition = EP4ExportDisposition::CreatorPackage;
        Selection.Rows.Add(Row);
    }
    if (!TestTrue(TEXT("Actual creator review allows skeletal mesh, skeleton and clip selection"),
            P4ReviewCreatorSelection(Request.Mod, Selection, nullptr).CanExport()))
        return false;
    for (UObject* Object : TArray<UObject*>{RebakedMesh, RebakedClip})
    {
        FP4ExportRoot ExportRoot;
        ExportRoot.Path = Object->GetOutermost()->GetName();
        Request.Roots.Add(ExportRoot);
    }
    UWorld* PlaybackWorld = nullptr;
    ON_SCOPE_EXIT
    {
        if (PlaybackWorld)
            PlaybackWorld->DestroyWorld(false);
    };
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_KADAN_RECEIPT_KIND")) == TEXT("animation"))
    {
        const FString LevelPackage = Names.PackageRoot / TEXT("Maps/Playback");
        PlaybackWorld = UWorld::CreateWorld(EWorldType::Editor, false, TEXT("Playback"), CreatePackage(*LevelPackage));
        if (!TestNotNull(TEXT("Creator playback level exists"), PlaybackWorld))
            return false;
        PlaybackWorld->SetFlags(RF_Public | RF_Standalone);
        FAssetRegistryModule::AssetCreated(PlaybackWorld);
        Owned.Add(PlaybackWorld);
        FActorSpawnParameters Spawn;
        Spawn.Name = TEXT("KadanAnimation");
        ASkeletalMeshActor* Dancer = PlaybackWorld->SpawnActor<ASkeletalMeshActor>(
            ASkeletalMeshActor::StaticClass(), FVector(0, 0, 200), FRotator::ZeroRotator, Spawn);
        if (!TestNotNull(TEXT("Creator skeletal actor exists"), Dancer))
            return false;
        USkeletalMeshComponent* Component = Dancer->GetSkeletalMeshComponent();
        Component->SetSkeletalMesh(RebakedMesh);
        Component->SetAnimationMode(EAnimationMode::AnimationSingleNode);
        Component->PlayAnimation(RebakedClip, true);
        Component->SetPlayRate(.5f);
        Component->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        Component->AnimationData.AnimToPlay = RebakedClip;
        Component->AnimationData.bSavedLooping = true;
        Component->AnimationData.bSavedPlaying = true;
        Component->AnimationData.SavedPlayRate = .5f;
        FP4ExportRoot PlaybackRoot;
        PlaybackRoot.Path = LevelPackage;
        Request.Roots.Add(PlaybackRoot);
        Request.MapPackage = TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave000/Cave000_F00/Cave000_F00_P");
    }
    FP4ExportModResult Exported;
    FString Error;
    if (!TestTrue(TEXT("One existing exporter writes creator mesh/skeleton/clip container"),
            P4ExportMod(Request, Exported, Error)))
    {
        AddError(Error);
        return false;
    }
    TestTrue(TEXT("Skeleton dependency ships with creator mesh and clip"),
        Exported.Packages.Contains(RebakedMesh->Skeleton->GetOutermost()->GetName()));
    if (SkinGraph)
        TestTrue(TEXT("The skeletal mesh's material of the creator's own ships as a runtime material"),
            !Exported.RuntimePaths.FindRef(SkinGraph->GetPathName()).IsEmpty());
    const FString Folder = Exported.Directory / TEXT("romfs/tsuchi/mods") / Request.Mod.Id,
                  Readback = Scratch / TEXT("readback");
    if (!TestTrue(TEXT("Actual emitted container extracts"),
            P4ExtractExportReadback(Request.Tools, Folder / (Names.Container + TEXT("_P.utoc")),
                Request.GamePaksDirectory, Request.WorkDirectory, Readback, TEXT(""), Error)))
    {
        AddError(Error);
        return false;
    }
    for (UObject* Object : TArray<UObject*>{RebakedMesh, RebakedMesh->Skeleton, RebakedClip})
    {
        const FString Base = Readback / TEXT("Carrot4/Content") / Object->GetOutermost()->GetName().Mid(6);
        TArray<uint8> Header, Body;
        if (!TestTrue(TEXT("Container contains actual split cooked skinned bytes"),
                FFileHelper::LoadFileToArray(Header, *(Base + TEXT(".uasset"))) &&
                    FFileHelper::LoadFileToArray(Body, *(Base + TEXT(".uexp")))))
            return false;
        const std::vector<uint8> H(Header.GetData(), Header.GetData() + Header.Num()),
            B(Body.GetData(), Body.GetData() + Body.Num());
        const auto Package = P4MapKit::ReadPackage(H, B);
        if (!TestTrue(
                TEXT("Native container package parses"), Package.Error.Code.empty() && Package.Exports.size() >= 1))
            return false;
        const auto& Asset = Package.Exports[0];
        const auto Properties = P4MapKit::ReadPropertyEnvelopes(
            B, Package.Names, Asset.Offset, Asset.Offset + Asset.Size);
        P4MapKit::FReadError Failure;
        if (Object == RebakedMesh->Skeleton)
        {
            P4MapKit::FSkeletonNative Native;
            if (!TestTrue(TEXT("Physical emitted skeleton stream parses fully"),
                    Properties.Error.Code.empty() &&
                        P4MapKit::ReadSkeletonNative(
                            B, Properties.End, Asset.Offset + Asset.Size, Package.Names, Native, Failure)))
            {
                if (!Failure.Code.empty())
                    AddError(UTF8_TO_TCHAR(Failure.Code.c_str()));
                return false;
            }
            TestTrue(TEXT("Native emitted root carries the rebaked scale"),
                Native.Bones.size() == 1 && FMath::Abs(Native.Bones[0].Pose[7] - 2.f) < 0.0001f);
        }
        if (Object == RebakedClip)
        {
            P4MapKit::FAnimSequenceNative Animation;
            TestTrue(TEXT("Physical emitted animation stream parses fully"),
                Properties.Error.Code.empty() &&
                    P4MapKit::ReadAnimSequenceNative(
                        B, Properties.End, Asset.Offset + Asset.Size, Package.Names, Animation, Failure));
            TestEqual(TEXT("Emitted animation contains authored frames"), Animation.Frames,
                RebakedClip->GetRawNumberOfFrames());
            TestTrue(TEXT("Emitted clip skeleton GUID matches native creator skeleton"),
                Animation.SkeletonGuid ==
                    std::array<uint32, 4>{RebakedMesh->Skeleton->GetGuid().A, RebakedMesh->Skeleton->GetGuid().B,
                        RebakedMesh->Skeleton->GetGuid().C, RebakedMesh->Skeleton->GetGuid().D});
        }
        if (!Failure.Code.empty())
            AddError(UTF8_TO_TCHAR(Failure.Code.c_str()));
    }
    if (!P4ReceiptPanelExport(
            *this, TEXT("animation"), Request, RebakedClip->GetPathName(), RebakedMesh->GetPathName()))
        return false;
    return !HasAnyErrors();
}
#endif

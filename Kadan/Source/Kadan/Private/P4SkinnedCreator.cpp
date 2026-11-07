#include "P4SkinnedCreator.h"
#include "P4GamePaths.h"
#include "P4CreatorSource.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AssetImportTask.h"
#include "AssetExportTask.h"
#include "AssetToolsModule.h"
#include "AssetRegistryModule.h"
#include "Engine/SkeletalMesh.h"
#include "Exporters/Exporter.h"
#include "Exporters/FbxExportOption.h"
#include "Exporters/Exporter.h"

// The FBX exporters are not exported from UnrealEd in 4.26, so they are made by class name.
static UExporter* P4NewFbxExporter(const TCHAR* ClassName)
{
    UClass* Class = FindObject<UClass>(ANY_PACKAGE, ClassName);
    return Class ? NewObject<UExporter>(GetTransientPackage(), Class) : nullptr;
}
#include "Factories/FbxImportUI.h"
#include "Factories/FbxFactory.h"
#include "Factories/FbxSkeletalMeshImportData.h"
#include "Factories/FbxAnimSequenceImportData.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

void P4ConfigureSkinnedFbx(UFbxImportUI* UI, USkeleton* Skeleton, float Scale, float Yaw, int32 SampleRate)
{
    UI->bImportMesh = Skeleton == nullptr;
    UI->bImportAsSkeletal = true;
    UI->MeshTypeToImport = Skeleton ? FBXIT_Animation : FBXIT_SkeletalMesh;
    UI->bAutomatedImportShouldDetectType = false;
    UI->bOverrideFullName = true;
    UI->bImportAnimations = Skeleton != nullptr;
    UI->bImportMaterials = false;
    UI->bImportTextures = false;
    UI->bCreatePhysicsAsset = false;
    UI->Skeleton = Skeleton;
    UFbxSkeletalMeshImportData* M = UI->SkeletalMeshImportData;
    M->ImportContentType = FBXICT_All;
    M->ImportUniformScale = Scale;
    M->ImportRotation = FRotator(0.f, Yaw, 0.f);
    M->NormalImportMethod = FBXNIM_ImportNormalsAndTangents;
    M->bImportMorphTargets = true;
    M->bUseT0AsRefPose = false;
    M->bPreserveSmoothingGroups = true;
    M->bImportMeshesInBoneHierarchy = true;
    UFbxAnimSequenceImportData* A = UI->AnimSequenceImportData;
    A->ImportUniformScale = Scale;
    A->ImportRotation = FRotator(0.f, Yaw, 0.f);
    A->AnimationLength = FBXALIT_ExportedTime;
    A->bImportBoneTracks = true;
    A->bRemoveRedundantKeys = false;
    A->bUseDefaultSampleRate = false;
    A->CustomSampleRate = SampleRate;
    A->bImportCustomAttribute = false;
}

FString UP4SkinnedCreatorTools::RebakeSkeletalAssets(
    USkeletalMesh* Mesh, const TArray<UAnimSequence*>& Clips, const FString& DestinationFolder, float Scale, float Yaw)
{
    if (!IsInGameThread() || !IsValid(Mesh) || !Mesh->Skeleton || !P4HasCreatorSource(Mesh))
        return TEXT("Import your skeletal mesh from its own FBX before rebaking it.");
    if (!P4HasCreatorSource(Mesh->Skeleton))
        return TEXT(
            "Rebake a mesh with its creator skeleton. A game skeleton remains a game reference when exporting your mesh.");
    if (!FMath::IsFinite(Scale) || Scale <= 0.f || !FMath::IsFinite(Yaw))
        return TEXT("Use a positive finite scale and a finite facing angle.");
    if (!P4IsValidContentPackageIgnoringCase(DestinationFolder))
        return TEXT("Choose an editor content folder for the new assets.");
    TArray<FAssetData> ExistingAssets;
    FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"))
        .Get()
        .GetAssetsByPath(FName(*DestinationFolder), ExistingAssets, true);
    TArray<FString> ExistingFiles;
    IFileManager::Get().FindFilesRecursive(
        ExistingFiles, *FPackageName::LongPackageNameToFilename(DestinationFolder), TEXT("*"), true, false);
    if (ExistingAssets.Num() || ExistingFiles.Num())
        return TEXT("Choose an empty destination folder for the mesh, generated skeleton and clips.");
    for (TObjectIterator<UPackage> It; It; ++It)
        if (!It->IsPendingKill() &&
            (It->GetName() == DestinationFolder || It->GetName().StartsWith(DestinationFolder + TEXT("/"))))
            return TEXT("Choose an empty destination folder: an editor package is already loaded there.");
    TSet<FName> Names;
    Names.Add(Mesh->GetFName());
    // A collision guard for the native factory's auxiliary output, never source evidence.
    Names.Add(FName(*(Mesh->GetName() + TEXT("_Skeleton"))));
    for (UAnimSequence* Clip : Clips)
    {
        if (!IsValid(Clip) || Clip->GetSkeleton() != Mesh->Skeleton || !P4HasCreatorSource(Clip))
            return TEXT("Use creator animation clips belonging to this mesh's skeleton.");
        if (Names.Contains(Clip->GetFName()))
            return TEXT("Use a distinct name for every mesh and animation clip.");
        Names.Add(Clip->GetFName());
    }
    for (const FName& Name : Names)
    {
        const FString Package = DestinationFolder / Name.ToString();
        if (FindPackage(nullptr, *Package) || FPackageName::DoesPackageExist(Package))
            return TEXT("Choose an empty destination: ") + Package + TEXT(" already exists.");
    }
    const FString Scratch = FPaths::ProjectSavedDir() / TEXT("P4MapKit/SkinnedRebake");
    if (!IFileManager::Get().MakeDirectory(*Scratch, true))
        return TEXT("Could not prepare the FBX working folder.");
    UFbxExportOption* Options = NewObject<UFbxExportOption>();
    Options->Collision = false;
    Options->LevelOfDetail = false;
    Options->VertexColor = true;
    Options->bExportPreviewMesh = false;
    Options->bExportMorphTargets = true;
    auto Export = [&](UObject* Asset, UExporter* Exporter)
    {
        UAssetExportTask* Task = NewObject<UAssetExportTask>();
        Task->Object = Asset;
        Task->Filename = Scratch / (Asset->GetName() + TEXT(".fbx"));
        Task->Exporter = Exporter;
        Task->Options = Options;
        Task->bAutomated = true;
        Task->bPrompt = false;
        Task->bReplaceIdentical = true;
        if (!IFileManager::Get().Delete(*Task->Filename, false, true))
            return false;
        return UExporter::RunAssetExportTask(Task) && IFileManager::Get().FileSize(*Task->Filename) > 0;
    };
    if (!Export(Mesh, P4NewFbxExporter(TEXT("SkeletalMeshExporterFBX"))))
        return TEXT("The native FBX mesh export failed.");
    for (UAnimSequence* Clip : Clips)
    {
        USkeletalMesh* Preview = Clip->GetPreviewMesh(false);
        const bool Dirty = Clip->GetOutermost()->IsDirty();
        ON_SCOPE_EXIT
        {
            Clip->SetPreviewMesh(Preview, false);
            Clip->GetOutermost()->SetDirtyFlag(Dirty);
        };
        Clip->SetPreviewMesh(Mesh, false);
        if (!Export(Clip, P4NewFbxExporter(TEXT("AnimSequenceExporterFBX"))))
            return TEXT("The native FBX animation export failed: ") + Clip->GetName();
    }
    auto Import = [&](const FString& Name, USkeleton* Skeleton, int32 Rate, UAnimSequence* RetrySource) -> UObject*
    {
        const FString PackageName = DestinationFolder / Name;
        const FString ObjectPath = PackageName + TEXT(".") + Name;
        UObject* Existing = FindObject<UObject>(nullptr, *ObjectPath);
        // Only the current operation's own first animation output can be retried at 30fps.
        // First imports cannot replace any package; the empty destination check covers all
        // auxiliary factory outputs as well as the explicit mesh/clip identities.
        if (RetrySource)
        {
            const UAnimSequence* Previous = Cast<UAnimSequence>(Existing);
            if (Previous != RetrySource || Previous->GetSkeleton() != Skeleton || !P4HasCreatorSource(Previous) ||
                P4HasIntrinsicGameSource(Previous))
                return nullptr;
        }
        else if (Existing || FindPackage(nullptr, *PackageName) || FPackageName::DoesPackageExist(PackageName))
            return nullptr;
        UAssetImportTask* Task = NewObject<UAssetImportTask>();
        Task->Filename = Scratch / (Name + TEXT(".fbx"));
        Task->DestinationPath = DestinationFolder;
        Task->DestinationName = Name;
        Task->bAutomated = true;
        Task->bReplaceExisting = RetrySource != nullptr;
        Task->bReplaceExistingSettings = RetrySource != nullptr;
        Task->bSave = false;
        UFbxFactory* Factory = NewObject<UFbxFactory>();
        Factory->SetDetectImportTypeOnImport(false);
        P4ConfigureSkinnedFbx(Factory->ImportUI, Skeleton, Scale, Yaw, Rate);
        Task->Options = Factory->ImportUI;
        Task->Factory = Factory;
        FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().ImportAssetTasks({Task});
        // UE 4.26's batch import fills Result only for script factories; the object it imported is the one it named.
        return Task->Result.Num()
            ? Task->Result[0]
            : (Task->ImportedObjectPaths.Num() ? FindObject<UObject>(nullptr, *ObjectPath) : nullptr);
    };
    USkeletalMesh* Made = Cast<USkeletalMesh>(Import(Mesh->GetName(), nullptr, 0, nullptr));
    if (!Made || !Made->Skeleton || !P4HasCreatorSource(Made) || !P4HasCreatorSource(Made->Skeleton))
        return TEXT("The native factory did not create a mesh and a new skeleton from the FBX.");
    if (Made->Materials.Num() != Mesh->Materials.Num())
        return TEXT("The FBX round trip changed the material slot count.");
    for (int32 Slot = 0; Slot < Made->Materials.Num(); ++Slot)
    {
        const FSkeletalMaterial& Wanted = Mesh->Materials[Slot];
        FSkeletalMaterial& Got = Made->Materials[Slot];
        const FName MaterialName = Wanted.MaterialInterface ? Wanted.MaterialInterface->GetFName() : NAME_None;
        if (Got.ImportedMaterialSlotName != Wanted.ImportedMaterialSlotName &&
            Got.ImportedMaterialSlotName != Wanted.MaterialSlotName && Got.ImportedMaterialSlotName != MaterialName)
            return TEXT("The FBX round trip changed the material slot order.");
        Got.MaterialInterface = Wanted.MaterialInterface;
        Got.MaterialSlotName = Wanted.MaterialSlotName;
    }
    Made->PhysicsAsset = nullptr;
    TArray<UPackage*> Packages{Made->GetOutermost(), Made->Skeleton->GetOutermost()};
    for (UAnimSequence* Clip : Clips)
    {
        UAnimSequence* NewClip = Cast<UAnimSequence>(Import(Clip->GetName(), Made->Skeleton, 0, nullptr));
        if (NewClip && NewClip->GetRawNumberOfFrames() != Clip->GetRawNumberOfFrames())
            NewClip = Cast<UAnimSequence>(Import(Clip->GetName(), Made->Skeleton, 30, NewClip));
        if (!NewClip || !P4HasCreatorSource(NewClip))
            return TEXT("The native FBX animation import failed: ") + Clip->GetName();
        if (NewClip->GetRawNumberOfFrames() != Clip->GetRawNumberOfFrames() ||
            FMath::Abs(NewClip->SequenceLength - Clip->SequenceLength) > 0.0001f ||
            NewClip->GetAnimationTrackNames() != Clip->GetAnimationTrackNames())
            return TEXT("The FBX round trip changed animation timing or tracks: ") + Clip->GetName();
        Packages.AddUnique(NewClip->GetOutermost());
    }
    // Saved one by one, so the log names the asset that could not be saved and why (LogSavePackage).
    for (UPackage* Package : Packages)
    {
        Package->MarkPackageDirty();
        UObject* Asset = Package->FindAssetInPackage();
        const FString File = FPackageName::LongPackageNameToFilename(
            Package->GetName(), FPackageName::GetAssetPackageExtension());
        if (!Asset ||
            !UPackage::SavePackage(
                Package, Asset, RF_Public | RF_Standalone, *File, GError, nullptr, false, true, SAVE_NoError))
            return TEXT("The new skeletal asset ") + Package->GetName() + TEXT(" could not be saved.");
    }
    return TEXT("Created the rebaked mesh, skeleton and animation clips in ") + DestinationFolder +
        TEXT(". Export them with the mod.");
}

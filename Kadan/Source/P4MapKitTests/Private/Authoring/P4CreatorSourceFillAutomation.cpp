#include "P4CreatorSource.h"
#include "P4OriginalAssetImport.h"
#include "P4OriginalImportSave.h"
#include "P4OriginalRecord.h"
#include "P4KadanReadOnly.h"
#include "AssetRegistryModule.h"
#include "Animation/Skeleton.h"
#include "Animation/SmartName.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFilemanager.h"
#include "Interfaces/Interface_AssetUserData.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "PackageTools.h"
#include "ReferenceSkeleton.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
struct FCreatorFillSkeleton : USkeleton
{
    static void Identity(USkeleton* Skeleton) { Skeleton->*(&FCreatorFillSkeleton::Guid) = FGuid::NewGuid(); }
    static void Source(USkeleton* Skeleton)
    {
        Identity(Skeleton);
        (Skeleton->*(&FCreatorFillSkeleton::BoneTree)).AddDefaulted();
        FReferenceSkeletonModifier Modifier(Skeleton->*(&FCreatorFillSkeleton::ReferenceSkeleton), Skeleton);
        Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform(FVector(5, 0, 0)));
        // UE4.26 Skeleton.cpp:113 creates the empty AnimationCurves mapping, but
        // SmartName.cpp:299 cooks its loaded copy. Match Skeleton.cpp:165 before
        // the first fixture cook; the public container PostLoad copies it at 312.
        (Skeleton->*(&FCreatorFillSkeleton::SmartNames)).PostLoad();
    }
};
TArray<uint8> FillRoleBytes(UObject* Object)
{
    TArray<uint8> Out;
    FObjectWriter Writer(Object, Out, false, false, false);
    return Out;
}
} //namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4CreatorSourceFill, "P4MapKit.Kadan.CreatorSource.VerifiedFill",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4CreatorSourceFill::RunTest(const FString&)
{
    if (!GEditor)
    {
        AddError(TEXT("Verified native fill requires the editor"));
        return false;
    }
    const FString PackageName = TEXT("/Game/__KadanCreatorSourceFillTest/Skeleton");
    const FString Path = PackageName + TEXT(".Skeleton");
    const FString Destination = FPaths::ConvertRelativePathToFull(
        FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension()));
    if (FindPackage(nullptr, *PackageName) || FPackageName::DoesPackageExist(PackageName))
    {
        AddError(TEXT("Verified fill fixture destination is occupied"));
        return false;
    }
    const FString Folder = FPaths::ConvertRelativePathToFull(
        FPaths::ProjectSavedDir() / TEXT("KadanCreatorSourceFillTest"));
    IFileManager::Get().MakeDirectory(*Folder, true);
    const FString Input = Folder / TEXT("Skeleton.uasset");
    ON_SCOPE_EXIT
    {
        if (UPackage* Package = FindPackage(nullptr, *PackageName))
        {
            Package->SetDirtyFlag(false);
            UPackageTools::UnloadPackages({Package});
        }
        for (const FString& Base : {Destination, Input})
            for (const FString& Suffix :
                {FString(TEXT("uasset")), FString(TEXT("uexp")), FString(TEXT("ubulk")), FString(TEXT("uptnl"))})
            {
                const FString Owned = FPaths::ChangeExtension(Base, Suffix);
                FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*Owned, false);
                if (IFileManager::Get().FileExists(*Owned))
                    IFileManager::Get().Delete(*Owned, false, true);
            }
    };
    ITargetPlatform* Target = GetTargetPlatformManagerRef().FindTargetPlatform(TEXT("WindowsNoEditor"));
    if (!TestNotNull(TEXT("Native source cook platform exists"), Target))
        return false;
    UPackage* SourcePackage = CreatePackage(*PackageName);
    SourcePackage->SetFolderName(FName(*PackageName));
    USkeleton* Source = NewObject<USkeleton>(SourcePackage, TEXT("Skeleton"), RF_Public | RF_Standalone);
    FCreatorFillSkeleton::Source(Source);
    const FGuid ExpectedGuid = Source->GetGuid();
    FAssetRegistryModule::AssetCreated(Source);
    const uint32 Flags = SourcePackage->GetPackageFlags();
    if (!Target->HasEditorOnlyData())
        SourcePackage->SetPackageFlags(PKG_FilterEditorOnly);
    const bool Cooked = GEditor->SavePackage(SourcePackage, Source, RF_Public | RF_Standalone, *Input, GError, nullptr,
        false, true, SAVE_KeepGUID | SAVE_Unversioned | SAVE_NoError | SAVE_Async, Target, FDateTime::MinValue(),
        false);
    UPackage::WaitForAsyncFileWrites();
    SourcePackage->SetPackageFlagsTo(Flags);
    SourcePackage->SetDirtyFlag(false);
    if (!TestTrue(TEXT("Actual native skeleton source was cooked to split input"),
            Cooked && IFileManager::Get().FileSize(*Input) > 0 &&
                IFileManager::Get().FileSize(*FPaths::ChangeExtension(Input, TEXT("uexp"))) > 0))
        return false;
    FAssetRegistryModule::AssetDeleted(Source);
    Source = nullptr;
    FText UnloadError;
    if (!TestTrue(TEXT("Source donor unloads before actual proxy construction"),
            UPackageTools::UnloadPackages({SourcePackage}, UnloadError)))
        return false;
    if (!TestNull(TEXT("Actual proxy target is initially free"), FindObject<UObject>(nullptr, *Path)))
        return false;
    UPackage* Package = CreatePackage(*PackageName);
    USkeleton* Proxy = NewObject<USkeleton>(Package, TEXT("Skeleton"), RF_Public | RF_Standalone | RF_Transactional);
    FCreatorFillSkeleton::Identity(Proxy);
    UMetaData* Meta = Package->GetMetaData();
    Meta->SetValue(Proxy, TEXT("P4BlueprintStandIn"), TEXT("1"));
    FP4OriginalRecord Placeholder;
    Placeholder.Package = PackageName;
    Placeholder.Class = Proxy->GetClass()->GetPathName();
    Placeholder.Kind = TEXT("placeholder");
    Placeholder.Fidelity = TEXT("stand-in");
    Placeholder.ReferenceOnly = true;
    FString Why;
    if (!TestTrue(TEXT("Publish exact synthetic missing-source placeholder boundary"),
            P4MarkOriginalAsset(Proxy, Placeholder, Why)))
        return false;
    FAssetRegistryModule::AssetCreated(Proxy);
    Package->SetDirtyFlag(false);
    TestTrue(TEXT("Untouched actual placeholder has intrinsic proxy role"), P4HasIntrinsicGameProxy(Proxy));
    Meta->RemoveValue(Proxy, TEXT("P4BlueprintStandIn"));
    P4Kadan::UpdateReadOnlyAsset(Proxy);
    TestTrue(TEXT("Removing editable metadata cannot retire intrinsic proxy"), P4HasIntrinsicGameProxy(Proxy));
    Meta->SetValue(Proxy, TEXT("P4BlueprintStandIn"), TEXT("1"));
    IInterface_AssetUserData* Owner = CastChecked<IInterface_AssetUserData>(Proxy);
    UAssetUserData* OldRole = Owner->GetAssetUserDataOfClass(UP4CreatorSourceData::StaticClass());
    if (!TestNotNull(TEXT("Actual placeholder owns intrinsic role data"), OldRole))
        return false;
    const TArray<uint8> OldRoleBytes = FillRoleBytes(OldRole);
    const FGuid OldGuid = Proxy->GetGuid();
    FP4OriginalAssetImportResult NoSave;
    {
        P4ImportPublication::FImportScope Import;
        NoSave = P4ImportOriginalAssetNative(Input, FString(), false);
    }
    TestTrue(TEXT("Existing explicit no-save placeholder fill contract refuses publication"),
        NoSave.Error == EP4OriginalAssetImportError::SaveRequired);
    TestTrue(TEXT("No-save refusal retains proxy and private role fields"),
        P4HasIntrinsicGameProxy(Proxy) && FillRoleBytes(OldRole) == OldRoleBytes);
    int32 Rejected = 0;
    FP4OriginalAssetImportResult Failed;
    {
        const auto Previous = FCoreUObjectDelegates::IsPackageOKToSaveDelegate;
        const auto Ours = FCoreUObjectDelegates::FIsPackageOKToSaveDelegate::CreateLambda(
            [&](UPackage* Saving, const FString& Filename, FOutputDevice* Output)
            {
                if (Saving == Package &&
                    FPaths::ConvertRelativePathToFull(Filename).Equals(Destination, ESearchCase::IgnoreCase))
                {
                    ++Rejected;
                    return false;
                }
                return !Previous.IsBound() || Previous.Execute(Saving, Filename, Output);
            });
        FCoreUObjectDelegates::IsPackageOKToSaveDelegate = Ours;
        ON_SCOPE_EXIT
        {
            if (FCoreUObjectDelegates::IsPackageOKToSaveDelegate.GetHandle() == Ours.GetHandle())
                FCoreUObjectDelegates::IsPackageOKToSaveDelegate = Previous;
            else
                AddError(TEXT("Verified fill callback ownership changed during fixture"));
        };
        P4ImportPublication::FImportScope Import;
        Failed = P4ImportOriginalAssetNative(Input, FString(), true);
    }
    TestEqual(TEXT("Verified fill veto applies only five exact ordinary target saves"), Rejected, 5);
    TestTrue(TEXT("Actual filled placeholder failed specifically at publication"),
        Failed.Error == EP4OriginalAssetImportError::SaveFailed);
    TestTrue(TEXT("Failed existing fill preserves target identity"), FindObject<UObject>(nullptr, *Path) == Proxy);
    TestTrue(TEXT("Failed fill restores the exact old intrinsic role object"),
        Owner->GetAssetUserDataOfClass(UP4CreatorSourceData::StaticClass()) == OldRole);
    TestTrue(TEXT("Failed fill restores every private role field"),
        FillRoleBytes(OldRole) == OldRoleBytes && P4HasIntrinsicGameProxy(Proxy));
    TestTrue(TEXT("Failed fill restores native GUID and empty source"),
        Proxy->GetGuid() == OldGuid && Proxy->GetReferenceSkeleton().GetRawBoneNum() == 0);
    TestFalse(TEXT("Failed fill restores original package dirty state"), Package->IsDirty());
    TestTrue(TEXT("Failed fill restores explicit stand-in metadata"),
        FString(Meta->GetValue(Proxy, TEXT("P4BlueprintStandIn"))) == TEXT("1"));
    FP4OriginalAssetImportResult Filled;
    {
        P4ImportPublication::FImportScope Import;
        Filled = P4ImportOriginalAssetNative(Input, FString(), true);
    }
    if (!TestTrue(TEXT("Actual native source fills and saves the existing proxy: ") + Filled.ErrorDetail,
            Filled.IsValid() && Filled.Saved && Filled.FilledStandIn && Filled.Asset == Proxy))
        return false;
    TestFalse(TEXT("Successful verified fill retires intrinsic proxy immediately without tick"),
        P4HasIntrinsicGameProxy(Proxy));
    FString Origin;
    TestTrue(TEXT("Verified fill keeps negative actual game source at its exact current identity"),
        P4HasIntrinsicGameSource(Proxy, &Origin) && Origin == PackageName);
    TestFalse(TEXT("Verified game fill never becomes positive creator source"), P4HasCreatorSource(Proxy));
    TestTrue(TEXT("Actual verified input GUID and root bone were reconstructed"),
        Proxy->GetGuid() == ExpectedGuid && Proxy->GetReferenceSkeleton().GetRawBoneNum() == 1);
    Filled.Asset = nullptr;
    Proxy = nullptr;
    OldRole = nullptr;
    if (!TestTrue(TEXT("Successfully filled original package unloads"),
            UPackageTools::UnloadPackages({Package}, UnloadError)))
        return false;
    if (!TestNull(TEXT("Warm filled source object is released before cold read"), FindObject<UObject>(nullptr, *Path)))
        return false;
    USkeleton* Cold = LoadObject<USkeleton>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
    if (!TestNotNull(TEXT("Filled native source is reopened from its actual saved package"), Cold))
        return false;
    TestFalse(
        TEXT("Cold package already stores nonproxy role without ticker downgrade"), P4HasIntrinsicGameProxy(Cold));
    TestTrue(TEXT("Cold package retains exact negative game source origin"),
        P4HasIntrinsicGameSource(Cold, &Origin) && Origin == PackageName);
    TestTrue(TEXT("Cold actual native GUID and source bone survived"),
        Cold->GetGuid() == ExpectedGuid && Cold->GetReferenceSkeleton().GetRawBoneNum() == 1);
    return !HasAnyErrors();
}
#endif

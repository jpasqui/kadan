#include "P4OriginalAssetImport.h"
#include "P4OriginalImportSave.h"
#include "P4OriginalRecord.h"
#include "AssetRegistryModule.h"
#include "Curves/CurveFloat.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFilemanager.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "PackageTools.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4CreatorSourceRetry, "P4MapKit.Kadan.CreatorSource.FreshImportRetry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4CreatorSourceRetry::RunTest(const FString&)
{
    if (!GEditor)
    {
        AddError(TEXT("Native retry fixture requires the editor"));
        return false;
    }
    const FString PackageName = TEXT("/Game/__KadanCreatorSourceRetryTest/Curve");
    const FString Path = PackageName + TEXT(".Curve");
    const FString Destination = FPaths::ConvertRelativePathToFull(
        FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension()));
    if (FindPackage(nullptr, *PackageName) || FPackageName::DoesPackageExist(PackageName))
    {
        AddError(TEXT("Native retry fixture destination is occupied: ") + PackageName);
        return false;
    }
    const FString Folder = FPaths::ConvertRelativePathToFull(
        FPaths::ProjectSavedDir() / TEXT("KadanCreatorSourceRetryTest"));
    IFileManager::Get().MakeDirectory(*Folder, true);
    const FString Input = Folder / TEXT("Curve.uasset");
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
                const FString OwnedFile = FPaths::ChangeExtension(Base, Suffix);
                FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*OwnedFile, false);
                if (IFileManager::Get().FileExists(*OwnedFile))
                    IFileManager::Get().Delete(*OwnedFile, false, true);
            }
    };
    auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    TestFalse(TEXT("Prechecked native retry registry destination is empty"),
        Registry.GetAssetByObjectPath(FName(*Path)).IsValid());
    ITargetPlatform* Target = GetTargetPlatformManagerRef().FindTargetPlatform(TEXT("WindowsNoEditor"));
    if (!TestNotNull(TEXT("Actual native cook platform is available"), Target))
        return false;
    // Actual engine output is the source input. The synthetic fixture carries its exact
    // package identity in the native summary, with no invented package serialization.
    UPackage* SourcePackage = CreatePackage(*PackageName);
    SourcePackage->SetFolderName(FName(*PackageName));
    UCurveFloat* Source = NewObject<UCurveFloat>(
        SourcePackage, TEXT("Curve"), RF_Public | RF_Standalone | RF_Transactional);
    Source->FloatCurve.AddKey(0.f, 3.f);
    Source->FloatCurve.AddKey(1.f, 7.f);
    FAssetRegistryModule::AssetCreated(Source);
    const uint32 SourceFlags = SourcePackage->GetPackageFlags();
    if (!Target->HasEditorOnlyData())
        SourcePackage->SetPackageFlags(PKG_FilterEditorOnly);
    const bool Cooked = GEditor->SavePackage(SourcePackage, Source, RF_Public | RF_Standalone, *Input, GError, nullptr,
        false, true, SAVE_KeepGUID | SAVE_Unversioned | SAVE_NoError | SAVE_Async, Target, FDateTime::MinValue(),
        false);
    UPackage::WaitForAsyncFileWrites();
    SourcePackage->SetPackageFlagsTo(SourceFlags);
    SourcePackage->SetDirtyFlag(false);
    if (!TestTrue(TEXT("Native source fixture cook writes actual split input"),
            Cooked && IFileManager::Get().FileSize(*Input) > 0 &&
                IFileManager::Get().FileSize(*FPaths::ChangeExtension(Input, TEXT("uexp"))) > 0))
        return false;
    FAssetRegistryModule::AssetDeleted(Source);
    Source = nullptr;
    FText UnloadError;
    if (!TestTrue(TEXT("Native source donor unloads before fresh import"),
            UPackageTools::UnloadPackages({SourcePackage}, UnloadError)))
        return false;
    if (!TestNull(TEXT("Native donor leaves no existing target object"), FindObject<UObject>(nullptr, *Path)))
        return false;
    TestFalse(
        TEXT("Native donor leaves no target registry identity"), Registry.GetAssetByObjectPath(FName(*Path)).IsValid());
    int32 Rejected = 0;
    FP4OriginalAssetImportResult First;
    {
        const auto Previous = FCoreUObjectDelegates::IsPackageOKToSaveDelegate;
        const auto Ours = FCoreUObjectDelegates::FIsPackageOKToSaveDelegate::CreateLambda(
            [&](UPackage* Package, const FString& Filename, FOutputDevice* Output)
            {
                if (Package && Package->GetName() == PackageName &&
                    FPaths::ConvertRelativePathToFull(Filename).Equals(Destination, ESearchCase::IgnoreCase))
                {
                    ++Rejected;
                    return false;
                }
                return !Previous.IsBound() || Previous.Execute(Package, Filename, Output);
            });
        FCoreUObjectDelegates::IsPackageOKToSaveDelegate = Ours;
        ON_SCOPE_EXIT
        {
            if (FCoreUObjectDelegates::IsPackageOKToSaveDelegate.GetHandle() == Ours.GetHandle())
                FCoreUObjectDelegates::IsPackageOKToSaveDelegate = Previous;
            else
                AddError(TEXT("Native retry save callback was changed by another owner"));
        };
        P4ImportPublication::FImportScope Import;
        First = P4ImportOriginalAssetNative(Input, FString(), true);
    }
    // SAVE_NoError is the existing native save's flag; this exact delegate refusal is
    // a returned failure, with no log filters or broad expected-error suppression.
    TestEqual(TEXT("Only the five bounded ordinary target-save retries were refused"), Rejected, 5);
    TestTrue(TEXT("Actual first fresh import fails specifically at package publication"),
        First.Error == EP4OriginalAssetImportError::SaveFailed);
    TestTrue(TEXT("Refused publication was a newly created asset"), First.Created);
    TestNull(
        TEXT("Fresh refused publication leaves no object at its target identity"), FindObject<UObject>(nullptr, *Path));
    TestFalse(TEXT("Fresh refused publication deletes its target asset-registry identity"),
        Registry.GetAssetByObjectPath(FName(*Path)).IsValid());
    TestFalse(TEXT("Fresh refused publication leaves no unusable target package file"),
        FPackageName::DoesPackageExist(PackageName));
    FP4OriginalAssetImportResult Retry;
    {
        P4ImportPublication::FImportScope Import;
        Retry = P4ImportOriginalAssetNative(Input, FString(), true);
    }
    if (!TestTrue(TEXT("Same actual input retries as a fresh successful saved import: ") + Retry.ErrorDetail,
            Retry.IsValid() && Retry.Created && Retry.Saved && Retry.Asset))
        return false;
    TestTrue(
        TEXT("Successful retry has its real registry identity"), Registry.GetAssetByObjectPath(FName(*Path)).IsValid());
    TestTrue(TEXT("Successful retry has its ordinary saved package file"), FPackageName::DoesPackageExist(PackageName));
    UCurveFloat* Imported = Cast<UCurveFloat>(Retry.Asset);
    if (TestNotNull(TEXT("Actual retry preserved native curve class"), Imported))
    {
        TestEqual(TEXT("Actual retry preserved first source curve value"), Imported->FloatCurve.Eval(0.f), 3.f);
        TestEqual(TEXT("Actual retry preserved second source curve value"), Imported->FloatCurve.Eval(1.f), 7.f);
        TestTrue(TEXT("Actual retry is recorded as original source"), P4QueryOriginalAsset(Imported).IsOriginal());
    }
    return !HasAnyErrors();
}
#endif

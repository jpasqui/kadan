#include "P4KadanImport.h"
#include "AssetRegistryModule.h"
#include "Curves/CurveFloat.h"
#include "PackageTools.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"

namespace
{
struct FPresenceFixture
{
    TArray<FString> Packages;
    FString Name(const FString& Role)
    {
        const FString Package = TEXT("/Game/KadanPresence_") + Role + TEXT("_") +
            FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Packages.Add(Package);
        return Package;
    }
    static UObject* Object(const FString& Name)
    {
        return NewObject<UCurveFloat>(
            CreatePackage(*Name), *FPackageName::GetLongPackageAssetName(Name), RF_Public | RF_Standalone);
    }
    static void Unload(UPackage* Package)
    {
        TArray<UObject*> Inside;
        GetObjectsWithOuter(Package, Inside, true);
        for (UObject* Object : Inside)
        {
            Object->ClearFlags(RF_Public | RF_Standalone);
            Object->MarkPendingKill();
        }
        ResetLoaders(Package);
        Package->SetDirtyFlag(false);
        Package->ClearFlags(RF_Public | RF_Standalone);
    }
    ~FPresenceFixture()
    {
        // Each name is unique to this test. Remove even the deliberately
        // stale registry row by its exact asset path before final GC.
        for (const FString& Name : Packages)
        {
            UPackage* Package = FindPackage(nullptr, *Name);
            UObject* Asset = FindObject<UObject>(
                nullptr, *(Name + TEXT(".") + FPackageName::GetLongPackageAssetName(Name)));
            if (!Asset || Asset->IsPendingKill())
                Asset = Object(Name);
            FAssetRegistryModule::AssetDeleted(Asset);
            Package = Asset->GetOutermost();
            Unload(Package);
            FString File;
            if (FPackageName::TryConvertLongPackageNameToFilename(Name, File, TEXT(".uasset")))
                IFileManager::Get().Delete(*File, false, false);
        }
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    }
};
FP4KadanImportPlan PresencePlan(const FString& Package)
{
    auto Source = MakeShared<FP4KadanSourceAsset>();
    Source->Package = Package;
    Source->Kind = TEXT("StaticMesh");
    Source->SourceBytes = 37;
    return P4KadanPlanImport({Source}, {Package}, true);
}
} //namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanResumePresenceTest, "P4MapKit.Kadan.ResumePresence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FP4KadanResumePresenceTest::RunTest(const FString&)
{
    FPresenceFixture Fixture;
    // The game registry may retain a material from an optional engine plugin
    // that is not mounted in this editor. It must remain an explicit missing
    // source refusal without turning a presence query into an engine error.
    const FString Unmounted = TEXT("/P4PresenceUnmounted_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) +
        TEXT("/M_Absent");
    FString UnmountedFile;
    if (!TestFalse(TEXT("The unique optional-plugin fixture has no content mount"),
            FPackageName::TryConvertLongPackageNameToFilename(Unmounted, UnmountedFile)))
        return false;
    for (const bool Resume : {false, true})
    {
        const auto MissingPlugin = P4KadanPlanImport({}, {Unmounted}, Resume, true);
        TestEqual(TEXT("The unmounted registry root remains requested"), MissingPlugin.Roots, 1);
        TestEqual(TEXT("The unmounted root does not become a native source asset"), MissingPlugin.Assets.Num(), 0);
        TestEqual(TEXT("The unmounted root is never reported already present"), MissingPlugin.AlreadyPresent, 0);
        TestTrue(TEXT("Canonical, source-identity and missing-package refusals all survive"),
            MissingPlugin.Problems.Num() == 3 &&
                MissingPlugin.Problems.Contains(Unmounted + TEXT(" is not a unique canonical requested package.")) &&
                MissingPlugin.Problems.Contains(Unmounted +
                    TEXT(
                        " requested source identity was not verified: No scanned source has this package identity.")) &&
                MissingPlugin.Problems.Contains(
                    Unmounted + TEXT(" is required, but it is in neither this game folder nor the project.")));
    }
    const FString MissingMounted = Fixture.Name(TEXT("MissingMounted"));
    FString MissingMountedFile;
    if (!TestTrue(TEXT("The missing game fixture has a real content mount"),
            FPackageName::TryConvertLongPackageNameToFilename(MissingMounted, MissingMountedFile)))
        return false;
    TestFalse(TEXT("The mounted missing fixture has no file"), IFileManager::Get().FileExists(*MissingMountedFile));
    const auto MissingMountedPlan = PresencePlan(MissingMounted);
    TestEqual(TEXT("A mounted missing package still needs actual source work"), MissingMountedPlan.AlreadyPresent, 0);
    TestEqual(
        TEXT("Mounted missing source bytes remain accounted for"), MissingMountedPlan.RemainingSourceBytes, int64(37));
    auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
    const FString UserName = Fixture.Name(TEXT("Unsaved"));
    UObject* User = FPresenceFixture::Object(UserName);
    if (!TestTrue(TEXT("Fixture creates a real top-level unsaved asset"), User->IsAsset()))
        return false;
    TestFalse(TEXT("Live user object has no package file"), FPackageName::DoesPackageExist(UserName));
    TArray<FAssetData> Registered;
    Registry.GetAssetsByPackageName(FName(*UserName), Registered, true);
    TestEqual(TEXT("Live unsaved user object has no disk registry row"), Registered.Num(), 0);
    const FP4KadanImportPlan UserPlan = PresencePlan(UserName);
    TestEqual(TEXT("Resume protects an unregistered live unsaved user asset"), UserPlan.AlreadyPresent, 1);
    const FP4KadanImportResult Preserved = P4KadanRunImport(FP4KadanImportSettings(), UserPlan, true);
    TestTrue(TEXT("Resume preserves the user asset without reading absent source inputs"), Preserved.Ok);
    TestTrue(TEXT("The same live user object remains"), FindObject<UObject>(nullptr, *User->GetPathName()) == User);
    TestFalse(TEXT("Resume did not save over the unsaved user package"), FPackageName::DoesPackageExist(UserName));

    const FString StandInName = Fixture.Name(TEXT("StandIn"));
    UObject* StandIn = FPresenceFixture::Object(StandInName);
    FAssetRegistryModule::AssetCreated(StandIn);
    StandIn->GetOutermost()->GetMetaData()->SetValue(StandIn, FName(TEXT("P4BlueprintStandIn")), TEXT("1"));
    TestEqual(TEXT("A positively marked live stand-in remains eligible for guarded fill"),
        PresencePlan(StandInName).AlreadyPresent, 0);
    StandIn->GetOutermost()->GetMetaData()->RemoveValue(StandIn, FName(TEXT("P4BlueprintStandIn")));
    TestEqual(
        TEXT("Live real metadata overrides older stand-in registry tags"), PresencePlan(StandInName).AlreadyPresent, 1);

    const FString StaleName = Fixture.Name(TEXT("Stale"));
    UObject* Stale = FPresenceFixture::Object(StaleName);
    UPackage* StalePackage = Stale->GetOutermost();
    StalePackage->GetMetaData();
    FString File;
    if (!TestTrue(TEXT("Unique stale fixture has a package filename"),
            FPackageName::TryConvertLongPackageNameToFilename(StaleName, File, TEXT(".uasset"))))
        return false;
    if (!TestTrue(TEXT("Creates the owned fixture package folder"),
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true)))
        return false;
    if (!TestTrue(TEXT("Saves a real package before its registry becomes stale"),
            UPackage::SavePackage(
                StalePackage, Stale, RF_Public | RF_Standalone, *File, GError, nullptr, false, true, SAVE_NoError)))
        return false;
    Registry.ScanFilesSynchronous({File}, true);
    Registered.Empty();
    Registry.GetAssetsByPackageName(FName(*StaleName), Registered, true);
    if (!TestTrue(TEXT("The actual saved fixture is registered on disk"), Registered.Num() > 0))
        return false;
    TestEqual(TEXT("A real package is preserved while it exists"), PresencePlan(StaleName).AlreadyPresent, 1);
    StalePackage->SetDirtyFlag(false);
    Stale = nullptr;
    FText UnloadError;
    if (!TestTrue(TEXT("Unloads the actual saved package through the native editor API"),
            UPackageTools::UnloadPackages({StalePackage}, UnloadError)))
    {
        AddError(UnloadError.ToString());
        return false;
    }
    StalePackage = nullptr;
    Registered.Empty();
    Registry.GetAssetsByPackageName(FName(*StaleName), Registered, true);
    if (!TestTrue(TEXT("The saved asset identity remains in the registry after unload"), Registered.Num() > 0))
        return false;
    if (!TestTrue(TEXT("The stale fixture has no loaded asset"),
            FindObject<UObject>(nullptr, *(StaleName + TEXT(".") + FPackageName::GetLongPackageAssetName(StaleName))) ==
                nullptr))
        return false;
    if (!TestTrue(
            TEXT("Removes only the unique unloaded fixture file"), IFileManager::Get().Delete(*File, false, false)))
        return false;
    TestFalse(TEXT("The stale fixture has no package on disk"), FPackageName::DoesPackageExist(StaleName));
    Registered.Empty();
    Registry.GetAssetsByPackageName(FName(*StaleName), Registered, true);
    if (!TestTrue(TEXT("The disk registry identity is still stale immediately before resume"), Registered.Num() > 0))
        return false;
    const FP4KadanImportPlan Missing = PresencePlan(StaleName);
    TestEqual(TEXT("A registry-only identity is not an existing import"), Missing.AlreadyPresent, 0);
    TestEqual(TEXT("Missing stale package stays in remaining source work"), Missing.RemainingSourceBytes, int64(37));
    const FP4KadanImportResult Retried = P4KadanRunImport(FP4KadanImportSettings(), Missing, true);
    TestFalse(TEXT("Fresh resume attempts the absent source instead of claiming it exists"), Retried.Ok);
    TestTrue(TEXT("Missing actual source is reported as an import refusal"), Retried.NotImported.Contains(StaleName));
    TestFalse(TEXT("Stale identity is not reported as an existing asset preserved"),
        Retried.Report.Contains(TEXT("existing asset preserved")));

    const FString UnknownName = Fixture.Name(TEXT("UnknownDisk"));
    FString UnknownFile;
    if (!FPackageName::TryConvertLongPackageNameToFilename(UnknownName, UnknownFile, TEXT(".uasset")))
        return false;
    UObject* Unknown = FPresenceFixture::Object(UnknownName);
    UPackage* UnknownPackage = Unknown->GetOutermost();
    UnknownPackage->GetMetaData();
    if (!TestTrue(TEXT("Saves a real user package without an import record"),
            UPackage::SavePackage(UnknownPackage, Unknown, RF_Public | RF_Standalone, *UnknownFile, GError, nullptr,
                false, true, SAVE_NoError)))
        return false;
    FAssetRegistryModule::AssetDeleted(Unknown);
    UnknownPackage->SetDirtyFlag(false);
    Unknown = nullptr;
    if (!TestTrue(TEXT("Unloads the real unknown user package"),
            UPackageTools::UnloadPackages({UnknownPackage}, UnloadError)))
    {
        AddError(UnloadError.ToString());
        return false;
    }
    UnknownPackage = nullptr;
    TArray<uint8> UserBytes;
    if (!TestTrue(TEXT("Reads the actual unknown user package before resume"),
            FFileHelper::LoadFileToArray(UserBytes, *UnknownFile)))
        return false;
    const FP4KadanImportPlan UnknownPlan = PresencePlan(UnknownName);
    TestEqual(TEXT("An unscanned on-disk user package is preserved"), UnknownPlan.AlreadyPresent, 1);
    const FP4KadanImportResult UnknownPreserved = P4KadanRunImport(FP4KadanImportSettings(), UnknownPlan, true);
    TestTrue(TEXT("Resume does not fill or reinterpret an unknown on-disk file"), UnknownPreserved.Ok);
    TArray<uint8> After;
    TestTrue(TEXT("Unknown file bytes remain untouched"),
        FFileHelper::LoadFileToArray(After, *UnknownFile) && After == UserBytes);
    return true;
}

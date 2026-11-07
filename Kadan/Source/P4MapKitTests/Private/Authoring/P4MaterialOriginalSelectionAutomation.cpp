#if WITH_DEV_AUTOMATION_TESTS
#include "P4ExportContainer.h"
#include "P4ExportSelection.h"
#include "P4NativeHash.h"
#include "P4OriginalRecord.h"
#include "P4RegistryInputLease.h"
#include "AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MaterialOriginalSelectionTest, "P4MapKit.Export.MaterialOriginalSelection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MaterialOriginalSelectionTest::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MATERIAL_SELECTION")) != TEXT("1"))
    {
        AddWarning(
            TEXT("P4MapKit test not run. ") TEXT("Opt-in controlled original-parent selection was not exercised."));
        return true;
    }
    const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    if (!TestTrue(TEXT("Only the retained private DevHost is used"),
            !Root.IsEmpty() &&
                FPaths::IsSamePath(FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()),
                    FPaths::ConvertRelativePathToFull(Root / TEXT("dev-host-tests/DevHost.uproject")))))
        return false;
    const FString ParentPackage = TEXT("/Game/Carrot4/__P4MaterialComparisonCold/NativeComparison");
    const FString File = FPackageName::LongPackageNameToFilename(
        ParentPackage, FPackageName::GetAssetPackageExtension());
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("P4MapKit/MaterialOriginalSelection");
    const FString Receipt = Directory / TEXT("selection.json");
    if (!TestFalse(TEXT("Existing selection evidence is never overwritten"), IFileManager::Get().FileExists(*Receipt)))
        return false;
    FString Text;
    TSharedPtr<FJsonObject> Saved;
    if (!TestTrue(TEXT("Reuse the successful existing138 save proof"),
            FFileHelper::LoadFileToString(
                Text, *(FPaths::ProjectSavedDir() / TEXT("P4MapKit/MaterialOriginalComparison/save.json"))) &&
                FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Saved) && Saved.IsValid() &&
                Saved->GetBoolField(TEXT("passed")) &&
                Saved->GetStringField(TEXT("owner")) == TEXT("p4-material-native-comparison-controlled-v1")))
        return false;
    P4RegistryLease::FInput Lease;
    if (!TestTrue(
            TEXT("The retained parent file is leased against writes and deletion"), Lease.Open(std::wstring(*File))))
        return false;
    int64 BeforeSize = 0;
    FString BeforeSha, Error;
    if (!TestTrue(TEXT("The parent is still exactly the qualified138 output"),
            P4HashFile(File, 1024 * 1024, BeforeSize, BeforeSha) &&
                BeforeSize == Saved->GetNumberField(TEXT("file_size")) &&
                P4ExactNativeString(BeforeSha, Saved->GetStringField(TEXT("file_sha256")))))
        return false;
    if (!TestFalse(TEXT("The retained parent starts unloaded"), FindPackage(nullptr, *ParentPackage) != nullptr))
        return false;
    TStrongObjectPtr<UMaterial> Parent(LoadObject<UMaterial>(nullptr, *(ParentPackage + TEXT(".NativeComparison"))));
    if (!TestNotNull(TEXT("The actual retained controlled material loads"), Parent.Get()))
        return false;
    UPackage* ParentOuter = Parent->GetOutermost();
    FP4OriginalRecord Record;
    if (!TestTrue(TEXT("The original baseline is valid and the package is clean"),
            P4ReadOriginalRecord(Parent.Get(), Record, Error) &&
                Record.ComparisonSchema == TEXT("material-native-state-v1") && Record.ReadOnly &&
                Record.ReferenceOnly && !ParentOuter->IsDirty() &&
                P4DiagnoseOriginalAsset(Parent.Get()).Comparison == EP4OriginalComparison::Equal))
        return false;
    const FString Marker = ParentOuter->GetMetaData()->GetValue(Parent.Get(), TEXT("P4OriginalRecord"));
    const bool OriginalTwoSided = Parent->TwoSided;
    ON_SCOPE_EXIT
    {
        Parent->TwoSided = OriginalTwoSided;
        ParentOuter->SetDirtyFlag(false);
    };
    const FString Repo = FPaths::GetPath(FPaths::GetPath(Root));
    FP4ExportContainerTools Tools;
    Tools.Retoc = Repo / TEXT("private/retoc/retoc.exe");
    Tools.Repak = Repo / TEXT("private/repak/repak.exe");
    FP4ExportGameInventory Inventory;
    if (!TestTrue(TEXT("Selection uses the actual selected game inventory"),
            P4ReadExportGameInventory(Tools,
                FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS")),
                Directory / TEXT("inventory"), Inventory, Error)))
    {
        AddError(Error);
        return false;
    }
    if (!TestFalse(TEXT("The controlled parent is not claimed as an actual game package"),
            Inventory.Contains(FName(*ParentPackage))))
        return false;
    FP4ModDescriptor Mod;
    Mod.Id = TEXT("native.material.selection");
    Mod.Name = TEXT("Native material selection");
    Mod.Version = TEXT("1.0.0");
    FP4ModNames Names;
    if (!TestTrue(TEXT("Creator namespace derives normally"), P4ModDerivedNames(Mod, Names, Error)))
        return false;
    const FString ChildPackage = Names.PackageRoot / TEXT("SelectionChild");
    if (!TestTrue(TEXT("The controlled child namespace is absent"),
            !FPackageName::DoesPackageExist(ChildPackage) && !FindPackage(nullptr, *ChildPackage)))
        return false;
    TStrongObjectPtr<UMaterialInstanceConstant> Child(NewObject<UMaterialInstanceConstant>(
        CreatePackage(*ChildPackage), TEXT("SelectionChild"), RF_Public | RF_Standalone | RF_Transactional));
    Child->SetParentEditorOnly(Parent.Get());
    FAssetRegistryModule::AssetCreated(Child.Get());
    ON_SCOPE_EXIT
    {
        FAssetRegistryModule::AssetDeleted(Child.Get());
        Child->ClearFlags(RF_Public | RF_Standalone);
        Child->Rename(
            nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional | REN_ForceNoResetLoaders);
        Child->MarkPendingKill();
    };
    // Explicit roots test the real selection consumer without saving a new child
    // merely to populate the registry's on-disk dependency graph. Dependency
    // traversal, cooking and game-source fidelity are separate qualifications.
    const TArray<FP4ExportRoot> Roots = {
        {EP4ExportRootKind::Package, ChildPackage}, {EP4ExportRootKind::Package, ParentPackage}};
    const auto ParentRow = [&](const FP4ExportSelectionPreview& Preview) -> const FP4ExportSelectionRow*
    {
        return Preview.Rows.FindByPredicate(
            [&](const FP4ExportSelectionRow& Row) { return Row.Package == ParentPackage; });
    };
    FP4ExportSelectionPreview Clean;
    if (!TestTrue(TEXT("Production preview reads both actual loaded roots"),
            P4PreviewExportSelection(Mod, Roots, Inventory, {}, Clean, Error)))
        return false;
    const auto* CleanParent = ParentRow(Clean);
    TestTrue(TEXT("Only the creator child is a cook candidate while the unchanged locked parent is omitted"),
        Clean.Rows.Num() == 2 && Clean.Refused.Num() == 0 && Clean.CookCandidates == TArray<FString>{ChildPackage} &&
            CleanParent && CleanParent->Disposition == EP4ExportDisposition::UnchangedGame);
    TestTrue(TEXT("The child still has the actual recovered parent"), Child->Parent == Parent.Get());
    Parent->TwoSided = !OriginalTwoSided;
    ParentOuter->SetDirtyFlag(false);
    FP4ExportSelectionPreview Edited;
    const bool EditedRead = P4PreviewExportSelection(Mod, Roots, Inventory, {}, Edited, Error);
    const auto* EditedParent = ParentRow(Edited);
    TestTrue(TEXT("A changed parent cannot be omitted even with a cleared dirty flag"),
        EditedRead && EditedParent && EditedParent->Disposition == EP4ExportDisposition::NeedsReplacementProof &&
            Edited.Refused.Num() == 1 && !Edited.CookCandidates.Contains(ParentPackage));
    Parent->TwoSided = OriginalTwoSided;
    ParentOuter->SetDirtyFlag(false);
    FP4ExportSelectionPreview Restored;
    TestTrue(TEXT("Restoring native state restores omission without recapturing provenance"),
        P4PreviewExportSelection(Mod, Roots, Inventory, {}, Restored, Error) && Restored.Refused.Num() == 0 &&
            ParentRow(Restored) && ParentRow(Restored)->Disposition == EP4ExportDisposition::UnchangedGame &&
            P4ExactNativeString(Marker, ParentOuter->GetMetaData()->GetValue(Parent.Get(), TEXT("P4OriginalRecord"))) &&
            P4DiagnoseOriginalAsset(Parent.Get()).Comparison == EP4OriginalComparison::Equal &&
            !ParentOuter->IsDirty());
    int64 AfterSize = 0;
    FString AfterSha;
    TestTrue(TEXT("The held parent file and actual bytes are preserved"),
        Lease.Current() && P4HashFile(File, 1024 * 1024, AfterSize, AfterSha) && BeforeSize == AfterSize &&
            P4ExactNativeString(BeforeSha, AfterSha));
    TestFalse(TEXT("No creator child content was saved"), FPackageName::DoesPackageExist(ChildPackage));
    auto Proof = MakeShared<FJsonObject>();
    Proof->SetStringField(TEXT("owner"), TEXT("p4-material-original-selection-controlled-v1"));
    Proof->SetNumberField(TEXT("pid"), FPlatformProcess::GetCurrentProcessId());
    Proof->SetStringField(TEXT("parent_file_sha256"), BeforeSha);
    Proof->SetStringField(TEXT("registry_sha256"), Inventory.RegistrySha256());
    Proof->SetStringField(TEXT("original_record"), Marker);
    Proof->SetBoolField(TEXT("explicit_roots_only"), true);
    Proof->SetBoolField(TEXT("game_source_fidelity_qualified"), false);
    Proof->SetBoolField(TEXT("parent_is_game_inventory_package"), Inventory.Contains(FName(*ParentPackage)));
    Proof->SetBoolField(TEXT("cook_or_game_qualified"), false);
    Proof->SetBoolField(TEXT("passed"), !HasAnyErrors());
    FString Json;
    FJsonSerializer::Serialize(Proof, TJsonWriterFactory<>::Create(&Json));
    TestTrue(TEXT("The new bounded selection receipt is retained"),
        IFileManager::Get().MakeDirectory(*Directory, true) &&
            FFileHelper::SaveStringToFile(Json, *Receipt, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    return !HasAnyErrors();
}
#endif

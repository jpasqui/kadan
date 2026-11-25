#include "P4KadanEditor.h"
#include "P4KadanReadOnly.h"
#include "P4OriginalRecord.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFilemanager.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "PackageTools.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

// Kadan keeps a game asset it holds only for reference by its own guards, not by its file's read-only mark. Locking
// such an asset leaves its file writable on disk. A file an earlier version of Kadan marked read-only is writable once
// Kadan locks its package, which is what both the start-up pass over the asset registry and the loading of an asset do.
// A save of a locked reference is still refused with Kadan's reason, and its file is not written.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanGameAssetFileTest, "P4MapKit.Kadan.GameAssetFileStaysWritable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4KadanGameAssetFileTest::RunTest(const FString&)
{
    if (!GEditor)
    {
        AddWarning(TEXT("P4MapKit test not run. It saves packages through the editor, and there is no editor."));
        return true;
    }
    P4RegisterKadanEditor();
    IPlatformFile& Disk = FPlatformFileManager::Get().GetPlatformFile();
    const FString Root = TEXT("/Game/__KadanGameAssetFile_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    struct FOne
    {
        FString Package, File;
        TStrongObjectPtr<UMaterial> Asset;
    };
    // The first stands for a reference-only game asset imported by this version, the second for one an earlier version
    // marked.
    const TCHAR* const Names[2] = {TEXT("Fresh"), TEXT("Marked")};
    FOne Ones[2];
    for (int32 Index = 0; Index < 2; ++Index)
    {
        Ones[Index].Package = Root + TEXT("/") + Names[Index];
        Ones[Index].File = FPaths::ConvertRelativePathToFull(
            FPackageName::LongPackageNameToFilename(Ones[Index].Package, FPackageName::GetAssetPackageExtension()));
    }
    ON_SCOPE_EXIT
    {
        // Only this run's packages, under a folder named for this run, are removed.
        TArray<UPackage*> Packages;
        for (FOne& One : Ones)
        {
            if (UMaterial* Asset = One.Asset.Get())
            {
                UPackage* Package = Asset->GetOutermost();
                Package->GetMetaData()->RemoveValue(Asset, TEXT("P4OriginalRecord"));
                P4Kadan::TrackAssetSource(Asset);
                Package->SetDirtyFlag(false);
                Packages.Add(Package);
            }
            One.Asset.Reset();
        }
        UPackageTools::UnloadPackages(Packages);
        for (const FOne& One : Ones)
            if (IFileManager::Get().FileExists(*One.File) && !IFileManager::Get().Delete(*One.File, false, true, true))
                AddWarning(TEXT("Could not remove this run's package file ") + One.File);
    };

    for (int32 Index = 0; Index < 2; ++Index)
    {
        FOne& One = Ones[Index];
        UPackage* Package = CreatePackage(*One.Package);
        One.Asset.Reset(NewObject<UMaterial>(Package, Names[Index], RF_Public | RF_Standalone | RF_Transactional));
        const bool Written = UPackage::SavePackage(Package, One.Asset.Get(), RF_Public | RF_Standalone, *One.File, GWarn,
            nullptr, false, true, SAVE_NoError);
        if (!TestTrue(TEXT("The package is written before it is the game's: ") + One.Package,
                Written && IFileManager::Get().FileExists(*One.File)))
            return false;
    }
    if (!TestTrue(TEXT("The second file is marked read-only, as an earlier version of Kadan left a game asset's file"),
            Disk.SetReadOnly(*Ones[1].File, true) && Disk.IsReadOnly(*Ones[1].File)))
        return false;

    for (FOne& One : Ones)
    {
        FP4OriginalRecord Record;
        Record.Package = One.Package;
        Record.Class = One.Asset->GetClass()->GetPathName();
        Record.Kind = TEXT("test-game-asset");
        // Kadan locks a game asset only when the editor cannot hold all of its data.
        Record.ReferenceOnly = true;
        Record.SourceDigest = FString::ChrN(64, TEXT('a'));
        Record.ComparisonDigest = FString::ChrN(64, TEXT('b'));
        Record.SourceDigestKind = TEXT("fixture");
        Record.ComparisonSchema = TEXT("fixture");
        FString Why;
        const bool Marked = P4MarkOriginalAsset(One.Asset.Get(), Record, Why);
        if (!TestTrue(TEXT("The reference-only game asset is locked: ") + One.Package + TEXT(" ") + Why,
                Marked && P4Kadan::IsLockedPackage(One.Asset->GetOutermost())))
            return false;
    }
    TestFalse(TEXT("Locking a game reference leaves its file writable"), Disk.IsReadOnly(*Ones[0].File));
    TestFalse(TEXT("A file an earlier version of Kadan marked read-only is writable once Kadan locks its package"),
        Disk.IsReadOnly(*Ones[1].File));

    for (FOne& One : Ones)
    {
        UPackage* Package = One.Asset->GetOutermost();
        TArray<uint8> Before;
        if (!TestTrue(TEXT("The file is read before the save: ") + One.File, FFileHelper::LoadFileToArray(Before, *One.File)))
            return false;
        const FDateTime WrittenAt = IFileManager::Get().GetTimeStamp(*One.File);
        P4Kadan::TakeReadOnlyMessages();
        Package->SetDirtyFlag(true);
        // A plugin or a script that calls Unreal's save, and the editor's own save.
        TestFalse(TEXT("A save of the locked game reference is refused: ") + One.Package,
            UPackage::SavePackage(Package, One.Asset.Get(), RF_Public | RF_Standalone, *One.File, GWarn, nullptr, false,
                true, SAVE_NoError));
        TestFalse(TEXT("The editor's save of the locked game reference is refused: ") + One.Package,
            GEditor->SavePackage(Package, One.Asset.Get(), RF_Public | RF_Standalone, *One.File, GWarn, nullptr, false,
                false, SAVE_NoError));
        const TArray<FString> Told = P4Kadan::TakeReadOnlyMessages();
        const FString Leaf = FPackageName::GetShortName(One.Package);
        TestTrue(TEXT("Kadan says why it did not save the game reference: ") + FString::Join(Told, TEXT(" / ")),
            Told.ContainsByPredicate([&Leaf](const FString& Message)
                { return Message.Contains(TEXT("did not save")) && Message.Contains(Leaf); }));
        TArray<uint8> After;
        TestTrue(TEXT("The refused save does not write the file: ") + One.File,
            FFileHelper::LoadFileToArray(After, *One.File) && After == Before &&
                IFileManager::Get().GetTimeStamp(*One.File) == WrittenAt);
        TestFalse(TEXT("The file is still writable after the refused save: ") + One.File, Disk.IsReadOnly(*One.File));
        Package->SetDirtyFlag(false);
    }
    return !HasAnyErrors();
}

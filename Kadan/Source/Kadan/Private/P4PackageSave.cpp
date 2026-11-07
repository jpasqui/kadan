#include "P4PackageSave.h"
#include "P4GamePaths.h"
#include "P4OriginalRecord.h"
#include "P4OriginalImportSave.h"
#include "P4KadanReadOnly.h"
#include "P4MaterialGraph.h"
#include "P4MaterialInstance.h"
#include "P4StaticMeshContent.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/FeedbackContext.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

namespace
{
// A reconstructed original is already constructed and source-checked. The
// editor's no-physics save fallback runs construction scripts twice, which can
// replace that component tree or round authored relative transforms. Supply
// the temporary save context without running construction, as diagnostic cooks do.
struct FOriginalWorldSaveContext
{
    UWorld* World = nullptr;
    ULevel* Level = nullptr;
    bool MadeScene = false, Registered = false;
    explicit FOriginalWorldSaveContext(UObject* Asset)
    {
        auto* Candidate = Cast<UWorld>(Asset);
        FP4OriginalRecord Record;
        FString Error;
        if (!Candidate || Candidate->WorldType != EWorldType::Inactive || !Candidate->PersistentLevel ||
            !P4ReadOriginalRecord(Candidate, Record, Error) || Record.Kind != TEXT("level"))
            return;
        World = Candidate;
        Level = World->PersistentLevel;
        MadeScene = !World->GetPhysicsScene() && !(Level->OwningWorld && Level->OwningWorld->GetPhysicsScene());
        if (MadeScene)
            World->CreatePhysicsScene();
        Registered = !Level->bAreComponentsCurrentlyRegistered;
        if (Registered)
            Level->bAreComponentsCurrentlyRegistered = true;
    }
    ~FOriginalWorldSaveContext()
    {
        if (Registered)
            Level->bAreComponentsCurrentlyRegistered = false;
        if (MadeScene)
            World->SetPhysicsScene(nullptr);
    }
};

// Where an asset's package is saved, as a whole path. Empty when it has no place in Content.
FString FileOf(UObject* Asset)
{
    UPackage* Package = Asset->GetOutermost();
    FString File;
    if (Package == GetTransientPackage() ||
        !FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), File,
            Asset->IsA<UWorld>() ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension()))
        return FString();
    return FPaths::ConvertRelativePathToFull(File);
}

// The engine's save of one package to its own file. A save that fails is tried again a few times, a
// moment apart.
bool Write(UObject* Asset, const FString& File, FString& Why)
{
    // A file that may not be written is not one a second try helps with. A locked game asset's file is
    // read-only so that nothing else writes it. This is the importer writing it again (a stand-in it
    // fills), which makes it writable for the save and read-only again after it.
    if (IFileManager::Get().FileExists(*File) && IFileManager::Get().IsReadOnly(*File) &&
        !P4Kadan::IsLockedPackage(Asset->GetOutermost()))
    {
        Why = TEXT("save-file-is-read-only: ") + File;
        return false;
    }
    P4ImportPublication::FSaveScope ImportSave(Asset->GetOutermost());
    bool Saved = false;
    for (int32 Attempt = 0; Attempt < 5 && !Saved; ++Attempt)
    {
        if (Attempt)
            FPlatformProcess::Sleep(0.05f);
        Saved = GEditor->SavePackage(Asset->GetOutermost(), Asset,
            Asset->IsA<UWorld>() ? RF_Standalone : RF_Public | RF_Standalone, *File, GWarn, nullptr, false, false,
            SAVE_NoError);
    }
    if (!Saved)
        Why = TEXT("save-engine-could-not-write: ") + File;
    return Saved;
}

// The assets an asset refers to, for the kinds that are made with others: a mesh's materials, an
// instance's parent and textures, a material's textures and collections.
void References(UObject* Asset, TArray<UObject*>& Out)
{
    if (UStaticMesh* Mesh = Cast<UStaticMesh>(Asset))
        P4StaticMeshSaveReferences(Mesh, Out);
    else if (UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(Asset))
        P4MaterialInstanceSaveReferences(Instance, Out);
    else if (UMaterial* Material = Cast<UMaterial>(Asset))
        P4MaterialSaveReferences(Material, Out);
}

// What an asset refers to that has no file yet is saved before it: a file that names a package without
// a file does not load whole in another editor. What already has a file is left as it is.
bool SaveWhatHasNoFile(UObject* Asset, TSet<UObject*>& Seen, FString& Why)
{
    TArray<UObject*> Outside;
    References(Asset, Outside);
    for (UObject* Object : Outside)
    {
        if (!IsValid(Object) || Seen.Contains(Object))
            continue;
        Seen.Add(Object);
        const FString Package = Object->GetOutermost()->GetName();
        if (!Object->IsAsset() || !P4IsContentPackage(Package) || FPackageName::DoesPackageExist(Package))
            continue;
        const FString File = FileOf(Object);
        if (File.IsEmpty())
        {
            Why = TEXT("save-package-has-no-file: ") + Package;
            return false;
        }
        if (!SaveWhatHasNoFile(Object, Seen, Why) || !Write(Object, File, Why))
            return false;
    }
    return true;
}
} //namespace

bool P4SaveAssetPackage(UObject* Asset, const TFunction<bool()>& StillRight, FString& Why)
{
    Why.Reset();
    if (!IsInGameThread() || !GEditor || !IsValid(Asset))
    {
        Why = TEXT("save-needs-the-editor-thread-and-an-asset");
        return false;
    }
    UPackage* Package = Asset->GetOutermost();
    const FString File = FileOf(Asset);
    if (File.IsEmpty())
    {
        Why = TEXT("save-package-has-no-file: ") + Package->GetName();
        return false;
    }
    FOriginalWorldSaveContext SourceWorldSave(Asset);
    // A rejected save restores both its previous disk bundle and the package's
    // native save identity. Successful ordinary saves keep the engine's new identity.
    const uint32 PreviousFlags = Package->GetPackageFlags();
    const uint64 PreviousFileSize = Package->FileSize;
    const FName PreviousFileName = Package->FileName;
    const FGuid PreviousGuid = Package->GetGuid();
#if WITH_EDITORONLY_DATA
    const FGuid PreviousPersistentGuid = Package->GetPersistentGuid();
#endif
    const bool PreviousDirty = Package->IsDirty();
    IFileManager& Files = IFileManager::Get();
    TMap<FString, TArray<uint8>> Before;
    TArray<FString> PackageFiles;
    const FString Stem = File.LeftChop(FPaths::GetExtension(File, true).Len());
    for (const FString& Suffix :
        {FPaths::GetExtension(File, true), FString(TEXT(".uexp")), FString(TEXT(".ubulk")), FString(TEXT(".uptnl"))})
    {
        const FString One = Stem + Suffix;
        PackageFiles.Add(One);
        if (StillRight && Files.FileExists(*One))
        {
            TArray<uint8> Bytes;
            if (!FFileHelper::LoadFileToArray(Bytes, *One, FILEREAD_Silent))
            {
                Why = TEXT("save-file-there-is-unreadable: ") + One;
                return false;
            }
            Before.Add(One, MoveTemp(Bytes));
        }
    }
    const auto TakeBack = [&]()
    {
        UPackage::WaitForAsyncFileWrites();
        ResetLoaders(Package);
        bool Back = true;
        for (const FString& One : PackageFiles)
        {
            if (const TArray<uint8>* Bytes = Before.Find(One))
            {
                TArray<uint8> Current;
                const bool AlreadySame = FFileHelper::LoadFileToArray(Current, *One, FILEREAD_Silent) &&
                    Current == *Bytes;
                // A refused pre-write save may leave a readonly file untouched.
                // Do not rewrite that exact existing bundle during rollback.
                if (!AlreadySame)
                    Back = FFileHelper::SaveArrayToFile(*Bytes, *One) && Back;
            }
            else if (Files.FileExists(*One))
                Back = Files.Delete(*One, false, true, true) && Back;
        }
        Package->SetPackageFlagsTo(PreviousFlags);
        Package->FileSize = PreviousFileSize;
        Package->FileName = PreviousFileName;
        Package->SetGuid(PreviousGuid);
#if WITH_EDITORONLY_DATA
        Package->SetPersistentGuid(PreviousPersistentGuid);
#endif
        Package->SetDirtyFlag(PreviousDirty);
        Back = Back && Package->GetPackageFlags() == PreviousFlags && Package->FileSize == PreviousFileSize &&
            Package->FileName == PreviousFileName && Package->GetGuid() == PreviousGuid &&
            Package->IsDirty() == PreviousDirty;
#if WITH_EDITORONLY_DATA
        Back = Back && Package->GetPersistentGuid() == PreviousPersistentGuid;
#endif
        return Back;
    };
    TSet<UObject*> Seen;
    Seen.Add(Asset);
    if (!SaveWhatHasNoFile(Asset, Seen, Why) || !Write(Asset, File, Why))
    {
        if (StillRight && !TakeBack())
            Why += TEXT("; save-failed-and-was-not-taken-back: ") + File;
        return false;
    }
    // Validation and saved-file capture observe the completed native write,
    // including any asynchronous package save effects.
    if (StillRight)
        UPackage::WaitForAsyncFileWrites();
    if (StillRight && !StillRight())
    {
        // Taken back. The engine holds the file it saved open to read from, and lets go of it first.
        const bool Back = TakeBack();
        Why = Back ? TEXT("save-changed-what-the-asset-says")
                   : TEXT("save-changed-what-the-asset-says-and-was-not-taken-back: ") + File;
        return false;
    }
    return true;
}

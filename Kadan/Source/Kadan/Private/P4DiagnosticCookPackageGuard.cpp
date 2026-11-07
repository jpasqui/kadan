#include "P4DiagnosticCookPackageGuard.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

struct FP4DiagnosticCookPackageGuard::FMaterialState
{
    struct FEntry
    {
        TStrongObjectPtr<UMaterialInterface> Material;
        TArray<FMaterialTextureInfo> TextureData;
        bool Sorted = false;
        explicit FEntry(UMaterialInterface* InMaterial) : Material(InMaterial) {}
    };
    TArray<FEntry> Entries;
    FBoolProperty* Sorted = nullptr;
    FArrayProperty* Data = nullptr;

    bool Schema()
    {
        UClass* Owner = UMaterialInterface::StaticClass();
        Sorted = FindFProperty<FBoolProperty>(Owner, TEXT("bTextureStreamingDataSorted"));
        Data = FindFProperty<FArrayProperty>(Owner, TEXT("TextureStreamingData"));
        const FStructProperty* Element = Data ? CastField<FStructProperty>(Data->Inner) : nullptr;
        return Sorted && Sorted->GetOwnerStruct() == Owner && Sorted->ArrayDim == 1 &&
            Sorted->HasAllPropertyFlags(CPF_Transient) &&
            !Sorted->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible | CPF_Config) && Data &&
            Data->GetOwnerStruct() == Owner && Data->ArrayDim == 1 && Element &&
            Element->Struct == FMaterialTextureInfo::StaticStruct() &&
            Element->Struct->GetPathName().Equals(
                TEXT("/Script/Engine.MaterialTextureInfo"), ESearchCase::CaseSensitive) &&
            Element->Struct->GetStructureSize() == int32(sizeof(FMaterialTextureInfo));
    }
    bool Capture(UPackage* Package)
    {
        TArray<UObject*> Objects;
        GetObjectsWithOuter(Package, Objects, true);
        for (UObject* Object : Objects)
            if (UMaterialInterface* Material = Cast<UMaterialInterface>(Object))
            {
                if (!IsValid(Material) || Material->GetOutermost() != Package || (!Sorted && !Schema()) ||
                    Data->ContainerPtrToValuePtr<void>(Material) != &Material->GetTextureStreamingData())
                    return false;
                FEntry Entry(Material);
                Entry.TextureData = Material->GetTextureStreamingData();
                Entry.Sorted = Sorted->GetPropertyValue_InContainer(Material);
                Entries.Add(MoveTemp(Entry));
            }
        return true;
    }
    bool Restore(UPackage* Package)
    {
        if (!Entries.Num())
            return true;
        FBoolProperty* PriorSorted = Sorted;
        FArrayProperty* PriorData = Data;
        if (!Schema() || Sorted != PriorSorted || Data != PriorData)
            return false;
        // Validate every saved target before changing any material. No names,
        // replacement objects or materials outside this cook package qualify.
        for (const auto& Entry : Entries)
            if (!IsValid(Entry.Material.Get()) || Entry.Material->GetOutermost() != Package ||
                Data->ContainerPtrToValuePtr<void>(Entry.Material.Get()) != &Entry.Material->GetTextureStreamingData())
                return false;
        for (const auto& Entry : Entries)
        {
            // SetTextureStreamingData additionally changes version/missing-entry
            // state and sorts again; direct public array assignment is required.
            Entry.Material->GetTextureStreamingData() = Entry.TextureData;
            Sorted->SetPropertyValue_InContainer(Entry.Material.Get(), Entry.Sorted);
        }
        for (const auto& Entry : Entries)
            if (Sorted->GetPropertyValue_InContainer(Entry.Material.Get()) != Entry.Sorted ||
                !Data->Identical(&Entry.Material->GetTextureStreamingData(), &Entry.TextureData, PPF_None))
                return false;
        return true;
    }
};

FP4DiagnosticCookPackageGuard::FP4DiagnosticCookPackageGuard(UPackage* InPackage) : Package(InPackage)
{
    if (!IsInGameThread())
    {
        Error = EP4DiagnosticCookPackageError::EditorThreadRequired;
        ErrorDetail = TEXT("diagnostic-cook-source-editor-thread-required");
        return;
    }
    if (!IsValid(InPackage))
    {
        Error = EP4DiagnosticCookPackageError::PackageRequired;
        ErrorDetail = TEXT("diagnostic-cook-source-package-required");
        return;
    }
    Materials = MakeUnique<FMaterialState>();
    if (!Materials->Capture(InPackage))
    {
        Error = EP4DiagnosticCookPackageError::MaterialStateSchemaUnsupported;
        ErrorDetail = TEXT("diagnostic-cook-material-streaming-schema-unsupported");
        return;
    }
    Flags = InPackage->GetPackageFlags();
    FileSize = InPackage->FileSize;
    FileName = InPackage->FileName;
    Guid = InPackage->GetGuid();
#if WITH_EDITORONLY_DATA
    PersistentGuid = InPackage->GetPersistentGuid();
#endif
    Dirty = InPackage->IsDirty();
    Ready = true;
}

FP4DiagnosticCookPackageGuard::~FP4DiagnosticCookPackageGuard()
{
    if (Ready && !Completed)
    {
        FString Ignored;
        AwaitAndRestore(Ignored);
    }
}

bool FP4DiagnosticCookPackageGuard::AwaitAndRestore(FString& Why)
{
    if (!Ready)
    {
        Why = ErrorDetail;
        return false;
    }
    if (!IsInGameThread())
    {
        Error = EP4DiagnosticCookPackageError::EditorThreadRequired;
        Why = ErrorDetail = TEXT("diagnostic-cook-source-editor-thread-required");
        return false;
    }
    // SAVE_Async can still update package metadata while output is written.
    UPackage::WaitForAsyncFileWrites();
    UPackage* Source = Package.Get();
    const bool MaterialsRestored = Materials->Restore(Source);
    // Package metadata/dirty restoration is last, including when restoration
    // of the known material state is refused.
    Source->SetPackageFlagsTo(Flags);
    Source->FileSize = FileSize;
    Source->FileName = FileName;
    Source->SetGuid(Guid);
#if WITH_EDITORONLY_DATA
    Source->SetPersistentGuid(PersistentGuid);
#endif
    Source->SetDirtyFlag(Dirty);
    bool Same = Source->GetPackageFlags() == Flags && Source->FileSize == FileSize && Source->FileName == FileName &&
        Source->GetGuid() == Guid && Source->IsDirty() == Dirty;
#if WITH_EDITORONLY_DATA
    Same = Same && Source->GetPersistentGuid() == PersistentGuid;
#endif
    if (!Same)
    {
        Error = EP4DiagnosticCookPackageError::MetadataNotRestored;
        Why = ErrorDetail = TEXT("diagnostic-cook-source-package-metadata-not-restored");
        return false;
    }
    if (!MaterialsRestored)
    {
        Error = EP4DiagnosticCookPackageError::MaterialStateNotRestored;
        Why = ErrorDetail = TEXT("diagnostic-cook-material-streaming-state-not-restored");
        return false;
    }
    Completed = true;
    return true;
}

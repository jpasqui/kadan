#include "P4KadanReimportAsset.h"
#include "P4GamePaths.h"
#include "P4ParticleReference.h"
#include "P4OriginalRecord.h"
#include "P4PackageSave.h"
#include "P4KadanReadOnly.h"
#include "AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"

namespace
{
const TCHAR* ParticleClass = TEXT("/Script/Engine.ParticleSystem");
const TCHAR* ReferenceSchema = TEXT("particle-identity-reference-v1");
bool MissingGameModule(const FP4OriginalAssetImportResult& Result)
{
    return Result.Error == EP4OriginalAssetImportError::ClassUnavailable &&
        Result.ErrorDetail ==
        TEXT("asset-import-class-not-in-the-project: /Script/Engine.ParticleModuleLocationPrimitiveC4Sphere");
}

} //namespace

FP4OriginalAssetImportResult P4KeepUnsupportedParticleReference(
    const FP4OriginalAssetImportResult& Refused, bool SaveToDisk)
{
    // An arbitrary import error is not a reason to invent a replacement for a game asset.
    if (!IsInGameThread() || !Refused.AssetIdentityKnown || !Refused.ObjectsKnown || Refused.Class != ParticleClass ||
        !MissingGameModule(Refused) || !P4IsValidContentPackage(Refused.Package))
        return Refused;
    const FString Name = FPackageName::GetLongPackageAssetName(Refused.Package);
    if (!Refused.AssetPath.Equals(Refused.Package + TEXT(".") + Name, ESearchCase::IgnoreCase))
        return Refused;

    FP4OriginalAssetImportResult Result;
    Result.Package = Refused.Package;
    Result.AssetPath = Refused.AssetPath;
    Result.Class = Refused.Class;
    Result.AssetIdentityKnown = true;
    Result.ReferenceOnly = true;
    Result.ValuesNotHeld = {
        TEXT(
            "This particle effect needs a game-only module. Its effect payload remains in the game; the editor keeps only its asset reference. ") +
        Refused.ErrorDetail};
    Result.ValuesNotHeldCount = 1;
    Result.ImportedValuesKnown = true;
    const auto Fail = [&](EP4OriginalAssetImportError Error, const FString& Why)
    {
        Result.Error = Error;
        Result.ErrorDetail = Why;
        return Result;
    };
    UObject* Asset = FindObject<UObject>(nullptr, *Refused.AssetPath);
    const bool OnDisk = !P4KadanOwnsReimportSlot(Refused.Package) && FPackageName::DoesPackageExist(Refused.Package);
    if (!Asset && OnDisk)
        Asset = StaticLoadObject(
            UObject::StaticClass(), nullptr, *Refused.AssetPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
    if (Asset)
    {
        const FP4OriginalStatus Status = P4QueryOriginalAsset(Asset);
        if (!IsValid(Asset) || Asset->GetClass()->GetPathName() != ParticleClass || !Status.Error.IsEmpty() ||
            !Status.IsOriginal() || !Status.Record.ReferenceOnly || !Status.Record.ReadOnly ||
            Status.Record.ComparisonSchema != ReferenceSchema || Asset->GetOutermost()->IsDirty())
            return Fail(EP4OriginalAssetImportError::OccupiedDestination,
                TEXT("particle-reference-destination-holds-another-asset"));
    }
    else
    {
        UPackage* Existing = FindPackage(nullptr, *Refused.Package);
        TArray<UObject*> Inside;
        if (Existing)
            GetObjectsWithOuter(Existing, Inside, false);
        Inside.RemoveAll([](const UObject* Object) { return !IsValid(Object) || Object->IsA<UMetaData>(); });
        if (OnDisk || Inside.Num())
            return Fail(EP4OriginalAssetImportError::OccupiedDestination,
                TEXT("particle-reference-destination-holds-another-asset"));
        UClass* Class = LoadObject<UClass>(nullptr, ParticleClass, nullptr, LOAD_NoWarn | LOAD_Quiet);
        if (!Class || Class->HasAnyClassFlags(CLASS_Abstract))
            return Fail(
                EP4OriginalAssetImportError::ClassUnavailable, TEXT("particle-reference-class-not-in-the-project"));
        Asset = NewObject<UObject>(
            CreatePackage(*Refused.Package), Class, FName(*Name), RF_Public | RF_Standalone | RF_Transactional);
        FP4OriginalRecord Record;
        Record.Package = Refused.Package;
        Record.Class = ParticleClass;
        Record.Kind = TEXT("particle-reference");
        Record.ReferenceOnly = true;
        Record.ReadOnly = true;
        Record.Fidelity = TEXT("identity-only");
        Record.SourceDigestKind = TEXT("not-recovered");
        Record.ComparisonSchema = ReferenceSchema;
        FP4OriginalValueNote Note;
        Note.Kind = TEXT("unsupported-particle-module");
        Note.Object = Refused.AssetPath;
        Note.GameValue = Refused.ErrorDetail;
        Note.Action = TEXT("not-held");
        Record.Values.Add(Note);
        FString Why;
        if (!P4MarkOriginalAsset(Asset, Record, Why))
            return Fail(EP4OriginalAssetImportError::RecordFailed, Why);
        FAssetRegistryModule::AssetCreated(Asset);
        Asset->MarkPackageDirty();
        Result.Created = true;
    }
    Result.Asset = Asset;
    if (SaveToDisk && (Result.Created || !OnDisk))
    {
        FString Why;
        // This checks only the declared identity/role after the ordinary save. It is never a source
        // equality check and deliberately does not fill the native comparison result fields.
        if (!P4SaveAssetPackage(
                Asset,
                [&]()
                {
                    const FP4OriginalStatus Status = P4QueryOriginalAsset(Asset);
                    return Status.Error.IsEmpty() && Status.Record.Package == Refused.Package &&
                        Status.Record.Class == ParticleClass && Status.Record.ReferenceOnly && Status.Record.ReadOnly &&
                        Status.Record.ComparisonSchema == ReferenceSchema;
                },
                Why))
            return Fail(EP4OriginalAssetImportError::SaveFailed, Why);
        Result.Saved = true;
    }
    P4Kadan::UpdateReadOnlyAsset(Asset);
    return Result;
}

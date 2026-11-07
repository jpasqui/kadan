#include "P4KadanReimportAsset.h"
#include "P4GamePaths.h"
#include "P4SoundReference.h"
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
const TCHAR* SoundClass = TEXT("/Script/AkAudio.AkAudioEvent");
const TCHAR* ReferenceSchema = TEXT("sound-identity-reference-v1");
bool MissingEventData(const FP4OriginalAssetImportResult& Result)
{
    return Result.Error == EP4OriginalAssetImportError::CookDiffers &&
        Result.ErrorDetail.Contains(TEXT("class /Script/AkAudio.AkAudioEventData, kind"), ESearchCase::CaseSensitive) &&
        Result.OnlyGameSays.ContainsByPredicate(
            [](const FString& Line)
            {
                return Line.Contains(
                    TEXT(" | class /Script/AkAudio.AkAudioEventData, kind"), ESearchCase::CaseSensitive);
            });
}
} //namespace

FP4OriginalAssetImportResult P4KeepUnsupportedSoundReference(
    const FP4OriginalAssetImportResult& Refused, bool SaveToDisk)
{
    // An arbitrary import error is not a reason to invent a replacement for a game asset.
    if (!IsInGameThread() || !Refused.AssetIdentityKnown || !Refused.ObjectsKnown || Refused.Class != SoundClass ||
        !MissingEventData(Refused) || !P4IsValidContentPackage(Refused.Package))
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
    Result.ValuesNotHeld = {TEXT("The game's sound event payload was not recovered. ") + Refused.ErrorDetail};
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
        if (!IsValid(Asset) || Asset->GetClass()->GetPathName() != SoundClass || !Status.Error.IsEmpty() ||
            !Status.IsOriginal() || !Status.Record.ReferenceOnly || !Status.Record.ReadOnly ||
            Status.Record.ComparisonSchema != ReferenceSchema || Asset->GetOutermost()->IsDirty())
            return Fail(EP4OriginalAssetImportError::OccupiedDestination,
                TEXT("sound-reference-destination-holds-another-asset"));
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
                TEXT("sound-reference-destination-holds-another-asset"));
        UClass* Class = LoadObject<UClass>(nullptr, SoundClass, nullptr, LOAD_NoWarn | LOAD_Quiet);
        if (!Class || Class->HasAnyClassFlags(CLASS_Abstract))
            return Fail(
                EP4OriginalAssetImportError::ClassUnavailable, TEXT("sound-reference-class-not-in-the-project"));
        Asset = NewObject<UObject>(
            CreatePackage(*Refused.Package), Class, FName(*Name), RF_Public | RF_Standalone | RF_Transactional);
        FP4OriginalRecord Record;
        Record.Package = Refused.Package;
        Record.Class = SoundClass;
        Record.Kind = TEXT("sound-reference");
        Record.ReferenceOnly = true;
        Record.ReadOnly = true;
        Record.Fidelity = TEXT("identity-only");
        Record.SourceDigestKind = TEXT("not-recovered");
        Record.ComparisonSchema = ReferenceSchema;
        FP4OriginalValueNote Note;
        Note.Kind = TEXT("unsupported-sound-payload");
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
                        Status.Record.Class == SoundClass && Status.Record.ReferenceOnly && Status.Record.ReadOnly &&
                        Status.Record.ComparisonSchema == ReferenceSchema;
                },
                Why))
            return Fail(EP4OriginalAssetImportError::SaveFailed, Why);
        Result.Saved = true;
    }
    P4Kadan::UpdateReadOnlyAsset(Asset);
    return Result;
}

#include "P4AnimationDependencyRetention.h"
#include "P4GamePaths.h"
#include "P4KadanImport.h"
#include "P4CreatorSource.h"
#include "P4OriginalRecord.h"
#include "P4OriginalAnimationData.h"
#include "Animation/AnimSequence.h"
#include "Misc/PackageName.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"

bool P4RetainUnchangedAnimationDependency(
    const FP4KadanImportPlan& Plan, const FP4KadanSourceAsset& Source, UObject*& Existing, FString& Error)
{
    Existing = nullptr;
    Error.Reset();
    if (!IsInGameThread() || Source.Kind != TEXT("AnimSequence") || !Plan.RequestedPackages.Num() ||
        Plan.RequestedPackages.ContainsByPredicate(
            [&](const FString& Root) { return FName(*Root) == FName(*Source.Package); }) ||
        !FPackageName::DoesPackageExist(Source.Package))
        return false;
    for (const FString& Root : Plan.RequestedPackages)
        if (!Plan.Scanned.ContainsByPredicate([&](const TSharedPtr<FP4KadanSourceAsset>& Row)
                { return Row.IsValid() && FName(*Row->Package) == FName(*Root) && Row->Kind == TEXT("World"); }))
            return false;
    const FString Path = P4ObjectPathOf(Source.Package);
    auto* Sequence = LoadObject<UAnimSequence>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
    if (!IsValid(Sequence) || Sequence->GetClass() != UAnimSequence::StaticClass() || Sequence->GetPathName() != Path ||
        Sequence->HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad | RF_NeedPostLoadSubobjects) ||
        Sequence->GetOutermost()->IsDirty() || P4HasCreatorSource(Sequence) || P4HasCopiedGameData(Sequence) ||
        P4HasIntrinsicGameProxy(Sequence) || P4IsVerifiedStandIn(Sequence))
        return false;
    const auto* Cache = Cast<UP4OriginalAnimationData>(Sequence->AssetImportData);
    if (!Cache || Cache->CacheBytes.Num() == 0 || Cache->CacheSuffix.IsEmpty() ||
        Cache->CacheSuffix != Sequence->GetDDCCacheKeySuffix(false) || !Sequence->IsCompressedDataValid())
        return false;
    TArray<UObject*> Roots;
    GetObjectsWithOuter(Sequence->GetOutermost(), Roots, false);
    for (UObject* Root : Roots)
        if (IsValid(Root) && Root != Sequence && !Root->IsA<UMetaData>())
            return false;
    FP4OriginalRecord Record;
    if (!P4ReadOriginalRecord(Sequence, Record, Error) || Record.Version != 2 || !Record.ReadOnly ||
        Record.Package != Source.Package || Record.Class != TEXT("/Script/Engine.AnimSequence") ||
        Record.Kind != TEXT("object") || Record.ReferenceOnly ||
        Record.ComparisonSchema != TEXT("animation-object-lines-v2") ||
        Record.SourceDigestKind != TEXT("header-body-sha256-v1"))
        return false;
    FString Digest, Class;
    if (!P4KadanRequestedSourceDigest(Source, Digest, Error, &Class) || Class != Record.Class ||
        Digest != Record.SourceDigest)
        return false;
    const FString Marker = Sequence->GetOutermost()->GetMetaData()->GetValue(Sequence, TEXT("P4OriginalRecord"));
    const auto Current = P4DiagnoseOriginalAsset(Sequence);
    if (!Current.Error.IsEmpty() || Current.Comparison != EP4OriginalComparison::Equal ||
        Current.CurrentDigest != Record.ComparisonDigest || Sequence->GetOutermost()->IsDirty() ||
        Cache != Sequence->AssetImportData || Cache->CacheSuffix != Sequence->GetDDCCacheKeySuffix(false) ||
        Marker != Sequence->GetOutermost()->GetMetaData()->GetValue(Sequence, TEXT("P4OriginalRecord")))
        return false;
    FString Confirmed;
    if (!P4KadanRequestedSourceDigest(Source, Confirmed, Error) || Confirmed != Digest)
        return false;
    Existing = Sequence;
    return true;
}

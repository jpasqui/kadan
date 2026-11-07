#include "P4MaterialDependencyRetention.h"
#include "P4Common.h"
#include "P4GamePaths.h"
#include "P4KadanImport.h"
#include "P4KadanImportSources.h"
#include "P4CreatorSource.h"
#include "P4NativeHash.h"
#include "P4OriginalRecord.h"
#include "P4MaterialRecovery.h"
#include "Materials/Material.h"
#include "Dom/JsonObject.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
THIRD_PARTY_INCLUDES_START
#include "P4MapKitCore/material_collection_source.h"
THIRD_PARTY_INCLUDES_END

bool P4RetainUnchangedMaterialCollectionDependency(
    const FP4KadanImportPlan& Plan, const FP4KadanSourceAsset& Source, UObject*& Existing, FString& Error)
{
    Existing = nullptr;
    Error.Reset();
    if (!IsInGameThread() || !Plan.bMaterialExecutionDeferred ||
        !P4ExactNativeString(Source.Kind, TEXT("MaterialParameterCollection")) || !Plan.RequestedPackages.Num() ||
        Plan.RequestedPackages.ContainsByPredicate(
            [&](const FString& Root) { return FName(*Root) == FName(*Source.Package); }) ||
        !FPackageName::DoesPackageExist(Source.Package))
        return false;
    const auto Refuse = [&](const TCHAR* Why)
    {
        Error = Why;
        return false;
    };
    const FString Path = P4ObjectPathOf(Source.Package);
    UObject* Asset = LoadObject<UObject>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
    if (!IsValid(Asset) || Asset->GetClass() != UMaterialParameterCollection::StaticClass() ||
        Asset->HasAnyFlags(
            RF_ClassDefaultObject | RF_ArchetypeObject | RF_NeedLoad | RF_NeedPostLoad | RF_NeedPostLoadSubobjects) ||
        !P4ExactNativeString(Asset->GetPathName(), Path) || Asset->GetOutermost()->IsDirty() ||
        P4HasCreatorSource(Asset) || P4HasCopiedGameData(Asset) || P4HasIntrinsicGameProxy(Asset) ||
        P4IsVerifiedStandIn(Asset))
        return Refuse(TEXT("Material collection dependency has no clean original native object"));
    // Stock MPC is UObject, without IInterface_AssetUserData. Its complete
    // original record below proves source identity; intrinsic creator/game-role
    // data cannot be required for this native class. Explicit copies and proxies
    // are rejected above, and another package's retained record fails below.
    TArray<UObject*> Roots;
    GetObjectsWithOuter(Asset->GetOutermost(), Roots, false);
    for (UObject* Root : Roots)
        if (IsValid(Root) && Root != Asset && !Root->IsA<UMetaData>())
            return Refuse(TEXT("Material collection dependency has another package root"));
    FP4OriginalRecord Record;
    if (!P4ReadOriginalRecord(Asset, Record, Error))
        return false;
    if (Record.Version != 2 || !P4ExactNativeString(Record.Package, Source.Package) ||
        !P4ExactNativeString(Record.Class, TEXT("/Script/Engine.MaterialParameterCollection")) ||
        !P4ExactNativeString(Record.Kind, TEXT("object")) || !P4ExactNativeString(Record.Fidelity, TEXT("exact")) ||
        !Record.ReadOnly || Record.ReferenceOnly || Record.Values.Num() ||
        !P4ExactNativeString(Record.ComparisonSchema, TEXT("object-lines-v1")) ||
        !P4ExactNativeString(Record.SourceDigestKind, TEXT("header-body-sha256-v1")))
        return Refuse(TEXT("Material collection dependency original record is incomplete"));
    const auto Row = MakeShared<FP4KadanSourceAsset>(Source);
    FP4KadanImportSourceScope Scope({Row});
    FP4KadanSourceBinding Binding;
    if (!P4KadanReadBoundSource(Source.Package, Binding, Error) || !P4KadanSourceBindingCurrent(Binding, Error))
        return false;
    if (!P4ExactNativeString(Record.SourceDigest, Binding.SourceDigest))
        return Refuse(TEXT("Material collection dependency source SHA differs"));
    try
    {
        // Typed canonical root, complete parameter schemas and native suffix;
        // no synthetic shader request or renderer authority is introduced.
        P4MapKit::Material::ReadMaterialCollectionSource(
            std::vector<uint8>(Binding.Header.GetData(), Binding.Header.GetData() + Binding.Header.Num()),
            std::vector<uint8>(Binding.Body.GetData(), Binding.Body.GetData() + Binding.Body.Num()),
            TCHAR_TO_UTF8(*Source.Package));
    }
    catch (const std::bad_alloc&)
    {
        throw;
    }
    catch (const std::exception& Why)
    {
        Error = TEXT("Material collection dependency source is incomplete: ") + FString(UTF8_TO_TCHAR(Why.what()));
        return false;
    }
    UMetaData* Meta = Asset->GetOutermost()->GetMetaData();
    const FString Marker = Meta->GetValue(Asset, TEXT("P4OriginalRecord"));
    TSharedPtr<FJsonObject> RecordJson;
    const TArray<TSharedPtr<FJsonValue>>* RecordedValues = nullptr;
    if (!P4ParseJson(Marker, RecordJson) || !RecordJson->TryGetArrayField(TEXT("values"), RecordedValues) ||
        RecordedValues->Num())
        return Refuse(TEXT("Material collection dependency original value ledger is incomplete"));
    if (!FString(Meta->GetValue(Asset, TEXT("P4NativeObjectValuesNotHeld"))).IsEmpty())
        return Refuse(TEXT("Material collection dependency has unsupported source notes"));
    const FString NativeBaseline = Meta->GetValue(Asset, TEXT("P4NativeObjectBaseline"));
    TSharedPtr<FJsonObject> Baseline;
    double Format = 0, Bytes = -1, Values = -1;
    FString Owner, Package, Digest, State;
    if (!P4ParseJson(NativeBaseline, Baseline) || !Baseline->TryGetNumberField(TEXT("format"), Format) || Format != 1 ||
        !Baseline->TryGetStringField(TEXT("owner"), Owner) ||
        !P4ExactNativeString(Owner, TEXT("p4mapkit-original-object-v1")) ||
        !Baseline->TryGetStringField(TEXT("package"), Package) || !P4ExactNativeString(Package, Source.Package) ||
        !Baseline->TryGetStringField(TEXT("source_sha256"), Digest) ||
        !P4ExactNativeString(Digest, Binding.SourceDigest) || !Baseline->TryGetStringField(TEXT("state"), State) ||
        State.Len() != 64 || !P4ExactNativeString(State, Record.ComparisonDigest) ||
        !Baseline->TryGetNumberField(TEXT("reference_only_bytes"), Bytes) || Bytes != 0 ||
        !Baseline->TryGetNumberField(TEXT("reference_only_values"), Values) || Values != 0)
        return Refuse(TEXT("Material collection dependency native baseline is incomplete"));
    // The generic native importer deliberately retains its complete original
    // baseline when it records v2 provenance. Partial or unrelated history is
    // not evidence; both omitted fields are also valid for a direct v2 record.
    if ((!Record.LegacyKey.IsEmpty() || !Record.LegacyEvidence.IsEmpty()) &&
        (!P4ExactNativeString(Record.LegacyKey, TEXT("P4NativeObjectBaseline")) ||
            !P4ExactNativeString(Record.LegacyEvidence, NativeBaseline)))
        return Refuse(TEXT("Material collection dependency legacy evidence differs"));
    for (TCHAR C : State)
        if (!((C >= TEXT('0') && C <= TEXT('9')) || (C >= TEXT('a') && C <= TEXT('f'))))
            return Refuse(TEXT("Material collection dependency native digest is invalid"));
    const auto Actual = P4DiagnoseOriginalAsset(Asset);
    if (!Actual.Error.IsEmpty() || Actual.Comparison != EP4OriginalComparison::Equal ||
        !P4ExactNativeString(Actual.CurrentDigest, Record.ComparisonDigest))
        return Refuse(TEXT("Material collection dependency current native content differs"));
    if (Asset->GetOutermost()->IsDirty() ||
        !P4ExactNativeString(Marker, Meta->GetValue(Asset, TEXT("P4OriginalRecord"))) ||
        !P4ExactNativeString(NativeBaseline, Meta->GetValue(Asset, TEXT("P4NativeObjectBaseline"))) ||
        !P4KadanSourceBindingCurrent(Binding, Error))
    {
        if (Error.IsEmpty())
            Error = TEXT("Material collection dependency custody changed during comparison");
        return false;
    }
    Existing = Asset;
    return true;
}

bool P4RetainUnchangedRecoveredMaterialDependency(const FP4KadanImportPlan& Plan, const FP4KadanSourceAsset& Source,
    const FString& CurrentRecipeMd5, const TFunction<bool(FString&)>& SourceCurrent, UObject*& Existing, FString& Error)
{
    Existing = nullptr;
    Error.Reset();
    if (!IsInGameThread() || !Plan.bMaterialExecutionDeferred || !P4ExactNativeString(Source.Kind, TEXT("Material")) ||
        !Plan.RequestedPackages.Num() ||
        Plan.RequestedPackages.ContainsByPredicate(
            [&](const FString& Root) { return FName(*Root) == FName(*Source.Package); }) ||
        !FPackageName::DoesPackageExist(Source.Package))
        return false;
    const auto Refuse = [&](const TCHAR* Why)
    {
        Error = Why;
        return false;
    };
    if (!SourceCurrent || !SourceCurrent(Error))
    {
        if (Error.IsEmpty())
            Error = TEXT("Recovered material dependency has no current complete family");
        return false;
    }
    const FString Path = P4ObjectPathOf(Source.Package);
    UObject* Asset = LoadObject<UObject>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
    FString Origin;
    if (!IsValid(Asset) || Asset->GetClass() != UMaterial::StaticClass() ||
        Asset->HasAnyFlags(
            RF_ClassDefaultObject | RF_ArchetypeObject | RF_NeedLoad | RF_NeedPostLoad | RF_NeedPostLoadSubobjects) ||
        !P4ExactNativeString(Asset->GetPathName(), Path) || Asset->GetOutermost()->IsDirty() ||
        P4HasCreatorSource(Asset) || P4HasCopiedGameData(Asset) || P4HasIntrinsicGameProxy(Asset) ||
        P4IsVerifiedStandIn(Asset) || !P4HasIntrinsicGameSource(Asset, &Origin) ||
        !P4ExactNativeString(Origin, Source.Package))
        return Refuse(TEXT("Recovered material dependency has no clean original native owner"));
    TArray<UObject*> Roots;
    GetObjectsWithOuter(Asset->GetOutermost(), Roots, false);
    for (UObject* Root : Roots)
        if (IsValid(Root) && Root != Asset && !Root->IsA<UMetaData>())
            return Refuse(TEXT("Recovered material dependency has another package root"));
    FP4OriginalRecord Record;
    if (!P4ReadOriginalRecord(Asset, Record, Error))
        return false;
    if (Record.Version != 2 || !P4ExactNativeString(Record.Package, Source.Package) ||
        !P4ExactNativeString(Record.Class, TEXT("/Script/Engine.Material")) ||
        !P4ExactNativeString(Record.Kind, TEXT("material")) ||
        !P4ExactNativeString(Record.Fidelity, TEXT("recovered-pixel-contract")) || !Record.ReadOnly ||
        !Record.ReferenceOnly || !P4ExactNativeString(Record.ComparisonSchema, TEXT("material-native-state-v1")) ||
        !P4ExactNativeString(Record.SourceDigestKind, TEXT("material-graph-v2-md5")))
        return Refuse(TEXT("Recovered material dependency original record is incomplete"));
    UMetaData* Meta = Asset->GetOutermost()->GetMetaData();
    const FString Marker = Meta->GetValue(Asset, TEXT("P4OriginalRecord"));
    TSharedPtr<FJsonObject> RecordJson;
    const TArray<TSharedPtr<FJsonValue>>* Notes = nullptr;
    if (!P4ParseJson(Marker, RecordJson) || !RecordJson->TryGetArrayField(TEXT("values"), Notes))
        return Refuse(TEXT("Recovered material dependency value ledger is incomplete"));
    // Material render-setting notes are part of its existing recovered contract;
    // unlike a complete raw collection, a recovered master can retain them.
    if (CurrentRecipeMd5.Len() != 32 || !P4ExactNativeString(CurrentRecipeMd5, Record.SourceDigest))
        return Refuse(TEXT("Recovered material dependency incoming recipe differs"));
    const auto Actual = P4DiagnoseOriginalAsset(Asset);
    if (!Actual.Error.IsEmpty() || Actual.Comparison != EP4OriginalComparison::Equal ||
        !P4ExactNativeString(Actual.CurrentDigest, Record.ComparisonDigest))
        return Refuse(TEXT("Recovered material dependency current native content differs"));
    // Both the actual execution descriptor and the complete family's current
    // raw binding must name identical incoming bytes. Keep the full family
    // scope active for the independent historical recipe/read witness below.
    FP4KadanSourceBinding Incoming, Family;
    {
        FP4KadanImportSourceScope Prepared({MakeShared<FP4KadanSourceAsset>(Source)});
        if (!P4KadanReadBoundSource(Source.Package, Incoming, Error) || !P4KadanSourceBindingCurrent(Incoming, Error))
            return false;
    }
    if (!P4KadanReadBoundSource(Source.Package, Family, Error) || !P4KadanSourceBindingCurrent(Family, Error))
        return false;
    if (!P4ExactNativeString(Incoming.SourceDigest, Family.SourceDigest))
        return Refuse(TEXT("Recovered material dependency prepared source differs from family"));
    const auto Inventory = P4MapKit::ReadPackage(
        std::vector<uint8>(Family.Header.GetData(), Family.Header.GetData() + Family.Header.Num()),
        std::vector<uint8>(Family.Body.GetData(), Family.Body.GetData() + Family.Body.Num()));
    std::string NativeClass, Name;
    if (!Inventory.Error.Code.empty() || Inventory.Package != TCHAR_TO_UTF8(*Source.Package) ||
        Inventory.Exports.empty() || Inventory.Exports[0].Outer ||
        !Inventory.Reference(Inventory.Exports[0].Class, NativeClass) || NativeClass != "/Script/Engine.Material" ||
        !Inventory.Reference(1, Name) || std::string(TCHAR_TO_UTF8(*Path)) != Inventory.Package + "." + Name)
        return Refuse(TEXT("Recovered material dependency raw class or owner differs"));
    if (!P4CurrentRecoveredMaterialParentRecipe(Source.Package, Family.SourceDigest, Record.SourceDigest, Error))
        return false;
    if (!SourceCurrent(Error) || !P4KadanSourceBindingCurrent(Incoming, Error) ||
        !P4KadanSourceBindingCurrent(Family, Error) || Asset->GetOutermost()->IsDirty() ||
        !P4ExactNativeString(Marker, Meta->GetValue(Asset, TEXT("P4OriginalRecord"))))
    {
        if (Error.IsEmpty())
            Error = TEXT("Recovered material dependency custody changed during comparison");
        return false;
    }
    const auto Confirmed = P4DiagnoseOriginalAsset(Asset);
    if (!Confirmed.Error.IsEmpty() || Confirmed.Comparison != EP4OriginalComparison::Equal ||
        !P4ExactNativeString(Confirmed.CurrentDigest, Actual.CurrentDigest) || Asset->GetOutermost()->IsDirty() ||
        !P4ExactNativeString(Marker, Meta->GetValue(Asset, TEXT("P4OriginalRecord"))))
        return Refuse(TEXT("Recovered material dependency native state changed during source verification"));
    Existing = Asset;
    return true;
}

#include "P4OriginalRecord.h"
#include "P4Common.h"
#include "P4CreatorSource.h"
#include "P4KadanReadOnly.h"
// Internal reconstruction boundary. Genuine game import never inherits creator origin.
void P4ForgetCreatorSourceDuringGameImport(UObject* Asset);
bool P4RecordKnownGameSource(UObject* Asset, const FString& OriginalPackage, bool Proxy);
#include "Dom/JsonObject.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
namespace
{
const TCHAR* MarkerKey = TEXT("P4OriginalRecord");
bool Hex(const FString& Value)
{
    if (Value.Len() != 64)
        return false;
    for (TCHAR C : Value)
        if (!(C >= TEXT('0') && C <= TEXT('9')) && !(C >= TEXT('a') && C <= TEXT('f')))
            return false;
    return true;
}
TSharedRef<FJsonObject> JsonOf(const FP4OriginalRecord& R)
{
    auto J = MakeShared<FJsonObject>();
    J->SetNumberField(TEXT("format"), 2);
    J->SetStringField(TEXT("owner"), TEXT("p4mapkit-original"));
    J->SetStringField(TEXT("package"), R.Package);
    J->SetStringField(TEXT("class"), R.Class);
    J->SetStringField(TEXT("kind"), R.Kind);
    J->SetStringField(TEXT("source_digest"), R.SourceDigest);
    J->SetStringField(TEXT("source_digest_kind"), R.SourceDigestKind);
    J->SetStringField(TEXT("comparison_digest"), R.ComparisonDigest);
    J->SetStringField(TEXT("comparison_schema"), R.ComparisonSchema);
    J->SetStringField(TEXT("fidelity"), R.Fidelity);
    J->SetBoolField(TEXT("reference_only"), R.ReferenceOnly);
    J->SetBoolField(TEXT("read_only"), R.ReadOnly);
    J->SetStringField(TEXT("legacy_key"), R.LegacyKey);
    J->SetStringField(TEXT("legacy_evidence"), R.LegacyEvidence);
    TArray<TSharedPtr<FJsonValue>> Values;
    for (const auto& N : R.Values)
    {
        auto V = MakeShared<FJsonObject>();
        V->SetStringField(TEXT("kind"), N.Kind);
        V->SetStringField(TEXT("object"), N.Object);
        V->SetStringField(TEXT("property"), N.Property);
        V->SetStringField(TEXT("game_value"), N.GameValue);
        V->SetStringField(TEXT("action"), N.Action);
        Values.Add(MakeShared<FJsonValueObject>(V));
    }
    J->SetArrayField(TEXT("values"), Values);
    return J;
}
FString TextOf(const FP4OriginalRecord& R)
{
    FString Text = P4CondensedJson(JsonOf(R));
    return Text;
}
bool Parse(const FString& Text, FP4OriginalRecord& R, FString& Error)
{
    TSharedPtr<FJsonObject> J;
    double Version = 0;
    FString Owner;
    if (!P4ParseJson(Text, J))
    {
        Error = TEXT("original-record-unreadable");
        return false;
    }
    if (!J->TryGetNumberField(TEXT("format"), Version) || Version != 2 || !J->TryGetStringField(TEXT("owner"), Owner) ||
        Owner != TEXT("p4mapkit-original"))
    {
        Error = TEXT("original-record-version-unsupported");
        return false;
    }
    if (!J->TryGetStringField(TEXT("package"), R.Package) || !J->TryGetStringField(TEXT("class"), R.Class) ||
        !J->TryGetStringField(TEXT("kind"), R.Kind) || !J->TryGetStringField(TEXT("source_digest"), R.SourceDigest) ||
        !J->TryGetStringField(TEXT("source_digest_kind"), R.SourceDigestKind) ||
        !J->TryGetStringField(TEXT("comparison_digest"), R.ComparisonDigest) ||
        !J->TryGetStringField(TEXT("comparison_schema"), R.ComparisonSchema) ||
        !J->TryGetStringField(TEXT("fidelity"), R.Fidelity) ||
        !J->TryGetBoolField(TEXT("reference_only"), R.ReferenceOnly) ||
        !J->TryGetBoolField(TEXT("read_only"), R.ReadOnly))
    {
        Error = TEXT("original-record-fields-unreadable");
        return false;
    }
    J->TryGetStringField(TEXT("legacy_key"), R.LegacyKey);
    J->TryGetStringField(TEXT("legacy_evidence"), R.LegacyEvidence);
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (J->TryGetArrayField(TEXT("values"), Values))
        for (const auto& Value : *Values)
        {
            const TSharedPtr<FJsonObject>* V = nullptr;
            FP4OriginalValueNote N;
            if (!Value->TryGetObject(V) || !(*V)->TryGetStringField(TEXT("kind"), N.Kind) ||
                !(*V)->TryGetStringField(TEXT("object"), N.Object) ||
                !(*V)->TryGetStringField(TEXT("property"), N.Property) ||
                !(*V)->TryGetStringField(TEXT("game_value"), N.GameValue) ||
                !(*V)->TryGetStringField(TEXT("action"), N.Action))
            {
                Error = TEXT("original-record-value-note-unreadable");
                return false;
            }
            R.Values.Add(N);
        }
    return true;
}
bool Legacy(UObject* Asset, FP4OriginalRecord& R, FString& Error)
{
    struct FEntry
    {
        const TCHAR* Key;
        const TCHAR* Owner;
        const TCHAR* Kind;
        const TCHAR* Schema;
        const TCHAR* Source;
    };
    const FEntry Entries[] = {{TEXT("P4NativeLevelBaseline"), TEXT("p4mapkit-original-level-v1"), TEXT("level"),
                                  TEXT("level-lines-v1"), TEXT("header-body-sha256-v1")},
        {TEXT("P4NativeObjectBaseline"), TEXT("p4mapkit-original-object-v1"), TEXT("object"), TEXT("object-lines-v1"),
            TEXT("header-body-sha256-v1")},
        {TEXT("P4NativeMeshBaseline"), TEXT("p4mapkit-original-mesh-v1"), TEXT("static-mesh"), TEXT("mesh-state-v1"),
            TEXT("mesh-source-v1")},
        {TEXT("P4NativeTextureBaseline"), TEXT("p4mapkit-original-texture-v1"), TEXT("texture"),
            TEXT("texture-state-v1"), TEXT("texture-source-v1")}};
    UMetaData* M = Asset->GetOutermost()->GetMetaData();
    for (const auto& Entry : Entries)
    {
        FString Text = M->GetValue(Asset, Entry.Key);
        if (Text.IsEmpty())
            continue;
        TSharedPtr<FJsonObject> J;
        double Version = 0;
        FString Owner;
        R.Package = Asset->GetOutermost()->GetName();
        R.Class = Asset->GetClass()->GetPathName();
        R.Kind = Entry.Kind;
        R.Version = 1;
        R.LegacyKey = Entry.Key;
        R.LegacyEvidence = Text;
        R.ComparisonSchema = Entry.Schema;
        R.SourceDigestKind = Entry.Source;
        if (!P4ParseJson(Text, J))
        {
            Error = TEXT("original-record-legacy-unreadable");
            return false;
        }
        FString Package;
        if (!J->TryGetNumberField(TEXT("format"), Version) || Version != 1 ||
            !J->TryGetStringField(TEXT("owner"), Owner) || Owner != Entry.Owner)
        {
            Error = TEXT("original-record-version-unsupported");
            return false;
        }
        if (!J->TryGetStringField(TEXT("package"), Package) || Package != R.Package ||
            !J->TryGetStringField(TEXT("source_sha256"), R.SourceDigest) ||
            !J->TryGetStringField(TEXT("state"), R.ComparisonDigest) || !Hex(R.SourceDigest) ||
            !Hex(R.ComparisonDigest))
        {
            Error = TEXT("original-record-legacy-fields-unreadable");
            return false;
        }
        double Bytes = 0, Values = 0;
        J->TryGetNumberField(TEXT("reference_only_bytes"), Bytes);
        J->TryGetNumberField(TEXT("reference_only_values"), Values);
        R.ReferenceOnly = Bytes > 0 || Values > 0;
        J->TryGetStringField(TEXT("equal"), R.Fidelity);
        return true;
    }
    if (FString(M->GetValue(Asset, TEXT("P4BlueprintStandIn"))) == TEXT("1"))
    {
        R.Package = Asset->GetOutermost()->GetName();
        R.Class = Asset->GetClass()->GetPathName();
        R.Kind = TEXT("placeholder");
        R.ComparisonSchema = TEXT("stand-in-v1");
        R.Fidelity = TEXT("stand-in");
        R.ReferenceOnly = true;
        R.ReadOnly = true;
        R.Version = 1;
        return true;
    }
    return false;
}
} //namespace
bool P4ReadOriginalRecord(UObject* Asset, FP4OriginalRecord& Out, FString& Error)
{
    Error.Reset();
    Out = FP4OriginalRecord();
    if (!IsInGameThread() || !IsValid(Asset))
    {
        Error = TEXT("original-record-needs-editor-thread-and-asset");
        return false;
    }
    const FString Marker = Asset->GetOutermost()->GetMetaData()->GetValue(Asset, MarkerKey);
    if (Marker.IsEmpty())
        return Legacy(Asset, Out, Error);
    if (!Parse(Marker, Out, Error))
        return false;
    if (Out.Package != Asset->GetOutermost()->GetName() || Out.Class != Asset->GetClass()->GetPathName())
    {
        Error = TEXT("original-record-asset-identity-differs");
        return false;
    }
    return true;
}
FP4OriginalStatus P4QueryOriginalAsset(UObject* Asset)
{
    FP4OriginalStatus S;
    if (!P4ReadOriginalRecord(Asset, S.Record, S.Error))
    {
        if (!S.Error.IsEmpty())
            S.State = S.Error == TEXT("original-record-version-unsupported") ? EP4OriginalState::UnsupportedRecord
                                                                             : EP4OriginalState::Unreadable;
        return S;
    }
    // Metadata records where the asset came from. Content equality is an explicit semantic comparison.
    S.State = Asset->GetOutermost()->IsDirty() ? EP4OriginalState::Dirty : EP4OriginalState::Recorded;
    return S;
}
bool P4MarkOriginalAsset(UObject* Asset, const FP4OriginalRecord& Record, FString& Error)
{
    Error.Reset();
    if (!IsInGameThread() || !IsValid(Asset) || Record.Package != Asset->GetOutermost()->GetName() ||
        Record.Class != Asset->GetClass()->GetPathName())
    {
        Error = TEXT("original-record-asset-identity-differs");
        return false;
    }
    P4ForgetCreatorSourceDuringGameImport(Asset);
    Asset->GetOutermost()->GetMetaData()->SetValue(Asset, MarkerKey, *TextOf(Record));
    if (!P4HasCreatorSource(Asset))
    {
        const bool Proxy = FString(Asset->GetOutermost()->MetaData->GetValue(Asset, TEXT("P4BlueprintStandIn"))) ==
            TEXT("1");
        P4RecordKnownGameSource(Asset, Record.Package, Proxy);
    }
    P4Kadan::UpdateReadOnlyAsset(Asset);
    return true;
}
bool P4CaptureOriginalRecord(UObject* Asset, FP4OriginalRecord Record, FString& Error)
{
    Error.Reset();
    if (!IsValid(Asset) || Record.Package != Asset->GetOutermost()->GetName() ||
        Record.Class != Asset->GetClass()->GetPathName())
    {
        Error = TEXT("original-record-asset-identity-differs");
        return false;
    }
    return true;
}
bool P4CaptureValidatedOriginal(UObject*, FString& Error)
{
    Error.Reset();
    return true;
}
bool P4MarkLegacyOriginalAsset(
    UObject* Asset, const TArray<FP4OriginalValueNote>& Notes, FString& Error, const FString& ComparisonSchema)
{
    FP4OriginalRecord Record;
    if (!Legacy(Asset, Record, Error))
        return false;
    Record.Version = 2;
    Record.Values = Notes;
    // Every game asset is read-only except levels and data tables, which creators edit.
    Record.ReadOnly = !Asset->IsA<UWorld>() && !Asset->IsA<UDataTable>();
    if (!ComparisonSchema.IsEmpty())
        Record.ComparisonSchema = ComparisonSchema;
    return P4MarkOriginalAsset(Asset, Record, Error);
}
bool P4IsVerifiedStandIn(UObject* Asset)
{
    return IsValid(Asset) &&
        FString(Asset->GetOutermost()->GetMetaData()->GetValue(Asset, TEXT("P4BlueprintStandIn"))) == TEXT("1") &&
        !Asset->GetOutermost()->IsDirty();
}
bool P4HasCopiedGameData(const UObject* Asset)
{
    if (!IsInGameThread() || !IsValid(Asset) || P4HasCreatorSource(Asset))
        return false;
    FString SourcePackage;
    if (P4HasIntrinsicGameSource(Asset, &SourcePackage) && SourcePackage != Asset->GetOutermost()->GetName())
        return true;
    UMetaData* Meta = Asset->GetOutermost()->MetaData;
    return Meta && !Meta->HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad) &&
        Meta->HasValue(Asset, TEXT("P4KadanCopiedGameAsset"));
}
bool P4MigrateOriginalRecord(UObject* Asset, const TFunction<bool()>& MatchesLegacy, FString& Error)
{
    Error.Reset();
    if (!IsValid(Asset) || Asset->GetOutermost()->IsDirty())
    {
        Error = TEXT("save-your-changes-before-importing");
        return false;
    }
    if (!MatchesLegacy || !MatchesLegacy())
    {
        Error = TEXT("original-content-differs");
        return false;
    }
    return true;
}
void P4OriginalStatusFields(const FP4OriginalStatus& S, FJsonObject& Out)
{
    const TCHAR* Names[] = {TEXT("not-imported"), TEXT("unchanged"), TEXT("dirty"), TEXT("saved-edit"),
        TEXT("needs-migration"), TEXT("unreadable"), TEXT("unsupported-record"), TEXT("recorded")};
    Out.SetBoolField(TEXT("original"), S.IsOriginal());
    Out.SetBoolField(TEXT("unchanged"), S.IsUnchanged());
    Out.SetStringField(TEXT("status"), Names[int32(S.State)]);
    Out.SetBoolField(TEXT("migration_required"), false);
    Out.SetBoolField(TEXT("edited"), S.State == EP4OriginalState::Dirty || S.State == EP4OriginalState::SavedEdit);
    Out.SetStringField(TEXT("source_digest"), S.Record.SourceDigest);
    Out.SetStringField(TEXT("source_digest_kind"), S.Record.SourceDigestKind);
    if (S.Record.SourceDigest.Len() == 64)
        Out.SetStringField(TEXT("source_sha256"), S.Record.SourceDigest);
    Out.SetStringField(TEXT("imported_state"), S.Record.ComparisonDigest);
    Out.SetStringField(TEXT("state"), S.Record.ComparisonDigest);
    Out.SetStringField(TEXT("state_kind"), S.Record.ComparisonSchema);
    Out.SetNumberField(TEXT("record_version"), S.Record.Version);
    Out.SetStringField(TEXT("comparison_schema"), S.Record.ComparisonSchema);
    Out.SetStringField(TEXT("fidelity"), S.Record.Fidelity);
    Out.SetBoolField(TEXT("reference_only"), S.Record.ReferenceOnly);
    Out.SetBoolField(TEXT("read_only"), S.Record.ReadOnly);
}

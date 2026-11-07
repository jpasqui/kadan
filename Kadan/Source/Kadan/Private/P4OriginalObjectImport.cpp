// Importing a game asset that is properties only, and the record it keeps of the game's file.

#include "P4LevelContentInternal.h"
#include "P4OriginalParticlePreflight.h"
#include "Particles/ParticleSystem.h"
#include "P4Common.h"
#include "P4GamePaths.h"

namespace P4LevelContent
{
// ----- An asset that is properties only: a data asset, a curve, a set of environment values. -----
// It is brought over the way a level's objects are: made of its class, given what the game's file says
// on top of what the class has, saved the way a cook saves it and compared with the game's file.

const TCHAR* const ObjectBaselineKey = TEXT("P4NativeObjectBaseline");

const TCHAR* const ObjectBaselineOwner = TEXT("p4mapkit-original-object-v1");

// The values of the game's file that this engine has no place for, one to a line, kept with a skeleton,
// a skinned mesh or a sequence: members the game's engine adds to the engine's own structs.
const TCHAR* const ObjectNotHeldKey = TEXT("P4NativeObjectValuesNotHeld");

FString ObjectBaselineText(const FBaseline& Value)
{
    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetNumberField(TEXT("format"), 1);
    Json->SetStringField(TEXT("owner"), ObjectBaselineOwner);
    Json->SetStringField(TEXT("package"), Value.Package);
    Json->SetStringField(TEXT("source_sha256"), Value.Source);
    Json->SetStringField(TEXT("state"), Value.State);
    Json->SetNumberField(TEXT("reference_only_bytes"), Value.ReferenceOnlyBytes);
    Json->SetNumberField(TEXT("reference_only_values"), Value.ReferenceOnlyValues);
    FString Out = P4CondensedJson(Json);
    return Out;
}

bool ReadObjectBaseline(UObject* Asset, FBaseline& Out)
{
    const FString Record = Asset->GetOutermost()->GetMetaData()->GetValue(Asset, ObjectBaselineKey);
    TSharedPtr<FJsonObject> Json;
    FString Owner;
    double Format = 0;
    if (Record.IsEmpty() || !P4ParseJson(Record, Json))
        return false;
    if (!Json->TryGetNumberField(TEXT("format"), Format) || Format != 1 ||
        !Json->TryGetStringField(TEXT("owner"), Owner) || !P4SameText(Owner, ObjectBaselineOwner) ||
        !Json->TryGetStringField(TEXT("package"), Out.Package) ||
        !Json->TryGetStringField(TEXT("source_sha256"), Out.Source) ||
        !Json->TryGetStringField(TEXT("state"), Out.State))
        return false;
    double Bytes = 0, Values = 0;
    if (!Json->TryGetNumberField(TEXT("reference_only_bytes"), Bytes) || Bytes < 0 ||
        !Json->TryGetNumberField(TEXT("reference_only_values"), Values) || Values < 0)
        return false;
    Out.ReferenceOnlyBytes = int32(Bytes);
    Out.ReferenceOnlyValues = int32(Values);
    return P4IsSha256Hex(Out.Source) && P4IsSha256Hex(Out.State) &&
        P4SameText(Out.Package, Asset->GetOutermost()->GetName());
}

FString ObjectState(UObject* Asset, TArray<FString>* CapturedLines = nullptr)
{
    if (CapturedLines)
        CapturedLines->Reset();
    if (!IsInGameThread() || !IsValid(Asset) || !Asset->IsAsset() || Asset->IsA<UWorld>())
        return FString();
    FFile File;
    FString Error, Out;
    TArray<FString> Content, EngineMade;
    P4RestoreSkinnedSource(Asset);
    // Generated defaults can load a particle before the module's OnAssetLoaded hook.
    // Restore only its source-bound, unchanged native builds after package metadata is
    // available. The complete cooked comparison below still decides equality.
    if (auto* Particle = Cast<UParticleSystem>(Asset))
        if (Particle->GetClass() == P4RecoveredParticleSystemClass())
        {
            if (!P4PrepareOriginalParticleState(Particle, Error))
            {
                UE_LOG(LogTemp, Warning, TEXT("Original particle state custody refused for %s: %s"),
                    *Particle->GetPathName(), *Error);
                return FString();
            }
        }
    if (!Cooked(Asset, false, File, Error))
        return FString();
    Lines(File, Content, EngineMade);
    // Only a current, source-bound recovered-parent witness can project proven engine
    // parameter bindings back to original GUIDs. Every refusal retains the strict lines.
    if (Asset->GetClass()->GetPathName() == TEXT("/Script/Engine.MaterialInstanceConstant"))
    {
        FString BindingError;
        if (!P4ProjectOriginalMaterialInstanceBindings(Asset, Content, BindingError) && !BindingError.IsEmpty())
            UE_LOG(LogTemp, Verbose, TEXT("Original MIC binding comparison retained current state for %s: %s"),
                *Asset->GetPathName(), *BindingError);
    }
    if (Asset->GetClass()->GetPathName() == TEXT("/Script/Engine.AnimSequence"))
        Content.Append(EngineMade);
    if (CapturedLines)
        *CapturedLines = Content;
    return Digest(Content, Out) ? Out : FString();
}

bool LegacyEmptyVectorFieldState(UObject* Asset, const FP4OriginalRecord& Record, FString& Out, FString& Error)
{
    Out.Reset();
    Error.Reset();
    const FString Class = TEXT("/Script/Engine.VectorFieldStatic");
    const auto Eq = [](const FString& A, const FString& B) { return P4ExactNativeString(A, B); };
    // Fully reconstructed fields keep the modern serializer. This path admits
    // only the exact old reference-only record, not arbitrary v1 native data.
    if (!IsInGameThread() || !IsValid(Asset) || !Eq(Asset->GetClass()->GetPathName(), Class) || !Record.ReferenceOnly)
        return false;
    const auto Fail = [&](const TCHAR* Why)
    {
        Error = Why;
        return false;
    };
    if (Record.Version != 2 || !Eq(Record.ComparisonSchema, TEXT("object-lines-v1")))
        return Fail(TEXT("legacy-vector-field-record-not-qualified"));
    UPackage* Package = Asset->GetOutermost();
    UMetaData* Meta = Package->GetMetaData();
    const FString Marker = Meta->GetValue(Asset, TEXT("P4OriginalRecord"));
    const FString LegacyMarker = Meta->GetValue(Asset, ObjectBaselineKey);
    const bool HadNotHeld = Meta->HasValue(Asset, ObjectNotHeldKey);
    const FString NotHeld = Meta->GetValue(Asset, ObjectNotHeldKey);
    TSharedPtr<FJsonObject> Json;
    const TArray<TSharedPtr<FJsonValue>>* Ledger = nullptr;
    FString Owner;
    double Format = 0;
    if (!P4ParseJson(Marker, Json) || !Json->TryGetNumberField(TEXT("format"), Format) || Format != 2 ||
        !Json->TryGetStringField(TEXT("owner"), Owner) || !Eq(Owner, TEXT("p4mapkit-original")) ||
        !Json->TryGetArrayField(TEXT("values"), Ledger) || !Ledger || Ledger->Num())
        return Fail(TEXT("legacy-vector-field-original-ledger-not-qualified"));
    if (!Record.ReadOnly || !Eq(Record.Class, Class) || !Eq(Record.Kind, TEXT("object")) ||
        !Eq(Record.Fidelity, TEXT("exact")) || !Eq(Record.Package, Package->GetName()) ||
        !Eq(Record.SourceDigestKind, TEXT("header-body-sha256-v1")) || !P4IsSha256Hex(Record.SourceDigest) ||
        !P4IsSha256Hex(Record.ComparisonDigest) || Record.Values.Num() || !Eq(Record.LegacyKey, ObjectBaselineKey) ||
        !NotHeld.IsEmpty() || P4HasCreatorSource(Asset) || Package->IsDirty())
        return Fail(TEXT("legacy-vector-field-record-not-qualified"));
    FBaseline Baseline;
    if (!ReadObjectBaseline(Asset, Baseline) || !Eq(Baseline.Package, Record.Package) ||
        !Eq(Baseline.Source, Record.SourceDigest) || !Eq(Baseline.State, Record.ComparisonDigest) ||
        Baseline.ReferenceOnlyValues || Baseline.ReferenceOnlyBytes <= 24 || !Eq(Record.LegacyEvidence, LegacyMarker))
        return Fail(TEXT("legacy-vector-field-baseline-not-qualified"));
    // A previous native cook sets SingleUse on SourceData. A later read-write
    // edit can detach its archive and discard its allocation on Unlock while
    // retaining a nonzero count. Reject that edit without loading or cooking it.
    // Actual empty fields still require the complete cooked tail/digest proof.
    const UVectorFieldStatic* Field = CastChecked<UVectorFieldStatic>(Asset);
    if (Field->SourceData.GetElementCount() != 0 || Field->SourceData.GetBulkDataSize() != 0)
        return Fail(TEXT("legacy-vector-field-current-bulk-not-empty"));
    FP4KadanSourceBinding Source;
    if (!P4KadanReadBoundSource(Record.Package, Source, Error) || !Eq(Source.SourceDigest, Record.SourceDigest))
        return Fail(TEXT("legacy-vector-field-current-source-not-qualified"));
    FFile Game;
    Game.Header.assign(Source.Header.GetData(), Source.Header.GetData() + Source.Header.Num());
    Game.Body.assign(Source.Body.GetData(), Source.Body.GetData() + Source.Body.Num());
    if (!ReadNodes(Game, Record.Package, Error) || Game.Nodes.Num() != 1)
        return Fail(TEXT("legacy-vector-field-source-inventory-not-qualified"));
    const FNode& GameNode = Game.Nodes[0];
    FP4VectorFieldTail GameTail;
    if (!Eq(GameNode.Path, Asset->GetName()) || !Eq(GameNode.Class, Class) || !GameNode.Undecoded.IsEmpty() ||
        int64(GameNode.TailEnd - GameNode.TailStart) != Baseline.ReferenceOnlyBytes ||
        !P4ReadVectorFieldTail(
            Game.Body.data() + GameNode.TailStart, int64(GameNode.TailEnd - GameNode.TailStart), GameTail, Error) ||
        !GameTail.Elements || GameTail.Payload.Num() != GameTail.Elements)
        return Fail(TEXT("legacy-vector-field-source-tail-not-qualified"));
    FFile Current;
    if (!Cooked(Asset, false, Current, Error) || Current.Nodes.Num() != 1)
        return Fail(TEXT("legacy-vector-field-current-cook-not-qualified"));
    const FNode& Node = Current.Nodes[0];
    FP4VectorFieldTail Empty;
    if (!Eq(Node.Path, Asset->GetName()) || !Eq(Node.Class, Class) || !Node.Undecoded.IsEmpty() || !Node.NativeTail ||
        !P4LegacyVectorField::EmptyTail(Current.Body.data() + Node.TailStart, int64(Node.TailEnd - Node.TailStart),
            uint64(Current.Header.size() + Node.TailEnd), Empty, Error))
        return Fail(TEXT("legacy-vector-field-current-tail-not-canonical-empty"));
    TArray<FString> Content, EngineMade;
    Lines(Current, Content, EngineMade);
    const FString Prefix = Node.Path + TEXT(" | ");
    const FString Modern = Prefix + P4VectorFieldTailLine(Empty);
    int32 Found = INDEX_NONE, Matches = 0;
    for (int32 Index = 0; Index < Content.Num(); ++Index)
        if (Eq(Content[Index], Modern))
        {
            Found = Index;
            ++Matches;
        }
    if (Matches != 1)
        return Fail(TEXT("legacy-vector-field-current-tail-line-not-unique"));
    // v1 told this same empty tail by its size. All other current lines remain
    // byte-for-byte, including the dimensions, Bounds, class and archetype.
    Content[Found] = Prefix + TEXT("native data of 24 bytes");
    Content.Sort([](const FString& A, const FString& B) { return A.Compare(B, ESearchCase::CaseSensitive) < 0; });
    FString Legacy;
    if (!Digest(Content, Legacy) || !Eq(Legacy, Record.ComparisonDigest))
        return Fail(TEXT("legacy-vector-field-existing-v1-digest-differs"));
    if (!P4KadanSourceBindingCurrent(Source, Error))
        return false;
    UMetaData* AfterMeta = Package->GetMetaData();
    if (Asset->GetOutermost() != Package || Package->IsDirty() || P4HasCreatorSource(Asset) ||
        !Eq(Marker, AfterMeta->GetValue(Asset, TEXT("P4OriginalRecord"))) ||
        !Eq(LegacyMarker, AfterMeta->GetValue(Asset, ObjectBaselineKey)) ||
        HadNotHeld != AfterMeta->HasValue(Asset, ObjectNotHeldKey) ||
        !Eq(NotHeld, AfterMeta->GetValue(Asset, ObjectNotHeldKey)))
        return Fail(TEXT("legacy-vector-field-provenance-changed-during-cook"));
    Out = MoveTemp(Legacy);
    Error.Reset();
    return true;
}

FString ObjectStatus(UObject* Asset)
{
    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetBoolField(TEXT("original"), false);
    Json->SetBoolField(TEXT("unchanged"), false);
    if (!IsInGameThread())
        return Finish(Json, TEXT("asset-status-requires-editor-thread"));
    if (!IsValid(Asset))
        return Finish(Json, TEXT("asset-status-needs-an-asset"));
    Json->SetStringField(TEXT("asset"), Asset->GetPathName());
    FBaseline Baseline;
    ReadObjectBaseline(Asset, Baseline);
    Json->SetBoolField(TEXT("original"), true);
    Json->SetStringField(TEXT("source_sha256"), Baseline.Source);
    Json->SetStringField(TEXT("imported_state"), Baseline.State);
    Json->SetBoolField(TEXT("reference_only"), Baseline.ReferenceOnlyBytes > 0 || Baseline.ReferenceOnlyValues > 0);
    Json->SetNumberField(TEXT("native_bytes_not_held"), Baseline.ReferenceOnlyBytes);
    Json->SetNumberField(TEXT("values_not_held"), Baseline.ReferenceOnlyValues);
    const FP4OriginalStatus Status = P4QueryOriginalAsset(Asset);
    P4OriginalStatusFields(Status, *Json);
    return Finish(Json, Status.Error);
}

void DiscardObject(UObject* Asset)
{
    UPackage* Package = Asset->GetOutermost();
    Asset->ClearFlags(RF_Public | RF_Standalone);
    Asset->Rename(
        nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional | REN_ForceNoResetLoaders);
    Asset->MarkPendingKill();
    Package->SetDirtyFlag(false);
}

// ReferenceOnly: an asset with native data, which an object of a class without the game's code cannot
// hold, is still made, with its properties, so that levels and assets that refer to it can be brought
// over. It is recorded as for reference only, with the size of what it does not hold.
FP4OriginalAssetImportResult ImportObject(const FString& HeaderFile, const FString& BodyFile, bool SaveToDisk,
    bool ReferenceOnly, UObject** StagedAsset = nullptr)
{
    FP4OriginalAssetImportResult Result;
    const auto Refuse = [&](EP4OriginalAssetImportError Code, const FString& Detail)
    {
        Result.Error = Code;
        Result.ErrorDetail = Detail;
        return Result;
    };
    if (!IsInGameThread())
        return Refuse(EP4OriginalAssetImportError::EditorThreadRequired, TEXT("asset-import-requires-editor-thread"));
    FFile Game;
    FString Error;
    FBaseline Baseline;
    {
        TArray<uint8> HeaderFromFile, BodyFromFile;
        const FString BodyPath = BodyFile.IsEmpty() ? FPaths::ChangeExtension(HeaderFile, TEXT("uexp")) : BodyFile;
        // A file just written can be held open for a moment by a virus scanner or the indexer, and one
        // failed read would otherwise leave this asset, and everything that needs it, out of the import.
        bool Read = false;
        for (int32 Try = 0; !Read && Try < 5; ++Try)
        {
            if (Try)
                FPlatformProcess::Sleep(0.2f);
            Read = FFileHelper::LoadFileToArray(HeaderFromFile, *HeaderFile, FILEREAD_Silent) &&
                FFileHelper::LoadFileToArray(BodyFromFile, *BodyPath, FILEREAD_Silent);
        }
        if (!Read)
            return Refuse(EP4OriginalAssetImportError::SourceUnreadable, TEXT("asset-import-source-unreadable"));
        const TArray<uint8>& Header = HeaderFromFile;
        const TArray<uint8>& Body = BodyFromFile;
        if (!P4HashSourceBuffers(Header.GetData(), Header.Num(), Body.GetData(), Body.Num(), Baseline.Source))
            return Refuse(EP4OriginalAssetImportError::SourceUnreadable, TEXT("asset-import-source-unreadable"));
        Game.Header.assign(Header.GetData(), Header.GetData() + Header.Num());
        Game.Body.assign(Body.GetData(), Body.GetData() + Body.Num());
    }
    if (!ReadNodes(Game, FString(), Error))
        return Refuse(EP4OriginalAssetImportError::DecodeFailed, Error);
    // A table's rows are brought over with it. Kept for reference only, a table is kept without them.
    if (!ReferenceOnly && !ReadTables(Game, true, Error))
        return Refuse(EP4OriginalAssetImportError::TablesUnreadable, Error);
    FString Short = FPackageName::GetLongPackageAssetName(Game.PackageName);
    const FString Package = Game.PackageName;
    Result.Package = Package;
    Result.Objects = Game.Nodes.Num();
    Result.ObjectsKnown = true;
    int32 Main = 0;
    for (int32 Index = 1; Index <= Game.Nodes.Num(); ++Index)
        if (Game.Node(Index).Outer == 0)
        {
            if (Main)
                return Refuse(EP4OriginalAssetImportError::MultipleTopObjects,
                    TEXT("asset-import-more-than-one-object-at-the-top"));
            Main = Index;
        }
    // The asset has its package's name. The game's files spell some with other capitals than their
    // package, which is the same name to the engine, and the asset is given the file's spelling.
    if (!Main || !P4IsValidContentPackage(Package) || !Game.Node(Main).Name.Equals(Short, ESearchCase::IgnoreCase))
        return Refuse(EP4OriginalAssetImportError::InvalidGamePath, TEXT("asset-import-game-path-unusable"));
    Short = Game.Node(Main).Name;
    const FString Path = Package + TEXT(".") + Short;
    Result.AssetPath = Path;
    Result.Class = Game.Node(Main).Class;
    Result.AssetIdentityKnown = true;
    // Nothing the game's file holds may be dropped: what is not read refuses the asset.
    for (int32 Index = 1; Index <= Game.Nodes.Num(); ++Index)
    {
        const FNode& Node = Game.Node(Index);
        if (!Node.Undecoded.IsEmpty())
            return Refuse(EP4OriginalAssetImportError::PropertiesUnreadable,
                TEXT("asset-import-properties-not-read: ") + Node.Path + TEXT(" (") + Node.Class + TEXT("): ") +
                    Node.Undecoded);
        if (!Node.PlainTail && !Game.Tables.Contains(Index) && !Node.Skinned && !Node.Physics && !Node.PhysicsBody &&
            !Node.ParticleRequired && !Node.NativeTail)
        {
            if (!ReferenceOnly)
                return Refuse(EP4OriginalAssetImportError::NativeDataUnsupported,
                    TEXT("asset-import-native-data-not-brought-over-yet: ") + Node.Path + TEXT(" (") + Node.Class +
                        TEXT(")") + (Node.SkinnedIssue.IsEmpty() ? FString() : TEXT(": ") + Node.SkinnedIssue));
            Baseline.ReferenceOnlyBytes += int32(Node.TailEnd - Node.TailStart);
        }
        if (Index != Main && !Game.Below(Index, Main))
            return Refuse(EP4OriginalAssetImportError::ObjectOutsideAsset,
                TEXT("asset-import-object-outside-the-asset: ") + Node.Path);
    }
    Baseline.Package = Package;

    const bool OnDisk = !StagedAsset && !P4KadanOwnsReimportSlot(Package) && FPackageName::DoesPackageExist(Package);
    UObject* Asset = StagedAsset ? nullptr : FindObject<UObject>(nullptr, *Path);
    if (Asset && !IsValid(Asset))
        Asset = nullptr;
    if (!Asset && OnDisk)
        Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
    const bool StandIn = Asset &&
        FString(Asset->GetOutermost()->GetMetaData()->GetValue(Asset, TEXT("P4BlueprintStandIn"))) == TEXT("1");
    TUniquePtr<FP4StandInFill> FillGuard;
    TStrongObjectPtr<UObject> StagedGuard;
    TArray<FString> ExpectedPublicationLines;
    bool HasExpectedPublicationLines = false;
    if (StandIn)
    {
        if (Asset->GetClass()->GetPathName() != Game.Node(Main).Class)
            return Refuse(
                EP4OriginalAssetImportError::PlaceholderClassMismatch, TEXT("asset-import-placeholder-class-differs"));
        if (!P4IsVerifiedStandIn(Asset))
            return Refuse(EP4OriginalAssetImportError::EditedPlaceholder,
                TEXT("asset-import-would-replace-an-edited-placeholder"));
        if (!SaveToDisk)
            return Refuse(
                EP4OriginalAssetImportError::SaveRequired, TEXT("asset-import-placeholder-fill-requires-save"));
        UObject* Donor = nullptr;
        const FP4OriginalAssetImportResult Checked = ImportObject(HeaderFile, BodyFile, false, ReferenceOnly, &Donor);
        if (!Checked.IsValid() || !Donor)
            return Refuse(EP4OriginalAssetImportError::StagingFailed,
                Checked.ErrorDetail.IsEmpty() ? FString(TEXT("asset-import-placeholder-staging-unreadable"))
                                              : Checked.ErrorDetail);
        StagedGuard.Reset(Donor);
        FillGuard = MakeUnique<FP4StandInFill>(Asset);
        if (!FillGuard->IsReady())
            return Refuse(
                EP4OriginalAssetImportError::BackupUnreadable, TEXT("asset-import-placeholder-backup-unreadable"));
    }
    const auto DiscardImported = [&](UObject* Imported)
    {
        if (!StandIn)
            DiscardObject(Imported);
    };
    if (Asset && !StandIn)
    {
        FBaseline Earlier;
        if (!ReadObjectBaseline(Asset, Earlier))
            return Refuse(
                EP4OriginalAssetImportError::OccupiedDestination, TEXT("asset-import-destination-holds-another-asset"));
        const FP4OriginalStatus Status = P4QueryOriginalAsset(Asset);
        FString MigrationError;
        const bool Untouched = Status.IsUnchanged() ||
            ((Status.State == EP4OriginalState::NeedsMigration || Status.State == EP4OriginalState::Recorded) &&
                P4MigrateOriginalRecord(
                    Asset,
                    [&]()
                    {
                        return (Asset->GetClass()->GetPathName() != TEXT("/Script/Engine.AnimSequence") ||
                                   Status.Record.ComparisonSchema == TEXT("animation-object-lines-v2")) &&
                            P4SameText(ObjectState(Asset), Earlier.State);
                    },
                    MigrationError));
        if (!Untouched)
            return Refuse(Status.State == EP4OriginalState::NeedsMigration
                    ? EP4OriginalAssetImportError::RecordValidationRequired
                    : EP4OriginalAssetImportError::EditedOriginal,
                Status.State == EP4OriginalState::NeedsMigration
                    ? TEXT("asset-import-record-needs-validation: ") + MigrationError
                    : FString(TEXT("asset-import-would-replace-an-edited-asset")));
        if (!P4SameText(Earlier.Source, Baseline.Source))
            return Refuse(EP4OriginalAssetImportError::EarlierImportReplacementUnsupported,
                TEXT("asset-import-replacing-an-earlier-import-is-not-done-yet"));
        if (UDataTable* Table = Cast<UDataTable>(Asset))
        {
            UMetaData* Metadata = Asset->GetOutermost()->GetMetaData();
            if (FString(Metadata->GetValue(Asset, TEXT("P4ImportedTableValues"))).IsEmpty())
            {
                // Older imported tables gain their values only during an explicit import and only
                // after an exact current-content comparison. An edited table is never re-baselined.
                if (!P4SameText(ObjectState(Asset), Earlier.State))
                    return Refuse(EP4OriginalAssetImportError::EditedOriginal,
                        TEXT("asset-import-would-replace-an-edited-asset"));
                const FString Values = UP4AuthoringTools::NativeTableRowsJson(Table);
                if (!Values.IsEmpty())
                {
                    Metadata->SetValue(Asset, TEXT("P4ImportedTableValues"), *Values);
                    Asset->MarkPackageDirty();
                }
            }
        }
        Baseline = Earlier;
    }
    else
    {
        UPackage* Loaded = FindPackage(nullptr, *Package);
        TArray<UObject*> Inside;
        if (Loaded)
            GetObjectsWithOuter(Loaded, Inside, false);
        Inside.RemoveAll([](const UObject* Object) { return !IsValid(Object) || Object->IsA<UMetaData>(); });
        if (!StandIn && !StagedAsset && (OnDisk || Inside.Num() != 0))
            return Refuse(
                EP4OriginalAssetImportError::OccupiedDestination, TEXT("asset-import-destination-holds-another-asset"));
        UClass* Class = LoadObject<UClass>(nullptr, *Game.Node(Main).Class, nullptr, LOAD_NoWarn | LOAD_Quiet);
        // Qualified native storage preserves the game's three foliage fields.
        // Its actual engine cook/cold load is validated; the diagnostic cook
        // translates this compatibility class back to the original game class.
        const bool Foliage = Class == UFoliageType_InstancedStaticMesh::StaticClass();
        if (Foliage)
            Class = P4RecoveredFoliageClass();
        const bool Particle = Class == UParticleSystem::StaticClass();
        if (Particle)
            Class = P4RecoveredParticleSystemClass();
        if (!Class || Class->HasAnyClassFlags(CLASS_Abstract))
            return Refuse(EP4OriginalAssetImportError::ClassUnavailable,
                TEXT("asset-import-class-not-in-the-project: ") + Game.Node(Main).Class);
        // A game asset that is a template (the game's effect requests are) is made one too, as the file says.
        if (!StandIn)
        {
            UPackage* Outer = CreatePackage(StagedAsset
                    ? *(TEXT("/Temp/P4NativeStandInStage_") + FGuid::NewGuid().ToString(EGuidFormats::Digits))
                    : *Package);
            const EObjectFlags Flags = RF_Public | RF_Standalone | (EObjectFlags(Game.Node(Main).Flags) & SavedFlags);
            Asset = Foliage ? static_cast<UObject*>(P4NewRecoveredFoliage(Outer, FName(*Short), Flags))
                : Particle  ? static_cast<UObject*>(P4NewRecoveredParticleSystem(Outer, FName(*Short), Flags))
                            : NewObject<UObject>(Outer, Class, FName(*Short), Flags);
            if (!Asset)
                return Refuse(EP4OriginalAssetImportError::ClassUnavailable,
                    TEXT("asset-import-native-storage-unavailable: ") + Game.Node(Main).Class);
        }
        {
            const EObjectFlags Wanted = RF_Public | RF_Standalone | (EObjectFlags(Game.Node(Main).Flags) & SavedFlags);
            Asset->ClearFlags(SavedFlags & ~Wanted);
            Asset->SetFlags(Wanted);
        }
        TMap<int32, UObject*> Made;
        Made.Add(Main, Asset);
        // The objects inside it, each after the one it is in.
        for (bool Progress = true; Progress;)
        {
            Progress = false;
            for (int32 Index = 1; Index <= Game.Nodes.Num(); ++Index)
            {
                const FNode& Node = Game.Node(Index);
                if (Made.Contains(Index) || !Made.Contains(Node.Outer))
                    continue;
                UClass* Kind = LoadObject<UClass>(nullptr, *Node.Class, nullptr, LOAD_NoWarn | LOAD_Quiet);
                if (!Kind)
                {
                    DiscardImported(Asset);
                    return Refuse(EP4OriginalAssetImportError::ClassUnavailable,
                        TEXT("asset-import-class-not-in-the-project: ") + Node.Class);
                }
                UObject* Outer = Made[Node.Outer];
                UObject* Object = StaticFindObjectFast(nullptr, Outer, FName(*Node.Name));
                if (Object && Object->GetClass() != Kind)
                {
                    DiscardImported(Asset);
                    return Refuse(EP4OriginalAssetImportError::EngineObjectConflict,
                        TEXT("asset-import-engine-made-another-object-there: ") + Node.Path);
                }
                const EObjectFlags Wanted = EObjectFlags(Node.Flags) & SavedFlags;
                if (!Object)
                    Object = NewObject<UObject>(Outer, Kind, FName(*Node.Name), Wanted);
                // An object the engine made there keeps the game's saved flags too.
                Object->ClearFlags(SavedFlags & ~Wanted);
                Object->SetFlags(Wanted);
                Made.Add(Index, Object);
                Progress = true;
            }
        }
        // The engine structs of a skinned mesh's settings and of a sequence's notifies have members the
        // game's engine adds. Those values are passed over, told and kept with the asset, as for an asset
        // kept for reference.
        bool Skinned = false;
        for (int32 Index = 1; Index <= Game.Nodes.Num(); ++Index)
            Skinned = Skinned || Game.Node(Index).Skinned;
        TArray<FString> NotHeld, NotShipped;
        for (const auto& Pair : Made)
            if (!Apply(Game, Pair.Key, Pair.Value, Made, true, Error, &NotHeld, &NotShipped, ReferenceOnly))
            {
                DiscardImported(Asset);
                return Refuse(EP4OriginalAssetImportError::PropertyApplicationFailed, Error);
            }
        // What the game's engine adds to the engine's own classes and structs (a member, or a choice of an
        // enum) is kept as the game's own tags, written back by the cook and compared below with everything
        // else (P4StockTags). What is not that, or cannot be carried exactly, stays not held.
        TArray<FString> KeptByTags;
        bool GameTags = false;
        if (NotHeld.Num())
        {
            TMap<int32, TArray<FP4GameStockTagWanted>> Wanted;
            for (const FString& Told : NotHeld)
            {
                FString Where, Field, Json;
                if (!Told.Split(TEXT(" "), &Where, &Field) || !Field.Split(TEXT(" = "), &Field, &Json))
                    continue;
                for (const auto& Pair : Made)
                    if (Game.Node(Pair.Key).Path == Where)
                    {
                        const EP4GameStockTag Kind = P4GameStockTagKind(Pair.Value, Field, Json);
                        if (Kind != EP4GameStockTag::None)
                            Wanted.FindOrAdd(Pair.Key).Add({Field, Json, Kind});
                        break;
                    }
            }
            for (const auto& Pair : Wanted)
            {
                const FNode& Node = Game.Node(Pair.Key);
                const auto& Export = Game.Package.Exports[Pair.Key - 1];
                TArray<FString> Kept;
                FString Owner = Node.Class;
                int32 Dot = INDEX_NONE;
                if (Owner.FindLastChar(TEXT('.'), Dot))
                    Owner = Owner.Mid(Dot + 1);
                P4StoreGameStockTags(Made[Pair.Key], Game.Body, Game.Package, Export.Offset,
                    Export.Offset + Export.Size, Owner, Pair.Value, Kept, Result.GameFieldsRefused);
                // A path not kept stays among the values not held, so the asset stays the game's.
                for (int32 At = NotHeld.Num(); At-- > 0;)
                {
                    FString Field;
                    const FString Told = NotHeld[At];
                    if (!Told.StartsWith(Node.Path + TEXT(" "), ESearchCase::CaseSensitive) ||
                        !Told.Mid(Node.Path.Len() + 1).Split(TEXT(" = "), &Field, nullptr) || !Kept.Contains(Field))
                        continue;
                    KeptByTags.Add(Told);
                    NotHeld.RemoveAt(At);
                }
            }
            GameTags = KeptByTags.Num() > 0;
        }
        // Without reference-only or skinned data, a value the engine cannot hold refuses the asset, as before.
        if (!ReferenceOnly && !Skinned && NotHeld.Num())
        {
            DiscardImported(Asset);
            return Refuse(EP4OriginalAssetImportError::PropertyApplicationFailed,
                TEXT("level-import-property-not-in-this-engine: ") + NotHeld[0]);
        }
        // The native data, once every object has its properties: the mesh needs its skeleton, the
        // sequence its frame count and its settings.
        for (const auto& Pair : Made)
        {
            const FNode& Node = Game.Node(Pair.Key);
            if (Node.Physics)
            {
                if (!P4FillPhysicsNative(Pair.Value, Node.PhysicsData, Error))
                {
                    DiscardImported(Asset);
                    return Refuse(EP4OriginalAssetImportError::NativeFillFailed, Error);
                }
            }
            if (Node.PhysicsBody && !P4FillPrimitivePhysicsBodyNative(Pair.Value, Node.PhysicsBodyData, Error))
            {
                DiscardImported(Asset);
                return Refuse(EP4OriginalAssetImportError::NativeFillFailed, Error);
            }
            if (Node.NativeTail)
            {
                // The class's reader gives the object its data, or keeps it with the asset for the cook
                // (P4GameNativeTails).
                const uint8* Tail = Game.Body.data() + Node.TailStart;
                const int64 Size = int64(Node.TailEnd - Node.TailStart);
                const FP4NativeTailReader* Reader = P4FindNativeTailReader(Node.Class);
                if (!Reader)
                    Error = TEXT("native-tail-reader-missing");
                if (!Reader || !Reader->Fill(Pair.Value, Tail, Size, Error))
                {
                    DiscardImported(Asset);
                    return Refuse(EP4OriginalAssetImportError::NativeFillFailed, Error);
                }
                if (Reader->TailObject)
                {
                    UObject** Named = Made.Find(Reader->TailObject(Tail, Size));
                    FObjectProperty* Field =
                        FindFProperty<FObjectProperty>(Pair.Value->GetClass(), Reader->TailObjectProperty);
                    if (!Named || !Field)
                    {
                        DiscardImported(Asset);
                        return Refuse(EP4OriginalAssetImportError::NativeFillFailed,
                            TEXT("native-tail-object-not-in-the-package: ") + Node.Path);
                    }
                    Field->SetObjectPropertyValue_InContainer(Pair.Value, *Named);
                }
            }
            if (!Node.Skinned)
                continue;
            FP4SkinnedFillReport Report;
            const bool Filled = P4FillSkinnedNative(
                Pair.Value, Game.Package, Game.Body, Node.TailStart, Node.TailEnd,
                [&](int32 Index) { return Game.Ref(Index); }, Report, Error);
            Result.NativeData = MoveTemp(Report);
            Result.HasNativeData = true;
            if (!Filled)
            {
                DiscardImported(Asset);
                return Refuse(EP4OriginalAssetImportError::NativeFillFailed, Error);
            }
        }
        // The rows of a table, in the table's order. A row starts as its type has it and is given every
        // member the game's file says, which is every member it has.
        int32 RowsMade = 0;
        for (const auto& Pair : Game.Tables)
        {
            const FNode& Node = Game.Node(Pair.Key);
            UDataTable* Table = Cast<UDataTable>(Made.FindRef(Pair.Key));
            const UScriptStruct* Type = Table ? Table->GetRowStruct() : nullptr;
            if (!Type || !Type->IsChildOf(FTableRowBase::StaticStruct()))
            {
                DiscardImported(Asset);
                return Refuse(EP4OriginalAssetImportError::InvalidRowType,
                    TEXT("asset-import-row-type-is-not-a-table-row: ") + Node.Path);
            }
            for (int32 Row = 0; Row < Pair.Value.Names.Num(); ++Row)
            {
                FStructOnScope Scratch(Type);
                for (const auto& Field : Pair.Value.Rows[Row].Fields)
                {
                    FString Key = Text(Field.first);
                    int32 Place = 0, Bracket = INDEX_NONE;
                    if (Key.FindChar(TEXT('['), Bracket))
                    {
                        Place = FCString::Atoi(*Key.Mid(Bracket + 1));
                        Key.LeftInline(Bracket);
                    }
                    const FProperty* Property = Type->FindPropertyByName(FName(*Key));
                    const FString Where = Node.Path + TEXT(" row ") + Pair.Value.Names[Row] + TEXT(" ") +
                        Text(Field.first);
                    if (!Property || Place < 0 || Place >= Property->ArrayDim)
                    {
                        DiscardImported(Asset);
                        return Refuse(EP4OriginalAssetImportError::RowPropertyUnavailable,
                            TEXT("level-import-property-not-in-this-engine: ") + Where + TEXT(" (") +
                                Type->GetPathName() + TEXT(")"));
                    }
                    FWriter Writer{Game, Made, true};
                    if (!Writer.Value(Property,
                            Property->ContainerPtrToValuePtr<void>(Scratch.GetStructMemory(), Place), Field.second,
                            Where))
                    {
                        DiscardImported(Asset);
                        return Refuse(EP4OriginalAssetImportError::RowPropertyApplicationFailed, Writer.Error);
                    }
                    for (const FString& Value : Writer.NotShipped)
                        NotShipped.AddUnique(Value);
                }
                Table->AddRow(
                    FName(*Pair.Value.Names[Row]), *reinterpret_cast<const FTableRowBase*>(Scratch.GetStructMemory()));
            }
            if (Table->GetRowMap().Num() != Pair.Value.Names.Num())
            {
                DiscardImported(Asset);
                return Refuse(EP4OriginalAssetImportError::DuplicateRowName,
                    TEXT("asset-import-table-has-two-rows-of-one-name: ") + Node.Path);
            }
            RowsMade += Pair.Value.Names.Num();
            if (Table == Asset)
            {
                Asset->GetOutermost()->GetMetaData()->SetValue(
                    Asset, TEXT("P4GameRows"), *FString::Join(Pair.Value.Names, TEXT("\n")));
                // Editor-only source values let export keep only creator changes. They never enter a
                // cooked package or a mod, and remain with the table across save and reopen.
                Asset->GetOutermost()->GetMetaData()->SetValue(
                    Asset, TEXT("P4ImportedTableValues"), *UP4AuthoringTools::NativeTableRowsJson(Table));
            }
        }
        if (Game.Tables.Num())
        {
            Result.Rows = RowsMade;
            Result.RowsKnown = true;
        }
        if (!StagedAsset && Asset->IsA<UMaterialParameterCollection>())
        {
            // A reflected copy restores parameters but cannot create the engine's private uniform
            // buffer layout/default resource. Run the native load lifecycle after the full source
            // has been restored; the following cooked comparison still checks its serialized values.
            Asset->SetFlags(RF_NeedPostLoad);
            Asset->ConditionalPostLoad();
            P4MaterialCollectionPostLoadCompleted(CastChecked<UMaterialParameterCollection>(Asset));
        }
        if (!StagedAsset && Asset->IsA<UParticleSystem>())
        {
            if (!P4PreflightOriginalParticleEditorBuild(CastChecked<UParticleSystem>(Asset), Error))
            {
                DiscardImported(Asset);
                return Refuse(EP4OriginalAssetImportError::NativeDataUnsupported, Error);
            }
            // Run the real native load lifecycle after the complete object family/resources exist.
            // A separate cold reload restores the retained authoritative native data through
            // the ordinary asset-load hook, without calling the importer or depending on old DDC.
            for (const auto& Pair : Made)
                if (Pair.Key != Main)
                {
                    Pair.Value->SetFlags(RF_NeedPostLoad);
                    Pair.Value->ConditionalPostLoad();
                }
            Asset->SetFlags(RF_NeedPostLoad);
            Asset->ConditionalPostLoad();
            FP4KadanSourceBinding ParticleSource;
            ParticleSource.Package = Game.PackageName;
            ParticleSource.HeaderPath = FPaths::ConvertRelativePathToFull(HeaderFile);
            ParticleSource.BodyPath = FPaths::ConvertRelativePathToFull(
                BodyFile.IsEmpty() ? FPaths::ChangeExtension(HeaderFile, TEXT("uexp")) : BodyFile);
            ParticleSource.SourceDigest = Baseline.Source;
            ParticleSource.Header.Append(Game.Header.data(), int32(Game.Header.size()));
            ParticleSource.Body.Append(Game.Body.data(), int32(Game.Body.size()));
            if (!P4CaptureOriginalParticleCutouts(CastChecked<UParticleSystem>(Asset), ParticleSource, Error))
            {
                DiscardImported(Asset);
                return Refuse(EP4OriginalAssetImportError::NativeFillFailed, Error);
            }
            for (const auto& Pair : Made)
                if (Game.Node(Pair.Key).ParticleRequired)
                {
                    const FNode& Node = Game.Node(Pair.Key);
                    TArray<uint8> Tail;
                    Tail.Append(Game.Body.data() + Node.TailStart, int32(Node.TailEnd - Node.TailStart));
                    if (!P4OriginalParticleRequiredTailMatches(Cast<UParticleModuleRequired>(Pair.Value), Tail, Error))
                    {
                        DiscardImported(Asset);
                        return Refuse(EP4OriginalAssetImportError::NativeFillFailed, Error);
                    }
                }
        }
        // A material's cached expression data and texture streaming data are what the editor works out from
        // its graph. A material kept for reference only has no graph, so they are not held and are told.
        const bool GraphlessMaterial = ReferenceOnly && Asset->IsA<UMaterial>();
        if (GraphlessMaterial)
            for (const FNode& Node : Game.Nodes)
                if (P4SameText(Node.Class, TEXT("/Script/Engine.Material")))
                    for (const char* Derived : {"CachedExpressionData", "TextureStreamingData"})
                        if (const FDecodedValue* Value = Node.Values.Field(Derived))
                            NotHeld.AddUnique(Node.Path + TEXT(" ") + UTF8_TO_TCHAR(Derived) + TEXT(" = ") +
                                Text(P4MapKit::DecodedJson(*Value)));
        // The kept game tags count only when the comparison below proves them. When the cook could not
        // write them, or what it wrote is not what the game's file says, they are dropped and the asset is
        // compared again as before, with them among the values not held (and refused where that refuses).
        bool Retried = false;
        const auto DropGameTags = [&](const FString& Why)
        {
            if (!GameTags)
                return false;
            GameTags = false;
            Retried = true;
            P4ClearGameStockTags(Asset->GetOutermost());
            NotHeld.Append(KeptByTags);
            KeptByTags.Reset();
            Result.GameFieldsRefused.Add(TEXT("game-fields-not-proved: ") + Why);
            return true;
        };
        // The comparison starts again here after DropGameTags; the values it declares are made again.
    CompareAgain:
        if (Retried && !ReferenceOnly && !Skinned && NotHeld.Num())
        {
            DiscardImported(Asset);
            return Refuse(EP4OriginalAssetImportError::PropertyApplicationFailed,
                TEXT("level-import-property-not-in-this-engine: ") + NotHeld[0]);
        }
        Result.UnshippedReferences = NotShipped.Num();
        Result.ImportedValuesKnown = true;
        Baseline.ReferenceOnlyValues = NotHeld.Num();
        // The values passed over are told. Everything else the game's file says is compared.
        Result.ValuesNotHeld = NotHeld;
        // What was made is saved the way a cook saves it and compared with the game's file.
        FFile Mine;
        TArray<FString> GameSays, GameEngine, MineSays, MineEngine, MineCompared, MineComparedEngine;
        // The lines compared, with object paths and typed names in small letters, as the engine tells them
        // apart (without regard to case). The game's files can spell one object two ways: a particle
        // module names MI_eft_Pongashi_petal00_aesfr.MI_eft_Pongashi_Petal00_aesfr, and the asset made
        // names it as the editor spells it. Strings stay exact, and the lines told keep their spelling.
        TArray<FString> GameKeys, MineKeys, FoldedEngine;
        if (!Cooked(Asset, false, Mine, Error, FString(), &Game))
        {
            if (DropGameTags(Error))
                goto CompareAgain;
            DiscardImported(Asset);
            return Refuse(EP4OriginalAssetImportError::CookFailed, Error);
        }
        if (Asset->IsA<UParticleSystem>())
        {
            P4AddParticleRangesNotHeld(Game, Mine, NotHeld);
            Result.ValuesNotHeld = NotHeld;
        }
        {
            FFile Compared = Game;
            Without(Compared, NotHeld);
            Lines(Compared, GameSays, GameEngine);
            Lines(Mine, MineSays, MineEngine);
            Lines(Compared, GameKeys, FoldedEngine, nullptr, true);
            // A value not held is left out of both sides: the game's file says it, and the asset made
            // says what its place keeps instead (an empty reference to an asset the project does not
            // hold, written in full where the engine writes every member of a struct in a list).
            FFile MineWithout = Mine;
            Without(MineWithout, NotHeld);
            Lines(MineWithout, MineCompared, MineComparedEngine);
            FoldedEngine.Reset();
            Lines(MineWithout, MineKeys, FoldedEngine, nullptr, true);
        }
        // The asset's state is of all it says, as it is when asked for later.
        TArray<FString> MineAll = MineSays;
        const bool Animation = Asset->GetClass()->GetPathName() == TEXT("/Script/Engine.AnimSequence");
        if (Animation)
            MineAll.Append(MineEngine);
        MineSays = MoveTemp(MineCompared);
        if (GameKeys.Num() != GameSays.Num() || MineKeys.Num() != MineSays.Num())
        {
            if (DropGameTags(TEXT("asset-import-compare-folded-lines-differ")))
                goto CompareAgain;
            DiscardImported(Asset);
            return Refuse(EP4OriginalAssetImportError::CookDiffers, TEXT("asset-import-compare-folded-lines-differ"));
        }
        // For an asset kept for reference only, the native data is what it does not hold: the game's, and
        // what this editor's objects write in its place. A table kept that way is kept without its rows,
        // and says so.
        if (Baseline.ReferenceOnlyBytes > 0)
        {
            const auto Drop = [](TArray<FString>& Says, TArray<FString>& Keys, bool Table)
            {
                for (int32 At = Says.Num(); At-- > 0;)
                    if ((Table && Says[At].EndsWith(TEXT(" | table of 0 rows"), ESearchCase::CaseSensitive)) ||
                        Says[At].Contains(TEXT(" | native data of "), ESearchCase::CaseSensitive))
                    {
                        Says.RemoveAt(At);
                        Keys.RemoveAt(At);
                    }
            };
            Drop(GameSays, GameKeys, false);
            Drop(MineSays, MineKeys, true);
        }
        TSet<FString, FLocKeySetFuncs> InGame(GameKeys), InMine(MineKeys);
        TArray<FString> OnlyGame, OnlyMine, OnlyGameKeys, OnlyMineKeys;
        int32 Differences = 0;
        FString First;
        for (int32 At = 0; At < GameKeys.Num(); ++At)
            if (!InMine.Contains(GameKeys[At]))
            {
                if (First.IsEmpty())
                    First = TEXT("the game's file says ") + GameSays[At];
                if (OnlyGame.Num() < 400)
                    OnlyGame.Add(GameSays[At]);
                if (OnlyGameKeys.Num() < 5)
                    OnlyGameKeys.Add(GameKeys[At]);
                ++Differences;
            }
        for (int32 At = 0; At < MineKeys.Num(); ++At)
            if (!InGame.Contains(MineKeys[At]))
            {
                if (First.IsEmpty())
                    First = TEXT("the asset made says ") + MineSays[At];
                if (OnlyMine.Num() < 400)
                    OnlyMine.Add(MineSays[At]);
                if (OnlyMineKeys.Num() < 5)
                    OnlyMineKeys.Add(MineKeys[At]);
                ++Differences;
            }
        if (Differences)
        {
            if (DropGameTags(TEXT("asset-import-cook-differs-from-game: ") + First))
                goto CompareAgain;
            Result.Differences = Differences;
            Result.DifferencesKnown = true;
            // The first of each side as they were compared, so that the reason a package stays the game's says what to
            // look at. A compared line can hold less than the line shown above.
            const FString Detail = FString::Printf(TEXT(" (%d differences. Only the game's file says: %s. Only the asset says: %s)"),
                Differences, *FString::Join(OnlyGameKeys, TEXT(" ; ")), *FString::Join(OnlyMineKeys, TEXT(" ; ")));
            Result.OnlyGameSays = MoveTemp(OnlyGame);
            Result.OnlyAssetSays = MoveTemp(OnlyMine);
            DiscardImported(Asset);
            return Refuse(EP4OriginalAssetImportError::CookDiffers,
                TEXT("asset-import-cook-differs-from-game: ") + First + Detail);
        }
        Result.Compared = GameSays.Num();
        Result.ComparedKnown = true;
        // Proved: what each kept tag's place holds now is recorded, for the cook to tell a creator's edit.
        Result.GameFieldsKept = KeptByTags.Num();
        if (GameTags)
            P4SealGameStockTags(Asset->GetOutermost());
        // What the lines leave to a measurement: the poses of a sequence, by the engine's own decoder,
        // for the game's compressed keys and for the keys of the cook's save.
        if (Skinned)
        {
            Result.GameEngineMade = GameEngine;
            Result.EngineMade = MineEngine;
            Result.EngineMadeKnown = true;
            for (int32 Index = 1; Index <= Game.Nodes.Num(); ++Index)
            {
                const FNode& Node = Game.Node(Index);
                if (!Node.Skinned || !Made.Contains(Index))
                    continue;
                const FNode* Other = Mine.Nodes.FindByPredicate([&](const FNode& Candidate)
                    { return Candidate.Skinned && Candidate.Path.Equals(Node.Path, ESearchCase::IgnoreCase); });
                FP4SkinnedCompareReport Report;
                const bool Close = Other &&
                    P4CompareSkinnedCooked(Made[Index], Game.Package, Game.Body, Node.TailStart, Node.TailEnd,
                        Mine.Package, Mine.Body, Other->TailStart, Other->TailEnd, Report, Error);
                Result.Measured = MoveTemp(Report);
                Result.HasMeasured = true;
                if (!Other)
                    Error = TEXT("asset-import-cook-holds-no-such-object: ") + Node.Path;
                if (!Close)
                {
                    DiscardImported(Asset);
                    return Refuse(EP4OriginalAssetImportError::NativeComparisonFailed, Error);
                }
            }
            if (NotHeld.Num())
                Asset->GetOutermost()->GetMetaData()->SetValue(
                    Asset, ObjectNotHeldKey, *FString::Join(NotHeld, TEXT("\n")));
        }
        if (!Digest(MineAll, Baseline.State))
        {
            DiscardImported(Asset);
            return Refuse(EP4OriginalAssetImportError::StateUnreadable, TEXT("asset-import-state-unreadable"));
        }
        if (SaveToDisk)
        {
            ExpectedPublicationLines = MineAll;
            HasExpectedPublicationLines = true;
        }
        if (StagedAsset)
        {
            Result.SourceDigest = Baseline.Source;
            Result.ImportedState = Baseline.State;
            Result.State = Baseline.State;
            Result.ReferenceOnly = Baseline.ReferenceOnlyBytes > 0 || Baseline.ReferenceOnlyValues > 0;
            Result.NativeBytesNotHeld = Baseline.ReferenceOnlyBytes;
            Result.ValuesNotHeldCount = Baseline.ReferenceOnlyValues;
            Result.BaselineKnown = true;
            *StagedAsset = Asset;
            Result.Asset = Asset;
            return Result;
        }
        Asset->GetOutermost()->GetMetaData()->SetValue(Asset, ObjectBaselineKey, *ObjectBaselineText(Baseline));
        TArray<FP4OriginalValueNote> Notes = ValueNotes(NotHeld, NotShipped);
        if (UParticleSystem* ParticleSystem = Cast<UParticleSystem>(Asset))
        {
            FP4OriginalValueNote NativeSource;
            if (!P4OriginalParticleCutoutCustodyNote(ParticleSystem, NativeSource, Error))
            {
                DiscardImported(Asset);
                return Refuse(EP4OriginalAssetImportError::RecordFailed, Error);
            }
            Notes.Add(MoveTemp(NativeSource));
        }
        if (!P4MarkLegacyOriginalAsset(
                Asset, Notes, Error, Animation ? TEXT("animation-object-lines-v2") : TEXT("object-lines-v1")))
        {
            DiscardImported(Asset);
            return Refuse(EP4OriginalAssetImportError::RecordFailed, Error);
        }
        if (StandIn)
            Asset->GetOutermost()->GetMetaData()->RemoveValue(Asset, FName(TEXT("P4BlueprintStandIn")));
        else
            FAssetRegistryModule::AssetCreated(Asset);
        Asset->MarkPackageDirty();
        Result.Created = !StandIn;
        Result.FilledStandIn = StandIn;
        Result.FilledStandInKnown = true;
    }
    Result.SourceDigest = Baseline.Source;
    Result.ImportedState = Baseline.State;
    Result.State = Baseline.State;
    Result.ReferenceOnly = Baseline.ReferenceOnlyBytes > 0 || Baseline.ReferenceOnlyValues > 0;
    Result.NativeBytesNotHeld = Baseline.ReferenceOnlyBytes;
    Result.ValuesNotHeldCount = Baseline.ReferenceOnlyValues;
    Result.BaselineKnown = true;
    FString FillSourceError;
    if (FillGuard && !P4PrepareVerifiedGameFillSource(Asset, *FillGuard, FillSourceError))
        return Refuse(EP4OriginalAssetImportError::RecordFailed, FillSourceError);
    if (SaveToDisk && (StandIn || Result.Created || Asset->GetOutermost()->IsDirty() || !OnDisk))
    {
        FString SaveError;
        if (!P4SaveAssetPackage(
                Asset,
                [&]()
                {
                    // Observe the same single actual recook used by the existing predicate. The
                    // receipt diagnoses a refusal; it never changes the expected state or admission.
                    TArray<FString> ActualLines;
                    const FString ActualState = ObjectState(Asset, &ActualLines);
                    if (P4SameText(ActualState, Baseline.State))
                        return true;
                    if (!HasExpectedPublicationLines)
                    {
                        UE_LOG(LogTemp, Display,
                            TEXT(
                                "Original publication state mismatch %s: baseline-only/no captured expected lines; expected=%s actual=%s actual-lines=%d"),
                            *Asset->GetPathName(), *Baseline.State, *ActualState, ActualLines.Num());
                        return false;
                    }
                    TSet<FString, FLocKeySetFuncs> ExpectedSet(ExpectedPublicationLines), ActualSet(ActualLines);
                    int32 Removed = 0, Added = 0;
                    TArray<FString> FirstRemoved, FirstAdded;
                    for (const FString& Line : ExpectedPublicationLines)
                        if (!ActualSet.Contains(Line))
                        {
                            ++Removed;
                            if (FirstRemoved.Num() < 2)
                                FirstRemoved.Add(Line);
                        }
                    for (const FString& Line : ActualLines)
                        if (!ExpectedSet.Contains(Line))
                        {
                            ++Added;
                            if (FirstAdded.Num() < 2)
                                FirstAdded.Add(Line);
                        }
                    UE_LOG(LogTemp, Display,
                        TEXT(
                            "Original publication state mismatch %s: expected-lines=%d actual-lines=%d removed=%d added=%d expected=%s actual=%s"),
                        *Asset->GetPathName(), ExpectedPublicationLines.Num(), ActualLines.Num(), Removed, Added,
                        *Baseline.State, *ActualState);
                    for (const FString& Line : FirstRemoved)
                        UE_LOG(LogTemp, Display, TEXT("Original publication removed: %s"), *Line);
                    for (const FString& Line : FirstAdded)
                        UE_LOG(LogTemp, Display, TEXT("Original publication added: %s"), *Line);
                    return false;
                },
                SaveError))
        {
            // A fresh failed publication has no usable saved identity. Leave an existing
            // asset or a stand-in under its original fill guard instead of discarding it.
            if (Result.Created)
            {
                FAssetRegistryModule::AssetDeleted(Asset);
                DiscardImported(Asset);
            }
            return Refuse(EP4OriginalAssetImportError::SaveFailed, TEXT("asset-import-save-refused: ") + SaveError);
        }
        Result.Saved = true;
    }
    if (FillGuard)
        FillGuard->Commit();
    if (StagedGuard.IsValid())
        StagedGuard->MarkPendingKill();
    Result.Asset = Asset;
    return Result;
}
} // namespace P4LevelContent

FString P4OriginalObjectAssetState(UObject* Asset)
{
    return P4LevelContent::ObjectState(Asset);
}

FString P4OriginalObjectAssetStateAndLines(UObject* Asset, TArray<FString>& OutLines)
{
    OutLines.Reset();
    return P4LevelContent::ObjectState(Asset, &OutLines);
}

bool P4LegacyEmptyVectorFieldState(UObject* Asset, const FP4OriginalRecord& Record, FString& Out, FString& Error)
{
    return P4LevelContent::LegacyEmptyVectorFieldState(Asset, Record, Out, Error);
}

FString P4OriginalObjectAssetStatus(UObject* Asset)
{
    return P4LevelContent::ObjectStatus(Asset);
}

FString P4ImportOriginalObjectAsset(
    const FString& HeaderFile, const FString& BodyFile, bool SaveToDisk, bool ReferenceOnly)
{
    return P4OriginalAssetImportJson(P4LevelContent::ImportObject(HeaderFile, BodyFile, SaveToDisk, ReferenceOnly));
}

bool P4IsOriginalObjectAsset(UObject* Asset)
{
    P4LevelContent::FBaseline Baseline;
    return IsInGameThread() && IsValid(Asset) && Asset->IsAsset() && !Asset->IsA<UWorld>() &&
        P4LevelContent::ReadObjectBaseline(Asset, Baseline);
}

FP4OriginalAssetImportResult P4ImportOriginalAssetNative(
    const FString& HeaderFile, const FString& BodyFile, bool SaveToDisk, bool ReferenceOnly)
{
    return P4LevelContent::ImportObject(HeaderFile, BodyFile, SaveToDisk, ReferenceOnly);
}

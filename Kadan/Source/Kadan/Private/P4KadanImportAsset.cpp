// The import of one package that is not a Blueprint or a material: its admission, one step for
// each kind of package, and its result.

#include "P4KadanImportInternal.h"
#include "P4GamePaths.h"
#include "P4OriginalBuiltLighting.h"
#include "Engine/MapBuildDataRegistry.h"

using namespace P4KadanImport;

namespace
{
// Importer refusals of a value the editor cannot hold. The asset is then made for reference only.
const TCHAR* const ReferenceOnlyReasons[] = {TEXT("asset-import-native-data-not-brought-over-yet"),
    TEXT("struct-member-not-in-this-engine"), TEXT("property-not-in-this-engine"),
    TEXT("enum-value-not-in-this-engine")};

} // namespace

// Imports one package that is not a Blueprint. False when the import has to stop.
bool FP4KadanImportRun::ImportOne(const TSharedPtr<FP4KadanSourceAsset>& Asset)
{
    if (CyclicAnimations.Contains(FName(*Asset->Package)) && !DeclareAnimationCycles())
    {
        FatalFailure = true;
        Checkpoint(true);
        return false;
    }
    const double OneStarted = FPlatformTime::Seconds(), ReportBefore = Seconds.FindRef(TEXT("report"));
    ON_SCOPE_EXIT
    {
        // The report's own time is counted under "report", not under the asset's kind.
        const double Since = OneStarted + (Seconds.FindRef(TEXT("report")) - ReportBefore);
        const FString& Kind = Asset->Kind;
        Spent(TEXT("import ") +
                (Kind.StartsWith(TEXT("Texture"))                  ? FString(TEXT("textures"))
                        : Kind == TEXT("Material")                 ? FString(TEXT("materials"))
                        : Kind == TEXT("MaterialInstanceConstant") ? FString(TEXT("material instances"))
                        : Kind == TEXT("StaticMesh")               ? FString(TEXT("static meshes"))
                        : Kind == TEXT("SkeletalMesh") || Kind == TEXT("Skeleton") || Kind == TEXT("PhysicsAsset")
                        ? FString(TEXT("skeletal meshes"))
                        : Kind.StartsWith(TEXT("Anim")) ? FString(TEXT("animations"))
                        : Kind == TEXT("World")         ? FString(TEXT("levels"))
                                                        : FString(TEXT("other assets"))),
            Since);
    };
    FAssetImport Item(Asset);
    if (const FImportOutcome Outcome = AdmitAsset(Item))
        return *Outcome;
    if (Asset->Kind == TEXT("Material") || Asset->Kind == TEXT("MaterialInstanceConstant"))
    {
        if (const FImportOutcome Outcome = ImportMaterial(Item))
            return *Outcome;
    }
    else if (Asset->Kind.StartsWith(TEXT("Texture")) || Asset->Kind == TEXT("VolumeTexture"))
        ImportTexture(Item);
    else if (Asset->Kind == TEXT("StaticMesh"))
        ImportStaticMesh(Item);
    else if (Asset->Kind == TEXT("World"))
        ImportWorld(Item);
    else if (Asset->Kind == TEXT("MapBuildDataRegistry"))
        ImportBuiltLighting(Item);
    else
        ImportOtherAsset(Item);
    return FinishAssetImport(Item);
}

FP4KadanImportRun::FImportOutcome FP4KadanImportRun::AdmitAsset(FAssetImport& Item)
{
    const TSharedPtr<FP4KadanSourceAsset>& Asset = Item.Asset;
    if (Resume && Asset->Kind != TEXT("World") && !GameBackedPackages.Contains(FName(*Asset->Package)) &&
        ExistingOrFromLibrary(Asset->Package))
    {
        auto Reply = MakeShared<FJsonObject>();
        Reply->SetBoolField(TEXT("valid"), true);
        Reply->SetStringField(TEXT("state"), TEXT("existing asset preserved"));
        Replies->SetObjectField(Asset->Package, Reply);
        return true;
    }
    if (const FString* Unreadable = UnreadableSources.Find(FName(*Asset->Package)))
    {
        auto Reply = NativeImportReport(
            false, *Unreadable, Asset->Package, FString(), false, false, false, TEXT("skipped"));
        Reply->SetStringField(TEXT("kind"), Asset->Kind);
        Replies->SetObjectField(Asset->Package, Reply);
        Failed(*Asset, *Unreadable, true, true);
        Checkpoint();
        return !ReportWriteFailed && !FatalFailure;
    }
    if (!BlueprintKind(Asset->Kind) && KeepNamedReady)
        for (const FString& Dependency : Asset->Dependencies)
            if (!SoftDependency(*Asset, Dependency))
                KeepNamed(Dependency, 0);
    Item.Missing = NeededAndMissing(*Asset);
    const FString& Missing = Item.Missing;
    if (FatalFailure)
        return false;
    if (!Missing.IsEmpty() && Asset->Kind != TEXT("World"))
    {
        auto Reply = NativeImportReport(false, TEXT("Needs a package that did not import."), Asset->Package, FString(),
            false, false, false, TEXT("skipped"));
        Reply->SetStringField(TEXT("kind"), Asset->Kind);
        Replies->SetObjectField(Asset->Package, Reply);
        Failed(*Asset, TEXT("Not imported because ") + Missing + TEXT(" did not import."));
        Checkpoint();
        return !ReportWriteFailed && !FatalFailure;
    }
    if (!Resume)
    {
        UObject* Retained = nullptr;
        FString RetentionError;
        if (P4RetainUnchangedAnimationDependency(Plan, *Asset, Retained, RetentionError) ||
            P4RetainUnchangedMaterialCollectionDependency(Plan, *Asset, Retained, RetentionError))
        {
            auto Kept = NativeImportReport(true, FString(), Asset->Package, Retained->GetPathName(), false, false,
                false, TEXT("existing unchanged source dependency retained"));
            Kept->SetStringField(TEXT("kind"), Asset->Kind);
            Replies->SetObjectField(Asset->Package, Kept);
            Out.Imported.Add(Asset->Package, Asset->Kind);
            ImportedIdentities.Add(FName(*Asset->Package));
            Checkpoint();
            return !ReportWriteFailed && !FatalFailure;
        }
    }
    // Only the old raw-object MIC schema can need the source-bound parent
    // GUID projection. Creator content, newer source-built MICs, masters,
    // and every other class keep their existing admission order/policy.
    bool& DeferredMICAdmission = Item.DeferredMICAdmission;
    if (!Resume && Asset->Kind == TEXT("MaterialInstanceConstant") && FPackageName::DoesPackageExist(Asset->Package))
    {
        UObject* Existing = LoadObject<UObject>(
            nullptr, *P4ObjectPathOf(Asset->Package), nullptr, LOAD_NoWarn | LOAD_Quiet);
        FP4OriginalRecord Earlier;
        FString ReadError;
        DeferredMICAdmission = IsValid(Existing) && !P4HasCreatorSource(Existing) &&
            P4ReadOriginalRecord(Existing, Earlier, ReadError) && Earlier.Version == 2 && Earlier.ReadOnly &&
            P4ExactNativeString(Earlier.Class, TEXT("/Script/Engine.MaterialInstanceConstant")) &&
            P4ExactNativeString(Earlier.Kind, TEXT("object")) &&
            P4ExactNativeString(Earlier.ComparisonSchema, TEXT("object-lines-v1")) &&
            P4ExactNativeString(Earlier.SourceDigestKind, TEXT("header-body-sha256-v1"));
    }
    bool& DeferredMasterDependencyAdmission = Item.DeferredMasterDependencyAdmission;
    if (!Resume && Plan.bMaterialExecutionDeferred && Plan.RequestedPackages.Num() && Asset->Kind == TEXT("Material") &&
        !Plan.RequestedPackages.ContainsByPredicate(
            [&](const FString& Root) { return FName(*Root) == FName(*Asset->Package); }) &&
        FPackageName::DoesPackageExist(Asset->Package))
    {
        UObject* Existing = LoadObject<UObject>(
            nullptr, *P4ObjectPathOf(Asset->Package), nullptr, LOAD_NoWarn | LOAD_Quiet);
        FP4OriginalRecord Earlier;
        FString ReadError;
        DeferredMasterDependencyAdmission = IsValid(Existing) && Existing->GetClass() == UMaterial::StaticClass() &&
            !P4HasCreatorSource(Existing) && !Existing->GetOutermost()->IsDirty() &&
            P4ReadOriginalRecord(Existing, Earlier, ReadError) && Earlier.Version == 2 && Earlier.ReadOnly &&
            Earlier.ReferenceOnly && P4ExactNativeString(Earlier.Class, TEXT("/Script/Engine.Material")) &&
            P4ExactNativeString(Earlier.Kind, TEXT("material")) &&
            P4ExactNativeString(Earlier.Fidelity, TEXT("recovered-pixel-contract")) &&
            P4ExactNativeString(Earlier.ComparisonSchema, TEXT("material-native-state-v1")) &&
            P4ExactNativeString(Earlier.SourceDigestKind, TEXT("material-graph-v2-md5"));
    }
    if (!DeferredMICAdmission && !DeferredMasterDependencyAdmission)
    {
        bool Finished = false;
        const bool Continue = BeginReimport(Item, Finished);
        if (Finished)
            return Continue;
    }
    return {};
}

bool FP4KadanImportRun::BeginReimport(FAssetImport& Item, bool& Finished)
{
    const TSharedPtr<FP4KadanSourceAsset>& Asset = Item.Asset;
    TUniquePtr<FP4KadanReimportAsset>& Reimport = Item.Reimport;
    Finished = false;
    if (Resume || Asset->Kind == TEXT("World"))
        return true;
    Reimport = MakeUnique<FP4KadanReimportAsset>(Asset->Package);
    if (!Reimport->IsReady())
    {
        Finished = true;
        Failed(*Asset, Reimport->GetError());
        Checkpoint();
        return !ReportWriteFailed && !FatalFailure;
    }
    if (Reimport->Preserved())
    {
        auto Kept = NativeImportReport(true, FString(), Asset->Package, FString(), false, false, false,
            TEXT("existing creator content preserved"));
        Kept->SetStringField(TEXT("kind"), Asset->Kind);
        Replies->SetObjectField(Asset->Package, Kept);
        Warnings.AddUnique(Asset->Package + TEXT(" keeps its existing content. Its game references were read again."));
        Finished = true;
        return true;
    }
    return true;
}

void FP4KadanImportRun::ImportTexture(FAssetImport& Item)
{
    const TSharedPtr<FP4KadanSourceAsset>& Asset = Item.Asset;
    bool& NativeFailureFatal = Item.NativeFailureFatal;
    bool& MayKeepGameIdentity = Item.MayKeepGameIdentity;
    bool& HasNativeResult = Item.HasNativeResult;
    bool& NativeValid = Item.NativeValid;
    FString& NativeError = Item.NativeError;
    TSharedPtr<FJsonObject>& Reply = Item.Reply;
    const auto Imported = P4ImportOriginalTextureNative(Asset->Header, Asset->Body, true);
    NativeFailureFatal = !Imported.IsValid() && Imported.Error != EP4OriginalTextureImportError::BuildFailed &&
        Imported.Error != EP4OriginalTextureImportError::BuildDiffers;
    MayKeepGameIdentity = Imported.Error == EP4OriginalTextureImportError::SourceUnreadable ||
        Imported.Error == EP4OriginalTextureImportError::BuiltSourceUnreadable;
    HasNativeResult = true;
    NativeValid = Imported.IsValid();
    NativeError = Imported.ErrorDetail;
    Reply = NativeImportReport(NativeValid, NativeError, Imported.Package, Imported.AssetPath, Imported.Created,
        Imported.Saved, Imported.FilledStandIn, Imported.State);
    Reply->SetBoolField(TEXT("replaced"), Imported.Replaced);
}

void FP4KadanImportRun::ImportStaticMesh(FAssetImport& Item)
{
    const TSharedPtr<FP4KadanSourceAsset>& Asset = Item.Asset;
    bool& NativeFailureFatal = Item.NativeFailureFatal;
    bool& MayKeepGameIdentity = Item.MayKeepGameIdentity;
    bool& HasNativeResult = Item.HasNativeResult;
    bool& NativeValid = Item.NativeValid;
    FString& NativeError = Item.NativeError;
    TSharedPtr<FJsonObject>& Reply = Item.Reply;
    const auto Imported = P4ImportOriginalMeshNative(Asset->Header, Asset->Body, true);
    switch (Imported.Error)
    {
        case EP4OriginalMeshImportError::None:
        case EP4OriginalMeshImportError::SocketsUnsupported:
        case EP4OriginalMeshImportError::PropertiesUndecoded:
        case EP4OriginalMeshImportError::PropertyUnrepresentable:
        case EP4OriginalMeshImportError::CollisionUnrepresentable:
        case EP4OriginalMeshImportError::MissingMaterial:
        case EP4OriginalMeshImportError::BuildFailed:
        case EP4OriginalMeshImportError::EngineBuildDiffers:
        case EP4OriginalMeshImportError::PropertyPreparationFailed:
        case EP4OriginalMeshImportError::CollisionProfileMismatch:
        case EP4OriginalMeshImportError::FillDiffers:
            break;
        default:
            NativeFailureFatal = true;
            break;
    }
    MayKeepGameIdentity = Imported.Error == EP4OriginalMeshImportError::DecodeFailed;
    HasNativeResult = true;
    NativeValid = Imported.IsValid();
    NativeError = Imported.ErrorDetail;
    Reply = NativeImportReport(NativeValid, NativeError, Imported.Package, Imported.AssetPath, Imported.Created,
        Imported.Saved, Imported.FilledStandIn, Imported.State);
    Reply->SetStringField(TEXT("equality"), Imported.Equality);
}

void FP4KadanImportRun::ImportWorld(FAssetImport& Item)
{
    const TSharedPtr<FP4KadanSourceAsset>& Asset = Item.Asset;
    const FString& Missing = Item.Missing;
    bool& HasNativeResult = Item.HasNativeResult;
    bool& NativeValid = Item.NativeValid;
    FString& NativeError = Item.NativeError;
    bool& NativeFailureFatal = Item.NativeFailureFatal;
    if (!Settings.AllowGameBackedLevels)
        StrictWorldSources.Add(Asset->Package, Asset);
    bool ExistingGameBacked = false;
    UWorld* ExistingWorld = nullptr;
    UPackage* ExistingLevelPackage = FindPackage(nullptr, *Asset->Package);
    if (!ExistingLevelPackage && FPackageName::DoesPackageExist(Asset->Package))
        ExistingLevelPackage = LoadPackage(nullptr, *Asset->Package, LOAD_NoWarn | LOAD_Quiet);
    if (UPackage* Loaded = ExistingLevelPackage)
        if (UWorld* World = UWorld::FindWorldInPackage(Loaded))
        {
            ExistingWorld = World;
            TSet<FName> Omitted;
            FString Source, CustodyError;
            ExistingGameBacked = P4ReadGameBackedLevelCustody(World, Omitted, Source, CustodyError);
        }
    const bool MissingDependency = !Missing.IsEmpty() || ExistingGameBacked;
    const bool PreserveOriginal = Resume && ExistingWorld;
    if (!Settings.AllowGameBackedLevels && PreserveOriginal)
        StrictResumedWorlds.Add(Asset->Package);
    FP4OriginalLevelImportResult Imported;
    if (!Settings.AllowGameBackedLevels && !PreserveOriginal && !Missing.IsEmpty())
    {
        // A level is never made without one of its actors. The import stops with the reason.
        Imported.Package = Asset->Package;
        Imported.Error = EP4OriginalLevelImportError::UnsupportedContent;
        Imported.ErrorDetail = TEXT("This level needs ") + Missing +
            TEXT(", which did not import. Its reason is in the report for that package.");
    }
    else
        Imported = PreserveOriginal ? P4ResumeOriginalLevelNative(ExistingWorld, Asset->Header, Asset->Body)
            : !Resume && ExistingWorld
            ? (Settings.AllowGameBackedLevels
                      ? P4ReimportOriginalLevelWithGameReferencesNative(
                            ExistingWorld, Asset->Header, Asset->Body, true, GameBackedPackages)
                      : P4ReimportOriginalLevelNative(ExistingWorld, Asset->Header, Asset->Body, true))
            : MissingDependency && Settings.AllowGameBackedLevels
            ? P4ImportGameBackedLevelNative(Asset->Header, Asset->Body, true, GameBackedPackages,
                  ExistingGameBacked ? TEXT("Existing game-backed layer preserved")
                                     : TEXT("Unsupported dependency: ") + Missing)
            : P4ImportOriginalLevelNative(Asset->Header, Asset->Body, true);
    if (!Imported.IsValid() && Settings.AllowGameBackedLevels && !PreserveOriginal && !MissingDependency &&
        (Imported.Error == EP4OriginalLevelImportError::UnsupportedContent ||
            Imported.Error == EP4OriginalLevelImportError::BuildFailed ||
            Imported.Error == EP4OriginalLevelImportError::CookDiffers ||
            (Imported.Error == EP4OriginalLevelImportError::CookFailed &&
                Imported.ErrorDetail.StartsWith(
                    TEXT("level-file-level-script-variable-type-not-read: "), ESearchCase::CaseSensitive))))
    {
        // A reconstructed script can cook a variable layout this reader cannot
        // prove. The projection independently validates original source data
        // and admits only proven actor families. Other cook failures refuse.
        const FString StrictReason = Imported.ErrorDetail;
        Imported = P4ImportGameBackedLevelNative(Asset->Header, Asset->Body, true, GameBackedPackages, StrictReason);
    }
    if (!Settings.AllowGameBackedLevels && Imported.GameBacked)
    {
        Imported.Error = EP4OriginalLevelImportError::UnsupportedContent;
        Imported.ErrorDetail = TEXT(
            "Strict native import requires a complete original level; an existing partial layer was preserved.");
    }
    HasNativeResult = true;
    NativeValid = Imported.IsValid();
    NativeError = Imported.ErrorDetail;
    NativeFailureFatal = !Imported.IsValid();
    if (BestEffort && Imported.IsValid() && !Imported.GameBacked && Imported.Created)
        StrictMade.Add(Asset->Package, Asset);
    ReportWorldImport(Item, Imported);
}

void FP4KadanImportRun::ReportWorldImport(FAssetImport& Item, const FP4OriginalLevelImportResult& Imported)
{
    const TSharedPtr<FP4KadanSourceAsset>& Asset = Item.Asset;
    TSharedPtr<FJsonObject>& Reply = Item.Reply;
    bool& NativeValid = Item.NativeValid;
    FString& NativeError = Item.NativeError;
    Reply = NativeImportReport(NativeValid, NativeError, Imported.Package, Imported.AssetPath, Imported.Created,
        Imported.Saved, false, Imported.State);
    Reply->SetNumberField(TEXT("level_error_code"), int32(Imported.Error));
    Reply->SetNumberField(TEXT("objects"), Imported.Objects);
    Reply->SetBoolField(TEXT("game_backed"), Imported.GameBacked);
    if (Imported.ActorCountsKnown)
    {
        Reply->SetNumberField(TEXT("original_actor_count"), Imported.OriginalActorCount);
        Reply->SetNumberField(TEXT("editable_original_actor_count"), Imported.EditableOriginalActorCount);
    }
    if (Imported.GameBacked)
    {
        Reply->SetStringField(TEXT("game_backed_reason"), Imported.GameBackedReason);
        Reply->SetNumberField(TEXT("uneditable_original_actor_count"), Imported.UneditableOriginalActors.Num());
        TArray<TSharedPtr<FJsonValue>> Omitted;
        for (const FString& Name : Imported.UneditableOriginalActors)
            Omitted.Add(MakeShared<FJsonValueString>(Name));
        Reply->SetArrayField(TEXT("uneditable_original_actors"), Omitted);
        if (Imported.IsValid())
            Warnings.AddUnique(FString::Printf(
                TEXT("%s keeps %d original actors in the game, so they cannot be edited here. ") TEXT(
                    "They need a package that did not import or a class Kadan cannot rebuild yet. The import report names the reason."),
                *Asset->Package, Imported.UneditableOriginalActors.Num()));
    }
    if (Imported.DifferencesKnown)
        Reply->SetNumberField(TEXT("differences"), Imported.Differences);
    if (Imported.ComparedKnown)
        Reply->SetNumberField(TEXT("compared"), Imported.Compared);
    const auto Words = [&](const TCHAR* Key, const TArray<FString>& Values)
    {
        TArray<TSharedPtr<FJsonValue>> Array;
        for (const FString& Value : Values)
            Array.Add(MakeShared<FJsonValueString>(Value));
        Reply->SetArrayField(Key, Array);
    };
    Words(TEXT("values_not_held"), Imported.ValuesNotHeld);
    Words(TEXT("class_problems"), Imported.ClassProblems);
    Words(TEXT("classes_that_differ"), Imported.DifferingClasses);
    Words(TEXT("classes_given"), Imported.LentClasses);
    Words(TEXT("not_shipped"), Imported.UnshippedReferences);
    Words(TEXT("not_in_game"), Imported.ProjectOnlyObjects);
    Words(TEXT("only_the_game_says"),
        TArray<FString>(Imported.OnlyGameSays.GetData(), FMath::Min(Imported.OnlyGameSays.Num(), 40)));
    Words(TEXT("only_the_level_made_says"),
        TArray<FString>(Imported.OnlyLevelSays.GetData(), FMath::Min(Imported.OnlyLevelSays.Num(), 40)));
    Reply->SetNumberField(TEXT("not_kept"), Imported.NotKeptCount);
    Reply->SetNumberField(TEXT("added"), Imported.AddedCount);
    if (Imported.PlacementKnown)
    {
        Reply->SetNumberField(TEXT("placement_rows"), Imported.PlacementRows);
        Reply->SetNumberField(TEXT("placement_values"), Imported.PlacementValues);
        Words(TEXT("placement_without_row"), Imported.PlacementWithoutRow);
        Words(TEXT("placement_not_read"), Imported.PlacementNotRead);
    }
    Words(TEXT("placement_not_given"), Imported.PlacementNotHeld);
    if (NativeValid)
    {
        MaterialRecovery->ReportLandscapeColorPreviews(Imported.AssetPath, *Asset, Settings, Reply, Warnings);
        const FString& Level = Asset->Package;
        for (const FString& Value : Imported.ValuesNotHeld)
            Warnings.Add(Level + TEXT(" has a value this editor cannot hold (") + Value + TEXT(")."));
        for (const FString& Problem : Imported.ClassProblems)
            Warnings.Add(
                Level + TEXT(" uses a class that could not be given the game's values (") + Problem + TEXT(")."));
        for (const FString& Class : Imported.DifferingClasses)
            Warnings.Add(Level + TEXT(" uses a class that differs from the game's (") + Class + TEXT(")."));
        if (Imported.UnshippedReferences.Num())
            Warnings.Add(FString::Printf(
                TEXT("%s refers to %d objects that the game does not ship. Those references are empty."), *Level,
                Imported.UnshippedReferences.Num()));
        if (Imported.NotKeptCount)
        {
            TArray<FString> Kinds;
            for (const FString& Line : Imported.NotKept)
            {
                TArray<FString> Bars;
                Line.ParseIntoArray(Bars, TEXT(" | "), false);
                if (Bars.Num() < 2)
                    continue;
                TArray<FString> Sides;
                Bars[1].ParseIntoArray(Sides, TEXT(" = "), false);
                TArray<FString> Parts;
                if (Sides.Num())
                    Sides[0].ParseIntoArray(Parts, TEXT(" "), false);
                if (Parts.Num())
                    Kinds.AddUnique(Parts[0]);
            }
            Kinds.Sort();
            Warnings.Add(FString::Printf(TEXT("%s has %d values that a construction script run did not keep (%s)."),
                *Level, Imported.NotKeptCount, *FString::Join(Kinds, TEXT(", "))));
        }
        if (Imported.AddedCount)
            Warnings.Add(FString::Printf(
                TEXT("%s has %d values that a construction script run added."), *Level, Imported.AddedCount));
        // The options the game keeps per placement, from the map's placement lists.
        for (const FString& Line : Imported.PlacementNotHeld)
            Warnings.Add(Level +
                TEXT(" has a gameplay object option from the game's placement list that the object does not hold (") +
                Line + TEXT(")."));
        if (Imported.PlacementNotRead.Num())
            Warnings.Add(Level +
                TEXT(
                    " has gameplay object options that Kadan does not read from the game's placement lists yet, so the Details panel shows the level's values for them (") +
                FString::Join(Imported.PlacementNotRead, TEXT(", ")) + TEXT(")."));
        Out.LevelObjects += Imported.Objects;
    }
}

void FP4KadanImportRun::ImportOtherAsset(FAssetImport& Item)
{
    const TSharedPtr<FP4KadanSourceAsset>& Asset = Item.Asset;
    bool& NativeFailureFatal = Item.NativeFailureFatal;
    bool& MayKeepGameIdentity = Item.MayKeepGameIdentity;
    bool& HasNativeResult = Item.HasNativeResult;
    bool& NativeValid = Item.NativeValid;
    FString& NativeError = Item.NativeError;
    TSharedPtr<FJsonObject>& Reply = Item.Reply;
    auto Imported = P4ImportOriginalAssetNative(Asset->Header, Asset->Body, true, false);
    // A value the editor cannot hold. The asset is then kept so that others can refer to it, and told.
    bool KeptForReference = false;
    if (!Imported.IsValid())
        for (const TCHAR* Reason : ReferenceOnlyReasons)
            if (Imported.ErrorDetail.Contains(Reason, ESearchCase::CaseSensitive))
            {
                KeptForReference = true;
                break;
            }
    // An asset that names a package the map import leaves the game's (an animation's effect
    // notify, for one) is kept for reference only: the reference is a value this editor does not
    // hold, it is named, and the game keeps its own asset.
    if (!Imported.IsValid() && !KeptForReference)
    {
        const FString MissingMark = TEXT("level-import-asset-not-in-the-project: ");
        const int32 At = Imported.ErrorDetail.Find(MissingMark, ESearchCase::CaseSensitive);
        if (At != INDEX_NONE)
        {
            FString Path = Imported.ErrorDetail.Mid(At + MissingMark.Len());
            int32 End = INDEX_NONE;
            if (Path.FindChar(TEXT(':'), End))
                Path.LeftInline(End);
            Path.TrimEndInline();
            KeptForReference = KeptAsTheGames.Contains(FName(*FPackageName::ObjectPathToPackageName(Path)));
        }
    }
    if (KeptForReference)
    {
        const FString First = Imported.ErrorDetail;
        Imported = P4ImportOriginalAssetNative(Asset->Header, Asset->Body, true, true);
        if (Imported.IsValid())
        {
            Out.ReferenceOnly.Add(Asset->Package);
            Warnings.Add(Asset->Package +
                TEXT(
                    " holds data this editor cannot hold, so it was kept for reference only and cannot go into a mod (") +
                First + TEXT(")."));
        }
    }
    if (!Imported.IsValid())
    {
        auto ParticleReference = P4KeepUnsupportedParticleReference(Imported, true);
        if (ParticleReference.IsValid())
        {
            Imported = MoveTemp(ParticleReference);
            Out.ReferenceOnly.AddUnique(Asset->Package);
            Warnings.Add(Asset->Package +
                TEXT(
                    " needs a game-only particle module. Its read-only asset reference was kept; its effect payload stays in the game and cannot go into a mod."));
        }
    }
    if (!Imported.IsValid())
    {
        auto SoundReference = P4KeepUnsupportedSoundReference(Imported, true);
        if (SoundReference.IsValid())
        {
            Imported = MoveTemp(SoundReference);
            Out.ReferenceOnly.AddUnique(Asset->Package);
            Warnings.Add(Asset->Package +
                TEXT(
                    " was kept as a read-only sound reference. Its sound event payload was not recovered and cannot go into a mod."));
        }
    }
    HasNativeResult = true;
    NativeValid = Imported.IsValid();
    NativeError = Imported.ErrorDetail;
    Reply = NativeImportReport(NativeValid, NativeError, Imported.Package, Imported.AssetPath, Imported.Created,
        Imported.Saved, Imported.FilledStandIn, Imported.State);
    NativeFailureFatal = !Imported.IsValid() && !AssetCanRemainInGame(Imported.Error);
    MayKeepGameIdentity = Imported.Error == EP4OriginalAssetImportError::DecodeFailed ||
        Imported.Error == EP4OriginalAssetImportError::SourceUnreadable ||
        Imported.Error == EP4OriginalAssetImportError::PropertiesUnreadable ||
        Imported.Error == EP4OriginalAssetImportError::TablesUnreadable;
    Reply->SetBoolField(TEXT("reference_only"), Imported.ReferenceOnly);
    Reply->SetNumberField(TEXT("game_fields_kept"), Imported.GameFieldsKept);
    if (Imported.GameFieldsRefused.Num())
    {
        TArray<TSharedPtr<FJsonValue>> Refused;
        for (const FString& Refusal : Imported.GameFieldsRefused)
            Refused.Add(MakeShared<FJsonValueString>(Refusal));
        Reply->SetArrayField(TEXT("game_fields_refused"), Refused);
    }
    if (Imported.DifferencesKnown)
        Reply->SetNumberField(TEXT("differences"), Imported.Differences);
    if (Imported.ComparedKnown)
        Reply->SetNumberField(TEXT("compared"), Imported.Compared);
    TArray<TSharedPtr<FJsonValue>> NotHeld;
    for (const FString& Value : Imported.ValuesNotHeld)
        NotHeld.Add(MakeShared<FJsonValueString>(Value));
    Reply->SetArrayField(TEXT("values_not_held"), NotHeld);
}

// A level's built lighting, as the game's project held it: reference-only, so the editor draws the level with the
// game's lighting and an edited map's lighting is built again at export.
void FP4KadanImportRun::ImportBuiltLighting(FAssetImport& Item)
{
    const TSharedPtr<FP4KadanSourceAsset>& Asset = Item.Asset;
    const FP4OriginalBuiltLightingResult Imported = P4ImportOriginalBuiltLighting(Asset->Header, Asset->Body, true);
    const bool Made = Imported.Registry != nullptr;
    // Without it the level refers to the game's copy and draws unlit, as it did before built lighting was brought in.
    if (!Made)
        Warnings.Add(Asset->Package + TEXT(" is the built lighting of a level. It could not be brought in (") +
            Imported.Error + TEXT("), so the editor draws the level without it. The game keeps its own."));
    Item.HasNativeResult = true;
    Item.NativeValid = true;
    Item.Reply = NativeImportReport(true, Imported.Error, Imported.Package,
        Made ? Imported.Registry->GetPathName() : FString(), Made, Imported.Saved, false,
        Made ? TEXT("game built lighting kept for reference") : TEXT("kept as the game's"));
    Item.Reply->SetBoolField(TEXT("reference_only"), true);
    Item.Reply->SetNumberField(TEXT("lighting_textures"), Imported.Textures);
    if (Made)
    {
        Out.ReferenceOnly.AddUnique(Asset->Package);
        // A level imported before its lighting refers to it now, as the game's editor held it.
        P4HoldOriginalBuiltLightingOfRegistry(Imported.Registry);
    }
}

bool FP4KadanImportRun::FinishAssetImport(FAssetImport& Item)
{
    const TSharedPtr<FP4KadanSourceAsset>& Asset = Item.Asset;
    TSharedPtr<FJsonObject>& Reply = Item.Reply;
    bool& HasNativeResult = Item.HasNativeResult;
    bool& NativeValid = Item.NativeValid;
    FString& NativeError = Item.NativeError;
    TUniquePtr<FP4KadanReimportAsset>& Reimport = Item.Reimport;
    bool& NativeFailureFatal = Item.NativeFailureFatal;
    bool& MayKeepGameIdentity = Item.MayKeepGameIdentity;
    bool Valid = false;
    if (Reply.IsValid())
    {
        Reply->SetStringField(TEXT("kind"), Asset->Kind);
        Reply->SetStringField(TEXT("header"), Asset->Header);
        Replies->SetObjectField(Asset->Package, Reply);
        const TArray<TSharedPtr<FJsonValue>>* Notes = nullptr;
        if (Asset->Kind != TEXT("World") && Reply->TryGetArrayField(TEXT("problems"), Notes))
            for (const auto& Note : *Notes)
                Warnings.AddUnique(TEXT("A note on ") + Asset->Package + TEXT(". ") + Note->AsString());
        if (HasNativeResult)
        {
            Valid = NativeValid;
            Why = NativeError;
        }
        else
        {
            if (!Reply->TryGetBoolField(TEXT("valid"), Valid))
                Reply->TryGetBoolField(TEXT("ok"), Valid);
            Reply->TryGetStringField(TEXT("error"), Why);
        }
    }
    if (Valid && Reimport && !Reimport->Finish(Why))
    {
        Valid = false;
        if (Reply.IsValid())
        {
            Reply->SetBoolField(TEXT("valid"), false);
            Reply->SetStringField(TEXT("error"), Why);
        }
    }
    if (Valid)
    {
        Out.Imported.Add(Asset->Package, Asset->Kind);
        ImportedIdentities.Add(FName(*Asset->Package));
    }
    else
        Failed(*Asset, Why, NativeFailureFatal, MayKeepGameIdentity);
    Why.Empty();
    Checkpoint();
    return !ReportWriteFailed && !FatalFailure;
}

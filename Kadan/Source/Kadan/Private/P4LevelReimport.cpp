// Importing a game level again over an earlier import, keeping the edits made to it.

#include "P4LevelContentInternal.h"
#include "P4Common.h"

namespace P4LevelContent
{
// Reimport stages a complete normal import under its real source identity. The old loaded
// package and its file stay available until the new source baseline and creator edits pass.
struct FLevelPropertyReplay
{
    FString ObjectClass, Schema, Text;
};

struct FLevelReimportCapture
{
    FBaseline Baseline;
    FP4LevelChangesResult Edits;
    TArray<FString> Lines;
    TMap<FString, FLevelPropertyReplay> Properties;
    FString PublishedState;
};

TMap<FString, TSharedPtr<FLevelReimportCapture>> SavedLevelReimports;

void ClearUnchangedPublishedLevelDirtyFlags()
{
    for (const auto& Entry : SavedLevelReimports)
    {
        if (Entry.Value->PublishedState.IsEmpty())
            continue;
        UPackage* Package = FindPackage(nullptr, *Entry.Key);
        UWorld* World = Package ? UWorld::FindWorldInPackage(Package) : nullptr;
        if (!IsValid(World) || !Package->IsDirty() || P4HasCreatorSource(World))
            continue;
        FP4OriginalRecord Record;
        FString Why;
        if (P4ReadOriginalRecord(World, Record, Why) && Record.Kind == TEXT("level") &&
            P4SameText(Record.SourceDigest, Entry.Value->Baseline.Source) &&
            P4SameText(State(World), Entry.Value->PublishedState))
            // Package reload/stream registration can mark another already-published
            // world dirty. Its complete saved source+creator state is still exact.
            // A later edit or unreadable state stays dirty and is never overwritten.
            Package->SetDirtyFlag(false);
    }
}

FString ReplayPropertySchema(const FProperty* Property)
{
    return Property ? Property->GetClass()->GetName() + TEXT("|") + Property->GetCPPType() +
            FString::Printf(TEXT("|%d|%d"), Property->ArrayDim, Property->ElementSize)
                    : FString();
}

bool CaptureLevelReimport(UWorld* World, FLevelReimportCapture& Capture, FString& Error)
{
    FP4OriginalRecord Record;
    if (!IsValid(World) || !ReadBaseline(World, Capture.Baseline) || !P4ReadOriginalRecord(World, Record, Error) ||
        Record.Kind != TEXT("level") || P4HasCreatorSource(World) ||
        Record.Package != World->GetOutermost()->GetName() || !P4SameText(Record.SourceDigest, Capture.Baseline.Source))
    {
        Error = TEXT("level-reimport-snapshot-source-not-proven: ") + Error;
        return false;
    }
    Capture.Edits = AnalyzeChanges(World);
    if (!Capture.Edits.IsValid())
    {
        Error = Capture.Edits.ErrorDetail;
        return false;
    }
    FFile File;
    TArray<FString> Engine;
    if (!Cooked(World, true, File, Error))
        return false;
    Lines(File, Capture.Lines, Engine);
    TArray<FString> Current = Capture.Lines;
    WithoutEditorNavigation(World, Current);
    WithEditorDroppedStreams(World, Current);
    WithEditorNavLinkIds(World, Current);
    WithEditorLandscapeMaterials(World, Current);
    WithInheritedAnimationClasses(World, Current);
    WithoutAddedNativeTransformTracking(World, Current);
    FString CapturedState;
    if (!Digest(Current, CapturedState) || !P4SameText(CapturedState, Capture.Edits.State))
    {
        Error = TEXT("level-reimport-snapshot-changed-during-capture");
        return false;
    }
    const auto Properties = [&](const FP4LevelLineChanges& Changed) -> bool
    {
        TArray<FString> Both = Changed.Before;
        Both.Append(Changed.After);
        for (const FString& Line : Both)
        {
            FString Path, Said, Name, Value;
            if (!Line.Split(TEXT(" | "), &Path, &Said) || !Said.Split(TEXT(" = "), &Name, &Value))
            {
                Error = TEXT("level-reimport-change-needs-object-property: ") + Line.Left(300);
                return false;
            }
            const FString Key = Path + TEXT(" | ") + Name;
            if (Capture.Properties.Contains(Key))
                continue;
            UObject* Object = StaticFindObject(UObject::StaticClass(), World->GetOutermost(), *Path);
            FProperty* Property = Object ? FindFProperty<FProperty>(Object->GetClass(), FName(*Name)) : nullptr;
            if (!Property || Property->HasAnyPropertyFlags(CPF_Transient))
            {
                Error = TEXT("level-reimport-changed-property-unavailable: ") + Key;
                return false;
            }
            FLevelPropertyReplay Replay;
            Replay.ObjectClass = Object->GetClass()->GetPathName();
            Replay.Schema = ReplayPropertySchema(Property);
            Property->ExportTextItem(
                Replay.Text, Property->ContainerPtrToValuePtr<void>(Object), nullptr, Object, PPF_Copy);
            Capture.Properties.Add(Key, MoveTemp(Replay));
        }
        return true;
    };
    for (const auto& Changed : Capture.Edits.OtherChanged)
        if (!Properties(Changed.Lines))
            return false;
    return Properties(Capture.Edits.LevelChanges);
}

bool CaptureSavedLevelReimport(UWorld* World, FString& Error)
{
    FP4OriginalRecord Record;
    FString Why;
    if (!IsValid(World) || !P4ReadOriginalRecord(World, Record, Why) || Record.Kind != TEXT("level") ||
        P4HasCreatorSource(World))
        return true;
    const FString Package = World->GetOutermost()->GetName();
    if (SavedLevelReimports.Contains(Package))
        return true;
    if (P4KadanHasSavedLevelReimportScope())
    {
        Error = TEXT("level-reimport-snapshot-not-captured-before-dependency-changes: ") + Package;
        return false;
    }
    auto Capture = MakeShared<FLevelReimportCapture>();
    if (!CaptureLevelReimport(World, *Capture, Error))
        return false;
    SavedLevelReimports.Add(Package, MoveTemp(Capture));
    return true;
}

bool ReplayLevelEdits(UWorld* Previous, UWorld* Made, const FLevelReimportCapture& Capture, FString& Error)
{
    const FP4LevelChangesResult& Edits = Capture.Edits;
    UPackage* OldPackage = Previous->GetOutermost();
    UPackage* NewPackage = Made->GetOutermost();
    TMap<UObject*, UObject*> Twins;
    Twins.Add(OldPackage, NewPackage);
    TArray<UObject*> OldObjects;
    GetObjectsWithOuter(OldPackage, OldObjects, true);
    for (UObject* Old : OldObjects)
        if (IsValid(Old) && !Old->IsA<UMetaData>())
            if (UObject* New = StaticFindObject(Old->GetClass(), NewPackage, *Old->GetPathName(OldPackage), true))
                Twins.Add(Old, New);
    for (const FString& Name : Edits.Removed)
        if (AActor* Actor = FindObject<AActor>(Made->PersistentLevel, *Name))
        {
            if (!Made->DestroyActor(Actor, false, false))
            {
                Error = TEXT("level-reimport-could-not-replay-removal: ") + Name;
                return false;
            }
        }
    for (const FString& Name : Edits.Added)
    {
        AActor* Original = FindObject<AActor>(Previous->PersistentLevel, *Name);
        if (!Original || FindObject<UObject>(Made->PersistentLevel, *Name))
        {
            Error = TEXT("level-reimport-added-actor-identity-conflict: ") + Name;
            return false;
        }
        // The editor's native duplication serializes actor content without copying running
        // renderer/physics pointers. This is also the normal added-actor export path.
        FObjectDuplicationParameters Copying(Original, Made->PersistentLevel);
        Copying.DestName = Original->GetFName();
        TMap<UObject*, UObject*> Created;
        Copying.CreatedObjects = &Created;
        AActor* Copy = Cast<AActor>(StaticDuplicateObjectEx(Copying));
        if (!Copy)
        {
            Error = TEXT("level-reimport-added-actor-not-made: ") + Name;
            return false;
        }
        Made->PersistentLevel->Actors.Add(Copy);
        Twins.Append(Created);
        Twins.Add(Original, Copy);
    }
    // Replay only properties that changed against the old imported baseline. Copy/paste text
    // uses Unreal's property serialization, leaving transient native scene pointers behind.
    TSet<FString> Applied;
    const auto Replay = [&](const FP4LevelLineChanges& Changed) -> bool
    {
        TArray<FString> Lines = Changed.Before;
        Lines.Append(Changed.After);
        for (const FString& Line : Lines)
        {
            FString Path, Said, PropertyName, Value;
            if (!Line.Split(TEXT(" | "), &Path, &Said) || !Said.Split(TEXT(" = "), &PropertyName, &Value))
            {
                Error = TEXT("level-reimport-change-needs-object-property: ") + Line.Left(300);
                return false;
            }
            const FString Key = Path + TEXT(" | ") + PropertyName;
            if (Applied.Contains(Key))
                continue;
            Applied.Add(Key);
            const FLevelPropertyReplay* Saved = Capture.Properties.Find(Key);
            UObject* To = StaticFindObject(UObject::StaticClass(), NewPackage, *Path);
            FProperty* Property = To ? FindFProperty<FProperty>(To->GetClass(), FName(*PropertyName)) : nullptr;
            if (!Saved || !To || !Property || To->GetClass()->GetPathName() != Saved->ObjectClass ||
                ReplayPropertySchema(Property) != Saved->Schema || Property->HasAnyPropertyFlags(CPF_Transient))
            {
                Error = TEXT("level-reimport-changed-property-schema-unavailable: ") + Key;
                return false;
            }
            if (!Property->ImportText(*Saved->Text, Property->ContainerPtrToValuePtr<void>(To), PPF_Copy, To))
            {
                Error = TEXT("level-reimport-changed-property-not-replayed: ") + Key;
                return false;
            }
        }
        return true;
    };
    for (const FP4LevelActorChanges& Changed : Edits.OtherChanged)
        if (!Replay(Changed.Lines))
            return false;
    if (!Replay(Edits.LevelChanges))
        return false;
    for (const FP4LevelActorMove& Move : Edits.Moved)
    {
        AActor* Actor = FindObject<AActor>(Made->PersistentLevel, *Move.Actor);
        const FTransform Transform(Move.Rotation, Move.Location, Move.Scale);
        if (!Actor || !Actor->SetActorTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics))
        {
            Error = TEXT("level-reimport-move-not-replayed: ") + Move.Actor;
            return false;
        }
    }
    // Keep editor organization as well as cooked edits. Do not copy Kadan's source records
    // or new game field storage over the freshly verified source baseline.
    for (const auto& Pair : Twins)
        if (AActor* From = Cast<AActor>(Pair.Key))
            if (AActor* To = Cast<AActor>(Pair.Value))
                for (const TCHAR* Name :
                    {TEXT("ActorLabel"), TEXT("FolderPath"), TEXT("bHiddenEd"), TEXT("bHiddenEdLayer")})
                    if (FProperty* Field = FindFProperty<FProperty>(From->GetClass(), Name))
                        if (Field->HasAnyPropertyFlags(CPF_EditorOnly) && From->GetClass() == To->GetClass())
                            Field->CopyCompleteValue_InContainer(To, From);
    TArray<UObject*> NewObjects;
    GetObjectsWithOuter(NewPackage, NewObjects, true);
    for (UObject* Object : NewObjects)
        if (IsValid(Object) && !Object->IsA<UMetaData>())
        {
            FArchiveReplaceObjectRef<UObject> References(Object, Twins, false, true, true);
        }
    for (AActor* Actor : Made->PersistentLevel->Actors)
        if (IsValid(Actor))
            if (USceneComponent* Root = Actor->GetRootComponent())
                Root->UpdateComponentToWorld();
    return true;
}

FP4OriginalLevelImportResult ReimportNative(UWorld* Previous, const FString& HeaderFile, const FString& BodyFile,
    bool SaveToDisk, const TSet<FName>* ProjectionDependencies = nullptr)
{
    FP4OriginalLevelImportResult Result;
    const auto Fail = [&](EP4OriginalLevelImportError Code, const FString& Why)
    {
        Result.Error = Code;
        Result.ErrorDetail = Why;
        return Result;
    };
    if (!IsInGameThread() || !GEditor)
        return Fail(EP4OriginalLevelImportError::EditorThreadRequired, TEXT("level-reimport-needs-editor-thread"));
    if (!SaveToDisk || !IsValid(Previous) || !Previous->PersistentLevel || GEditor->PlayWorld)
        return Fail(EP4OriginalLevelImportError::OccupiedDestination,
            TEXT("level-reimport-needs-a-saved-original-outside-play"));
    UPackage* OldPackage = Previous->GetOutermost();
    const FString PackageName = OldPackage->GetName();
    const uint32 OriginalFlags = OldPackage->GetPackageFlags();
    const bool WasCurrentEditorMap = GEditor->GetEditorWorldContext().World() == Previous;
    const TWeakObjectPtr<UWorld> PreviousIdentity(Previous);
    Result.Package = PackageName;
    Result.AssetPath = Previous->GetPathName();
    FBaseline OldBaseline;
    FP4OriginalRecord Record;
    FString Error;
    FP4OriginalWorldSourcePtr Source;
    if (!ReadBaseline(Previous, OldBaseline) || !P4ReadOriginalRecord(Previous, Record, Error) ||
        Record.Kind != TEXT("level") || P4HasCreatorSource(Previous) ||
        !P4ReadOriginalWorldSource(HeaderFile, BodyFile, PackageName, Source, Error) ||
        !P4SameText(OldBaseline.Source, Source->SourceDigest()) || !P4SameText(Record.SourceDigest, OldBaseline.Source))
        return Fail(EP4OriginalLevelImportError::RecordValidationRequired,
            TEXT("level-reimport-original-source-not-proven: ") + Error);
    const bool MayImproveProjection = ProjectionDependencies &&
        Record.ComparisonSchema == TEXT("game-backed-level-lines-v1");
    TSet<FName> PreviouslyOmitted;
    FString PreviousSource;
    if (MayImproveProjection &&
        !ReadGameBackedCustody(Previous, PreviouslyOmitted, PreviousSource, Error, Source.Get()))
        return Fail(EP4OriginalLevelImportError::RecordValidationRequired, Error);
    if (!P4KadanHasSavedLevelReimportScope() && OldPackage->IsDirty())
        return Fail(EP4OriginalLevelImportError::EditedOriginal, TEXT("Save your level changes before reimporting."));
    // Reloading a persistent map also reloads its streaming layers. Never let that discard
    // pending work in another layer of the current editor map.
    if (!P4KadanHasSavedLevelReimportScope())
        if (UWorld* EditorWorld = GEditor->GetEditorWorldContext().World())
            for (ULevel* Level : EditorWorld->GetLevels())
                if (Level && Level->GetOutermost()->IsDirty())
                    return Fail(EP4OriginalLevelImportError::EditedOriginal,
                        TEXT("Save your level changes before reimporting."));
    // A batch captures before any Blueprint can replace an actor's source components. A direct
    // single-level reimport has no dependency phase and captures at this boundary instead.
    TSharedPtr<FLevelReimportCapture> Capture = SavedLevelReimports.FindRef(PackageName);
    if (!Capture.IsValid())
    {
        if (P4KadanHasSavedLevelReimportScope())
            return Fail(EP4OriginalLevelImportError::RecordValidationRequired,
                TEXT("level-reimport-snapshot-unavailable: ") + PackageName);
        Capture = MakeShared<FLevelReimportCapture>();
        if (!CaptureLevelReimport(Previous, *Capture, Error))
            return Fail(EP4OriginalLevelImportError::RecordValidationRequired, Error);
    }
    if (!P4SameText(Capture->Baseline.Source, OldBaseline.Source) ||
        !P4SameText(Capture->Baseline.State, OldBaseline.State))
        return Fail(EP4OriginalLevelImportError::RecordValidationRequired,
            TEXT("level-reimport-snapshot-baseline-differs: ") + PackageName);
    const FP4LevelChangesResult& Edits = Capture->Edits;
    const TArray<FString>& PreviousLines = Capture->Lines;
    // Additions are duplicated from the live old actor. Refuse if a dependency changed any of its
    // cooked content since capture; never substitute the changed actor for the creator's saved data.
    if (Edits.Added.Num())
    {
        FFile Current;
        TArray<FString> CurrentLines, Engine;
        if (!Cooked(Previous, true, Current, Error))
            return Fail(EP4OriginalLevelImportError::CookFailed, Error);
        Lines(Current, CurrentLines, Engine);
        const FString LevelPath = Previous->GetName() + TEXT(".") + Previous->PersistentLevel->GetName();
        for (const FString& Added : Edits.Added)
        {
            TArray<FString> Before, After;
            for (const FString& Line : PreviousLines)
                if (OwnerOf(Line, LevelPath) == Added)
                    Before.Add(Line);
            for (const FString& Line : CurrentLines)
                if (OwnerOf(Line, LevelPath) == Added)
                    After.Add(Line);
            Before.Sort();
            After.Sort();
            if (Before != After)
                return Fail(EP4OriginalLevelImportError::RecordValidationRequired,
                    TEXT("level-reimport-added-actor-changed-during-dependency-import: ") + Added);
        }
    }
    TStrongObjectPtr<UWorld> KeepPrevious(Previous);
    TStrongObjectPtr<UPackage> KeepOldPackage(OldPackage);
    TStrongObjectPtr<UWorld> Fresh;
    const FString Retired = TEXT("/Temp/KadanReimport_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/") +
        Previous->GetName();
    const FString Staged = Retired + TEXT("_Stage");
    bool OldRetired = false;
    const auto RestoreIdentity = [&]() -> bool
    {
        UWorld* World = Fresh.Get();
        UPackage* NewPackage = World ? World->GetOutermost()
                                     : (OldRetired ? FindPackage(nullptr, *PackageName) : nullptr);
        if (NewPackage && NewPackage != OldPackage)
        {
            if (NewPackage->GetName() == PackageName &&
                !NewPackage->Rename(
                    *Staged, nullptr, REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional))
                return false;
            if (!World)
                World = UWorld::FindWorldInPackage(NewPackage);
            Fresh.Reset();
            if (IsValid(World))
                Discard(World);
            NewPackage->SetDirtyFlag(false);
        }
        if (OldRetired)
        {
            if (!OldPackage->Rename(
                    *PackageName, nullptr, REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional))
                return false;
            OldRetired = false;
            FAssetRegistryModule::AssetCreated(Previous);
            OldPackage->SetPackageFlagsTo(OriginalFlags);
        }
        return true;
    };
    ON_SCOPE_EXIT
    {
        if (OldRetired || Fresh.IsValid())
            ensure(RestoreIdentity());
    };
    ResetLoaders(OldPackage);
    if (!OldPackage->Rename(*Retired, nullptr, REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional))
        return Fail(EP4OriginalLevelImportError::BuildFailed, TEXT("level-reimport-could-not-stage-original-package"));
    OldRetired = true;
    // The private slot flag is reached only after provenance, source and edit validation above.
    // No file is removed: ordinary reconstruction and all normal source equality checks run.
    Result = ImportNative(HeaderFile, BodyFile, false, true);
    if (!Result.IsValid() && MayImproveProjection &&
        (Result.Error == EP4OriginalLevelImportError::UnsupportedContent ||
            Result.Error == EP4OriginalLevelImportError::BuildFailed ||
            Result.Error == EP4OriginalLevelImportError::CookDiffers ||
            (Result.Error == EP4OriginalLevelImportError::CookFailed &&
                Result.ErrorDetail.StartsWith(TEXT("level-file-level-script-variable-type-not-read: ")))))
    {
        const FString Reason = Result.ErrorDetail;
        // This namespace belongs only to our failed unsaved strict reconstruction.
        // Keep its generated classes alive; no parent or live dependency is retired.
        if (UPackage* Failed = FindPackage(nullptr, *PackageName))
        {
            if (Failed == OldPackage ||
                !Failed->Rename(*(Staged + TEXT("_Strict")), nullptr,
                    REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional))
                return Fail(
                    EP4OriginalLevelImportError::BuildFailed, TEXT("level-reimport-could-not-stage-strict-refusal"));
            Failed->SetDirtyFlag(false);
        }
        Result = ImportGameBacked(
            HeaderFile, BodyFile, false, *ProjectionDependencies, Reason, true, FString(), {}, true);
        if (Result.IsValid())
            for (const FString& Name : Result.UneditableOriginalActors)
                if (!PreviouslyOmitted.Contains(FName(*Name)))
                    return Fail(EP4OriginalLevelImportError::BuildFailed,
                        TEXT("level-reimport-projection-would-drop-an-editable-original: ") + Name);
    }
    if (!Result.IsValid())
        return Result;
    Fresh.Reset(Result.Asset);
    if (!Fresh.IsValid() || !ReplayLevelEdits(Previous, Fresh.Get(), *Capture, Error))
        return Fail(EP4OriginalLevelImportError::BuildFailed, Error);
    const FP4LevelChangesResult Replayed = AnalyzeChanges(Fresh.Get());
    if (!Replayed.IsValid() || Replayed.Removed != Edits.Removed || Replayed.Added != Edits.Added ||
        Replayed.Moved.Num() != Edits.Moved.Num())
        return Fail(EP4OriginalLevelImportError::BuildFailed,
            TEXT("level-reimport-creator-changes-differ: ") + Replayed.ErrorDetail);
    for (const FP4LevelActorMove& Move : Edits.Moved)
        if (!Replayed.Moved.ContainsByPredicate(
                [&](const FP4LevelActorMove& Actual)
                {
                    return Actual.Actor == Move.Actor && Actual.Location.Equals(Move.Location) &&
                        Actual.Rotation.Equals(Move.Rotation) && Actual.Scale.Equals(Move.Scale);
                }))
            return Fail(EP4OriginalLevelImportError::BuildFailed,
                TEXT("level-reimport-creator-transform-differs: ") + Move.Actor);
    FFile Merged;
    TArray<FString> MergedLines, MergedEngine;
    if (!Cooked(Fresh.Get(), true, Merged, Error))
        return Fail(EP4OriginalLevelImportError::CookFailed, Error);
    Lines(Merged, MergedLines, MergedEngine);
    // Verify the complete serialized content of each creator addition, independently of duplication.
    const FString LevelPath = Previous->GetName() + TEXT(".") + Previous->PersistentLevel->GetName();
    for (const FString& Added : Edits.Added)
    {
        TArray<FString> Before, After;
        for (const FString& Line : PreviousLines)
            if (OwnerOf(Line, LevelPath) == Added)
                Before.Add(Line);
        for (const FString& Line : MergedLines)
            if (OwnerOf(Line, LevelPath) == Added)
                After.Add(Line);
        Before.Sort();
        After.Sort();
        if (Before != After)
            return Fail(EP4OriginalLevelImportError::BuildFailed,
                TEXT("level-reimport-creator-actor-content-differs: ") + Added);
    }
    // Every changed property must still serialize to the creator's value, including a value
    // reset to a default (which can remove its serialized tag altogether).
    TSet<FString> ChangedKeys;
    const auto Keys = [&](const FP4LevelLineChanges& Changed)
    {
        TArray<FString> Both = Changed.Before;
        Both.Append(Changed.After);
        for (const FString& Line : Both)
        {
            FString Key, Value;
            if (Line.Split(TEXT(" = "), &Key, &Value))
                ChangedKeys.Add(Key + TEXT(" = "));
        }
    };
    for (const auto& Changed : Edits.OtherChanged)
        Keys(Changed.Lines);
    Keys(Edits.LevelChanges);
    for (const FString& Key : ChangedKeys)
    {
        const FString* Before = PreviousLines.FindByPredicate(
            [&](const FString& Line) { return Line.StartsWith(Key, ESearchCase::CaseSensitive); });
        const FString* After = MergedLines.FindByPredicate(
            [&](const FString& Line) { return Line.StartsWith(Key, ESearchCase::CaseSensitive); });
        if (bool(Before) != bool(After) || (Before && *Before != *After))
            return Fail(
                EP4OriginalLevelImportError::BuildFailed, TEXT("level-reimport-creator-property-differs: ") + Key);
    }
    const FString ExpectedState = Replayed.State;
    if (!P4SaveAssetPackage(
            Fresh.Get(),
            [&]()
            {
                const FString Actual = State(Fresh.Get());
                if (P4SameText(Actual, ExpectedState))
                    return true;
                const FString Delta = P4LevelChangesJson(AnalyzeChanges(Fresh.Get()));
                const FString Folder = FPaths::ProjectSavedDir() / TEXT("P4MapKit");
                IFileManager::Get().MakeDirectory(*Folder, true);
                FFileHelper::SaveStringToFile(Delta, *(Folder / (Fresh->GetName() + TEXT("-save-delta.json"))),
                    FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
                UE_LOG(LogTemp, Display, TEXT("Source level publication changed %s: expected=%s actual=%s delta=%s"),
                    *PackageName, *ExpectedState, *Actual, *Delta.Left(14000));
                return false;
            },
            Error))
        return Fail(EP4OriginalLevelImportError::SaveFailed, Error);
    // Reuse Unreal's package reload to update current maps, streaming layers and external references.
    // It handles renderer lifecycles; no running world/scene pointers are copied or restored.
    if (!RestoreIdentity())
        return Fail(
            EP4OriginalLevelImportError::SaveFailed, TEXT("level-reimport-could-not-restore-loaded-package-identity"));
    // Any dirty flag since the checked batch began came from import/reference fixups. The
    // creator delta has now been replayed, checked and saved in the replacement.
    OldPackage->SetDirtyFlag(false);
    // UEditorLoadingAndSavingUtils::SaveMap calls AssetCreated after its successful save, so a
    // real saved map can still carry NewlyCreated. The replacement's native save just passed;
    // it is now an existing disk package. Do not restore that stale creation bit into reload.
    // All other flags (including CompiledIn) keep their original admission behavior.
    OldPackage->ClearPackageFlags(PKG_NewlyCreated);
    ClearUnchangedPublishedLevelDirtyFlags();
    KeepPrevious.Reset();
    KeepOldPackage.Reset();
    FText ReloadError;
    const bool ReloadedAssets = UPackageTools::ReloadPackages(
        {OldPackage}, ReloadError, EReloadPackagesInteractionMode::AssumePositive);
    UPackage* Reloaded = FindPackage(nullptr, *PackageName);
    UWorld* World = Reloaded ? UWorld::FindWorldInPackage(Reloaded) : nullptr;
    // UE4.26 removes the current map from its generic package list, opens it separately, then
    // returns only the generic list's result. A current-map-only reload can therefore return false
    // without an error. Admit that path only after proving actual world replacement and below,
    // the complete saved source+creator state. A generic failure or unchanged old world still fails.
    const bool ReloadedCurrentMap = WasCurrentEditorMap && ReloadError.IsEmpty() && World && World != Previous &&
        World != PreviousIdentity.Get() && GEditor->GetEditorWorldContext().World() == World &&
        World->GetOutermost()->GetName() == PackageName;
    if (!ReloadedAssets && !ReloadedCurrentMap)
        return Fail(
            EP4OriginalLevelImportError::SaveFailed, TEXT("level-reimport-reload-failed: ") + ReloadError.ToString());
    if (!World || !P4SameText(State(World), ExpectedState))
        return Fail(EP4OriginalLevelImportError::SaveFailed, TEXT("level-reimport-saved-creator-state-differs"));
    Capture->PublishedState = ExpectedState;
    ClearUnchangedPublishedLevelDirtyFlags();
    Result.Asset = World;
    Result.Created = false;
    Result.Saved = true;
    Result.State = ExpectedState;
    return Result;
}
} // namespace P4LevelContent

FP4OriginalLevelImportResult P4ReimportOriginalLevelNative(
    UWorld* World, const FString& HeaderFile, const FString& BodyFile, bool SaveToDisk)
{
    return P4LevelContent::ReimportNative(World, HeaderFile, BodyFile, SaveToDisk);
}

FP4OriginalLevelImportResult P4ReimportOriginalLevelWithGameReferencesNative(UWorld* World, const FString& HeaderFile,
    const FString& BodyFile, bool SaveToDisk, const TSet<FName>& UnavailablePackages)
{
    return P4LevelContent::ReimportNative(World, HeaderFile, BodyFile, SaveToDisk, &UnavailablePackages);
}

bool P4CaptureSavedLevelReimport(UWorld* World, FString& Error)
{
    return P4LevelContent::CaptureSavedLevelReimport(World, Error);
}

void P4ClearSavedLevelReimports()
{
    P4LevelContent::ClearUnchangedPublishedLevelDirtyFlags();
    P4LevelContent::SavedLevelReimports.Empty();
}

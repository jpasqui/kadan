// The record an imported level keeps of what the game's file said, and what was changed since.

#include "P4LevelContentInternal.h"
#include "P4Common.h"

namespace P4LevelContent
{
FString State(UWorld* World)
{
    if (!IsInGameThread() || !IsValid(World) || !World->PersistentLevel ||
        World->GetOutermost() == GetTransientPackage())
        return FString();
    FFile File;
    FString Error, Out;
    TArray<FString> Content, EngineMade;
    if (!Cooked(World, true, File, Error))
    {
        if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_STATE_TRACE")) == TEXT("1"))
            UE_LOG(LogTemp, Display, TEXT("P4LevelState cook refused %s: %s"), *World->GetPathName(), *Error);
        return FString();
    }
    Lines(File, Content, EngineMade);
    WithoutEditorNavigation(World, Content);
    WithEditorDroppedStreams(World, Content);
    WithEditorNavLinkIds(World, Content);
    WithEditorLandscapeMaterials(World, Content);
    WithInheritedAnimationClasses(World, Content);
    WithInheritedRuleTableDefaults(World, Content);
    WithoutAddedNativeTransformTracking(World, Content);
    TArray<FString> ImportedVisibility;
    if (ReadVisibilityBaseline(World, ImportedVisibility))
    {
        P4WithDroppedVisibilityIds(World, ImportedVisibility, Content);
        P4WithImportedStreamingTextureData(ImportedVisibility, Content);
    }
    return Digest(Content, Out) ? Out : FString();
}

FString BaselineText(const FBaseline& Value)
{
    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetNumberField(TEXT("format"), 1);
    Json->SetStringField(TEXT("owner"), BaselineOwner);
    Json->SetStringField(TEXT("package"), Value.Package);
    Json->SetStringField(TEXT("source_sha256"), Value.Source);
    Json->SetStringField(TEXT("state"), Value.State);
    FString Out = P4CondensedJson(Json);
    return Out;
}

bool ReadBaseline(UWorld* World, FBaseline& Out)
{
    const FString Record = World->GetOutermost()->GetMetaData()->GetValue(World, BaselineKey);
    TSharedPtr<FJsonObject> Json;
    FString Owner;
    double Format = 0;
    if (Record.IsEmpty() || !P4ParseJson(Record, Json))
        return false;
    if (!Json->TryGetNumberField(TEXT("format"), Format) || Format != 1 ||
        !Json->TryGetStringField(TEXT("owner"), Owner) || !P4SameText(Owner, BaselineOwner) ||
        !Json->TryGetStringField(TEXT("package"), Out.Package) ||
        !Json->TryGetStringField(TEXT("source_sha256"), Out.Source) ||
        !Json->TryGetStringField(TEXT("state"), Out.State))
        return false;
    return P4IsSha256Hex(Out.Source) && P4IsSha256Hex(Out.State) &&
        P4SameText(Out.Package, World->GetOutermost()->GetName());
}

// Validates the imported visibility comparison source.
bool ReadVisibilityBaseline(UWorld* World, TArray<FString>& Lines)
{
    FBaseline Baseline;
    FString Check;
    return ReadBaseline(World, Baseline) &&
        Unpack(World->GetOutermost()->GetMetaData()->GetValue(World, ImportedKey), Lines) && Digest(Lines, Check) &&
        P4SameText(Check, Baseline.State);
}

// The post-process order a level's import kept, and the source digest of the level's import it has to be
// bound to. A level without either is refused by name: its order is never made up.
bool KeptPostProcessOrder(UObject* Object, FString& Json, FString& Source, FString& Error)
{
    UWorld* World = Cast<UWorld>(Object);
    FBaseline Baseline;
    if (!World)
    {
        Error = TEXT("postprocess-cook-kept-order-needs-a-level");
        return false;
    }
    if (!World->GetOutermost()->GetMetaData()->HasValue(World, PostProcessOrderKey))
    {
        Error = TEXT("postprocess-cook-kept-order-missing");
        return false;
    }
    if (!ReadBaseline(World, Baseline))
    {
        Error = TEXT("postprocess-cook-kept-order-level-baseline-unreadable");
        return false;
    }
    Json = World->GetOutermost()->GetMetaData()->GetValue(World, PostProcessOrderKey);
    Source = Baseline.Source;
    return true;
}

bool HashGameBackedSourceFiles(const FString& StoredHeader, const FString& StoredBody, FString& Digest)
{
    const FString Header = P4ResolveSourceCustodyPath(StoredHeader), Body = P4ResolveSourceCustodyPath(StoredBody);
    return P4HashByteStream(
        [&](const TFunction<bool(const uint8*, int32)>& Update)
        {
            for (const FString& Path : {Header, Body})
            {
                if (Path.IsEmpty() || FPaths::IsRelative(Path))
                    return false;
                TUniquePtr<FArchive> File(IFileManager::Get().CreateFileReader(*Path));
                if (!File || File->TotalSize() < 0 || File->TotalSize() > 64 * 1024 * 1024)
                    return false;
                uint8 Bytes[64 * 1024];
                while (File->Tell() < File->TotalSize())
                {
                    const int32 Count = int32(FMath::Min<int64>(sizeof(Bytes), File->TotalSize() - File->Tell()));
                    File->Serialize(Bytes, Count);
                    if (File->IsError() || !Update(Bytes, Count))
                        return false;
                }
                if (File->IsError())
                    return false;
            }
            return true;
        },
        Digest);
}

bool ReadGameBackedCustody(
    UWorld* World, TSet<FName>& Omitted, FString& Source, FString& Error, const FP4OriginalWorldSource* SelectedSource)
{
    Omitted.Reset();
    Source.Reset();
    Error.Reset();
    if (!IsValid(World))
    {
        Error = TEXT("game-backed-level-required");
        return false;
    }
    const FString TextValue = World->GetOutermost()->GetMetaData()->GetValue(World, GameBackedKey);
    FP4OriginalRecord Record;
    FString RecordError;
    const bool Marked = P4ReadOriginalRecord(World, Record, RecordError) &&
        Record.ComparisonSchema == TEXT("game-backed-level-lines-v1");
    if (TextValue.IsEmpty())
    {
        if (Marked)
        {
            Error = TEXT("game-backed-source-custody-missing");
            return false;
        }
        return true;
    }
    TSharedPtr<FJsonObject> Json;
    FString Package, Format;
    double Version = 0;
    const TArray<TSharedPtr<FJsonValue>>* Actors = nullptr;
    FBaseline Baseline;
    if (!Marked || !P4ParseJson(TextValue, Json) || !Json->TryGetStringField(TEXT("format"), Format) ||
        Format != TEXT("p4-game-backed-level") || !Json->TryGetNumberField(TEXT("version"), Version) || Version != 1 ||
        !Json->TryGetStringField(TEXT("package"), Package) || !P4SameText(Package, World->GetOutermost()->GetName()) ||
        !Json->TryGetStringField(TEXT("source_sha256"), Source) || !P4IsSha256Hex(Source) ||
        !ReadBaseline(World, Baseline) || !P4SameText(Source, Baseline.Source) ||
        !P4SameText(Source, Record.SourceDigest) || !P4SameText(Baseline.State, Record.ComparisonDigest) ||
        Record.Fidelity != TEXT("game-backed-overlay") || !Json->TryGetArrayField(TEXT("actors"), Actors))
    {
        Error = TEXT("game-backed-source-custody-differs");
        return false;
    }
    TSet<FName> Names;
    FString CustodyDigest;
    bool Bound = false;
    if (!P4HashUtf8(TextValue, CustodyDigest))
    {
        Error = TEXT("game-backed-custody-digest-unreadable");
        return false;
    }
    for (const auto& Note : Record.Values)
        if (Note.Kind == TEXT("game-backed-custody") && P4SameText(Note.GameValue, CustodyDigest))
            Bound = true;
    if (!Bound)
    {
        Error = TEXT("game-backed-actor-custody-digest-differs");
        return false;
    }
    FString Header, Body, CurrentSource;
    if (!Json->TryGetStringField(TEXT("source_header"), Header) || !Json->TryGetStringField(TEXT("source_body"), Body))
    {
        Error = TEXT("game-backed-original-source-missing-or-changed");
        return false;
    }
    if (SelectedSource)
    {
        if (!P4SameText(SelectedSource->Package(), Package) || !P4SameText(SelectedSource->SourceDigest(), Source) ||
            !SelectedSource->Current(Error))
        {
            if (Error.IsEmpty())
                Error = TEXT("game-backed-selected-original-source-differs");
            return false;
        }
        CurrentSource = SelectedSource->SourceDigest();
    }
    else if (!HashGameBackedSourceFiles(Header, Body, CurrentSource) || !P4SameText(CurrentSource, Source))
    {
        Error = TEXT("game-backed-original-source-missing-or-changed");
        return false;
    }
    for (const auto& Actor : *Actors)
    {
        const auto Row = Actor->AsObject();
        FString Name, Class;
        bool Editable = false;
        double Export = 0;
        if (!Row.IsValid() || !Row->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty() ||
            !Row->TryGetStringField(TEXT("class"), Class) || Class.IsEmpty() ||
            !Row->TryGetBoolField(TEXT("editable"), Editable) ||
            !Row->TryGetNumberField(TEXT("source_export"), Export) || Export < 1 || Names.Contains(FName(*Name)))
        {
            Error = TEXT("game-backed-actor-custody-unreadable");
            return false;
        }
        Names.Add(FName(*Name));
        if (!Editable)
            Omitted.Add(FName(*Name));
    }
    return true;
}

// Relocate only proven source bookkeeping. Actor rows, source and comparison
// digests and baseline content remain exactly as before. A normal save, never
// resume itself, persists this dirty metadata alongside any creator edits.
bool RetargetGameBackedSource(UWorld* World, const FP4OriginalWorldSource& Selected, FString& Error)
{
    TSet<FName> Omitted;
    FString Source;
    if (!ReadGameBackedCustody(World, Omitted, Source, Error, &Selected))
        return false;
    UPackage* Package = World->GetOutermost();
    UMetaData* Meta = Package->GetMetaData();
    const FString OldCustody = Meta->GetValue(World, GameBackedKey),
                  OldRecord = Meta->GetValue(World, TEXT("P4OriginalRecord"));
    TSharedPtr<FJsonObject> Custody, Record;
    if (!P4ParseJson(OldCustody, Custody) || !P4ParseJson(OldRecord, Record) || !Custody || !Record)
    {
        Error = TEXT("game-backed-source-location-record-unreadable");
        return false;
    }
    const FString PortableHeader = P4PortableSourceCustodyPath(Selected.HeaderPath()),
                  PortableBody = P4PortableSourceCustodyPath(Selected.BodyPath());
    if (Custody->GetStringField(TEXT("source_header")) == PortableHeader &&
        Custody->GetStringField(TEXT("source_body")) == PortableBody)
        return Selected.Current(Error);
    FString OldHash, NewHash, NewCustody, NewRecord;
    if (!P4HashUtf8(OldCustody, OldHash))
    {
        Error = TEXT("game-backed-source-location-digest-unreadable");
        return false;
    }
    Custody->SetStringField(TEXT("source_header"), PortableHeader);
    Custody->SetStringField(TEXT("source_body"), PortableBody);
    if (!FJsonSerializer::Serialize(
            Custody.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&NewCustody)) ||
        !P4HashUtf8(NewCustody, NewHash))
    {
        Error = TEXT("game-backed-source-location-digest-unreadable");
        return false;
    }
    const TArray<TSharedPtr<FJsonValue>>* Notes = nullptr;
    int32 Matches = 0;
    if (!Record->TryGetArrayField(TEXT("values"), Notes))
    {
        Error = TEXT("game-backed-source-location-notes-missing");
        return false;
    }
    for (const auto& Value : *Notes)
    {
        const TSharedPtr<FJsonObject>* Note = nullptr;
        FString Kind, Hash, Object, Action;
        if (!Value || !Value->TryGetObject(Note) || !Note || !(*Note))
        {
            Error = TEXT("game-backed-source-location-note-unreadable");
            return false;
        }
        if ((*Note)->TryGetStringField(TEXT("kind"), Kind) && Kind == TEXT("game-backed-custody") &&
            (*Note)->TryGetStringField(TEXT("game_value"), Hash) && P4SameText(Hash, OldHash))
        {
            if (!(*Note)->TryGetStringField(TEXT("object"), Object) || !P4SameText(Object, World->GetPathName()) ||
                !(*Note)->TryGetStringField(TEXT("action"), Action) || Action != TEXT("preserve-uneditable-originals"))
            {
                Error = TEXT("game-backed-source-location-note-identity-differs");
                return false;
            }
            (*Note)->SetStringField(TEXT("game_value"), NewHash);
            ++Matches;
        }
    }
    if (Matches != 1 ||
        !FJsonSerializer::Serialize(
            Record.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&NewRecord)))
    {
        Error = TEXT("game-backed-source-location-note-ambiguous");
        return false;
    }
    const bool WasDirty = Package->IsDirty();
    Meta->SetValue(World, GameBackedKey, *NewCustody);
    Meta->SetValue(World, TEXT("P4OriginalRecord"), *NewRecord);
    if (!Selected.Current(Error) || !ReadGameBackedCustody(World, Omitted, Source, Error))
    {
        Meta->SetValue(World, GameBackedKey, *OldCustody);
        Meta->SetValue(World, TEXT("P4OriginalRecord"), *OldRecord);
        Package->SetDirtyFlag(WasDirty);
        return false;
    }
    World->MarkPackageDirty();
    return true;
}

FString Status(UWorld* World)
{
    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetBoolField(TEXT("original"), false);
    Json->SetBoolField(TEXT("unchanged"), false);
    if (!IsInGameThread())
        return Finish(Json, TEXT("level-status-requires-editor-thread"));
    if (!IsValid(World))
        return Finish(Json, TEXT("level-status-needs-a-level"));
    Json->SetStringField(TEXT("asset"), World->GetPathName());
    FBaseline Baseline;
    {
        TArray<FString> NotHeld;
        World->GetOutermost()->GetMetaData()->GetValue(World, NotHeldKey).ParseIntoArrayLines(NotHeld);
        Json->SetNumberField(TEXT("values_this_engine_cannot_hold"), NotHeld.Num());
    }
    const FP4OriginalStatus Status = P4QueryOriginalAsset(World);
    P4OriginalStatusFields(Status, *Json);
    return Finish(Json, Status.Error);
}

// Whose a line is: the name of an actor of the level at Level (its path below the package), or nothing
// for the level's own. A child actor's lines are told under the component that makes it, so they are
// its parent's.
FString OwnerOf(const FString& Line, const FString& Level)
{
    const int32 Bar = Line.Find(TEXT(" | "), ESearchCase::CaseSensitive);
    const FString Path = Bar == INDEX_NONE ? Line : Line.Left(Bar);
    if (!Path.StartsWith(Level + TEXT("."), ESearchCase::CaseSensitive))
        return FString();
    FString Name = Path.Mid(Level.Len() + 1);
    int32 Dot = INDEX_NONE;
    if (Name.FindChar(TEXT('.'), Dot))
        Name.LeftInline(Dot);
    return Name;
}

// What an edit to an imported level amounts to, in the terms a list of level changes holds (the mod
// loader's levels.ini): actors of the game's level taken out, actors moved, actors added, and what is
// neither: an actor of the game's with another change, or a change to the level itself.
FP4LevelChangesResult AnalyzeChanges(UWorld* World)
{
    FP4LevelChangesResult Result;
    const auto Fail = [&](EP4LevelChangesError Code, const FString& Detail = FString())
    {
        Result.Error = Code;
        Result.ErrorDetail = Detail;
        return Result;
    };
    if (!IsInGameThread())
        return Fail(EP4LevelChangesError::EditorThreadRequired);
    if (!IsValid(World) || !World->PersistentLevel)
        return Fail(EP4LevelChangesError::LevelRequired);
    FBaseline Baseline;
    TArray<FString> Before, After, EngineMade;
    FString Check, Error;
    if (!ReadBaseline(World, Baseline))
        return Fail(EP4LevelChangesError::ImportedLevelRequired);
    if (!Unpack(World->GetOutermost()->GetMetaData()->GetValue(World, ImportedKey), Before))
        return Fail(
            EP4LevelChangesError::ImportedComparisonRequired, TEXT("imported-level-lines-could-not-be-unpacked"));
    if (!Digest(Before, Check))
        return Fail(EP4LevelChangesError::ImportedComparisonRequired, TEXT("imported-level-lines-digest-unreadable"));
    if (!P4SameText(Check, Baseline.State))
        return Fail(EP4LevelChangesError::ImportedComparisonRequired,
            TEXT("imported-level-lines-digest-differs: ") + Check + TEXT(" expected ") + Baseline.State);
    TSet<FName> Omitted;
    FString SourceCustody;
    if (!ReadGameBackedCustody(World, Omitted, SourceCustody, Error))
        return Fail(EP4LevelChangesError::ImportedComparisonRequired, Error);
    {
        FFile File;
        if (!Cooked(World, true, File, Error))
            return Fail(EP4LevelChangesError::CookFailed, Error);
        Lines(File, After, EngineMade);
    }
    WithoutEditorNavigation(World, After, &Result.EditorNavigation);
    WithEditorDroppedStreams(World, After);
    WithEditorNavLinkIds(World, After);
    WithEditorLandscapeMaterials(World, After);
    WithInheritedAnimationClasses(World, After);
    WithInheritedRuleTableDefaults(World, After);
    WithoutAddedNativeTransformTracking(World, After);
    P4WithDroppedVisibilityIds(World, Before, After);
    P4WithImportedStreamingTextureData(Before, After);
    FString CurrentState;
    if (!Digest(After, CurrentState))
        return Fail(EP4LevelChangesError::StateUnreadable);
    Result.State = CurrentState;
    // A line names an object by its path below the package: the world, its level, the actor.
    const FString Package = World->GetOutermost()->GetName(),
                  Level = World->GetName() + TEXT(".") + World->PersistentLevel->GetName();
    Result.Package = Package;
    Result.Level = FPackageName::GetLongPackageAssetName(Package);
    // Whose a line is: the actor's name, or nothing for the level's own. A child actor's lines are told
    // under the component that makes it, so they are its parent's.
    const auto Owner = [&](const FString& Line) { return OwnerOf(Line, Level); };
    TSet<FString, FLocKeySetFuncs> Was(Before), Is(After);
    TSet<FString, FLocKeySetFuncs> ActorsBefore, ActorsAfter;
    for (const FString& Line : Before)
    {
        const FString Name = Owner(Line);
        if (!Name.IsEmpty())
            ActorsBefore.Add(Name);
    }
    for (const FString& Line : After)
    {
        const FString Name = Owner(Line);
        if (!Name.IsEmpty())
            ActorsAfter.Add(Name);
    }
    for (const FString& Name : ActorsBefore)
        if (Omitted.Contains(FName(*Name)))
            return Fail(EP4LevelChangesError::ImportedComparisonRequired,
                TEXT("game-backed-omitted-actor-entered-editable-baseline: ") + Name);
    for (const FString& Name : ActorsAfter)
        if (Omitted.Contains(FName(*Name)))
            return Fail(EP4LevelChangesError::ImportedComparisonRequired,
                TEXT("This name belongs to an uneditable original game actor: ") + Name);
    TMap<FString, TArray<FString>> Gone, New;
    TArray<FString> LevelGone, LevelNew;
    // The level's list of its actors follows from which actors there are.
    const FString ListStart = Level + TEXT(" | actors ");
    for (const FString& Line : Before)
        if (!Is.Contains(Line) && !Line.StartsWith(ListStart, ESearchCase::CaseSensitive))
        {
            const FString Name = Owner(Line);
            if (Name.IsEmpty())
                LevelGone.Add(Line);
            else
                Gone.FindOrAdd(Name).Add(Line);
        }
    for (const FString& Line : After)
        if (!Was.Contains(Line) && !Line.StartsWith(ListStart, ESearchCase::CaseSensitive))
        {
            const FString Name = Owner(Line);
            if (Name.IsEmpty())
                LevelNew.Add(Line);
            else
                New.FindOrAdd(Name).Add(Line);
        }
    TArray<FString> Removed, Added, Touched;
    for (const FString& Name : ActorsBefore)
        if (!ActorsAfter.Contains(Name))
            Removed.Add(Name);
    for (const FString& Name : ActorsAfter)
        if (!ActorsBefore.Contains(Name))
            Added.Add(Name);
    for (const auto& Pair : Gone)
        if (ActorsAfter.Contains(Pair.Key))
            Touched.AddUnique(Pair.Key);
    for (const auto& Pair : New)
        if (ActorsBefore.Contains(Pair.Key))
            Touched.AddUnique(Pair.Key);
    Removed.Sort();
    Added.Sort();
    Touched.Sort();
    // The actor list is derived from actor lines. Its serialization may differ
    // even when every actor line agrees, so the normalized line differences
    // decide whether the level has an edit.
    Result.Unchanged = !Gone.Num() && !New.Num() && !LevelGone.Num() && !LevelNew.Num() && !Removed.Num() &&
        !Added.Num();
    // A removed actor is gone from the editor, so what it was is read from the level as imported: its class,
    // and the placement number by which the game's placement lists hold a gameplay object.
    for (const FString& Name : Removed)
    {
        FP4RemovedActor Record;
        const FString At = Level + TEXT(".") + Name + TEXT(" | ");
        for (const FString& Line : Before)
        {
            if (!Line.StartsWith(At, ESearchCase::CaseSensitive))
                continue;
            const FString Said = Line.Mid(At.Len());
            int32 Comma = INDEX_NONE;
            if (Said.StartsWith(TEXT("class "), ESearchCase::CaseSensitive) && Said.FindLastChar(TEXT(','), Comma))
                Record.Class = Said.Mid(6, Comma - 6);
            else if (Said.StartsWith(TEXT("generateinfo = "), ESearchCase::CaseSensitive))
            {
                const std::string Json = TCHAR_TO_UTF8(*Said.Mid(15));
                std::size_t Read = 0;
                FDecodedValue Info;
                if (!P4MapKit::ReadDecodedJson(Json, Read, Info) || Info.Kind != FDecodedValue::EKind::Object)
                    continue;
                for (const auto& Member : Info.Fields)
                    if (FString(UTF8_TO_TCHAR(Member.first.c_str()))
                            .Equals(TEXT("DebugUniqueId"), ESearchCase::IgnoreCase) &&
                        Member.second.Kind == FDecodedValue::EKind::Number && !Member.second.Text.empty() &&
                        Member.second.Text[0] != '-')
                        Record.PlacementNumber = uint64(std::strtoull(Member.second.Text.c_str(), nullptr, 10));
            }
        }
        Result.RemovedActors.Add(Name, Record);
    }
    // What a mod does not build again: the game's navigation data and its built lighting stay as the
    // game has them. So that this can be told, the actors a change concerns are sorted by whether
    // navigation takes them into account and whether they are lit by a lighting build.
    TArray<FString> Walked, LitWhereTheyWere, NotLit;
    const auto Concerns = [&](const FString& Name, bool New)
    {
        const AActor* Actor = FindObject<AActor>(World->PersistentLevel, *Name);
        if (!Actor)
            return;
        bool Navigation = false, Lighting = false;
        TInlineComponentArray<UPrimitiveComponent*> Parts(Actor);
        for (const UPrimitiveComponent* Part : Parts)
        {
            Navigation = Navigation || Part->IsNavigationRelevant();
            Lighting = Lighting || Part->HasStaticLighting();
        }
        if (Navigation)
            Walked.Add(Name);
        if (New && Lighting)
            NotLit.AddUnique(Name);
    };
    for (const FString& Name : Added)
        Concerns(Name, true);
    for (const FString& Name : Touched)
    {
        Concerns(Name, false);
        const TArray<FString> Nothing;
        const TArray<FString>& Lost = Gone.Contains(Name) ? Gone[Name] : Nothing;
        const TArray<FString>& Got = New.Contains(Name) ? New[Name] : Nothing;
        // Moved and nothing else: every line that differs is where the actor's root component is.
        AActor* Actor = FindObject<AActor>(World->PersistentLevel, *Name);
        const USceneComponent* Root = Actor ? Actor->GetRootComponent() : nullptr;
        const FString RootAt = Root ? Level + TEXT(".") + Name + TEXT(".") + Root->GetName() + TEXT(" | ") : FString();
        bool Only = Root && Root->GetOuter() == Actor && !Root->GetAttachParent();
        for (const TArray<FString>* List : {&Lost, &Got})
            for (const FString& Line : *List)
                Only = Only && Line.StartsWith(RootAt, ESearchCase::CaseSensitive) &&
                    (Line.Mid(RootAt.Len()).StartsWith(TEXT("relativelocation = "), ESearchCase::CaseSensitive) ||
                        Line.Mid(RootAt.Len()).StartsWith(TEXT("relativerotation = "), ESearchCase::CaseSensitive) ||
                        Line.Mid(RootAt.Len()).StartsWith(TEXT("relativescale3d = "), ESearchCase::CaseSensitive));
        if (Only)
        {
            // The place of a root that is attached to nothing is the actor's place in the world, in the
            // numbers the level holds.
            const FVector At = Root->GetRelativeLocation(), Scale = Root->GetRelativeScale3D();
            const FRotator Turn = Root->GetRelativeRotation();
            FP4LevelActorMove Move;
            Move.Actor = Name;
            Move.Location = At;
            Move.Rotation = Turn;
            Move.Scale = Scale;
            Result.Moved.Add(MoveTemp(Move));
        }
        else
        {
            FP4LevelActorChanges Change;
            Change.Actor = Name;
            Change.Lines.Before = Lost;
            Change.Lines.After = Got;
            Result.OtherChanged.Add(MoveTemp(Change));
        }
    }
    FP4OriginalLevelLighting OriginalLighting;
    FString LightingError;
    if (Removed.Num() || Touched.Num())
    {
        if (!P4ReadOriginalLevelLighting(World, OriginalLighting, LightingError))
        {
            Result.Error = EP4LevelChangesError::ImportedComparisonRequired;
            Result.ErrorDetail = LightingError;
            // Preserve the actual cook delta that led to the missing-lighting guard;
            // otherwise an import/reload regression is hidden by the metadata error.
            for (int32 Index = 0; Index < FMath::Min(4, Touched.Num()); ++Index)
            {
                const FString& Name = Touched[Index];
                Result.ErrorDetail += TEXT("; changed actor ") + Name;
                if (const auto* Lost = Gone.Find(Name))
                    if (Lost->Num())
                        Result.ErrorDetail += TEXT("; no longer says ") + (*Lost)[0].Left(1000);
                if (const auto* Got = New.Find(Name))
                    if (Got->Num())
                        Result.ErrorDetail += TEXT("; now says ") + (*Got)[0].Left(1000);
            }
            if (Removed.Num())
                Result.ErrorDetail += TEXT("; removed actor ") + Removed[0];
            return Result;
        }
        for (const FString& Name : Removed)
            if (OriginalLighting.Actors.Contains(Name))
                LitWhereTheyWere.AddUnique(Name);
        for (const FString& Name : Touched)
            if (OriginalLighting.Actors.Contains(Name))
                LitWhereTheyWere.AddUnique(Name);
    }
    Result.Removed = MoveTemp(Removed);
    Result.Added = MoveTemp(Added);
    Result.LevelChanges.Before = MoveTemp(LevelGone);
    Result.LevelChanges.After = MoveTemp(LevelNew);
    Result.NavigationRelevant = MoveTemp(Walked);
    Result.LitByGame = MoveTemp(LitWhereTheyWere);
    Result.AddedUnlit = MoveTemp(NotLit);
    // Which of the level's own objects holds its world settings, so that a change there can be told as one.
    if (const AWorldSettings* Settings = World->PersistentLevel->GetWorldSettings(false))
        Result.WorldSettings = Settings->GetName();
    // A list of level changes holds removals, moves and the mod's own sublevels. It does not hold another
    // change to one of the game's actors, or a change to the level itself.
    Result.HeldByLevelChanges = Result.OtherChanged.Num() == 0 && Result.LevelChanges.Before.Num() == 0 &&
        Result.LevelChanges.After.Num() == 0;
    return Result;
}

FString Changes(UWorld* World)
{
    return P4LevelChangesJson(AnalyzeChanges(World));
}
} // namespace P4LevelContent

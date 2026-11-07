// The checks of what an import made: levels that change when their map opens, and the class defaults
// a strict import has to prove.

#include "P4KadanImportInternal.h"
#include "P4GamePaths.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/App.h"
#include "UObject/UObjectIterator.h"

using namespace P4KadanImport;

namespace
{
// Whether a level of the project's has edits that are not saved: opening another map in the editor drops them.
bool UnsavedLevels()
{
    for (TObjectIterator<UWorld> It; It; ++It)
    {
        const UPackage* Package = It->GetOutermost();
        if (Package->IsDirty() && !It->HasAnyFlags(RF_Transient) && P4IsContentPackageIgnoringCase(Package->GetName()))
            return true;
    }
    return false;
}

// Before the open check replaces the editor's world: the person saves their unsaved levels or keeps them, which skips
// the check. Nothing is asked when no one can answer; the check is then skipped. Previous is the open map's package,
// opened again afterwards, or empty when the open world was never saved.
bool MayOpenOtherMaps(FString& Previous, FString& Why)
{
    if (UnsavedLevels())
    {
        const bool CanAsk = !FApp::IsUnattended() && !GIsAutomationTesting && FSlateApplication::IsInitialized();
        if (!CanAsk || !FEditorFileUtils::SaveDirtyPackages(true, true, false, false, false, true) || UnsavedLevels())
        {
            Why = TEXT("Your open levels have unsaved edits, so the imported maps were not opened to check them.");
            return false;
        }
    }
    const UWorld* Open = GEditor->GetEditorWorldContext().World();
    const FString Package = Open ? Open->GetOutermost()->GetName() : FString();
    Previous = FPackageName::DoesPackageExist(Package) ? Package : FString();
    return true;
}
} // namespace

// Best effort: open each imported map once, as the person will, and look at the levels this run
// made. A level the editor changes when its map opens (a child actor made again without its parts)
// would start edited and could not be exported unchanged. It is made again game-backed, and the
// actors that changed stay the game's by their classes. Strict imports never come here.
void FP4KadanImportRun::ReimportLevelsChangedWhenOpened()
{
    if (BestEffort && StrictMade.Num() && !Cancelled && !FatalFailure && !ReportWriteFailed && GEditor)
    {
        const double OpenStarted = FPlatformTime::Seconds();
        TMap<FString, TSet<FString>> Unstable;
        TMap<FString, FString> UnstableWhy, UnstableDetails;
        FindLevelsChangedWhenOpened(Unstable, UnstableWhy, UnstableDetails);
        for (const auto& Pair : Unstable)
        {
            const TSharedPtr<FP4KadanSourceAsset>& Source = StrictMade[Pair.Key];
            UPackage* Loaded = FindPackage(nullptr, *Pair.Key);
            FString LevelFile;
            if ((Loaded && !UPackageTools::UnloadPackages({Loaded})) ||
                !FPackageName::TryConvertLongPackageNameToFilename(
                    Pair.Key, LevelFile, FPackageName::GetMapPackageExtension()) ||
                !IFileManager::Get().Delete(*LevelFile, false, true, true))
            {
                Warnings.AddUnique(Pair.Key + TEXT(" changes when its map opens (") + UnstableWhy[Pair.Key] +
                    TEXT("), and it could not be made again game-backed."));
                continue;
            }
            const FP4OriginalLevelImportResult Again = P4ImportGameBackedLevelNative(Source->Header, Source->Body, true,
                GameBackedPackages,
                TEXT("The editor changes these actors when the map opens: ") + UnstableWhy[Pair.Key], true, Pair.Value);
            const TSharedPtr<FJsonObject>* Reply = nullptr;
            if (Replies->TryGetObjectField(Pair.Key, Reply))
            {
                (*Reply)->SetBoolField(TEXT("valid"), Again.IsValid());
                (*Reply)->SetBoolField(TEXT("game_backed"), Again.IsValid() && Again.GameBacked);
                (*Reply)->SetStringField(TEXT("changes_when_opened"), UnstableWhy[Pair.Key]);
                (*Reply)->SetStringField(TEXT("changes_when_opened_details"), UnstableDetails[Pair.Key]);
                if (Again.ActorCountsKnown)
                {
                    (*Reply)->SetNumberField(TEXT("original_actor_count"), Again.OriginalActorCount);
                    (*Reply)->SetNumberField(TEXT("editable_original_actor_count"), Again.EditableOriginalActorCount);
                }
                (*Reply)->SetNumberField(TEXT("uneditable_original_actor_count"), Again.UneditableOriginalActors.Num());
                const auto Words = [&](const TCHAR* Key, const TArray<FString>& Values)
                {
                    TArray<TSharedPtr<FJsonValue>> Array;
                    for (const FString& Value : Values)
                        Array.Add(MakeShared<FJsonValueString>(Value));
                    (*Reply)->SetArrayField(Key, Array);
                };
                Words(TEXT("uneditable_original_actors"), Again.UneditableOriginalActors);
                Words(TEXT("class_problems"), Again.ClassProblems);
                Words(TEXT("values_not_held"), Again.ValuesNotHeld);
                if (Again.GameBacked)
                    (*Reply)->SetStringField(TEXT("game_backed_reason"), Again.GameBackedReason);
                if (!Again.IsValid())
                {
                    (*Reply)->SetStringField(TEXT("game_backed_error"), Again.ErrorDetail);
                    (*Reply)->SetStringField(TEXT("error"), Again.ErrorDetail);
                }
            }
            if (Again.IsValid())
                Warnings.AddUnique(FString::Printf(
                    TEXT(
                        "%s keeps %d original actors in the game, because the editor changes them when the map opens (%s)."),
                    *Pair.Key, Again.UneditableOriginalActors.Num(), *UnstableWhy[Pair.Key]));
            else
            {
                Problems.Add(MakeShared<FJsonValueString>(Pair.Key +
                    TEXT(" changes when its map opens and could not be made again game-backed: ") + Again.ErrorDetail));
                Out.NotImported.AddUnique(Pair.Key);
                Out.Imported.Remove(Pair.Key);
            }
        }
        Spent(TEXT("open check"), OpenStarted);
        Checkpoint();
    }
}

void FP4KadanImportRun::FindLevelsChangedWhenOpened(TMap<FString, TSet<FString>>& Unstable,
    TMap<FString, FString>& UnstableWhy, TMap<FString, FString>& UnstableDetails)
{
    TSet<FString> Checked;
    TArray<FString> Maps;
    for (const auto& Asset : Plan.Assets)
        if (Asset->Kind == TEXT("World") && FPackageName::GetShortName(Asset->Package).EndsWith(TEXT("_P")) &&
            FPackageName::DoesPackageExist(Asset->Package))
            Maps.AddUnique(Asset->Package);
    FString Previous;
    FString Skipped;
    if (Maps.Num() && !MayOpenOtherMaps(Previous, Skipped))
    {
        Warnings.AddUnique(Skipped);
        return;
    }
    for (const FString& Map : Maps)
    {
        if (!UEditorLoadingAndSavingUtils::LoadMap(Map))
            continue;
        for (const auto& Pair : StrictMade)
        {
            if (Checked.Contains(Pair.Key))
                continue;
            UWorld* Layer = FindObject<UWorld>(nullptr, *P4ObjectPathOf(Pair.Key));
            if (!Layer)
                continue;
            Checked.Add(Pair.Key);
            const FP4LevelChangesResult Changes = P4AnalyzeOriginalLevelChanges(Layer);
            if (!Changes.IsValid() || Changes.Unchanged)
                continue;
            TSet<FString> Names;
            for (const auto& Changed : Changes.OtherChanged)
                Names.Add(Changed.Actor);
            if (Names.Num() && Changes.Removed.Num() == 0 && Changes.Added.Num() == 0 && Changes.Moved.Num() == 0 &&
                Changes.LevelChanges.Before.Num() == 0 && Changes.LevelChanges.After.Num() == 0)
            {
                UnstableWhy.Add(Pair.Key, FString::Join(Names.Array(), TEXT(", ")));
                UnstableDetails.Add(Pair.Key, P4LevelChangesJson(Changes).Left(32768));
                Unstable.Add(Pair.Key, MoveTemp(Names));
            }
        }
    }
    // The map the person had open comes back; a world that was never saved gives way to a blank one.
    if (Maps.Num() && (Previous.IsEmpty() || !UEditorLoadingAndSavingUtils::LoadMap(Previous)))
        UEditorLoadingAndSavingUtils::NewBlankMap(false);
}

// Class defaults are proved only after every Blueprint write, construction and
// temporary source loan has finished. A resumed world needs the same proof.
// The unique class union avoids recooking shared classes once per stream layer.
void FP4KadanImportRun::ProveStrictClassDefaults()
{
    if (!Settings.AllowGameBackedLevels && StrictWorldSources.Num() && !Cancelled && !FatalFailure &&
        !ReportWriteFailed && !Problems.Num())
    {
        const double Started = FPlatformTime::Seconds();
        TArray<FString> Worlds;
        StrictWorldSources.GetKeys(Worlds);
        Worlds.Sort();
        TArray<FP4StrictWorldClassInventory> Inventories;
        for (const FString& Package : Worlds)
        {
            const auto& Source = StrictWorldSources[Package];
            Inventories.Add(P4ReadStrictWorldClassInventory(
                Package, Source->Header, Source->Body, StrictResumedWorlds.Contains(Package)));
        }
        auto Proof = P4EvaluateStrictRequiredClasses(
            Worlds, Inventories,
            [](const FString& Package, FP4KadanSourceBinding& Source, UClass*& Class, FString& Error)
            { return P4ReadStrictNativeClassSource(Package, Source, Class, Error); },
            [](const FP4KadanSourceBinding& Source)
            { return P4CompareOriginalClassSourceNative(Source.Header, Source.Body, Source.Package); },
            EP4OriginalClassDefaultObservation::NativeStoredDefaults);
        // Concurrent source-file changes cannot retain an earlier inventory proof.
        for (auto& World : Proof.Worlds)
        {
            const auto& Source = StrictWorldSources[World.WorldPackage];
            const auto Current = P4ReadStrictWorldClassInventory(
                World.WorldPackage, Source->Header, Source->Body, World.Resumed);
            if (!Current.Known || Current.WorldSourceDigest != World.WorldSourceDigest)
            {
                World.Qualified = false;
                Proof.Qualified = false;
                World.Problems.Add(TEXT("World source changed during native class postflight."));
                Proof.Problems.Add(World.WorldPackage + TEXT(": world source changed during native class postflight"));
            }
        }
        ReportStrictClassDefaults(Proof);
        Spent(TEXT("native class postflight"), Started);
    }
}

void FP4KadanImportRun::ReportStrictClassDefaults(const FP4StrictRequiredClassPostflight& Proof)
{
    auto Json = MakeShared<FJsonObject>();
    Json->SetBoolField(TEXT("qualified"), Proof.Qualified);
    TArray<TSharedPtr<FJsonValue>> Classes, WorldRows;
    for (const auto& Class : Proof.Classes)
    {
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("package"), Class.Package);
        Row->SetBoolField(TEXT("qualified"), Class.Qualified);
        Row->SetStringField(TEXT("source_sha256"), Class.SourceDigest);
        Row->SetStringField(TEXT("native_default_facts_sha256"), Class.DefaultFactsDigest);
        Row->SetStringField(TEXT("reason"), Class.Reason);
        Row->SetNumberField(TEXT("compared"), Class.Compared);
        Row->SetNumberField(TEXT("values_not_held"), Class.ValuesNotHeld);
        Row->SetNumberField(TEXT("differences"), Class.Differences);
        Classes.Add(MakeShared<FJsonValueObject>(Row));
    }
    for (const auto& World : Proof.Worlds)
    {
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("package"), World.WorldPackage);
        Row->SetBoolField(TEXT("qualified"), World.Qualified);
        Row->SetStringField(TEXT("source_sha256"), World.WorldSourceDigest);
        Row->SetBoolField(TEXT("resumed"), World.Resumed);
        Row->SetNumberField(TEXT("required_classes"), World.RequiredClasses);
        TArray<TSharedPtr<FJsonValue>> Reasons;
        for (const FString& Reason : World.Problems)
            Reasons.Add(MakeShared<FJsonValueString>(Reason));
        Row->SetArrayField(TEXT("problems"), Reasons);
        WorldRows.Add(MakeShared<FJsonValueObject>(Row));
        const TSharedPtr<FJsonObject>* Reply = nullptr;
        if (Replies->TryGetObjectField(World.WorldPackage, Reply))
        {
            bool LevelValid = false;
            (*Reply)->TryGetBoolField(TEXT("valid"), LevelValid);
            (*Reply)->SetBoolField(TEXT("native_level_valid"), LevelValid);
            (*Reply)->SetBoolField(TEXT("native_class_defaults_qualified"), World.Qualified);
            (*Reply)->SetBoolField(TEXT("valid"), LevelValid && World.Qualified);
            if (!World.Qualified)
            {
                // Problems read "<class package>: <reason>". The note names the class in a whole sentence.
                FString Class = World.Problems.Num() ? World.Problems[0] : FString(), Reason;
                Class.Split(TEXT(": "), &Class, &Reason);
                (*Reply)->SetStringField(TEXT("error"),
                    FString::Printf(
                        TEXT("This level was built, but the game's class defaults of %d ") TEXT(
                            "classes it uses are not proved yet, so the import does not accept it yet. The first is %s. The import report has the reason."),
                        World.Problems.Num(), *Class.Left(512)));
            }
        }
    }
    Json->SetArrayField(TEXT("classes"), Classes);
    Json->SetArrayField(TEXT("worlds"), WorldRows);
    Result->SetObjectField(TEXT("native_class_postflight"), Json);
    for (const FString& Problem : Proof.Problems)
        Problems.Add(MakeShared<FJsonValueString>(Problem));
    if (!Proof.Qualified && !Proof.Problems.Num())
        Problems.Add(MakeShared<FJsonValueString>(TEXT("Strict native class defaults were not fully qualified.")));
}

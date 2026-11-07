#include "P4ExportSelection.h"
#include "P4Common.h"
#include "P4GamePaths.h"
#include "P4ExportContainer.h"
#include "P4GameReplacement.h"
#include "P4OriginalRecord.h"
#include "P4AuthoringRows.h"
#include "P4LevelChanges.h"
#include "Engine/World.h"
#include "AssetRegistryModule.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"

namespace
{
bool Package(const FString& Path)
{
    for (TCHAR C : Path)
        if (C == 0)
            return false;
    return P4IsValidContentPackage(Path);
}
FString SelectionFile(const FString& Directory, const FString& Id)
{
    return FPaths::Combine(Directory, TEXT("Config/KadanSelections"), Id + TEXT(".json"));
}
} //namespace

bool P4ValidateExportRoots(const TArray<FP4ExportRoot>& Roots, FString& Error)
{
    if (!Roots.Num() || Roots.Num() > 256)
    {
        Error = TEXT("Choose between one and 256 package or folder roots");
        return false;
    }
    TSet<FString> Seen;
    for (const FP4ExportRoot& Root : Roots)
    {
        if ((Root.Kind != EP4ExportRootKind::Package && Root.Kind != EP4ExportRootKind::Folder) || !Package(Root.Path))
        {
            Error = TEXT("Selection roots must be canonical /Game package or folder paths");
            return false;
        }
        const FString Key = FString::FromInt(int32(Root.Kind)) + TEXT(":") + Root.Path.ToLower();
        if (Seen.Contains(Key))
        {
            Error = Root.Path + TEXT(" is selected twice.");
            return false;
        }
        Seen.Add(Key);
    }
    Error.Reset();
    return true;
}

bool P4ReadExportRoots(
    const FString& Directory, const FP4ModDescriptor& Mod, TArray<FP4ExportRoot>& Out, FString& Error)
{
    if (Directory.IsEmpty() || !P4ValidateModDescriptor(Mod, Error))
    {
        if (Error.IsEmpty())
            Error = TEXT("A project directory is required");
        return false;
    }
    const FString File = SelectionFile(Directory, Mod.Id);
    FString Text;
    const int64 Size = IFileManager::Get().FileSize(*File);
    if (Size < 0)
    {
        Out.Reset();
        Error.Reset();
        return true;
    } // A newly created mod has no roots yet.
    if (Size > 65536 || !FFileHelper::LoadFileToString(Text, *File))
    {
        Error = TEXT("Saved selection is missing, unreadable or exceeds 64 KiB");
        return false;
    }
    TSharedPtr<FJsonObject> Json;
    double Format = 0;
    FString Id;
    const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
    if (!P4ParseJson(Text, Json) || Json->Values.Num() != 3 || !Json->HasTypedField<EJson::Number>(TEXT("format")) ||
        !Json->TryGetNumberField(TEXT("format"), Format) || Format != 1 || !Json->TryGetStringField(TEXT("id"), Id) ||
        !Id.Equals(Mod.Id, ESearchCase::CaseSensitive) || !Json->TryGetArrayField(TEXT("roots"), Rows))
    {
        Error = TEXT("Invalid saved selection document");
        return false;
    }
    TArray<FP4ExportRoot> Candidate;
    for (const TSharedPtr<FJsonValue>& Value : *Rows)
    {
        const TSharedPtr<FJsonObject>* Object = nullptr;
        FString Kind;
        FP4ExportRoot Root;
        if (!Value.IsValid() || !Value->TryGetObject(Object) || !Object->IsValid() || (*Object)->Values.Num() != 2 ||
            !(*Object)->TryGetStringField(TEXT("kind"), Kind) ||
            !(*Object)->TryGetStringField(TEXT("path"), Root.Path) ||
            (Kind != TEXT("package") && Kind != TEXT("folder")))
        {
            Error = TEXT("Invalid selection root document");
            return false;
        }
        Root.Kind = Kind == TEXT("package") ? EP4ExportRootKind::Package : EP4ExportRootKind::Folder;
        Candidate.Add(MoveTemp(Root));
    }
    if (Candidate.Num() && !P4ValidateExportRoots(Candidate, Error))
        return false;
    Out = MoveTemp(Candidate);
    return true;
}

bool P4WriteExportRoots(
    const FString& Directory, const FP4ModDescriptor& Mod, const TArray<FP4ExportRoot>& Roots, FString& Error)
{
    if (Directory.IsEmpty() || !P4ValidateModDescriptor(Mod, Error) ||
        (Roots.Num() && !P4ValidateExportRoots(Roots, Error)))
    {
        if (Error.IsEmpty())
            Error = TEXT("A project directory is required");
        return false;
    }
    auto Json = MakeShared<FJsonObject>();
    Json->SetNumberField(TEXT("format"), 1);
    Json->SetStringField(TEXT("id"), Mod.Id);
    TArray<TSharedPtr<FJsonValue>> Rows;
    for (const FP4ExportRoot& Root : Roots)
    {
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("kind"), Root.Kind == EP4ExportRootKind::Package ? TEXT("package") : TEXT("folder"));
        Row->SetStringField(TEXT("path"), Root.Path);
        Rows.Add(MakeShared<FJsonValueObject>(Row));
    }
    Json->SetArrayField(TEXT("roots"), Rows);
    FString Text;
    FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Text));
    const FTCHARToUTF8 Encoded(*Text);
    if (Encoded.Length() > 65536)
    {
        Error = TEXT("Serialized selection exceeds 64 KiB");
        return false;
    }
    const FString File = SelectionFile(Directory, Mod.Id);
    const FString Temporary = File + TEXT(".tmp.") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true) ||
        !FFileHelper::SaveStringToFile(Text, *Temporary, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) ||
        !IFileManager::Get().Move(*File, *Temporary, true, false, false, false))
    {
        IFileManager::Get().Delete(*Temporary);
        Error = TEXT("Cannot publish saved selection");
        return false;
    }
    Error.Reset();
    return true;
}

bool P4PreviewExportSelection(const FP4ModDescriptor& Mod, const TArray<FP4ExportRoot>& Roots,
    const FP4ExportGameInventory& Inventory, const TMap<FName, FString>& Claims, FP4ExportSelectionPreview& Out,
    FString& Error)
{
    if (!IsInGameThread())
    {
        Error = TEXT("Selection inspection requires the editor thread");
        return false;
    }
    FP4ModNames Names;
    if (!P4ModDerivedNames(Mod, Names, Error) || !P4ValidateExportRoots(Roots, Error))
        return false;
    if (!Inventory.IsValid())
    {
        Error = TEXT("Read the actual game package inventory before inspecting selection");
        return false;
    }
    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    TSet<FName> Explicit, Seen;
    TArray<FName> Pending;
    TMap<FName, FName> UsedBy; // The first package that brought each dependency in.
    TMap<FName, FString> SelectingRoots;
    for (const FP4ExportRoot& Root : Roots)
    {
        TArray<FAssetData> Assets;
        if (Root.Kind == EP4ExportRootKind::Folder)
            Registry.GetAssetsByPath(FName(*Root.Path), Assets, true);
        else
            Registry.GetAssetsByPackageName(FName(*Root.Path), Assets);
        if (!Assets.Num())
        {
            Error = Root.Path + TEXT(" holds no project assets.");
            return false;
        }
        for (const FAssetData& Asset : Assets)
        {
            Explicit.Add(Asset.PackageName);
            Pending.AddUnique(Asset.PackageName);
            if (!SelectingRoots.Contains(Asset.PackageName))
            {
                SelectingRoots.Add(Asset.PackageName, Root.Path);
            }
        }
    }
    FP4ExportSelectionPreview Result;
    for (int32 Index = 0; Index < Pending.Num(); ++Index)
    {
        if (Pending.Num() > 100000)
        {
            Error = TEXT("Selected dependency closure exceeds 100000 packages");
            return false;
        }
        const FName Identity = Pending[Index];
        if (Seen.Contains(Identity))
            continue;
        Seen.Add(Identity);
        const FString Name = Identity.ToString();
        if (!P4IsContentPackage(Name))
            continue;
        FP4ExportSelectionRow Row;
        Row.Package = Name;
        Row.Explicit = Explicit.Contains(Identity);
        Row.SelectingRoot = SelectingRoots.FindRef(Identity);
        TArray<FAssetData> Assets;
        Registry.GetAssetsByPackageName(Identity, Assets);
        const bool Game = Inventory.Contains(Identity);
        auto Refuse = [&](const FString& Why)
        {
            Row.Disposition = EP4ExportDisposition::Refused;
            Row.Reason = Why;
            Result.Refused.Add(Name + TEXT(" cannot be shipped. ") + Why);
        };
        // Several mods of one project can ship the same asset, from any folder: each ships its own copy.
        if (Row.Reason.IsEmpty() && !Assets.Num())
        {
            if (Game && !Row.Explicit)
            {
                Row.Disposition = EP4ExportDisposition::UnchangedGame;
                Row.Reason = TEXT("This is a game asset, so it is not cooked.");
            }
            else if (const FName* User = UsedBy.Find(Identity))
                Refuse(FString::Printf(
                    TEXT(
                        "There is no asset at this path, but %s still uses it. Open %s and replace what it uses, or put the missing asset back."),
                    *User->ToString(), *FPackageName::GetShortName(*User)));
            else
                Refuse(TEXT("There is no asset at this path."));
        }
        bool Original = false, AllUnchanged = true, Level = false, Table = false, Changed = false, Replacement = false,
             NotCompared = false;
        for (const FAssetData& Asset : Assets)
        {
            Row.AssetClass = Asset.AssetClass.ToString();
            Level |= Asset.AssetClass == FName(TEXT("World"));
            Table |= Asset.AssetClass == FName(TEXT("DataTable"));
            UObject* Object = Asset.GetAsset();
            if (!IsValid(Object))
            {
                if (Row.Reason.IsEmpty())
                    Refuse(TEXT("Project asset could not be loaded"));
                continue;
            }
            const FP4OriginalStatus Status = P4QueryOriginalAsset(Object);
            // An edited game asset that is the same as its import again (after an undo) is unchanged.
            const bool WholeReplacement = P4GameReplacementShips(Object);
            Replacement |= WholeReplacement;
            if (P4IsVerifiedStandIn(Object))
            {
                if (Row.Reason.IsEmpty())
                    Refuse(TEXT("Blueprint stand-in cannot ship"));
            }
            if (Status.IsOriginal())
            {
                Original = true;
                if (WholeReplacement)
                {
                    AllUnchanged = false;
                    Changed = true;
                }
                else
                {
                    const FP4OriginalDiagnostic Compared = P4DiagnoseOriginalAsset(Object);
                    AllUnchanged &= !Object->GetOutermost()->IsDirty() &&
                        Compared.Comparison == EP4OriginalComparison::Equal;
                    Changed |= Object->GetOutermost()->IsDirty() ||
                        Compared.Comparison == EP4OriginalComparison::Different;
                    NotCompared |= Compared.Comparison == EP4OriginalComparison::NotCompared;
                }
            }
            if ((Status.State == EP4OriginalState::NeedsMigration || Status.State == EP4OriginalState::Unreadable ||
                    Status.State == EP4OriginalState::UnsupportedRecord) &&
                Row.Reason.IsEmpty())
                Refuse(
                    Status.Error.IsEmpty() ? TEXT("Original provenance requires repair or migration") : Status.Error);
        }
        if (Row.Reason.IsEmpty())
        {
            if (Replacement)
            {
                // The creator's file in place of the game's asset, cooked at the game's path (K9).
                Row.Disposition = EP4ExportDisposition::CreatorPackage;
                Row.Reason = TEXT("This replacement is cooked whole into the mod at the game's path.");
                Result.CookCandidates.Add(Name);
            }
            else if (Game || Original)
            {
                if (Original && AllUnchanged)
                {
                    Row.Disposition = EP4ExportDisposition::UnchangedGame;
                    Row.Reason = TEXT("This game asset is unchanged, so it is not cooked.");
                    if (Game && Level && Row.Explicit)
                        Result.UnchangedLevelCandidates.Add(Name);
                }
                else if (Original && Game && !Row.Explicit && !Changed && NotCompared && !Level && !Table)
                {
                    // A game asset that selected content only uses, such as the game material of a replaced mesh,
                    // whose kind has no comparison with its import yet (game material instances). It is read-only
                    // and unsaved changes are put back, so it is the game's as the game has it and is not cooked.
                    Row.Disposition = EP4ExportDisposition::UnchangedGame;
                    Row.Reason = TEXT("This is a game asset your content uses, so it is not cooked.");
                }
                else if (Level && Original)
                {
                    Row.Disposition = EP4ExportDisposition::GameEdit;
                    Row.Reason = TEXT(
                        "Your changes to this game level are exported, as a list of changes or whole, as you choose below. The game's own level stays in the game.");
                    Result.GameEditCandidates.Add(Name);
                }
                else if (Table && Original)
                {
                    Row.Disposition = EP4ExportDisposition::GameEdit;
                    Row.Reason = TEXT("Your table changes are written in the mod's manifest.");
                    Result.TableEditCandidates.Add(Name);
                }
                else
                {
                    // A game asset differs from what its import recorded. Game assets are read-only, so the
                    // export refuses it by name.
                    Row.Disposition = EP4ExportDisposition::NeedsReplacementProof;
                    Row.Reason = Changed
                        ? TEXT(
                              "This game asset was changed after it was imported. Game assets are read-only, so put it back or import it again.")
                        : (Row.AssetClass == TEXT("Texture2D") || Row.AssetClass == TEXT("StaticMesh"))
                        ? TEXT(
                              "Game data cannot be cooked. To ship your own in its place, right-click it and choose Replace with my own file.")
                        : TEXT("Game data cannot be cooked. Import your own source file to ship this asset.");
                    Result.Refused.Add(Name + TEXT(" cannot be shipped. ") + Row.Reason);
                }
            }
            else
            {
                Row.Disposition = EP4ExportDisposition::CreatorPackage;
                Row.Reason = Row.AssetClass == TEXT("SoundWave")
                    ? TEXT("This is your source sound, and it becomes part of the mod's sound bank.")
                    : TEXT("This is your own content, and it is cooked into the mod.");
                Result.CookCandidates.Add(Name);
            }
        }
        Result.Rows.Add(MoveTemp(Row));
        // Omitted originals use their game's dependency closure at runtime, so that graph is not cooked.
        // An edited game level is never cooked either. What its added actors use is read from the
        // mod's own level when the export makes it.
        if (Result.Rows.Last().Disposition == EP4ExportDisposition::UnchangedGame ||
            Result.Rows.Last().Disposition == EP4ExportDisposition::GameEdit)
            continue;
        TArray<FName> Dependencies;
        Registry.GetDependencies(Identity, Dependencies, UE::AssetRegistry::EDependencyCategory::Package);
        for (FName Dependency : Dependencies)
            if (!Seen.Contains(Dependency))
            {
                Pending.AddUnique(Dependency);
                if (!UsedBy.Contains(Dependency))
                {
                    UsedBy.Add(Dependency, Identity);
                }
                if (!SelectingRoots.Contains(Dependency))
                {
                    SelectingRoots.Add(Dependency, SelectingRoots.FindRef(Identity));
                }
            }
    }
    Result.Rows.Sort(
        [](const FP4ExportSelectionRow& A, const FP4ExportSelectionRow& B) { return A.Package < B.Package; });
    Result.CookCandidates.Sort();
    Result.GameEditCandidates.Sort();
    Result.TableEditCandidates.Sort();
    Result.UnchangedLevelCandidates.Sort();
    Result.Refused.Sort();
    Out = MoveTemp(Result);
    Error.Reset();
    return true;
}

bool P4ValidateUnchangedMapSelection(
    const FP4ModDescriptor& Mod, const FP4ExportSelectionPreview& Preview, UWorld* OpenMap, FString& Error)
{
    if (!IsInGameThread() || !IsValid(OpenMap) || !OpenMap->PersistentLevel ||
        !P4QueryOriginalAsset(OpenMap).IsOriginal())
    {
        Error = TEXT("Open the selected levels' original persistent map before exporting.");
        return false;
    }
    if (Preview.Refused.Num() || !Preview.UnchangedLevelCandidates.Num() || Preview.CookCandidates.Num() ||
        Preview.GameEditCandidates.Num() || Preview.TableEditCandidates.Num())
    {
        Error = TEXT("Select unchanged original map levels, your own content or supported game edits.");
        return false;
    }
    FP4ModNames Names;
    if (!P4ModDerivedNames(Mod, Names, Error))
        return false;
    TSharedPtr<FJsonObject> GraphReply;
    const TSharedPtr<FJsonObject>* Graph = nullptr;
    bool Ok = false;
    if (!P4ParseJson(
            UP4AuthoringTools::NativeWorldStreamingGraph(OpenMap, Names.PackageRoot + TEXT("/ExportValidation")),
            GraphReply) ||
        !GraphReply->TryGetBoolField(TEXT("valid"), Ok) || !Ok || !GraphReply->TryGetObjectField(TEXT("graph"), Graph))
    {
        Error = TEXT("Original map streaming graph could not be inspected.");
        return false;
    }
    for (const FString& Package : Preview.UnchangedLevelCandidates)
    {
        if (!(*Graph)->HasField(Package))
        {
            Error = Package + TEXT(" is outside the selected original map.");
            return false;
        }
        const FP4ExportSelectionRow* Row = Preview.Rows.FindByPredicate(
            [&](const FP4ExportSelectionRow& Candidate) { return Candidate.Package == Package; });
        if (!Row || !Row->Explicit || Row->Disposition != EP4ExportDisposition::UnchangedGame ||
            Row->AssetClass != TEXT("World"))
        {
            Error = Package + TEXT(" is not a selected unchanged original level.");
            return false;
        }
        UWorld* World = LoadObject<UWorld>(nullptr, *P4ObjectPathOf(Package));
        if (!World || !P4QueryOriginalAsset(World).IsOriginal())
        {
            Error = Package + TEXT(" has no original level provenance.");
            return false;
        }
        const FP4LevelChangesResult Changes = P4AnalyzeOriginalLevelChanges(World);
        if (!Changes.IsValid() || !Changes.Unchanged)
        {
            Error = Package + TEXT(" is no longer unchanged. ") + P4LevelChangesErrorText(Changes);
            return false;
        }
    }
    Error.Reset();
    return true;
}

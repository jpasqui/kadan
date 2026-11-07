// The level content functions other parts of Kadan call: level state and changes, lighting, foliage,
// source custody paths and map editor layers.

#include "P4LevelContentInternal.h"
#include "P4Common.h"
#include "P4NativeConstructionPreview.h"
#include "P4KadanSettings.h"
#include "P4GamePaths.h"
#include "P4NavigationLayer.h"
#include "P4OriginalLayerIdentity.h"

namespace P4LevelContent
{
FString Finish(const TSharedRef<FJsonObject>& Json, const FString& Error)
{
    Json->SetBoolField(TEXT("valid"), Error.IsEmpty());
    Json->SetStringField(TEXT("error"), Error);
    FString Out = P4CondensedJson(Json);
    return Out;
}
} // namespace P4LevelContent

FString P4LevelState(UWorld* World)
{
    return P4LevelContent::State(World);
}

FString P4LevelContentText(UWorld* World)
{
    using namespace P4LevelContent;
    if (!IsInGameThread() || !IsValid(World) || !World->PersistentLevel ||
        World->GetOutermost() == GetTransientPackage())
        return FString();
    FFile File;
    FString Error;
    TArray<FString> Content, EngineMade;
    if (!Cooked(World, true, File, Error))
        return FString();
    Lines(File, Content, EngineMade);
    WithoutEditorNavigation(World, Content);
    WithEditorDroppedStreams(World, Content);
    WithEditorNavLinkIds(World, Content);
    WithEditorLandscapeMaterials(World, Content);
    WithInheritedAnimationClasses(World, Content);
    WithInheritedRuleTableDefaults(World, Content);
    WithoutAddedNativeTransformTracking(World, Content);
    return FString::Join(Content, TEXT("\n"));
}

FString P4OriginalLevelStatus(UWorld* World)
{
    return P4LevelContent::Status(World);
}

FString P4OriginalLevelChanges(UWorld* World)
{
    return P4LevelContent::Changes(World);
}

bool P4ReadOriginalLevelLighting(UWorld* World, FP4OriginalLevelLighting& Out, FString& Error)
{
    Out = FP4OriginalLevelLighting();
    if (!IsInGameThread() || !IsValid(World) || !World->PersistentLevel)
    {
        Error = TEXT("Open the original game's level to read its lighting.");
        return false;
    }
    TArray<FString> Lines;
    if (!P4LevelContent::Unpack(
            World->GetOutermost()->GetMetaData()->GetValue(World, P4LevelContent::LightingKey), Lines) ||
        !Lines.Num() || !Lines[0].StartsWith(TEXT("registry\t")))
    {
        Error = TEXT(
            "This imported level has no original lighting information. Import it again before changing lit actors.");
        return false;
    }
    Out.Registry = Lines[0].Mid(9);
    const FString Level = World->GetName() + TEXT(".") + World->PersistentLevel->GetName();
    for (int32 Index = 1; Index < Lines.Num(); ++Index)
    {
        if (Lines[Index].StartsWith(TEXT("light\t")))
        {
            FString Component, Digits;
            FGuid Guid;
            if (!Lines[Index].Mid(6).Split(TEXT("\t"), &Component, &Digits) || Out.Lights.Contains(Component) ||
                !FGuid::ParseExact(Digits, EGuidFormats::Digits, Guid))
            {
                Error = TEXT("The original light identity cannot be read.");
                return false;
            }
            Out.Lights.Add(Component, Guid);
            if (Guid.IsValid())
            {
                const FString Actor = P4LevelContent::OwnerOf(Component, Level);
                if (Actor.IsEmpty())
                {
                    Error = TEXT("The original light has no actor identity.");
                    return false;
                }
                Out.Actors.Add(Actor);
            }
            continue;
        }
        FString Component, Text;
        if (!Lines[Index].Split(TEXT("\t"), &Component, &Text) || Out.Components.Contains(Component))
        {
            Error = TEXT("The original component lighting information cannot be read.");
            return false;
        }
        TArray<FString> Ids;
        Text.ParseIntoArray(Ids, TEXT(","), true);
        TArray<FGuid> Guids;
        for (const FString& Id : Ids)
        {
            TArray<FString> Words;
            Id.ParseIntoArray(Words, TEXT(" "), true);
            if (Words.Num() != 4)
            {
                Error = TEXT("The original component lighting IDs cannot be read.");
                return false;
            }
            FString Digits = FString::Join(Words, TEXT(""));
            FGuid Guid;
            if (!FGuid::ParseExact(Digits, EGuidFormats::Digits, Guid))
            {
                Error = TEXT("The original component lighting IDs cannot be read.");
                return false;
            }
            Guids.Add(Guid);
            if (Guid.IsValid())
            {
                const FString Actor = P4LevelContent::OwnerOf(Component, Level);
                if (Actor.IsEmpty())
                {
                    Error = TEXT("The original lit component has no actor identity.");
                    return false;
                }
                Out.Actors.Add(Actor);
            }
        }
        Out.Components.Add(Component, MoveTemp(Guids));
    }
    // Some game levels keep lighting ids on their components but ship no lighting build (Area003's
    // Hero objects). With no registry nothing there is lit by a build of the game's, so no actor is.
    if (Out.Actors.Num() && Out.Registry.IsEmpty())
        Out.Actors.Empty();
    Error.Reset();
    return true;
}

FP4LevelChangesResult P4AnalyzeOriginalLevelChanges(UWorld* World)
{
    // The map as it is, without what the editor previews of the game's native classes.
    FP4SuspendNativePreviews Previews;
    return P4LevelContent::AnalyzeChanges(World);
}

FString P4WriteActorNavigationCollision(AActor* Actor, const FString& File)
{
    return P4LevelContent::NavigationCollision(Actor, File);
}

FString P4EraseFoliage(UWorld* World, const FVector& Center, float Radius)
{
    return P4LevelContent::EraseFoliage(World, Center, Radius);
}

FString P4AddFoliage(UWorld* World, UInstancedStaticMeshComponent* Kind, const FVector& Location,
    const FRotator& Rotation, const FVector& Scale)
{
    return P4LevelContent::AddFoliage(World, Kind, Location, Rotation, Scale);
}

namespace
{
const TCHAR* const ProjectRoot = TEXT("{project}/");

const TCHAR* const GameRoot = TEXT("{game}/");

FString CustodyFolder(const FString& Folder)
{
    if (Folder.IsEmpty())
        return FString();
    FString Full = FPaths::ConvertRelativePathToFull(Folder);
    FPaths::NormalizeDirectoryName(Full);
    FPaths::CollapseRelativeDirectories(Full);
    return Full.EndsWith(TEXT("/")) ? Full : Full + TEXT("/");
}

FString CustodyGameFolder()
{
    return CustodyFolder(P4KadanSettings::Get(P4KadanSettings::GameFolder));
}
} // namespace

FString P4PortableSourceCustodyPath(const FString& FullPath)
{
    FString Full = FPaths::ConvertRelativePathToFull(FullPath);
    FPaths::NormalizeFilename(Full);
    FPaths::CollapseRelativeDirectories(Full);
    for (const auto& Root : {TPair<FString, const TCHAR*>(CustodyFolder(FPaths::ProjectDir()), ProjectRoot),
             TPair<FString, const TCHAR*>(CustodyGameFolder(), GameRoot)})
        if (!Root.Key.IsEmpty() && Full.StartsWith(Root.Key, ESearchCase::IgnoreCase) && Full.Len() > Root.Key.Len())
            return FString(Root.Value) + Full.Mid(Root.Key.Len());
    return Full;
}

FString P4ResolveSourceCustodyPath(const FString& Stored)
{
    // Older custody holds full paths, which stay as they are.
    for (const auto& Root : {TPair<const TCHAR*, FString>(ProjectRoot, CustodyFolder(FPaths::ProjectDir())),
             TPair<const TCHAR*, FString>(GameRoot, CustodyGameFolder())})
        if (Stored.StartsWith(Root.Key, ESearchCase::CaseSensitive))
        {
            const FString Rest = Stored.Mid(FCString::Strlen(Root.Key));
            if (Root.Value.IsEmpty() || Rest.IsEmpty() || Rest.Contains(TEXT("..")))
                return FString();
            return Root.Value + Rest;
        }
    return Stored;
}

bool P4ReadGameBackedLevelCustody(UWorld* World, TSet<FName>& OmittedActors, FString& SourceDigest, FString& Error)
{
    if (!IsValid(World) || !World->GetOutermost()->GetMetaData()->HasValue(World, P4LevelContent::GameBackedKey))
    {
        Error = TEXT("level-is-not-game-backed");
        return false;
    }
    return P4LevelContent::ReadGameBackedCustody(World, OmittedActors, SourceDigest, Error);
}

namespace P4LevelContent
{
KADAN_API void SetOriginalLayerEditorVisibility(ULevelStreaming* Streaming, bool Visible)
{
    // UE4.26's editor ShouldBeVisible() reads only this editor-only flag. The
    // runtime load/visibility flags are serialized source properties, so changing
    // them to show a map would alter its immutable cook/export baseline.
    Streaming->SetShouldBeVisibleInEditor(Visible);
}
} // namespace P4LevelContent

bool P4ActivateOriginalMapEditorLayers(
    UWorld* World, const FString& Variant, TSet<FName>& ExpectedPackages, FString& Error)
{
    ExpectedPackages.Reset();
    Error.Reset();
    if (!IsInGameThread() || !IsValid(World) || World->WorldType != EWorldType::Editor ||
        (Variant != TEXT("day") && Variant != TEXT("night") && Variant != TEXT("hero")))
    {
        Error = TEXT("original-map-presentation-requires-editor-world-and-valid-variant");
        return false;
    }
    TArray<TPair<UWorld*, bool>> Pending;
    Pending.Emplace(World, true);
    TMap<FName, bool> Seen;
    TMap<ULevelStreaming*, bool> Changes;
    TSet<FName> Expected;
    const auto Mode = [](const FString& Package)
    {
        const FString Leaf = FPackageName::GetShortName(Package);
        return P4IsOlimarNavigationLevel(Package)                    ? FString(TEXT("hero"))
            : Leaf.Contains(TEXT("_Night"), ESearchCase::IgnoreCase) ? FString(TEXT("night"))
            : Leaf.Contains(TEXT("_Day"), ESearchCase::IgnoreCase)   ? FString(TEXT("day"))
                                                                     : FString(TEXT("common"));
    };
    while (Pending.Num())
    {
        const auto Visit = Pending.Pop(false);
        UWorld* Owner = Visit.Key;
        const bool Selected = Visit.Value;
        if (!IsValid(Owner))
        {
            Error = TEXT("original-map-presentation-layer-unavailable");
            return false;
        }
        const FName Package = Owner->GetOutermost()->GetFName();
        if (const bool* Prior = Seen.Find(Package))
            if (*Prior || !Selected)
                continue;
        Seen.Add(Package, Selected);
        FP4OriginalRecord Record;
        P4LevelContent::FBaseline Baseline;
        if (!P4ReadOriginalRecord(Owner, Record, Error) || Record.Kind != TEXT("level") ||
            !P4LevelContent::ReadBaseline(Owner, Baseline) || !P4SameText(Record.SourceDigest, Baseline.Source) ||
            !P4SameText(Record.ComparisonDigest, Baseline.State) ||
            (Record.ComparisonSchema != TEXT("level-lines-v1") &&
                Record.ComparisonSchema != TEXT("game-backed-level-lines-v1")))
        {
            Error = TEXT("original-map-presentation-layer-unqualified: ") + Package.ToString() + TEXT(" ") + Error;
            return false;
        }
        TSet<FName> Omitted;
        FString Source;
        if (!P4LevelContent::ReadGameBackedCustody(Owner, Omitted, Source, Error))
            return false;
        TArray<FString> Imported;
        FString ImportedDigest;
        if (!P4LevelContent::Unpack(
                Owner->GetOutermost()->GetMetaData()->GetValue(Owner, P4LevelContent::ImportedKey), Imported) ||
            !P4LevelContent::Digest(Imported, ImportedDigest) || !P4SameText(ImportedDigest, Baseline.State))
        {
            Error = TEXT("original-map-presentation-baseline-unqualified: ") + Package.ToString();
            return false;
        }
        if (Selected)
            Expected.Add(Package);
        for (ULevelStreaming* Streaming : Owner->GetStreamingLevels())
            if (Streaming)
            {
                // Only streaming objects in the immutable imported baseline are original
                // presentation. Creator-added custom sublevels retain their existing flags.
                if (!P4OriginalLayerRecorded(Streaming->GetPathName(Owner->GetOutermost()), Imported))
                {
                    continue;
                }
                const FString Child = Streaming->GetWorldAssetPackageName(), ChildMode = Mode(Child);
                const bool Enabled = Selected && (ChildMode == TEXT("common") || ChildMode == Variant);
                Changes.FindOrAdd(Streaming) |= Enabled;
                UWorld* ChildWorld = LoadObject<UWorld>(
                    nullptr, *P4ObjectPathOf(Child), nullptr, LOAD_NoWarn | LOAD_Quiet);
                if (!ChildWorld)
                {
                    Error = TEXT("original-map-presentation-child-unavailable: ") + Child;
                    return false;
                }
                Pending.Emplace(ChildWorld, Enabled);
            }
    }
    // No stream flag changes occur until the entire original graph and all source
    // records/custody pass. A later refusal leaves the editor presentation untouched.
    for (const auto& Change : Changes)
        P4LevelContent::SetOriginalLayerEditorVisibility(Change.Key, Change.Value);
    ExpectedPackages = MoveTemp(Expected);
    World->FlushLevelStreaming(EFlushLevelStreamingType::Full);
    return true;
}

void P4ShowOriginalLayersForOpen(UWorld* World)
{
    FP4OriginalRecord Record;
    FString Error;
    if (!IsValid(World) || World->IsGameWorld() || !P4ReadOriginalRecord(World, Record, Error) ||
        Record.Kind != TEXT("level"))
        return;
    UMetaData* Meta = World->GetOutermost()->GetMetaData();
    TArray<FString> Imported;
    if (!Meta->HasValue(World, P4LevelContent::ImportedKey) ||
        !P4LevelContent::Unpack(Meta->GetValue(World, P4LevelContent::ImportedKey), Imported))
        return;
    // Creator-added sublevels are not in the imported record and keep their own flags.
    for (ULevelStreaming* Streaming : World->GetStreamingLevels())
        if (Streaming && P4OriginalLayerRecorded(Streaming->GetPathName(World->GetOutermost()), Imported))
            P4LevelContent::SetOriginalLayerEditorVisibility(Streaming, true);
}

// The names of the game's actors in an imported level, from what the level said when it was imported.
TSet<FName> P4ImportedLevelActors(UWorld* World)
{
    TSet<FName> Out;
    if (!IsInGameThread() || !IsValid(World) || !World->PersistentLevel)
        return Out;
    UMetaData* Meta = World->GetOutermost()->GetMetaData();
    TArray<FString> Imported;
    if (!Meta->HasValue(World, P4LevelContent::ImportedKey))
        return Out;
    // Asked again for every component a construction script replaces, so a level's names are kept while its
    // imported list is unchanged.
    struct FKept
    {
        FString Packed, Level;
        TSet<FName> Names;
    };
    static TMap<TWeakObjectPtr<UWorld>, FKept> Kept;
    const FString& Packed = Meta->GetValue(World, P4LevelContent::ImportedKey);
    const FString Level = World->GetName() + TEXT(".") + World->PersistentLevel->GetName();
    if (const FKept* Found = Kept.Find(World))
        if (Found->Level == Level && Found->Packed.Equals(Packed, ESearchCase::CaseSensitive))
            return Found->Names;
    if (!P4LevelContent::Unpack(Packed, Imported))
        return Out;
    for (const FString& Line : Imported)
    {
        const FString Name = P4LevelContent::OwnerOf(Line, Level);
        if (!Name.IsEmpty())
            Out.Add(FName(*Name));
    }
    if (Kept.Num() >= 64)
        Kept.Reset();
    Kept.Add(World, FKept{Packed, Level, Out});
    return Out;
}

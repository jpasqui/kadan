// What an edit of one of the game's levels amounts to: removals, moves and changed values for the changes
// file, added actors for a level of the mod's own, and changes of the game's placement lists.

#include "P4ExportInternal.h"

using namespace P4Export;

namespace
{
// The object a line names below an actor, and the path the framework's reflection takes to it from the
// actor: through the actor's references to its own parts (its components and what they hold).
bool Reach(AActor* Actor, const FString& Below, UObject*& Object, FString& Path)
{
    Object = Actor;
    Path.Reset();
    if (Below.IsEmpty())
        return true;
    UObject* Target = StaticFindObject(UObject::StaticClass(), Actor, *Below);
    if (!Target)
        return false;
    TArray<TPair<UObject*, FString>> Front;
    Front.Emplace(Actor, FString());
    TSet<UObject*> Seen;
    Seen.Add(Actor);
    for (int32 At = 0; At < Front.Num() && At < 256; ++At)
        for (TFieldIterator<FObjectPropertyBase> Field(Front[At].Key->GetClass()); Field; ++Field)
        {
            if (Field->ArrayDim != 1 || CastField<FSoftObjectProperty>(*Field))
                continue;
            UObject* Value = Field->GetObjectPropertyValue_InContainer(Front[At].Key);
            if (!Value || Seen.Contains(Value) || !Value->IsIn(Actor))
                continue;
            Seen.Add(Value);
            const FString Next = Front[At].Value.IsEmpty() ? Field->GetName()
                                                           : Front[At].Value + TEXT(".") + Field->GetName();
            if (Value == Target)
            {
                Object = Target;
                Path = Next;
                return true;
            }
            Front.Emplace(Value, Next);
        }
    return false;
}

// What changed of one of the game's actors other than a plain move, as values to set. A change to the
// place of its root is a move.
bool ActorValues(UWorld* World, const FP4LevelActorChanges& Changed, P4MapKit::Export::LevelEdit& Edit,
    TArray<FString>& References, TArray<FAssetValueReference>& AssetValues, FString& Error)
{
    AActor* Actor = FindObject<AActor>(World->PersistentLevel, *Changed.Actor);
    if (!Actor)
    {
        Error = FString::Printf(TEXT("The changed actor %s is not in the editor's level."), *Changed.Actor);
        return false;
    }
    const FString Base = World->GetName() + TEXT(".") + World->PersistentLevel->GetName() + TEXT(".") + Changed.Actor;
    USceneComponent* Root = Actor->GetRootComponent();
    FValues Values;
    bool Moved = false;
    TArray<FString> Lines = Changed.Lines.Before;
    Lines.Append(Changed.Lines.After);
    for (const FString& Line : Lines)
    {
        const int32 Bar = Line.Find(TEXT(" | "), ESearchCase::CaseSensitive);
        FString Below = Bar == INDEX_NONE ? FString() : Line.Left(Bar);
        if (Bar == INDEX_NONE || !Below.StartsWith(Base, ESearchCase::CaseSensitive))
        {
            Error = FString::Printf(TEXT("A change of %s could not be read."), *Changed.Actor);
            return false;
        }
        Below = Below.Mid(Base.Len());
        Below.RemoveFromStart(TEXT("."), ESearchCase::CaseSensitive);
        const FString Rest = Line.Mid(Bar + 3);
        const int32 Is = Rest.Find(TEXT(" = "), ESearchCase::CaseSensitive);
        FString Key = Is == INDEX_NONE ? FString() : Rest.Left(Is);
        if (Below.Contains(TEXT(">")))
        {
            Error = FString::Printf(TEXT("A child actor of %s was changed, which cannot be exported."), *Changed.Actor);
            return false;
        }
        if (Key.IsEmpty() || Key.Contains(TEXT(" ")))
        {
            Error = FString::Printf(
                TEXT("A part of %s was added, removed or replaced, which cannot be exported."), *Changed.Actor);
            return false;
        }
        UObject* Object = nullptr;
        FString Path;
        if (!Reach(Actor, Below, Object, Path))
        {
            Error = FString::Printf(
                TEXT("The changed part %s of %s cannot be reached from the actor, so it cannot be exported."), *Below,
                *Changed.Actor);
            return false;
        }
        int32 Index = INDEX_NONE, Bracket = INDEX_NONE;
        if (Key.FindChar(TEXT('['), Bracket))
        {
            Index = FCString::Atoi(*Key.Mid(Bracket + 1));
            Key.LeftInline(Bracket);
        }
        const FProperty* Property = Object->GetClass()->FindPropertyByName(FName(*Key));
        if (!Property || Index >= Property->ArrayDim)
        {
            Error = FString::Printf(
                TEXT("The changed value %s of %s is not in the editor's actor."), *Key, *Changed.Actor);
            return false;
        }
        if (Object == Root && !Root->GetAttachParent() &&
            (Key == TEXT("relativelocation") || Key == TEXT("relativerotation") || Key == TEXT("relativescale3d")))
        {
            Moved = true;
            continue;
        }
        const FString Prefix = Path.IsEmpty() ? FString() : Path + TEXT(".");
        for (int32 Slot = Index == INDEX_NONE ? 0 : Index;
            Slot < (Index == INDEX_NONE ? Property->ArrayDim : Index + 1); ++Slot)
            if (!P4PlanActorValue(Property, Property->ContainerPtrToValuePtr<void>(Object, Slot),
                    Prefix + Property->GetName() +
                        (Property->ArrayDim > 1 ? FString::Printf(TEXT("[%d]"), Slot) : FString()),
                    Changed.Actor, World, Object, Values, References, AssetValues, Error))
                return false;
    }
    if (Moved)
    {
        const FVector At = Root->GetRelativeLocation(), Scale = Root->GetRelativeScale3D();
        const FRotator Turn = Root->GetRelativeRotation();
        P4MapKit::Export::Transform& Move = Edit.Move[TCHAR_TO_UTF8(*Changed.Actor)];
        Move.Location = {{At.X, At.Y, At.Z}};
        Move.Rotation = {{Turn.Pitch, Turn.Yaw, Turn.Roll}};
        Move.Scale = {{Scale.X, Scale.Y, Scale.Z}};
    }
    if (!Values.empty())
        Edit.Set[TCHAR_TO_UTF8(*Changed.Actor)] = std::move(Values);
    return true;
}
} // namespace

namespace P4Export
{
bool LevelChanges(UWorld* World, const FP4LevelChangesResult& Changes, P4MapKit::Export::MapEdit& Map, FLevelEdit& Out,
    FString& Error, bool ReadRows, const TArray<FP4GamePlacementRow>* GameRows)
{
    if (!Changes.IsValid())
    {
        Error = P4LevelChangesErrorText(Changes);
        return false;
    }
    if (Changes.LevelChanges.Before.Num() || Changes.LevelChanges.After.Num())
    {
        Error = FString::Printf(
            TEXT(
                "The level %s itself was changed, which cannot be exported. Changes to its actors and its world settings can be."),
            *Changes.Level);
        return false;
    }
    P4MapKit::Export::LevelEdit Edit;
    for (const FString& Name : Changes.Removed)
        Edit.Remove.emplace_back(TCHAR_TO_UTF8(*Name));
    for (const FP4LevelActorMove& Move : Changes.Moved)
    {
        P4MapKit::Export::Transform Transform;
        Transform.Location = {{Move.Location.X, Move.Location.Y, Move.Location.Z}};
        Transform.Rotation = {{Move.Rotation.Pitch, Move.Rotation.Yaw, Move.Rotation.Roll}};
        Transform.Scale = {{Move.Scale.X, Move.Scale.Y, Move.Scale.Z}};
        if (!Edit.Move.emplace(TCHAR_TO_UTF8(*Move.Actor), Transform).second)
        {
            Error = TEXT("Duplicate actor move");
            return false;
        }
    }
    for (const FP4LevelActorChanges& Changed : Changes.OtherChanged)
        if (!ActorValues(World, Changed, Edit, Out.References, Out.AssetValues, Error))
            return false;
    // The game builds its gameplay objects from the rows of its placement lists, not from the level: a move
    // or a removal of one is a change of its row.
    TMap<FString, FP4GameRowEdit> Rows;
    if (ReadRows && !P4GameRowsOfLevelEdit(World, Changes, GameRows, Rows, Error))
        return false;
    Out.RowEdits.Reset();
    for (const auto& Pair : Rows)
    {
        const std::string Actor = TCHAR_TO_UTF8(*Pair.Key);
        P4MapKit::Export::PlacementEdit& Row = Map.PlacementChanges[TCHAR_TO_UTF8(*Pair.Value.List)];
        if (Pair.Value.Removed)
        {
            Edit.Remove.erase(std::remove(Edit.Remove.begin(), Edit.Remove.end(), Actor), Edit.Remove.end());
            Row.Remove.push_back(Pair.Value.Id);
        }
        else
        {
            const auto Moved = Edit.Move.find(Actor);
            if (Moved == Edit.Move.end())
                continue; // changed values only: they stay values of the level's actor
            if (!Row.Move.emplace(Pair.Value.Id, Moved->second).second)
            {
                Error = FString::Printf(
                    TEXT("%s is moved twice in the game's placement list %s."), *Pair.Key, *Pair.Value.List);
                return false;
            }
            Edit.Move.erase(Moved);
        }
        Out.RowEdits.Add(Pair.Value);
    }
    Out.RowEdits.Sort([](const FP4GameRowEdit& A, const FP4GameRowEdit& B) { return A.Actor < B.Actor; });
    if (Map.Changes.count(TCHAR_TO_UTF8(*Changes.Level)))
    {
        Error = TEXT("Duplicate source level identity");
        return false;
    }
    Map.Changes.emplace(TCHAR_TO_UTF8(*Changes.Level), std::move(Edit));
    Out.World = World;
    Out.Package = World->GetOutermost()->GetName();
    Out.Level = Changes.Level;
    Out.Added.Reset();
    Out.Placed.Reset();
    // The game's level keeps a gameplay object built from a placement row only as the editor's picture of it,
    // so moving or removing one leaves the level's lighting as it is.
    Out.RebuildLighting = false;
    for (const FString& Lit : Changes.LitByGame)
        Out.RebuildLighting = Out.RebuildLighting ||
            !Out.RowEdits.ContainsByPredicate([&](const FP4GameRowEdit& Row) { return Row.Actor == Lit; }) ||
            Changes.OtherChanged.ContainsByPredicate(
                [&](const FP4LevelActorChanges& Changed) { return Changed.Actor == Lit; });
    Out.NavigationOnly.Reset();
    Out.Navigation = P4EditChangesNavigation(Changes, Out.NavigationWhy);
    for (const FString& Name : Changes.Added)
    {
        AActor* Actor = FindObject<AActor>(World->PersistentLevel, *Name);
        if (P4IsNavigationOnlyActor(Actor))
        {
            Out.NavigationOnly.Add(Name);
            if (!Out.Navigation)
            {
                Out.Navigation = true;
                Out.NavigationWhy = Name;
            }
            continue;
        }
        if (!P4IsGameplayActor(Actor))
        {
            Out.Added.Add(Name);
            continue;
        }
        const FString Issue = P4PlacementIssue(Actor);
        if (!Issue.IsEmpty())
        {
            Error = Issue;
            return false;
        }
        Out.Placed.Add(Actor);
    }
    return true;
}
} // namespace P4Export

FString P4ExportLevelChangeIssue(UWorld* World, const FP4LevelChangesResult& Changes)
{
    if (!IsInGameThread() || !IsValid(World) || !World->PersistentLevel)
        return TEXT("The selected level cannot be opened.");
    P4MapKit::Export::MapEdit Map;
    FLevelEdit Edit;
    FString Error;
    return LevelChanges(World, Changes, Map, Edit, Error) ? FString() : Error;
}

bool P4ExportLevelRowEdits(UWorld* World, const FP4LevelChangesResult& Changes, const FString& GameFolder,
    TArray<FP4GameRowEdit>& Out, bool& RebuildLighting, FString& Error)
{
    Out.Reset();
    RebuildLighting = false;
    if (!IsInGameThread() || !IsValid(World) || !World->PersistentLevel)
    {
        Error = TEXT("The selected level cannot be opened.");
        return false;
    }
    const FString Package = World->GetOutermost()->GetName();
    const FString Folder = GameFolder.IsEmpty() ? FString() : P4GamePlacementFolder(GameFolder, Package);
    TArray<FP4GamePlacementRow> Rows;
    if (!Folder.IsEmpty() && !P4ReadGamePlacementLists(Folder, Rows, Error))
    {
        Error = TEXT("The game's placement lists of ") + Package + TEXT(" could not be read (") + Error + TEXT(").");
        return false;
    }
    P4MapKit::Export::MapEdit Map;
    FLevelEdit Edit;
    if (!LevelChanges(World, Changes, Map, Edit, Error, true, Folder.IsEmpty() ? nullptr : &Rows))
        return false;
    Out = Edit.RowEdits;
    RebuildLighting = Edit.RebuildLighting;
    Error.Reset();
    return true;
}

bool P4InspectLevelCreatorReferences(
    UWorld* World, const FP4LevelChangesResult& Changes, FP4CreatorReferenceInputs& Out, FString& Error)
{
    Out = FP4CreatorReferenceInputs();
    if (!IsInGameThread() || !IsValid(World) || !World->PersistentLevel)
    {
        Error = TEXT("The selected level cannot be opened.");
        return false;
    }
    P4MapKit::Export::MapEdit Map;
    FLevelEdit Edit;
    if (!LevelChanges(World, Changes, Map, Edit, Error))
        return false;
    Out.Packages = Edit.References;
    Out.AssetValues = Edit.AssetValues;
    for (const FString& Name : Edit.Added)
    {
        AActor* Actor = FindObject<AActor>(World->PersistentLevel, *Name);
        if (!Actor)
        {
            Error = TEXT("An added actor disappeared before export review: ") + Name;
            return false;
        }
        Out.AddedActors.Add(Actor);
    }
    Error.Reset();
    return true;
}

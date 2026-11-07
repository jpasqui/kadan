// Actors of an imported level carried into a level of a mod's own, and what lighting and collision
// they take with them.

#include "P4LevelContentInternal.h"
#include "P4Common.h"
#include "P4GamePaths.h"

namespace P4LevelContent
{
// The collision of an actor as navigation takes it. The engine's navigation build asks each component
// for its geometry and keeps it in the form Recast reads. That same answer is written here, in
// Unreal's coordinates, in the form the kit's collision files have (written for Unreal's importer,
// which mirrors Y, so Y is negated), so that the kit's navigation bake takes an added actor the way
// the game's build would have. A triangle keeps the order of its corners: Recast takes a triangle
// as ground when its corners go round one way seen from above, and from Recast's numbers to the
// file's that way stays the same. In the kit's files the ground goes round the same way.
FString NavigationCollision(AActor* Actor, const FString& File)
{
    return P4ActorCollisionJson(P4ExportActorNavigationCollision(Actor, File));
}

// Takes out the foliage of a level that stands inside a sphere, with the calls the foliage mode's eraser
// makes: the foliage actor's own record of each kind finds the instances and takes them out, and the
// component that draws the kind follows.
FString EraseFoliage(UWorld* World, const FVector& Center, float Radius)
{
    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetNumberField(TEXT("erased"), 0);
    if (!IsInGameThread())
        return Finish(Json, TEXT("foliage-erase-requires-editor-thread"));
    if (!IsValid(World) || !World->PersistentLevel)
        return Finish(Json, TEXT("foliage-erase-needs-a-level"));
    if (!(Radius > 0.0f))
        return Finish(Json, TEXT("foliage-erase-needs-a-radius"));
    AInstancedFoliageActor* Foliage = AInstancedFoliageActor::GetInstancedFoliageActorForLevel(
        World->PersistentLevel, false);
    if (!Foliage)
        return Finish(Json, TEXT("foliage-erase-the-level-has-no-foliage"));
    int32 Erased = 0, Kinds = 0;
    for (auto& Pair : Foliage->FoliageInfos)
    {
        TArray<int32> Inside;
        Pair.Value->GetInstancesInsideSphere(FSphere(Center, Radius), Inside);
        if (!Inside.Num())
            continue;
        Foliage->Modify();
        Pair.Value->RemoveInstances(Foliage, Inside, true);
        Erased += Inside.Num();
        ++Kinds;
    }
    Json->SetNumberField(TEXT("erased"), Erased);
    Json->SetNumberField(TEXT("kinds"), Kinds);
    Json->SetStringField(TEXT("actor"), Foliage->GetName());
    return Finish(Json, FString());
}

// Adds one piece of foliage of the kind a component draws, with the call the foliage mode's brush makes:
// the foliage actor's record of the kind takes the new instance, and the component follows.
FString AddFoliage(UWorld* World, UInstancedStaticMeshComponent* Kind, const FVector& Location,
    const FRotator& Rotation, const FVector& Scale)
{
    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    if (!IsInGameThread())
        return Finish(Json, TEXT("foliage-add-requires-editor-thread"));
    if (!IsValid(World) || !World->PersistentLevel)
        return Finish(Json, TEXT("foliage-add-needs-a-level"));
    AInstancedFoliageActor* Foliage = AInstancedFoliageActor::GetInstancedFoliageActorForLevel(
        World->PersistentLevel, false);
    if (!Foliage)
        return Finish(Json, TEXT("foliage-add-the-level-has-no-foliage"));
    if (!IsValid(Kind) || Kind->GetOwner() != Foliage)
        return Finish(Json, TEXT("foliage-add-needs-a-component-of-the-level's-foliage"));
    for (auto& Pair : Foliage->FoliageInfos)
    {
        if (Pair.Value->GetComponent() != Kind)
            continue;
        FFoliageInstance Instance;
        Instance.Location = Location;
        Instance.Rotation = Rotation;
        Instance.DrawScale3D = Scale;
        const int32 Before = Kind->GetInstanceCount();
        Foliage->Modify();
        Pair.Value->AddInstance(Foliage, Pair.Key, Instance);
        Json->SetStringField(TEXT("kind"), Pair.Key ? Pair.Key->GetPathName() : FString());
        Json->SetNumberField(TEXT("instances_before"), Before);
        Json->SetNumberField(TEXT("instances"), Kind->GetInstanceCount());
        return Finish(Json,
            Kind->GetInstanceCount() == Before + 1 ? FString()
                                                   : FString(TEXT("foliage-add-the-component-did-not-take-it")));
    }
    return Finish(Json, TEXT("foliage-add-the-foliage-actor-has-no-record-of-that-kind"));
}

void* FLentTemplates::Keep(UObject* Object)
{
    const UClass* Class = Object->GetClass();
    {
        TArray<UObject*> Named;
        FReferenceFinder Finder(Named, nullptr, false, false, false, false);
        Finder.FindReferences(Object);
        for (UObject* One : Named)
            if (One)
                Held.AddUnique(One);
    }
    void* Memory = FMemory::Malloc(Class->GetPropertiesSize(), Class->GetMinAlignment());
    FMemory::Memzero(Memory, Class->GetPropertiesSize());
    for (TFieldIterator<FProperty> Field(Class); Field; ++Field)
    {
        Field->InitializeValue_InContainer(Memory);
        Field->CopyCompleteValue_InContainer(Memory, Object);
    }
    return Memory;
}

void FLentTemplates::GiveBack(FOne& One)
{
    UObject* Object = One.Object.Get();
    if (One.Before && Object)
    {
        for (TFieldIterator<FProperty> Field(Object->GetClass()); Field; ++Field)
            Field->CopyCompleteValue_InContainer(Object, One.Before);
        for (TFieldIterator<FProperty> Field(Object->GetClass()); Field; ++Field)
            Field->DestroyValue_InContainer(One.Before);
        FMemory::Free(One.Before);
    }
    // An object that is gone took its class's layout with it. Its copy is not walked then.
    One.Before = nullptr;
}

void FLentTemplates::Lend(const FString& Package, const FString& Root, const FP4KadanSourceBinding* ProvenSource)
{
    FP4KadanSourceBinding ReadSource;
    FString Error;
    if (!ProvenSource)
    {
        if (!ReadClassSource(Package, Root, ReadSource, Error))
        {
            Problems.Add(Package + TEXT(": ") + Error);
            return;
        }
        ProvenSource = &ReadSource;
    }
    if (ProvenSource->Package != Package || !P4KadanSourceBindingCurrent(*ProvenSource, Error))
    {
        Problems.Add(Package + TEXT(": class source snapshot changed before lending: ") + Error);
        return;
    }
    FFile Class;
    Class.Header.assign(ProvenSource->Header.GetData(), ProvenSource->Header.GetData() + ProvenSource->Header.Num());
    Class.Body.assign(ProvenSource->Body.GetData(), ProvenSource->Body.GetData() + ProvenSource->Body.Num());
    if (!ReadNodes(Class, Package, Error))
    {
        Problems.Add(Package + TEXT(": ") + Error);
        return;
    }
    UPackage* Loaded = LoadPackage(nullptr, *Package, LOAD_NoWarn | LOAD_Quiet);
    if (!Loaded)
    {
        Problems.Add(Package + TEXT(": the class is not in the project"));
        return;
    }
    const bool WasDirty = Loaded->IsDirty();
    TMap<int32, UObject*> Made;
    TArray<int32> Templates;
    for (int32 Index = 1; Index <= Class.Nodes.Num(); ++Index)
    {
        const FNode& Node = Class.Node(Index);
        UObject* Live = StaticFindObject(UObject::StaticClass(), Loaded, *Node.Path);
        if (!Live)
            continue;
        Made.Add(Index, Live);
        if (Node.Undecoded.IsEmpty() && (Node.Flags & uint32(RF_ClassDefaultObject | RF_ArchetypeObject)) != 0 &&
            P4SameText(Live->GetClass()->GetPathName(), Node.Class))
            Templates.Add(Index);
    }
    TArray<FOne> Mine;
    for (const int32 Index : Templates)
    {
        FOne One;
        One.Object = Made[Index];
        One.Before = Keep(Made[Index]);
        Mine.Add(One);
    }
    bool Fine = true;
    int32 Count = 0;
    TArray<FString> NotHeld, NotShipped;
    for (const int32 Index : Templates)
    {
        FString StorageError;
        Unheld(Made[Index]->GetClass(), Class.Nodes[Index - 1].Values, Count, 0, nullptr, FString(), FString(), nullptr,
            &StorageError);
        if (!StorageError.IsEmpty())
        {
            Error = StorageError;
            Fine = false;
            break;
        }
        if (!Apply(Class, Index, Made[Index], Made, true, Error, &NotHeld, &NotShipped))
        {
            Fine = false;
            break;
        }
    }
    Loaded->SetDirtyFlag(WasDirty);
    if (!Fine)
    {
        for (int32 Index = Mine.Num(); Index-- > 0;)
            GiveBack(Mine[Index]);
        Problems.Add(Package + TEXT(": ") + Error);
        return;
    }
    Kept.Append(Mine);
    Classes.Add(Package);
}

void FLentTemplates::Return()
{
    for (int32 Index = Kept.Num(); Index-- > 0;)
        GiveBack(Kept[Index]);
    Kept.Reset();
    Held.Reset();
}

// Why an added actor is not one that a lighting build of the mod's own can light, as a sentence for the
// person. Empty when it is one: a static mesh actor of the engine's own class with a mesh, whose mesh
// does not move, attached to nothing and with nothing attached. Tied says that it names another actor
// of the level or is named by one, which the caller finds from what the level says.
FString WhyNotLit(const AActor* Actor, bool Tied)
{
    if (!Actor || Actor->IsPendingKill())
        return TEXT("It is not in the level.");
    if (Actor->GetClass() != AStaticMeshActor::StaticClass())
        return TEXT("It is not a static mesh actor.");
    const UStaticMeshComponent* Part = CastChecked<AStaticMeshActor>(Actor)->GetStaticMeshComponent();
    if (!Part || !Part->GetStaticMesh())
        return TEXT("It has no mesh.");
    if (Part->Mobility != EComponentMobility::Static)
        return TEXT("Its mesh can move, and the game lights what moves by itself.");
    TArray<AActor*> Attached;
    Actor->GetAttachedActors(Attached);
    if (Tied || Actor->GetAttachParentActor() || Attached.Num())
        return TEXT("It is tied to another actor of the level.");
    return FString();
}

// The added actors that a lighting build can light, as a level held in the editor at LitLevel and never
// saved, so that the export can light it inside the open map and cook it with its lighting data. A mod's
// level has one set of built lighting, and an actor of the game's that a mod carries keeps naming the
// game's, so what is to be lit by a build of the mod's own is a level by itself. Existing source or build
// data at either destination is refused: this producer has no authority to replace it. The caller
// owns the level from here and throws it away when the export ends.
FString SaveLit(UWorld* World, const FString& LitLevel, const TArray<FString>& Names)
{
    const FString Source = World->GetOutermost()->GetName(), Short = FPackageName::GetLongPackageAssetName(LitLevel);
    if (!P4IsValidContentPackage(LitLevel) || P4SameText(LitLevel, Source))
        return TEXT("level-lighting-path-unusable");
    for (const FString& Package : {LitLevel, LitLevel + TEXT("_BuiltData")})
    {
        if (FindPackage(nullptr, *Package))
            return TEXT("level-lighting-earlier-level-is-open-in-the-editor: ") + Package;
        if (FPackageName::DoesPackageExist(Package))
            return TEXT("level-lighting-destination-already-exists: ") + Package;
        FString Stem;
        if (!FPackageName::TryConvertLongPackageNameToFilename(Package, Stem))
            return TEXT("level-lighting-path-unusable: ") + Package;
        for (const FString& Suffix : {FString(TEXT(".umap")), FString(TEXT(".uasset")), FString(TEXT(".uexp")),
                 FString(TEXT(".ubulk")), FString(TEXT(".uptnl"))})
            if (IFileManager::Get().FileExists(*(Stem + Suffix)))
                return TEXT("level-lighting-destination-bundle-already-exists: ") + Package;
    }
    UWorld* Made = UWorld::CreateWorld(
        EWorldType::Inactive, false, FName(*Short), CreatePackage(*LitLevel), false, ERHIFeatureLevel::Num);
    if (!Made || !Made->PersistentLevel)
        return TEXT("level-lighting-engine-made-no-level");
    Made->SetFlags(RF_Public | RF_Standalone);
    GEditor->InitBuilderBrush(Made);
    FString Problem;
    for (const FString& Name : Names)
    {
        AActor* Actor = FindObject<AActor>(World->PersistentLevel, *Name);
        FObjectDuplicationParameters Copying(Actor, Made->PersistentLevel);
        Copying.DestName = Actor->GetFName();
        AActor* Copy = Cast<AActor>(StaticDuplicateObjectEx(Copying));
        if (!Copy)
        {
            Problem = TEXT("level-lighting-actor-not-made: ") + Name;
            break;
        }
        Made->PersistentLevel->Actors.Add(Copy);
        // A copy's place is not copied with it until its components are placed, and its lighting
        // identities would be the editor actor's. The build gives the copy identities of its own.
        TInlineComponentArray<UStaticMeshComponent*> Parts(Copy);
        for (UStaticMeshComponent* Part : Parts)
            for (FStaticMeshComponentLODInfo& Lod : Part->LODData)
                Lod.MapBuildDataId.Invalidate();
        if (USceneComponent* Root = Copy->GetRootComponent())
            Root->UpdateComponentToWorld();
    }
    if (!Problem.IsEmpty())
        Discard(Made);
    return Problem;
}

// The actors added to an imported level, as a level of the mod's own. A mod does not carry an edited
// copy of a game level, so what was added to one goes into a level the mod loads into the map. The
// level is made new at ModLevel, each added actor is made in it from the one in the game's level,
// it is saved the way a cook saves it under Folder (laid out as the game's content is, for packing),
// and it is thrown away: the editor's level with the actors in it stays what the person edits. An
// added actor that refers to something in the game's level is refused: the reference would not hold
// across two levels.
//
// Replaced names actors of the game's that were changed otherwise. A list of level changes can take
// such an actor out, so the changed actor is carried in the mod's level in its place. What a lighting
// build gave it goes with it: its components keep the ids the game's file has, and the mod's level
// names the game's own lighting data, which the mod does not carry. Refused: an actor of which the
// editor does not hold all the game's file says, one that something else in the level refers to, and
// one with child actors.
//
// LitLevel, when given, is where the added actors that a lighting build of the mod's own can light are
// put, as a level held in the editor and not saved (SaveLit). They are then left out of the level made here, which
// has no lighting data of its own. The others are told with why (WhyNotLit). When nothing is left
// for the level made here, it is not made, and the answer says so.
FP4LevelExportResult ExportActors(UWorld* World, const FString& ModLevel, const FString& Folder,
    const TArray<FString>& Replaced = TArray<FString>(), const FString& GameFiles = FString(),
    const FString& LitLevel = FString(), const TArray<FString>& Leave = TArray<FString>(),
    const TFunction<bool(UWorld*, FString&)>& PrepareCook = nullptr,
    const TFunction<FString(const AActor*)>& WhyNotBaked = nullptr)
{
    FP4LevelExportResult Result;
    const auto Refuse = [&](EP4LevelExportError Code, const FString& Detail)
    {
        Result.Error = Code;
        Result.ErrorDetail = Detail;
        return Result;
    };
    if (!IsInGameThread())
        return Refuse(EP4LevelExportError::EditorThreadRequired, TEXT("level-export-requires-editor-thread"));
    if (!IsValid(World) || !World->PersistentLevel || !GEditor)
        return Refuse(EP4LevelExportError::LevelRequired, TEXT("level-export-needs-a-level"));
    TStrongObjectPtr<UWorld> HeldSourceWorld(World);
    const FString Source = World->GetOutermost()->GetName(), Short = FPackageName::GetLongPackageAssetName(ModLevel);
    if (!P4IsValidContentPackage(ModLevel) || P4SameText(ModLevel, Source) ||
        (!LitLevel.IsEmpty() && P4SameText(ModLevel, LitLevel)))
        return Refuse(EP4LevelExportError::InvalidModPath, TEXT("level-export-path-unusable"));
    if (FPackageName::DoesPackageExist(ModLevel) || FindPackage(nullptr, *ModLevel))
        return Refuse(EP4LevelExportError::OccupiedModPath, TEXT("level-export-path-is-taken-in-the-project"));
    if (Folder.IsEmpty())
        return Refuse(EP4LevelExportError::FolderRequired, TEXT("level-export-needs-a-folder"));
    if (FPaths::IsRelative(Folder))
        return Refuse(EP4LevelExportError::AbsoluteFolderRequired, TEXT("level-export-needs-a-whole-folder-path"));
    const FP4LevelChangesResult Said = AnalyzeChanges(World);
    if (!Said.IsValid())
        return Refuse(EP4LevelExportError::ChangesFailed, P4LevelChangesErrorText(Said));
    TArray<FString> Names = Said.Added;
    Names.RemoveAll([&](const FString& Name) { return Leave.Contains(Name); });
    Result.SourcePackage = World->GetOutermost()->GetName();
    Result.Package = ModLevel;
    Result.Level = Said.Level;
    int32 AddedCount = Names.Num();
    const FString Level = World->GetName() + TEXT(".") + World->PersistentLevel->GetName();
    TArray<FString> Now, Lighting, RunLines;
    // A mod's level says of an actor only what differs from the project's class, and the game fills the
    // rest from its own class. Where the two classes do not say the same, the actor in the game would
    // not be the actor in the editor.
    // Classes used by actors carried into the mod receive the game's defaults for the export.
    // Untouched original actors and their classes continue to come from the game's own packages.
    FLentTemplates Lent;
    TArray<FString> NotTheGames;
    World->GetOutermost()->GetMetaData()->GetValue(World, ClassesNotTheGamesKey).ParseIntoArrayLines(NotTheGames);
    const auto LendActorClasses = [&](const TArray<FString>& ActorNames)
    {
        TSet<FString> Needed;
        const auto NeedClass = [&](const UObject* Object)
        {
            for (const UClass* Class = Object ? Object->GetClass() : nullptr; Class; Class = Class->GetSuperClass())
                Needed.Add(Class->GetOutermost()->GetName());
        };
        for (const FString& Name : ActorNames)
        {
            AActor* Actor = FindObject<AActor>(World->PersistentLevel, *Name);
            TArray<AActor*> Family;
            if (Actor)
            {
                Actor->GetAllChildActors(Family, true);
                Family.Add(Actor);
            }
            for (const AActor* One : Family)
            {
                NeedClass(One);
                if (One)
                    for (const UActorComponent* Component : One->GetComponents())
                        NeedClass(Component);
            }
        }
        TArray<FString> Ordered;
        for (const FString& Name : NotTheGames)
            if (Needed.Contains(Name) && !Lent.Classes.Contains(Name))
                Ordered.Add(Name);
        const auto Depth = [](const FString& Name)
        {
            int32 Count = 0;
            const UClass* Class = LoadObject<UClass>(nullptr, *P4ClassPathOf(Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
            for (; Class; Class = Class->GetSuperClass())
                ++Count;
            return Count;
        };
        Ordered.StableSort([&](const FString& A, const FString& B) { return Depth(A) < Depth(B); });
        for (const FString& Name : Ordered)
            if (!GameFiles.IsEmpty())
                Lent.Lend(Name, GameFiles);
            else
                Lent.Problems.Add(Name + TEXT(": the original class source folder is required."));
        Result.LentClasses = Lent.Classes;
        return !Lent.Problems.Num();
    };
    {
        TArray<FString> Asked = Names;
        Asked.Append(Replaced);
        // Original families that remain in the game need no editor default reconstruction.
        // A refused collision reference in one must not block an unrelated creator addition.
        if (!LendActorClasses(Asked))
            return Refuse(EP4LevelExportError::ClassDefaultsMismatch,
                TEXT("level-export-a-class-could-not-be-given-what-the-game's-says: ") + Lent.Problems[0]);
        for (const FString& Name : Asked)
        {
            const AActor* Actor = FindObject<AActor>(World->PersistentLevel, *Name);
            TArray<AActor*> With;
            if (Actor)
            {
                Actor->GetAllChildActors(With, true);
                With.Add(const_cast<AActor*>(Actor));
            }
            for (const AActor* One : With)
                for (const UClass* Class = One ? One->GetClass() : nullptr; Class; Class = Class->GetSuperClass())
                    if (NotTheGames.Contains(Class->GetOutermost()->GetName()) &&
                        !Lent.Classes.Contains(Class->GetOutermost()->GetName()))
                        return Refuse(EP4LevelExportError::ClassDefaultsMismatch,
                            TEXT("level-export-the-actor's-class-does-not-say-what-the-game's-does: ") + Name +
                                TEXT(": ") + Class->GetOutermost()->GetName());
        }
    }
    // A reference does not hold from one level to another. So an actor that goes into the mod's level
    // takes with it every actor of the game's that it names and every one that names it, and those
    // take theirs: each is carried as a changed actor is, and the game's own is taken out. What the
    // level itself names (its script actor) cannot go.
    TArray<FString> Carry = Replaced, Pulled;
    // Every actor that names another of the level or is named by one.
    TSet<FString, FLocKeySetFuncs> Tied;
    if (Replaced.Num() || Names.Num())
    {
        {
            FFile Current;
            FString Problem;
            TArray<FString> EngineMade;
            if (!Cooked(World, true, Current, Problem))
                return Refuse(EP4LevelExportError::CookFailed, Problem);
            Lines(Current, Now, EngineMade);
        }
        TSet<FString, FLocKeySetFuncs> Actors;
        for (const FString& Line : Now)
        {
            const FString Owner = OwnerOf(Line, Level);
            if (!Owner.IsEmpty())
                Actors.Add(Owner);
        }
        const FString Prefix = Level + TEXT("."), List = Level + TEXT(" | actors ");
        const auto NamedIn = [&](const FString& Value, TArray<FString>& Out)
        {
            for (int32 From = 0;
                (From = Value.Find(Prefix, ESearchCase::CaseSensitive, ESearchDir::FromStart, From)) != INDEX_NONE;
                From += Prefix.Len())
            {
                int32 End = From + Prefix.Len();
                while (End < Value.Len() &&
                    (FChar::IsAlnum(Value[End]) || Value[End] == TEXT('_') || Value[End] == TEXT('-')))
                    ++End;
                const FString Found = Value.Mid(From + Prefix.Len(), End - From - Prefix.Len());
                if (Actors.Contains(Found))
                    Out.AddUnique(Found);
            }
        };
        TArray<FString> Front = Names;
        Front.Append(Replaced);
        TSet<FString, FLocKeySetFuncs> Group(Front);
        for (int32 At = 0; At < Front.Num(); ++At)
        {
            const FString One = Front[At];
            for (const FString& Line : Now)
            {
                if (Line.StartsWith(List, ESearchCase::CaseSensitive))
                    continue;
                const int32 Bar = Line.Find(TEXT(" | "), ESearchCase::CaseSensitive);
                if (Bar == INDEX_NONE)
                    continue;
                const FString Owner = OwnerOf(Line, Level);
                TArray<FString> Named;
                NamedIn(Line.Mid(Bar + 3), Named);
                if (P4SameText(Owner, One))
                {
                    for (const FString& Other : Named)
                    {
                        if (!P4SameText(Other, One))
                        {
                            Tied.Add(One);
                            Tied.Add(Other);
                        }
                        if (!Group.Contains(Other))
                        {
                            Group.Add(Other);
                            Front.Add(Other);
                        }
                    }
                }
                else if (Named.Contains(One))
                {
                    if (Owner.IsEmpty())
                        return Refuse(EP4LevelExportError::ReferenceCrossesLevel,
                            TEXT("level-export-the-level-itself-refers-to-an-actor-that-would-be-carried: ") + One +
                                TEXT(": ") + Line.Left(160));
                    Tied.Add(One);
                    Tied.Add(Owner);
                    if (!Group.Contains(Owner))
                    {
                        Group.Add(Owner);
                        Front.Add(Owner);
                    }
                }
            }
        }
        for (const FString& One : Front)
            if (!Names.Contains(One) && !Replaced.Contains(One))
            {
                Carry.Add(One);
                Pulled.Add(One);
            }
        // An actor that comes along is of the game's too, and its class has to be the game's as well.
        if (!LendActorClasses(Pulled))
            return Refuse(EP4LevelExportError::ClassDefaultsMismatch,
                TEXT("level-export-a-class-could-not-be-given-what-the-game's-says: ") + Lent.Problems[0]);
        for (const FString& One : Pulled)
        {
            const AActor* Actor = FindObject<AActor>(World->PersistentLevel, *One);
            TArray<AActor*> With;
            if (Actor)
            {
                Actor->GetAllChildActors(With, true);
                With.Add(const_cast<AActor*>(Actor));
            }
            for (const AActor* Each : With)
                for (const UClass* Class = Each ? Each->GetClass() : nullptr; Class; Class = Class->GetSuperClass())
                    if (NotTheGames.Contains(Class->GetOutermost()->GetName()) &&
                        !Lent.Classes.Contains(Class->GetOutermost()->GetName()))
                        return Refuse(EP4LevelExportError::ClassDefaultsMismatch,
                            TEXT("level-export-the-actor's-class-does-not-say-what-the-game's-does: ") + One +
                                TEXT(": ") + Class->GetOutermost()->GetName());
        }
    }
    // The added actors a lighting build of the mod's own can light go into a level by themselves.
    TArray<FString> Lit;
    if (!LitLevel.IsEmpty())
    {
        for (const FString& Name : Names)
        {
            const AActor* Added = FindObject<AActor>(World->PersistentLevel, *Name);
            FString Why = WhyNotLit(Added, Tied.Contains(Name));
            if (Why.IsEmpty() && WhyNotBaked)
                Why = WhyNotBaked(Added);
            if (Why.IsEmpty())
            {
                Lit.Add(Name);
                continue;
            }
            FP4LevelUnlitActor Unlit;
            Unlit.Actor = Name;
            Unlit.Reason = Why;
            Result.UnlitActors.Add(MoveTemp(Unlit));
        }
        if (Lit.Num())
        {
            Names.RemoveAll([&](const FString& Name) { return Lit.Contains(Name); });
            AddedCount = Names.Num();
        }
        Result.LightingConsidered = true;
        Result.LitActors = Lit;
    }
    if (Carry.Num())
    {
        TSet<FString, FLocKeySetFuncs> Changed;
        for (const FP4LevelActorChanges& Value : Said.OtherChanged)
            Changed.Add(Value.Actor);
        UMetaData* Record = World->GetOutermost()->GetMetaData();
        if (!Record->HasValue(World, RunLinesKey) || !Unpack(Record->GetValue(World, RunLinesKey), RunLines) ||
            !Unpack(Record->GetValue(World, LightingKey), Lighting))
            return Refuse(EP4LevelExportError::OriginalComparisonMissing,
                TEXT("level-export-needs-what-the-import-kept-of-the-level: import the level again"));
        for (const FString& Name : Carry)
        {
            if (Replaced.Contains(Name) && !Changed.Contains(Name))
                return Refuse(EP4LevelExportError::ReplacementNotChanged,
                    TEXT("level-export-not-a-changed-actor-of-the-game's: ") + Name);
            // What the level says of it after a construction script run and the game's file does not
            // cannot be taken back out of a copy.
            for (const FString& Line : RunLines)
                if (Line.StartsWith(TEXT("+"), ESearchCase::CaseSensitive) &&
                    P4SameText(OwnerOf(Line.Mid(1), Level), Name))
                    return Refuse(EP4LevelExportError::ConstructionAddsValues,
                        TEXT("level-export-the-editor's-level-says-what-the-game's-file-does-not: ") +
                            Line.Mid(1).Left(160));
            if (!FindObject<AActor>(World->PersistentLevel, *Name))
                return Refuse(EP4LevelExportError::ActorMissing, TEXT("level-export-changed-actor-not-found: ") + Name);
            Names.Add(Name);
        }
    }
    if (!Names.Num() && Lit.Num())
    {
        // Everything added is lit in its own level, and nothing of the game's is carried.
        const FString Problem = SaveLit(World, LitLevel, Lit);
        if (!Problem.IsEmpty())
            return Refuse(EP4LevelExportError::LightingSetupFailed, Problem);
        Result.LitLevel = LitLevel;
        Result.HasLevelDecision = true;
        Result.LevelMade = false;
        return Result;
    }
    if (!Names.Num())
        return Refuse(EP4LevelExportError::NoAddedActors, TEXT("level-export-nothing-was-added"));
    Result.HasLevelDecision = true;
    Result.LevelMade = true;
    UWorld* Made = UWorld::CreateWorld(
        EWorldType::Inactive, false, FName(*Short), CreatePackage(*ModLevel), false, ERHIFeatureLevel::Num);
    if (!Made || !Made->PersistentLevel)
        return Refuse(EP4LevelExportError::WorldCreationFailed, TEXT("level-export-engine-made-no-level"));
    // The editor's save of a level collects garbage first, and keeps only what stands by itself.
    Made->SetFlags(RF_Public | RF_Standalone);
    GEditor->InitBuilderBrush(Made);
    const auto Fail = [&](const FString& Why, EP4LevelExportError Code = EP4LevelExportError::CopyFailed)
    {
        Discard(Made);
        return Refuse(Code, Why);
    };
    // Each actor with the actors its components make (its child actors), which are actors of the level
    // in their own right and go with it. Stands says that the copy stands for an actor of the game's.
    struct FCarried
    {
        AActor* From;
        AActor* To;
        bool Stands;
    };
    TArray<FCarried> Carried;
    TMap<UObject*, UObject*> Twins;
    int32 ChildActors = 0;
    TArray<TPair<AActor*, bool>> Sources;
    for (const FString& Name : Names)
    {
        AActor* Actor = FindObject<AActor>(World->PersistentLevel, *Name);
        if (!Actor || Actor->IsPendingKill())
            return Fail(TEXT("level-export-added-actor-not-found: ") + Name);
        const bool Stands = Names.IndexOfByKey(Name) >= AddedCount;
        Sources.Emplace(Actor, Stands);
        TArray<AActor*> Children;
        Actor->GetAllChildActors(Children, true);
        for (AActor* Child : Children)
            if (Child && !Child->IsPendingKill() && Child->GetLevel() == World->PersistentLevel)
            {
                Sources.Emplace(Child, Stands);
                ++ChildActors;
            }
    }
    for (const TPair<AActor*, bool>& Entry : Sources)
    {
        AActor* Actor = Entry.Key;
        const FString Name = Actor->GetName();
        // A copy made by saving and loading in memory, as the editor copies a level to play in it: it
        // has what the actor would be saved with and nothing of its running state (its place in the
        // scene, its physics body). An actor spawned with the live actor as its template takes those too.
        FObjectDuplicationParameters Copying(Actor, Made->PersistentLevel);
        Copying.DestName = Actor->GetFName();
        // An added actor is a new one. A replaced one stands for the game's and keeps what a build gave
        // it, which the engine keeps only when it copies a level to play in it.
        if (Entry.Value)
            Copying.DuplicateMode = EDuplicateMode::PIE;
        TMap<UObject*, UObject*> Step;
        Copying.CreatedObjects = &Step;
        AActor* Copy = Cast<AActor>(StaticDuplicateObjectEx(Copying));
        if (!Copy)
            return Fail(TEXT("level-export-actor-not-made: ") + Name);
        Made->PersistentLevel->Actors.Add(Copy);
        Twins.Append(Step);
        Twins.Add(Actor, Copy);
        Carried.Add({Actor, Copy, Entry.Value});
    }
    // What one copy names of another's original is the other copy. A component and the actor it makes
    // name each other in ways the engine's copy leaves empty, so those are set.
    {
        const FObjectProperty* MadeActor = FindFProperty<FObjectProperty>(
            UChildActorComponent::StaticClass(), TEXT("ChildActor"));
        const FObjectPropertyBase* Maker = FindFProperty<FObjectPropertyBase>(
            AActor::StaticClass(), TEXT("ParentComponent"));
        for (const FCarried& One : Carried)
        {
            FArchiveReplaceObjectRef<UObject> Renamed(One.To, Twins, false, true, true);
            UChildActorComponent* Parent = One.From->GetParentComponent();
            UObject** Twin = Parent ? Twins.Find(Parent) : nullptr;
            if (!Parent)
                continue;
            if (!Twin || !MadeActor || !Maker)
                return Fail(TEXT("level-export-child-actor-not-tied-to-its-component: ") + One.From->GetName());
            MadeActor->SetObjectPropertyValue_InContainer(*Twin, One.To);
            Maker->SetObjectPropertyValue_InContainer(One.To, *Twin);
        }
    }
    // A replaced actor's components keep the ids the game's lighting build gave them, and the mod's
    // level names the game's lighting data for them. The data stays the game's: here it is an empty
    // object of that name for the length of the save, so that the level can name it.
    TMap<FString, TArray<FGuid>> Wanted;
    UMapBuildDataRegistry* Borrowed = nullptr;
    bool OwnRegistry = false;
    // A component that draws many instances has in the game's lighting data a place for each of them,
    // in their order. When its instances are no longer the ones the level had when it was imported,
    // those places belong to other instances, so the component does not claim the game's lighting data
    // any more: the game lights its instances as it lights what has no built lighting.
    int32 LightingLeft = 0;
    TSet<FString, FLocKeySetFuncs> SaidThen, SaidNow(Now);
    {
        TArray<FString> Then;
        if (Carry.Num() && Unpack(World->GetOutermost()->GetMetaData()->GetValue(World, ImportedKey), Then))
            SaidThen.Append(Then);
    }
    const auto Release = [&]()
    {
        if (!OwnRegistry || !Borrowed)
            return;
        Made->PersistentLevel->MapBuildData = nullptr;
        Borrowed->ClearFlags(RF_Public | RF_Standalone);
        Borrowed->MarkPendingKill();
        Borrowed->GetOutermost()->SetDirtyFlag(false);
        OwnRegistry = false;
    };
    for (const FCarried& One : Carried)
    {
        if (!One.Stands)
            continue;
        AActor* Actor = One.From;
        AActor* Copy = One.To;
        // The engine's copy leaves out what a copy is to get anew, which for a placed actor is what a
        // build gave it (its place in the precomputed visibility). A replaced actor keeps it: it stands
        // where the game's actor stood. References are left as the copy has them.
        {
            TArray<UObject*> Inside;
            GetObjectsWithOuter(Copy, Inside, true);
            Inside.Add(Copy);
            for (UObject* Twin : Inside)
            {
                const UObject* Theirs = Twin == Copy
                    ? Actor
                    : StaticFindObject(Twin->GetClass(), Actor, *Twin->GetPathName(Copy), true);
                if (!Theirs || Theirs->GetClass() != Twin->GetClass())
                    continue;
                for (TFieldIterator<FProperty> Field(Twin->GetClass()); Field; ++Field)
                {
                    TArray<const FStructProperty*> Seen;
                    if (Field->HasAnyPropertyFlags(CPF_DuplicateTransient) &&
                        !Field->HasAnyPropertyFlags(CPF_Transient | CPF_EditorOnly) &&
                        !Field->ContainsObjectReference(Seen))
                        Field->CopyCompleteValue_InContainer(Twin, Theirs);
                }
            }
        }
        TInlineComponentArray<UStaticMeshComponent*> Parts(Copy);
        for (UStaticMeshComponent* Part : Parts)
        {
            const UObject* Original = StaticFindObjectFast(
                UStaticMeshComponent::StaticClass(), Actor, Part->GetFName());
            const FString Key = Original ? Original->GetPathName(World->GetOutermost()).Replace(TEXT(":"), TEXT("."))
                                         : FString();
            const FString* Line = Lighting.FindByPredicate(
                [&](const FString& One) { return One.StartsWith(Key + TEXT("\t"), ESearchCase::CaseSensitive); });
            if (Key.IsEmpty() || !Line)
                continue;
            if (Part->IsA<UInstancedStaticMeshComponent>())
            {
                const FString Start = Key + TEXT(" | ");
                bool Changed = false;
                for (const FString& NowLine : Now)
                    Changed = Changed ||
                        (NowLine.StartsWith(Start, ESearchCase::CaseSensitive) && !SaidThen.Contains(NowLine));
                for (const FString& ThenLine : SaidThen)
                    Changed = Changed ||
                        (ThenLine.StartsWith(Start, ESearchCase::CaseSensitive) && !SaidNow.Contains(ThenLine));
                if (Changed)
                {
                    ++LightingLeft;
                    continue;
                }
            }
            TArray<FString> Ids;
            TArray<FGuid> Guids;
            Line->Mid(Key.Len() + 1).ParseIntoArray(Ids, TEXT(","), true);
            for (const FString& Id : Ids)
            {
                TArray<FString> Words;
                Id.ParseIntoArray(Words, TEXT(" "), true);
                if (Words.Num() != 4)
                    return Fail(TEXT("level-export-lighting-ids-unreadable: ") + Key);
                Guids.Emplace(FParse::HexNumber(*Words[0]), FParse::HexNumber(*Words[1]), FParse::HexNumber(*Words[2]),
                    FParse::HexNumber(*Words[3]));
            }
            Part->SetLODDataCount(Guids.Num(), Guids.Num());
            for (int32 Lod = 0; Lod < Guids.Num() && Lod < Part->LODData.Num(); ++Lod)
                Part->LODData[Lod].MapBuildDataId = Guids[Lod];
            Wanted.Add(Part->GetPathName(Made->GetOutermost()).Replace(TEXT(":"), TEXT(".")), Guids);
        }
    }
    // A replaced actor is the game's actor with the edit, so it gets back what the editor's level does
    // not hold of it: what a construction script run did not keep and what the game's own code made, as
    // the game's file says them. A value that refers to something of the level is not put back yet.
    TArray<FString> PutBack, GameMade;
    int32 Distances = 0, FromClass = 0;
    for (int32 Slot = AddedCount; Slot < Names.Num(); ++Slot)
    {
        AActor* Copy = FindObject<AActor>(Made->PersistentLevel, *Names[Slot]);
        const FString Base = Level + TEXT(".") + Names[Slot];
        for (const FString& Marked : RunLines)
        {
            const bool Lost = Marked.StartsWith(TEXT("-"), ESearchCase::CaseSensitive),
                       Coded = Marked.StartsWith(TEXT("g"), ESearchCase::CaseSensitive);
            const FString Line = Marked.Mid(1);
            if ((!Lost && !Coded) || !P4SameText(OwnerOf(Line, Level), Names[Slot]))
                continue;
            // The distance a cull distance volume gave a component is worked out again by the editor's
            // save, from the volumes of the level being saved, which the mod's level has none of.
            if (Coded && Line.Contains(TEXT(" | cachedmaxdrawdistance = "), ESearchCase::CaseSensitive))
            {
                ++Distances;
                continue;
            }
            const int32 Bar = Line.Find(TEXT(" | "), ESearchCase::CaseSensitive);
            const int32 Is = Bar == INDEX_NONE
                ? INDEX_NONE
                : Line.Find(TEXT(" = "), ESearchCase::CaseSensitive, ESearchDir::FromStart, Bar);
            const FString Path = Bar == INDEX_NONE ? FString() : Line.Left(Bar);
            if (Is == INDEX_NONE || !Path.StartsWith(Base, ESearchCase::CaseSensitive))
            {
                Release();
                return Fail(TEXT("level-export-cannot-put-back: ") + Line.Left(200));
            }
            FString Key = Line.Mid(Bar + 3, Is - Bar - 3);
            int32 Place = 0, Bracket = INDEX_NONE;
            if (Key.FindChar(TEXT('['), Bracket))
            {
                Place = FCString::Atoi(*Key.Mid(Bracket + 1));
                Key.LeftInline(Bracket);
            }
            // A child actor is told under the component that makes it ("Component>child").
            UObject* Target = Copy;
            FString Below = Path.Mid(Base.Len());
            Below.RemoveFromStart(TEXT("."), ESearchCase::CaseSensitive);
            for (int32 Guard = 0; Target && !Below.IsEmpty() && Guard < 8; ++Guard)
            {
                const int32 Mark = Below.Find(TEXT(">child"), ESearchCase::CaseSensitive);
                if (Mark == INDEX_NONE)
                {
                    Target = StaticFindObject(UObject::StaticClass(), Target, *Below);
                    Below.Reset();
                    break;
                }
                UChildActorComponent* Parent = Cast<UChildActorComponent>(
                    StaticFindObject(UObject::StaticClass(), Target, *Below.Left(Mark)));
                Target = Parent ? Parent->GetChildActor() : nullptr;
                Below = Below.Mid(Mark + 6);
                Below.RemoveFromStart(TEXT("."), ESearchCase::CaseSensitive);
            }
            const FProperty* Property = Target ? Target->GetClass()->FindPropertyByName(FName(*Key)) : nullptr;
            FDecodedValue Value;
            if (!Property || Place < 0 || Place >= Property->ArrayDim ||
                !P4MapKit::ReadDecodedJson(std::string(TCHAR_TO_UTF8(*Line.Mid(Is + 3))), Value))
            {
                Release();
                return Fail(TEXT("level-export-cannot-put-back: ") + Line.Left(200));
            }
            FString Problem;
            if (!PutValue(Target, Property, Place, Value, Line.Left(Is), Problem))
            {
                Release();
                return Fail(TEXT("level-export-cannot-put-back: ") + Problem.Left(240));
            }
            // A value the object's class already has is one a saved level does not say: the game's file
            // says it only because the game's construction script set it. The copy has it from its class.
            const UObject* Class = Target->GetArchetype();
            if (Coded && Class && Class->GetClass() == Target->GetClass() &&
                Property->Identical_InContainer(Target, Class, Place))
            {
                ++FromClass;
                continue;
            }
            (Lost ? PutBack : GameMade).Add(Line);
        }
    }
    if (Wanted.Num())
    {
        const FString* Line = Lighting.FindByPredicate(
            [](const FString& One) { return One.StartsWith(TEXT("registry\t"), ESearchCase::CaseSensitive); });
        const FString Registry = Line ? Line->Mid(9) : FString();
        int32 Dot = INDEX_NONE;
        if (!P4IsContentPackage(Registry) || !Registry.FindChar(TEXT('.'), Dot))
            return Fail(
                TEXT("level-export-the-game's-level-names-no-lighting-data"), EP4LevelExportError::LightingDataMissing);
        Borrowed = Cast<UMapBuildDataRegistry>(StaticLoadObject(
            UMapBuildDataRegistry::StaticClass(), nullptr, *Registry, nullptr, LOAD_NoWarn | LOAD_Quiet));
        if (!Borrowed)
        {
            Borrowed = NewObject<UMapBuildDataRegistry>(
                CreatePackage(*Registry.Left(Dot)), FName(*Registry.Mid(Dot + 1)), RF_Public | RF_Standalone);
            OwnRegistry = true;
        }
        Made->PersistentLevel->MapBuildData = Borrowed;
    }
    // Where the game's content has the package: the project's folder name is the game's.
    FString At = Folder / P4ContainerRelativePath(ModLevel);
    FPaths::NormalizeFilename(At);
    if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(At), true))
    {
        Release();
        return Fail(TEXT("level-export-folder-not-made"), EP4LevelExportError::FolderCreationFailed);
    }
    FFile File;
    FString Error;
    TArray<FString> Content, EngineMade;
    // Generated assets may replace MaterialInterface references only in this
    // private copy. The editable imported world is never changed. Carried game
    // actors require their existing exact comparison and do not use this seam.
    if (PrepareCook && Carry.Num())
    {
        Release();
        return Fail(TEXT("This material conversion cannot include copied game actors."));
    }
    if (PrepareCook && !PrepareCook(Made, Error))
    {
        Release();
        return Fail(Error);
    }
    const bool Written = Cooked(Made, true, File, Error, At);
    Release();
    if (!Written)
        return Fail(Error, EP4LevelExportError::CookFailed);
    // What the level refers to outside itself. Nothing of it may be in the game's level.
    for (const auto& Import : File.Package.Imports)
        if (Import.Outer == 0 && P4SameText(Text(Import.Name), Source))
        {
            for (const TCHAR* Suffix : {TEXT(".umap"), TEXT(".uexp")})
                IFileManager::Get().Delete(*(At + Suffix), false, true, true);
            return Fail(TEXT("level-export-an-added-actor-refers-to-the-game's-level: ") + Source,
                EP4LevelExportError::ReferenceCrossesLevel);
        }
    Lines(File, Content, EngineMade);
    const auto Unwritten = [&](const FString& Why)
    {
        for (const TCHAR* Suffix : {TEXT(".umap"), TEXT(".uexp")})
            IFileManager::Get().Delete(*(At + Suffix), false, true, true);
        return Fail(Why, EP4LevelExportError::CookedActorMismatch);
    };
    // The ids as the cooked level has them, and each replaced actor as the editor's level has it.
    for (const FNode& Node : File.Nodes)
        if (const TArray<FGuid>* Ids = Wanted.Find(Node.Path))
        {
            TArray<FString> Said2;
            for (const FGuid& Id : *Ids)
                Said2.Add(FString::Printf(TEXT("%08x %08x %08x %08x"), Id.A, Id.B, Id.C, Id.D));
            if (Node.LightingIds != Said2)
                return Unwritten(TEXT("level-export-lighting-ids-not-kept: ") + Node.Path);
        }
    {
        const FString Mine = Short + TEXT(".") + World->PersistentLevel->GetName();
        for (int32 Slot = AddedCount; Slot < Names.Num(); ++Slot)
        {
            TSet<FString, FLocKeySetFuncs> Here, There;
            for (const FString& Line : Now)
                if (P4SameText(OwnerOf(Line, Level), Names[Slot]))
                    Here.Add(Line);
            for (const FString& Line : PutBack)
                if (P4SameText(OwnerOf(Line, Level), Names[Slot]))
                    Here.Add(Line);
            for (const FString& Line : Content)
                if (P4SameText(OwnerOf(Line, Mine), Names[Slot]))
                    There.Add(Line.Replace(*Mine, *Level, ESearchCase::CaseSensitive));
            TArray<FString> Missing, Extra;
            for (const FString& Line : Here)
                if (!There.Contains(Line) && Missing.Num() < 30)
                    Missing.Add(Line.Left(300));
            for (const FString& Line : There)
                if (!Here.Contains(Line) && Extra.Num() < 30)
                    Extra.Add(Line.Left(300));
            if (Missing.Num() || Extra.Num())
            {
                Result.MissingLines = MoveTemp(Missing);
                Result.ExtraLines = MoveTemp(Extra);
                return Unwritten(TEXT("level-export-the-copy-is-not-what-the-editor-has: ") + Names[Slot]);
            }
        }
    }
    {
        // What the game's code made is not among what a level is compared by. It has to be in the cooked copy.
        const FString Mine = Short + TEXT(".") + World->PersistentLevel->GetName();
        TSet<FString, FLocKeySetFuncs> Has;
        for (const FString& Line : EngineMade)
            Has.Add(Line.Replace(*Mine, *Level, ESearchCase::CaseSensitive));
        for (const FString& Line : GameMade)
        {
            if (Has.Contains(Line))
                continue;
            // What the copy says of that object instead, to tell the two apart.
            const int32 Bar = Line.Find(TEXT(" | "), ESearchCase::CaseSensitive);
            TArray<FString> Instead;
            for (const FString& Other : Has)
                if (Bar != INDEX_NONE && Other.StartsWith(Line.Left(Bar + 3), ESearchCase::CaseSensitive))
                    Instead.Add(Other.Left(300));
            for (const FString& Other : Content)
                if (Bar != INDEX_NONE &&
                    Other.Replace(*Mine, *Level, ESearchCase::CaseSensitive)
                        .StartsWith(Line.Left(Bar + 3), ESearchCase::CaseSensitive))
                    Instead.Add(Other.Left(300));
            Result.HasObjectDifference = true;
            Result.DifferentObjectLines = MoveTemp(Instead);
            return Unwritten(TEXT("level-export-the-copy-does-not-say: ") + Line.Left(200));
        }
    }
    if (!P4SameText(File.PackageName, ModLevel))
        return Unwritten(TEXT("level-export-package-name-differs"));
    Result.CountsReady = true;
    Result.ChildActors = ChildActors;
    Result.RestoredConstructionValues = PutBack.Num();
    Result.RestoredGameValues = GameMade.Num();
    Result.CullDistancesOmitted = Distances;
    Result.GameValuesFromClass = FromClass;
    Result.LightingComponents = Wanted.Num();
    Result.InstancesWithoutGameLighting = LightingLeft;
    for (int32 Slot = AddedCount; Slot < Names.Num(); ++Slot)
        Result.Replaced.Add(Names[Slot]);
    Result.PulledActors = Pulled;
    Result.Actors = Names;
    for (const TCHAR* Suffix : {TEXT(".umap"), TEXT(".uexp")})
        Result.Files.Add(At + Suffix);
    Result.Objects = File.Nodes.Num();
    Result.Lines = Content.Num();
    Discard(Made);
    // Save the editable lit source only after every non-lit actor/reference and
    // native cook check has passed. Failure leaves no usable cook proof.
    if (Lit.Num())
    {
        const FString Problem = SaveLit(World, LitLevel, Lit);
        if (!Problem.IsEmpty())
            return Refuse(EP4LevelExportError::LightingSetupFailed, Problem);
        Result.LitLevel = LitLevel;
    }
    return Result;
}
} // namespace P4LevelContent

FP4LitActorFactsResult P4InspectLitActorFactsNative(UWorld* World, const TArray<FString>& ActorNames)
{
    FP4LitActorFactsResult Result;
    const auto Refuse = [&](EP4LitActorFactsError Error, const FString& Detail)
    {
        Result.Error = Error;
        Result.ErrorDetail = Detail;
        return Result;
    };
    if (!IsInGameThread())
        return Refuse(EP4LitActorFactsError::EditorThreadRequired, TEXT("lit-actor-facts-editor-thread-required"));
    if (!IsValid(World) || !World->PersistentLevel)
        return Refuse(EP4LitActorFactsError::LevelRequired, TEXT("lit-actor-facts-level-required"));
    if (!ActorNames.Num() || ActorNames.Num() > 65536)
        return Refuse(EP4LitActorFactsError::ActorsRequired, TEXT("lit-actor-facts-bounded-actors-required"));
    TStrongObjectPtr<UWorld> Held(World);
    TSet<FName> Names;
    for (const FString& Name : ActorNames)
    {
        AStaticMeshActor* Actor = FindObject<AStaticMeshActor>(World->PersistentLevel, *Name);
        if (Name.IsEmpty() || Names.Contains(FName(*Name)) || !IsValid(Actor) ||
            Actor->GetOuter() != World->PersistentLevel || Actor->GetClass() != AStaticMeshActor::StaticClass() ||
            !Actor->GetName().Equals(Name, ESearchCase::CaseSensitive))
            return Refuse(EP4LitActorFactsError::ActorUnsupported,
                TEXT("lit-actor-facts-stock-static-mesh-actor-required: ") + Name);
        Names.Add(FName(*Name));
        Result.Actors.Add(Name);
    }
    Result.Package = World->GetOutermost()->GetName();
    P4LevelContent::FFile File;
    FString Error;
    if (!P4LevelContent::Cooked(World, true, File, Error))
        return Refuse(EP4LitActorFactsError::CookFailed, Error);
    TSet<int32> Actors, Included;
    for (int32 Index : File.Actors)
        if (Names.Contains(FName(*File.Node(Index).Name)))
        {
            const auto& Node = File.Node(Index);
            if (!Node.Class.Equals(TEXT("/Script/Engine.StaticMeshActor"), ESearchCase::CaseSensitive) ||
                !Result.Actors.ContainsByPredicate(
                    [&](const FString& Name) { return Name.Equals(Node.Name, ESearchCase::CaseSensitive); }))
                return Refuse(
                    EP4LitActorFactsError::ActorUnsupported, TEXT("lit-actor-facts-cooked-actor-identity-differs"));
            Actors.Add(Index);
        }
    if (Actors.Num() != ActorNames.Num())
        return Refuse(EP4LitActorFactsError::ActorUnsupported, TEXT("lit-actor-facts-cooked-actor-set-differs"));
    for (int32 Index = 1; Index <= File.Nodes.Num(); ++Index)
        if (Actors.Contains(File.ActorOf(Index)))
        {
            const auto& Node = File.Node(Index);
            const bool Actor = Actors.Contains(Index);
            if (!Node.Undecoded.IsEmpty() ||
                (!Actor &&
                    !Node.Class.Equals(TEXT("/Script/Engine.StaticMeshComponent"), ESearchCase::CaseSensitive)) ||
                (Actor ? !Node.PlainTail : Node.Lods < 0))
                return Refuse(EP4LitActorFactsError::NativeDataUnsupported,
                    TEXT("lit-actor-facts-native-object-unsupported: ") + Node.Path);
            Included.Add(Index);
        }
    TArray<FString> Content, DerivedLighting;
    P4LevelContent::Lines(File, Content, DerivedLighting, &Included);
    Result.Objects = Included.Num();
    Result.ContentLines = Content.Num();
    Content.Insert(FString::Printf(TEXT("lit-stock-static-actor-facts-v1\t%d\t%d\t%d"), Actors.Num(), Result.Objects,
                       Result.ContentLines),
        0);
    if (!P4LevelContent::Digest(Content, Result.Digest))
        return Refuse(EP4LitActorFactsError::DigestFailed, TEXT("lit-actor-facts-digest-failed"));
    return Result;
}

FString P4WhyNotLitByABuildOfTheMods(const AActor* Actor)
{
    return P4LevelContent::WhyNotLit(Actor, false);
}

bool P4CookWholeOriginalLevel(UWorld* World, const FString& Base, FString& Error)
{
    if (!IsInGameThread() || !IsValid(World))
    {
        Error = TEXT("A whole level is cooked from a loaded world on the editor thread.");
        return false;
    }
    P4LevelContent::FFile Out;
    return P4LevelContent::Cooked(World, true, Out, Error, Base);
}

FP4LevelExportResult P4ExportOriginalLevelActors(UWorld* World, const FString& ModLevel, const FString& Folder,
    const TArray<FString>& Replaced, const FString& GameFiles, const FString& LitLevel, const TArray<FString>& Leave,
    const TFunction<bool(UWorld*, FString&)>& PrepareCook, const TFunction<FString(const AActor*)>& WhyNotBaked)
{
    return P4LevelContent::ExportActors(
        World, ModLevel, Folder, Replaced, GameFiles, LitLevel, Leave, PrepareCook, WhyNotBaked);
}

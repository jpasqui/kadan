#include "P4GameMirror.h"
#include "P4GamePaths.h"
#include "P4CreatorSource.h"
#include "P4LevelContent.h"
#include "P4GamePreviewWorld.h"
#include "P4GameConnection.h"
#include "P4GameLink.h"
#include "Editor.h"
#include "Editor/TransBuffer.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInterface.h"
#include "Sound/SoundBase.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Engine/Note.h"
#include "Engine/Selection.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Model.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "JsonObjectConverter.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "Misc/ScopeExit.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"
#include "UObject/Package.h"
#include "Widgets/Notifications/SNotificationList.h"

namespace
{
constexpr double QueryEvery = 0.25;

UWorld* EditorWorld()
{
    return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
}

void SetEditable(AActor* Actor, bool Value)
{
    // UE4 exposes the getter but keeps the reflected editor flag protected.
    if (FBoolProperty* Property = FindFProperty<FBoolProperty>(AActor::StaticClass(), TEXT("bEditable")))
        Property->SetPropertyValue_InContainer(Actor, Value);
}

void LiveObject(UObject* Object)
{
    Object->SetFlags(RF_Transient);
    if (UActorComponent* Component = Cast<UActorComponent>(Object))
        Component->SetCanEverAffectNavigation(false);
}

EditorUtilities::FCopyOptions KeepOptions(bool bPreview = false)
{
    const auto Flags = static_cast<EditorUtilities::ECopyOptions::Type>(
        EditorUtilities::ECopyOptions::CallPostEditChangeProperty | EditorUtilities::ECopyOptions::CallPostEditMove |
        EditorUtilities::ECopyOptions::OnlyCopyEditOrInterpProperties |
        EditorUtilities::ECopyOptions::FilterBlueprintReadOnly |
        (bPreview ? EditorUtilities::ECopyOptions::PreviewOnly : 0));
    EditorUtilities::FCopyOptions Options(Flags);
    Options.PropertyFilter = [](FProperty& Property, UObject&)
    {
        // Content references are shared with the editor. Preview actors and components
        // have no PIE remapping table and must never be persisted into the real level.
        TArray<const FStructProperty*> Encountered;
        const FProperty* ContentProperty = &Property;
        if (const FArrayProperty* Array = CastField<FArrayProperty>(ContentProperty))
            ContentProperty = Array->Inner;
        const FObjectPropertyBase* Object = CastField<FObjectPropertyBase>(ContentProperty);
        const bool bContent = Object && Object->PropertyClass &&
            (Object->PropertyClass->IsChildOf(UStaticMesh::StaticClass()) ||
                Object->PropertyClass->IsChildOf(USkeletalMesh::StaticClass()) ||
                Object->PropertyClass->IsChildOf(UMaterialInterface::StaticClass()) ||
                Object->PropertyClass->IsChildOf(UTexture::StaticClass()) ||
                Object->PropertyClass->IsChildOf(USoundBase::StaticClass()));
        if (!bContent &&
            (Property.ContainsObjectReference(Encountered) || Property.ContainsWeakObjectReference() || Object))
            return false;
        const FName Name = Property.GetFName();
        return Name != TEXT("ActorLabel") && Name != TEXT("FolderPath") && Name != TEXT("bEditable") &&
            Name != TEXT("bHiddenEd") && Name != TEXT("bHiddenEdTemporary") && Name != TEXT("bHiddenEdLayer") &&
            Name != TEXT("GenerateInfo") && Name != TEXT("DebugUniqueId") && Name != TEXT("bCanEverAffectNavigation");
    };
    return Options;
}

void Toast(const FString& Text)
{
    FNotificationInfo Info(FText::FromString(Text));
    Info.ExpireDuration = 4.f;
    if (const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
        Item->SetCompletionState(SNotificationItem::CS_Fail);
}

FString LuaText(const FString& Value)
{
    return TEXT("\"") + Value.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\"")) + TEXT("\"");
}

FString LuaPlace(const FVector& At, const FRotator& Turn)
{
    return FString::Printf(TEXT("{x = %f, y = %f, z = %f}, {pitch = %f, yaw = %f, roll = %f}"), At.X, At.Y, At.Z,
        Turn.Pitch, Turn.Yaw, Turn.Roll);
}

// Lua that sizes the just spawned `made` as the actor is, when it is not at its natural size.
FString LuaScale(const AActor* Actor)
{
    const FVector Scale = Actor->GetActorScale3D();
    if (Scale.Equals(FVector::OneVector, 0.0001f))
        return FString();
    return FString::Printf(TEXT("local _,scaled=reflection.call(made,'SetActorScale3D',{['NewScale3D.X']=%.9g,")
                               TEXT("['NewScale3D.Y']=%.9g,['NewScale3D.Z']=%.9g}) ")
                                   TEXT("if type(scaled)=='string' then error(scaled) end "),
        Scale.X, Scale.Y, Scale.Z);
}

FString LuaPose(const AActor* Actor)
{
    const FVector L = Actor->GetActorLocation(), S = Actor->GetActorScale3D();
    const FRotator R = Actor->GetActorRotation();
    const FQuat Q = Actor->GetActorQuat();
    return FString::Printf(
        TEXT(
            "{location={x=%.9g,y=%.9g,z=%.9g},rotation={pitch=%.9g,yaw=%.9g,roll=%.9g},quaternion={x=%.9g,y=%.9g,z=%.9g,w=%.9g},scale={x=%.9g,y=%.9g,z=%.9g}}"),
        L.X, L.Y, L.Z, R.Pitch, R.Yaw, R.Roll, Q.X, Q.Y, Q.Z, Q.W, S.X, S.Y, S.Z);
}

// Fields of the game's own classes that hold plain values: what the Details panel shows and the game can take.
bool Mirrored(const FProperty* Property)
{
    // Placement identity stays exact in the original/template. JSON numbers cannot carry
    // every 64-bit DebugUniqueId, and a preview must never replace an actor's identity.
    if (Property->GetFName() == TEXT("GenerateInfo") || Property->GetFName() == TEXT("DebugUniqueId"))
        return false;
    if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated) ||
        !Property->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible))
        return false;
    if (Property->IsA<FObjectPropertyBase>() || Property->IsA<FInterfaceProperty>() ||
        Property->IsA<FDelegateProperty>() || Property->IsA<FMulticastDelegateProperty>())
        return false;
    const UClass* Owner = Property->GetOwnerClass();
    const FString Package = Owner ? Owner->GetOutermost()->GetName() : FString();
    return !Package.StartsWith(TEXT("/Script/Engine")) && !Package.StartsWith(TEXT("/Script/CoreUObject"));
}

// A class that is or derives from one the game has: one of its native classes or a Blueprint of its own.
bool GameClass(const UClass* Class)
{
    for (const UClass* Each = Class; Each; Each = Each->GetSuperClass())
        if (P4IsGameClass(Each))
            return true;
    return false;
}

// Keeps the open map's packages as they were: preview never leaves the level changed. Spawning, naming, moving
// and removing the live actors would otherwise mark the map as having unsaved changes.
struct FKeepClean
{
    TArray<UPackage*> Clean;
    explicit FKeepClean(UWorld* Level)
    {
        if (!Level)
            return;
        if (!Level->GetOutermost()->IsDirty())
            Clean.Add(Level->GetOutermost());
        for (ULevel* Each : Level->GetLevels())
            if (Each && !Each->GetOutermost()->IsDirty())
                Clean.AddUnique(Each->GetOutermost());
    }
    ~FKeepClean()
    {
        for (UPackage* Package : Clean)
            if (Package->IsDirty())
                Package->SetDirtyFlag(false);
    }
};

// Every entity with its kind, placement and pose, in one round trip.
const TCHAR* Query = TEXT(
    "local rows = {} "
    "for _, a in ipairs(entity.list() or {}) do "
    "  local p = entity.pose(a) "
    "  if p then local l, r = p.location, p.rotation "
    "    rows[#rows + 1] = {a, tostring(entity.kind(a)), tostring(entity.placement(a) or 0), l.x, l.y, l.z, r.pitch, r.yaw, r.roll} end "
    "end "
    " ");
} //namespace

FP4GameMirror::FP4GameMirror(FP4GameLink& InLink, const FString& InOwner) : Link(InLink), Owner(InOwner)
{
    SelectionHandle = USelection::SelectionChangedEvent.AddRaw(this, &FP4GameMirror::OnSelection);
    PropertyHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this, &FP4GameMirror::OnPropertyChanged);
    // Saving kept edits during preview must serialize the originals' real editor flags.
    // Restore preview presentation after both successful and failed world saves.
    PreSaveHandle = FEditorDelegates::PreSaveWorld.AddLambda([this](uint32, UWorld*) { OriginalPresentation(false); });
    PostSaveHandle = FEditorDelegates::PostSaveWorld.AddLambda(
        [this](uint32, UWorld*, bool) { OriginalPresentation(true); });
    if (GEditor)
        Transactions = Cast<UTransBuffer>(GEditor->Trans);
    if (UTransBuffer* Buffer = Transactions.Get())
    {
        BeforeUndoHandle = Buffer->OnBeforeRedoUndo().AddLambda(
            [this](const FTransactionContext&) { OriginalPresentation(false); });
        UndoHandle = Buffer->OnUndo().AddLambda(
            [this](const FTransactionContext&, bool) { OriginalPresentation(true); });
        RedoHandle = Buffer->OnRedo().AddLambda(
            [this](const FTransactionContext&, bool) { OriginalPresentation(true); });
    }
    if (GEditor)
        MovedHandle = GEditor->OnActorMoved().AddRaw(this, &FP4GameMirror::OnActorMoved);
    if (GEngine)
    {
        AddedHandle = GEngine->OnLevelActorAdded().AddRaw(this, &FP4GameMirror::OnActorAdded);
        DeletedHandle = GEngine->OnLevelActorDeleted().AddRaw(this, &FP4GameMirror::OnActorDeleted);
    }
}

FP4GameMirror::~FP4GameMirror()
{
    USelection::SelectionChangedEvent.Remove(SelectionHandle);
    FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PropertyHandle);
    FEditorDelegates::PreSaveWorld.Remove(PreSaveHandle);
    FEditorDelegates::PostSaveWorld.Remove(PostSaveHandle);
    if (UTransBuffer* Buffer = Transactions.Get())
    {
        Buffer->OnBeforeRedoUndo().Remove(BeforeUndoHandle);
        Buffer->OnUndo().Remove(UndoHandle);
        Buffer->OnRedo().Remove(RedoHandle);
    }
    if (GEditor)
        GEditor->OnActorMoved().Remove(MovedHandle);
    if (GEngine)
    {
        GEngine->OnLevelActorAdded().Remove(AddedHandle);
        GEngine->OnLevelActorDeleted().Remove(DeletedHandle);
    }
    Clear();
}

void FP4GameMirror::Forget(FEntry& Entry)
{
    if (AActor* Copy = Entry.Actor.Get())
    {
        TGuardValue<bool> Applying(bApplying, true);
        FKeepClean Keep(Copy->GetWorld());
        Copy->Destroy();
    }
    Entry.Actor.Reset();
    if (AActor* Anchor = Entry.Anchor.Get())
    {
        TGuardValue<bool> Applying(bApplying, true);
        FKeepClean Keep(Anchor->GetWorld());
        Anchor->Destroy();
    }
    Entry.Anchor.Reset();
}

void FP4GameMirror::Clear()
{
    Lifetime = MakeShared<uint8>(0);
    bWaiting = bClassesWaiting = false;
    bBindCooked = bCookedWaiting = false;
    CookedBindCursor = CookedBindPasses = CookedQueryCursor = 0;
    TGuardValue<bool> Applying(bApplying, true);
    UWorld* LevelWorld = World.Get();
    FKeepClean Keep(LevelWorld);
    if (LevelWorld && LiveLevel.IsValid() && LevelWorld->GetCurrentLevel() == LiveLevel.Get())
        LevelWorld->SetCurrentLevel(PreviousLevel.IsValid() ? PreviousLevel.Get() : LevelWorld->PersistentLevel);
    for (auto& Item : Entries)
        Forget(Item.Value);
    Entries.Reset();
    if (ULevel* Level = LiveLevel.Get())
    {
        // Includes actors dropped during preview that aren't supported by the game.
        const TArray<AActor*> Actors = Level->Actors;
        for (AActor* Actor : Actors)
            if (Actor)
                Actor->Destroy();
        Level->ClearLevelComponents();
        if (LevelWorld)
            LevelWorld->RemoveLevel(Level);
        Level->OwningWorld = nullptr;
    }
    for (const FOriginal& Original : Originals)
        if (AActor* Actor = Original.Actor.Get())
        {
            SetEditable(Actor, Original.bEditable);
            Actor->SetIsTemporarilyHiddenInEditor(Original.bHidden);
        }
    Originals.Reset();
    LiveLevel.Reset();
    PreviousLevel.Reset();
    World.Reset();
    MissingClasses.Reset();
}

void FP4GameMirror::OriginalPresentation(bool bPreview)
{
    for (const FOriginal& Saved : Originals)
        if (AActor* Actor = Saved.Actor.Get())
        {
            SetEditable(Actor, bPreview ? false : Saved.bEditable);
            Actor->SetIsTemporarilyHiddenInEditor(Saved.bHidden);
        }
    if (bPreview)
        for (const auto& Item : Entries)
            if (AActor* Actor = Item.Value.LevelActor.Get())
                Actor->SetIsTemporarilyHiddenInEditor(true);
}

void FP4GameMirror::Attach(UWorld* InWorld)
{
    if (World.Get() == InWorld && LiveLevel.IsValid())
        return;
    Clear();
    if (!InWorld)
        return;
    TGuardValue<bool> Applying(bApplying, true);
    FKeepClean Keep(InWorld);
    World = InWorld;
    PreviousLevel = InWorld->GetCurrentLevel();
    for (TActorIterator<AActor> It(InWorld); It; ++It)
        if (!It->HasAnyFlags(RF_Transient))
        {
            Originals.Add({*It, It->IsEditable(), It->IsTemporarilyHiddenInEditor()});
            SetEditable(*It, false);
        }
    // ULevel requires a world outer. World::Levels is transient, so this extra level is never saved.
    ULevel* Level = NewObject<ULevel>(InWorld, NAME_None, RF_Transient);
    Level->Initialize(FURL(nullptr));
    Level->OwningWorld = InWorld;
    Level->bIsVisible = true;
    Level->Model = NewObject<UModel>(Level, NAME_None, RF_Transient);
    Level->Model->Initialize(nullptr, 1);
    InWorld->AddLevel(Level);
    LiveLevel = Level;
    FActorSpawnParameters Params;
    Params.OverrideLevel = Level;
    Params.ObjectFlags = RF_Transient;
    AWorldSettings* Settings = InWorld->SpawnActor<AWorldSettings>(AWorldSettings::StaticClass(), Params);
    Level->SetWorldSettings(Settings);
    ForEachObjectWithOuter(Level, [](UObject* Part) { Part->SetFlags(RF_Transient); }, true);
    InWorld->SetCurrentLevel(Level);
    // Actors kept from an earlier preview are ordinary authored actors now. Recreate their
    // live counterparts on the next Play, without duplicating the imported game's actors.
    const TSet<FName> Imported = P4ImportedLevelActors(InWorld);
    if (!Owner.IsEmpty() && Imported.Num())
        for (const FOriginal& Saved : Originals)
            if (AActor* Original = Saved.Actor.Get())
            {
                if (Original->GetLevel() != PreviousLevel.Get() || Imported.Contains(Original->GetFName()))
                    continue;
                const bool CreatorBlueprint = Original->GetClass()->ClassGeneratedBy &&
                    P4IsContentPackageIgnoringCase(Original->GetClass()->GetOutermost()->GetName());
                if (!Original->IsA<AStaticMeshActor>() && !GameClass(Original->GetClass()) && !CreatorBlueprint)
                    continue;
                const FString Key = FString::Printf(TEXT("editor:%u"), Original->GetUniqueID());
                FEntry& Entry = Entries.Add(Key);
                Entry.Kind = Original->GetActorLabel();
                Entry.ClassPath = Original->GetClass()->GetPathName();
                Entry.LevelActor = Original;
                Entry.At = Entry.Shown = Original->GetActorLocation();
                Entry.Turn = Original->GetActorRotation();
                Entry.EditorFolder = Original->GetFolderPath();
                Entry.bAsked = Entry.bUserPlaced = Entry.bPendingSpawn = true;
                Entry.bOrdinary = !GameClass(Original->GetClass());
                Spawn(Entry);
                Original->SetIsTemporarilyHiddenInEditor(true);
            }
    NextQuery = 0;
    bBindCooked = !Owner.IsEmpty() && P4IsGamePreviewScratchWorld(InWorld);
    NextCookedBind = 0;
}

bool FP4GameMirror::BindCookedActor(AActor* Original, const FString& Handle, const FVector& At, const FRotator& Turn)
{
    if (!World.IsValid() || !LiveLevel.IsValid() || !Original || !Original->IsA<AStaticMeshActor>() ||
        Original->GetWorld() != World.Get() || Original->GetLevel() != World->PersistentLevel ||
        !Handle.StartsWith(TEXT("@actor:")))
        return false;
    for (const auto& Item : Entries)
    {
        if (Item.Value.LevelActor.Get() == Original)
            return Item.Key == Handle;
        if (Item.Key == Handle)
            return false;
    }
    TGuardValue<bool> Applying(bApplying, true);
    FKeepClean Keep(World.Get());
    FEntry& Entry = Entries.Add(Handle);
    Entry.Handle = Handle;
    Entry.Kind = Original->GetActorLabel();
    Entry.ClassPath = Original->GetClass()->GetPathName();
    Entry.LevelActor = Original;
    Entry.At = Entry.Shown = At;
    Entry.Turn = Turn;
    Entry.EditorFolder = Original->GetFolderPath();
    Entry.bAsked = Entry.bOrdinary = Entry.bCooked = true;
    Spawn(Entry);
    if (!Entry.Actor.IsValid())
    {
        Entries.Remove(Handle);
        return false;
    }
    Original->SetIsTemporarilyHiddenInEditor(true);
    return true;
}

void FP4GameMirror::BindCookedActors()
{
    // Only source actors from the scratch persistent level can correspond to its
    // cooked GameArea. Batches and retries are bounded, including failed matches.
    const int32 Count = FMath::Min(Originals.Num(), 4096);
    if (!Count || CookedBindPasses >= 3)
        return;
    TMap<FString, TWeakObjectPtr<AActor>> Requested;
    FString Names;
    for (int32 Visited = 0; Visited < Count && Requested.Num() < 40 && CookedBindPasses < 3; ++Visited)
    {
        AActor* Original = Originals[CookedBindCursor++].Actor.Get();
        if (CookedBindCursor == Count)
        {
            CookedBindCursor = 0;
            ++CookedBindPasses;
        }
        const AStaticMeshActor* Static = Cast<AStaticMeshActor>(Original);
        if (!Static || Static->GetLevel() != World->PersistentLevel ||
            !Static->GetStaticMeshComponent()->GetStaticMesh())
            continue;
        bool bBound = false;
        for (const auto& Item : Entries)
            if (Item.Value.LevelActor.Get() == Original)
            {
                bBound = true;
                break;
            }
        if (bBound)
            continue;
        const FString Name = Original->GetName();
        Requested.Add(Name, Original);
        Names += TEXT("[") + LuaText(Name) + TEXT("]=") + LuaText(Original->GetClass()->GetPathName()) + TEXT(",");
    }
    if (!Requested.Num())
        return;
    bCookedWaiting = true;
    const FString Source = TEXT("local wanted={") + Names + TEXT("}; local found,counts={},{}; ") +
        TEXT(
            "local geometry=(game.world() or ''):gsub('_P$','_GameArea'); local level='.'..geometry..':PersistentLevel.'; ") +
        TEXT("local class=reflection.find_class('StaticMeshActor'); if not class then return {} end ") +
        TEXT(
            "for i,a in ipairs(actor.list(class) or {}) do if i>4096 then break end local path=reflection.path(a) or ''; ") +
        TEXT("local name=path:match('%.([^%.:]+)$'); local source=name and wanted[name]; ") +
        TEXT(
            "if source and path:sub(-#(level..name))==level..name then local c=reflection.class_of(a); local cp=c and reflection.path(c); ") +
        TEXT(
            "if cp==(kadan_preview.paths[source] or source) then counts[name]=(counts[name] or 0)+1; local p=actor.transform(a); ") +
        TEXT(
            "if p then local l,r=p.location,p.rotation; found[name]={name,a,l.x,l.y,l.z,r.pitch,r.yaw,r.roll} end end end end ") +
        TEXT(
            "local out={}; for name,row in pairs(found) do if counts[name]==1 then out[#out+1]=row end end return out");
    Link.Eval(Source,
        [this, Requested, Guard = TWeakPtr<uint8>(Lifetime)](const FP4GameReply& Reply)
        {
            if (!Guard.IsValid())
                return;
            bCookedWaiting = false;
            const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
            if (!Reply.bOk || !Reply.Value || !Reply.Value->TryGetArray(Rows))
                return;
            for (const auto& Row : *Rows)
            {
                const TArray<TSharedPtr<FJsonValue>>* Cells = nullptr;
                if (!Row->TryGetArray(Cells) || Cells->Num() != 8)
                    continue;
                const auto& C = *Cells;
                const TWeakObjectPtr<AActor>* Original = Requested.Find(C[0]->AsString());
                if (Original && Original->IsValid())
                    BindCookedActor(Original->Get(), C[1]->AsString(),
                        FVector(C[2]->AsNumber(), C[3]->AsNumber(), C[4]->AsNumber()),
                        FRotator(C[5]->AsNumber(), C[6]->AsNumber(), C[7]->AsNumber()));
            }
        });
}

bool FP4GameMirror::CanKeepSimulationChanges() const
{
    if (!GEditor)
        return false;
    for (const auto& Item : Entries)
    {
        AActor* Copy = Item.Value.Actor.Get();
        AActor* Original = Item.Value.LevelActor.Get();
        if (Copy && Copy->IsSelected() && Item.Value.bUserPlaced && !Original && PreviousLevel.IsValid())
            return true;
        if (Copy && Copy->IsSelected() && Original && Original->GetClass()->IsChildOf(Copy->GetClass()) &&
            EditorUtilities::CopyActorProperties(Copy, Original, KeepOptions(true)) > 0)
            return true;
    }
    return false;
}

int32 FP4GameMirror::KeepSimulationChanges()
{
    if (!CanKeepSimulationChanges())
        return 0;
    TGuardValue<bool> Applying(bApplying, true);
    OriginalPresentation(false);
    ON_SCOPE_EXIT
    {
        OriginalPresentation(true);
    };
    const FScopedTransaction Transaction(
        NSLOCTEXT("LevelEditorCommands", "KeepSimulationChanges", "Keep Simulation Changes"));
    const auto Options = KeepOptions();
    int32 Count = 0;
    for (auto& Item : Entries)
    {
        AActor* Copy = Item.Value.Actor.Get();
        AActor* Original = Item.Value.LevelActor.Get();
        if (Copy && Copy->IsSelected() && Item.Value.bUserPlaced && !Original && PreviousLevel.IsValid())
        {
            FActorSpawnParameters Params;
            Params.OverrideLevel = PreviousLevel.Get();
            Params.ObjectFlags = RF_Transactional;
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Original = World->SpawnActor<AActor>(Copy->GetClass(), Copy->GetActorTransform(), Params);
            if (!Original)
            {
                Toast(TEXT("The editor could not keep that new actor."));
                continue;
            }
            Item.Value.LevelActor = Original;
            Originals.Add({Original, Original->IsEditable(), Original->IsTemporarilyHiddenInEditor()});
            Original->SetActorLabel(Copy->GetActorLabel());
            Original->SetFolderPath(Item.Value.EditorFolder);
            EditorUtilities::CopyActorProperties(Copy, Original, Options);
            Original->SetActorTransform(Copy->GetActorTransform());
            Original->MarkPackageDirty();
            ++Count;
            continue;
        }
        if (Copy && Copy->IsSelected() && Original && Original->GetClass()->IsChildOf(Copy->GetClass()))
        {
            Count += EditorUtilities::CopyActorProperties(Copy, Original, Options);
        }
    }
    return Count;
}

void FP4GameMirror::Tick(double Now)
{
    UWorld* Current = EditorWorld();
    if (!World.IsValid())
        Attach(Current);
    if (!World.IsValid())
        return;
    if (bBindCooked && !bCookedWaiting && CookedBindPasses < 3 && Now >= NextCookedBind)
    {
        NextCookedBind = Now + 0.5;
        BindCookedActors();
    }
    for (auto& Item : Entries)
        if (Item.Value.bPendingSpawn)
            SpawnPlaced(Item.Key, Item.Value);
    if (!bWaiting && Now >= NextQuery)
    {
        NextQuery = Now + QueryEvery;
        Query();
    }
    // Ease the copies toward the game's places between answers, except one the user just moved.
    TGuardValue<bool> Applying(bApplying, true);
    FKeepClean Keep(World.Get());
    for (auto& Item : Entries)
    {
        FEntry& Entry = Item.Value;
        AActor* Actor = Entry.Actor.Get();
        if (!Actor || Entry.Handle.IsEmpty() || Now - Entry.MovedByUser < 1.5)
            continue;
        Entry.Shown = FMath::Lerp(Entry.Shown, Entry.At, 0.5f);
        Actor->SetActorLocationAndRotation(Entry.Shown, Entry.Turn);
    }
}

void FP4GameMirror::Query()
{
    bWaiting = true;
    FString Source(::Query);
    if (!Owner.IsEmpty())
        Source = Source.Replace(TEXT("local rows = {} "),
            *(TEXT("local rows = kadan_preview.ordinary_rows(") + LuaText(Owner) + TEXT(") ")));
    TArray<FString> CookedHandles;
    for (const auto& Item : Entries)
        if (Item.Value.bCooked)
            CookedHandles.Add(Item.Key);
    FString Cooked;
    TSet<FString> AskedCooked;
    if (CookedHandles.Num())
    {
        CookedQueryCursor %= CookedHandles.Num();
        for (int32 Count = 0; Count < FMath::Min(40, CookedHandles.Num()); ++Count)
        {
            const FString& Handle = CookedHandles[CookedQueryCursor];
            Cooked += LuaText(Handle) + TEXT(",");
            AskedCooked.Add(Handle);
            CookedQueryCursor = (CookedQueryCursor + 1) % CookedHandles.Num();
        }
    }
    if (!Cooked.IsEmpty())
        Source += TEXT("for _,token in ipairs({") + Cooked +
            TEXT("}) do local a=handle(token); if a and actor.alive(a) then local p=actor.transform(a); ") +
            TEXT(
                "if p then local l,r=p.location,p.rotation; rows[#rows+1]={a,'MESH','0',l.x,l.y,l.z,r.pitch,r.yaw,r.roll} end end end ");
    Source += TEXT("return rows");
    Link.Eval(Source,
        [this, AskedCooked, Guard = TWeakPtr<uint8>(Lifetime)](const FP4GameReply& Reply)
        {
            if (!Guard.IsValid())
                return;
            TGuardValue<bool> Applying(bApplying, true);
            FKeepClean Keep(World.Get());
            bWaiting = false;
            const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
            if (!Reply.bOk || !Reply.Value || !Reply.Value->TryGetArray(Rows))
                return;
            TSet<FString> Seen;
            for (const auto& Row : *Rows)
            {
                const TArray<TSharedPtr<FJsonValue>>* Cells = nullptr;
                if (!Row->TryGetArray(Cells) || Cells->Num() < 9)
                    continue;
                const auto& C = *Cells;
                const FString Handle = C[0]->AsString();
                Seen.Add(Handle);
                // The census can arrive before the spawn reply. Match its client token to the
                // actor already dragged in UE rather than creating a second editor copy.
                if (C.Num() > 9)
                {
                    FString Token;
                    if (C[9]->TryGetString(Token) && !Token.IsEmpty() && Token != Handle)
                    {
                        FEntry Before;
                        if (Entries.RemoveAndCopyValue(Token, Before))
                        {
                            if (!Before.Actor.IsValid())
                            {
                                Link.Eval(TEXT("return kadan_preview.ordinary_remove(") + LuaText(Owner) +
                                        TEXT(",handle(") + LuaText(Handle) + TEXT("))"),
                                    [](const FP4GameReply&) {});
                                Entries.Remove(Handle);
                                continue;
                            }
                            Entries.Add(Handle, MoveTemp(Before));
                        }
                    }
                }
                FEntry& Entry = Entries.FindOrAdd(Handle);
                if (Entry.Handle.IsEmpty())
                {
                    Entry.Handle = Handle;
                    Entry.Shown = FVector(C[3]->AsNumber(), C[4]->AsNumber(), C[5]->AsNumber());
                }
                Entry.Kind = C[1]->AsString();
                FP4GameLink::ReadPlacement(C[2], Entry.Placement);
                Entry.At = FVector(C[3]->AsNumber(), C[4]->AsNumber(), C[5]->AsNumber());
                Entry.Turn = FRotator(C[6]->AsNumber(), C[7]->AsNumber(), C[8]->AsNumber());
                Place(Entry);
            }
            for (auto It = Entries.CreateIterator(); It; ++It)
            {
                if (Seen.Contains(It.Key()) || (It.Value().bUserPlaced && It.Value().Actor.IsValid()) ||
                    (It.Value().bCooked && !AskedCooked.Contains(It.Key())))
                    continue;
                Forget(It.Value());
                It.RemoveCurrent();
            }
            AskClasses();
        });
}

// The class path of every new actor, a few dozen per round trip.
void FP4GameMirror::AskClasses()
{
    if (bClassesWaiting)
        return;
    FString List;
    TArray<FString> Requested;
    int32 Count = 0;
    for (auto& Item : Entries)
    {
        if (Item.Value.bAsked || Count >= 40)
            continue;
        Item.Value.bAsked = true;
        List += LuaText(Item.Key) + TEXT(",");
        Requested.Add(Item.Key);
        ++Count;
    }
    if (!Count)
        return;
    bClassesWaiting = true;
    Link.Eval(
        FString::Printf(
            TEXT(
                "local out = {} "
                "for _, t in ipairs({%s}) do local a = handle(t) local c = a and reflection.class_of(a) out[#out + 1] = {t, c and reflection.path(c) or ''} end "
                "return out"),
            *List),
        [this, Requested, Guard = TWeakPtr<uint8>(Lifetime)](const FP4GameReply& Reply)
        {
            if (!Guard.IsValid())
                return;
            TGuardValue<bool> Applying(bApplying, true);
            FKeepClean Keep(World.Get());
            bClassesWaiting = false;
            const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
            if (!Reply.bOk || !Reply.Value || !Reply.Value->TryGetArray(Rows))
            {
                for (const FString& Handle : Requested)
                    if (FEntry* Entry = Entries.Find(Handle))
                        Entry->bAsked = false;
                return;
            }
            for (const auto& Row : *Rows)
            {
                const TArray<TSharedPtr<FJsonValue>>* Cells = nullptr;
                if (!Row->TryGetArray(Cells) || Cells->Num() < 2)
                    continue;
                if (FEntry* Entry = Entries.Find((*Cells)[0]->AsString()))
                {
                    Entry->ClassPath = (*Cells)[1]->AsString();
                    Place(*Entry);
                }
            }
        });
}

void FP4GameMirror::Place(FEntry& Entry)
{
    UWorld* Level = World.Get();
    if (!Level)
        return;
    if (Entry.Placement && !Entry.LevelActor.IsValid())
    {
        for (TActorIterator<AActor> It(Level); It; ++It)
            if (!It->HasAnyFlags(RF_Transient) && P4GamePlacementOf(*It) == Entry.Placement)
            {
                Entry.LevelActor = *It;
                break;
            }
    }
    if (AActor* Own = Entry.LevelActor.Get())
        Own->SetIsTemporarilyHiddenInEditor(true);
    if (!Entry.Actor.IsValid() && !Entry.ClassPath.IsEmpty())
        Spawn(Entry);
}

void FP4GameMirror::Spawn(FEntry& Entry)
{
    UWorld* Level = World.Get();
    if (!Level)
        return;
    UClass* Class = FindObject<UClass>(nullptr, *Entry.ClassPath);
    if (!Class && !MissingClasses.Contains(Entry.ClassPath))
        Class = StaticLoadClass(AActor::StaticClass(), nullptr, *Entry.ClassPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
    const bool bKnown = Class && Class->IsChildOf(AActor::StaticClass()) && !Class->HasAnyClassFlags(CLASS_Abstract);
    if (!bKnown)
        MissingClasses.Add(Entry.ClassPath);

    TGuardValue<bool> Applying(bApplying, true);
    FKeepClean Keep(Level);
    FActorSpawnParameters Params;
    Params.ObjectFlags = RF_Transient;
    Params.OverrideLevel = LiveLevel.Get();
    if (AActor* Original = Entry.LevelActor.Get())
        if (bKnown && Original->GetClass() == Class)
            Params.Template = Original;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Params.bNoFail = true;
    AActor* Actor = Level->SpawnActor<AActor>(bKnown ? Class : ANote::StaticClass(), Entry.At, Entry.Turn, Params);
    if (Actor)
    {
        // Outliner folders aren't stored in the map (UE 4.26 rebuilds them from the actors), and these actors aren't saved.
        Actor->SetActorLabel(bKnown ? Entry.Kind : Entry.Kind + TEXT(" (class not in the project)"));
        Actor->SetFolderPath(TEXT("Game (live)"));
        SetEditable(Actor, true);
        Actor->SetIsTemporarilyHiddenInEditor(false);
        if (const AActor* Original = Entry.LevelActor.Get())
            Actor->SetActorScale3D(Original->GetActorScale3D());
        if (AActor* Original = Entry.LevelActor.Get())
            if (USceneComponent* Parent = Original->GetRootComponent() ? Original->GetRootComponent()->GetAttachParent()
                                                                       : nullptr)
            {
                // Actor::Modify also modifies its attachment parent. Use a transient anchor,
                // never the original component. Keep the same parent space so UE4's native
                // property copier transfers relative transforms correctly when changes are kept.
                FActorSpawnParameters AnchorParams;
                AnchorParams.OverrideLevel = LiveLevel.Get();
                AnchorParams.ObjectFlags = RF_Transient;
                AActor* Anchor = Level->SpawnActor<AActor>(AActor::StaticClass(), AnchorParams);
                USceneComponent* Root = NewObject<USceneComponent>(Anchor, NAME_None, RF_Transient);
                Anchor->SetRootComponent(Root);
                Anchor->AddInstanceComponent(Root);
                Root->SetMobility(Parent->Mobility);
                Root->RegisterComponent();
                Root->SetWorldTransform(Parent->GetComponentTransform());
                SetEditable(Anchor, false);
                if (FBoolProperty* Listed = FindFProperty<FBoolProperty>(
                        AActor::StaticClass(), TEXT("bListedInSceneOutliner")))
                    Listed->SetPropertyValue_InContainer(Anchor, false);
                Actor->AttachToComponent(Root, FAttachmentTransformRules::KeepWorldTransform);
                Entry.Anchor = Anchor;
            }
        // UE4 composes a template root's relative transform with the spawn
        // transform. A placed source already carries its location and rotation;
        // restore the requested world pose before a reseed can send it to game.
        Actor->SetActorLocationAndRotation(Entry.At, Entry.Turn);
        if (ANote* Note = Cast<ANote>(Actor))
            Note->Text = Entry.ClassPath;
        // Actor::Modify also modifies its root and attachment component. RF_Transient on the actor
        // alone does not protect the map when the viewport or Details panel modifies those objects.
        // Set this after SetActorLabel, whose PostEditChange can rebuild Blueprint components.
        ForEachObjectWithOuter(Actor, &LiveObject, true);
        Entry.Actor = Actor;
        Entry.Shown = Entry.At;
    }
}

FP4GameMirror::FEntry* FP4GameMirror::EntryOf(const AActor* Actor)
{
    if (!Actor)
        return nullptr;
    for (auto& Item : Entries)
        if (Item.Value.Actor.Get() == Actor)
            return &Item.Value;
    return nullptr;
}

FString FP4GameMirror::HandleOf(const AActor* Actor) const
{
    for (const auto& Item : Entries)
        if (Item.Value.Actor.Get() == Actor || Item.Value.LevelActor.Get() == Actor)
            return Item.Value.Handle;
    return FString();
}

void FP4GameMirror::OnSelection(UObject*)
{
    if (!GEditor || GEditor->GetSelectedActorCount() != 1)
        return;
    AActor* Actor = GEditor->GetSelectedActors()->GetTop<AActor>();
    const FEntry* Entry = EntryOf(Actor);
    // Only a live copy takes the game's values; the level's own actors keep what the level says.
    if (Entry && !Entry->Handle.IsEmpty() && Entry->Actor.Get() == Actor)
        PullFields(Actor, Entry->Handle);
}

void FP4GameMirror::PushFields(AActor* Actor, const FString& Handle)
{
    const AActor* Defaults = Actor->GetClass()->GetDefaultObject<AActor>();
    for (TFieldIterator<FProperty> It(Actor->GetClass()); It; ++It)
    {
        if (!Mirrored(*It) || It->Identical_InContainer(Actor, Defaults))
            continue;
        const TSharedPtr<FJsonValue> Value = FJsonObjectConverter::UPropertyToJsonValue(
            *It, It->ContainerPtrToValuePtr<void>(Actor), 0, 0);
        if (!Value)
            continue;
        const FString Name = It->GetName();
        Link.Call(
            TEXT("reflection.set"), {FP4GameLink::Text(Handle), FP4GameLink::Text(Name), Value},
            [Name](const FP4GameReply& Reply)
            {
                if (!Reply.bOk)
                    Toast(FString::Printf(TEXT("The game didn't take %s (%s)."), *Name, *Reply.Error));
            },
            true);
    }
}

void FP4GameMirror::PullFields(AActor* Actor, const FString& Handle)
{
    TArray<FProperty*> Fields;
    for (TFieldIterator<FProperty> It(Actor->GetClass()); It && Fields.Num() < 200; ++It)
        if (Mirrored(*It))
            Fields.Add(*It);
    if (!Fields.Num())
        return;
    FString List;
    for (const FProperty* Field : Fields)
        List += LuaText(Field->GetName()) + TEXT(",");
    const TWeakObjectPtr<AActor> Weak = Actor;
    Link.Eval(
        FString::Printf(
            TEXT(
                "local a = handle(%s) if not a then return nil end local out = {} "
                "for i, n in ipairs({%s}) do local v, why = reflection.get(a, n) if v == nil and why then v = reflection.get_text(a, n) end out[i] = {n, v} end "
                "return out"),
            *LuaText(Handle), *List),
        [this, Weak, Guard = TWeakPtr<uint8>(Lifetime)](const FP4GameReply& Reply)
        {
            if (!Guard.IsValid())
                return;
            AActor* Target = Weak.Get();
            const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
            if (!Target || !Reply.bOk || !Reply.Value || !Reply.Value->TryGetArray(Rows))
                return;
            TGuardValue<bool> Applying(bApplying, true);
            FKeepClean Keep(Target->GetWorld());
            for (const auto& Row : *Rows)
            {
                const TArray<TSharedPtr<FJsonValue>>* Cells = nullptr;
                if (!Row->TryGetArray(Cells) || Cells->Num() < 2 || (*Cells)[1]->IsNull())
                    continue;
                FProperty* Field = Target->GetClass()->FindPropertyByName(*(*Cells)[0]->AsString());
                if (Field)
                    FJsonObjectConverter::JsonValueToUProperty(
                        (*Cells)[1], Field, Field->ContainerPtrToValuePtr<void>(Target), 0, 0);
            }
        });
}

// A field changed in the Details panel, on a live actor or a level actor the game placed.
void FP4GameMirror::OnPropertyChanged(UObject* Object, FPropertyChangedEvent& Event)
{
    if (bApplying)
        return;
    AActor* Actor = Cast<AActor>(Object);
    if (!Actor && Object)
        Actor = Object->GetTypedOuter<AActor>();
    const FEntry* Entry = EntryOf(Actor);
    if (Entry)
        ForEachObjectWithOuter(Actor, &LiveObject, true);
    if (Object != Actor)
        return;
    FProperty* Field = Event.MemberProperty ? Event.MemberProperty : Event.Property;
    if (!Entry || Entry->Handle.IsEmpty() || !Field || !Mirrored(Field))
        return;
    const TSharedPtr<FJsonValue> Value = FJsonObjectConverter::UPropertyToJsonValue(
        Field, Field->ContainerPtrToValuePtr<void>(Actor), 0, 0);
    if (!Value)
        return;
    const FString Name = Field->GetName();
    Link.Call(
        TEXT("reflection.set"), {FP4GameLink::Text(Entry->Handle), FP4GameLink::Text(Name), Value},
        [Name](const FP4GameReply& Reply)
        {
            if (!Reply.bOk)
                Toast(FString::Printf(TEXT("The game didn't take %s (%s)."), *Name, *Reply.Error));
        },
        true);
}

// Moved in the viewport. A live actor is warped the way the game warps one (a captain on the dog moves with
// the dog); a level actor the game placed is put exactly where the level now has it.
void FP4GameMirror::OnActorMoved(AActor* Actor)
{
    if (bApplying)
        return;
    FEntry* Entry = EntryOf(Actor);
    if (!Entry)
        return;
    const FVector At = Actor->GetActorLocation();
    const FRotator Turn = Actor->GetActorRotation();
    if (Entry->Handle.IsEmpty())
    {
        Entry->MovedByUser = FPlatformTime::Seconds();
        Entry->At = Entry->Shown = At;
        Entry->Turn = Turn;
        return;
    }
    FString Source;
    if (Entry->bCooked)
    {
        Entry->MovedByUser = FPlatformTime::Seconds();
        Entry->At = Entry->Shown = At;
        Entry->Turn = Turn;
        Source = TEXT("local a=handle(") + LuaText(Entry->Handle) + TEXT("); local p=") + LuaPose(Actor) +
            TEXT(
                "; local c=reflection.get(a,'StaticMeshComponent'); if c then local _,why=reflection.call(c,'SetMobility',{NewMobility=2}); ") +
            TEXT(
                "if type(why)=='string' then error(why) end end local ok,why=actor.set_transform(a,{location=p.location,rotation=p.rotation}); ") +
            TEXT(
                "if not ok then error(why) end local _,err=reflection.call(a,'SetActorScale3D',{['NewScale3D.X']=p.scale.x,['NewScale3D.Y']=p.scale.y,['NewScale3D.Z']=p.scale.z}); ") +
            TEXT("if type(err)=='string' then error(err) end return true");
    }
    else if (Entry->bOrdinary)
    {
        Entry->MovedByUser = FPlatformTime::Seconds();
        Entry->At = Entry->Shown = At;
        Entry->Turn = Turn;
        Source = TEXT("return kadan_preview.ordinary_move(") + LuaText(Owner) + TEXT(",handle(") +
            LuaText(Entry->Handle) + TEXT("),") + LuaPose(Actor) + TEXT(")");
    }
    else if (Entry->Actor.Get() == Actor)
    {
        Entry->MovedByUser = FPlatformTime::Seconds();
        Entry->At = Entry->Shown = At;
        Entry->Turn = Turn;
        Source = FString::Printf(TEXT("local a = handle(%s) local dog = captain.riding(a) "
                                      "local done, why = entity.warp(dog or a, %s) "
                                      "if not done then done, why = entity.set_pose(a, {location = %s}) end "
                                      "if not done then error(why) end return true"),
            *LuaText(Entry->Handle), *LuaPlace(At, Turn),
            *LuaPlace(At, Turn).Replace(TEXT("}, {"), TEXT("}, rotation = {")));
    }
    else
    {
        Source = FString::Printf(
            TEXT(
                "local done, why = entity.set_pose(handle(%s), {location = %s}) if not done then error(why) end return true"),
            *LuaText(Entry->Handle), *LuaPlace(At, Turn).Replace(TEXT("}, {"), TEXT("}, rotation = {")));
    }
    Link.Eval(Source,
        [](const FP4GameReply& Reply)
        {
            if (!Reply.bOk)
            {
                FString Detail;
                if (Reply.Whole)
                    Reply.Whole->TryGetStringField(TEXT("detail"), Detail);
                UE_LOG(LogTemp, Warning, TEXT("Kadan move refused: %s %s"), *Reply.Error, *Detail);
                Toast(FString::Printf(TEXT("The game didn't move it (%s)."), *Reply.Error));
            }
        });
}

void FP4GameMirror::SetPlayContent(const TArray<FString>& Packages)
{
    PlayPackages = TSet<FString>(Packages);
    bHasPlayContent = true;
}

FString FP4GameMirror::NotInPlay(const AActor* Actor) const
{
    if (!bHasPlayContent || !IsValid(Actor))
        return FString();
    TArray<const UObject*> Used;
    const UClass* Class = Actor->GetClass();
    Used.Add(Class->ClassGeneratedBy ? static_cast<const UObject*>(Class->ClassGeneratedBy) : Class);
    if (const AStaticMeshActor* Static = Cast<AStaticMeshActor>(Actor))
        if (const UStaticMeshComponent* Component = Static->GetStaticMeshComponent())
        {
            Used.Add(Component->GetStaticMesh());
            for (int32 Slot = 0; Slot < Component->GetNumMaterials(); ++Slot)
                Used.Add(Component->GetMaterial(Slot));
        }
    for (const UObject* Object : Used)
    {
        if (!Object)
            continue;
        const FString Package = Object->GetOutermost()->GetName();
        if (!P4IsContentPackage(Package) || PlayPackages.Contains(Package) ||
            P4IsGamePreviewGameSource(const_cast<UObject*>(Object)))
            continue;
        return FPackageName::GetShortName(Package);
    }
    return FString();
}

// A game Blueprint placed in the level is spawned in the game too, and then stands for that game actor.
void FP4GameMirror::OnActorAdded(AActor* Actor)
{
    if (bApplying || !Actor || Actor->bIsEditorPreviewActor || Actor->GetLevel() != LiveLevel.Get() || GIsTransacting)
        return;
    Actor->SetFlags(RF_Transient);
    ForEachObjectWithOuter(Actor, &LiveObject, true);
    const FString EditorKey = FString::Printf(TEXT("editor:%u"), Actor->GetUniqueID());
    FEntry& Placed = Entries.FindOrAdd(EditorKey);
    Placed.Actor = Actor;
    Placed.ClassPath = Actor->GetClass()->GetPathName();
    Placed.bAsked = Placed.bUserPlaced = true;
    Placed.bPendingSpawn = true;
    Placed.bOrdinary = !GameClass(Actor->GetClass());
    Placed.EditorFolder = Actor->GetFolderPath() == FName(TEXT("Game (live)")) ? NAME_None : Actor->GetFolderPath();
}

void FP4GameMirror::SpawnPlaced(const FString& EditorKey, FEntry& Placed)
{
    AActor* Actor = Placed.Actor.Get();
    if (!Actor || Owner.IsEmpty())
        return;
    FString Source;
    if (Placed.bOrdinary)
    {
        FString Mesh, Materials = TEXT("{");
        if (const AStaticMeshActor* Static = Cast<AStaticMeshActor>(Actor))
        {
            const UStaticMeshComponent* Component = Static->GetStaticMeshComponent();
            // Actor factories assign their asset after spawning the actor. Wait until it is ready.
            if (!Component || !Component->GetStaticMesh())
                return;
            Mesh = TEXT(",mesh=") + LuaText(Component->GetStaticMesh()->GetPathName());
            for (int32 Slot = 0; Slot < Component->GetNumMaterials(); ++Slot)
                if (const UMaterialInterface* Material = Component->GetMaterial(Slot))
                    Materials += FString::Printf(TEXT("[%d]=%s,"), Slot + 1, *LuaText(Material->GetPathName()));
        }
        Materials += TEXT("}");
        Source = TEXT("return kadan_preview.ordinary_create(") + LuaText(Owner) + TEXT(",{class_name=") +
            LuaText(Actor->GetClass()->GetName()) + TEXT(",token=") + LuaText(EditorKey) + TEXT(",class_path=") +
            LuaText(Actor->GetClass()->GetPathName()) + Mesh + TEXT(",materials=") + Materials + TEXT(",pose=") +
            LuaPose(Actor) + TEXT("})");
    }
    else
    {
        const UClass* Class = Actor->GetClass();
        Source = TEXT("local path=") + LuaText(Class->GetPathName()) +
            TEXT(" local c=asset.load(kadan_preview.paths[path] or path) ") +
            TEXT(
                "if not c then error('The preview actor class could not be loaded.') end local made,why=entity.spawn(c,") +
            LuaPlace(Actor->GetActorLocation(), Actor->GetActorRotation()) +
            TEXT(") pcall(asset.release,c) if not made then error(why) end ") + LuaScale(Actor) +
            TEXT("return kadan_preview.ordinary_track(") + LuaText(Owner) + TEXT(",made,") + LuaText(EditorKey) +
            TEXT(")");
    }
    const FString Missing = NotInPlay(Actor);
    if (!Missing.IsEmpty())
    {
        Placed.bPendingSpawn = false;
        Toast(Missing + TEXT(" is not in the game yet. Press Play again to send it."));
        return;
    }
    // Actor factories finish placement after OnActorAdded. Initialize smoothing
    // from the completed drop, before a reply can make its handle live.
    Placed.At = Placed.Shown = Actor->GetActorLocation();
    Placed.Turn = Actor->GetActorRotation();
    const FTransform RequestedTransform = Actor->GetActorTransform();
    const double RequestedAt = FPlatformTime::Seconds();
    Placed.bPendingSpawn = false;
    const bool bOrdinary = Placed.bOrdinary;
    const TWeakObjectPtr<AActor> Weak = Actor;
    Link.Eval(Source,
        [this, Weak, EditorKey, bOrdinary, RequestedTransform, RequestedAt, Guard = TWeakPtr<uint8>(Lifetime)](
            const FP4GameReply& Reply)
        {
            if (!Guard.IsValid())
                return;
            FString Handle;
            if (!Reply.bOk || !Reply.Value || !Reply.Value->TryGetString(Handle))
            {
                // A lost UDP reply can already have been recovered by the next census.
                if (const FEntry* Recovered = EntryOf(Weak.Get()))
                    if (!Recovered->Handle.IsEmpty())
                        return;
                Toast(TEXT("The game couldn't spawn that actor."));
                return;
            }
            if (!Weak.IsValid())
            {
                Entries.Remove(EditorKey);
                Link.Eval(TEXT("return kadan_preview.ordinary_remove(") + LuaText(Owner) + TEXT(",handle(") +
                        LuaText(Handle) + TEXT("))"),
                    [](const FP4GameReply&) {});
                return;
            }
            FEntry& Entry = Entries.FindOrAdd(Handle);
            FEntry Before;
            if (Entries.RemoveAndCopyValue(EditorKey, Before))
                Entry = MoveTemp(Before);
            Entry.Handle = Handle;
            Entry.Actor = Weak;
            Entry.bAsked = true;
            if (AActor* Made = Weak.Get())
            {
                // Before anything can read the copy's values back: the spawn gave it the class's.
                if (!bOrdinary)
                    PushFields(Made, Handle);
                Entry.ClassPath = Made->GetClass()->GetPathName();
                Entry.Shown = Made->GetActorLocation();
                // A user can finish dragging while the game answers the initial
                // spawn. Send that latest placement instead of easing it backward.
                if (Entry.MovedByUser >= RequestedAt && !Made->GetActorTransform().Equals(RequestedTransform, 0.001f))
                    OnActorMoved(Made);
            }
        });
}

// Deleted in the editor: gone from the game too.
void FP4GameMirror::OnActorDeleted(AActor* Actor)
{
    if (bApplying || GIsTransacting)
        return;
    FEntry* Entry = EntryOf(Actor);
    if (!Entry)
        return;
    const FString Handle = Entry->Handle;
    if (Entry->LevelActor.Get() == Actor)
        Entry->LevelActor.Reset();
    if (Entry->Actor.Get() == Actor)
        Entry->Actor.Reset();
    if (Handle.IsEmpty())
        return;
    if (Entry->bCooked)
    {
        Link.Eval(TEXT("local a=handle(") + LuaText(Handle) +
                TEXT("); if a and actor.alive(a) then local _,why=reflection.call(a,'K2_DestroyActor',{}); ") +
                TEXT("if type(why)=='string' then error(why) end end return true"),
            [](const FP4GameReply&) {});
        return;
    }
    if (Entry->bOrdinary)
    {
        Link.Eval(TEXT("return kadan_preview.ordinary_remove(") + LuaText(Owner) + TEXT(",handle(") + LuaText(Handle) +
                TEXT("))"),
            [](const FP4GameReply&) {});
        return;
    }
    Link.Call(
        TEXT("entity.destroy"), {FP4GameLink::Text(Handle)},
        [](const FP4GameReply& Reply)
        {
            if (!Reply.bOk && Reply.Error != TEXT("not_found"))
                Toast(FString::Printf(TEXT("The game didn't remove it (%s)."), *Reply.Error));
        },
        true);
}

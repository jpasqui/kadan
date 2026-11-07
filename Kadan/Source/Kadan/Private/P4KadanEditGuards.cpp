// What Kadan lets a creator change in a game level and in a game table: what Tsuchi can apply. Other edits
// are put back when they are made, and the person is told which actor, property or row it was.
#include "P4KadanReadOnly.h"
#include "Components/SceneComponent.h"
#include "P4RecoveredPostProcessAuthoring.h"
#include "P4LevelContent.h"
#include "P4OriginalImportSave.h"
#include "P4OriginalRecord.h"
#include "Components/ActorComponent.h"
#include "DataTableEditorUtils.h"
#include "Editor.h"
#include "Engine/DataTable.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "UObject/EnumProperty.h"
#include "UObject/TextProperty.h"
#include "GameFramework/Actor.h"
#include "InstancedFoliageActor.h"
#include "Misc/PackageName.h"
#include "Serialization/ObjectReader.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace P4Kadan
{
namespace
{
bool Active = false, Restoring = false;
FDelegateHandle PreChanged, Changed, ModifiedHandle, MapOpened;

// The game's actors of each imported level, read once from what the level said when it was imported.
TMap<TWeakObjectPtr<UWorld>, TSet<FName>> GameActors;
// Rows of game tables imported before Kadan kept their names: the rows they had when first seen.
TMap<TWeakObjectPtr<const UDataTable>, TArray<FName>> SeenRows;

// What a property of a game actor said before a change, kept until the change is judged.
struct FBefore
{
    TWeakObjectPtr<UObject> Object;
    FProperty* Member = nullptr;
    TArray<FString> Values;
};
TArray<FBefore> Befores;
struct FRefusal
{
    FBefore Before;
    FString Message;
};
TArray<FRefusal> Refusals;
// The components a game actor had before something modified it.
struct FParts
{
    TWeakObjectPtr<AActor> Actor;
    TSet<FName> Components;
};
TArray<FParts> Parts;
// What a game table held before its rows were changed.
struct FTableBefore
{
    TWeakObjectPtr<UDataTable> Table;
    TArray<uint8> Bytes;
    TArray<FName> Rows;
};
TArray<FTableBefore> Tables;

FString Label(const UObject* Object)
{
    if (const AActor* Actor = Cast<AActor>(Object))
        return Actor->GetActorLabel();
    return Object->GetName();
}
FString PropertyName(const FProperty* Property)
{
#if WITH_EDITOR
    return Property->GetDisplayNameText().ToString();
#else
    return Property->GetName();
#endif
}

bool Ignoring()
{
    return !Active || Restoring || !IsInGameThread() || P4ImportPublication::IsImporting() || GIsTransacting;
}

// The object whose memory holds a property. The editor reports a move or a Transform edit of an actor
// as the actor's own change, with a property of its root component such as RelativeLocation.
UObject* HolderOf(UObject* Object, const FProperty* Property)
{
    const UClass* Owner = Property ? Property->GetOwnerClass() : nullptr;
    if (!Object || !Owner)
        return nullptr;
    if (Object->IsA(Owner))
        return Object;
    if (AActor* Actor = Cast<AActor>(Object))
        if (USceneComponent* Root = Actor->GetRootComponent())
            if (Root->IsA(Owner))
                return Root;
    return nullptr;
}

TArray<FString> ValuesOf(UObject* Object, const FProperty* Property)
{
    TArray<FString> Out;
    for (int32 Index = 0; Index < Property->ArrayDim; ++Index)
    {
        FString Text;
        Property->ExportText_InContainer(Index, Text, Object, Object, Object, PPF_None);
        Out.Add(MoveTemp(Text));
    }
    return Out;
}

void Remember(UObject* Object, FProperty* Member)
{
    Object = HolderOf(Object, Member);
    if (!Object)
        return;
    for (const FBefore& One : Befores)
        if (One.Object.Get() == Object && One.Member == Member)
            return;
    FBefore One;
    One.Object = Object;
    One.Member = Member;
    One.Values = ValuesOf(Object, Member);
    Befores.Add(MoveTemp(One));
}

// Whether Tsuchi can write a value of this kind. Kind says what it is when it cannot.
bool ValueKind(const FProperty* Property, FString& Kind)
{
    if (CastField<FBoolProperty>(Property) || CastField<FNumericProperty>(Property) ||
        CastField<FEnumProperty>(Property) || CastField<FNameProperty>(Property) || CastField<FStrProperty>(Property) ||
        CastField<FSoftObjectProperty>(Property))
        return true;
    if (CastField<FTextProperty>(Property))
    {
        Kind = TEXT("localized text");
        return false;
    }
    if (CastField<FArrayProperty>(Property) || CastField<FMapProperty>(Property) || CastField<FSetProperty>(Property))
    {
        Kind = TEXT("a list");
        return false;
    }
    if (Property->HasAnyPropertyFlags(CPF_InstancedReference | CPF_PersistentInstance | CPF_ContainsInstancedReference))
    {
        Kind = TEXT("an object inside the actor");
        return false;
    }
    if (CastField<FObjectProperty>(Property) && !CastField<FWeakObjectProperty>(Property) &&
        !CastField<FLazyObjectProperty>(Property))
        return true;
    if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
    {
        for (TFieldIterator<FProperty> It(Struct->Struct); It; ++It)
            if (!ValueKind(*It, Kind))
                return false;
        return true;
    }
    Kind = TEXT("a kind of value Tsuchi does not write");
    return false;
}

// A reference of a game actor's value goes to an asset by its path. Something of a level has no such path.
bool PointsIntoLevel(const FProperty* Property, const void* Value)
{
    if (const FObjectProperty* Object = CastField<FObjectProperty>(Property))
    {
        const UObject* Target = Object->GetObjectPropertyValue(Value);
        if (!Target)
            return false;
        if (Target->IsA<UClass>())
            return false;
        return !Target->IsAsset() && UWorld::FindWorldInPackage(Target->GetOutermost()) != nullptr;
    }
    if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
        for (TFieldIterator<FProperty> It(Struct->Struct); It; ++It)
            for (int32 Index = 0; Index < It->ArrayDim; ++Index)
                if (PointsIntoLevel(*It, It->ContainerPtrToValuePtr<void>(Value, Index)))
                    return true;
    return false;
}

bool Allowed(UObject* Object, AActor* Actor, const FProperty& Member, FString& Why)
{
    const FString Name = PropertyName(&Member), Who = Label(Actor);
    FString Unsupported;
    if (P4RefuseRecoveredPostProcessAuthoring(Object, &Member, nullptr, Unsupported))
    {
        Why = FString::Printf(TEXT("%s of %s because %s"), *Name, *Who, *Unsupported);
        return false;
    }
    if (Object != Actor && !Object->IsA<UActorComponent>())
    {
        Why = FString::Printf(
            TEXT(
                "%s of %s because it belongs to an object inside the actor, and Tsuchi cannot change those on a game actor."),
            *Name, *Who);
        return false;
    }
    FString Kind;
    if (!ValueKind(&Member, Kind))
    {
        Why = FString::Printf(
            TEXT("%s of %s because it is %s, and Tsuchi cannot change that on a game actor."), *Name, *Who, *Kind);
        return false;
    }
    for (int32 Index = 0; Index < Member.ArrayDim; ++Index)
        if (PointsIntoLevel(&Member, Member.ContainerPtrToValuePtr<void>(Object, Index)))
        {
            Why = FString::Printf(
                TEXT(
                    "%s of %s because it now points at something in the level. A game actor can point only at assets."),
                *Name, *Who);
            return false;
        }
    return true;
}

bool Foliage(const AActor* Actor)
{
    return Actor->IsA<AInstancedFoliageActor>() || Actor->GetClass()->GetName().StartsWith(TEXT("Landscape"));
}

TSet<FName> ComponentsOf(const AActor* Actor)
{
    TSet<FName> Out;
    TInlineComponentArray<UActorComponent*> Found(Actor);
    for (const UActorComponent* Component : Found)
        if (Component)
            Out.Add(Component->GetFName());
    return Out;
}

bool IsTableGuarded(const UDataTable* Table)
{
    if (!Table)
        return false;
    FP4OriginalRecord Record;
    FString Error;
    return Table->GetOutermost()->GetMetaData()->HasValue(Table, TEXT("P4GameRows")) ||
        P4ReadOriginalRecord(const_cast<UDataTable*>(Table), Record, Error);
}

void RememberTable(UDataTable* Table)
{
    if (Ignoring() || !IsTableGuarded(Table))
        return;
    GameRowsOf(Table);
    for (const FTableBefore& One : Tables)
        if (One.Table.Get() == Table)
            return;
    FTableBefore One;
    One.Table = Table;
    One.Rows = Table->GetRowNames();
    FObjectWriter Writer(Table, One.Bytes, false, false, false);
    Tables.Add(MoveTemp(One));
}

class FTableListener : public FDataTableEditorUtils::INotifyOnDataTableChanged
{
public:
    virtual void PreChange(const UDataTable* Table, FDataTableEditorUtils::EDataTableChangeInfo Info) override
    {
        if (Info == FDataTableEditorUtils::EDataTableChangeInfo::RowList)
            RememberTable(const_cast<UDataTable*>(Table));
    }
    virtual void PostChange(const UDataTable*, FDataTableEditorUtils::EDataTableChangeInfo) override {}
};
TUniquePtr<FTableListener> TableListener;

void OnModified(UObject* Object)
{
    if (Ignoring() || !Object)
        return;
    if (UDataTable* Table = Cast<UDataTable>(Object))
    {
        RememberTable(Table);
        return;
    }
    AActor* Actor = GameActorOf(Object);
    if (!Actor)
        return;
    if (Object == Actor && !Foliage(Actor))
    {
        bool Known = false;
        for (const FParts& One : Parts)
            if (One.Actor.Get() == Actor)
            {
                Known = true;
                break;
            }
        if (!Known)
        {
            FParts One;
            One.Actor = Actor;
            One.Components = ComponentsOf(Actor);
            Parts.Add(MoveTemp(One));
        }
    }
    // What a refused change would have to be put back to: the values of the kinds that can be refused.
    for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
    {
        FString Kind;
        if (!It->HasAnyPropertyFlags(CPF_Edit) || It->HasAnyPropertyFlags(CPF_Transient))
            continue;
        FString Unsupported;
        if (P4RefuseRecoveredPostProcessAuthoring(Object, *It, nullptr, Unsupported) || !ValueKind(*It, Kind) ||
            CastField<FObjectProperty>(*It))
            Remember(Object, *It);
    }
}

void OnPreChange(UObject* Object, const FEditPropertyChain& Chain)
{
    if (Ignoring() || !Object || !GameActorOf(Object))
        return;
    const auto* Node = Chain.GetActiveMemberNode() ? Chain.GetActiveMemberNode() : Chain.GetHead();
    if (Node && Node->GetValue())
        Remember(Object, Node->GetValue());
}

void OnChanged(UObject* Object, FPropertyChangedEvent& Event)
{
    if (Ignoring() || !Object)
        return;
    AActor* Actor = GameActorOf(Object);
    if (!Actor || Event.ChangeType == EPropertyChangeType::Interactive)
        return;
    FProperty* Member = Event.MemberProperty ? Event.MemberProperty : Event.Property;
    if (!Member)
        return;
    Object = HolderOf(Object, Member);
    if (!Object)
        return;
    int32 Found = INDEX_NONE;
    for (int32 Index = 0; Index < Befores.Num(); ++Index)
        if (Befores[Index].Object.Get() == Object && Befores[Index].Member == Member)
        {
            Found = Index;
            break;
        }
    FString Why;
    bool Ok = Allowed(Object, Actor, *Member, Why);
    if (Ok &&
        (Event.ChangeType == EPropertyChangeType::ArrayAdd || Event.ChangeType == EPropertyChangeType::ArrayRemove ||
            Event.ChangeType == EPropertyChangeType::ArrayClear || Event.ChangeType == EPropertyChangeType::Duplicate))
    {
        Ok = false;
        Why = FString::Printf(TEXT("%s of %s because it is a list, and Tsuchi cannot change that on a game actor."),
            *PropertyName(Member), *Label(Actor));
    }
    if (!Ok)
    {
        FRefusal Refusal;
        Refusal.Message = TEXT("Kadan put back ") + Why;
        if (Found != INDEX_NONE)
            Refusal.Before = Befores[Found];
        else
        {
            Refusal.Before.Object = Object;
            Refusal.Before.Member = Member;
        }
        Refusals.Add(MoveTemp(Refusal));
    }
    if (Found != INDEX_NONE)
        Befores.RemoveAt(Found);
}

void PutBackLevels()
{
    // Some native classes (BodySetup among them) omit Super in PostEditChangeProperty. Judge
    // only their still-pending prechange values, rather than scanning a level or serializing it.
    for (const FBefore& Before : Befores)
    {
        UObject* Object = Before.Object.Get();
        if (!IsValid(Object) || !Before.Member)
            continue;
        AActor* Actor = GameActorOf(Object);
        if (!Actor)
            continue;
        FString Why;
        if (Allowed(Object, Actor, *Before.Member, Why) || ValuesOf(Object, Before.Member) == Before.Values)
            continue;
        FRefusal Refusal;
        Refusal.Before = Before;
        Refusal.Message = TEXT("Kadan put back ") + Why;
        Refusals.Add(MoveTemp(Refusal));
    }
    TArray<FRefusal> Held = MoveTemp(Refusals);
    Refusals.Reset();
    for (FRefusal& One : Held)
    {
        UObject* Object = One.Before.Object.Get();
        FProperty* Member = One.Before.Member;
        if (IsValid(Object) && Member && One.Before.Values.Num() == Member->ArrayDim)
        {
            Object->PreEditChange(Member);
            for (int32 Index = 0; Index < Member->ArrayDim; ++Index)
                Member->ImportText(
                    *One.Before.Values[Index], Member->ContainerPtrToValuePtr<void>(Object, Index), PPF_None, Object);
            FPropertyChangedEvent Event(Member, EPropertyChangeType::ValueSet);
            Object->PostEditChangeProperty(Event);
            Tell(One.Message);
        }
        else if (IsValid(Object))
            Tell(One.Message.Replace(TEXT("Kadan put back "), TEXT("Kadan cannot export a change of ")) +
                TEXT(" Undo the change before you export."));
    }
    TArray<FParts> Actors = MoveTemp(Parts);
    Parts.Reset();
    for (const FParts& One : Actors)
    {
        AActor* Actor = One.Actor.Get();
        if (!IsValid(Actor))
            continue;
        TInlineComponentArray<UActorComponent*> Found(Actor);
        for (UActorComponent* Component : Found)
        {
            if (!Component || One.Components.Contains(Component->GetFName()) ||
                Component->CreationMethod != EComponentCreationMethod::Instance)
                continue;
            const FString Name = Component->GetName();
            Actor->Modify();
            Component->Modify();
            Actor->RemoveInstanceComponent(Component);
            Component->DestroyComponent();
            Tell(
                FString::Printf(TEXT("Kadan removed %s from %s because Tsuchi cannot add a component to a game actor."),
                    *Name, *Label(Actor)));
        }
    }
    // A value is kept only until the change it was taken for is judged.
    Befores.Reset();
}

void PutBackTables()
{
    TArray<FTableBefore> Held = MoveTemp(Tables);
    Tables.Reset();
    for (FTableBefore& One : Held)
    {
        UDataTable* Table = One.Table.Get();
        if (!IsValid(Table))
            continue;
        const TArray<FName> Game = GameRowsOf(Table), Now = Table->GetRowNames();
        const FString Name = FPackageName::GetLongPackageAssetName(Table->GetOutermost()->GetName());
        TArray<FString> Messages;
        TArray<FName> Lost, Added;
        for (const FName& Row : One.Rows)
            if (Game.Contains(Row) && !Now.Contains(Row))
                Lost.Add(Row);
        for (const FName& Row : Now)
            if (!One.Rows.Contains(Row))
                Added.Add(Row);
        if (Lost.Num())
            Messages.Add(FString::Printf(
                TEXT(
                    "Kadan put back the game row %s in %s because game rows cannot be deleted or renamed. Add a new row instead."),
                *Lost[0].ToString(), *Name));
        TArray<FName> OrderBefore, OrderNow;
        for (const FName& Row : One.Rows)
            if (Game.Contains(Row) && Now.Contains(Row))
                OrderBefore.Add(Row);
        for (const FName& Row : Now)
            if (Game.Contains(Row) && One.Rows.Contains(Row))
                OrderNow.Add(Row);
        if (OrderBefore != OrderNow)
            Messages.Add(FString::Printf(
                TEXT("Kadan put back the order of the game rows in %s because game rows cannot be moved."), *Name));
        if (Added.Num() && IsKindTable(Table))
            Messages.Add(FString::Printf(
                TEXT(
                    "Kadan removed the new row %s from %s. New treasures, creatures and Pikmin types are made through content."),
                *Added[0].ToString(), *Name));
        if (!Messages.Num())
            continue;
        FDataTableEditorUtils::BroadcastPreChange(Table, FDataTableEditorUtils::EDataTableChangeInfo::RowList);
        {
            FObjectReader Reader(Table, One.Bytes, false, false);
        }
        FDataTableEditorUtils::BroadcastPostChange(Table, FDataTableEditorUtils::EDataTableChangeInfo::RowList);
        for (const FString& Message : Messages)
            Tell(Message);
    }
}
} //namespace

AActor* GameActorOf(const UObject* Object)
{
    if (!Active || !Object || Object->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
        return nullptr;
    AActor* Actor = const_cast<AActor*>(Cast<AActor>(Object));
    if (!Actor)
        Actor = Object->GetTypedOuter<AActor>();
    if (!Actor || !Actor->GetLevel())
        return nullptr;
    UWorld* World = UWorld::FindWorldInPackage(Actor->GetOutermost());
    if (!World || World->PersistentLevel != Actor->GetLevel())
        return nullptr;
    const TSet<FName>* Names = GameActors.Find(World);
    if (!Names)
    {
        for (auto It = GameActors.CreateIterator(); It; ++It)
            if (!It.Key().IsValid())
                It.RemoveCurrent();
        Names = &GameActors.Add(World, P4ImportedLevelActors(World));
    }
    return Names->Contains(Actor->GetFName()) ? Actor : nullptr;
}

#if P4_KADAN_TEST_SEAMS
// Only evicts the controlled test world's name cache; the actual classifier still
// reads its ordinary packed imported-actor metadata on the next public call.
void ForgetGameActorNamesForTest(UWorld* World)
{
    GameActors.Remove(World);
}
#endif

bool CanEditGameProperty(
    UObject* Object, const FProperty& Property, const TArray<const FProperty*>& Parents, FString* Why)
{
    AActor* Actor = GameActorOf(Object);
    if (!Actor)
        return true;
    FString Reason;
    const FProperty& Member = Parents.Num() ? *Parents[0] : Property;
    Object = HolderOf(Object, &Member);
    if (!Object)
        return true;
    const bool Ok = Allowed(Object, Actor, Member, Reason);
    if (Why)
        *Why = Reason;
    return Ok;
}

TArray<FName> GameRowsOf(const UDataTable* Table)
{
    TArray<FName> Out;
    if (!Table)
        return Out;
    const FString Kept = Table->GetOutermost()->GetMetaData()->GetValue(Table, TEXT("P4GameRows"));
    if (!Kept.IsEmpty())
    {
        TArray<FString> Names;
        Kept.ParseIntoArray(Names, TEXT("\n"), true);
        for (const FString& Name : Names)
            Out.Add(FName(*Name));
        return Out;
    }
    FP4OriginalRecord Record;
    FString Error;
    if (!P4ReadOriginalRecord(const_cast<UDataTable*>(Table), Record, Error))
        return Out;
    // Imported before Kadan kept the game's row names: the rows the table had when this editor first saw it.
    if (const TArray<FName>* Seen = SeenRows.Find(Table))
        return *Seen;
    return SeenRows.Add(Table, Table->GetRowNames());
}

bool IsKindTable(const UDataTable* Table)
{
    if (!Table)
        return false;
    const FString Package = Table->GetOutermost()->GetName();
    for (const TCHAR* Kind : {TEXT("/Game/Carrot4/Core/GActor/DT_OtakaraParameter"),
             TEXT("/Game/Carrot4/Core/GActor/DT_TekiParameter"), TEXT("/Game/Carrot4/Core/GActor/DT_PikminProperty"),
             TEXT("/Game/Carrot4/Placeables/Zukan/DT_ZukanOtakaraParameter"),
             TEXT("/Game/Carrot4/Placeables/Zukan/DT_ZukanTekiParameter"),
             TEXT("/Game/Carrot4/Placeables/Zukan/DT_ZukanBP"), TEXT("/Game/Carrot4/Core/UI/DT_ActorTexture")})
        if (Package.Equals(Kind, ESearchCase::IgnoreCase))
            return true;
    return false;
}

void FlushEditGuards()
{
    if (!Active || Restoring || !IsInGameThread())
        return;
    TGuardValue<bool> Guard(Restoring, true);
    PutBackLevels();
    PutBackTables();
}

void InitializeEditGuards()
{
    if (Active)
        return;
    Active = true;
    PreChanged = FCoreUObjectDelegates::OnPreObjectPropertyChanged.AddStatic(&OnPreChange);
    Changed = FCoreUObjectDelegates::OnObjectPropertyChanged.AddStatic(&OnChanged);
    ModifiedHandle = FCoreUObjectDelegates::OnObjectModified.AddStatic(&OnModified);
    MapOpened = FEditorDelegates::OnMapOpened.AddLambda([](const FString&, bool) { GameActors.Reset(); });
    TableListener = MakeUnique<FTableListener>();
}

void ShutdownEditGuards()
{
    if (!Active)
        return;
    TableListener.Reset();
    FCoreUObjectDelegates::OnPreObjectPropertyChanged.Remove(PreChanged);
    FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(Changed);
    FCoreUObjectDelegates::OnObjectModified.Remove(ModifiedHandle);
    FEditorDelegates::OnMapOpened.Remove(MapOpened);
    GameActors.Reset();
    SeenRows.Reset();
    Befores.Reset();
    Refusals.Reset();
    Parts.Reset();
    Tables.Reset();
    Active = false;
}
} //namespace P4Kadan

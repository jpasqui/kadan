// Placement export: the placement list of a mod's map, its rows and their blobs.
#include "P4PlacementInternal.h"

using namespace P4PlacementDetail;

namespace
{
// A slot's blob from an object's values (the placed actor's, or its class's for comparison).
bool Fill(P::Slot Slot, const P::Context& Context, UObject* Owner, AActor* Actor, P::Blob& Out, FString& Error,
    bool ClassDefaults = false, uint64 Version = P::RowVersion)
{
    if (!P::Fresh(Slot, Context, Version, Out))
    {
        Error = TEXT("no layout");
        return false;
    }
    const std::vector<P::Field> Fields = LayoutFields(Slot, Context);
    for (auto& Member : Out.Root.Members)
    {
        const std::string& Name = Member.first;
        P::Value& Value = Member.second;
        const P::Field* Found = FieldNamed(Fields, Name);
        if (!Found)
        {
            Error = Wide(Name) + TEXT(" is not in the layout");
            return false;
        }
        const P::Field& Field = *Found;
        if (Field.ComponentTransform)
        {
            if (!ComponentValue(Actor, Field, Value, Error))
                return false;
            continue;
        }
        if (!Field.ComponentTag.empty())
        {
            // A counted run of tagged locations: as many as its counter, a property filled before it, says.
            const P::Value* Counter = Out.Root.Find(Field.Of->CountFrom);
            if (!Counter)
            {
                Error = Wide(Name) + TEXT(" has no counter");
                return false;
            }
            if (!TaggedRun(Actor, Field.ComponentTag, Counter->Bits, Value.Items))
            {
                Error = FString::Printf(TEXT("%s says %llu, but the actor has fewer components tagged %s"),
                    *Wide(Field.Of->CountFrom), Counter->Bits, *Wide(Field.ComponentTag));
                return false;
            }
            continue;
        }
        if (!Field.CountOf.empty())
        {
            const int32 Count = CountOf(Owner, *Wide(Field.CountOf));
            if (Field.Of->Is == P::Kind::Bools || Field.Of->Is == P::Kind::Repeat)
            {
                Value.Items.assign(size_t(Count), P::Value());
                for (auto& Item : Value.Items)
                    Item.Is = Field.Of->Item->Is;
            }
            else
                Value.Bits = uint32(Count);
            if (Field.StopIfSet)
                Out.UnreadFields = Value.Set();
            continue;
        }
        // Written as the game starts it: dropped on load, or run-time state.
        if (Field.Derived)
        {
            Value.Bits = Field.RuntimeDefault;
            continue;
        }
        FProperty* Leaf = nullptr;
        void* Data = nullptr;
        AActor* FieldActor = ClassDefaults ? Cast<AActor>(Actor->GetArchetype()) : Actor;
        if (!ResolvePlacementField(Owner, FieldActor, Wide(Name), Leaf, Data, Error))
            return false;
        if (!ToValue(Leaf, Data, *Field.Of, Value, Error))
        {
            Error = Wide(Name) + TEXT(": ") + Error;
            return false;
        }
    }
    return true;
}

// What a blob says that the creator can change: every field but the derived ones.
std::vector<std::string> Options(P::Slot Slot, const P::Context& Context, const P::Blob& Blob)
{
    std::vector<std::string> Out;
    const std::vector<P::Field> Fields = LayoutFields(Slot, Context, Blob.Variant);
    for (const auto& Member : Blob.Root.Members)
    {
        const P::Field* Field = FieldNamed(Fields, Member.first);
        if (!Field || !Field->Derived)
            P::Flatten(Member.second, Member.first, Out);
    }
    return Out;
}

// ------------------------------------------------------------------ the row, through reflection
struct FIn
{
    const UStruct* Type;
    void* Data;
};

FProperty* MemberOf(const FIn& In, const TCHAR* Name, void*& Value)
{
    FProperty* Field = In.Type->FindPropertyByName(Name);
    Value = Field ? Field->ContainerPtrToValuePtr<void>(In.Data) : nullptr;
    return Field;
}

bool SetInt(const FIn& In, const TCHAR* Name, int64 Number, bool Unsigned = false, uint64 Big = 0)
{
    void* Value;
    const FNumericProperty* Field = Numeric(MemberOf(In, Name, Value));
    if (!Field || !Field->IsInteger())
        return false;
    if (Unsigned)
        Field->SetIntPropertyValue(Value, Big);
    else
        Field->SetIntPropertyValue(Value, Number);
    return true;
}

bool SetFloat(const FIn& In, const TCHAR* Name, float Number)
{
    void* Value;
    const FFloatProperty* Field = CastField<FFloatProperty>(MemberOf(In, Name, Value));
    if (Field)
        Field->SetPropertyValue(Value, Number);
    return Field != nullptr;
}

bool SetBool(const FIn& In, const TCHAR* Name, bool On)
{
    void* Value;
    const FBoolProperty* Field = CastField<FBoolProperty>(MemberOf(In, Name, Value));
    if (Field)
        Field->SetPropertyValue(Value, On);
    return Field != nullptr;
}

bool SetName(const FIn& In, const TCHAR* Name, FName Text)
{
    void* Value;
    const FNameProperty* Field = CastField<FNameProperty>(MemberOf(In, Name, Value));
    if (Field)
        Field->SetPropertyValue(Value, Text);
    return Field != nullptr;
}

bool SetEnum(const FIn& In, const TCHAR* Name, const TCHAR* Member)
{
    void* Value;
    FProperty* Field = MemberOf(In, Name, Value);
    const UEnum* Enum = CastField<FEnumProperty>(Field) ? CastField<FEnumProperty>(Field)->GetEnum()
        : CastField<FByteProperty>(Field)               ? CastField<FByteProperty>(Field)->Enum
                                                        : nullptr;
    const FNumericProperty* Number = Numeric(Field);
    if (!Enum || !Number)
        return false;
    const int64 Found = Enum->GetValueByNameString(Enum->GetName() + TEXT("::") + Member);
    if (Found == INDEX_NONE)
        return false;
    Number->SetIntPropertyValue(Value, Found);
    return true;
}

template <class T>
bool SetStruct(const FIn& In, const TCHAR* Name, const T& What)
{
    void* Value;
    const FStructProperty* Field = CastField<FStructProperty>(MemberOf(In, Name, Value));
    if (!Field || Field->Struct != TBaseStructure<T>::Get())
        return false;
    *static_cast<T*>(Value) = What;
    return true;
}

bool Inner(const FIn& In, const TCHAR* Name, FIn& Out)
{
    void* Value;
    const FStructProperty* Field = CastField<FStructProperty>(MemberOf(In, Name, Value));
    if (!Field)
        return false;
    Out = FIn{Field->Struct, Value};
    return true;
}

bool SetBytes(const FIn& In, const TCHAR* Name, const std::vector<uint8>& Bytes)
{
    void* Value;
    const FArrayProperty* Field = CastField<FArrayProperty>(MemberOf(In, Name, Value));
    if (!Field || !CastField<FByteProperty>(Field->Inner))
        return false;
    FScriptArrayHelper Array(Field, Value);
    Array.Resize(int32(Bytes.size()));
    if (Bytes.size())
        FMemory::Memcpy(Array.GetRawPtr(0), Bytes.data(), Bytes.size());
    return true;
}

// Each value of a struct the creator changed on the actor (it differs from the class's) goes into the row's.
void TakeChanged(const UStruct* Type, void* Row, const void* Placed, const void* Class)
{
    for (TFieldIterator<FProperty> It(Type); It; ++It)
    {
        const FProperty* Field = *It;
        const FStructProperty* Struct = CastField<FStructProperty>(Field);
        if (Struct && Struct->Struct != TBaseStructure<FVector>::Get() &&
            Struct->Struct != TBaseStructure<FRotator>::Get())
            TakeChanged(Struct->Struct, Field->ContainerPtrToValuePtr<void>(Row),
                Field->ContainerPtrToValuePtr<void>(Placed), Field->ContainerPtrToValuePtr<void>(Class));
        else if (!Field->Identical_InContainer(Placed, Class))
            Field->CopyCompleteValue_InContainer(Row, Placed);
    }
}

bool TakeChangedMember(const FIn& Row, AActor* Actor, const TCHAR* Name)
{
    const UObject* Class = Actor->GetArchetype();
    const FProperty* Field = Actor->GetClass()->FindPropertyByName(Name);
    FIn Target;
    const FStructProperty* Struct = CastField<FStructProperty>(Field);
    if (!Struct || !Class || !Inner(Row, Name, Target) || Target.Type != Struct->Struct)
        return false;
    TakeChanged(Struct->Struct, Target.Data, Field->ContainerPtrToValuePtr<void>(Actor),
        Field->ContainerPtrToValuePtr<void>(const_cast<UObject*>(Class)));
    return true;
}

// The options of an object that the creator set apart from its class's, by name.
TArray<FString> EditedFromClass(UObject* Object)
{
    TArray<FString> Out;
    const UObject* Class = Object ? Object->GetArchetype() : nullptr;
    if (!Class || Class->GetClass() != Object->GetClass())
        return Out;
    for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
        if (It->HasAnyPropertyFlags(CPF_Edit) && !It->HasAnyPropertyFlags(CPF_Transient | CPF_EditConst) &&
            !It->Identical_InContainer(Object, Class))
            Out.Add(It->GetName());
    return Out;
}

// The values every row of Kadan's starts from: one actor made, kept once used up, not counted for the
// exploration rate, no team.
bool CommonRow(const FIn& Row, uint64 Id, bool Teki, FString& Error)
{
    FIn Generate, Rebirth, Drop;
    const bool Set = SetInt(Row, TEXT("AssetVersion"), 0, true, P::RowVersion) &&
        SetInt(Row, TEXT("GeneratorVersion"), 0, true, P::RowVersion) && SetInt(Row, TEXT("GeneratorID"), -1) &&
        SetEnum(Row, TEXT("ExploreRateType"), TEXT("None")) && SetInt(Row, TEXT("ActorVersion"), 1) &&
        SetName(Row, TEXT("OutlineFolderPath"), NAME_None) && SetEnum(Row, TEXT("TeamId"), TEXT("No")) &&
        SetInt(Row, TEXT("RefOriginalGenID"), -1) && Inner(Row, TEXT("GenerateInfo"), Generate) &&
        SetBool(Generate, TEXT("EnableSave"), true) && SetInt(Generate, TEXT("DebugUniqueId"), 0, true, Id) &&
        SetName(Generate, TEXT("ActorGlobalId"), NAME_None) && SetInt(Generate, TEXT("GenerateNum"), 1) &&
        SetFloat(Generate, TEXT("GenerateRadius"), 300.f) && Inner(Row, TEXT("RebirthInfo"), Rebirth) &&
        SetEnum(Rebirth, TEXT("ActivityTime"), TEXT("Allday")) &&
        SetEnum(Rebirth, TEXT("RebirthType"), TEXT("NoRebirth")) && SetInt(Rebirth, TEXT("CurrNum"), 1) &&
        SetEnum(Rebirth, TEXT("SaveFlag"), Teki ? TEXT("SaveNum") : TEXT("SaveNumAndActors")) &&
        SetInt(Rebirth, TEXT("MyID"), -1) && SetInt(Rebirth, TEXT("RefID"), -1) &&
        Inner(Row, TEXT("DropActorInfo"), Drop) && SetInt(Drop, TEXT("DropOwnerDebugUniqueId"), 0, true, ~uint64(0)) &&
        SetInt(Drop, TEXT("DropIndex"), -1);
    if (!Set)
        Error = TEXT("The game's placement row is not laid out as Kadan expects (ActorGenerator)");
    return Set;
}

// The drops of a value's fields get their generator numbers, in field order. Drops that are generators of their
// own get numbers of the list, keyed by the actor, the field's key word and their place, and are listed in the
// field's list; other drops get none. Place is the indices of the arrays of structs the value is in.
void NumberDrops(P::Value& In, const std::vector<P::Field>& Fields, const std::string& Actor, const std::string& Place,
    uint32 List, std::vector<uint64>& Taken)
{
    for (const P::Field& Field : Fields)
    {
        P::Value* Value = In.Find(Field.Name);
        if (!Value)
            continue;
        if (Field.Drops)
        {
            std::vector<uint64> Ids;
            if (!Field.DropKey.empty())
            {
                std::vector<std::string> Keys;
                for (size_t I = 0; I < Value->Items.size(); ++I)
                    Keys.push_back(Actor + "\n" + Field.DropKey + Place + "\n" + std::to_string(I));
                Ids = P::Numbers(List, Keys, Taken);
                Taken.insert(Taken.end(), Ids.begin(), Ids.end());
            }
            P::SetDropGeneratorNumbers(*Value, Field.DropList.empty() ? nullptr : In.Find(Field.DropList), Ids);
            continue;
        }
        const P::Type* Item = Field.Of->Is == P::Kind::Array ? Field.Of->Item : nullptr;
        if (Item && Item->Is == P::Kind::Struct)
            for (size_t I = 0; I < Value->Items.size(); ++I)
                NumberDrops(Value->Items[I], Item->Members, Actor, Place + "\n" + std::to_string(I), List, Taken);
        if (Field.Of->Is == P::Kind::Struct)
            NumberDrops(*Value, Field.Of->Members, Actor, Place, List, Taken);
    }
}
} // namespace

bool P4IsGameplayActor(const AActor* Actor)
{
    return Actor && Derives(Actor->GetClass(), TEXT("GActor"));
}

FString P4PlacementIssue(const AActor* Actor)
{
    if (!P4IsGameplayActor(Actor))
        return TEXT("It is not one of the game's gameplay objects.");
    const UClass* Class = Actor->GetClass();
    const FString Package = Class->GetOutermost()->GetName();
    if (!Class->ClassGeneratedBy || !P4IsContentPackage(Package))
        return FString::Printf(
            TEXT("%s is placed from the game's code class %s. Place the game's Blueprint of it instead."),
            *Actor->GetName(), *Class->GetName());
    if (P4IsModFolderPackage(Package))
    {
        const FString Root = P4ModFolderRootOf(Package);
        if (Root.IsEmpty())
            return TEXT("Keep the new gameplay class inside its mod's folder.");
        const FString Issue = P4CreatorBlueprintIssue(Cast<UBlueprint>(Class->ClassGeneratedBy), Root);
        if (!Issue.IsEmpty())
            return Issue;
    }
    if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
        return FString::Printf(TEXT("%s is of a class the game does not place."), *Actor->GetName());
    return FString();
}

bool P4BuildPlacementList(const FString& ModId, const FString& PackageRoot, const FString& MapPackage,
    const TArray<AActor*>& Actors, FP4PlacementList& Out, FString& Error)
{
    Out = FP4PlacementList();
    if (!IsInGameThread())
    {
        Error = TEXT("Placement lists are made on the editor thread");
        return false;
    }
    UClass* InfoClass = GameClass(TEXT("ActorPlacementInfoAsset"));
    UClass* ListClass = GameClass(TEXT("ActorPlacementInfoListAsset"));
    if (!InfoClass || !ListClass)
    {
        Error = TEXT(
            "The game's placement list classes are not in this project. Add the game's code classes (P4Stubs) to place gameplay objects.");
        return false;
    }
    for (const AActor* Actor : Actors)
    {
        const FString Issue = P4PlacementIssue(Actor);
        if (!Issue.IsEmpty())
        {
            Error = Issue;
            return false;
        }
    }
    const FString Leaf = FPackageName::GetLongPackageAssetName(MapPackage);
    const FString Package = PackageRoot + TEXT("/Placements/") + Leaf + TEXT("_Objects"),
                  ListPackage = PackageRoot + TEXT("/Placements/") + Leaf + TEXT("_Placements");
    for (const FString& One : {Package, ListPackage})
        if (FPackageName::DoesPackageExist(One) || FindPackage(nullptr, *One))
        {
            Error = One + TEXT(" is where export puts this map's placement list. Move what is there.");
            return false;
        }

    // Placement numbers: the same actors give the same numbers on every export.
    TArray<AActor*> Sorted = Actors;
    Sorted.Sort([](const AActor& A, const AActor& B) { return ActorKey(&A) < ActorKey(&B); });
    const uint32 List = P::ListWord(Utf8(ModId), Utf8(MapPackage));
    std::vector<std::string> Keys{"\nmark"};
    for (const AActor* Actor : Sorted)
        Keys.push_back(Utf8(ActorKey(Actor)));
    std::vector<uint64> Taken = P::Numbers(List, Keys);

    UPackage* Made = CreatePackage(*Package);
    UObject* Asset = NewObject<UObject>(
        Made, InfoClass, FName(*FPackageName::GetLongPackageAssetName(Package)), RF_Public | RF_Standalone);
    Out.Package = Package;
    Out.Asset.Reset(Asset);
    Out.MarkId = Taken[0];
    FArrayProperty* Rows = CastField<FArrayProperty>(InfoClass->FindPropertyByName(TEXT("ActorGeneratorList")));
    FStructProperty* RowField = Rows ? CastField<FStructProperty>(Rows->Inner) : nullptr;
    if (!RowField)
    {
        P4ReleasePlacementList(Out);
        Error = TEXT("The game's placement list has no ActorGeneratorList");
        return false;
    }
    FScriptArrayHelper Array(Rows, Rows->ContainerPtrToValuePtr<void>(Asset));
    Array.Resize(Sorted.Num() + 1);
    // The mark row: no class, so the game never makes anything of it and it is never used up.
    {
        const FIn Mark{RowField->Struct, Array.GetRawPtr(0)};
        if (!CommonRow(Mark, Taken[0], true, Error))
        {
            P4ReleasePlacementList(Out);
            return false;
        }
        SetName(Mark, TEXT("SubLevelName"), FName(*Leaf));
    }
    UClass* GActor = GameClass(TEXT("GActor"));
    for (int32 Index = 0; Index < Sorted.Num(); ++Index)
    {
        AActor* Actor = Sorted[Index];
        const uint64 Id = Taken[size_t(Index) + 1];
        const FIn Row{RowField->Struct, Array.GetRawPtr(Index + 1)};
        UClass* Class = Actor->GetClass();
        UActorComponent* AI = ChainComponentOf(Actor);
        const bool Teki = AI && Derives(AI->GetClass(), TEXT("TekiAIComponent"));
        FP4PlacementRow Told;
        Told.Actor = Actor->GetName();
        Told.Class = Class->GetPathName();
        Told.Id = Id;
        Told.Transform = Actor->GetActorTransform();
        if (!CommonRow(Row, Id, Teki, Error))
        {
            P4ReleasePlacementList(Out);
            return false;
        }
        bool Set = true;
        {
            void* Value = nullptr;
            if (FObjectPropertyBase* Field = CastField<FObjectPropertyBase>(MemberOf(Row, TEXT("ActorClass"), Value)))
                Field->SetObjectPropertyValue(Value, Class);
            else
                Set = false;
            if (FSoftObjectProperty* Field = CastField<FSoftObjectProperty>(
                    MemberOf(Row, TEXT("SoftRefActorClass"), Value)))
                Field->SetPropertyValue(Value, FSoftObjectPtr(FSoftObjectPath(Class->GetPathName())));
            else
                Set = false;
        }
        const FTransform Place(Actor->GetActorQuat(), Actor->GetActorLocation(), Actor->GetActorScale3D());
        Set = Set && SetStruct(Row, TEXT("InitTransform"), Place) && SetStruct(Row, TEXT("Transform"), Place) &&
            SetStruct(Row, TEXT("LastNavPos"), Actor->GetActorLocation()) &&
            SetStruct(Row, TEXT("LastNavRotator"), Actor->GetActorRotation()) &&
            SetName(Row, TEXT("SubLevelName"), FName(*LevelLeaf(Actor)));
        // The actor's own generator settings, where the creator changed them.
        Set = Set && TakeChangedMember(Row, Actor, TEXT("GenerateInfo")) &&
            TakeChangedMember(Row, Actor, TEXT("RebirthInfo")) && TakeChangedMember(Row, Actor, TEXT("CarriedInfo"));
        FIn Generate;
        Set = Set && Inner(Row, TEXT("GenerateInfo"), Generate) && SetInt(Generate, TEXT("DebugUniqueId"), 0, true, Id);
        {
            const UObject* Archetype = Actor->GetArchetype();
            const auto Copy = [&](const TCHAR* From, const TCHAR* To, bool OnlyChanged)
            {
                const FProperty* Source = GActor ? GActor->FindPropertyByName(From) : nullptr;
                void* Target = nullptr;
                FProperty* Field = MemberOf(Row, To, Target);
                if (!Source || !Field || !Source->SameType(Field))
                    return false;
                if (!OnlyChanged || !Source->Identical_InContainer(Actor, Archetype))
                    Source->CopyCompleteValue(Target, Source->ContainerPtrToValuePtr<void>(Actor));
                return true;
            };
            Set = Set && Copy(TEXT("TeamIdEditor"), TEXT("TeamId"), true) &&
                Copy(TEXT("GenerateFlags"), TEXT("GenerateFlags"), false) &&
                Copy(TEXT("OriginalPhysicsRadiusZ"), TEXT("OriginalPhysicsRadiusZ"), false) &&
                Copy(TEXT("ActorVersion"), TEXT("ActorVersion"), false);
        }
        if (!Set)
        {
            P4ReleasePlacementList(Out);
            Error = TEXT("The game's placement row is not laid out as Kadan expects (ActorGenerator)");
            return false;
        }

        // The blobs. One the creator left as the class has it stays empty, so the game uses the class's values.
        FIn Serialize;
        if (!Inner(Row, TEXT("ActorSerializeParameter"), Serialize))
        {
            P4ReleasePlacementList(Out);
            Error = TEXT("The row has no ActorSerializeParameter");
            return false;
        }
        P::Context Context = ContextOf(Actor);
        for (const P::SlotName& Slot : P::Slots())
        {
            UObject* Owner = OwnerOf(Actor, Slot);
            if (Owner && Owner != Actor && P::Layouts(Slot.S, Context).empty() && !NotAnOption(SlotKey(Slot)))
            {
                // A part whose blob Kadan cannot write: the game gives it the class's values, so the creator hears of
                // the options set on it.
                const TArray<FString> Edited = EditedFromClass(Owner);
                if (Edited.Num())
                    Out.Told.Add(FString::Printf(
                        TEXT("%s: Kadan cannot write the game's options of %s (%s) yet, so the game uses the class's values in place of %s."),
                        *Actor->GetName(), *Owner->GetClass()->GetName(), *SlotKey(Slot), *FString::Join(Edited, TEXT(", "))));
                continue;
            }
            if (!Owner || P::Layouts(Slot.S, Context).empty())
                continue;
            if (!Writable(Owner, Slot, Context))
            {
                // Kadan knows only a part of this blob for the owner's class, and the game would read past a short
                // one, or the blob holds what the game takes from the actor's components and Kadan does not fill yet:
                // the game keeps the class's values, and the creator hears of the options set on it.
                // Only the options this blob holds: its fields' paths start with the property, or with the class and
                // then the property.
                TSet<FString> Held;
                for (const P::Field& Field : LayoutFields(Slot.S, Context))
                {
                    TArray<FString> Steps;
                    Wide(Field.Name).ParseIntoArray(Steps, TEXT("."));
                    for (int32 Step = 0; Step < FMath::Min(2, Steps.Num()); ++Step)
                        Held.Add(Steps[Step]);
                }
                TArray<FString> Edited = EditedFromClass(Owner);
                Edited.RemoveAll([&](const FString& Name) { return !Held.Contains(Name); });
                if (Edited.Num() && !NotAnOption(SlotKey(Slot)))
                    Out.Told.Add(FString::Printf(
                        TEXT("%s: Kadan cannot write the game's options of %s (%s) yet, so the game uses the class's values in place of %s."),
                        *Actor->GetName(), *Owner->GetClass()->GetName(), *SlotKey(Slot), *FString::Join(Edited, TEXT(", "))));
                continue;
            }
            UObject* Archetype = Owner->GetArchetype();
            P::Blob Placed, Default;
            const FString Where = FString::Printf(TEXT("%s (%s)"), *Actor->GetName(), *SlotKey(Slot));
            if (!Fill(Slot.S, Context, Owner, Actor, Placed, Error))
            {
                P4ReleasePlacementList(Out);
                Error = TEXT("The options of ") + Where + TEXT(" cannot be read: ") + Error;
                return false;
            }
            // A counted field that stops the stream (the audio positions' shape references) is not written yet.
            if (Placed.UnreadFields)
            {
                P4ReleasePlacementList(Out);
                Error = TEXT("The options of ") + Where + TEXT(" include references that cannot be written yet.");
                return false;
            }
            if (!Archetype || !Fill(Slot.S, Context, Archetype, Actor, Default, Error, true))
            {
                P4ReleasePlacementList(Out);
                Error = TEXT("The options of ") + Where + TEXT(" cannot be read: ") + Error;
                return false;
            }
            if (Options(Slot.S, Context, Placed) == Options(Slot.S, Context, Default))
                continue;
            // A creature's drops are generators of their own, with numbers of the list's; a gimmick's are not.
            NumberDrops(Placed.Root, LayoutFields(Slot.S, Context), Utf8(ActorKey(Actor)), std::string(), List, Taken);
            std::vector<uint8> Bytes;
            std::string Why;
            if (!P::Encode(Slot.S, Context, Placed, P::RowVersion, Bytes, Why) || !Placed.Exact())
            {
                P4ReleasePlacementList(Out);
                Error = TEXT("The changed options of ") + Where + TEXT(" cannot be written yet") +
                    (Why.empty() ? FString(TEXT(" (a part of them is not decoded)"))
                                 : TEXT(" (") + Wide(Why) + TEXT(")"));
                return false;
            }
            FIn Pair;
            if (!Inner(Serialize, *FString(Slot.Row), Pair) ||
                !SetBytes(Pair, Slot.Static ? TEXT("Static") : TEXT("Dynamic"), Bytes))
            {
                P4ReleasePlacementList(Out);
                Error = TEXT("The row has no ") + FString(Slot.Row);
                return false;
            }
            Told.Blobs.Add(SlotKey(Slot), TArray<uint8>(Bytes.data(), int32(Bytes.size())));
        }
        // A value the creator set that neither a blob of the row nor the row's own settings hold does not reach the
        // game, which makes the object from its row: the creator hears of it, as for the game's own objects.
        {
            TInlineComponentArray<UActorComponent*> Parts(Actor);
            TArray<UObject*> Objects{Actor};
            for (UActorComponent* Part : Parts)
                if (Part)
                    Objects.Add(Part);
            for (UObject* Object : Objects)
            {
                const TArray<TPair<FProperty*, bool>> Held = HeldOptions(Actor, Object, Context);
                for (const FString& Name : EditedFromClass(Object))
                {
                    const FProperty* Property = Object->GetClass()->FindPropertyByName(FName(*Name));
                    if (!GameProperty(Property) || (Object == Actor && RowSetting(Property)) ||
                        Held.ContainsByPredicate([&](const TPair<FProperty*, bool>& One) { return One.Key == Property; }))
                        continue;
                    Out.Told.Add(FString::Printf(
                        TEXT("%s: the change to %s does not reach the game: the game makes the object from its placement row, which does not hold this value."),
                        *Actor->GetName(), *((Object == Actor ? FString() : Object->GetName() + TEXT(".")) + Name)));
                }
            }
        }
        Out.Rows.Add(MoveTemp(Told));
    }
    // The list asset of the map, holding the placement list.
    UPackage* MadeList = CreatePackage(*ListPackage);
    UObject* Holder = NewObject<UObject>(
        MadeList, ListClass, FName(*FPackageName::GetLongPackageAssetName(ListPackage)), RF_Public | RF_Standalone);
    Out.ListPackage = ListPackage;
    Out.ListAsset.Reset(Holder);
    FArrayProperty* Assets = CastField<FArrayProperty>(ListClass->FindPropertyByName(TEXT("AssetList")));
    FObjectPropertyBase* Entry = Assets ? CastField<FObjectPropertyBase>(Assets->Inner) : nullptr;
    if (!Entry)
    {
        P4ReleasePlacementList(Out);
        Error = TEXT("The game's list of placement lists has no AssetList");
        return false;
    }
    FScriptArrayHelper Held(Assets, Assets->ContainerPtrToValuePtr<void>(Holder));
    Held.Resize(1);
    Entry->SetObjectPropertyValue(Held.GetRawPtr(0), Asset);
    Error.Reset();
    return true;
}

namespace
{
// The changed property whose value holds the memory at Data, or INDEX_NONE.
int32 ChangedAt(const TArray<FP4ChangedProperty>& Changed, const void* Data)
{
    for (int32 Index = 0; Index < Changed.Num(); ++Index)
    {
        const FP4ChangedProperty& One = Changed[Index];
        if (!One.Object || !One.Property)
            continue;
        const uint8* Start = One.Property->ContainerPtrToValuePtr<uint8>(One.Object);
        const uint8* End = Start + One.Property->ElementSize * One.Property->ArrayDim;
        if (static_cast<const uint8*>(Data) >= Start && static_cast<const uint8*>(Data) < End)
            return Index;
    }
    return INDEX_NONE;
}

// Counted fields follow what they count, when the creator changed the list they count.
void Recount(P::Blob& Blob, const std::vector<P::Field>& Fields, UObject* Owner)
{
    for (const P::Field& Field : Fields)
    {
        P::Value* Value = Field.CountOf.empty() ? nullptr : Blob.Root.Find(Field.Name);
        if (!Value)
            continue;
        const int32 Count = CountOf(Owner, *Wide(Field.CountOf));
        if (Field.Of->Is == P::Kind::Bools || Field.Of->Is == P::Kind::Repeat)
        {
            const size_t Before = Value->Items.size();
            Value->Items.resize(size_t(Count));
            for (size_t Item = Before; Item < Value->Items.size(); ++Item)
                Value->Items[Item].Is = Field.Of->Item->Is;
        }
        else
            Value->Bits = uint32(Count);
    }
}

// Whether a blob holds a drop: a drop of the game's is a generator of its own, numbered in the game's lists.
bool HasDrops(const P::Blob& Blob, const std::vector<P::Field>& Fields)
{
    for (const P::Field& Field : Fields)
    {
        const P::Value* Value = Field.Drops ? Blob.Root.Find(Field.Name) : nullptr;
        if (Value && !Value->Items.empty())
            return true;
    }
    return false;
}
} // namespace

bool P4GameRowBlobChanges(AActor* Actor, const FP4GamePlacementRow& Row, const TArray<FP4ChangedProperty>& Changed,
    TMap<FString, TArray<uint8>>& Out, TArray<FString>& Left, FString& Error)
{
    Out.Reset();
    Left.Reset();
    Error.Reset();
    if (!IsInGameThread() || !IsValid(Actor))
    {
        Error = TEXT("A placement row is given new options on the editor thread, from an actor that is there");
        return false;
    }
    // What became of each changed property: whether a field of a blob holds it, and why it was not written when not.
    TArray<bool> Held;
    Held.Init(false, Changed.Num());
    TArray<FString> Why;
    Why.SetNum(Changed.Num());
    const auto Note = [&](int32 At, const FString& Reason)
    {
        Held[At] = true;
        if (Why[At].IsEmpty())
            Why[At] = Reason;
    };
    const P::Context Context = ContextOf(Actor);
    for (const P::SlotName& Slot : P::Slots())
    {
        const FString Key = SlotKey(Slot);
        UObject* Owner = OwnerOf(Actor, Slot);
        if (!Owner || NotAnOption(Key))
            continue;
        if (P::Layouts(Slot.S, Context).empty())
        {
            // A part whose blob Kadan cannot write: what the creator changed on it stays as the game has it.
            if (Owner != Actor)
                for (int32 Index = 0; Index < Changed.Num(); ++Index)
                    if (Changed[Index].Object == Owner)
                        Note(Index, FString::Printf(TEXT("Kadan cannot write the game's options of %s (%s) yet"),
                            *Owner->GetClass()->GetName(), *Key));
            continue;
        }
        // The game reads a row's blob at the row's own version, so a blob given anew is written at it.
        const uint64 Version = Slot.Static ? Row.AssetVersion : Row.GeneratorVersion;
        const TArray<uint8>* Before = Row.Blobs.Find(Key);
        TArray<int32> Here;
        if (!Before)
        {
            // The row has no such blob, so the game gives the object the class's values. The creator's values go
            // into a blob made whole from the actor, as for an object the creator placed.
            const std::vector<P::Field> Fields = LayoutFields(Slot.S, Context);
            for (const P::Field& Field : Fields)
            {
                FProperty* Leaf = nullptr;
                void* Data = nullptr;
                FString Miss;
                if (Field.Derived || NotAProperty(Field) ||
                    !ResolvePlacementField(Owner, Actor, Wide(Field.Name), Leaf, Data, Miss))
                    continue;
                const int32 At = ChangedAt(Changed, Data);
                if (At != INDEX_NONE)
                    Here.AddUnique(At);
            }
            if (!Here.Num())
                continue;
            for (const int32 At : Here)
                Held[At] = true;
            P::Blob Placed, Default;
            FString Miss;
            UObject* Archetype = Owner->GetArchetype();
            if (!Version || !Fill(Slot.S, Context, Owner, Actor, Placed, Miss, false, Version) || Placed.UnreadFields ||
                !Archetype || !Fill(Slot.S, Context, Archetype, Actor, Default, Miss, true, Version))
            {
                for (const int32 At : Here)
                    Note(At, Miss.IsEmpty() ? FString(TEXT("its blob cannot be written yet")) : Miss);
                continue;
            }
            if (Options(Slot.S, Context, Placed) == Options(Slot.S, Context, Default))
                continue;
            if (HasDrops(Placed, Fields))
            {
                for (const int32 At : Here)
                    Note(At, TEXT("drops of one of the game's objects are not written yet"));
                continue;
            }
            std::vector<uint8> Bytes;
            std::string Wrong;
            if (!P::Encode(Slot.S, Context, Placed, Version, Bytes, Wrong) || !Placed.Exact())
            {
                for (const int32 At : Here)
                    Note(At, TEXT("its blob cannot be written yet (") + Wide(Wrong) + TEXT(")"));
                continue;
            }
            Out.Add(Key, TArray<uint8>(Bytes.data(), int32(Bytes.size())));
            continue;
        }
        // The row's blob as the game has it, with each field that a changed property holds taken from the actor.
        // Every other field, the run-time state and the drops' numbers among them, stays the game's.
        P::Blob Blob;
        FString Unread;
        const bool Read = ReadRowBlob(Row, Key, Context, Blob, Unread);
        const std::vector<P::Field> Fields = LayoutFields(Slot.S, Context, Read ? Blob.Variant : 0);
        if (!Read)
        {
            // Which changed properties the blob would hold, to say why their change is not written.
            for (const P::Field& Field : Fields)
            {
                FProperty* Leaf = nullptr;
                void* Data = nullptr;
                FString Miss;
                if (Field.Derived || NotAProperty(Field) ||
                    !ResolvePlacementField(Owner, Actor, Wide(Field.Name), Leaf, Data, Miss))
                    continue;
                const int32 At = ChangedAt(Changed, Data);
                if (At != INDEX_NONE)
                    Note(At, TEXT("its blob in the game's row ") + Unread);
            }
            continue;
        }
        bool Touched = false;
        for (auto& Member : Blob.Root.Members)
        {
            const P::Field* Field = FieldNamed(Fields, Member.first);
            if (!Field || Field->Derived || NotAProperty(*Field))
                continue;
            FProperty* Leaf = nullptr;
            void* Data = nullptr;
            FString Miss;
            if (!ResolvePlacementField(Owner, Actor, Wide(Member.first), Leaf, Data, Miss))
                continue;
            const int32 At = ChangedAt(Changed, Data);
            if (At == INDEX_NONE)
                continue;
            Here.AddUnique(At);
            Held[At] = true;
            P::Value Now = Member.second;
            if (!ToValue(Leaf, Data, *Field->Of, Now, Miss))
            {
                Note(At, Miss);
                continue;
            }
            if (Canonical(Now, std::string()) == Canonical(Member.second, std::string()))
                continue;
            if (Field->Drops)
            {
                Note(At, TEXT("drops of one of the game's objects are not written yet"));
                continue;
            }
            Member.second = MoveTemp(Now);
            Touched = true;
        }
        if (!Touched)
            continue;
        if (!Blob.Exact())
        {
            for (const int32 At : Here)
                Note(At, TEXT("a part of its blob in the game's row is not read yet"));
            continue;
        }
        Recount(Blob, Fields, Owner);
        std::vector<uint8> Bytes;
        std::string Wrong;
        if (!P::Encode(Slot.S, Context, Blob, Version, Bytes, Wrong))
        {
            for (const int32 At : Here)
                Note(At, TEXT("its blob cannot be written (") + Wide(Wrong) + TEXT(")"));
            continue;
        }
        if (Bytes.size() == size_t(Before->Num()) &&
            (Bytes.empty() || !FMemory::Memcmp(Bytes.data(), Before->GetData(), Bytes.size())))
            continue;
        Out.Add(Key, TArray<uint8>(Bytes.data(), int32(Bytes.size())));
    }
    // A change no blob holds, or one a blob could not be written with, does not reach the game.
    for (int32 Index = 0; Index < Changed.Num(); ++Index)
    {
        if (Held[Index] && Why[Index].IsEmpty())
            continue;
        const FP4ChangedProperty& One = Changed[Index];
        const FString Name = (One.Object == Actor || !One.Object ? FString() : One.Object->GetName() + TEXT(".")) +
            (One.Property ? One.Property->GetName() : FString(TEXT("?")));
        Left.Add(Name + TEXT(": ") +
            (Held[Index] ? Why[Index]
                         : FString(TEXT("the game makes the object from its placement row, which does not hold this value"))));
    }
    return true;
}

void P4ReleasePlacementList(FP4PlacementList& List)
{
    for (TStrongObjectPtr<UObject>* Held : {&List.ListAsset, &List.Asset})
    {
        UObject* Asset = Held->Get();
        if (!Asset)
            continue;
        UPackage* Package = Asset->GetOutermost();
        Asset->ClearFlags(RF_Public | RF_Standalone);
        Asset->Rename(
            nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional | REN_ForceNoResetLoaders);
        Asset->MarkPendingKill();
        // The package goes under another name, so that the next export can make the list at the same path.
        if (Package && Package != GetTransientPackage())
        {
            Package->SetDirtyFlag(false);
            Package->Rename(
                *MakeUniqueObjectName(nullptr, UPackage::StaticClass(), TEXT("/Temp/P4ReleasedPlacementList"))
                    .ToString(),
                nullptr, REN_DontCreateRedirectors | REN_NonTransactional | REN_ForceNoResetLoaders);
            Package->MarkPendingKill();
        }
        Held->Reset();
    }
}

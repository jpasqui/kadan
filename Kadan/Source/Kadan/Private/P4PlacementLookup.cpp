// Placement export: names, native class chains, each slot's owner and layout, field paths, tagged components.
#include "P4PlacementInternal.h"

using namespace P4PlacementDetail;

namespace
{
// The native classes of a class, most derived first, by the names the game gives them.
std::vector<std::string> NativeChain(const UClass* Class)
{
    std::vector<std::string> Out;
    for (; Class; Class = Class->GetSuperClass())
        if (Class->HasAnyClassFlags(CLASS_Native))
            Out.push_back(Utf8(Class->GetName()));
    return Out;
}

UActorComponent* Named(AActor* Actor, const TCHAR* Property)
{
    const FObjectPropertyBase* Field = CastField<FObjectPropertyBase>(Actor->GetClass()->FindPropertyByName(Property));
    return Field ? Cast<UActorComponent>(Field->GetObjectPropertyValue_InContainer(Actor)) : nullptr;
}

UActorComponent* Having(AActor* Actor, const TCHAR* Native, const TCHAR* Not = nullptr)
{
    TInlineComponentArray<UActorComponent*> Components(Actor);
    for (UActorComponent* One : Components)
        if (One && Derives(One->GetClass(), Native) && !(Not && Derives(One->GetClass(), Not)))
            return One;
    return nullptr;
}

// The world location of each scene component with the tag, which moves with the actor.
std::vector<P::Value> TaggedLocations(AActor* Actor, const std::string& Tag)
{
    std::vector<P::Value> Out;
    TInlineComponentArray<USceneComponent*> Scene(Actor);
    for (USceneComponent* One : Scene)
        if (One && One->ComponentHasTag(FName(*Wide(Tag))))
        {
            const FVector At = One->GetComponentLocation();
            P::Value Point;
            Point.Is = P::Kind::Vec;
            Point.Words[0] = P::Value::FloatBits(At.X);
            Point.Words[1] = P::Value::FloatBits(At.Y);
            Point.Words[2] = P::Value::FloatBits(At.Z);
            Out.push_back(Point);
        }
    return Out;
}
} // namespace

namespace P4PlacementDetail
{
UClass* GameClass(const TCHAR* Name)
{
    return FindObject<UClass>(nullptr, *(FString(TEXT(P4_GAME_SCRIPT_PACKAGE ".")) + Name));
}

std::string Utf8(const FString& Text)
{
    return std::string(TCHAR_TO_UTF8(*Text));
}

FString Wide(const std::string& Text)
{
    return FString(UTF8_TO_TCHAR(Text.c_str()));
}

bool Derives(const UClass* Class, const TCHAR* Native)
{
    for (; Class; Class = Class->GetSuperClass())
        if (Class->HasAnyClassFlags(CLASS_Native) && Class->GetName() == Native)
            return true;
    return false;
}

// The object a slot's blob is read from, as the layout data says: the actor, or its component of the slot's
// owner classes that the owner property holds, or else the first one of them it has.
UObject* OwnerOf(AActor* Actor, const P::SlotName& Slot)
{
    if (Slot.OwnerClasses.empty())
        return Actor;
    const FString Except = Wide(Slot.OwnerExcept);
    const auto Fits = [&](const UActorComponent* Component)
    {
        for (const std::string& Class : Slot.OwnerClasses)
            if (Derives(Component->GetClass(), *Wide(Class)))
                return true;
        return false;
    };
    if (!Slot.OwnerProperty.empty())
        if (UActorComponent* Held = Named(Actor, *Wide(Slot.OwnerProperty)))
            if (Fits(Held))
                return Held;
    for (const std::string& Class : Slot.OwnerClasses)
        if (UActorComponent* Found = Having(Actor, *Wide(Class), Except.IsEmpty() ? nullptr : *Except))
            return Found;
    return nullptr;
}

// The component whose native classes choose the layouts of the slots that follow a component's chain.
UActorComponent* ChainComponentOf(AActor* Actor)
{
    for (const P::SlotName& Slot : P::Slots())
        if (Slot.Chain == 'c')
            return Cast<UActorComponent>(OwnerOf(Actor, Slot));
    return nullptr;
}

// A field the creator's values do not set: one no property holds, or a count of the actor's.
bool NotAProperty(const P::Field& Field)
{
    return Field.Derived || !Field.CountOf.empty();
}

// The fields of a slot's layout for an actor, in the variant a blob was read in.
std::vector<P::Field> LayoutFields(P::Slot Slot, const P::Context& Context, int Variant)
{
    const std::vector<P::Layout> Layouts = P::Layouts(Slot, Context);
    if (Variant < 0 || size_t(Variant) >= Layouts.size())
        return {};
    return Layouts[size_t(Variant)].Fields;
}

const P::Field* FieldNamed(const std::vector<P::Field>& Fields, const std::string& Name)
{
    for (const P::Field& Field : Fields)
        if (Field.Name == Name)
            return &Field;
    return nullptr;
}

// The property a field's path names on an object: its members joined by dots. The first part may name
// the object's own class (GActor.DemoBindName) or a struct's type rather than the property holding it
// (OtakaraAIParameter is the type of OtakaraAIComponent.AIParameter).
bool Resolve(UObject* Object, const FString& Path, FProperty*& Leaf, void*& Data, FString& Error)
{
    TArray<FString> Parts;
    Path.ParseIntoArray(Parts, TEXT("."), true);
    UStruct* Type = Object->GetClass();
    void* Container = Object;
    int32 At = 0;
    FProperty* First = Parts.Num() ? Type->FindPropertyByName(FName(*Parts[0])) : nullptr;
    if (!First && Parts.Num() > 1)
    {
        if (Derives(Object->GetClass(), *Parts[0]))
            At = 1;
        else
            for (TFieldIterator<FStructProperty> It(Object->GetClass()); It; ++It)
                if (It->Struct->GetName() == Parts[0])
                {
                    First = *It;
                    break;
                }
    }
    for (; At < Parts.Num(); ++At)
    {
        FProperty* Field = (At == 0 && First) ? First : Type->FindPropertyByName(FName(*Parts[At]));
        if (!Field)
        {
            Error = FString::Printf(TEXT("%s has no %s"), *Object->GetClass()->GetName(), *Path);
            return false;
        }
        void* Value = Field->ContainerPtrToValuePtr<void>(Container);
        if (At == Parts.Num() - 1)
        {
            Leaf = Field;
            Data = Value;
            return true;
        }
        const FStructProperty* Struct = CastField<FStructProperty>(Field);
        if (!Struct)
        {
            Error = FString::Printf(TEXT("%s of %s is not a struct"), *Parts[At], *Path);
            return false;
        }
        Type = Struct->Struct;
        Container = Value;
    }
    Error = TEXT("An empty field path");
    return false;
}

// A path under PlacedActor names a component property of the placed actor, then a field of that component.
bool ResolvePlacementField(
    UObject* Owner, AActor* Actor, const FString& Path, FProperty*& Leaf, void*& Data, FString& Error)
{
    const FString Prefix = TEXT("PlacedActor.");
    if (!Path.StartsWith(Prefix, ESearchCase::CaseSensitive))
        return Resolve(Owner, Path, Leaf, Data, Error);
    FString Property, Rest;
    if (!Path.Mid(Prefix.Len()).Split(TEXT("."), &Property, &Rest))
    {
        Error = Path + TEXT(" names no field of a component");
        return false;
    }
    UActorComponent* Component = IsValid(Actor) ? Named(Actor, *Property) : nullptr;
    if (!Component)
    {
        Error = FString::Printf(TEXT("The placed actor has no %s for its serialized option."), *Property);
        return false;
    }
    return Resolve(Component, Rest, Leaf, Data, Error);
}

int32 CountOf(UObject* Owner, const TCHAR* Path)
{
    FProperty* Leaf = nullptr;
    void* Data = nullptr;
    FString Ignored;
    const FArrayProperty* Array = Resolve(Owner, Path, Leaf, Data, Ignored) ? CastField<FArrayProperty>(Leaf) : nullptr;
    return Array ? FScriptArrayHelper(Array, Data).Num() : 0;
}

USceneComponent* SceneComponentNamed(AActor* Actor, const FName Name)
{
    TInlineComponentArray<USceneComponent*> Components(Actor);
    for (USceneComponent* Component : Components)
        if (Component && Component->GetFName() == Name)
            return Component;
    return nullptr;
}

// The world locations of the actor's first Count scene components with the tag, in its component order, as the
// game serializes them. False when the actor has fewer: the game would write fewer than its counter says.
bool TaggedRun(AActor* Actor, const std::string& Tag, uint64 Count, std::vector<P::Value>& Out)
{
    Out = TaggedLocations(Actor, Tag);
    if (Out.size() < Count)
        return false;
    Out.resize(size_t(Count));
    return true;
}

// The context a gameplay object's blobs are read in: its native classes and its AI component's.
P::Context ContextOf(AActor* Actor)
{
    P::Context Context;
    Context.ActorChain = NativeChain(Actor->GetClass());
    if (UActorComponent* Component = ChainComponentOf(Actor))
        Context.ComponentChain = NativeChain(Component->GetClass());
    TInlineComponentArray<UActorComponent*> Components(Actor);
    for (const UActorComponent* Component : Components)
        if (Component)
            Context.Components.push_back(Utf8(Component->GetName()));
    return Context;
}

FString SlotKey(const P::SlotName& Slot)
{
    return FString(Slot.Row) + (Slot.Static ? TEXT(".Static") : TEXT(".Dynamic"));
}

const P::SlotName* SlotNamed(const FString& Key)
{
    for (const P::SlotName& Slot : P::Slots())
        if (Key == SlotKey(Slot))
            return &Slot;
    return nullptr;
}

// A blob of the game's row read for the actor. False with Why when it is not read (no layout, or the bytes
// do not fit it); a blob read in part has a Tail.
bool ReadRowBlob(
    const FP4GamePlacementRow& Row, const FString& Key, const P::Context& Context, P::Blob& Out, FString& Why)
{
    const P::SlotName* Slot = SlotNamed(Key);
    const TArray<uint8>* Bytes = Row.Blobs.Find(Key);
    if (!Slot || !Bytes || P::Layouts(Slot->S, Context).empty())
    {
        Why = TEXT("not read yet");
        return false;
    }
    std::string Error;
    if (!P::Decode(Slot->S, Context, std::vector<uint8>(Bytes->GetData(), Bytes->GetData() + Bytes->Num()),
            Slot->Static ? Row.AssetVersion : Row.GeneratorVersion, Out, Error))
    {
        Why = TEXT("does not fit its layout (") + Wide(Error) + TEXT(")");
        return false;
    }
    return true;
}

// Blobs that are not options: the class's hash, which the game does not read.
bool NotAnOption(const FString& Key)
{
    return Key.StartsWith(TEXT("Hash.")) || Key.StartsWith(TEXT("CheckComp"));
}

uint64 PlacementNumber(const AActor* Actor)
{
    const FStructProperty* Info = CastField<FStructProperty>(
        Actor->GetClass()->FindPropertyByName(TEXT("GenerateInfo")));
    const FProperty* Id = Info ? Info->Struct->FindPropertyByName(TEXT("DebugUniqueId")) : nullptr;
    const FNumericProperty* Number = Id ? Numeric(Id) : nullptr;
    if (!Number || !Number->IsInteger())
        return 0;
    return Number->GetUnsignedIntPropertyValue(
        Id->ContainerPtrToValuePtr<void>(Info->ContainerPtrToValuePtr<void>(const_cast<AActor*>(Actor))));
}

FString LevelLeaf(const AActor* Actor)
{
    const ULevel* Level = Actor->GetLevel();
    return Level ? FPackageName::GetLongPackageAssetName(Level->GetOutermost()->GetName()) : FString();
}

FString ActorKey(const AActor* Actor)
{
    return Actor->GetOutermost()->GetName() + TEXT(":") + Actor->GetName();
}
} // namespace P4PlacementDetail

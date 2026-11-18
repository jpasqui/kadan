// Fillers of placement row blobs (P4PlacementFillers.h): places and nav links.
#include "P4PlacementFillers.h"
#include "AI/Navigation/NavLinkDefinition.h"
#include "Components/BoxComponent.h"
#include "NavLinkComponent.h"

namespace P4PlacementDetail
{
namespace
{
void SetVec(P::Value& Out, const FVector& V)
{
    for (int32 I = 0; I < 3; ++I)
        Out.Words[I] = P::Value::FloatBits(V[I]);
}

void SetQuat(P::Value& Out, const FQuat& Q)
{
    const float Words[] = {Q.X, Q.Y, Q.Z, Q.W};
    for (int32 I = 0; I < 4; ++I)
        Out.Words[I] = P::Value::FloatBits(Words[I]);
}

USceneComponent* SceneOf(UActorComponent* Component)
{
    return Cast<USceneComponent>(Component);
}

FString Wanted(const P::Field& Field)
{
    FString Out = Wide(Field.FillClass);
    if (!Field.FillTag.empty())
        Out += (Out.IsEmpty() ? TEXT("") : TEXT(" ")) + FString(TEXT("tagged ")) + Wide(Field.FillTag);
    if (!Field.FillName.empty())
        Out += (Out.IsEmpty() ? TEXT("") : TEXT(" ")) + FString(TEXT("named ")) + Wide(Field.FillName);
    return Out.IsEmpty() ? FString(TEXT("component")) : Out;
}

// The first of the actor's matching components (AActor::GetComponentByClass, or the first of GetComponents), or the
// last (a loop over GetComponents that keeps the last match), as a scene component.
USceneComponent* SceneComponentFor(AActor* Actor, const P::Field& Field, bool Last, FString& Error)
{
    const TArray<UActorComponent*> Found = ComponentsFor(Actor, Field);
    if (Found.Num() == 0)
    {
        Error = FString::Printf(TEXT("the actor has no %s"), *Wanted(Field));
        return nullptr;
    }
    USceneComponent* Scene = SceneOf(Last ? Found.Last() : Found[0]);
    if (!Scene)
        Error = FString::Printf(TEXT("the actor's %s is not a scene component"), *Wanted(Field));
    return Scene;
}

// One of a nav link component's links, as the game's serializer writes it: the members the item type names, in its
// order (NavLink: all of them; NavLinkEnds: the two ends, the direction and the snap height flag). SupportsAgentBits
// is the link's deprecated agent bits (FNavigationLinkBase::SupportedAgentsBits), which is what the game writes.
bool LinkValue(const FNavigationLink& Link, const P::Type& Item, P::Value& Out, FString& Error)
{
    Out = P::EmptyOf(Item, P::RowVersion);
    for (auto& Member : Out.Members)
    {
        const std::string& Name = Member.first;
        P::Value& V = Member.second;
        if (Name == "Left")
            SetVec(V, Link.Left);
        else if (Name == "Right")
            SetVec(V, Link.Right);
        else if (Name == "LeftProjectHeight")
            V.SetFloat(Link.LeftProjectHeight);
        else if (Name == "MaxFallDownLength")
            V.SetFloat(Link.MaxFallDownLength);
        else if (Name == "Direction")
            V.Bits = uint8(Link.Direction.GetValue());
        else if (Name == "SnapRadius")
            V.SetFloat(Link.SnapRadius);
        else if (Name == "SnapHeight")
            V.SetFloat(Link.SnapHeight);
        else if (Name == "SupportsAgentBits")
            V.Bits = Link.SupportedAgentsBits;
        else if (Name == "bUseSnapHeight")
            V.Bits = Link.bUseSnapHeight ? 1 : 0;
        else if (Name == "bSnapToCheapestArea")
            V.Bits = Link.bSnapToCheapestArea ? 1 : 0;
        else
        {
            Error = Wide(Name) + TEXT(" is not a member of a nav link");
            return false;
        }
    }
    return true;
}

bool LinksValue(const UNavLinkComponent* Component, const P::Type& Array, P::Value& Out, FString& Error)
{
    if (!Array.Item)
    {
        Error = TEXT("not an array of links");
        return false;
    }
    Out.Items.clear();
    for (const FNavigationLink& Link : Component->Links)
    {
        P::Value Item;
        if (!LinkValue(Link, *Array.Item, Item, Error))
            return false;
        Out.Items.push_back(std::move(Item));
    }
    return true;
}

// world_location: the world location of the actor's first component of the class / tag / name (the game reads its
// component-to-world translation); the field is not there when the actor has none.
bool WorldLocation(AActor* Actor, UObject*, const P::Field& Field, P::Value& Out, FString& Error)
{
    USceneComponent* Scene = SceneComponentFor(Actor, Field, false, Error);
    if (!Scene)
        return false;
    SetVec(Out, Scene->GetComponentLocation());
    return true;
}

// world_location_or_zero: the same, and the zero vector when the actor has no such component (a fence without its
// sound emitter).
bool WorldLocationOrZero(AActor* Actor, UObject*, const P::Field& Field, P::Value& Out, FString& Error)
{
    const TArray<UActorComponent*> Found = ComponentsFor(Actor, Field);
    if (Found.Num() == 0)
    {
        SetVec(Out, FVector::ZeroVector);
        return true;
    }
    USceneComponent* Scene = SceneOf(Found[0]);
    if (!Scene)
    {
        Error = FString::Printf(TEXT("the actor's %s is not a scene component"), *Wanted(Field));
        return false;
    }
    SetVec(Out, Scene->GetComponentLocation());
    return true;
}

// relative_location: the relative location of the last of the actor's components of the class / tag / name.
bool RelativeLocation(AActor* Actor, UObject*, const P::Field& Field, P::Value& Out, FString& Error)
{
    USceneComponent* Scene = SceneComponentFor(Actor, Field, true, Error);
    if (!Scene)
        return false;
    SetVec(Out, Scene->GetRelativeLocation());
    return true;
}

// nav_links: the links of the actor's first nav link component (of the class / tag / name), as stored: the points are
// the component's own, not moved into the world.
bool NavLinks(AActor* Actor, UObject*, const P::Field& Field, P::Value& Out, FString& Error)
{
    const TArray<UActorComponent*> Found = ComponentsFor(Actor, Field);
    if (Found.Num() == 0)
    {
        Error = FString::Printf(TEXT("the actor has no %s"), *Wanted(Field));
        return false;
    }
    const UNavLinkComponent* Links = Cast<UNavLinkComponent>(Found[0]);
    if (!Links)
    {
        Error = FString::Printf(TEXT("the actor's %s is not a nav link component"), *Wanted(Field));
        return false;
    }
    return LinksValue(Links, *Field.Of, Out, Error);
}

// nav_link_place: the last of the actor's components with the tag, a nav link component: its world location, then its
// links (the item type's members of each).
bool NavLinkPlace(AActor* Actor, UObject*, const P::Field& Field, P::Value& Out, FString& Error)
{
    USceneComponent* Scene = SceneComponentFor(Actor, Field, true, Error);
    if (!Scene)
        return false;
    const UNavLinkComponent* Links = Cast<UNavLinkComponent>(Scene);
    P::Value* Location = Out.Find("Location");
    P::Value* Items = Out.Find("Links");
    const P::Type* ItemsType = nullptr;
    for (const auto& Member : Field.Of->Members)
        if (Member.Name == "Links")
            ItemsType = Member.Of;
    if (!Links || !Location || !Items || !ItemsType)
    {
        Error = !Links ? FString::Printf(TEXT("the actor's %s is not a nav link component"), *Wanted(Field))
                       : FString(TEXT("not a place with links"));
        return false;
    }
    SetVec(*Location, Scene->GetComponentLocation());
    return LinksValue(Links, *ItemsType, *Items, Error);
}

// box_relative: the first of the actor's box components of the class / tag / name: its relative transform (rotation,
// location, scale) and its extent scaled by its world scale (UBoxComponent::GetScaledBoxExtent).
bool BoxRelative(AActor* Actor, UObject*, const P::Field& Field, P::Value& Out, FString& Error)
{
    USceneComponent* Scene = SceneComponentFor(Actor, Field, false, Error);
    if (!Scene)
        return false;
    const UBoxComponent* Box = Cast<UBoxComponent>(Scene);
    P::Value* Rotation = Out.Find("Rotation");
    P::Value* Translation = Out.Find("Translation");
    P::Value* Scale = Out.Find("Scale3D");
    P::Value* Extent = Out.Find("BoxExtent");
    if (!Box || !Rotation || !Translation || !Scale || !Extent)
    {
        Error = !Box ? FString::Printf(TEXT("the actor's %s is not a box component"), *Wanted(Field))
                     : FString(TEXT("not a box"));
        return false;
    }
    const FTransform Relative = Box->GetRelativeTransform();
    SetQuat(*Rotation, Relative.GetRotation());
    SetVec(*Translation, Relative.GetTranslation());
    SetVec(*Scale, Relative.GetScale3D());
    SetVec(*Extent, Box->GetScaledBoxExtent());
    return true;
}

const FFillerKind Kinds[] = {
    {TEXT("world_location"), &WorldLocation},
    {TEXT("world_location_or_zero"), &WorldLocationOrZero},
    {TEXT("relative_location"), &RelativeLocation},
    {TEXT("nav_links"), &NavLinks},
    {TEXT("nav_link_place"), &NavLinkPlace},
    {TEXT("box_relative"), &BoxRelative},
};
} // namespace

TArrayView<const FFillerKind> NavFillers()
{
    return MakeArrayView(Kinds);
}
} // namespace P4PlacementDetail

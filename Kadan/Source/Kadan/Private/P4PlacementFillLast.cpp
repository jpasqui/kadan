// Fillers of placement row blobs (P4PlacementFillers.h): a nav link component's link ends.
#include "P4PlacementFillers.h"

namespace P4PlacementDetail
{
namespace
{
// A vector member of a struct, as the stream's three float words.
bool LinkEndVector(const UScriptStruct* Struct, const void* Data, const TCHAR* Name, P::Value& Out)
{
    const FStructProperty* Field = CastField<FStructProperty>(Struct->FindPropertyByName(Name));
    if (!Field || Field->Struct != TBaseStructure<FVector>::Get())
        return false;
    const FVector& V = *Field->ContainerPtrToValuePtr<FVector>(Data);
    for (int32 I = 0; I < 3; ++I)
        Out.Words[I] = P::Value::FloatBits(V[I]);
    return true;
}

// BridgeFlexibleAIComponent's static serializer (0x018085c0), above 0x202301069, finds the actor's first
// NavLinkComponent (AActor::FindComponentByClass) and, when there is one, writes its Links (FUN_018088b0): the count,
// then each link's Left and Right as the component holds them (FNavigationLink, in the component's space).
bool FillNavLinkEnds(AActor* Actor, UObject*, const P::Field& Field, P::Value& Out, FString& Error)
{
    const TArray<UActorComponent*> Found = ComponentsFor(Actor, Field);
    if (!Found.Num())
    {
        Error = FString::Printf(TEXT("the actor has no %s"), *Wide(Field.FillClass));
        return false;
    }
    const FArrayProperty* Links = CastField<FArrayProperty>(Found[0]->GetClass()->FindPropertyByName(TEXT("Links")));
    const FStructProperty* Link = Links ? CastField<FStructProperty>(Links->Inner) : nullptr;
    if (!Link || !Field.Of || !Field.Of->Item)
    {
        Error = TEXT("the component has no Links of nav links");
        return false;
    }
    FScriptArrayHelper Items(Links, Links->ContainerPtrToValuePtr<void>(Found[0]));
    Out.Items.clear();
    for (int32 I = 0; I < Items.Num(); ++I)
    {
        P::Value Item = P::EmptyOf(*Field.Of->Item, P::RowVersion);
        P::Value* Left = Item.Find("Left");
        P::Value* Right = Item.Find("Right");
        if (!Left || !Right || !LinkEndVector(Link->Struct, Items.GetRawPtr(I), TEXT("Left"), *Left) ||
            !LinkEndVector(Link->Struct, Items.GetRawPtr(I), TEXT("Right"), *Right))
        {
            Error = TEXT("a nav link has no Left and Right vectors");
            return false;
        }
        Out.Items.push_back(std::move(Item));
    }
    return true;
}

const FFillerKind LastKinds[] = {
    {TEXT("nav_link_ends"), &FillNavLinkEnds},
};
} // namespace

TArrayView<const FFillerKind> LastFillers()
{
    return MakeArrayView(LastKinds);
}
} // namespace P4PlacementDetail

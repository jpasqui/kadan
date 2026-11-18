// The fillers' list and the lookups they share.
#include "P4PlacementFillers.h"

namespace P4PlacementDetail
{
TArrayView<const FFillerKind> FillerKinds()
{
    static TArray<FFillerKind> All = []
    {
        TArray<FFillerKind> Out;
        Out.Append(NavFillers().GetData(), NavFillers().Num());
        Out.Append(ShapeFillers().GetData(), ShapeFillers().Num());
        return Out;
    }();
    return All;
}

bool FillFromActor(AActor* Actor, UObject* Owner, const P::Field& Field, P::Value& Out, FString& Error)
{
    const FString Kind = Wide(Field.Fill);
    for (const FFillerKind& One : FillerKinds())
        if (Kind == One.Kind)
        {
            if (One.Fill(Actor, Owner, Field, Out, Error))
                return true;
            Error = Wide(Field.Name) + TEXT(": ") + Error;
            return false;
        }
    Error = FString::Printf(TEXT("%s: Kadan has no filler %s"), *Wide(Field.Name), *Kind);
    return false;
}

TArray<UActorComponent*> ComponentsFor(AActor* Actor, const P::Field& Field)
{
    TArray<UActorComponent*> Out;
    if (!Actor)
        return Out;
    const FString Class = Wide(Field.FillClass), Tag = Wide(Field.FillTag), Name = Wide(Field.FillName);
    TInlineComponentArray<UActorComponent*> Parts(Actor);
    for (UActorComponent* Part : Parts)
    {
        if (!Part)
            continue;
        bool Fits = true;
        if (!Class.IsEmpty())
        {
            Fits = false;
            for (const UClass* Of = Part->GetClass(); Of && !Fits; Of = Of->GetSuperClass())
                Fits = Of->GetName() == Class || Of->GetName() == Class + TEXT("_C");
        }
        if (Fits && !Tag.IsEmpty())
            Fits = Part->ComponentHasTag(FName(*Tag));
        if (Fits && !Name.IsEmpty())
            Fits = Part->GetName() == Name;
        if (Fits)
            Out.Add(Part);
    }
    return Out;
}
} // namespace P4PlacementDetail

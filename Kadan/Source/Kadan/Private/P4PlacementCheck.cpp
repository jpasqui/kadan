// Placement export: where a gameplay object's values differ from its row in the game's lists.
#include "P4PlacementInternal.h"

using namespace P4PlacementDetail;

TArray<FString> P4PlacementOptionDifferences(AActor* Actor, const FP4GamePlacementRow& Row)
{
    TArray<FString> Out;
    if (!IsInGameThread() || !IsValid(Actor))
    {
        Out.Add(TEXT("no actor"));
        return Out;
    }
    const P::Context Context = ContextOf(Actor);
    TArray<FString> Keys;
    Row.Blobs.GetKeys(Keys);
    Keys.Sort();
    for (const FString& Key : Keys)
    {
        if (NotAnOption(Key))
            continue;
        P::Blob Blob;
        FString Why;
        if (!ReadRowBlob(Row, Key, Context, Blob, Why))
        {
            Out.Add(Key + TEXT(" ") + Why);
            continue;
        }
        if (!Blob.Exact())
            Out.Add(Key + TEXT(" is read in part only"));
        const P::SlotName* Slot = SlotNamed(Key);
        UObject* Owner = OwnerOf(Actor, *Slot);
        if (!Owner)
        {
            Out.Add(Key + TEXT(": the actor has no component for it"));
            continue;
        }
        const std::vector<P::Field> Fields = LayoutFields(Slot->S, Context, Blob.Variant);
        for (const auto& Member : Blob.Root.Members)
        {
            const std::string& Name = Member.first;
            const FString Field = Wide(Name);
            const P::Field* Found = FieldNamed(Fields, Name);
            const bool Known = Found != nullptr;
            const P::Field Layout = Known ? *Found : P::Field();
            // What the stream counts is compared with what the actor has to count.
            const auto Counted = [&](const FString& Counter, const P::Value& Said, int64 Has)
            {
                if (uint64(Has) != Said.Bits)
                    Out.Add(FString::Printf(
                        TEXT("%s %s: the actor has %lld, the game's row %llu"), *Key, *Counter, Has, Said.Bits));
            };
            // The row's run of tagged locations is compared with the actor's first components of the tag, as many as
            // the row's counter says. The counter itself is a property, compared below.
            if (Known && !Layout.ComponentTag.empty())
            {
                P::Value Points;
                Points.Is = P::Kind::Repeat;
                if (!TaggedRun(Actor, Layout.ComponentTag, Member.second.Items.size(), Points.Items))
                    Out.Add(FString::Printf(TEXT("%s %s: the game's row has %d, the actor fewer components tagged %s"),
                        *Key, *Field, int32(Member.second.Items.size()), *Wide(Layout.ComponentTag)));
                else if (Canonical(Points, Name) != Canonical(Member.second, Name))
                    Out.Add(Key + TEXT(" ") + Field + TEXT(": the actor's tagged components are elsewhere"));
            }
            const bool Scalar = Known && Layout.Of->Is != P::Kind::Bools && Layout.Of->Is != P::Kind::Repeat;
            if (Scalar && !Layout.CountOf.empty())
                Counted(Field, Member.second, CountOf(Owner, *Wide(Layout.CountOf)));
            if (Known && NotAProperty(Layout))
                continue;
            const P::Type* Type = Known ? Layout.Of : nullptr;
            FProperty* Leaf = nullptr;
            void* Data = nullptr;
            FString Error;
            P::Value Now;
            if (!Type || !ResolvePlacementField(Owner, Actor, Field, Leaf, Data, Error) ||
                !ToValue(Leaf, Data, *Type, Now, Error))
            {
                Out.Add(
                    Key + TEXT(" ") + Field + TEXT(": ") + (Error.IsEmpty() ? FString(TEXT("no such field")) : Error));
                continue;
            }
            const std::vector<std::string> Mine = Canonical(Now, Name), Game = Canonical(Member.second, Name);
            if (Mine == Game)
                continue;
            std::string First;
            for (std::size_t I = 0; I < std::max(Mine.size(), Game.size()) && First.empty(); ++I)
            {
                const std::string A = I < Mine.size() ? Mine[I] : "-", B = I < Game.size() ? Game[I] : "-";
                if (A != B)
                    First = "the actor has " + A + ", the game's row " + B;
            }
            Out.Add(Key + TEXT(" ") + Wide(First));
        }
    }
    return Out;
}

#include "P4DroppedVisibilityIds.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"

void P4WithDroppedVisibilityIds(UWorld* World, const TArray<FString>& Imported, TArray<FString>& Current)
{
    if (!World)
        return;
    const FString Mark = TEXT(" | visibilityid = ");
    const auto* Native = FindFProperty<FIntProperty>(UPrimitiveComponent::StaticClass(), TEXT("VisibilityId"));
    if (!Native)
        return;
    bool Changed = false;
    for (const FString& Line : Imported)
    {
        const int32 At = Line.Find(Mark, ESearchCase::CaseSensitive);
        if (At == INDEX_NONE)
            continue;
        const FString Token = Line.Mid(At + Mark.Len());
        int32 Original = INDEX_NONE;
        if (!LexTryParseString(Original, *Token) || Original < 0 || FString::FromInt(Original) != Token)
            continue;
        const FString Path = Line.Left(At);
        auto* Component = Cast<UPrimitiveComponent>(
            StaticFindObject(UObject::StaticClass(), World->GetOutermost(), *Path));
        // A component of a removed actor is no longer in the level, whatever its id.
        if (!IsValid(Component) || (Component->GetOwner() && !IsValid(Component->GetOwner())) ||
            Component->VisibilityId != INDEX_NONE ||
            Component->GetClass()->FindPropertyByName(TEXT("VisibilityId")) != Native)
            continue;
        const FString Prefix = Path + Mark;
        if (Current.ContainsByPredicate(
                [&](const FString& Other) { return Other.StartsWith(Prefix, ESearchCase::CaseSensitive); }))
            continue;
        // Editor load/construction can invalidate precomputed visibility without
        // changing placement or authored properties. Compare that missing cache
        // ID against the caller's validated imported lines. Never rewrite the
        // component, saved source, baseline, or any transform/material line.
        Current.Add(Line);
        Changed = true;
    }
    if (Changed)
        Current.Sort([](const FString& A, const FString& B) { return A.Compare(B, ESearchCase::CaseSensitive) < 0; });
}

void P4WithImportedStreamingTextureData(const TArray<FString>& Imported, TArray<FString>& Current)
{
    const FString Mark = TEXT(" | streamingtexturedata = ");
    const auto Built = [&](const FString& Line) { return Line.Contains(Mark, ESearchCase::CaseSensitive); };
    const int32 Removed = Current.RemoveAll(Built);
    // The objects still in the level: every one has a class line.
    TSet<FString> Present;
    for (const FString& Line : Current)
    {
        const int32 At = Line.Find(TEXT(" | "), ESearchCase::CaseSensitive);
        if (At != INDEX_NONE)
            Present.Add(Line.Left(At));
    }
    int32 Added = 0;
    for (const FString& Line : Imported)
        if (Built(Line) && Present.Contains(Line.Left(Line.Find(Mark, ESearchCase::CaseSensitive))))
        {
            Current.Add(Line);
            ++Added;
        }
    if (Removed || Added)
        Current.Sort([](const FString& A, const FString& B) { return A.Compare(B, ESearchCase::CaseSensitive) < 0; });
}

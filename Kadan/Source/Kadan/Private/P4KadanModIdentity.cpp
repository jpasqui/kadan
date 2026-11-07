#include "P4KadanModIdentity.h"
#include "P4MapKitCore/export_contract.h"

FString P4KadanSuggestedModId(const FString& Name)
{
    FString Id;
    bool Separator = false;
    for (TCHAR Character : Name)
    {
        if (Character >= TCHAR('A') && Character <= TCHAR('Z'))
        {
            Character += TCHAR('a') - TCHAR('A');
        }
        const bool Alphanumeric = (Character >= TCHAR('a') && Character <= TCHAR('z')) ||
            (Character >= TCHAR('0') && Character <= TCHAR('9'));
        if (!Alphanumeric)
        {
            Separator = !Id.IsEmpty();
            continue;
        }
        if (Separator)
        {
            Id.AppendChar(TCHAR('_'));
        }
        Id.AppendChar(Character);
        Separator = false;
        if (Id.Len() >= 47)
        {
            Id.LeftInline(47);
            break;
        }
    }
    Id.RemoveFromEnd(TEXT("_"));
    // A name that gives Tsuchi's own ID suggests a mod's ID beside it.
    if (P4MapKit::Export::ReservedId(TCHAR_TO_UTF8(*Id)))
        Id += TEXT("_mod");
    return Id;
}

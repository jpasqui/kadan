#include "P4KadanPathPicker.h"
#include "CoreMinimal.h"
#include "DesktopPlatformModule.h"

#if P4_KADAN_TEST_SEAMS
namespace
{
bool UnavailableForTest = false;
}

void P4KadanUnavailablePathPickerForTest(bool Unavailable)
{
    check(IsInGameThread());
    UnavailableForTest = Unavailable;
}
#endif

IDesktopPlatform* P4KadanPathPicker()
{
#if P4_KADAN_TEST_SEAMS
    if (UnavailableForTest)
    {
        return nullptr;
    }
#endif
    return FDesktopPlatformModule::Get();
}

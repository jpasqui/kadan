#include "P4BlueprintSubobjectStorage.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "UObject/UObjectGlobals.h"

bool P4BlueprintSourceSubobjectClassMatches(const UClass* Actual, const UClass* Declared)
{
    if (!Actual || !Declared) return false;
    if (Actual == Declared) return true;
    // Only exact registered storage children retain the original engine pointer
    // type. No generated-SDK dependency or arbitrary subclass admission.
    const TCHAR* Path = Declared == UDirectionalLightComponent::StaticClass() ?
        TEXT("/Script/P4StubsCore.P4RecoveredDirectionalLightComponent") :
        Declared == UPostProcessComponent::StaticClass() ? TEXT("/Script/P4StubsCore.P4RecoveredPostProcessComponent") : nullptr;
    if (!Path || Actual->GetSuperClass() != Declared ||
        !Actual->HasAnyClassFlags(CLASS_Native) || Actual->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) return false;
    return Actual->GetPathName() == Path && FindObject<UClass>(nullptr, Path) == Actual;
}

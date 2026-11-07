#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "Misc/AutomationTest.h"
#include "UObject/Class.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ParticleEngineDeclaration, "P4MapKit.Quality.ParticleEngineDeclaration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ParticleEngineDeclaration::RunTest(const FString& Parameters)
{
    (void)Parameters;
    UClass* Class = FindObject<UClass>(nullptr, TEXT("/Script/Engine.ParticleModuleC4GPUSubUV"));
    UClass* Parent = FindObject<UClass>(nullptr, TEXT("/Script/Engine.ParticleModuleSubUVBase"));
    if (!TestNotNull(TEXT("Game particle declaration is registered"), Class) ||
        !TestNotNull(TEXT("Native editor parent is registered"), Parent))
        return false;
    TestTrue(TEXT("Declaration retains the captured parent"), Class->GetSuperClass() == Parent);
    TestEqual(
        TEXT("Declaration adds no uncaptured native storage"), Class->GetPropertiesSize(), Parent->GetPropertiesSize());
    int32 OwnFields = 0;
    for (TFieldIterator<FProperty> Field(Class, EFieldIteratorFlags::ExcludeSuper); Field; ++Field)
        ++OwnFields;
    TestEqual(TEXT("Declaration adds no uncaptured reflected fields"), OwnFields, 0);
    TestEqual(TEXT("Declaration is explicitly marked without game behavior"),
        FString(Class->GetOutermost()->GetMetaData()->GetValue(Class, TEXT("P4DeclarationOnly"))), FString(TEXT("1")));
    UObject* Object = NewObject<UObject>(GetTransientPackage(), Class);
    TestNotNull(TEXT("Inherited constructor produces a native object"), Object);
    if (Object)
        TestTrue(TEXT("The object retains its declared class and engine ancestry"),
            Object->GetClass() == Class && Object->IsA(Parent));
    return !HasAnyErrors();
}
#endif

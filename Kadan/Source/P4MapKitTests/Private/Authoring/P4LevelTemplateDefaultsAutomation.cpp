#include "P4LevelContent.h"
#include "P4KadanTestSeams.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4LevelTemplateDefaultsTest, "P4MapKit.NativeImport.DeclaredSubobjectDefaults",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4LevelTemplateDefaultsTest::RunTest(const FString&)
{
    P4_REQUIRE_KADAN_TEST_SEAMS();
    UClass* SpeedClass = LoadObject<UClass>(nullptr, TEXT("/Script/Carrot4.CakMoveSpeedControlUnit"));
    UClass* SinkClass = LoadObject<UClass>(nullptr, TEXT("/Script/Carrot4.CakSinkRatioControlUnit"));
    UClass* HandlerClass = LoadObject<UClass>(nullptr, TEXT("/Script/Carrot4.CakEventHandlerComponent"));
    if (!TestTrue(TEXT("The genuine native handler/unit fields exist"), SpeedClass && SinkClass && HandlerClass)) return false;
    TStrongObjectPtr<UObject> SpeedTemplate(NewObject<UObject>(GetTransientPackage(), SpeedClass));
    TStrongObjectPtr<UObject> SpeedInstance(NewObject<UObject>(GetTransientPackage(), SpeedClass));
    FBoolProperty* Speed = FindFProperty<FBoolProperty>(SpeedClass, TEXT("bSendRTPCMoveSpeedVerticalValue"));
    if (!TestNotNull(TEXT("The original bool is reflected"), Speed)) return false;
    Speed->SetPropertyValue_InContainer(SpeedTemplate.Get(), true);
    Speed->SetPropertyValue_InContainer(SpeedInstance.Get(), false);
    TestTrue(TEXT("The constructor found a native fallback rather than the original export template"), SpeedInstance->GetArchetype() != SpeedTemplate.Get());
    FString Error;
    TestTrue(TEXT("An empty source payload inherits its declared original template"),
        P4ApplyDeclaredLevelDefaultsForTest(SpeedInstance.Get(), SpeedTemplate.Get(), FString(), false, Error));
    TestTrue(TEXT("The source-absent bool inherits True, not the constructor's False"), Speed->GetPropertyValue_InContainer(SpeedInstance.Get()));
    TestTrue(TEXT("An explicit source False tag still applies on top of template defaults"),
        P4ApplyDeclaredLevelDefaultsForTest(SpeedInstance.Get(), SpeedTemplate.Get(), TEXT("bSendRTPCMoveSpeedVerticalValue"), false, Error));
    TestFalse(TEXT("Hydration never replaces an explicit authored False with template True"), Speed->GetPropertyValue_InContainer(SpeedInstance.Get()));
    TStrongObjectPtr<UObject> SinkTemplate(NewObject<UObject>(GetTransientPackage(), SinkClass));
    TStrongObjectPtr<UObject> SinkInstance(NewObject<UObject>(GetTransientPackage(), SinkClass));
    for (const auto& Pair : {TPair<const TCHAR*, const TCHAR*>(TEXT("AudioKeyOnSinked"), TEXT("WaterScrape")),
        TPair<const TCHAR*, const TCHAR*>(TEXT("AudioKeyOnLand"), TEXT("WaterScrape_Stop"))})
    {
        FNameProperty* Name = FindFProperty<FNameProperty>(SinkClass, Pair.Key);
        if (!TestNotNull(TEXT("The original sound key is reflected"), Name)) return false;
        Name->SetPropertyValue_InContainer(SinkTemplate.Get(), FName(Pair.Value));
        Name->SetPropertyValue_InContainer(SinkInstance.Get(), NAME_None);
    }
    TestTrue(TEXT("The source-absent sound keys hydrate from the original template"),
        P4ApplyDeclaredLevelDefaultsForTest(SinkInstance.Get(), SinkTemplate.Get(), FString(), false, Error));
    TestEqual(TEXT("WaterScrape is preserved rather than native None"),
        FindFProperty<FNameProperty>(SinkClass, TEXT("AudioKeyOnSinked"))->GetPropertyValue_InContainer(SinkInstance.Get()), FName(TEXT("WaterScrape")));
    TStrongObjectPtr<UObject> OwnerTemplate(NewObject<UObject>(GetTransientPackage(), HandlerClass));
    TStrongObjectPtr<UObject> OwnerInstance(NewObject<UObject>(GetTransientPackage(), HandlerClass));
    UObject* SourceChild = NewObject<UObject>(OwnerTemplate.Get(), SpeedClass, TEXT("OriginalUnit_0"));
    UObject* OriginalChild = NewObject<UObject>(OwnerInstance.Get(), SpeedClass, TEXT("OriginalUnit_0"), RF_NoFlags, SourceChild);
    UObject* ConstructorChild = NewObject<UObject>(OwnerInstance.Get(), SpeedClass, TEXT("ParentUnit_1"));
    FObjectPropertyBase* Slot = FindFProperty<FObjectPropertyBase>(HandlerClass, TEXT("Unit_0"));
    if (!TestTrue(TEXT("The source slot is truly instanced"), Slot && Slot->HasAnyPropertyFlags(CPF_InstancedReference))) return false;
    Slot->SetObjectPropertyValue_InContainer(OwnerTemplate.Get(), SourceChild);
    Slot->SetObjectPropertyValue_InContainer(OwnerInstance.Get(), ConstructorChild);
    TestTrue(TEXT("Absent source references map to the original named child of this instance"),
        P4ApplyDeclaredLevelDefaultsForTest(OwnerInstance.Get(), OwnerTemplate.Get(), FString(), false, Error));
    TestTrue(TEXT("The source slot points at its own original child, not a constructor leftover or template child"),
        Slot->GetObjectPropertyValue_InContainer(OwnerInstance.Get()) == OriginalChild && OriginalChild != SourceChild && IsValid(ConstructorChild));
    return !HasAnyErrors();
}
#endif

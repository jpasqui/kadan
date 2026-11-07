#if P4_MAPKIT_TESTS
#include "P4AuthoringRows.h"
#include "P4CreatorSource.h"
#include "P4CreatorMaterialFactories.h"
#include "Misc/AutomationTest.h"
#include "UObject/CoreRedirects.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanScriptNamesTest, "P4MapKit.Kadan.ScriptNames",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4KadanScriptNamesTest::RunTest(const FString&)
{
    const UClass* Classes[] = {UP4AuthoringTools::StaticClass(), UP4MaterialExportData::StaticClass(), AP4GameplayHandle::StaticClass(), UP4CreatorSourceData::StaticClass(), UP4CreatorMaterialFactory::StaticClass(), UP4CreatorMaterialInstanceFactory::StaticClass()};
    for (const UClass* Class : Classes)
    {
        TestEqual(TEXT("the class belongs to Kadan"), Class->GetOutermost()->GetName(), FString(TEXT("/Script/Kadan")));
        const FString OldPath = TEXT("/Script/P4MapKitAuthoring.") + Class->GetName();
        const FCoreRedirectObjectName Redirected = FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Class, FCoreRedirectObjectName(OldPath));
        TestEqual(TEXT("the old script name redirects to the same class"), Redirected.ToString(), Class->GetPathName());
        TestTrue(TEXT("the old class path loads the canonical class"), LoadClass<UObject>(nullptr, *OldPath) == Class);
    }
    return !HasAnyErrors();
}
#endif

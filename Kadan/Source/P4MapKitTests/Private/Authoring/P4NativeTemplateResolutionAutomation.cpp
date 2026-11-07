#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Components/ArrowComponent.h"
#include "UObject/Package.h"
namespace P4LevelContent { KADAN_API UObject* ResolveOriginalExternalTemplate(const FString& Reference); }
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4NativeTemplateResolution,
    "P4MapKit.NativeImport.DeclaredNativeTemplateResolution",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4NativeTemplateResolution::RunTest(const FString&)
{
    UObject* Template = P4LevelContent::ResolveOriginalExternalTemplate(TEXT("/Script/Engine.Default__ArrowComponent"));
    TestNotNull(TEXT("Compiled-in Engine package is registered without needing a disk linker"), FindPackage(nullptr, TEXT("/Script/Engine")));
    TestNotNull(TEXT("The declared native template resolves before comparisons"), Template);
    TestTrue(TEXT("The source's exact stock native template is materialized"),
        Template && Template == UArrowComponent::StaticClass()->GetDefaultObject() &&
        Template->GetPathName() == TEXT("/Script/Engine.Default__ArrowComponent"));
    TestNull(TEXT("A source-absent native child is never fabricated"),
        P4LevelContent::ResolveOriginalExternalTemplate(TEXT("/Script/Engine.Default__ArrowComponent.NotAnOriginalSubobject")));
    TestNull(TEXT("A mismatched native CDO name cannot borrow another class default"),
        P4LevelContent::ResolveOriginalExternalTemplate(TEXT("/Script/Engine.Default__P4MissingSourceClass")));
    TestNull(TEXT("An unqualified short name is not authoritative source identity"),
        P4LevelContent::ResolveOriginalExternalTemplate(TEXT("Default__ArrowComponent")));
    return !HasAnyErrors();
}
#endif

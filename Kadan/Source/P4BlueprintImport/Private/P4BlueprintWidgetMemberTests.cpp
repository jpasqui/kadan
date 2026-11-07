#if WITH_DEV_AUTOMATION_TESTS
#include "P4BlueprintWidgetMembers.h"
#include "Misc/AutomationTest.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Curves/CurveFloat.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UObject/StrongObjectPtr.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Animation/WidgetAnimation.h"
#include "Blueprint/UserWidget.h"
#include "MovieScene.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4WidgetGeneratedMemberGuards,
    "P4MapKit.Blueprints.WidgetGeneratedMemberGuards",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FP4WidgetGeneratedMemberGuards::RunTest(const FString&)
{
    auto Plan=MakeShared<FJsonObject>();Plan->SetStringField(TEXT("package"),TEXT("/Game/P4WidgetSourceGuard"));
    Plan->SetStringField(TEXT("kind"),TEXT("WidgetBlueprint"));auto Widget=MakeShared<FJsonObject>();
    Plan->SetObjectField(TEXT("widget"),Widget);FString Why;
    TestTrue(TEXT("Legacy plan without a native caller keeps its existing route"),P4WidgetMembers::Preflight(Plan,nullptr,Why));
    Widget->SetObjectField(TEXT("generated_members"),MakeShared<FJsonObject>());
    TestFalse(TEXT("JSON witness without native authority refuses"),P4WidgetMembers::Preflight(Plan,nullptr,Why));
    TestTrue(TEXT("Missing source authority gives concrete Kadan action"),Why.Contains(TEXT("through Kadan")));
    int32 Claims=0,Missing=0;
    P4WidgetMembers::FValidateSource Reject=[&](const FString& Package,const TSharedPtr<FJsonObject>& Claim,FString& Error)
    {
        TestEqual(TEXT("Validator receives exact selected package"),Package,FString(TEXT("/Game/P4WidgetSourceGuard")));
        if(Claim.IsValid())++Claims;else ++Missing;
        Error=Claim.IsValid()?TEXT("Actual source rejected this claim"):TEXT("Actual Widget source requires its witness");return false;
    };
    {
        P4WidgetMembers::FSourceScope Source(Reject);
        TestFalse(TEXT("Actual source refusal propagates before any object work"),P4WidgetMembers::Preflight(Plan,nullptr,Why));
        TestEqual(TEXT("One native claim callback invocation"),Claims,1);
        Widget->RemoveField(TEXT("generated_members"));
        TestFalse(TEXT("Stripping witness cannot select legacy semantics in native route"),P4WidgetMembers::Preflight(Plan,nullptr,Why));
        TestEqual(TEXT("Native caller explicitly checks missing witness"),Missing,1);
        TArray<TSharedPtr<FJsonValue>> Malformed;Widget->SetArrayField(TEXT("generated_members"),Malformed);
        TestFalse(TEXT("Malformed witness refuses"),P4WidgetMembers::Preflight(Plan,nullptr,Why));
        TestEqual(TEXT("Malformed witness did not reach source or mutation code"),Claims+Missing,2);
    }
    Widget->RemoveField(TEXT("generated_members"));
    TestTrue(TEXT("Native authority scope is restored"),P4WidgetMembers::Preflight(Plan,nullptr,Why));
    // Crafted JSON is used only for negative component controls. No source
    // validator grants it authority and no declaration/import/save runs here.
    UWidgetBlueprint* Existing=NewObject<UWidgetBlueprint>(GetTransientPackage());
    Existing->WidgetTree=NewObject<UWidgetTree>(Existing,TEXT("WidgetTree"));
    UImage* Image=NewObject<UImage>(Existing->WidgetTree,TEXT("OwnedWidget"));
    Existing->WidgetTree->RootWidget=Image;Image->bIsVariable=true;
    auto Record=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Path;
    Path.Add(MakeShared<FJsonValueString>(TEXT("WidgetTree")));Path.Add(MakeShared<FJsonValueString>(TEXT("OwnedWidget")));
    Record->SetArrayField(TEXT("path"),Path);Record->SetStringField(TEXT("class"),Image->GetClass()->GetPathName());
    auto Proof=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Empty,Tree;
    Tree.Add(MakeShared<FJsonValueObject>(Record));Proof->SetArrayField(TEXT("tree_objects"),Tree);Proof->SetArrayField(TEXT("members"),Empty);
    Widget->SetObjectField(TEXT("generated_members"),Proof);const FName Before=Image->GetFName();
    TestFalse(TEXT("Existing differing variable intent refuses before reset"),P4WidgetMembers::ApplyVariableIntent(Existing,Plan,Why));
    TestTrue(TEXT("Existing variable edit remains exact"),Image->bIsVariable&&Image->GetFName()==Before&&Existing->WidgetTree->RootWidget==Image);
    UObject* NotWidget=NewObject<UCurveFloat>(Existing->WidgetTree,TEXT("NotWidget"));
    auto Wrong=MakeShared<FJsonObject>(*Record);Path[1]=MakeShared<FJsonValueString>(TEXT("NotWidget"));
    Wrong->SetArrayField(TEXT("path"),Path);Wrong->SetStringField(TEXT("role"),TEXT("tree"));
    Wrong->SetStringField(TEXT("name"),NotWidget->GetName());Wrong->SetStringField(TEXT("class"),NotWidget->GetClass()->GetPathName());
    TArray<TSharedPtr<FJsonValue>> WrongMembers;WrongMembers.Add(MakeShared<FJsonValueObject>(Wrong));Proof->SetArrayField(TEXT("members"),WrongMembers);
    TestFalse(TEXT("An arbitrary UObject under WidgetTree cannot become a widget field"),P4WidgetMembers::ApplyVariableIntent(Existing,Plan,Why));
    TestTrue(TEXT("Nonwidget refusal preserved existing widget edit"),Image->bIsVariable&&Existing->WidgetTree->RootWidget==Image);
    return true;
}
#if defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
namespace P4FixtureRetirement { bool Package(UPackage*); }
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4WidgetGeneratedAnimationFlags,
    "P4MapKit.Blueprints.WidgetGeneratedAnimationFlags",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FP4WidgetGeneratedAnimationFlags::RunTest(const FString&)
{
    UPackage* Package=CreatePackage(*(TEXT("/Game/P4WidgetAnimationFlags_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    TStrongObjectPtr<UWidgetBlueprint> Blueprint(Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(UUserWidget::StaticClass(),Package,TEXT("WidgetFlags"),BPTYPE_Normal,UWidgetBlueprint::StaticClass(),UWidgetBlueprintGeneratedClass::StaticClass())));
    ON_SCOPE_EXIT { Blueprint.Reset(); P4FixtureRetirement::Package(Package); };
    if(!TestTrue(TEXT("create a genuine widget Blueprint"),Blueprint.IsValid()))return false;
    if(!Blueprint->WidgetTree)Blueprint->WidgetTree=NewObject<UWidgetTree>(Blueprint.Get(),TEXT("WidgetTree"));
    UWidgetAnimation* Animation=NewObject<UWidgetAnimation>(Blueprint.Get(),TEXT("Out_Many"));
    FObjectProperty* Scene=FindFProperty<FObjectProperty>(UWidgetAnimation::StaticClass(),TEXT("MovieScene"));
    if(!TestNotNull(TEXT("animation movie-scene field exists"),Scene))return false;
    Scene->SetObjectPropertyValue_InContainer(Animation,NewObject<UMovieScene>(Animation,TEXT("Out_Many")));
    Blueprint->Animations.Add(Animation);
    for(int32 Compile=0;Compile<3;++Compile)
    {
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint.Get());
        FKismetEditorUtilities::CompileBlueprint(Blueprint.Get(),EBlueprintCompileOptions::SkipGarbageCollection|EBlueprintCompileOptions::SkipSave);
        const FObjectProperty* Field=FindFProperty<FObjectProperty>(Blueprint->GeneratedClass,TEXT("Out_Many"));
        if(!TestNotNull(TEXT("native compiler creates the source animation field"),Field))return false;
        AddInfo(FString::Printf(TEXT("compile %d animation flags 0x%llx"),Compile,uint64(Field->GetPropertyFlags())));
        TestTrue(TEXT("native linked animation field matches the game's independently read serialized member"),P4WidgetMembers::SourceFieldFlagsMatch(uint64(Field->GetPropertyFlags()),uint64(0x80002014)));
        TestFalse(TEXT("an authored Edit flag change remains a real mismatch"),P4WidgetMembers::SourceFieldFlagsMatch(uint64(Field->GetPropertyFlags())^uint64(CPF_Edit),uint64(0x80002014)));
        TestFalse(TEXT("an authored RepSkip change remains a real mismatch"),P4WidgetMembers::SourceFieldFlagsMatch(uint64(Field->GetPropertyFlags())^uint64(CPF_RepSkip),uint64(0x80002014)));
    }
    return !HasAnyErrors();
}
#endif
#endif

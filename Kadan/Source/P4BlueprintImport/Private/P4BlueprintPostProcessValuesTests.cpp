#include "P4BlueprintImport.h"
#include "P4BlueprintTestSeams.h"
#include "P4RecoveredPostProcessStorage.h"
#include "Components/PostProcessComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Texture2D.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
namespace {
TSharedPtr<FJsonObject> Member(const FString& Name,const FString& Type,const FString& Text,const FString& Detail=TEXT(""))
{
    auto R=MakeShared<FJsonObject>(); R->SetStringField(TEXT("name"),Name); R->SetStringField(TEXT("type"),Type);
    R->SetNumberField(TEXT("index"),0); R->SetStringField(TEXT("text"),Text);
    TArray<TSharedPtr<FJsonValue>> D; if(!Detail.IsEmpty()) D.Add(MakeShared<FJsonValueString>(Detail));
    R->SetArrayField(TEXT("detail"),D); if(Type==TEXT("StructProperty")) R->SetStringField(TEXT("struct"),Detail); return R;
}
TSharedPtr<FJsonObject> Settings(const TArray<TSharedPtr<FJsonValue>>& Members)
{
    auto R=Member(TEXT("Settings"),TEXT("StructProperty"),TEXT("(BloomIntensity=99.0)"),TEXT("PostProcessSettings"));
    R->SetStringField(TEXT("struct"),TEXT("PostProcessSettings")); R->SetArrayField(TEXT("members"),Members); return R;
}
FString Apply(UObject* Target,const TSharedPtr<FJsonObject>& Value)
{
    auto R=MakeShared<FJsonObject>(); R->SetArrayField(TEXT("values"),{MakeShared<FJsonValueObject>(Value)});
    return P4BlueprintApplySourceValuesForTest(Target,R);
}
bool Ok(const FString& Text)
{
    TSharedPtr<FJsonObject> R; return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),R)&&R.IsValid()&&R->GetBoolField(TEXT("ok"))&&R->GetArrayField(TEXT("problems")).Num()==0;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4BlueprintPostProcessTypedValues,"P4Blueprints.PostProcessTypedValues",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FP4BlueprintPostProcessTypedValues::RunTest(const FString&)
{
	P4_REQUIRE_BLUEPRINT_TEST_SEAMS();
    UClass* Class=LoadClass<UPostProcessComponent>(nullptr,TEXT("/Script/P4StubsCore.P4RecoveredPostProcessComponent"));
    if(!TestNotNull(TEXT("Requires actual UHT postprocess SDK storage"),Class)) return false;
    TStrongObjectPtr<UPostProcessComponent> Target(NewObject<UPostProcessComponent>(GetTransientPackage(),Class));
    FP4RecoveredPostProcessStorage Storage; FString Error;
    if(!TestTrue(TEXT("Exact actual native storage contract"),P4GetRecoveredPostProcessStorage(Class,Storage,Error))) return false;
    TStrongObjectPtr<UTexture2D> Mask(NewObject<UTexture2D>(GetTransientPackage(),NAME_None,RF_Transient));
    TArray<TSharedPtr<FJsonValue>> Members;
    auto Add=[&](const FString& Name,const FString& Type,const FString& Text,const FString& Detail=TEXT("")) { Members.Add(MakeShared<FJsonValueObject>(Member(Name,Type,Text,Detail))); };
    Add(TEXT("BloomIntensity"),TEXT("FloatProperty"),TEXT("0.125"));
    Add(TEXT("bOverride_C4DepthOfFieldMask"),TEXT("BoolProperty"),TEXT("True"));
    Add(TEXT("C4PostProcessParam0"),TEXT("StructProperty"),TEXT("(X=0.25,Y=0.5,Z=0.75,W=1.0)"),TEXT("Vector4"));
    Add(TEXT("C4DepthOfFieldMask"),TEXT("ObjectProperty"),Mask->GetPathName());
    Add(TEXT("DepthOfFieldMethod"),TEXT("ByteProperty"),TEXT("DOFM_GatherDOF"),TEXT("EDepthOfFieldMethod"));
    TestTrue(TEXT("Production Apply takes typed common and fork members"),Ok(Apply(Target.Get(),Settings(Members))));
    TestEqual(TEXT("Common native value comes from member, not flattened text"),Target->Settings.BloomIntensity,0.125f);
    FP4RecoveredPostProcessMember Bool,Vector,Texture,Enum;
    if(!TestTrue(TEXT("All genuine fork properties resolve"),P4ResolveRecoveredPostProcessMember(Storage,TEXT("bOverride_C4DepthOfFieldMask"),Bool)
        &&P4ResolveRecoveredPostProcessMember(Storage,TEXT("C4PostProcessParam0"),Vector)
        &&P4ResolveRecoveredPostProcessMember(Storage,TEXT("C4DepthOfFieldMask"),Texture)
        &&P4ResolveRecoveredPostProcessMember(Storage,TEXT("DepthOfFieldMethod"),Enum))) return false;
    TestTrue(TEXT("Actual packed bool was assigned"),CastFieldChecked<FBoolProperty>(Bool.Property)->GetPropertyValue(P4RecoveredPostProcessMemberData(Target.Get(),Storage,Bool)));
    const FVector4* V=static_cast<const FVector4*>(P4RecoveredPostProcessMemberData(Target.Get(),Storage,Vector));
    TestTrue(TEXT("Real typed vector storage"),V&&V->X==0.25f&&V->Y==0.5f&&V->Z==0.75f&&V->W==1.f);
    TestTrue(TEXT("Original object identity reaches native reference storage"),CastFieldChecked<FObjectPropertyBase>(Texture.Property)->GetObjectPropertyValue(P4RecoveredPostProcessMemberData(Target.Get(),Storage,Texture))==Mask.Get());
    const FByteProperty* E=CastFieldChecked<FByteProperty>(Enum.Property);
    const FString Native=E->Enum->GetNameStringByValue(E->GetPropertyValue(P4RecoveredPostProcessMemberData(Target.Get(),Storage,Enum)));
    FString Original; TestTrue(TEXT("Actual distinct enum retains GatherDOF meaning"),P4RecoveredPostProcessNativeEnum(Enum,Native,Original)&&Original==TEXT("DOFM_GatherDOF"));
    // Every structural refusal occurs before any common/fork assignment.
    auto CheckRefused=[&](const TSharedPtr<FJsonObject>& R,const FString& Why) { Target->Settings.BloomIntensity=0.75f; TestFalse(Why,Ok(Apply(Target.Get(),R))); TestEqual(TEXT("Refused contract leaves common storage unchanged"),Target->Settings.BloomIntensity,0.75f); };
    auto Unknown=Members; Unknown.Add(MakeShared<FJsonValueObject>(Member(TEXT("UnknownForkMember"),TEXT("FloatProperty"),TEXT("2.0"))));
    CheckRefused(Settings(Unknown),TEXT("Unknown members remain explicit refusal"));
    auto Duplicate=Members; Duplicate.Add(Members[0]); CheckRefused(Settings(Duplicate),TEXT("Duplicate native field assignment refused"));
    auto Wrong=Members; Wrong[0]=MakeShared<FJsonValueObject>(Member(TEXT("BloomIntensity"),TEXT("IntProperty"),TEXT("3")));
    CheckRefused(Settings(Wrong),TEXT("Source type mismatch refused"));
    auto WrongEnum=Members; WrongEnum[4]=MakeShared<FJsonValueObject>(Member(TEXT("DepthOfFieldMethod"),TEXT("ByteProperty"),TEXT("DOFM_Gaussian"),TEXT("EDepthOfFieldMethod")));
    CheckRefused(Settings(WrongEnum),TEXT("Stock enum aliases cannot change original meaning"));
    auto Malformed=Members; Malformed.Add(MakeShared<FJsonValueString>(TEXT("not a typed member")));
    CheckRefused(Settings(Malformed),TEXT("Malformed member is explicit refusal without partial writes"));
    auto MissingType=Members; auto Incomplete=Member(TEXT("BloomIntensity"),TEXT("FloatProperty"),TEXT("0.25")); Incomplete->RemoveField(TEXT("type"));
    MissingType[0]=MakeShared<FJsonValueObject>(Incomplete); CheckRefused(Settings(MissingType),TEXT("Missing member type refused"));
    auto BadIndex=Members; auto Indexed=Member(TEXT("BloomIntensity"),TEXT("FloatProperty"),TEXT("0.25")); Indexed->SetNumberField(TEXT("index"),0.5);
    BadIndex[0]=MakeShared<FJsonValueObject>(Indexed); CheckRefused(Settings(BadIndex),TEXT("Fractional array index refused"));
    auto OldPlan=Settings(Members); OldPlan->RemoveField(TEXT("members")); CheckRefused(OldPlan,TEXT("Old whole-text contract cannot silently lose fork values"));
    auto Direct=Settings(Members); Direct->SetStringField(TEXT("name"),TEXT("ForkSettings")); CheckRefused(Direct,TEXT("Editor extension cannot appear as a source path"));
    Direct->SetStringField(TEXT("name"),TEXT("forksettings")); CheckRefused(Direct,TEXT("FName case cannot bypass extension path refusal"));
    TStrongObjectPtr<UPostProcessComponent> Stock(NewObject<UPostProcessComponent>());
    auto StockPlan=Settings(Members); StockPlan->SetStringField(TEXT("text"),TEXT("(BloomIntensity=0.625)"));
    TestTrue(TEXT("Unrelated stock component keeps ordinary text application"),Ok(Apply(Stock.Get(),StockPlan)));
    TestEqual(TEXT("Stock path does not use extension member aliases"),Stock->Settings.BloomIntensity,0.625f);
    return !HasAnyErrors();
}
#endif

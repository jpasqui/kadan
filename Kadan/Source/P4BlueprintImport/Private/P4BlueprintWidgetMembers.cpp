#include "P4BlueprintWidgetMembers.h"
#include "P4BlueprintReimport.h"
#include "P4BlueprintImport.h"
#include "P4BlueprintPlan.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Animation/WidgetAnimation.h"
#include "Components/Widget.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace P4WidgetMembers
{
namespace
{
const FValidateSource* Source=nullptr;
const TSharedPtr<FJsonObject>* Proof(const TSharedPtr<FJsonObject>& Plan)
{
    const TSharedPtr<FJsonObject>* Widget=nullptr;const TSharedPtr<FJsonObject>* Found=nullptr;
    return Plan.IsValid()&&Plan->TryGetObjectField(TEXT("widget"),Widget)&&(*Widget)->TryGetObjectField(TEXT("generated_members"),Found)?Found:nullptr;
}
UObject* Placed(UWidgetBlueprint* Blueprint,const TSharedPtr<FJsonObject>& Member)
{
    UObject* Here=Blueprint;
    for(const auto& Part:Member->GetArrayField(TEXT("path")))Here=Here?StaticFindObjectFast(nullptr,Here,FName(*Part->AsString())):nullptr;
    return Here;
}
FString SourceObjectPath(const UObject* Object)
{
    if(!Object)return FString();const UPackage* Package=Object->GetOutermost();
    TArray<FString> Names;TSet<const UObject*> Seen;const UObject* Here=Object;
    while(Here&&Here!=Package)
    {
        if(Seen.Contains(Here)||Seen.Num()>=512)return FString();Seen.Add(Here);
        Names.Insert(Here->GetName(),0);Here=Here->GetOuter();
    }
    // Package.Reference encodes the actual owner chain with dots. GetPathName
    // instead inserts ':' at one subobject boundary; do not compare aliases or
    // discard owner levels to bridge that formatting difference.
    return Here==Package?Package->GetName()+TEXT(".")+FString::Join(Names,TEXT(".")):FString();
}
bool Intent(UWidgetBlueprint* Blueprint,const TSharedPtr<FJsonObject>& Plan,bool Apply,FString& Why)
{
    const auto* Held=Proof(Plan);if(!Held)return true;
    const auto& Members=(*Held)->GetArrayField(TEXT("members"));TMap<FName,bool> Wanted;
    for(const auto& Item:Members)
    {
        const auto Member=Item->AsObject();if(Member->GetStringField(TEXT("role"))!=TEXT("tree"))continue;
        UWidget* Widget=Cast<UWidget>(Placed(Blueprint,Member));
        if(!Widget||!Widget->IsIn(Blueprint->WidgetTree)||Widget->GetClass()->GetPathName()!=Member->GetStringField(TEXT("class")))
        {Why=TEXT("source widget member has no exact owned widget: ")+Member->GetStringField(TEXT("name"));return false;}
        Wanted.Add(Widget->GetFName(),(P4Plan::Flags(Member,TEXT("flags"))&CPF_BlueprintVisible)!=0);
    }
    // Only source-listed tree objects are considered; independent creator objects are untouched.
    for(const auto& Item:(*Held)->GetArrayField(TEXT("tree_objects")))
    {
        const auto Record=Item->AsObject();const auto& Path=Record->GetArrayField(TEXT("path"));
        if(!Path.Num()||Path[0]->AsString()!=TEXT("WidgetTree"))continue;
        UWidget* Widget=Cast<UWidget>(Placed(Blueprint,Record));if(!Widget)continue;
        if(!Widget->IsIn(Blueprint->WidgetTree)||Widget->GetClass()->GetPathName()!=Record->GetStringField(TEXT("class")))
        {Why=TEXT("source widget class differs before variable intent");return false;}
        const bool Expected=Wanted.FindRef(Widget->GetFName());
        if(!Apply&&Widget->bIsVariable!=Expected)
        {Why=TEXT("existing source widget variable intent differs: ")+Widget->GetName();return false;}
        if(Apply)Widget->bIsVariable=Expected;
    }
    return true;
}
}
FSourceScope::FSourceScope(const FValidateSource& In):Previous(Source){Source=&In;}
FSourceScope::~FSourceScope(){Source=Previous;}
bool Preflight(const TSharedPtr<FJsonObject>& Plan,UObject* Existing,FString& Why)
{
    const auto* Held=Proof(Plan);
    if(!Held)
    {
        const TSharedPtr<FJsonObject>* Widget=nullptr;
        if(Plan.IsValid()&&Plan->TryGetObjectField(TEXT("widget"),Widget)&&(*Widget)->HasField(TEXT("generated_members")))
        {Why=TEXT("Replan this Widget Blueprint import; its generated-member source witness is malformed.");return false;}
        // The authentic caller knows which packages are Widgets independently
        // of mutable JSON. Removing the witness cannot select legacy semantics.
        if(Source&&*Source&&!(*Source)(Plan->GetStringField(TEXT("package")),nullptr,Why))return false;
        return true;
    }
    if(!Source||!*Source){Why=TEXT("Import this Widget Blueprint through Kadan so its original source can be checked.");return false;}
    if(!(*Source)(Plan->GetStringField(TEXT("package")),*Held,Why))return false;
    if(Existing&&!UP4BlueprintImport::IsStandIn(Existing))
    {
        UWidgetBlueprint* Blueprint=Cast<UWidgetBlueprint>(Existing);
        if(!Blueprint||!Blueprint->WidgetTree){Why=TEXT("existing source widget has no owned tree");return false;}
        return Intent(Blueprint,Plan,false,Why);
    }
    return true;
}
bool ApplyVariableIntent(UWidgetBlueprint* Blueprint,const TSharedPtr<FJsonObject>& Plan,FString& Why)
{
    if(!Proof(Plan))return true;
    const bool Initialize=P4Reimport::IsFreshBlueprint(Blueprint)||P4Reimport::CanInitializeAnimationOwnership(Blueprint);
    return Intent(Blueprint,Plan,Initialize,Why);
}
bool SourceFieldFlagsMatch(uint64 Live,uint64 Original)
{
    // FProperty::Link derives POD/zeroing/destruction/hash traits from the live
    // native property implementation. They are not source-authored field flags.
    // Keep every ownership, shape and noncomputed flag check exact.
    const uint64 Declared=~uint64(CPF_ComputedFlags);
    return (Live&Declared)==(Original&Declared);
}
bool VerifyCompiled(UWidgetBlueprint* Blueprint,const TSharedPtr<FJsonObject>& Plan,bool CheckDefaults,FString& Why)
{
    const auto* Held=Proof(Plan);if(!Held)return true;
    UWidgetBlueprintGeneratedClass* Class=Cast<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass);
    if(!Class||Class->ClassGeneratedBy!=Blueprint||Class->GetOuter()!=Blueprint->GetOuter()||Blueprint->Status==BS_Error||
        Class->GetPathName()!=(*Held)->GetStringField(TEXT("source_class")))
    {Why=TEXT("widget generated class owner differs");return false;}
    UObject* Cdo=Class->GetDefaultObject();
    if(!Cdo||Cdo->GetClass()!=Class||Cdo->GetOuter()!=Class->GetOuter()||Cdo->GetPathName()!=(*Held)->GetStringField(TEXT("source_cdo")))
    {Why=TEXT("widget generated class CDO differs");return false;}
    TSet<FName> ExpectedFields;
    for(const auto& Item:(*Held)->GetArrayField(TEXT("members")))
    {
        const auto Member=Item->AsObject();const FString Name=Member->GetStringField(TEXT("name"));
        ExpectedFields.Add(FName(*Name));
        const FObjectProperty* Field=FindFProperty<FObjectProperty>(Class,FName(*Name));
        if(!Field||Field->GetOwnerStruct()!=Class||Field->ArrayDim!=1||Field->ElementSize!=8||
            Field->PropertyClass->GetPathName()!=Member->GetStringField(TEXT("class"))||
            !SourceFieldFlagsMatch(uint64(Field->GetPropertyFlags()),P4Plan::Flags(Member,TEXT("flags"))))
        {
            Why=TEXT("widget generated field differs: ")+Name+FString::Printf(TEXT(" (actual owner=%s class=%s dim=%d size=%d flags=0x%llx; source class=%s flags=0x%llx)"),
                Field?*Field->GetOwnerStruct()->GetPathName():TEXT("missing"),Field?*Field->PropertyClass->GetPathName():TEXT("missing"),
                Field?Field->ArrayDim:0,Field?Field->ElementSize:0,Field?uint64(Field->GetPropertyFlags()):0,
                *Member->GetStringField(TEXT("class")),P4Plan::Flags(Member,TEXT("flags")));
            return false;
        }
        UObject* Compiled=nullptr;
        if(Member->GetStringField(TEXT("role"))==TEXT("tree"))
        {
            UWidgetTree* Tree=Class->GetWidgetTreeArchetype();const auto& Path=Member->GetArrayField(TEXT("path"));
            Compiled=Class;
            for(int32 N=0;N<Path.Num();++N)
            {
                Compiled=Compiled?StaticFindObjectFast(nullptr,Compiled,FName(*Path[N]->AsString())):nullptr;
                if(N==0&&Compiled!=Tree){Why=TEXT("widget compiled tree owner differs: ")+Name;return false;}
            }
            if(!Cast<UWidget>(Compiled)||!Tree||Tree->FindWidget(FName(*Name))!=Compiled)
            {Why=TEXT("widget compiled tree placement differs: ")+Name;return false;}
        }
        else
        {
            Compiled=StaticFindObjectFast(UWidgetAnimation::StaticClass(),Class,FName(*Member->GetStringField(TEXT("source_object_name"))));
            int32 Listed=0;
            for(UWidgetAnimation* Animation:Class->Animations)
                if(Animation==Compiled)++Listed;
            if(!Compiled||Compiled->GetOuter()!=Class||Listed!=1)
            {Why=TEXT("widget compiled animation owner/list differs: ")+Name;return false;}
        }
        if(!Compiled||Compiled->GetClass()!=Field->PropertyClass||!Compiled->IsIn(Class)||
            SourceObjectPath(Compiled)!=Member->GetStringField(TEXT("source_ref"))||
            SourceObjectPath(Compiled->GetOuter())!=Member->GetStringField(TEXT("source_outer")))
        {Why=TEXT("widget compiled tree/animation link differs: ")+Name;return false;}
        if(CheckDefaults)
        {
            UObject* Value=Field->GetObjectPropertyValue_InContainer(Cdo);const auto Expected=Member->Values.Find(TEXT("cdo_value"));
            if(!Expected||((*Expected)->Type==EJson::Null?Value!=nullptr:
                (*Expected)->Type!=EJson::String||!Value||SourceObjectPath(Value)!=(*Expected)->AsString()))
            {Why=TEXT("widget source CDO member differs: ")+Name;return false;}
        }
    }
    for(const auto& Item:(*Held)->GetArrayField(TEXT("tree_objects")))
    {
        UWidget* Widget=Cast<UWidget>(Placed(Blueprint,Item->AsObject()));if(!Widget)continue;
        const FProperty* Field=Class->FindPropertyByName(Widget->GetFName());
        if(Field&&Field->GetOwnerStruct()==Class&&!ExpectedFields.Contains(Widget->GetFName()))
        {Why=TEXT("source widget generated an extra native field: ")+Widget->GetName();return false;}
    }
    return true;
}
}

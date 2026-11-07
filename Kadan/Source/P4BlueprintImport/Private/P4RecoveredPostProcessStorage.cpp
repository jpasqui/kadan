#include "P4RecoveredPostProcessStorage.h"
#include "Components/PostProcessComponent.h"
#include "Engine/Texture.h"
#include "UObject/UnrealType.h"

namespace
{
const TCHAR* ClassPath=TEXT("/Script/P4StubsCore.P4RecoveredPostProcessComponent");
const TCHAR* StructPath=TEXT("/Script/P4StubsCore.P4RecoveredPostProcessForkSettings");
const TCHAR* EnumPath=TEXT("/Script/P4StubsCore.EP4RecoveredDepthOfFieldMethod");
enum class EKind{Bit,Float,Color,Vector2,Vector4,Texture,Enum};
struct FPostProcessFieldDescriptor{const TCHAR* Name;EKind Kind;};
const FPostProcessFieldDescriptor Fields[]={
    {TEXT("bOverride_C4IndirectLightingVolumetricLightmapIntensity"),EKind::Bit},
    {TEXT("bOverride_C4IndirectLightingSpecularColor"),EKind::Bit},
    {TEXT("bOverride_C4IndirectLightingSpecularIntensity"),EKind::Bit},
    {TEXT("bOverride_DepthOfFieldTiltAngles"),EKind::Bit},
    {TEXT("bOverride_C4DepthOfFieldMask"),EKind::Bit},
    {TEXT("bOverride_C4DepthOfFieldParam0"),EKind::Bit},
    {TEXT("bOverride_C4DepthOfFieldParam1"),EKind::Bit},
    {TEXT("bOverride_C4DepthOfFieldParam2"),EKind::Bit},
    {TEXT("bOverride_C4DepthOfFieldParam3"),EKind::Bit},
    {TEXT("bOverride_C4PostProcessParam0"),EKind::Bit},
    {TEXT("bOverride_DepthOfFieldMethod"),EKind::Bit},
    {TEXT("C4IndirectLightingVolumetricLightmapIntensity"),EKind::Float},
    {TEXT("C4IndirectLightingSpecularColor"),EKind::Color},
    {TEXT("C4IndirectLightingSpecularIntensity"),EKind::Float},
    {TEXT("DepthOfFieldTiltAngles"),EKind::Vector2},
    {TEXT("C4DepthOfFieldMask"),EKind::Texture},
    {TEXT("C4DepthOfFieldParam0"),EKind::Vector4},
    {TEXT("C4DepthOfFieldParam1"),EKind::Vector4},
    {TEXT("C4DepthOfFieldParam2"),EKind::Vector4},
    {TEXT("C4DepthOfFieldParam3"),EKind::Vector4},
    {TEXT("C4PostProcessParam0"),EKind::Vector4},
    {TEXT("DepthOfFieldMethod"),EKind::Enum}};
const TCHAR* SourceEnum[]={TEXT("DOFM_DefaultDOF"),TEXT("DOFM_GatherDOF"),TEXT("DOFM_MAX")};
const TCHAR* NativeEnum[]={TEXT("P4DOFM_DefaultDOF"),TEXT("P4DOFM_GatherDOF"),TEXT("P4DOFM_MAX")};

const FPostProcessFieldDescriptor* Known(FName Name)
{for(const auto& Field:Fields)if(Name==FName(Field.Name))return &Field;return nullptr;}
bool EnumValid(const FByteProperty* Property)
{
    if(!Property||!Property->Enum||Property->Enum->GetPathName()!=EnumPath)return false;
    for(int32 N=0;N<UE_ARRAY_COUNT(NativeEnum);++N)
        if(Property->Enum->GetValueByNameString(NativeEnum[N])!=N)return false;
    return true;
}
bool FieldValid(const FProperty* Property,EKind Kind)
{
    constexpr uint64 SemanticMask=CPF_Edit|CPF_BlueprintVisible|CPF_BlueprintReadOnly|CPF_EditConst|
        CPF_DisableEditOnInstance|CPF_DisableEditOnTemplate|CPF_EditFixedSize|CPF_Interp|CPF_AdvancedDisplay|
        CPF_Transient|CPF_SkipSerialization|CPF_EditorOnly;
    const uint64 Expected=uint64(CPF_Edit|CPF_BlueprintVisible)|
        ((Kind==EKind::Float||Kind==EKind::Color||Kind==EKind::Vector2||Kind==EKind::Vector4)?uint64(CPF_Interp):0)|
        (Kind==EKind::Enum?uint64(CPF_AdvancedDisplay):0);
    if(!Property||Property->ArrayDim!=1||(uint64(Property->GetPropertyFlags())&SemanticMask)!=Expected)return false;
    switch(Kind)
    {
    case EKind::Bit:{const auto* Bit=CastField<FBoolProperty>(Property);return Bit&&!Bit->IsNativeBool();}
    case EKind::Float:return CastField<FFloatProperty>(Property)!=nullptr;
    case EKind::Texture:{const auto* Object=CastField<FObjectProperty>(Property);return Object&&Object->PropertyClass==UTexture::StaticClass();}
    case EKind::Enum:return EnumValid(CastField<FByteProperty>(Property));
    default:
    {
        const auto* Struct=CastField<FStructProperty>(Property);
        const TCHAR* ExpectedStructPath=Kind==EKind::Color?TEXT("/Script/CoreUObject.LinearColor"):
            Kind==EKind::Vector2?TEXT("/Script/CoreUObject.Vector2D"):TEXT("/Script/CoreUObject.Vector4");
        return Struct&&Struct->Struct&&Struct->Struct->GetPathName()==ExpectedStructPath;
    }
    }
}
bool MemberValid(const FP4RecoveredPostProcessStorage& View,const FP4RecoveredPostProcessMember& Member)
{
    return View.ActualClass&&Member.Property&&Member.Container&&
        (Member.Container==View.StockSettings||Member.Container==View.ForkSettings)&&
        Member.Property->GetOwnerStruct()==Member.Container->Struct;
}
bool EnumMemberValid(const FP4RecoveredPostProcessMember& Member)
{
    return Member.OriginalDepthOfFieldMethod&&Member.Container&&Member.Container->GetFName()==TEXT("ForkSettings")&&
        Member.Container->Struct&&Member.Container->Struct->GetPathName()==StructPath&&Member.Property&&
        Member.Property->GetFName()==TEXT("DepthOfFieldMethod")&&Member.Property->GetOwnerStruct()==Member.Container->Struct&&
        EnumValid(CastField<FByteProperty>(Member.Property));
}
bool Spelling(const FString& Name,const TCHAR* Short,const TCHAR* Prefix)
{return Name.Equals(Short,ESearchCase::CaseSensitive)||Name.Equals(FString(Prefix)+TEXT("::")+Short,ESearchCase::CaseSensitive);}
}

bool P4GetRecoveredPostProcessStorage(const UClass* Actual,FP4RecoveredPostProcessStorage& Out,FString& Error)
{
    Out={};Error.Reset();
    if(!Actual||!Actual->GetPathName().Equals(ClassPath,ESearchCase::CaseSensitive))return false;
    const auto Invalid=[&](const FString& Why){Error=TEXT("recovered-postprocess-native-storage-invalid: ")+Why;return false;};
    if(FindObject<UClass>(nullptr,ClassPath)!=Actual||Actual->GetSuperClass()!=UPostProcessComponent::StaticClass()||
        !Actual->HasAnyClassFlags(CLASS_Native)||Actual->HasAnyClassFlags(CLASS_Abstract|CLASS_Deprecated|CLASS_NewerVersionExists))return Invalid(TEXT("class"));
    const auto* Stock=FindFProperty<FStructProperty>(Actual,TEXT("Settings"));
    const auto* Fork=FindFProperty<FStructProperty>(Actual,TEXT("ForkSettings"));
    if(!Stock||Stock->GetOwnerStruct()!=UPostProcessComponent::StaticClass()||Stock->ArrayDim!=1||
        !Stock->Struct||Stock->Struct->GetPathName()!=TEXT("/Script/Engine.PostProcessSettings"))return Invalid(TEXT("inherited Settings"));
    if(!Fork||Fork->GetOwnerStruct()!=Actual||Fork->ArrayDim!=1||!Fork->Struct||Fork->Struct->GetPathName()!=StructPath||
        Fork->Struct->GetSuperStruct()!=nullptr||!Fork->Struct->GetCppStructOps())return Invalid(TEXT("ForkSettings"));
    int32 Count=0;
    for(TFieldIterator<FProperty> It(Fork->Struct,EFieldIteratorFlags::ExcludeSuper);It;++It)
    {
        ++Count;const auto* Descriptor=Known(It->GetFName());
        if(!Descriptor||!FieldValid(*It,Descriptor->Kind)||It->GetOffset_ForInternal()<0||
            It->GetOffset_ForInternal()+It->GetSize()>Fork->Struct->GetPropertiesSize())return Invalid(It->GetName());
    }
    if(Count!=UE_ARRAY_COUNT(Fields))return Invalid(TEXT("incomplete fork member set"));
    Out.ActualClass=Actual;Out.StockSettings=Stock;Out.ForkSettings=Fork;return true;
}
bool P4ResolveRecoveredPostProcessMember(const FP4RecoveredPostProcessStorage& View,FName Name,FP4RecoveredPostProcessMember& Out)
{
    Out={};if(!View.ActualClass||!View.StockSettings||!View.ForkSettings)return false;
    const auto* Descriptor=Known(Name);
    const FStructProperty* Region=Descriptor?View.ForkSettings:View.StockSettings;
    const FProperty* Property=Region->Struct->FindPropertyByName(Name);
    if(!Property||Property->ArrayDim<1||Property->GetOwnerStruct()!=Region->Struct)return false;
    if(Descriptor&&!FieldValid(Property,Descriptor->Kind))return false;
    Out.Container=Region;Out.Property=Property;Out.OriginalDepthOfFieldMethod=Descriptor&&Descriptor->Kind==EKind::Enum;return true;
}
void* P4RecoveredPostProcessMemberData(UObject* Object,const FP4RecoveredPostProcessStorage& View,const FP4RecoveredPostProcessMember& Member)
{
    return Object&&Object->GetClass()==View.ActualClass&&MemberValid(View,Member)?
        Member.Property->ContainerPtrToValuePtr<void>(Member.Container->ContainerPtrToValuePtr<void>(Object)):nullptr;
}
const void* P4RecoveredPostProcessMemberData(const UObject* Object,const FP4RecoveredPostProcessStorage& View,const FP4RecoveredPostProcessMember& Member)
{
    return Object&&Object->GetClass()==View.ActualClass&&MemberValid(View,Member)?
        Member.Property->ContainerPtrToValuePtr<const void>(Member.Container->ContainerPtrToValuePtr<const void>(Object)):nullptr;
}
bool P4RecoveredPostProcessSourceEnum(const FP4RecoveredPostProcessMember& Member,const FString& Original,FString& Native)
{
    Native.Reset();if(!EnumMemberValid(Member))return false;
    for(int32 N=0;N<UE_ARRAY_COUNT(SourceEnum);++N)if(Spelling(Original,SourceEnum[N],TEXT("EDepthOfFieldMethod")))
    {Native=NativeEnum[N];return true;}return false;
}
bool P4RecoveredPostProcessNativeEnum(const FP4RecoveredPostProcessMember& Member,const FString& Native,FString& Original)
{
    Original.Reset();if(!EnumMemberValid(Member))return false;
    for(int32 N=0;N<UE_ARRAY_COUNT(NativeEnum);++N)if(Spelling(Native,NativeEnum[N],TEXT("EP4RecoveredDepthOfFieldMethod")))
    {Original=SourceEnum[N];return true;}return false;
}

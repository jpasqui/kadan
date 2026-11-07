#include "P4AuthoringRows.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "Misc/AutomationTest.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "Camera/CameraShakeSourceComponent.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

namespace P4PropertyAfter201Native
{
UPackage* MakePackage()
{
    return CreatePackage(*(FString(TEXT("/Game/P4PropertyAfter201Native/")) +
        FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/Fixture")));
}
TArray<uint8> Bytes(UObject* Object, bool* Ready = nullptr)
{
    TArray<uint8> Result;
    FObjectWriter Writer(Object, Result, false, false, false, PPF_DuplicateVerbatim);
    if (Ready)
        *Ready = !Writer.IsError() && Result.Num() > 0;
    return Result;
}
struct FObjectState
{
    UObject* Object;
    UObject* Outer;
    UClass* Class;
    FString Path;
    EObjectFlags Flags;
    bool BeforeReady = false;
    TArray<uint8> Before;
    explicit FObjectState(UObject* In)
        : Object(In), Outer(In->GetOuter()), Class(In->GetClass()), Path(In->GetPathName()), Flags(In->GetFlags()),
          Before(Bytes(In, &BeforeReady))
    {}
    bool Same() const
    {
        if (!IsValid(Object) || !BeforeReady)
            return false;
        bool CurrentReady = false;
        const TArray<uint8> Current = Bytes(Object, &CurrentReady);
        return CurrentReady && Object->GetOuter() == Outer && Object->GetClass() == Class &&
            Object->GetPathName() == Path && Object->GetFlags() == Flags && Current == Before;
    }
};
struct FReadState
{
    UObject* Object;
    UPackage* Package;
    UMetaData* Metadata;
    bool Dirty;
    uint32 PackageFlags;
    EObjectFlags Flags;
    FGuid Guid, PersistentGuid;
    FName Filename;
    uint64 FileSize;
    FString PackagePath;
    bool MetadataReady = false;
    TArray<uint8> MetadataBytes;
    TArray<UObject*> Inventory;
    TArray<FObjectState> Objects;
    TArray<FProperty*> Properties;
    TArray<EPropertyFlags> PropertyFlags;
    int32 Pre = 0, Post = 0;
    FDelegateHandle A, B;
    explicit FReadState(UObject* In)
        : Object(In), Package(In->GetOutermost()), Metadata(Package->GetMetaData()), Dirty(Package->IsDirty()),
          PackageFlags(Package->GetPackageFlags()), Flags(Package->GetFlags()), Guid(Package->GetGuid()),
          PersistentGuid(Package->GetPersistentGuid()), Filename(Package->FileName), FileSize(Package->FileSize),
          PackagePath(Package->GetPathName()), MetadataBytes(Bytes(Metadata, &MetadataReady))
    {
        GetObjectsWithOuter(Package, Inventory, true);
        TArray<UObject*> Children;
        GetObjectsWithOuter(Object, Children, true);
        Children.AddUnique(Object);
        for (UObject* Child : Children)
            Objects.Emplace(Child);
        for (TFieldIterator<FProperty> Field(Object->GetClass()); Field; ++Field)
        {
            Properties.Add(*Field);
            PropertyFlags.Add(Field->GetPropertyFlags());
        }
        A = FCoreUObjectDelegates::OnPreObjectPropertyChanged.AddLambda(
            [this](UObject* Target, const FEditPropertyChain&)
            {
                if (Target == Object)
                    ++Pre;
            });
        B = FCoreUObjectDelegates::OnObjectPropertyChanged.AddLambda(
            [this](UObject* Target, FPropertyChangedEvent&)
            {
                if (Target == Object)
                    ++Post;
            });
    }
    ~FReadState()
    {
        FCoreUObjectDelegates::OnPreObjectPropertyChanged.Remove(A);
        FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(B);
    }
    bool Ready() const
    {
        if (!MetadataReady || Objects.Num() == 0)
            return false;
        for (const FObjectState& State : Objects)
            if (!State.BeforeReady)
                return false;
        return true;
    }
    bool MetadataSame() const
    {
        if (!MetadataReady || Package->GetMetaData() != Metadata)
            return false;
        bool CurrentReady = false;
        const TArray<uint8> Current = Bytes(Metadata, &CurrentReady);
        return CurrentReady && Current == MetadataBytes;
    }
    bool Same() const
    {
        if (!Ready())
            return false;
        for (const FObjectState& State : Objects)
            if (!State.Same())
                return false;
        TArray<UObject*> Now;
        GetObjectsWithOuter(Package, Now, true);
        if (Now.Num() != Inventory.Num())
            return false;
        for (UObject* Entry : Inventory)
            if (!Now.Contains(Entry))
                return false;
        if (Package->GetPackageFlags() != PackageFlags || Package->GetFlags() != Flags || Package->GetGuid() != Guid ||
            Package->GetPersistentGuid() != PersistentGuid || Package->IsDirty() != Dirty ||
            Package->FileName != Filename || Package->FileSize != FileSize || Package->GetPathName() != PackagePath ||
            Object->GetOutermost() != Package || !MetadataSame() || Pre != 0 || Post != 0)
            return false;
        int32 Index = 0;
        for (TFieldIterator<FProperty> Field(Object->GetClass()); Field; ++Field, ++Index)
            if (!Properties.IsValidIndex(Index) || Properties[Index] != *Field ||
                PropertyFlags[Index] != Field->GetPropertyFlags())
                return false;
        return Index == Properties.Num();
    }
    bool Preflight(FAutomationTestBase& Test) const
    {
        const bool SnapshotReady = Ready();
        Test.TestTrue(
            TEXT("Mandatory pre-API native object/metadata snapshots serialize without errors and contain bytes"),
            SnapshotReady);
        const bool Stable = SnapshotReady && Same();
        Test.TestTrue(
            TEXT("Mandatory pre-API complete native snapshot is stable before exercising property service"), Stable);
        return Stable;
    }
    void Check(FAutomationTestBase& Test) const
    {
        Test.TestTrue(TEXT("Native object and metadata snapshot readiness is mandatory"), Ready());
        for (const FObjectState& State : Objects)
            Test.TestTrue(TEXT("Refusal/no-op retains complete native bytes and object/outer/class/path/flag identity"),
                State.Same());
        TArray<UObject*> Now;
        GetObjectsWithOuter(Package, Now, true);
        Test.TestTrue(
            TEXT("Refusal/no-op retains exact native package UObject inventory"), Now.Num() == Inventory.Num());
        for (UObject* Entry : Inventory)
            Test.TestTrue(TEXT("Every original native package object remains"), Now.Contains(Entry));
        Test.TestTrue(TEXT("Package flags, native GUIDs and dirty state are unchanged"),
            Package->GetPackageFlags() == PackageFlags && Package->GetFlags() == Flags && Package->GetGuid() == Guid &&
                Package->GetPersistentGuid() == PersistentGuid && Package->IsDirty() == Dirty &&
                Package->FileName == Filename && Package->FileSize == FileSize &&
                Package->GetPathName() == PackagePath && Object->GetOutermost() == Package);
        Test.TestTrue(TEXT("Whole native package metadata object and bytes are unchanged"), MetadataSame());
        int32 Index = 0;
        for (TFieldIterator<FProperty> Field(Object->GetClass()); Field; ++Field, ++Index)
            Test.TestTrue(TEXT("Actual reflected field identities and native flags are unchanged"),
                Properties.IsValidIndex(Index) && Properties[Index] == *Field &&
                    PropertyFlags[Index] == Field->GetPropertyFlags());
        Test.TestEqual(TEXT("Reflected field inventory remains exact"), Index, Properties.Num());
        Test.TestTrue(TEXT("No property pre/post notifications on refused or identical input"), Pre == 0 && Post == 0);
    }
};
void NoPublication(FAutomationTestBase& Test, UPackage* Package)
{
    const FString Directory = FPaths::GetPath(FPackageName::LongPackageNameToFilename(Package->GetName()));
    TArray<FString> Files;
    IFileManager::Get().FindFilesRecursive(Files, *Directory, TEXT("*"), true, false);
    Test.TestEqual(TEXT("Exact GUID-owned Content namespace has no published files or sidecars"), Files.Num(), 0);
}
} //namespace P4PropertyAfter201Native

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PropertyAfter201TemplatePolicy,
    "P4MapKit.Quality.PropertyAfter201NativeTemplatePolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PropertyAfter201TemplatePolicy::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4PropertyAfter201Native;
    UPackage* Owner = MakePackage();
    TStrongObjectPtr<UPhysicsConstraintComponent> Instance(
        NewObject<UPhysicsConstraintComponent>(Owner, NAME_None, RF_Transactional));
    UPhysicsConstraintComponent* Default = GetMutableDefault<UPhysicsConstraintComponent>();
    auto* ActorField = CastField<FObjectProperty>(
        UPhysicsConstraintComponent::StaticClass()->FindPropertyByName(TEXT("ConstraintActor1")));
    if (!TestTrue(TEXT("Installed ConstraintActor1 is genuinely EditInstanceOnly, not EditConst"),
            ActorField && ActorField->HasAllPropertyFlags(CPF_Edit | CPF_DisableEditOnTemplate) &&
                !ActorField->HasAnyPropertyFlags(CPF_EditConst | CPF_DisableEditOnInstance)) ||
        !TestTrue(TEXT("Actual native CDO and ordinary component have distinct template status"),
            Default->IsTemplate() && !Instance->IsTemplate()) ||
        !TestTrue(TEXT("Both constructor-produced actor references are null"),
            !Default->ConstraintActor1 && !Instance->ConstraintActor1))
        return false;
    Owner->SetDirtyFlag(false);
    NoPublication(*this, Owner);
    {
        FReadState DefaultState(Default), InstanceState(Instance.Get());
        if (!DefaultState.Preflight(*this) || !InstanceState.Preflight(*this))
            return false;
        TestFalse(TEXT("Instance-only native actor reference refuses on its real CDO even for None"),
            UP4AuthoringTools::SetEditablePropertyText(Default, TEXT("ConstraintActor1"), TEXT("None")));
        TestTrue(TEXT("Same public field accepts exact None as ordinary instance no-op"),
            UP4AuthoringTools::SetEditablePropertyText(Instance.Get(), TEXT("ConstraintActor1"), TEXT("None")));
        TestTrue(TEXT("Independent actor references remain null on CDO and instance"),
            !Default->ConstraintActor1 && !Instance->ConstraintActor1);
        TestEqual(TEXT("Independent native instance-reference JSON remains null path"),
            UP4AuthoringTools::PropertyJson(Instance.Get(), TEXT("ConstraintActor1")), FString(TEXT("\"\"")));
        DefaultState.Check(*this);
        InstanceState.Check(*this);
    }
    NoPublication(*this, Owner);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PropertyAfter201QuotedEnum, "P4MapKit.Quality.PropertyAfter201NativeQuotedEnum",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PropertyAfter201QuotedEnum::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4PropertyAfter201Native;
    UPackage* Owner = MakePackage();
    TStrongObjectPtr<UCameraShakeSourceComponent> Instance(
        NewObject<UCameraShakeSourceComponent>(Owner, NAME_None, RF_Transactional));
    UCameraShakeSourceComponent* Default = GetMutableDefault<UCameraShakeSourceComponent>();
    auto* Field = CastField<FEnumProperty>(
        UCameraShakeSourceComponent::StaticClass()->FindPropertyByName(TEXT("Attenuation")));
    if (!TestTrue(TEXT("Installed attenuation enum is genuinely writable on templates and instances"),
            Field && Field->GetEnum() && Field->HasAnyPropertyFlags(CPF_Edit) &&
                !Field->HasAnyPropertyFlags(CPF_EditConst | CPF_DisableEditOnTemplate | CPF_DisableEditOnInstance)))
        return false;
    Instance->Attenuation = ECameraShakeAttenuation::Linear;
    Instance->InnerAttenuationRadius = 123.25f;
    Owner->SetDirtyFlag(false);
    NoPublication(*this, Owner);
    {
        FReadState State(Instance.Get());
        if (!State.Preflight(*this))
            return false;
        TestTrue(TEXT("Engine quoted enum token accepts an exact native enum no-op"),
            UP4AuthoringTools::SetEditablePropertyText(
                Instance.Get(), TEXT("Attenuation"), TEXT("\"ECameraShakeAttenuation::Linear\"")));
        TestFalse(TEXT("Quoted valid enum followed by trailing text refuses whole edit"),
            UP4AuthoringTools::SetEditablePropertyText(
                Instance.Get(), TEXT("Attenuation"), TEXT("\"ECameraShakeAttenuation::Quadratic\" trailing")));
        TestTrue(TEXT("Independent native enum and float sibling remain exact"),
            Instance->Attenuation == ECameraShakeAttenuation::Linear && Instance->InnerAttenuationRadius == 123.25f);
        TestEqual(TEXT("Quoted token has exact enum JSON"),
            UP4AuthoringTools::PropertyJson(Instance.Get(), TEXT("Attenuation")),
            FString(TEXT("\"ECameraShakeAttenuation::Linear\"")));
        State.Check(*this);
    }
    {
        // Read the genuine native CDO's existing value; never change a shared CDO.
        const ECameraShakeAttenuation Before = Default->Attenuation;
        const int64 Number = Field->GetUnderlyingProperty()->GetSignedIntPropertyValue(
            Field->ContainerPtrToValuePtr<void>(Default));
        if (!TestTrue(TEXT("Constructor CDO enum is declared by its native enumeration"),
                Field->GetEnum()->IsValidEnumValue(Number)))
            return false;
        const FString Exact = Field->GetEnum()->GetNameByValue(Number).ToString();
        FReadState State(Default);
        if (!State.Preflight(*this))
            return false;
        TestTrue(TEXT("EditAnywhere native enum permits exact no-op on its real CDO"),
            UP4AuthoringTools::SetEditablePropertyText(Default, TEXT("Attenuation"), Exact));
        TestTrue(TEXT("Independent constructor CDO enum retained"), Default->Attenuation == Before);
        State.Check(*this);
    }
    NoPublication(*this, Owner);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PropertyAfter201ScientificNoop,
    "P4MapKit.Quality.PropertyAfter201NativeScientificNoop",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PropertyAfter201ScientificNoop::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4PropertyAfter201Native;
    UClass* Class = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.CarrotSaveGame_Story"));
    auto* Play = Class ? CastField<FDoubleProperty>(Class->FindPropertyByName(TEXT("PlayTime"))) : nullptr;
    auto* Sibling = Class ? CastField<FDoubleProperty>(Class->FindPropertyByName(TEXT("MultiPlayTime"))) : nullptr;
    if (!TestTrue(TEXT("Established captured save-game owns actual native scalar doubles"),
            Play && Sibling && Play->ArrayDim == 1 && Sibling->ArrayDim == 1))
        return false;
    UPackage* Owner = MakePackage();
    TStrongObjectPtr<UObject> Object(NewObject<UObject>(Owner, Class, NAME_None, RF_Transactional));
    Play->SetPropertyValue_InContainer(Object.Get(), 1e20);
    Sibling->SetPropertyValue_InContainer(Object.Get(), 37.25);
    Owner->SetDirtyFlag(false);
    NoPublication(*this, Owner);
    {
        FReadState State(Object.Get());
        if (!State.Preflight(*this))
            return false;
        TestEqual(TEXT("Mantissa-free scientific double JSON is exact and does not append .0"),
            UP4AuthoringTools::PropertyJson(Object.Get(), TEXT("PlayTime")), FString(TEXT("1e+20")));
        TestTrue(TEXT("Exact scientific spelling is a whole-object no-op"),
            UP4AuthoringTools::SetPropertyText(Object.Get(), TEXT("PlayTime"), TEXT("1e20")));
        for (const TCHAR* Invalid : {TEXT("f"), TEXT("F"), TEXT("1e+q")})
            TestFalse(
                *FString::Printf(TEXT("Suffix-only or nondigit exponent refuses exact native double: %s"), Invalid),
                UP4AuthoringTools::SetPropertyText(Object.Get(), TEXT("PlayTime"), Invalid));
        TestTrue(TEXT("Independent requested double and sibling remain native-exact"),
            Play->GetPropertyValue_InContainer(Object.Get()) == 1e20 &&
                Sibling->GetPropertyValue_InContainer(Object.Get()) == 37.25);
        State.Check(*this);
    }
    NoPublication(*this, Owner);
    return !HasAnyErrors();
}
#endif

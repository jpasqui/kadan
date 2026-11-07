// PRIVATE REVIEW DRAFT: not compiled, registered in a build, or executed.
// Rebind historical full110 gaps against the fresh full138 report first.
#include "P4AuthoringRows.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "Editor.h"
#include "AssetRegistryModule.h"
#include "Curves/CurveVector.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TargetPoint.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace P4PropertyRemainingDraft
{
UPackage* P4RemainingPackage(const TCHAR* Case)
{
    return CreatePackage(*(FString(TEXT("/Game/NativePropertyRemaining/")) + Case + TEXT("_") +
        FGuid::NewGuid().ToString(EGuidFormats::Digits)));
}
TArray<uint8> P4RemainingSnapshot(UObject* Object)
{
    TArray<uint8> Bytes;
    FObjectWriter Writer(Object, Bytes, false, false, false, PPF_DuplicateVerbatim);
    return Bytes;
}
struct FP4RemainingGuard
{
    UObject* Object;
    UClass* Class;
    TArray<uint8> Bytes;
    EObjectFlags Flags;
    bool Dirty;
    int32 Pre = 0, Post = 0;
    FDelegateHandle A, B;
    explicit FP4RemainingGuard(UObject* In)
        : Object(In), Class(In->GetClass()), Bytes(P4RemainingSnapshot(In)), Flags(In->GetFlags()),
          Dirty(In->GetOutermost()->IsDirty())
    {
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
    ~FP4RemainingGuard()
    {
        FCoreUObjectDelegates::OnPreObjectPropertyChanged.Remove(A);
        FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(B);
    }
    void Check(FAutomationTestBase& Test)
    {
        Test.TestTrue(TEXT("Independent native nondelta bytes unchanged"), P4RemainingSnapshot(Object) == Bytes);
        Test.TestTrue(
            TEXT("Actual class and flags unchanged"), Object->GetClass() == Class && Object->GetFlags() == Flags);
        Test.TestEqual(TEXT("Prior package dirty state retained"), Object->GetOutermost()->IsDirty(), Dirty);
        Test.TestEqual(TEXT("No pre-edit callback"), Pre, 0);
        Test.TestEqual(TEXT("No post-edit callback"), Post, 0);
    }
};

} //namespace P4PropertyRemainingDraft

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PropertyExistingAssetDraft, "P4MapKit.Quality.PropertyExistingAssetDraft",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PropertyExistingAssetDraft::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4PropertyRemainingDraft;
    UClass* Class = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.OnyonCombineEftColorData"));
    auto* Red = Class ? CastField<FStructProperty>(Class->FindPropertyByName(TEXT("Red"))) : nullptr;
    auto* R = Red ? CastField<FFloatProperty>(Red->Struct->FindPropertyByName(TEXT("R"))) : nullptr;
    if (!TestNotNull(TEXT("Existing original color schema"), Class) ||
        !TestNotNull(TEXT("Actual native float member"), R))
        return false;
    UPackage* Owner = P4RemainingPackage(TEXT("Reuse"));
    TStrongObjectPtr<UObject> Asset(UP4AuthoringTools::CreateNativeAsset(Class, Owner->GetName()));
    if (!TestTrue(TEXT("Native initial creation succeeded"), Asset.IsValid()))
        return false;
    ON_SCOPE_EXIT
    {
        FAssetRegistryModule::AssetDeleted(Asset.Get());
        Asset->ClearFlags(RF_Standalone);
    };
    void* Storage = R->ContainerPtrToValuePtr<void>(Red->ContainerPtrToValuePtr<void>(Asset.Get()));
    R->SetFloatingPointPropertyValue(Storage, 0.3125);
    Owner->SetDirtyFlag(false);
    IAssetRegistry& Registry = FAssetRegistryModule::GetRegistry();
    int32 Created = 0;
    const FDelegateHandle Observer = Registry.OnInMemoryAssetCreated().AddLambda(
        [&](UObject* Object)
        {
            if (Object && Object->GetOutermost() == Owner)
                ++Created;
        });
    ON_SCOPE_EXIT
    {
        Registry.OnInMemoryAssetCreated().Remove(Observer);
    };
    {
        FP4RemainingGuard State(Asset.Get());
        TestTrue(TEXT("Same class/path returns exact existing pointer"),
            UP4AuthoringTools::CreateNativeAsset(Class, Owner->GetName()) == Asset.Get());
        TestEqual(TEXT("Authored native float preserved"), R->GetFloatingPointPropertyValue(Storage), 0.3125);
        TestEqual(TEXT("Re-entry publishes no second registry creation"), Created, 0);
        State.Check(*this);
    }
    UPackage* CollisionOwner = P4RemainingPackage(TEXT("Collision"));
    const FName Name(*FPackageName::GetLongPackageAssetName(CollisionOwner->GetName()));
    TStrongObjectPtr<UCurveVector> Existing(NewObject<UCurveVector>(CollisionOwner, Name, RF_Transactional));
    Existing->FloatCurves[1].AddKey(2.0f, 37.5f);
    CollisionOwner->SetDirtyFlag(false);
    {
        FP4RemainingGuard State(Existing.Get());
        TestNull(TEXT("Wrong class with exact basename refuses replacement"),
            UP4AuthoringTools::CreateNativeAsset(Class, CollisionOwner->GetName()));
        TestTrue(TEXT("Native lookup retains wrong-class pointer"),
            FindObject<UObject>(CollisionOwner, *Name.ToString()) == Existing.Get());
        TestEqual(TEXT("Native curve value survives refusal"), Existing->FloatCurves[1].Eval(2.0f), 37.5f);
        State.Check(*this);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PropertyOriginalRowIdentityDraft,
    "P4MapKit.Quality.PropertyOriginalRowIdentityDraft",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PropertyOriginalRowIdentityDraft::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4PropertyRemainingDraft;
    UScriptStruct* Struct = FindObject<UScriptStruct>(nullptr, TEXT("/Script/Carrot4.DebugLevelRow"));
    auto* Name = Struct ? CastField<FStrProperty>(Struct->FindPropertyByName(TEXT("Name"))) : nullptr;
    auto* Count = Struct ? CastField<FIntProperty>(Struct->FindPropertyByName(TEXT("NightStartNum"))) : nullptr;
    auto* Night = Struct ? CastField<FBoolProperty>(Struct->FindPropertyByName(TEXT("bNightStart"))) : nullptr;
    auto* Menu = Struct ? CastField<FEnumProperty>(Struct->FindPropertyByName(TEXT("MenuType"))) : nullptr;
    if (!TestTrue(TEXT("Actual original TableRowBase child and native fields"),
            Struct && Struct->IsChildOf(FTableRowBase::StaticStruct()) && Name && Count && Night && Menu))
        return false;
    UPackage* Owner = P4RemainingPackage(TEXT("RowIdentity"));
    TStrongObjectPtr<UDataTable> Table(NewObject<UDataTable>(Owner, NAME_None, RF_Transactional));
    Table->RowStruct = Struct;
    FStructOnScope Row(Struct);
    Name->SetPropertyValue_InContainer(Row.GetStructMemory(), TEXT("Distinct original member"));
    Count->SetPropertyValue_InContainer(Row.GetStructMemory(), 17);
    Night->SetPropertyValue_InContainer(Row.GetStructMemory(), true);
    UEnum* Enum = Menu->GetEnum();
    if (!TestTrue(TEXT("Native enum has an independently declared ordinal"),
            Enum && Enum->NumEnums() > 0 && Enum->IsValidEnumValue(Enum->GetValueByIndex(0))))
        return false;
    const int64 MenuValue = Enum->GetValueByIndex(0);
    Menu->GetUnderlyingProperty()->SetIntPropertyValue(
        Menu->ContainerPtrToValuePtr<void>(Row.GetStructMemory()), MenuValue);
    const FName Key(TEXT("DistinctOuterRowKey"));
    Table->AddRow(Key, *reinterpret_cast<const FTableRowBase*>(Row.GetStructMemory()));
    Owner->SetDirtyFlag(false);
    uint8* const* Stored = Table->GetRowMap().Find(Key);
    if (!TestNotNull(TEXT("Native row map owns copied row"), Stored))
        return false;
    {
        FP4RemainingGuard State(Table.Get());
        TestTrue(TEXT("Populated original Name conflict refuses complete transport"),
            UP4AuthoringTools::NativeTableRowsJson(Table.Get()).IsEmpty());
        TestEqual(TEXT("Outer row key remains independently present"), Table->GetRowMap().Num(), 1);
        TestEqual(TEXT("Authored member Name is not hidden or overwritten"),
            Name->GetPropertyValue_InContainer(*Stored), FString(TEXT("Distinct original member")));
        TestEqual(TEXT("Original sibling number preserved"), Count->GetPropertyValue_InContainer(*Stored), 17);
        TestTrue(TEXT("Original sibling bool preserved"), Night->GetPropertyValue_InContainer(*Stored));
        TestEqual(TEXT("Initialized valid enum survives refusal"),
            Menu->GetUnderlyingProperty()->GetSignedIntPropertyValue(Menu->ContainerPtrToValuePtr<void>(*Stored)),
            MenuValue);
        State.Check(*this);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PropertyStoredEnumKeyDraft, "P4MapKit.Quality.PropertyStoredEnumKeyDraft",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PropertyStoredEnumKeyDraft::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4PropertyRemainingDraft;
    UClass* Class = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.VsPokerParameter"));
    auto* Property = Class ? CastField<FMapProperty>(Class->FindPropertyByName(TEXT("PorkerHandTableMap"))) : nullptr;
    auto* Key = Property ? CastField<FEnumProperty>(Property->KeyProp) : nullptr;
    auto* Value = Property ? CastField<FObjectProperty>(Property->ValueProp) : nullptr;
    if (!TestTrue(TEXT("Actual original enum to hard table map"),
            Class && Key && Value && Value->PropertyClass == UDataTable::StaticClass()))
        return false;
    UEnum* Enum = Key->GetEnum();
    if (!TestNotNull(TEXT("Original stored map enum exists"), Enum))
        return false;
    int64 Invalid = 0;
    while (Invalid < 256 && Enum->IsValidEnumValue(Invalid))
        ++Invalid;
    if (!TestTrue(TEXT("Underlying byte has a genuinely undeclared ordinal"),
            Invalid < 256 && !Enum->IsValidEnumValue(Invalid)))
        return false;
    const int64 Valid = Enum->GetValueByNameString(TEXT("EVsPokerGameMode::OneCard"));
    if (!TestTrue(TEXT("Independent original valid key"), Valid == 1 && Enum->IsValidEnumValue(Valid)))
        return false;
    UPackage* Owner = P4RemainingPackage(TEXT("StoredEnum"));
    TStrongObjectPtr<UObject> Object(NewObject<UObject>(Owner, Class, NAME_None, RF_Transactional));
    TStrongObjectPtr<UDataTable> First(NewObject<UDataTable>(Owner));
    TStrongObjectPtr<UDataTable> Second(NewObject<UDataTable>(Owner));
    First->RowStruct = Second->RowStruct = FTableRowBase::StaticStruct();
    FScriptMapHelper Map(Property, Property->ContainerPtrToValuePtr<void>(Object.Get()));
    for (int32 Index = 0; Index < 2; ++Index)
    {
        const int32 Slot = Map.AddDefaultValue_Invalid_NeedsRehash();
        Key->GetUnderlyingProperty()->SetIntPropertyValue(Map.GetKeyPtr(Slot), Index == 0 ? Valid : Invalid);
        Value->SetObjectPropertyValue(Map.GetValuePtr(Slot), Index == 0 ? First.Get() : Second.Get());
    }
    Map.Rehash();
    Owner->SetDirtyFlag(false);
    const int32 Slots = Map.GetMaxIndex();
    {
        FP4RemainingGuard State(Object.Get());
        TestTrue(TEXT("Unrepresented stored enum key refuses whole map JSON"),
            UP4AuthoringTools::PropertyJson(Object.Get(), TEXT("PorkerHandTableMap")).IsEmpty());
        TestEqual(TEXT("Native count retained"), Map.Num(), 2);
        TestEqual(TEXT("Native slot layout retained"), Map.GetMaxIndex(), Slots);
        bool FoundValid = false, FoundInvalid = false;
        for (int32 Slot = 0; Slot < Map.GetMaxIndex(); ++Slot)
        {
            if (!Map.IsValidIndex(Slot))
                continue;
            const int64 Actual = Key->GetUnderlyingProperty()->GetSignedIntPropertyValue(Map.GetKeyPtr(Slot));
            UObject* Reference = Value->GetObjectPropertyValue(Map.GetValuePtr(Slot));
            FoundValid |= Actual == Valid && Reference == First.Get();
            FoundInvalid |= Actual == Invalid && Reference == Second.Get();
        }
        TestTrue(TEXT("Exact valid and invalid native key/value identities preserved"), FoundValid && FoundInvalid);
        State.Check(*this);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PropertyBlueprintCacheDraft, "P4MapKit.Quality.PropertyBlueprintCacheDraft",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PropertyBlueprintCacheDraft::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4PropertyRemainingDraft;
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!TestNotNull(TEXT("Existing isolated editor world"), World))
        return false;
    UPackage* Owner = P4RemainingPackage(TEXT("Blueprint"));
    const FName Name(*FPackageName::GetLongPackageAssetName(Owner->GetName()));
    TStrongObjectPtr<UBlueprint> Blueprint(
        FKismetEditorUtilities::CreateBlueprint(AStaticMeshActor::StaticClass(), Owner, Name, BPTYPE_Normal,
            UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), FName(TEXT("P4PropertyRemaining"))));
    if (!TestTrue(TEXT("Actual engine Blueprint producer succeeded"), Blueprint.IsValid()))
        return false;
    ON_SCOPE_EXIT
    {
        Blueprint->ClearFlags(RF_Standalone);
    };
    FKismetEditorUtilities::CompileBlueprint(Blueprint.Get(), EBlueprintCompileOptions::None, nullptr);
    auto* Class = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass);
    auto* Default = Class ? Cast<AStaticMeshActor>(Class->GetDefaultObject(false)) : nullptr;
    auto* Life = CastField<FFloatProperty>(AActor::StaticClass()->FindPropertyByName(TEXT("InitialLifeSpan")));
    if (!TestTrue(TEXT("Compiler created genuine class, CDO, and inherited native field"), Class && Default && Life))
        return false;
    Default->InitialLifeSpan = 13.25f;
    UPackage* MeshOwner = P4RemainingPackage(TEXT("BlueprintMesh"));
    TStrongObjectPtr<UStaticMesh> Mesh(NewObject<UStaticMesh>(MeshOwner, NAME_None, RF_Public));
    UStaticMeshComponent* Component = Default->GetStaticMeshComponent();
    if (!TestNotNull(TEXT("Original native parent component"), Component))
        return false;
    Component->SetStaticMesh(Mesh.Get());
    Owner->SetDirtyFlag(false);
    MeshOwner->SetDirtyFlag(false);
    {
        FP4RemainingGuard DefaultState(Default), ComponentState(Component), MeshState(Mesh.Get());
        TestTrue(
            TEXT("Genuine Blueprint default-cache refresh succeeds"), UP4AuthoringTools::RefreshActorDefaults(Default));
        FStructOnScope CacheDestination(Class);
        void* CachedLife = Life->ContainerPtrToValuePtr<void>(CacheDestination.GetStructMemory());
        Life->SetFloatingPointPropertyValue(CachedLife, -7.5);
        Class->InitPropertiesFromCustomList(
            CacheDestination.GetStructMemory(), static_cast<const uint8*>(static_cast<const void*>(Default)));
        TestEqual(TEXT("Public engine cache consumer copies exact authored inherited field"),
            Life->GetFloatingPointPropertyValue(CachedLife), 13.25);
        TestTrue(TEXT("Generated class and existing CDO identity retained"),
            Blueprint->GeneratedClass == Class && Class->GetDefaultObject(false) == Default);
        TestEqual(TEXT("Authored native default retained"), Default->InitialLifeSpan, 13.25f);
        const auto Dependencies = UP4AuthoringTools::NativeAssetDependencies(Blueprint.Get());
        TestTrue(TEXT("Blueprint dependencies include actual native CDO mesh package"),
            Dependencies.Contains(MeshOwner->GetName()));
        TestFalse(TEXT("Blueprint own package is excluded"), Dependencies.Contains(Owner->GetName()));
        TestTrue(
            TEXT("Dependency query leaves existing CDO identity retained"), Class->GetDefaultObject(false) == Default);
        DefaultState.Check(*this);
        ComponentState.Check(*this);
        MeshState.Check(*this);
    }
    FActorSpawnParameters Spawn;
    Spawn.Name = MakeUniqueObjectName(World->PersistentLevel, Class, TEXT("P4BlueprintCacheDraft"));
    AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Class, FTransform::Identity, Spawn);
    if (!TestNotNull(TEXT("Real spawned compiler-produced actor"), Actor))
        return false;
    ON_SCOPE_EXIT
    {
        World->DestroyActor(Actor);
    };
    TestTrue(TEXT("Spawn uses exact generated class"), Actor->GetClass() == Class);
    TestEqual(TEXT("Real native spawn inherits authored cached float"), Actor->InitialLifeSpan, 13.25f);
    TestTrue(TEXT("Real native spawn inherits exact mesh pointer"),
        Actor->GetStaticMeshComponent()->GetStaticMesh() == Mesh.Get());
    TStrongObjectPtr<UBlueprint> Uncompiled(NewObject<UBlueprint>(Owner));
    if (!TestNull(
            TEXT("Plain native uncompiled Blueprint naturally has no generated class"), Uncompiled->GeneratedClass))
        return false;
    {
        FP4RemainingGuard State(Uncompiled.Get());
        UP4AuthoringTools::NativeAssetDependencies(Uncompiled.Get());
        TestNull(TEXT("Read-only dependencies did not invent a generated class"), Uncompiled->GeneratedClass);
        State.Check(*this);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PropertyActorDependencyScopeDraft,
    "P4MapKit.Quality.PropertyActorDependencyScopeDraft",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PropertyActorDependencyScopeDraft::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4PropertyRemainingDraft;
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!TestNotNull(TEXT("Existing isolated editor world"), World))
        return false;
    const bool PriorWorldDirty = World->GetOutermost()->IsDirty();
    ON_SCOPE_EXIT
    {
        World->GetOutermost()->SetDirtyFlag(PriorWorldDirty);
    };
    ATargetPoint* Actor = World->SpawnActor<ATargetPoint>();
    ATargetPoint* OtherActor = World->SpawnActor<ATargetPoint>();
    ON_SCOPE_EXIT
    {
        if (OtherActor)
            World->DestroyActor(OtherActor);
        if (Actor)
            World->DestroyActor(Actor);
    };
    if (!TestTrue(TEXT("Two real same-world actors"),
            Actor && OtherActor && Actor->GetOutermost() == OtherActor->GetOutermost()))
        return false;
    TStrongObjectPtr<UStaticMesh> Mesh(
        NewObject<UStaticMesh>(P4RemainingPackage(TEXT("OwnMesh")), NAME_None, RF_Public));
    TStrongObjectPtr<UStaticMesh> OtherMesh(
        NewObject<UStaticMesh>(P4RemainingPackage(TEXT("OtherMesh")), NAME_None, RF_Public));
    TStrongObjectPtr<UStaticMesh> EditorMesh(
        NewObject<UStaticMesh>(P4RemainingPackage(TEXT("EditorMesh")), NAME_None, RF_Public));
    TStrongObjectPtr<UStaticMesh> ChildTransientMesh(
        NewObject<UStaticMesh>(P4RemainingPackage(TEXT("ChildTransientMesh")), NAME_None, RF_Public));
    TStrongObjectPtr<UStaticMesh> AssetTransientMesh(
        NewObject<UStaticMesh>(P4RemainingPackage(TEXT("AssetTransientMesh")), NAME_None, RF_Transient));
    const auto Add = [](AActor* Owner, UStaticMesh* Source, EObjectFlags Flags, bool EditorOnly)
    {
        auto* Component = NewObject<UStaticMeshComponent>(Owner, NAME_None, Flags);
        Component->bIsEditorOnly = EditorOnly;
        Component->SetStaticMesh(Source);
        Owner->AddInstanceComponent(Component);
        return Component;
    };
    UStaticMeshComponent* Own = Add(Actor, Mesh.Get(), RF_Transactional, false);
    UStaticMeshComponent* Other = Add(OtherActor, OtherMesh.Get(), RF_Transactional, false);
    UStaticMeshComponent* Editor = Add(Actor, EditorMesh.Get(), RF_Transactional, true);
    UStaticMeshComponent* ChildTransient = Add(Actor, ChildTransientMesh.Get(), RF_Transient, false);
    UStaticMeshComponent* AssetTransient = Add(Actor, AssetTransientMesh.Get(), RF_Transactional, false);
    if (!TestTrue(TEXT("Exact native producer references, flags, and editor-only identity"),
            Own->GetStaticMesh() == Mesh.Get() && Other->GetStaticMesh() == OtherMesh.Get() && Editor->IsEditorOnly() &&
                ChildTransient->HasAnyFlags(RF_Transient) && AssetTransientMesh->HasAnyFlags(RF_Transient)))
        return false;
    // Components remain unregistered; no render data, collision, geometry, or cook.
    TestFalse(TEXT("Authored dependency fixture component unregistered"), Own->IsRegistered());
    TArray<UObject*> Objects = {Actor, OtherActor, Own, Other, Editor, ChildTransient, AssetTransient, Mesh.Get(),
        OtherMesh.Get(), EditorMesh.Get(), ChildTransientMesh.Get(), AssetTransientMesh.Get()};
    TArray<TUniquePtr<FP4RemainingGuard>> States;
    for (UObject* Object : Objects)
        States.Add(MakeUnique<FP4RemainingGuard>(Object));
    const auto Dependencies = UP4AuthoringTools::NativeAssetDependencies(Actor);
    TestTrue(
        TEXT("Actual actor component dependency is included"), Dependencies.Contains(Mesh->GetOutermost()->GetName()));
    TestFalse(
        TEXT("Unrelated same-world actor is excluded"), Dependencies.Contains(OtherMesh->GetOutermost()->GetName()));
    TestFalse(TEXT("Editor-only component dependency is excluded"),
        Dependencies.Contains(EditorMesh->GetOutermost()->GetName()));
    TestFalse(TEXT("Transient component dependency is excluded"),
        Dependencies.Contains(ChildTransientMesh->GetOutermost()->GetName()));
    TestFalse(TEXT("Transient asset reference is excluded"),
        Dependencies.Contains(AssetTransientMesh->GetOutermost()->GetName()));
    TestFalse(TEXT("World owner package is excluded"), Dependencies.Contains(World->GetOutermost()->GetName()));
    TestTrue(
        TEXT("Repeated query is deterministic"), UP4AuthoringTools::NativeAssetDependencies(Actor) == Dependencies);
    TArray<FString> Sorted = Dependencies;
    Sorted.Sort();
    TestTrue(TEXT("Packages are lexically sorted"), Sorted == Dependencies);
    for (int32 Index = 1; Index < Dependencies.Num(); ++Index)
        TestFalse(TEXT("Package list contains no duplicate adjacent identity"),
            Dependencies[Index] == Dependencies[Index - 1]);
    for (const auto& State : States)
        State->Check(*this);
    TestTrue(TEXT("Component references retained independently"),
        Own->GetStaticMesh() == Mesh.Get() && Other->GetStaticMesh() == OtherMesh.Get() &&
            Editor->GetStaticMesh() == EditorMesh.Get() &&
            ChildTransient->GetStaticMesh() == ChildTransientMesh.Get() &&
            AssetTransient->GetStaticMesh() == AssetTransientMesh.Get());
    return !HasAnyErrors();
}
#endif

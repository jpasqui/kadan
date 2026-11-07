#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "P4ManualEngineClassRegistration.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "UObject/StrongObjectPtr.h"

// Native constructor/CDO/four-bool control in the existing Engine script package. Test module only.
// Never fakes GIsInitialLoad or changes package flags. No disk writes or assets.
struct FP4ManualEngineClassRegistrationControlResult
{
    bool Success = false, InitialLoad = false, NotificationsRequired = false;
    bool TokenStreamAssembled = false, GarbageCollected = false;
    FString Error;
};

namespace
{
const TCHAR* Names[] = {
    TEXT("P4RegistrationBool0"), TEXT("P4RegistrationBool1"), TEXT("P4RegistrationBool2"), TEXT("P4RegistrationBool3")};
bool BootNotified = false, RuntimeNotified = false;
UClass* MakeControlClass(bool Boot);
UObject* RegisterBootControl()
{
    return MakeControlClass(true);
}
UObject* RegisterRuntimeControl()
{
    return MakeControlClass(false);
}

UClass* MakeControlClass(bool Boot)
{
    UPackage* Package = FindPackage(nullptr, TEXT("/Script/Engine"));
    if (!Package)
        return nullptr;
    const FName Name(
        Boot ? TEXT("P4ManualEngineRegistrationBootControl") : TEXT("P4ManualEngineRegistrationRuntimeControl"));
    if (UClass* Existing = FindObjectFast<UClass>(Package, Name))
        return Existing;
    UClass* Parent = UObject::StaticClass();
    if (!Parent || !Parent->ClassConstructor || !Parent->GetDefaultObject())
        return nullptr;
    UClass* Class = NewObject<UClass>(Package, Name, RF_Public);
    Class->SetSuperStruct(Parent);
    Class->ClassWithin = Parent->ClassWithin;
    Class->ClassConfigName = Parent->ClassConfigName;
    Class->ClassFlags = EClassFlags((Parent->ClassFlags & CLASS_Inherit) & ~CLASS_Abstract);
    Class->ClassCastFlags = Parent->ClassCastFlags;
    Class->ClassConstructor = Parent->ClassConstructor;
    Class->ClassVTableHelperCtorCaller = Parent->ClassVTableHelperCtorCaller;
    Class->ClassAddReferencedObjects = Parent->ClassAddReferencedObjects;
    for (int32 I = 3; I >= 0; --I)
    {
        auto* Field = new FBoolProperty(Class, Names[I], RF_Public);
        Field->SetBoolSize(sizeof(bool), true);
        Field->SetPropertyFlags(CPF_NativeAccessSpecifierPublic);
        Class->AddCppProperty(Field);
    }
    FP4ManualEngineClassRegistration Registration(Class, Boot ? &RegisterBootControl : &RegisterRuntimeControl);
    if (!Registration.IsValid())
        return nullptr;
    Class->Bind();
    Class->StaticLink(true);
    for (const TCHAR* FieldName : Names)
    {
        const auto* Field = FindFProperty<FBoolProperty>(Class, FieldName);
        if (!Field || !Field->IsNativeBool() || Field->GetOwnerStruct() != Class ||
            Field->GetOffset_ForInternal() < Parent->GetPropertiesSize() ||
            Field->GetOffset_ForInternal() + Field->GetSize() > Class->GetPropertiesSize())
            return nullptr;
    }
    if (!Registration.FinishClass() || !Registration.FinishClass() || !Registration.IsClassFinished())
        return nullptr;
    (Boot ? BootNotified : RuntimeNotified) = Registration.NotificationsRequired();
    // Keep the two private control identities stable through registration and
    // repeated tests. They are never saved, cooked or assigned to game assets.
    Class->AddToRoot();
    UObject* CDO = Class->GetDefaultObject();
    for (int32 I = 0; I < 4; ++I)
        FindFProperty<FBoolProperty>(Class, Names[I])->SetPropertyValue_InContainer(CDO, (I % 2) == 0);
    return Class;
}
} //namespace

FP4ManualEngineClassRegistrationControlResult P4RunManualEngineClassRegistrationControl(
    bool RequireInitialLoad, bool CollectAfterConstruction = false)
{
    FP4ManualEngineClassRegistrationControlResult Result;
    Result.InitialLoad = GIsInitialLoad;
    const auto Fail = [&](const TCHAR* Error)
    {
        Result.Error = Error;
        return Result;
    };
    if (!IsInGameThread() || Result.InitialLoad != RequireInitialLoad)
        return Fail(TEXT("control-needs-actual-requested-initial-load-phase"));
    if (RequireInitialLoad && CollectAfterConstruction)
        return Fail(TEXT("control-must-not-collect-during-initial-load"));
    UPackage* Package = FindPackage(nullptr, TEXT("/Script/Engine"));
    if (!Package || !Package->HasAnyPackageFlags(PKG_CompiledIn) || Package->HasAnyPackageFlags(PKG_RuntimeGenerated))
        return Fail(TEXT("control-needs-existing-compiled-engine-package"));
    const uint32 PackageFlags = Package->GetPackageFlags();
    UClass* Class = MakeControlClass(RequireInitialLoad);
    UClass* Parent = UObject::StaticClass();
    if (!Class || Class->GetSuperClass() != Parent || Class->ClassConstructor != Parent->ClassConstructor ||
        Class->ClassVTableHelperCtorCaller != Parent->ClassVTableHelperCtorCaller ||
        Class->ClassAddReferencedObjects != Parent->ClassAddReferencedObjects)
        return Fail(TEXT("control-native-construction-contract-differs"));
    Result.NotificationsRequired = RequireInitialLoad ? BootNotified : RuntimeNotified;
    if (Result.NotificationsRequired != RequireInitialLoad)
        return Fail(TEXT("control-notification-predicate-differs"));
    Result.TokenStreamAssembled = Class->HasAnyClassFlags(CLASS_TokenStreamAssembled);
    if (!Result.TokenStreamAssembled)
        return Fail(TEXT("control-native-reference-token-stream-not-assembled"));
    int32 Count = 0;
    for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
        ++Count;
    if (Count != 4)
        return Fail(TEXT("control-own-field-count-differs"));
    UObject* CDO = Class->GetDefaultObject();
    if (!CDO || CDO != Class->GetDefaultObject() || CDO->GetClass() != Class ||
        !CDO->HasAllFlags(RF_ClassDefaultObject | RF_ArchetypeObject) ||
        CDO->GetFName() != Class->GetDefaultObjectName())
        return Fail(TEXT("control-native-cdo-identity-differs"));
    // Reusing the factory never emits Added twice or replaces the native CDO.
    if (MakeControlClass(RequireInitialLoad) != Class || Class->GetDefaultObject() != CDO)
        return Fail(TEXT("control-repeated-factory-replaced-class-or-cdo"));
    UObject* Instance = NewObject<UObject>(GetTransientPackage(), Class, NAME_None, RF_Transient);
    if (!Instance || Instance == CDO || Instance->GetClass() != Class || Instance->HasAnyFlags(RF_ClassDefaultObject))
        return Fail(TEXT("control-native-instance-construction-failed"));
    TStrongObjectPtr<UObject> KeptInstance(Instance);
    if (CollectAfterConstruction)
    {
        TWeakObjectPtr<UObject> Identity(Instance);
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        Result.GarbageCollected = true;
        if (!Identity.IsValid() || Identity.Get() != KeptInstance.Get() || Class->GetDefaultObject(false) != CDO ||
            !Class->HasAnyClassFlags(CLASS_TokenStreamAssembled))
            return Fail(TEXT("control-native-instance-or-cdo-did-not-survive-gc"));
    }
    for (int32 I = 0; I < 4; ++I)
    {
        const auto* Field = FindFProperty<FBoolProperty>(Class, Names[I]);
        if (!Field || !Field->IsNativeBool() || Field->GetOwnerStruct() != Class ||
            Field->GetOffset_ForInternal() < Parent->GetPropertiesSize() ||
            Field->GetOffset_ForInternal() + Field->GetSize() > Class->GetPropertiesSize() ||
            Field->GetPropertyValue_InContainer(CDO) != ((I % 2) == 0) ||
            Field->GetPropertyValue_InContainer(Instance) != ((I % 2) == 0))
        {
            Instance->MarkPendingKill();
            return Fail(TEXT("control-native-bool-storage-or-defaults-differ"));
        }
    }
    Instance->MarkPendingKill();
    if (Package->GetPackageFlags() != PackageFlags || GIsInitialLoad != Result.InitialLoad)
        return Fail(TEXT("control-mutated-package-or-initial-load-state"));
    Result.Success = true;
    return Result;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ManualEngineClassRegistrationTest,
    "P4MapKit.EngineDeclarations.ManualRegistrationLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ManualEngineClassRegistrationTest::RunTest(const FString&)
{
    const auto First = P4RunManualEngineClassRegistrationControl(false, true);
    TestTrue(*FString::Printf(TEXT("Native post-initial-load construction succeeds: %s"), *First.Error), First.Success);
    TestFalse(TEXT("Ordinary native registration does not fake initial load"), First.InitialLoad);
    TestFalse(TEXT("Ordinary registration sends no initial-load notifications"), First.NotificationsRequired);
    TestTrue(TEXT("Native reference stream exists before parallel GC can visit the class"), First.TokenStreamAssembled);
    TestTrue(TEXT("Actual held instance and native CDO survive explicit collection"), First.GarbageCollected);
    const auto Again = P4RunManualEngineClassRegistrationControl(false);
    TestTrue(
        *FString::Printf(TEXT("Repeated factory retains class/CDO/native defaults: %s"), *Again.Error), Again.Success);
    return !HasAnyErrors();
}
#endif

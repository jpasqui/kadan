#include "P4RecoveredPostProcessStorage.h"
#include "P4LevelImportWorld.h"
#include "Components/PostProcessComponent.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Serialization/ObjectReader.h"
#include "Serialization/ObjectWriter.h"
#include "Serialization/StructuredArchive.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/StructOnScope.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
namespace
{
class FPostProcessReader : public FObjectReader
{
public:
    explicit FPostProcessReader(TArray<uint8>& Bytes) : FObjectReader(Bytes)
    {
        ArIgnoreClassRef = true;
        ArIgnoreArchetypeRef = true;
    }
};
class FPostProcessWriter : public FObjectWriter
{
public:
    explicit FPostProcessWriter(TArray<uint8>& Bytes) : FObjectWriter(Bytes)
    {
        ArIgnoreClassRef = true;
        ArIgnoreArchetypeRef = true;
        ArNoDelta = true;
    }
};
} //namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4RecoveredPostProcessStorageLifecycle,
    "P4MapKit.EngineFields.RecoveredPostProcessStorageLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4RecoveredPostProcessStorageLifecycle::RunTest(const FString&)
{
    UClass* Class = LoadClass<UPostProcessComponent>(
        nullptr, TEXT("/Script/P4StubsCore.P4RecoveredPostProcessComponent"));
    if (!TestNotNull(TEXT("Requires actual rebuilt UHT PP SDK"), Class))
        return false;
    FP4RecoveredPostProcessStorage Storage;
    FString Error;
    if (!TestTrue(TEXT("Full class/struct/type/semantic flag contract qualifies"),
            P4GetRecoveredPostProcessStorage(Class, Storage, Error)))
    {
        AddError(Error);
        return false;
    }
    const UObject* CDO = Class->GetDefaultObject();
    if (!TestNotNull(TEXT("Ordinary engine CDO initialization completes before the GC precondition"), CDO))
        return false;
    TestTrue(TEXT("Native GC token stream is assembled after ordinary CDO initialization"),
        Class->HasAnyClassFlags(CLASS_TokenStreamAssembled));
    TestTrue(TEXT("Extension has genuine C++ init/copy/destroy operations"),
        Storage.ForkSettings->Struct->GetCppStructOps() != nullptr);
    TestNull(TEXT("Base engine class is not extended in place"),
        UPostProcessComponent::StaticClass()->FindPropertyByName(TEXT("ForkSettings")));
    TStrongObjectPtr<UPostProcessComponent> Instance(NewObject<UPostProcessComponent>(GetTransientPackage(), Class));
    const FPostProcessVolumeProperties Initial = Instance->GetProperties();
    TestTrue(TEXT("Inherited interface returns the exact stock Settings member, never ForkSettings"),
        Initial.Settings == &Instance->Settings &&
            Initial.Settings == Storage.StockSettings->ContainerPtrToValuePtr<FPostProcessSettings>(Instance.Get()));
    TestTrue(TEXT("Inherited interface retains stock initial priority/radius/weight/enabled semantics"),
        Initial.Priority == Instance->Priority && Initial.BlendRadius == Instance->BlendRadius &&
            Initial.BlendWeight == Instance->BlendWeight && Initial.bIsEnabled == (Instance->bEnabled != 0));
    TestTrue(TEXT("Without a shape attachment stock interface remains unbound"),
        Initial.bIsUnbound && Instance->GetAttachParent() == nullptr);
    TArray<const FProperty*> Members;
    for (TFieldIterator<FProperty> It(Storage.ForkSettings->Struct); It; ++It)
    {
        Members.Add(*It);
        const int32 Begin = It->GetOffset_ForInternal(), End = Begin + It->GetSize();
        TestTrue(TEXT("Every extension member lies in actual UHT struct allocation"),
            Begin >= 0 && End <= Storage.ForkSettings->Struct->GetPropertiesSize() &&
                End <= Storage.ForkSettings->Struct->GetStructureSize());
        TestTrue(TEXT("Native construction receives exact CDO struct defaults"),
            It->Identical_InContainer(Storage.ForkSettings->ContainerPtrToValuePtr<void>(CDO),
                Storage.ForkSettings->ContainerPtrToValuePtr<void>(Instance.Get())));
        FP4RecoveredPostProcessMember Member;
        if (!TestTrue(TEXT("Each native field resolves as original logical Settings"),
                P4ResolveRecoveredPostProcessMember(Storage, It->GetFName(), Member)))
            return false;
        const void* Data = P4RecoveredPostProcessMemberData(Instance.Get(), Storage, Member);
        const FString Name = It->GetName();
        if (const FBoolProperty* Bit = CastField<FBoolProperty>(*It))
            TestTrue(TEXT("All eleven captured packed defaults are false"),
                !Bit->IsNativeBool() && !Bit->GetPropertyValue(Data));
        else if (const FFloatProperty* Float = CastField<FFloatProperty>(*It))
            TestEqual(TEXT("Captured indirect-light float defaults are one"), Float->GetPropertyValue(Data), 1.f);
        else if (const FObjectPropertyBase* Object = CastField<FObjectPropertyBase>(*It))
            TestNull(TEXT("Captured mask default is null"), Object->GetObjectPropertyValue(Data));
        else if (const FByteProperty* Byte = CastField<FByteProperty>(*It))
            TestEqual(
                TEXT("Captured default original enum meaning is DefaultDOF"), int32(Byte->GetPropertyValue(Data)), 0);
        else if (const FStructProperty* Struct = CastField<FStructProperty>(*It))
        {
            if (Struct->Struct->GetFName() == TEXT("LinearColor"))
                TestTrue(TEXT("Captured color default is all ones"),
                    *static_cast<const FLinearColor*>(Data) == FLinearColor(1, 1, 1, 1));
            else if (Struct->Struct->GetFName() == TEXT("Vector2D"))
                TestTrue(
                    TEXT("Captured tilt default is zero"), *static_cast<const FVector2D*>(Data) == FVector2D(0, 0));
            else
            {
                const FVector4* Vector = static_cast<const FVector4*>(Data);
                const bool One = Name == TEXT("C4DepthOfFieldParam1") || Name == TEXT("C4DepthOfFieldParam2");
                TestTrue(TEXT("Captured four-channel defaults retain every component"),
                    Vector->X == (One ? 1.f : 0.f) && Vector->Y == (One ? 1.f : 0.f) && Vector->Z == 0.f &&
                        Vector->W == 0.f);
            }
        }
    }
    TestEqual(TEXT("All21 missing source fields plus distinct original enum exist"), Members.Num(), 22);
    FStructOnScope PackedControl(Storage.ForkSettings->Struct);
    for (int32 A = 0; A < Members.Num(); ++A)
        for (int32 B = A + 1; B < Members.Num(); ++B)
        {
            const auto* X = Members[A];
            const auto* Y = Members[B];
            const int32 XB = X->GetOffset_ForInternal(), YB = Y->GetOffset_ForInternal();
            const bool Overlap = XB < YB + Y->GetSize() && YB < XB + X->GetSize();
            const auto* BX = CastField<FBoolProperty>(X);
            const auto* BY = CastField<FBoolProperty>(Y);
            TestTrue(TEXT("Member spans never overlap except real UHT packed bool storage"), !Overlap || (BX && BY));
            if (Overlap && BX && BY)
            {
                void* Data = PackedControl.GetStructMemory();
                BX->SetPropertyValue_InContainer(Data, false);
                BY->SetPropertyValue_InContainer(Data, false);
                BX->SetPropertyValue_InContainer(Data, true);
                TestFalse(TEXT("Setting one packed bit never changes its overlapping neighbor"),
                    BY->GetPropertyValue_InContainer(Data));
                BY->SetPropertyValue_InContainer(Data, true);
                TestTrue(
                    TEXT("Setting the neighbor never clears the first bit"), BX->GetPropertyValue_InContainer(Data));
                BX->SetPropertyValue_InContainer(Data, false);
                BY->SetPropertyValue_InContainer(Data, false);
            }
        }
    const int32 Begin = Storage.ForkSettings->GetOffset_ForInternal(), End = Begin + Storage.ForkSettings->GetSize();
    TestTrue(TEXT("Extension lies in actual child class allocation"),
        Begin >= 0 && End <= Class->GetPropertiesSize() && End <= Class->GetStructureSize());
    for (TFieldIterator<FProperty> It(Class->GetSuperClass()); It; ++It)
        TestTrue(TEXT("Extension never overlaps inherited reflected storage"),
            End <= It->GetOffset_ForInternal() || Begin >= It->GetOffset_ForInternal() + It->GetSize());
    FP4RecoveredPostProcessMember Mask, Enum;
    if (!P4ResolveRecoveredPostProcessMember(Storage, TEXT("C4DepthOfFieldMask"), Mask) ||
        !P4ResolveRecoveredPostProcessMember(Storage, TEXT("DepthOfFieldMethod"), Enum))
        return false;
    TStrongObjectPtr<UTexture2D> Texture(NewObject<UTexture2D>());
    TWeakObjectPtr<UTexture2D> WeakTexture = Texture.Get();
    CastFieldChecked<FObjectPropertyBase>(Mask.Property)
        ->SetObjectPropertyValue(P4RecoveredPostProcessMemberData(Instance.Get(), Storage, Mask), Texture.Get());
    CastFieldChecked<FByteProperty>(Enum.Property)
        ->SetPropertyValue(P4RecoveredPostProcessMemberData(Instance.Get(), Storage, Enum), 1);
    Instance->Priority = 7.f;
    Instance->BlendRadius = 13.f;
    Instance->BlendWeight = .25f;
    Instance->bEnabled = false;
    Instance->bUnbound = true;
    Instance->Settings.BloomIntensity = .5f;
    const FPostProcessVolumeProperties Changed = Instance->GetProperties();
    TestTrue(TEXT("Stock interface observes changed real common settings and volume controls"),
        Changed.Settings == &Instance->Settings && Changed.Settings->BloomIntensity == .5f && Changed.Priority == 7.f &&
            Changed.BlendRadius == 13.f && Changed.BlendWeight == .25f && !Changed.bIsEnabled && Changed.bIsUnbound);
    Texture.Reset();
    CollectGarbage(RF_NoFlags);
    if (!TestTrue(TEXT("UHT reference traversal keeps texture reachable only through genuine fork struct"),
            WeakTexture.IsValid()))
        return false;
    {
        TStrongObjectPtr<UPostProcessComponent> Classic(NewObject<UPostProcessComponent>(GetTransientPackage(), Class));
        TArray<uint8> Bytes;
        FObjectWriter Writer(Instance.Get(), Bytes, true, true, false);
        FObjectReader Reader(Classic.Get(), Bytes, true, true);
        TestFalse(TEXT("Classic native serializer readback has no archive error"), Reader.IsError());
        TestTrue(TEXT("Classic native archive preserves actual extension values/references"),
            Storage.ForkSettings->Identical_InContainer(Instance.Get(), Classic.Get()));
        TStrongObjectPtr<UPostProcessComponent> StructuredCopy(
            NewObject<UPostProcessComponent>(GetTransientPackage(), Class));
        TArray<uint8> StructuredBytes;
        {
            FPostProcessWriter W(StructuredBytes);
            FStructuredArchiveFromArchive Archive(W);
            static_cast<UObject*>(Instance.Get())->Serialize(Archive.GetSlot().EnterRecord());
        }
        {
            FPostProcessReader R(StructuredBytes);
            FStructuredArchiveFromArchive Archive(R);
            static_cast<UObject*>(StructuredCopy.Get())->Serialize(Archive.GetSlot().EnterRecord());
            TestFalse(TEXT("Structured child adapter readback has no archive error"), R.IsError());
        }
        TestTrue(TEXT("Own generated structured adapter preserves actual extension values/references"),
            Storage.ForkSettings->Identical_InContainer(Instance.Get(), StructuredCopy.Get()));
    }
    {
        TStrongObjectPtr<UWorld> World(P4CreateOriginalImportWorld(
            MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("PostProcessLifecycle")),
            GetTransientPackage()));
        if (!TestNotNull(TEXT("Owned inactive world initializes ordinary registration"), World.Get()))
            return false;
        TStrongObjectPtr<UPostProcessComponent> Registered(NewObject<UPostProcessComponent>(World.Get(), Class));
        ON_SCOPE_EXIT
        {
            if (Registered->IsRegistered())
                Registered->UnregisterComponent();
            World->DestroyWorld(false);
            World->MarkPendingKill();
        };
        for (int32 Pass = 0; Pass < 4; ++Pass)
        {
            // RegisterComponent() requires an actor owner; this deliberately
            // world-outer component uses the public explicit-world contract.
            Registered->RegisterComponentWithWorld(World.Get());
            int32 Count = 0;
            for (auto* Volume : World->PostProcessVolumes)
                if (Volume == Registered.Get())
                    ++Count;
            TestTrue(TEXT("Real stock lifecycle inserts exactly one actual post-process volume"),
                Registered->IsRegistered() && Count == 1);
            Registered->UnregisterComponent();
            TestTrue(TEXT("Real stock lifecycle removes the actual post-process volume"),
                !Registered->IsRegistered() && !World->PostProcessVolumes.Contains(Registered.Get()));
        }
    }
    Instance.Reset();
    CollectGarbage(RF_NoFlags);
    TestFalse(TEXT("Native instance/struct destruction releases the only texture reference"), WeakTexture.IsValid());
    return !HasAnyErrors();
}
#endif
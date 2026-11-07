#if WITH_DEV_AUTOMATION_TESTS
#include "P4DiagnosticCookPackageGuard.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace
{
TArray<uint8> MaterialCookSnapshot(UObject* Object)
{
    TArray<uint8> Bytes;
    FObjectWriter Writer(Object, Bytes, false, false, false, PPF_DuplicateVerbatim);
    return Bytes;
}
} //namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4DiagnosticMaterialCookPreservation,
    "P4MapKit.Kadan.DiagnosticMaterialCookPreservation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4DiagnosticMaterialCookPreservation::RunTest(const FString&)
{
    const FString Name = TEXT("/Game/P4MapKitDiagnosticMaterialCook/OnlyFixture");
    const FString ForeignName = Name + TEXT("Foreign");
    if (!TestTrue(TEXT("Unsaved guard fixtures have no existing packages or files"),
            !FindPackage(nullptr, *Name) && !FindPackage(nullptr, *ForeignName) &&
                !FPackageName::DoesPackageExist(Name) && !FPackageName::DoesPackageExist(ForeignName)))
        return false;
    TStrongObjectPtr<UPackage> Package(CreatePackage(*Name)), ForeignPackage(CreatePackage(*ForeignName));
    TStrongObjectPtr<UMaterial> Material(NewObject<UMaterial>(Package.Get(), TEXT("OnlyMaterial"), RF_Transient));
    TStrongObjectPtr<UMaterialInstanceConstant> Instance(
        NewObject<UMaterialInstanceConstant>(Package.Get(), TEXT("OnlyInstance"), RF_Transient));
    TStrongObjectPtr<UMaterial> Foreign(
        NewObject<UMaterial>(ForeignPackage.Get(), TEXT("ForeignMaterial"), RF_Transient));
    ON_SCOPE_EXIT
    {
        Material->MarkPendingKill();
        Instance->MarkPendingKill();
        Foreign->MarkPendingKill();
        Package->SetDirtyFlag(false);
        ForeignPackage->SetDirtyFlag(false);
        TestFalse(TEXT("The diagnostic never publishes its own package"), FPackageName::DoesPackageExist(Name));
        TestFalse(
            TEXT("The diagnostic never publishes a foreign package"), FPackageName::DoesPackageExist(ForeignName));
    };
    FBoolProperty* Sorted = FindFProperty<FBoolProperty>(
        UMaterialInterface::StaticClass(), TEXT("bTextureStreamingDataSorted"));
    FArrayProperty* Array = FindFProperty<FArrayProperty>(
        UMaterialInterface::StaticClass(), TEXT("TextureStreamingData"));
    FStructProperty* Element = Array ? CastField<FStructProperty>(Array->Inner) : nullptr;
    FNameProperty* TextureName = Element ? FindFProperty<FNameProperty>(Element->Struct, TEXT("TextureName")) : nullptr;
    if (!TestTrue(TEXT("Actual native streaming state is a typed transient flag and texture-name array"),
            Sorted && Sorted->HasAllPropertyFlags(CPF_Transient) && Element && TextureName))
        return false;
    auto& Values = Material->GetTextureStreamingData();
    Values.SetNum(3);
    for (int32 Index = 0; Index < Values.Num(); ++Index)
        TextureName->SetPropertyValue_InContainer(
            &Values[Index], FName(*FString::Printf(TEXT("SourceName_%d"), 2 - Index)));
    auto& Other = Instance->GetTextureStreamingData();
    Other.SetNum(2);
    TextureName->SetPropertyValue_InContainer(&Other[0], FName(TEXT("InstanceSourceZ")));
    TextureName->SetPropertyValue_InContainer(&Other[1], FName(TEXT("InstanceSourceA")));
    Sorted->SetPropertyValue_InContainer(Material.Get(), false);
    Sorted->SetPropertyValue_InContainer(Instance.Get(), true);
    const auto Before = MaterialCookSnapshot(Material.Get()), InstanceBefore = MaterialCookSnapshot(Instance.Get());
    const EObjectFlags ObjectFlags = Material->GetFlags(), InstanceFlags = Instance->GetFlags();
    const uint32 PackageFlags = Package->GetPackageFlags();
    const bool Dirty = Package->IsDirty();
    ITargetPlatform* Target = GetTargetPlatformManagerRef().FindTargetPlatform(TEXT("WindowsNoEditor"));
    if (!TestNotNull(TEXT("The same actual cooked target is available"), Target))
        return false;
    {
        FP4DiagnosticCookPackageGuard Guard(Package.Get());
        if (!TestTrue(TEXT("Native material streaming schema is admitted"), Guard.IsReady()))
            return false;
        // Invoke the proven stock mutation itself, then exercise independent
        // name/order/removal changes as a partially completed cook could leave.
        // Synthetic names are not renderer texture indices: the real PreSave
        // receives the same valid empty list as the actual Shadow regression.
        Values.Reset();
        Material->UMaterialInterface::PreSave(Target);
        TestTrue(TEXT("The actual stock cooked PreSave sets its transient sorted flag"),
            Sorted->GetPropertyValue_InContainer(Material.Get()));
        Values.SetNum(2);
        TextureName->SetPropertyValue_InContainer(&Values[0], FName(TEXT("ChangedDuringCook")));
        Values.Swap(0, 1);
        Other.Reset();
        Sorted->SetPropertyValue_InContainer(Instance.Get(), false);
        Foreign->GetTextureStreamingData().SetNum(1);
        Sorted->SetPropertyValue_InContainer(Foreign.Get(), true);
        Package->SetPackageFlags(PKG_FilterEditorOnly);
        Package->SetDirtyFlag(!Dirty);
        FString Error;
        TestTrue(TEXT("Explicit async completion restores the known source state"), Guard.AwaitAndRestore(Error));
        if (!Error.IsEmpty())
            AddError(Error);
        TestTrue(TEXT("A false sorted flag and all nonempty array names/order/count restore exactly"),
            MaterialCookSnapshot(Material.Get()) == Before);
        TestTrue(TEXT("A true flag and removed instance entries restore exactly"),
            MaterialCookSnapshot(Instance.Get()) == InstanceBefore);
        TestTrue(TEXT("Only the owned cook package's materials are restored"),
            Foreign->GetTextureStreamingData().Num() == 1 && Sorted->GetPropertyValue_InContainer(Foreign.Get()));
    }
    {
        // An abandoned/failed diagnostic still completes the same restoration
        // through its destructor; it does not require a successful save result.
        FP4DiagnosticCookPackageGuard Guard(Package.Get());
        TestTrue(TEXT("The destructor-path guard is ready"), Guard.IsReady());
        Values.Reset();
        Other.SetNum(1);
        TextureName->SetPropertyValue_InContainer(&Other[0], FName(TEXT("AbortedCook")));
        Sorted->SetPropertyValue_InContainer(Material.Get(), true);
        Sorted->SetPropertyValue_InContainer(Instance.Get(), false);
    }
    TestTrue(TEXT("An aborted scope preserves the complete native material serialization"),
        MaterialCookSnapshot(Material.Get()) == Before);
    TestTrue(TEXT("An aborted scope also restores the second native material subclass"),
        MaterialCookSnapshot(Instance.Get()) == InstanceBefore);
    TestTrue(TEXT("Object flags and package metadata remain unchanged"),
        Material->GetFlags() == ObjectFlags && Instance->GetFlags() == InstanceFlags &&
            Package->GetPackageFlags() == PackageFlags && Package->IsDirty() == Dirty);
    FP4DiagnosticCookPackageGuard Invalid(nullptr);
    TestFalse(TEXT("No package cannot qualify a restoration scope"), Invalid.IsReady());
    return !HasAnyErrors();
}
#endif

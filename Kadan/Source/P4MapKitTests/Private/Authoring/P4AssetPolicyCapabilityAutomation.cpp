#include "P4KadanAssetPolicy.h"
#include "P4OriginalRecord.h"
#include "P4ExportDriver.h"
#include "Curves/CurveFloat.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4AssetPolicyCapabilityReasonsTest, "P4MapKit.Kadan.CapabilityPolicyReasons",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4AssetPolicyCapabilityReasonsTest::RunTest(const FString&)
{
    const FString Root = TEXT("/Game/__P4CapabilityPolicy_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    FP4ModDescriptor Mod;
    Mod.Id = TEXT("capability.audit");
    Mod.Name = TEXT("Capability audit");
    Mod.Version = TEXT("1.0.0");
    const auto MarkOriginal = [&](UObject* Asset, bool ReferenceOnly)
    {
        FP4OriginalRecord Record;
        Record.Package = Asset->GetOutermost()->GetName();
        Record.Class = Asset->GetClass()->GetPathName();
        Record.Kind = TEXT("policy-fixture");
        Record.SourceDigestKind = TEXT("fixture");
        Record.ComparisonSchema = TEXT("fixture");
        Record.SourceDigest = FString::ChrN(64, TEXT('a'));
        Record.ComparisonDigest = FString::ChrN(64, TEXT('b'));
        Record.ReferenceOnly = ReferenceOnly;
        FString Error;
        const bool Marked = P4MarkOriginalAsset(Asset, Record, Error);
        return TestTrue(*FString::Printf(TEXT("Policy fixture provenance is readable: %s"), *Error), Marked);
    };
    TStrongObjectPtr<UPackage> Package(CreatePackage(*(Root / TEXT("Curve"))));
    TStrongObjectPtr<UCurveFloat> Curve(NewObject<UCurveFloat>(Package.Get(), TEXT("Curve"), RF_Transient));
    TestTrue(TEXT("Creator content keeps its ordinary editor"), P4Kadan::GetAssetPolicy(Curve.Get()).AllowEdit);
    for (bool ReferenceOnly : {false, true})
    {
        if (!MarkOriginal(Curve.Get(), ReferenceOnly))
            return false;
        TArray<uint8> Before;
        {
            FObjectWriter Writer(Curve.Get(), Before, false, false, false);
        }
        TMap<FProperty*, EPropertyFlags> Flags;
        for (TFieldIterator<FProperty> It(Curve->GetClass()); It; ++It)
            Flags.Add(*It, It->GetPropertyFlags());
        const bool Dirty = Package->IsDirty();
        const uint32 PackageFlags = Package->GetPackageFlags();
        const auto Policy = P4Kadan::GetAssetPolicy(Curve.Get());
        TestTrue(
            TEXT("Recovered original provenance is distinguished from stand-ins"), Policy.Original && !Policy.StandIn);
        TestEqual(TEXT("Reference-only status comes from its actual record"), Policy.ReferenceOnly, ReferenceOnly);
        TestFalse(TEXT("Recovery alone does not admit an original-path edit"), Policy.AllowEdit);
        TestTrue(
            TEXT("The reason names the missing edited export path"), Policy.Reason.Contains(TEXT("Exporting edits")));
        TestFalse(TEXT("A recovered original is not described as unrecovered"),
            Policy.Reason.Contains(TEXT("not recovered")));
        TestFalse(TEXT("The exporter independently refuses the original curve"),
            P4ExportAssetAvailability(Curve.Get(), Mod).IsAvailable());
        TArray<uint8> After;
        {
            FObjectWriter Writer(Curve.Get(), After, false, false, false);
        }
        TestTrue(TEXT("Capability readback changes no native asset values"), Before == After);
        TestEqual(TEXT("Capability readback preserves dirty state"), Package->IsDirty(), Dirty);
        TestEqual(TEXT("Capability readback preserves package flags"), Package->GetPackageFlags(), PackageFlags);
        for (const auto& Field : Flags)
            TestTrue(TEXT("Original engine property flags remain exact"), Field.Key->GetPropertyFlags() == Field.Value);
    }
    TStrongObjectPtr<UPackage> WorldPackage(CreatePackage(*(Root / TEXT("World"))));
    TStrongObjectPtr<UWorld> World(NewObject<UWorld>(WorldPackage.Get(), TEXT("World"), RF_Transient));
    TStrongObjectPtr<UPackage> TablePackage(CreatePackage(*(Root / TEXT("Table"))));
    TStrongObjectPtr<UDataTable> Table(NewObject<UDataTable>(TablePackage.Get(), TEXT("Table"), RF_Transient));
    if (!MarkOriginal(World.Get(), false) || !MarkOriginal(Table.Get(), false))
        return false;
    TestTrue(TEXT("Original map overlay editing stays admitted"), P4Kadan::GetAssetPolicy(World.Get()).AllowEdit);
    TestTrue(TEXT("Supported original table editing stays admitted"), P4Kadan::GetAssetPolicy(Table.Get()).AllowEdit);
    TestTrue(TEXT("Map edits keep their dedicated export route"),
        P4ExportAssetAvailability(World.Get(), Mod).Route == EP4ExportAssetRoute::OriginalLevelChanges);
    TestTrue(TEXT("Table edits keep their dedicated export route"),
        P4ExportAssetAvailability(Table.Get(), Mod).Route == EP4ExportAssetRoute::OriginalTableFields);
    return !HasAnyErrors();
}
#endif
